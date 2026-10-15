"""SEC Form 4 insider buying: parse filings, find C-suite open-market purchases, and test the
idea the Jane Street way (tests first, costs in, a 12-month holdout run once).

Sources, verified 2026-10-14 (through the Firecrawl connector; sec.gov blocks this build host):
  live feed     https://www.sec.gov/cgi-bin/browse-edgar?action=getcurrent&type=4&output=atom
                Atom; each filing appears twice (Reporting and Issuer) with one accession number.
  filing folder https://www.sec.gov/Archives/edgar/data/<cik>/<accession-no-dashes>/index.json
                lists the Form 4 XML (its name varies by filer software).
  Form 4 XML    <ownershipDocument>: issuer/issuerTradingSymbol, reportingOwner/
                reportingOwnerRelationship (isDirector, isOfficer, officerTitle, isOther, otherText),
                aff10b5One (schema X0609: the trade was made under a 10b5-1 plan),
                nonDerivativeTransaction (transactionCode, transactionShares,
                transactionPricePerShare, transactionAcquiredDisposedCode).
                A chair can be filed as isOther + otherText "Chairman of the Board", so titles
                are matched from both officerTitle and otherText.
  bulk history  Insider Transactions Data Sets, quarterly <yyyy>q<n>_form345.zip, 2006 onward:
                SUBMISSION, REPORTINGOWNER, NONDERIV_TRANS. Dates are DD-MON-YYYY. The readme PDF
                did not render the REPORTINGOWNER relationship/title column names, so the loader
                checks every column it needs by name and refuses with the list it found.

SEC asks automated clients to send a User-Agent with a contact address and to stay under
10 requests a second. Set SEC_USER_AGENT="your-name you@example.com".
"""
from __future__ import annotations

import csv
import io
import json
import math
import os
import re
import time
import urllib.request
import xml.etree.ElementTree as ET
import zipfile
from dataclasses import asdict, dataclass, field
from datetime import date, datetime, timedelta
from pathlib import Path
from typing import Callable, Iterable

FEED = "https://www.sec.gov/cgi-bin/browse-edgar?action=getcurrent&type=4&company=&dateb=&owner=include&start={start}&count=100&output=atom"
ARCHIVE = "https://www.sec.gov/Archives/edgar/data/{cik}/{acc}/"
Get = Callable[[str], bytes]

ROLES = [  # (role, pattern) checked against officerTitle and otherText, case-insensitive
    ("CEO", r"\bchief executive\b|\bceo\b"),
    ("CFO", r"\bchief financial\b|\bcfo\b"),
    ("COO", r"\bchief operating\b|\bcoo\b"),
    ("President", r"(?<!vice )(?<!vice-)\bpresident\b"),
    ("Chair", r"(?<!vice )(?<!vice-)\bchair(man|woman|person)?\b"),
]


def sec_get(url: str, timeout: float = 15) -> bytes:
    ua = os.environ.get("SEC_USER_AGENT", "")
    if "@" not in ua:
        raise RuntimeError("set SEC_USER_AGENT to 'your-name you@example.com' (SEC requires a contact in the User-Agent)")
    req = urllib.request.Request(url, headers={"User-Agent": ua, "Accept-Encoding": "identity"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        body = r.read()
    time.sleep(0.12)  # stay under the SEC's 10 requests per second
    return body


# ------------------------------------------------------------------ one filing


@dataclass
class Txn:
    code: str
    shares: float
    price: float
    acq_disp: str
    trans_date: str


@dataclass
class Filing:
    accession: str
    symbol: str
    issuer: str
    issuer_cik: str
    owner: str
    owner_cik: str
    is_director: bool
    is_officer: bool
    officer_title: str
    other_text: str
    plan_10b5_1: bool
    filed: str  # YYYY-MM-DD
    url: str
    txns: list[Txn] = field(default_factory=list)

    @property
    def role(self) -> str | None:
        return role_of(self.officer_title, self.other_text)

    @property
    def purchase_usd(self) -> float:
        """Open-market purchases (code P, acquired) in this filing, in dollars."""
        return sum(t.shares * t.price for t in self.txns if t.code == "P" and t.acq_disp == "A")


def role_of(officer_title: str, other_text: str = "") -> str | None:
    text = f"{officer_title or ''} | {other_text or ''}".lower()
    for role, pat in ROLES:
        if re.search(pat, text):
            return role
    return None


def _xml(raw: bytes | str):
    """Parse filer-supplied XML. Documents that declare a DTD or entities are refused outright
    (no XXE, no entity expansion); real Form 4s and EDGAR feeds never carry them."""
    b = raw.encode() if isinstance(raw, str) else raw
    if b"<!DOCTYPE" in b[:2048].upper() or b"<!ENTITY" in b.upper():
        raise ValueError("XML with a DTD or entity declarations refused")
    return ET.fromstring(b)


def _t(el, path: str) -> str:
    x = el.find(path)
    return (x.text or "").strip() if x is not None and x.text else ""


def _flag(el, path: str) -> bool:
    return _t(el, path).lower() in ("1", "true")


def parse_form4(xml: bytes | str, accession: str = "", url: str = "", filed: str = "") -> Filing:
    root = _xml(xml)
    ro = root.find("reportingOwner")
    rel = ro.find("reportingOwnerRelationship") if ro is not None else None
    txns = []
    for t in root.findall("nonDerivativeTable/nonDerivativeTransaction"):
        try:
            txns.append(Txn(code=_t(t, "transactionCoding/transactionCode"),
                            shares=float(_t(t, "transactionAmounts/transactionShares/value") or 0),
                            price=float(_t(t, "transactionAmounts/transactionPricePerShare/value") or 0),
                            acq_disp=_t(t, "transactionAmounts/transactionAcquiredDisposedCode/value"),
                            trans_date=_t(t, "transactionDate/value")))
        except ValueError:
            continue  # a malformed row never sinks the whole filing
    return Filing(
        accession=accession, symbol=_t(root, "issuer/issuerTradingSymbol").upper(), issuer=_t(root, "issuer/issuerName"),
        issuer_cik=_t(root, "issuer/issuerCik"), owner=_t(ro, "reportingOwnerId/rptOwnerName") if ro is not None else "",
        owner_cik=_t(ro, "reportingOwnerId/rptOwnerCik") if ro is not None else "",
        is_director=_flag(rel, "isDirector") if rel is not None else False, is_officer=_flag(rel, "isOfficer") if rel is not None else False,
        officer_title=_t(rel, "officerTitle") if rel is not None else "", other_text=_t(rel, "otherText") if rel is not None else "",
        plan_10b5_1=_flag(root, "aff10b5One"), filed=filed or _t(root, "ownerSignature/signatureDate"), url=url, txns=txns)


# ------------------------------------------------------------------ live feed


@dataclass(frozen=True)
class FeedEntry:
    accession: str
    index_url: str
    updated: str


def parse_feed(atom: bytes | str) -> list[FeedEntry]:
    ns = {"a": "http://www.w3.org/2005/Atom"}
    root = _xml(atom)
    seen, out = set(), []
    for e in root.findall("a:entry", ns):
        acc = (e.findtext("a:id", "", ns).split("accession-number=")[-1]).strip()
        link = e.find("a:link", ns)
        if not acc or acc in seen or link is None:
            continue
        seen.add(acc)
        out.append(FeedEntry(acc, link.get("href", ""), e.findtext("a:updated", "", ns)))
    return out


def folder_of(index_url: str) -> str:
    return index_url.rsplit("/", 1)[0] + "/"


def form4_xml_name(index_json: bytes | str) -> str | None:
    items = json.loads(index_json)["directory"]["item"]
    xmls = [i["name"] for i in items if i["name"].lower().endswith(".xml")]
    return xmls[0] if xmls else None


def fetch_recent(get: Get = sec_get, pages: int = 3, since: str | None = None) -> tuple[list[Filing], list[str]]:
    """Form 4s from the live feed, newest first. Returns filings and the errors met on the way."""
    filings, errors = [], []
    for p in range(pages):
        try:
            entries = parse_feed(get(FEED.format(start=p * 100)))
        except Exception as e:  # a source being down is named, not fatal
            errors.append(f"feed page {p}: {type(e).__name__}: {e}"[:200])
            break
        for en in entries:
            if since and en.updated[:10] < since:
                return filings, errors
            try:
                folder = folder_of(en.index_url)
                name = form4_xml_name(get(folder + "index.json"))
                if not name:
                    continue
                filings.append(parse_form4(get(folder + name), en.accession, folder + name, en.updated[:10]))
            except Exception as e:
                errors.append(f"{en.accession}: {type(e).__name__}: {e}"[:200])
    return filings, errors


# ------------------------------------------------------------------ signals


@dataclass
class Signal:
    symbol: str
    filed: str  # YYYY-MM-DD, the day the filing became public
    owner: str
    role: str
    usd: float
    accession: str
    url: str = ""
    plan_10b5_1: bool = False
    batch: bool = False
    flags: list[str] = field(default_factory=list)


def signals_from(filings: Iterable[Filing], min_usd: float = 100_000) -> list[Signal]:
    out = []
    for f in filings:
        role, usd = f.role, f.purchase_usd
        if role and usd >= min_usd and f.symbol:
            out.append(Signal(f.symbol, f.filed, f.owner, role, usd, f.accession, f.url, f.plan_10b5_1))
    return mark_batches(out)


def mark_batches(sigs: list[Signal], all_buyers: dict | None = None, min_people: int = 3, max_spread: float = 3.0) -> list[Signal]:
    """A batch is several insiders of one company filing buys the same day for similar amounts:
    usually a pay plan or a coordinated program, not conviction. Batches never pass."""
    groups: dict[tuple, list[Signal]] = {}
    for s in sigs:
        groups.setdefault((s.symbol, s.filed), []).append(s)
    for key, g in groups.items():
        people = {s.owner for s in g} | set((all_buyers or {}).get(key, []))
        amounts = [s.usd for s in g]
        if len(people) >= min_people and max(amounts) / max(min(amounts), 1) <= max_spread:
            for s in g:
                s.batch = True
                s.flags.append(f"batch: {len(people)} insiders filed buys the same day for similar amounts")
    for s in sigs:
        if s.plan_10b5_1:
            s.flags.append("bought under a 10b5-1 plan (scheduled, not a fresh decision)")
    return sigs


# ------------------------------------------------------------------ bulk history


_MON = {m: i for i, m in enumerate(["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"], 1)}


def sec_date(s: str) -> str:
    """'14-OCT-2026' -> '2026-10-14'; ISO passes through."""
    s = (s or "").strip()
    if re.match(r"^\d{4}-\d{2}-\d{2}", s):
        return s[:10]
    d, m, y = s.split("-")
    return f"{int(y):04d}-{_MON[m.upper()[:3]]:02d}-{int(d):02d}"


NEED = {
    "SUBMISSION": ["ACCESSION_NUMBER", "FILING_DATE", "DOCUMENT_TYPE", "ISSUERTRADINGSYMBOL"],
    "REPORTINGOWNER": ["ACCESSION_NUMBER", "RPTOWNERNAME", "RPTOWNER_RELATIONSHIP", "RPTOWNER_TITLE"],
    "NONDERIV_TRANS": ["ACCESSION_NUMBER", "TRANS_CODE", "TRANS_SHARES", "TRANS_PRICEPERSHARE", "TRANS_ACQUIRED_DISP_CD"],
}


def _read_table(src, name: str) -> list[dict]:
    if isinstance(src, zipfile.ZipFile):
        member = next((n for n in src.namelist() if Path(n).stem.upper() == name), None)
        if member is None:
            raise FileNotFoundError(f"{name} not in the zip")
        text = src.read(member).decode("utf-8", errors="replace")
    else:
        path = next((p for p in Path(src).iterdir() if p.stem.upper() == name), None)
        if path is None:
            raise FileNotFoundError(f"{name} not in {src}")
        text = path.read_text(encoding="utf-8", errors="replace")
    first = text.split("\n", 1)[0]
    rows = list(csv.DictReader(io.StringIO(text), delimiter="\t" if "\t" in first else ","))
    missing = [c for c in NEED[name] if rows and c not in rows[0]]
    if missing:
        raise ValueError(f"{name} is missing columns {missing}; it has {list(rows[0])[:40]}")
    return rows


def load_form345(src: str | Path, min_usd: float = 100_000, any_insider: bool = False) -> tuple[list[Signal], dict]:
    """C-suite open-market buys from one quarterly data set (a zip or an unpacked folder).
    Amendments (4/A) are skipped so one trade is not counted twice. any_insider=True keeps every
    reporting owner (a study's own filters, for replicating a published result first)."""
    src = Path(src)
    zf = zipfile.ZipFile(src) if src.suffix.lower() == ".zip" else None
    try:
        sub = {r["ACCESSION_NUMBER"]: r for r in _read_table(zf or src, "SUBMISSION") if r["DOCUMENT_TYPE"].strip() == "4"}
        owners: dict[str, list[dict]] = {}
        for r in _read_table(zf or src, "REPORTINGOWNER"):
            owners.setdefault(r["ACCESSION_NUMBER"], []).append(r)
        buys: dict[str, float] = {}
        for r in _read_table(zf or src, "NONDERIV_TRANS"):
            if r["TRANS_CODE"].strip() == "P" and r["TRANS_ACQUIRED_DISP_CD"].strip() == "A":
                try:
                    buys[r["ACCESSION_NUMBER"]] = buys.get(r["ACCESSION_NUMBER"], 0.0) + float(r["TRANS_SHARES"] or 0) * float(r["TRANS_PRICEPERSHARE"] or 0)
                except ValueError:
                    pass
    finally:
        if zf:
            zf.close()
    sigs, all_buyers, stats = [], {}, {"filings": len(sub), "with_purchase": 0, "c_suite_100k": 0}
    for acc, usd in buys.items():
        s = sub.get(acc)
        if not s:
            continue
        stats["with_purchase"] += 1
        filed, sym = sec_date(s["FILING_DATE"]), s["ISSUERTRADINGSYMBOL"].strip().upper()
        for o in owners.get(acc, []):
            all_buyers.setdefault((sym, filed), []).append(o["RPTOWNERNAME"])
            role = role_of(o.get("RPTOWNER_TITLE", ""), o.get("RPTOWNER_TXT", "") or o.get("RPTOWNER_RELATIONSHIP", "")) or ("insider" if any_insider else None)
            if role and usd >= min_usd and sym:
                aff = (s.get("AFF10B5ONE") or "").strip() in ("1", "true", "Y")
                sigs.append(Signal(sym, filed, o["RPTOWNERNAME"], role, usd, acc, plan_10b5_1=aff))
                stats["c_suite_100k"] += 1
    return mark_batches(sigs, all_buyers), stats


# ------------------------------------------------------------------ prices


@dataclass(frozen=True)
class DayBar:
    d: str
    o: float
    h: float
    l: float
    c: float
    v: float


class CsvPrices:
    """Daily bars, one CSV per symbol: <dir>/<SYMBOL>.csv with date,open,high,low,close,volume."""

    def __init__(self, folder: str | Path):
        self.folder, self._cache = Path(folder), {}

    def bars(self, symbol: str) -> list[DayBar]:
        if symbol not in self._cache:
            p = self.folder / f"{symbol}.csv"
            rows = []
            if p.exists():
                for r in csv.DictReader(p.open()):
                    k = {x.lower(): x for x in r}
                    try:
                        rows.append(DayBar(r[k["date"]][:10], float(r[k["open"]]), float(r[k["high"]]), float(r[k["low"]]),
                                           float(r[k["close"]]), float(r[k.get("volume", "volume")] or 0)))
                    except (KeyError, ValueError):
                        continue
            self._cache[symbol] = sorted(rows, key=lambda b: b.d)
        return self._cache[symbol]


class AlpacaBars:
    """Alpaca market data, GET https://data.alpaca.markets/v2/stocks/{symbol}/bars?timeframe=1Day
    (verified against the OpenAPI definition 2026-10-14). Free accounts must use feed=iex."""

    def __init__(self, start: str, end: str, feed: str = "iex", transport=None, key_id: str | None = None, secret: str | None = None):
        from .brokers import urllib_transport
        self.start, self.end, self.feed, self.t = start, end, feed, transport or urllib_transport
        self.h = {"APCA-API-KEY-ID": key_id or os.environ.get("APCA_API_KEY_ID", ""),
                  "APCA-API-SECRET-KEY": secret or os.environ.get("APCA_API_SECRET_KEY", "")}
        self._cache = {}

    def bars(self, symbol: str) -> list[DayBar]:
        if symbol in self._cache:
            return self._cache[symbol]
        out, token = [], None
        for _ in range(50):
            url = (f"https://data.alpaca.markets/v2/stocks/{symbol}/bars?timeframe=1Day&start={self.start}&end={self.end}"
                   f"&adjustment=all&feed={self.feed}&limit=10000" + (f"&page_token={token}" if token else ""))
            status, js = self.t("GET", url, self.h, None)
            if status >= 300:
                break
            out += [DayBar(b["t"][:10], b["o"], b["h"], b["l"], b["c"], b["v"]) for b in js.get("bars") or []]
            token = js.get("next_page_token")
            if not token:
                break
        self._cache[symbol] = out
        return out


def atr(bars: list[DayBar], i: int, n: int = 14) -> float | None:
    """ATR over the n bars that END at index i (all known at the close of bar i)."""
    if i < n:
        return None
    trs = [max(bars[k].h - bars[k].l, abs(bars[k].h - bars[k - 1].c), abs(bars[k].l - bars[k - 1].c)) for k in range(i - n + 1, i + 1)]
    return sum(trs) / n


def adv(bars: list[DayBar], i: int, n: int = 20) -> float:
    w = bars[max(0, i - n + 1):i + 1]
    return sum(b.c * b.v for b in w) / len(w) if w else 0.0


# ------------------------------------------------------------------ the trade, simulated


@dataclass
class TradeResult:
    symbol: str
    filed: str
    entry_date: str
    entry: float
    stop: float
    exit_date: str
    exit: float
    reason: str
    ret: float  # after costs, fraction of entry
    r: float  # after costs, in units of the risk to the stop
    bench_ret: float | None = None
    car: float | None = None  # market-model abnormal return over the holding days, after costs
    beta: float | None = None  # estimated on trading days -130..-11 before entry


def simulate(sig: Signal, bars: list[DayBar], hold: int = 5, atr_mult: float = 2.0, cost: float = 0.001,
             min_adv: float = 20e6, bench: list[DayBar] | None = None) -> TradeResult | str:
    """Buy at the first open AFTER the filing date (a filing can land after the close), stop at
    entry - 2 x ATR(14) known at the prior close, exit at the close of the 5th trading day.
    A gap through the stop fills at the open, not at the stop. Returns a reason string when the
    signal cannot be priced or fails a filter, so unpriced signals are counted, never dropped."""
    k = next((j for j, b in enumerate(bars) if b.d > sig.filed), None)
    if k is None or k == 0:
        return "unpriced: no bar after the filing (delisted, renamed or missing data)"
    a = atr(bars, k - 1)
    if a is None:
        return "unpriced: under 15 bars of history before entry"
    if adv(bars, k - 1) < min_adv:
        return "filtered: average daily dollar volume under the minimum"
    entry = bars[k].o
    stop = entry - atr_mult * a
    if stop <= 0 or entry <= 0:
        return "unpriced: bad prices"
    last = min(k + hold - 1, len(bars) - 1)
    if last < k + hold - 1:
        return "unpriced: fewer than 5 trading days after entry"
    exit_px, reason, exit_i = bars[last].c, "time exit (day 5)", last
    for j in range(k, last + 1):
        b = bars[j]
        if j > k and b.o <= stop:
            exit_px, reason, exit_i = b.o, "stop (gapped through)", j
            break
        if b.l <= stop:
            exit_px, reason, exit_i = stop, "stop", j
            break
    gross = exit_px / entry - 1
    ret = gross - 2 * cost
    r = ret / ((entry - stop) / entry)
    bret, car, beta = None, None, None
    if bench:
        bi = {b.d: b for b in bench}
        if bars[k].d in bi and bars[exit_i].d in bi:
            bret = bi[bars[exit_i].d].c / bi[bars[k].d].o - 1
        car, beta = _event_car(bars, k, exit_i, exit_px, bi, cost)
    return TradeResult(sig.symbol, sig.filed, bars[k].d, entry, stop, bars[exit_i].d, exit_px, reason, ret, r, bret, car, beta)


def _event_car(bars: list[DayBar], k: int, exit_i: int, exit_px: float, bi: dict, cost: float, est: tuple[int, int] = (130, 11)):
    """Market-model abnormal return (MacKinlay 1997): alpha and beta from daily close-to-close
    returns over trading days k-130..k-11 (a gap so the estimate cannot see the event), then the
    stock's actual path (open entry -> exit price) minus alpha + beta x the market's same path."""
    from .quant import market_model
    lo, hi = k - est[0], k - est[1]
    if lo < 1:
        return None, None
    xs, ys = [], []
    for j in range(lo, hi + 1):
        a, b = bars[j - 1], bars[j]
        if a.d in bi and b.d in bi and a.c > 0 and bi[a.d].c > 0:
            ys.append(b.c / a.c - 1)
            xs.append(bi[b.d].c / bi[a.d].c - 1)
    if len(xs) < 60 or bars[k].d not in bi or bars[exit_i].d not in bi:
        return None, None
    alpha, beta, _ = market_model(ys, xs)
    days = exit_i - k + 1
    stock = exit_px / bars[k].o - 1 - 2 * cost
    mkt = bi[bars[exit_i].d].c / bi[bars[k].d].o - 1
    return stock - (alpha * days + beta * mkt), beta


def report(trades: list[TradeResult], skipped: dict, risk_per_trade: float = 0.01) -> dict:
    """The numbers the Quant seat must always report, same for build and holdout."""
    n = len(trades)
    wins = [t for t in trades if t.ret > 0]
    losses = [t for t in trades if t.ret <= 0]
    eq, peak, mdd = 1.0, 1.0, 0.0
    for t in sorted(trades, key=lambda x: x.exit_date):  # sized at 1% risk per trade
        eq *= 1 + risk_per_trade * t.r
        peak = max(peak, eq)
        mdd = max(mdd, 1 - eq / peak)
    bench = [t.bench_ret for t in trades if t.bench_ret is not None]
    mean = lambda xs: sum(xs) / len(xs) if xs else None
    return {
        "trades": n, "win_rate": len(wins) / n if n else None,
        "avg_win": mean([t.ret for t in wins]), "avg_loss": mean([t.ret for t in losses]),
        "avg_r": mean([t.r for t in trades]), "avg_ret": mean([t.ret for t in trades]),
        "avg_bench_ret_same_days": mean(bench), "excess_vs_bench": (mean([t.ret for t in trades if t.bench_ret is not None]) - mean(bench)) if bench else None,
        "total_return_at_1pct_risk": eq - 1, "max_drawdown_at_1pct_risk": mdd,
        "stops": sum(t.reason.startswith("stop") for t in trades),
        "skipped": skipped, "unpriced": sum(v for k, v in skipped.items() if k.startswith("unpriced")),
        **_stats(trades),
    }


def _stats(trades: list[TradeResult]) -> dict:
    """The statistics that say whether avg R is distinguishable from luck (quant.py)."""
    from . import quant
    rs = [t.r for t in sorted(trades, key=lambda x: x.entry_date)]
    if len(rs) < 3:
        return {"r_std": None, "sharpe_per_trade": None, "psr": None, "avg_r_ci95": None, "min_trades_for_95pct": None,
                "avg_car": None, "car_t": None, "car_trades": 0}
    _, sd, _, _ = quant.moments(rs)
    cars = [t.car for t in trades if t.car is not None]
    car_t = None
    if len(cars) >= 3:
        _, csd, _, _ = quant.moments(cars)
        car_t = (sum(cars) / len(cars)) / (csd / math.sqrt(len(cars) - 1)) if csd else None
    return {"r_std": sd, "sharpe_per_trade": quant.sharpe(rs), "psr": quant.psr(rs),
            # trades in date order, blocks kept together: trades in the same week share news
            "avg_r_ci95": list(quant.stationary_bootstrap_ci(rs, reps=1000)),
            "min_trades_for_95pct": quant.min_track_record(rs),
            "avg_car": sum(cars) / len(cars) if cars else None, "car_t": car_t, "car_trades": len(cars)}


def backtest(sigs: list[Signal], prices, *, bench_symbol: str = "SPY", hold: int = 5, atr_mult: float = 2.0,
             cost: float = 0.001, min_adv: float = 20e6, include_batches: bool = False, include_plans: bool = False) -> dict:
    bench = prices.bars(bench_symbol) if bench_symbol else None
    trades, skipped = [], {}
    for s in sorted(sigs, key=lambda x: x.filed):
        if s.batch and not include_batches:
            skipped["filtered: batch filing"] = skipped.get("filtered: batch filing", 0) + 1
            continue
        if s.plan_10b5_1 and not include_plans:
            skipped["filtered: 10b5-1 plan"] = skipped.get("filtered: 10b5-1 plan", 0) + 1
            continue
        res = simulate(s, prices.bars(s.symbol), hold, atr_mult, cost, min_adv, bench)
        if isinstance(res, str):
            skipped[res] = skipped.get(res, 0) + 1
        else:
            trades.append(res)
    out = report(trades, skipped)
    out["trades_list"] = [asdict(t) for t in trades]
    return out


def split_holdout(sigs: list[Signal], months: int = 12, today: str | None = None) -> tuple[list[Signal], list[Signal], str]:
    """Everything filed in the last `months` months is the holdout."""
    end = date.fromisoformat(today) if today else date.today()
    cut = (end - timedelta(days=round(months * 30.44))).isoformat()
    return [s for s in sigs if s.filed < cut], [s for s in sigs if s.filed >= cut], cut


def verdict(build: dict, holdout: dict | None, deflated: dict | None = None) -> str:
    """Plain words. The default is no edge. `deflated` is quant.dsr() over the build trades with
    the number of configurations tried: below 0.95 the best run is what luck alone would show."""
    def ok(r):
        return r and r["trades"] >= 30 and (r["avg_r"] or 0) > 0 and (r["excess_vs_bench"] is None or r["excess_vs_bench"] > 0)
    if not ok(build):
        return "NO EDGE: the build period itself does not beat costs and the benchmark over enough trades."
    if deflated and deflated.get("dsr") is not None and deflated["dsr"] < 0.95:
        return (f"NO EDGE (deflated): after {deflated['trials']} configurations tried, the probability this Sharpe is real is "
                f"{deflated['dsr']:.0%} (needs 95%). The best of many tries looks this good by luck.")
    ci = build.get("avg_r_ci95")
    if ci and ci[0] is not None and ci[0] <= 0:
        return f"NOT PROVEN: average R is positive but its 95% interval ({ci[0]:+.2f}R to {ci[1]:+.2f}R) still includes zero. More data."
    if holdout is None:
        return "NOT FINISHED: the build period looks positive; the holdout has not been run."
    if not ok(holdout):
        return "NO EDGE: positive while building, not on the 12 months it never saw. Treat as overfit."
    if (holdout["avg_r"] or 0) < 0.5 * (build["avg_r"] or 0):
        return "WEAK: the holdout kept the sign but lost more than half the build period's average R."
    return "EDGE (best case): positive in the build period and the holdout, after costs. Survivorship bias still flatters it."


# ------------------------------------------------------------------ downloads (run where sec.gov is reachable)

DATASET_URLS = ("https://www.sec.gov/files/structureddata/data/insider-transactions-data-sets/{q}_form345.zip",
                "https://www.sec.gov/files/datastandardsinnovation/data/insider-transactions-data-sets/{q}_form345.zip")


def quarter_list(specs: list[str]) -> list[str]:
    """'2016q1', '2016q1-2017q2' -> ['2016q1', ...]."""
    out = []
    for s in specs:
        a, _, b = s.lower().partition("-")
        b = b or a
        (y, q), (y2, q2) = (map(int, a.split("q")), map(int, b.split("q")))
        if not (1 <= q <= 4 and 1 <= q2 <= 4) or (y, q) > (y2, q2):
            raise ValueError(f"bad quarter range {s}")
        while (y, q) <= (y2, q2):
            out.append(f"{y}q{q}")
            y, q = (y + 1, 1) if q == 4 else (y, q + 1)
    return list(dict.fromkeys(out))


def fetch_datasets(quarters: list[str], out: str | Path, get=sec_get) -> list[dict]:
    """Download each quarter's form345.zip once (skips files already there and valid). The SEC
    moved the newest quarters to a second path in 2026, so both are tried; the newer path first
    from 2026q2 on."""
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    for q in quarters:
        dest = out / f"{q}_form345.zip"
        if dest.exists() and zipfile.is_zipfile(dest):
            rows.append({"quarter": q, "file": str(dest), "status": "already there"})
            continue
        urls = DATASET_URLS[::-1] if q >= "2026q2" else DATASET_URLS
        err = None
        for u in urls:
            try:
                body = get(u.format(q=q), timeout=120)
            except Exception as e:
                err = f"{type(e).__name__}: {e}"[:160]
                continue
            if body[:2] != b"PK":
                err = f"{u.format(q=q)} did not return a zip"
                continue
            tmp = dest.with_suffix(".part")
            tmp.write_bytes(body)
            if not zipfile.is_zipfile(tmp):
                tmp.unlink()
                err = "corrupt zip"
                continue
            tmp.replace(dest)
            rows.append({"quarter": q, "file": str(dest), "status": "downloaded", "bytes": len(body), "url": u.format(q=q)})
            err = None
            break
        if err:
            rows.append({"quarter": q, "status": "FAILED", "error": err})
    return rows


class RateLimit:
    """Token bucket shared by worker threads: at most `per_minute` calls in any minute."""

    def __init__(self, per_minute: float, clock=time.monotonic, sleep=time.sleep):
        import threading
        self.gap, self.next, self.lock, self.clock, self.sleep = 60.0 / per_minute, 0.0, threading.Lock(), clock, sleep

    def wait(self) -> None:
        with self.lock:
            now = self.clock()
            at = max(now, self.next)
            self.next = at + self.gap
        if at > now:
            self.sleep(at - now)


def fetch_prices(symbols: list[str], prices, out: str | Path, workers: int = 4, per_minute: float = 180) -> list[dict]:
    """Write <SYMBOL>.csv for each symbol from any .bars() source (AlpacaBars, OpenBBPrices),
    `workers` at a time under a shared rate limit (Alpaca's free tier allows 200 requests/min).
    Symbols with no bars are reported: that count is the survivorship hole (delisted names)."""
    from concurrent.futures import ThreadPoolExecutor
    from .platforms import write_bars_csv
    limit = RateLimit(per_minute)

    def one(s):
        limit.wait()
        try:
            bars = prices.bars(s)
        except Exception as e:
            return {"symbol": s, "bars": 0, "error": f"{type(e).__name__}: {e}"[:120]}
        if bars:
            write_bars_csv(bars, Path(out) / f"{s}.csv")
        return {"symbol": s, "bars": len(bars), "first": bars[0].d if bars else None, "last": bars[-1].d if bars else None}
    syms = sorted(set(symbols) | {"SPY"})
    with ThreadPoolExecutor(max_workers=max(1, workers)) as ex:
        return list(ex.map(one, syms))
