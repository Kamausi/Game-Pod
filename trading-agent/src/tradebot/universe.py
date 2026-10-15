"""Which coins and stocks are cheap to trade relative to how much they move?

The price of one coin does not matter (you buy fractions, and fees are a percentage). What a trade
costs is: fee (maker or taker, per side) + spread (crossed when taking) + market impact (grows
with order size / daily volume). What a trade can earn is bounded by how far the price moves.
So the ranking is   score = typical daily move / round-trip cost,   after a liquidity floor.

Measured on Crypto.com's live tickers 2026-10-15: BTC's spread was 0.00 bp and ETH's 0.04 bp,
the cheapest on the exchange; only 7 spot USD pairs traded over $2M in 24 h. The fee, not the
coin, dominated: an entry-level taker pays 0.50% per side, 100 bp per round trip.

Fee schedules (verified 2026-10-15, entry tier unless you pass another):
  cryptocom      maker 0.25%  taker 0.50%   (crypto.com/exchange/document/fees-limits, < $10K 30d volume)
  cryptocom_cro  maker 0.00%  taker 0.44%   (same page, with the CRO-balance discount)
  alpaca_crypto  maker 0.15%  taker 0.25%   (alpaca.markets/support/crypto-maker-taker-gmt-faq, < $100K)
  binance        0.10% / 0.10%              (binance.com/en/fee/schedule, via alphapoint 2026 comparison)
  kraken         maker 0.40%  taker 0.80%   (kraken.com/features/fee-schedule, Tier 1 spot)
  alpaca_stocks  commission 0; regulatory fees on SELLS only (SEC fee on principal, FINRA TAF per
                 share, capped). The rates change by order of the SEC/FINRA: the defaults below are
                 parameters to check against alpaca.markets/support/regulatory-fees, not facts.

"Saturated" is a separate idea from "expensive": the deepest markets have the most competition,
so simple signals there are the most arbitraged. --exclude BTC,ETH removes them by choice; the
cost table still shows what that choice costs.
"""
from __future__ import annotations

import json
import math
import os
import urllib.request
from dataclasses import asdict, dataclass

FEES = {  # per side, fraction
    "cryptocom": {"maker": 0.0025, "taker": 0.0050},
    "cryptocom_cro": {"maker": 0.0, "taker": 0.0044},
    "alpaca_crypto": {"maker": 0.0015, "taker": 0.0025},
    "binance": {"maker": 0.0010, "taker": 0.0010},
    "kraken": {"maker": 0.0040, "taker": 0.0080},
    "alpaca_stocks": {"maker": 0.0, "taker": 0.0},
}
SEC_FEE_PER_DOLLAR = 27.80e-6  # sells only; CHECK the current SEC fee rate advisory
FINRA_TAF_PER_SHARE, FINRA_TAF_CAP = 0.000166, 8.30  # sells only; CHECK FINRA's current schedule


@dataclass
class Row:
    symbol: str
    kind: str  # spot / perp / stock
    price: float
    spread_bps: float
    move_bps: float  # 24 h high-low range as a share of the price: what a day can offer
    volume_usd: float
    fee_bps: float  # round trip
    impact_bps: float  # round trip, square-root model with the range as the volatility proxy
    cost_bps: float
    score: float
    note: str = ""


def _f(r: dict, *keys) -> float | None:
    for k in keys:
        v = r.get(k)
        if v not in (None, ""):
            try:
                return float(v)
            except (TypeError, ValueError):
                return None
    return None


def normalize_cryptocom(r: dict) -> dict | None:
    """Ticker in either the REST short form (i, b, k, a, h, l, vv) or the connector's long form."""
    name = r.get("instrument_name") or r.get("i")
    bid, ask = _f(r, "best_bid", "b"), _f(r, "best_ask", "k")
    last, hi, lo, vv = _f(r, "last", "a"), _f(r, "high", "h"), _f(r, "low", "l"), _f(r, "volume_value", "vv")
    if not name or not all(x and x > 0 for x in (bid, ask, last, hi, lo)) or vv is None:
        return None
    kind = "perp" if name.endswith("PERP") else "spot" if name.endswith("USD") and "-" not in name and "_" not in name else None
    if kind is None and name.endswith("_USD"):
        kind = "spot"
    return {"symbol": name, "kind": kind, "bid": bid, "ask": ask, "last": last, "high": hi, "low": lo, "volume_usd": vv} if kind else None


def cost_row(sym: str, kind: str, bid: float, ask: float, last: float, high: float, low: float, volume_usd: float,
             venue: str, maker: bool, order_usd: float, shares: float | None = None) -> Row:
    mid = (bid + ask) / 2
    spread = (ask - bid) / mid * 1e4
    move = (high - low) / last * 1e4
    fee = FEES[venue]["maker" if maker else "taker"]
    fee_bps = 2 * fee * 1e4
    if venue == "alpaca_stocks":  # regulatory fees on the sell leg only
        sh = shares if shares is not None else order_usd / last
        fee_bps += (SEC_FEE_PER_DOLLAR * order_usd + min(FINRA_TAF_CAP, FINRA_TAF_PER_SHARE * sh)) / order_usd * 1e4
    spread_paid = 0.0 if maker else spread  # half on the way in, half on the way out
    sigma = move / 1e4 / 2  # daily range ~ 2 x daily sigma for a random walk (Parkinson); a rough proxy
    impact = 2 * sigma * math.sqrt(order_usd / volume_usd) * 1e4 if volume_usd > 0 else math.inf
    cost = fee_bps + spread_paid + impact
    return Row(sym, kind, last, round(spread, 3), round(move, 1), volume_usd, round(fee_bps, 2), round(impact, 2), round(cost, 2),
               round(move / cost, 2) if cost > 0 else math.inf)


def rank(rows: list[Row], min_volume_usd: float = 1e6, max_order_share: float = 0.01, order_usd: float = 1000,
         exclude: tuple[str, ...] = (), kinds: tuple[str, ...] = ("spot",), top: int = 25) -> list[Row]:
    """Liquidity floor first (24 h volume, and the order under 1% of it), then by move / cost."""
    ex = {e.upper() for e in exclude}
    keep = []
    for r in rows:
        base = r.symbol.replace("_", "").replace("PERP", "").removesuffix("USD")
        if r.kind not in kinds or base.upper() in ex or r.symbol.upper() in ex:
            continue
        if r.volume_usd < min_volume_usd or order_usd > max_order_share * r.volume_usd:
            continue
        if r.symbol.startswith(("USDT", "USDC", "DAI", "PYUSD", "FDUSD")) or r.move_bps < 20:
            r.note = "stablecoin or does not move"
            continue
        keep.append(r)
    return sorted(keep, key=lambda r: -r.score)[:top]


def crypto_universe(tickers: list[dict], venue: str = "cryptocom", maker: bool = True, **kw) -> list[Row]:
    order = kw.get("order_usd", 1000)
    rows = []
    for t in tickers:
        n = normalize_cryptocom(t)
        if n:
            rows.append(cost_row(n["symbol"], n["kind"], n["bid"], n["ask"], n["last"], n["high"], n["low"], n["volume_usd"], venue, maker, order))
    return rank(rows, **kw)


def stock_universe(snapshots: dict, maker: bool = True, **kw) -> list[Row]:
    """Alpaca /v2/stocks/snapshots: {SYM: {latestQuote: {ap, bp}, dailyBar or prevDailyBar: {o,h,l,c,v}}}."""
    order = kw.get("order_usd", 1000)
    rows = []
    for sym, s in snapshots.items():
        q, d = s.get("latestQuote") or {}, s.get("prevDailyBar") or s.get("dailyBar") or {}
        bid, ask, c, h, lo, v = q.get("bp"), q.get("ap"), d.get("c"), d.get("h"), d.get("l"), d.get("v")
        if not all(x and x > 0 for x in (bid, ask, c, h, lo, v)) or ask < bid:
            continue
        rows.append(cost_row(sym, "stock", bid, ask, c, h, lo, v * c, "alpaca_stocks", maker, order))
    kw.setdefault("kinds", ("stock",))
    return rank(rows, **kw)


# ------------------------------------------------------------------ fetchers (public / your keys)


def _get(url: str, headers: dict | None = None) -> dict:
    req = urllib.request.Request(url, headers={"User-Agent": "tradebot-universe", **(headers or {})})
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.loads(r.read())


def fetch_cryptocom_tickers(get=_get) -> list[dict]:
    return get("https://api.crypto.com/exchange/v1/public/get-tickers")["result"]["data"]


def fetch_alpaca_snapshots(top: int = 100, get=_get) -> dict:
    """Most-active stocks (GET data.alpaca.markets/v1beta1/screener/stocks/most-actives?by=volume&top=N),
    then their snapshots (GET /v2/stocks/snapshots?symbols=...&feed=iex). Needs your Alpaca keys."""
    h = {"APCA-API-KEY-ID": os.environ.get("APCA_API_KEY_ID", ""), "APCA-API-SECRET-KEY": os.environ.get("APCA_API_SECRET_KEY", "")}
    act = get(f"https://data.alpaca.markets/v1beta1/screener/stocks/most-actives?by=volume&top={top}", h)
    syms = [a["symbol"] for a in act.get("most_actives", [])]
    out = {}
    for i in range(0, len(syms), 50):
        out.update(get("https://data.alpaca.markets/v2/stocks/snapshots?feed=iex&symbols=" + ",".join(syms[i:i + 50]), h))
    return out


def table(rows: list[Row]) -> str:
    head = f"{'symbol':<14}{'move':>8}{'spread':>9}{'fees':>8}{'impact':>8}{'cost':>8}{'move/cost':>11}{'24h $':>12}"
    lines = [head, "-" * len(head)]
    for r in rows:
        lines.append(f"{r.symbol:<14}{r.move_bps:>7.0f}b{r.spread_bps:>8.2f}b{r.fee_bps:>7.1f}b{r.impact_bps:>7.2f}b{r.cost_bps:>7.1f}b{r.score:>11.1f}{r.volume_usd / 1e6:>10.1f}M")
    return "\n".join(lines)
