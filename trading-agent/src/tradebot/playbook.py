"""The crypto playbook as code: protect capital first, then trade what the data shows.

  regime        BTC's 50- and 200-day averages. UP = price above the 200 and the 50 above the 200;
                DOWN = both below; else MIXED. Altcoin buys only in UP ("only buy alts when the
                broad market trends up").
  levels        support and resistance from swing pivots (a high/low that is the extreme of the
                `k` days either side), clustered within half an ATR, ranked by how often price
                turned there. Computed only from bars before the decision day.
  fomo          no entry after a run: +15% in a day or +50% in a week means "wait for a pullback
                to support", not "buy".
  size          the 1% rule: shares = equity x risk% / (entry - stop), risk% capped at 2%.
  swing         buy a pullback to a support with 2+ touches while price is above its 50-day
                average, the market regime allows it, and FOMO is clear. Stop 1 ATR under the
                support. Take-profit fixed BEFORE entry at the next resistance, only if it pays
                2R or more after fees; 20-day time stop. backtest() walks forward day by day.
  tokenomics    from CoinGecko /coins/{id}: share of max supply already circulating and FDV /
                market cap. A large locked share is supply still to be sold (unlocks, vesting).
                CoinGecko has no unlock calendar: check the project's own schedule.
  tax_lots      every sell, including a crypto-to-crypto swap, closes FIFO lots; short or long
                term by a 365-day holding; fees go into basis and proceeds (Form 8949 columns).
                A record to give your tax software or accountant, not tax advice.
"""
from __future__ import annotations

import csv
import io
import math
from dataclasses import asdict, dataclass
from datetime import date


def sma(xs: list[float], n: int) -> float | None:
    return sum(xs[-n:]) / n if len(xs) >= n else None


def atr(bars, i: int, n: int = 14) -> float | None:
    if i < n:
        return None
    trs = [max(bars[j].h - bars[j].l, abs(bars[j].h - bars[j - 1].c), abs(bars[j].l - bars[j - 1].c)) for j in range(i - n + 1, i + 1)]
    return sum(trs) / n


# ------------------------------------------------------------------ regime, levels, FOMO, size


def regime(closes: list[float]) -> dict:
    s50, s200 = sma(closes, 50), sma(closes, 200)
    if s200 is None:
        return {"regime": "UNKNOWN", "why": f"needs 200 daily closes, has {len(closes)}"}
    px = closes[-1]
    r = "UP" if px > s200 and s50 > s200 else "DOWN" if px < s200 and s50 < s200 else "MIXED"
    return {"regime": r, "close": px, "sma50": s50, "sma200": s200, "alts_allowed": r == "UP"}


def regime_series(bars) -> dict[str, str]:
    """Regime known at each day's close, by date (for walk-forward tests)."""
    out, closes = {}, []
    for b in bars:
        closes.append(b.c)
        out[b.d] = regime(closes)["regime"]
    return out


def levels(bars, k: int = 3, lookback: int = 180, min_touches: int = 2) -> list[dict]:
    """Price levels where swings turned, oldest data first. Uses bars[-lookback:] only."""
    xs = bars[-lookback:]
    if len(xs) < 2 * k + 15:
        return []
    a = atr(xs, len(xs) - 1) or 0.0
    pivots = []
    for i in range(k, len(xs) - k):
        win = xs[i - k:i + k + 1]
        if xs[i].h == max(b.h for b in win):
            pivots.append((xs[i].h, xs[i].d))
        if xs[i].l == min(b.l for b in win):
            pivots.append((xs[i].l, xs[i].d))
    pivots.sort()
    tol = max(a * 0.5, xs[-1].c * 0.005)
    clusters: list[list[tuple[float, str]]] = []
    for p in pivots:
        if clusters and p[0] - clusters[-1][-1][0] <= tol:
            clusters[-1].append(p)
        else:
            clusters.append([p])
    out = [{"price": sum(p for p, _ in c) / len(c), "touches": len(c), "last": max(d for _, d in c)} for c in clusters if len(c) >= min_touches]
    return sorted(out, key=lambda x: x["price"])


def fomo(bars, day_max: float = 0.15, week_max: float = 0.50) -> dict:
    if len(bars) < 8:
        return {"blocked": False, "why": "not enough history"}
    d1, d7 = bars[-1].c / bars[-2].c - 1, bars[-1].c / bars[-8].c - 1
    why = []
    if d1 > day_max:
        why.append(f"up {d1:+.0%} in a day")
    if d7 > week_max:
        why.append(f"up {d7:+.0%} in a week")
    return {"blocked": bool(why), "day": d1, "week": d7, "why": "; ".join(why) + " — wait for a pullback to support" if why else "no chase"}


def size(equity: float, entry: float, stop: float, risk_pct: float = 1.0) -> dict:
    risk_pct = min(max(risk_pct, 0.1), 2.0)  # the 1-2% rule, enforced
    per_unit = entry - stop
    if per_unit <= 0:
        return {"units": 0.0, "why": "stop is not below entry"}
    units = equity * risk_pct / 100 / per_unit
    return {"units": units, "risk_usd": units * per_unit, "risk_pct": risk_pct, "value": units * entry}


# ------------------------------------------------------------------ swing setup and walk-forward backtest


def swing_setup(bars, regime_now: str = "UP", alt: bool = True, fee: float = 0.0025, min_rr: float = 2.0) -> dict:
    """Decision at the last bar's close for an entry at the next open."""
    i = len(bars) - 1
    a = atr(bars, i)
    closes = [b.c for b in bars]
    s50 = sma(closes, 50)
    if a is None or s50 is None:
        return {"setup": False, "why": "not enough history"}
    if alt and regime_now != "UP":
        return {"setup": False, "why": f"market regime {regime_now}: no altcoin buys"}
    if closes[-1] < s50:
        return {"setup": False, "why": "below its 50-day average: no uptrend"}
    f = fomo(bars)
    if f["blocked"]:
        return {"setup": False, "why": "FOMO: " + f["why"]}
    lv = levels(bars[:-1])
    px = closes[-1]
    sup = [l for l in lv if l["price"] <= px and px - l["price"] <= a]
    if not sup:
        return {"setup": False, "why": "not at a support level"}
    s = max(sup, key=lambda l: l["price"])
    stop = s["price"] - a
    res = [l for l in lv if l["price"] > px * 1.005]
    if not res:
        return {"setup": False, "why": "no resistance above to set a take-profit (blue sky: no plan, no trade)"}
    target = min(res, key=lambda l: l["price"])["price"]
    cost = 2 * fee * px
    rr = (target - px - cost) / (px - stop + cost)
    if rr < min_rr:
        return {"setup": False, "why": f"reward:risk {rr:.1f} after fees, needs {min_rr}"}
    return {"setup": True, "entry_ref": px, "stop": stop, "target": target, "rr": rr, "support": s, "why": "pullback to support in an uptrend"}


@dataclass
class SwingTrade:
    entry_date: str
    entry: float
    stop: float
    target: float
    exit_date: str
    exit: float
    reason: str
    r: float
    ret: float


def backtest(bars, btc_regime: dict[str, str] | None = None, alt: bool = True, fee: float = 0.0025, max_days: int = 20,
             warmup: int = 200) -> dict:
    """Walk forward: decide at day i's close with bars[:i+1], enter at day i+1's open, then the
    stop (gaps fill at the open), the fixed take-profit, or the time stop."""
    from . import quant
    trades, i = [], max(warmup, 60)
    while i < len(bars) - 1:
        reg = (btc_regime or {}).get(bars[i].d, "UP")
        st = swing_setup(bars[:i + 1], reg, alt, fee)
        if not st["setup"]:
            i += 1
            continue
        e = bars[i + 1].o
        stop, target = st["stop"], st["target"]
        if not stop < e < target:
            i += 1
            continue
        exit_px, reason, j = None, "time stop", i + 1
        for j in range(i + 1, min(len(bars), i + 1 + max_days)):
            b = bars[j]
            if j > i + 1 and b.o <= stop:
                exit_px, reason = b.o, "stop (gap)"
                break
            if b.l <= stop:
                exit_px, reason = stop, "stop"
                break
            if b.h >= target:
                exit_px, reason = max(target, b.o) if j > i + 1 else target, "take-profit"
                break
        if exit_px is None:
            exit_px = bars[j].c
        ret = exit_px / e - 1 - 2 * fee
        trades.append(SwingTrade(bars[i + 1].d, e, stop, target, bars[j].d, exit_px, reason, ret / ((e - stop) / e), ret))
        i = j + 1
    rs = [t.r for t in trades]
    bh = bars[-1].c / bars[warmup].c - 1 if len(bars) > warmup else None
    eq = 1.0
    for t in trades:
        eq *= 1 + 0.01 * t.r  # 1% risk per trade
    return {"trades": len(trades), "win_rate": sum(r > 0 for r in rs) / len(rs) if rs else None, "avg_r": sum(rs) / len(rs) if rs else None,
            "psr": quant.psr(rs) if len(rs) >= 3 else None, "total_at_1pct_risk": eq - 1, "buy_and_hold": bh,
            "exits": {k: sum(t.reason == k for t in trades) for k in ("take-profit", "stop", "stop (gap)", "time stop")},
            "trades_list": [asdict(t) for t in trades]}


# ------------------------------------------------------------------ tokenomics


def tokenomics(cg: dict) -> dict:
    """From CoinGecko /api/v3/coins/{id} (verified 2026-10-15: market_data.circulating_supply,
    total_supply, max_supply, market_cap.usd, fully_diluted_valuation.usd)."""
    md = cg.get("market_data") or {}
    circ, total, mx = md.get("circulating_supply"), md.get("total_supply"), md.get("max_supply")
    mcap, fdv = (md.get("market_cap") or {}).get("usd"), (md.get("fully_diluted_valuation") or {}).get("usd")
    cap = mx or total
    out = {"id": cg.get("id"), "symbol": (cg.get("symbol") or "").upper(), "circulating": circ, "max_supply": mx, "total_supply": total,
           "circulating_share": circ / cap if circ and cap else None, "fdv_to_mcap": fdv / mcap if fdv and mcap else None, "flags": []}
    if mx is None:
        out["flags"].append("no max supply: issuance can continue indefinitely (check the inflation rate)")
    if out["circulating_share"] is not None and out["circulating_share"] < 0.7:
        out["flags"].append(f"only {out['circulating_share']:.0%} of supply circulating: {1 - out['circulating_share']:.0%} still to unlock or be issued — selling pressure")
    if out["fdv_to_mcap"] and out["fdv_to_mcap"] > 2:
        out["flags"].append(f"fully diluted value {out['fdv_to_mcap']:.1f}x today's market cap")
    out["verdict"] = "DILUTION RISK" if out["flags"] else "OK on supply"
    out["note"] = "CoinGecko has no unlock calendar: read the project's vesting schedule before buying"
    return out


# ------------------------------------------------------------------ tax lots


def tax_lots(fills: list[dict]) -> dict:
    """fills: {time (ISO), asset, side buy/sell, qty, price (USD), fee (USD)}. A crypto-to-crypto
    swap is two fills (sell one asset, buy the other) at USD values: both are recorded."""
    lots: dict[str, list[dict]] = {}
    rows = []
    for f in sorted(fills, key=lambda x: x["time"]):
        a, q, px, fee = f["asset"], float(f["qty"]), float(f["price"]), float(f.get("fee", 0) or 0)
        if f["side"] == "buy":
            lots.setdefault(a, []).append({"qty": q, "basis_per": (q * px + fee) / q, "time": f["time"]})
            continue
        left, fee_per = q, fee / q if q else 0
        while left > 1e-12 and lots.get(a):
            lot = lots[a][0]
            take = min(left, lot["qty"])
            held = (date.fromisoformat(f["time"][:10]) - date.fromisoformat(lot["time"][:10])).days
            proceeds, basis = take * (px - fee_per), take * lot["basis_per"]
            rows.append({"description": f"{take:.8g} {a}", "date_acquired": lot["time"][:10], "date_sold": f["time"][:10],
                         "proceeds": round(proceeds, 2), "cost_basis": round(basis, 2), "gain": round(proceeds - basis, 2),
                         "term": "long" if held > 365 else "short"})
            lot["qty"] -= take
            left -= take
            if lot["qty"] <= 1e-12:
                lots[a].pop(0)
        if left > 1e-9:
            rows.append({"description": f"{left:.8g} {a}", "date_acquired": "UNKNOWN", "date_sold": f["time"][:10],
                         "proceeds": round(left * (px - fee_per), 2), "cost_basis": None, "gain": None, "term": "UNKNOWN: sold more than the recorded buys"})
    tot = lambda term: round(sum(r["gain"] for r in rows if r["term"] == term and r["gain"] is not None), 2)
    return {"rows": rows, "short_term_gain": tot("short"), "long_term_gain": tot("long"),
            "open_lots": {a: round(sum(l["qty"] for l in ls), 10) for a, ls in lots.items() if ls}}


def tax_csv(rows: list[dict]) -> str:
    buf = io.StringIO()
    w = csv.DictWriter(buf, fieldnames=["description", "date_acquired", "date_sold", "proceeds", "cost_basis", "gain", "term"])
    w.writeheader()
    w.writerows(rows)
    return buf.getvalue()


def dca(bars, amount: float = 100.0, every_days: int = 7, fee: float = 0.0025, start: int = 0) -> dict:
    """Buy `amount` every `every_days` days at the close; compare with putting the same total in on day one."""
    units = invested = peak = 0.0
    worst = 0.0
    for i in range(start, len(bars)):
        if (i - start) % every_days == 0:
            units += amount * (1 - fee) / bars[i].c
            invested += amount
        val = units * bars[i].c
        peak = max(peak, val - invested)
        worst = min(worst, (val - invested) / invested if invested else 0)
    lump_units = invested * (1 - fee) / bars[start].c
    return {"invested": invested, "value": units * bars[-1].c, "return": units * bars[-1].c / invested - 1,
            "lump_sum_return": lump_units * bars[-1].c / invested - 1, "worst_drawdown_vs_invested": worst,
            "avg_cost": invested * (1 - fee) / units if units else None}


# ------------------------------------------------------------------ fetchers (run on your machine)


def yahoo_daily(symbol: str, rng: str = "2y", get=None):
    """Daily bars from GET query1.finance.yahoo.com/v8/finance/chart/{symbol}?range=..&interval=1d
    (verified 2026-10-15: chart.result[0].timestamp + indicators.quote[0].open/high/low/close/volume)."""
    import json
    import urllib.request
    from datetime import datetime, timezone
    from .insider import DayBar
    url = f"https://query1.finance.yahoo.com/v8/finance/chart/{symbol}?range={rng}&interval=1d"
    if get is None:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 tradebot"})
        with urllib.request.urlopen(req, timeout=20) as r:
            js = json.loads(r.read())
    else:
        js = get(url)
    res = js["chart"]["result"][0]
    q = res["indicators"]["quote"][0]
    out = []
    for i, t in enumerate(res["timestamp"]):
        o, h, l, c = (q[k][i] for k in ("open", "high", "low", "close"))
        if None not in (o, h, l, c):
            out.append(DayBar(datetime.fromtimestamp(t, timezone.utc).date().isoformat(), o, h, l, c, q["volume"][i] or 0))
    return out


def coingecko(coin_id: str, get=None) -> dict:
    import json
    import urllib.request
    url = (f"https://api.coingecko.com/api/v3/coins/{coin_id}?localization=false&tickers=false&market_data=true"
           "&community_data=false&developer_data=false&sparkline=false")
    if get:
        return get(url)
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "tradebot"}), timeout=20) as r:
        return json.loads(r.read())
