"""Cross-exchange price-gap scanner and paper arbitrage book.

Venues (public REST, no keys): Crypto.com, Coinbase Exchange, Kraken.
An opportunity is "buy on A at A's ask, sell on B at B's bid". It counts only
if, after both taker fees and a latency/slippage allowance, it is still
positive, and both quotes are fresh and close together in time.

Paper execution assumes pre-funded inventory on every venue (no transfers
mid-trade, which would take minutes and kill the edge). Each executed arb
moves inventory from one venue to the other; the book shows when a venue
runs out, which in practice is what limits real arbitrage.

Not tested against the live endpoints from the build environment.
"""
from __future__ import annotations

import json
import time
import urllib.request
from dataclasses import dataclass, field
from typing import Callable

TAKER_FEE_BPS = {"cryptocom": 7.5, "coinbase": 60.0, "kraken": 40.0}  # retail top-tier defaults; set yours
SYMBOLS = {  # our symbol -> venue symbols
    "BTC_USD": {"cryptocom": "BTC_USD", "coinbase": "BTC-USD", "kraken": "XBTUSD"},
    "ETH_USD": {"cryptocom": "ETH_USD", "coinbase": "ETH-USD", "kraken": "ETHUSD"},
    "SOL_USD": {"cryptocom": "SOL_USD", "coinbase": "SOL-USD", "kraken": "SOLUSD"},
    "DOGE_USD": {"cryptocom": "DOGE_USD", "coinbase": "DOGE-USD", "kraken": "XDGUSD"},
}


@dataclass(frozen=True)
class Quote:
    venue: str
    symbol: str
    bid: float
    ask: float
    ts_ms: int  # venue timestamp
    recv_ms: int  # when we received it


def _get(url: str) -> dict:
    req = urllib.request.Request(url, headers={"User-Agent": "tradebot-research/0.1"})
    with urllib.request.urlopen(req, timeout=5) as r:
        return json.loads(r.read())


def parse_cryptocom(js: dict, symbol: str, recv_ms: int) -> Quote:
    d = js["result"]["data"][0]
    return Quote("cryptocom", symbol, float(d["b"]), float(d["k"]), int(d["t"]), recv_ms)


def parse_coinbase(js: dict, symbol: str, recv_ms: int) -> Quote:
    from datetime import datetime

    ts = int(datetime.fromisoformat(js["time"].replace("Z", "+00:00")).timestamp() * 1000)
    return Quote("coinbase", symbol, float(js["bid"]), float(js["ask"]), ts, recv_ms)


def parse_kraken(js: dict, symbol: str, recv_ms: int) -> Quote:
    if js.get("error"):
        raise RuntimeError(f"kraken: {js['error']}")
    (_, d), = js["result"].items()
    return Quote("kraken", symbol, float(d["b"][0]), float(d["a"][0]), recv_ms, recv_ms)  # Kraken ticker has no timestamp


def fetch_quotes(symbol: str, get: Callable[[str], dict] = _get) -> tuple[list[Quote], dict]:
    vs = SYMBOLS[symbol]
    urls = {
        "cryptocom": (f"https://api.crypto.com/exchange/v1/public/get-tickers?instrument_name={vs['cryptocom']}", parse_cryptocom),
        "coinbase": (f"https://api.exchange.coinbase.com/products/{vs['coinbase']}/ticker", parse_coinbase),
        "kraken": (f"https://api.kraken.com/0/public/Ticker?pair={vs['kraken']}", parse_kraken),
    }
    quotes, errors = [], {}
    for venue, (url, parse) in urls.items():
        try:
            quotes.append(parse(get(url), symbol, int(time.time() * 1000)))
        except Exception as e:
            errors[venue] = f"{type(e).__name__}: {e}"[:200]
    return quotes, errors


@dataclass(frozen=True)
class Opportunity:
    symbol: str
    buy_venue: str
    sell_venue: str
    buy_px: float
    sell_px: float
    gross_bps: float
    net_bps: float


def scan(quotes: list[Quote], now_ms: int, fees_bps: dict[str, float] = TAKER_FEE_BPS, latency_bps: float = 3.0,
         max_age_ms: int = 5000, max_skew_ms: int = 2000) -> list[Opportunity]:
    fresh = [q for q in quotes if now_ms - q.recv_ms <= max_age_ms and q.bid > 0 and q.ask > q.bid]
    out = []
    for a in fresh:
        for b in fresh:
            if a.venue == b.venue or abs(a.recv_ms - b.recv_ms) > max_skew_ms:
                continue
            gross = (b.bid / a.ask - 1) * 1e4
            net = gross - fees_bps[a.venue] - fees_bps[b.venue] - latency_bps
            if gross > 0:
                out.append(Opportunity(a.symbol, a.venue, b.venue, a.ask, b.bid, gross, net))
    return sorted(out, key=lambda o: -o.net_bps)


@dataclass
class PaperArbBook:
    """Inventory per venue: {venue: {"USD": x, "BTC_USD": qty}}."""

    balances: dict[str, dict[str, float]]
    trades: list[dict] = field(default_factory=list)

    def execute(self, opp: Opportunity, max_usd: float, fees_bps: dict[str, float] = TAKER_FEE_BPS, slip_bps: float = 2.0) -> dict | None:
        if opp.net_bps <= 0:
            return None
        buy_px = opp.buy_px * (1 + slip_bps / 1e4)
        sell_px = opp.sell_px * (1 - slip_bps / 1e4)
        usd_avail = self.balances[opp.buy_venue].get("USD", 0.0)
        coin_avail = self.balances[opp.sell_venue].get(opp.symbol, 0.0)
        qty = min(max_usd / buy_px, usd_avail / (buy_px * (1 + fees_bps[opp.buy_venue] / 1e4)), coin_avail)
        if qty <= 0:
            return {"skipped": "inventory exhausted", "buy_venue_usd": usd_avail, "sell_venue_coin": coin_avail}
        cost = qty * buy_px * (1 + fees_bps[opp.buy_venue] / 1e4)
        proceeds = qty * sell_px * (1 - fees_bps[opp.sell_venue] / 1e4)
        bv, sv = self.balances[opp.buy_venue], self.balances[opp.sell_venue]
        bv["USD"] -= cost
        bv[opp.symbol] = bv.get(opp.symbol, 0.0) + qty
        sv[opp.symbol] -= qty
        sv["USD"] = sv.get("USD", 0.0) + proceeds
        t = {"symbol": opp.symbol, "buy": opp.buy_venue, "sell": opp.sell_venue, "qty": qty, "pnl": proceeds - cost}
        self.trades.append(t)
        return t
