"""Read-only Polymarket daily-temperature scanner: NWS forecast -> bucket probabilities -> edge
after fees -> quarter-Kelly size. It never places an order.

Verified live 2026-10-14 (through the Firecrawl connector; these hosts are blocked from the build machine):
  Polymarket  GET https://gamma-api.polymarket.com/events?slug=highest-temperature-in-nyc-on-october-16-2026
              one event, one market per bucket: groupItemTitle "63°F or below" / "64-65°F" /
              "82°F or higher", bestBid / bestAsk, feeSchedule {rate 0.05, exponent 1, takerOnly}.
              resolutionSource https://www.weather.gov/wrh/timeseries?site=klga : the market settles
              on the LaGuardia STATION (KLGA), whole °F, highest hourly "Temp" of the local day. Not
              Central Park. A forecast for the wrong spot is a different bet.
  NWS         /stations/KLGA/observations/latest -> geometry [lon, lat], temperature in degC
              /points/{lat},{lon} -> properties.forecastHourly, timeZone
              /gridpoints/OKX/37,46/forecast/hourly -> periods[].startTime, temperature (°F)

Model, as the open-source weather bots do it, with the parts they leave out made explicit:
  forecast max  = max of the hourly forecast over the local calendar day at that station
  error         = Normal(bias, sigma). Until a station has 30 settled days we use a PRIOR
                  (sigma 2.5°F under 36 h lead, 3.5°F to 60 h, 4.5°F beyond; bias 0). After that,
                  bias and sigma are the station's own forecast errors for that lead bucket:
                  "if the forecast runs 4°F hot at KLGA, learn it and subtract it".
  bucket P      = P(lo - 0.5 <= T < hi + 0.5): the reading is whole degrees
  edge          = P - ask - taker fee, fee = 0.05 x p x (1 - p)
  size          = quarter Kelly f = (P - c) / (1 - c) with c = ask + fee, capped at 5% of bankroll

Settling (to learn bias): NWS observations are degC from METAR (often whole degC), so the
derived daily max can differ from NOAA's whole-°F table by about 1°F. Settled values are
marked APPROX and the bias estimate carries that noise.
"""
from __future__ import annotations

import json
import math
import re
import statistics
import urllib.parse
import urllib.request
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Callable
from zoneinfo import ZoneInfo

GAMMA = "https://gamma-api.polymarket.com/events?slug="
NWS = "https://api.weather.gov"
UA = "tradebot-research (weather scanner)"
Get = Callable[[str], dict]
PRIOR = [(36, 2.5), (60, 3.5), (1e9, 4.5)]  # (lead hours up to, sigma °F)


def http_json(url: str) -> dict:
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "application/geo+json, application/json"})
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.loads(r.read())


def phi(x: float) -> float:
    return 0.5 * (1 + math.erf(x / math.sqrt(2)))


@dataclass
class Bucket:
    title: str
    lo: float  # -inf for "or below"
    hi: float  # +inf for "or higher"
    bid: float
    ask: float
    fee_rate: float
    tradable: bool
    slug: str


def parse_bucket(title: str) -> tuple[float, float]:
    t = title.replace("°", "").replace("º", "")
    if m := re.search(r"(-?\d+)\s*F?\s*or below", t, re.I):
        return -math.inf, float(m.group(1))
    if m := re.search(r"(-?\d+)\s*F?\s*or (higher|above)", t, re.I):
        return float(m.group(1)), math.inf
    if m := re.search(r"(-?\d+)\s*-\s*(-?\d+)", t):
        return float(m.group(1)), float(m.group(2))
    if m := re.search(r"^\s*(-?\d+)\s*F?\s*$", t):
        return float(m.group(1)), float(m.group(1))
    raise ValueError(f"can't read bucket {title!r}")


@dataclass
class Event:
    slug: str
    title: str
    station: str
    end: str
    unit: str
    buckets: list[Bucket]


def parse_event(js) -> Event:
    e = js[0] if isinstance(js, list) else js
    src = e.get("resolutionSource") or ""
    m = re.search(r"site=([A-Za-z0-9]{3,5})", src) or re.search(r"\b(K[A-Z]{3})\b", e.get("description", ""))
    if not m:
        raise ValueError("no weather station in the resolution source; not scanning a market whose station is unknown")
    unit = "C" if "°C" in json.dumps([x.get("groupItemTitle") for x in e.get("markets", [])], ensure_ascii=False) else "F"
    buckets = []
    for mk in e.get("markets", []):
        try:
            lo, hi = parse_bucket(mk.get("groupItemTitle") or mk.get("question", ""))
        except ValueError:
            continue
        fs = mk.get("feeSchedule") or {}
        bid, ask = float(mk.get("bestBid") or 0), float(mk.get("bestAsk") or 0)
        buckets.append(Bucket(mk.get("groupItemTitle", ""), lo, hi, bid, ask, float(fs.get("rate", 0)) if mk.get("feesEnabled") else 0.0,
                              bool(mk.get("acceptingOrders")) and not mk.get("closed") and 0 < ask < 1, mk.get("slug", "")))
    buckets.sort(key=lambda b: b.lo)
    return Event(e.get("slug", ""), e.get("title", ""), m.group(1).upper(), e.get("endDate", ""), unit, buckets)


def bucket_prob(b: Bucket, mu: float, sigma: float) -> float:
    lo = -math.inf if b.lo == -math.inf else b.lo - 0.5
    hi = math.inf if b.hi == math.inf else b.hi + 0.5
    return (1.0 if hi == math.inf else phi((hi - mu) / sigma)) - (0.0 if lo == -math.inf else phi((lo - mu) / sigma))


def target_date(title: str, end_iso: str) -> str:
    """'Highest temperature in NYC on October 16?' + endDate year -> 2026-10-16."""
    m = re.search(r"on ([A-Z][a-z]+) (\d{1,2})", title)
    year = (end_iso or "")[:4] or str(datetime.utcnow().year)
    if not m:
        return (end_iso or "")[:10]
    return datetime.strptime(f"{m.group(1)} {m.group(2)} {year}", "%B %d %Y").date().isoformat()


def forecast_max(station: str, day: str, get: Get = http_json) -> dict:
    st = get(f"{NWS}/stations/{station}/observations/latest")
    lon, lat = st["geometry"]["coordinates"]
    pt = get(f"{NWS}/points/{lat:.4f},{lon:.4f}")["properties"]
    tz = ZoneInfo(pt["timeZone"])
    fc = get(pt["forecastHourly"])["properties"]
    temps = [p["temperature"] if p.get("temperatureUnit", "F") == "F" else p["temperature"] * 9 / 5 + 32
             for p in fc["periods"] if datetime.fromisoformat(p["startTime"]).astimezone(tz).date().isoformat() == day]
    if not temps:
        raise ValueError(f"the hourly forecast does not cover {day} yet")
    issued = fc.get("updateTime") or fc.get("generatedAt") or ""
    day_start = datetime.fromisoformat(day + "T12:00:00").replace(tzinfo=tz)
    lead = (day_start - datetime.fromisoformat(issued)).total_seconds() / 3600 if issued else 24.0
    return {"station": station, "day": day, "max_f": max(temps), "hours": len(temps), "issued": issued, "lead_h": lead,
            "grid": pt["forecastHourly"], "partial_day": len(temps) < 20, "lat": lat, "lon": lon, "tz": pt["timeZone"]}


# ------------------------------------------------------------------ ensemble (Open-Meteo)
#
# Verified 2026-10-15 from a live response (captured in tests/fixtures/openmeteo_ensemble_klga.json):
#   GET https://ensemble-api.open-meteo.com/v1/ensemble?latitude=&longitude=&hourly=temperature_2m
#       &models=gfs025,ecmwf_ifs025&temperature_unit=fahrenheit&timezone=America/New_York&forecast_days=3
#   hourly.time local ISO hours; with two models the keys carry the model's own suffix, which is NOT
#   the requested id: temperature_2m_ncep_gefs025 (control) + temperature_2m_member01..30_ncep_gefs025,
#   temperature_2m_ecmwf_ifs025_ensemble + member01..50. 31 + 51 = 82 runs. The point snaps to the
#   0.25 degree grid (40.75, -74.0 for KLGA), so the station bias still has to be learned.
#   Free for non-commercial use (open-meteo.com terms); self-host the open-source server otherwise.
#
# Model: ensemble dressing (Roulston & Smith 2003). Each run's daily max, minus the learned bias,
# is the centre of a small normal kernel; P(bucket) = average over runs. The kernel covers what
# the ensemble does not: grid-vs-station offset and hourly sampling. Its width starts at 1.5 F and
# is learned once a station has 30 settled days: kernel^2 = var(ens mean error) - mean(spread^2).

OPEN_METEO = "https://ensemble-api.open-meteo.com/v1/ensemble"
ENS_MODELS = ("gfs025", "ecmwf_ifs025")
KERNEL_SD = 1.5
_ENS_KEY = re.compile(r"^temperature_2m(?:_member(\d+))?(?:_(.+))?$")


def parse_ensemble(js: dict, day: str, min_hours: int = 20) -> dict[str, list[float]]:
    """Daily max of every run (control included) over the local day, by model suffix."""
    times = js["hourly"]["time"]
    idx = [i for i, t in enumerate(times) if t[:10] == day]
    out: dict[str, list[float]] = {}
    for key, vals in js["hourly"].items():
        m = _ENS_KEY.match(key)
        if key == "time" or not m:
            continue
        vs = [vals[i] for i in idx if vals[i] is not None]
        if len(vs) >= min_hours:
            out.setdefault(m.group(2) or "single", []).append(max(vs))
    return out


def ensemble_max(lat: float, lon: float, day: str, tz: str, get: Get = http_json, models=ENS_MODELS, days: int = 7) -> dict:
    url = (f"{OPEN_METEO}?latitude={lat:.4f}&longitude={lon:.4f}&hourly=temperature_2m&models={','.join(models)}"
           f"&temperature_unit=fahrenheit&timezone={urllib.parse.quote(tz)}&forecast_days={days}")
    js = get(url)
    by = parse_ensemble(js, day)
    allm = [v for vs in by.values() for v in vs]
    if len(allm) < 10:
        raise ValueError(f"ensemble has {len(allm)} complete runs for {day}")
    return {"members": allm, "by_model": {k: len(v) for k, v in by.items()}, "mean": statistics.mean(allm),
            "spread": statistics.stdev(allm), "grid": [js.get("latitude"), js.get("longitude")], "url": url}


def ensemble_bucket_prob(b: Bucket, members: list[float], bias: float = 0.0, kernel_sd: float = KERNEL_SD) -> float:
    return sum(bucket_prob(b, m - bias, kernel_sd) for m in members) / len(members)


class ErrorModel:
    """Forecast-minus-actual errors per station and lead bucket, learned from settled days."""

    def __init__(self, path: str | Path | None = None, min_n: int = 30):
        self.path, self.min_n = Path(path) if path else None, min_n
        self.rows = []
        if self.path and self.path.exists():
            self.rows = [json.loads(x) for x in self.path.read_text().splitlines() if x.strip()]

    @staticmethod
    def bucket_of(lead_h: float) -> int:
        return next(i for i, (cap, _) in enumerate(PRIOR) if lead_h <= cap)

    def params(self, station: str, lead_h: float, source: str = "nws") -> dict:
        b = self.bucket_of(lead_h)
        rows = [r for r in self.rows if r["station"] == station and self.bucket_of(r["lead_h"]) == b and r.get("actual_f") is not None
                and r.get("source", "nws") == source]
        errs = [r["forecast_f"] - r["actual_f"] for r in rows]
        if source == "ensemble":
            if len(errs) >= self.min_n:
                spread2 = statistics.mean(r.get("spread", 0.0) ** 2 for r in rows)
                k = math.sqrt(max(0.5 ** 2, statistics.variance(errs) - spread2))
                return {"bias": statistics.mean(errs), "kernel_sd": k, "n": len(errs), "source": "learned"}
            return {"bias": 0.0, "kernel_sd": KERNEL_SD, "n": len(errs), "source": f"PRIOR kernel {KERNEL_SD} F (not yet 30 settled days)"}
        if len(errs) >= self.min_n:
            return {"bias": statistics.mean(errs), "sigma": max(1.0, statistics.stdev(errs)), "n": len(errs), "source": "learned"}
        return {"bias": 0.0, "sigma": PRIOR[b][1], "n": len(errs), "source": "PRIOR (not yet 30 settled days)"}

    def record(self, row: dict) -> None:
        self.rows.append(row)
        if self.path:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            with self.path.open("a") as fh:
                fh.write(json.dumps(row) + "\n")


def kelly_yes(p: float, cost: float) -> float:
    return max(0.0, (p - cost) / (1 - cost)) if 0 < cost < 1 else 0.0


def scan(slug: str, get: Get = http_json, model: ErrorModel | None = None, bankroll: float = 1000.0,
         kelly_mult: float = 0.25, cap: float = 0.05, min_edge: float = 0.02, source: str = "nws") -> dict:
    """source: 'nws' (point forecast + error model), 'ensemble' (Open-Meteo runs, dressed), or
    'blend' (the average of the two probabilities; if the ensemble fails, NWS alone, said so)."""
    ev = parse_event(get(GAMMA + urllib.parse.quote(slug)))
    if ev.unit != "F":
        raise ValueError("Celsius markets are not modelled here yet")
    day = target_date(ev.title, ev.end)
    fc = forecast_max(ev.station, day, get)
    model = model or ErrorModel()
    prm = model.params(ev.station, fc["lead_h"])
    mu = fc["max_f"] - prm["bias"]
    ens, ens_prm, notes = None, None, []
    if source in ("ensemble", "blend"):
        try:
            ens = ensemble_max(fc["lat"], fc["lon"], day, fc["tz"], get)
            ens_prm = model.params(ev.station, fc["lead_h"], "ensemble")
        except Exception as e:
            if source == "ensemble":
                raise
            notes.append(f"ensemble unavailable ({type(e).__name__}: {str(e)[:80]}): NWS only")
    rows, total = [], 0.0
    for b in ev.buckets:
        p_nws = bucket_prob(b, mu, prm["sigma"])
        p_ens = ensemble_bucket_prob(b, ens["members"], ens_prm["bias"], ens_prm["kernel_sd"]) if ens else None
        p = p_nws if p_ens is None else p_ens if source == "ensemble" else 0.5 * (p_nws + p_ens)
        total += p
        fee = b.fee_rate * b.ask * (1 - b.ask)
        cost = b.ask + fee
        edge = p - cost if b.tradable else None
        f = kelly_yes(p, cost) * kelly_mult if b.tradable else 0.0
        rows.append({"bucket": b.title, "lo": None if b.lo == -math.inf else b.lo, "hi": None if b.hi == math.inf else b.hi, "model_p": round(p, 4), "nws_p": round(p_nws, 4),
                     "ensemble_p": None if p_ens is None else round(p_ens, 4), "bid": b.bid, "ask": b.ask, "fee": round(fee, 4),
                     "edge": None if edge is None else round(edge, 4),
                     "stake_usd": round(min(f, cap) * bankroll, 2) if edge is not None and edge >= min_edge else 0.0, "tradable": b.tradable})
    return {"event": ev.title, "slug": ev.slug, "station": ev.station, "day": day, "forecast": fc, "error_model": prm,
            "mu_f": round(mu, 2), "prob_sum": round(total, 4), "buckets": rows, "source": source if ens or source == "nws" else "nws",
            "ensemble": None if not ens else {k: v for k, v in ens.items() if k != "members"} | {"error_model": ens_prm,
                                                                                               "min": min(ens["members"]), "max": max(ens["members"])},
            "notes": notes, "note": "read-only; check the event's resolution rules yourself before believing an edge"}


def observed_max(station: str, day: str, get: Get = http_json) -> dict:
    """Daily max from NWS observations over the station's local day (APPROX: see module doc)."""
    st = get(f"{NWS}/stations/{station}/observations/latest")
    lon, lat = st["geometry"]["coordinates"]
    tz = ZoneInfo(get(f"{NWS}/points/{lat:.4f},{lon:.4f}")["properties"]["timeZone"])
    start = datetime.fromisoformat(day + "T00:00:00").replace(tzinfo=tz)
    end = datetime.fromisoformat(day + "T23:59:59").replace(tzinfo=tz)
    js = get(f"{NWS}/stations/{station}/observations?start={urllib.parse.quote(start.isoformat())}&end={urllib.parse.quote(end.isoformat())}")
    temps = [f["properties"]["temperature"]["value"] for f in js.get("features", [])
             if f["properties"].get("temperature", {}).get("value") is not None]
    if not temps:
        raise ValueError(f"no observations for {station} on {day}")
    return {"station": station, "day": day, "max_f": round(max(temps) * 9 / 5 + 32), "n_obs": len(temps), "quality": "APPROX"}


def settle(log_path: str | Path, station: str, day: str, forecast_f: float, lead_h: float, get: Get = http_json,
           ens_mean: float | None = None, ens_spread: float | None = None, buckets: list | None = None) -> dict:
    """Record the observed high against the NWS forecast (and the ensemble mean, if it was used),
    and every bucket probability with its 0/1 outcome for calibration (<log>_calib.jsonl)."""
    obs = observed_max(station, day, get)
    row = {"station": station, "day": day, "lead_h": lead_h, "forecast_f": forecast_f, "actual_f": obs["max_f"], "quality": obs["quality"]}
    em = ErrorModel(log_path)
    em.record(row)
    if ens_mean is not None:
        em.record({**row, "source": "ensemble", "forecast_f": ens_mean, "spread": ens_spread or 0.0})
    if buckets:
        cp = Path(log_path).with_name(Path(log_path).stem + "_calib.jsonl")
        with cp.open("a") as fh:
            for lo, hi, p in buckets:
                y = int((lo is None or obs["max_f"] >= lo) and (hi is None or obs["max_f"] <= hi))
                fh.write(json.dumps({"station": station, "day": day, "p": p, "y": y}) + "\n")
    return row


def calibration(log_path: str | Path) -> dict:
    """Are the bucket probabilities honest? Brier score, log loss and a reliability table over
    every settled bucket (quant.reliability)."""
    from . import quant
    cp = Path(log_path).with_name(Path(log_path).stem + "_calib.jsonl")
    rows = [json.loads(x) for x in cp.read_text().splitlines() if x.strip()] if cp.exists() else []
    if not rows:
        return {"n": 0, "note": "nothing settled yet: run `tradebot weather settle` after scanned days pass"}
    p, y = [r["p"] for r in rows], [r["y"] for r in rows]
    return {"n": len(rows), "days": len({(r["station"], r["day"]) for r in rows}), **quant.reliability(p, y, bins=5),
            "log_loss": quant.log_loss(p, y)}
