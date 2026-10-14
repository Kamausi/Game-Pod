"""Read-only prediction-market gap scanner: Polymarket vs Kalshi, and complete sets inside Polymarket.

Nothing here places an order or touches a wallet. It reads two public APIs, verified
live on 2026-10-14:
  Polymarket Gamma  GET https://gamma-api.polymarket.com/markets?slug=<slug>
                    bestBid / bestAsk (YES side), outcomePrices (JSON string), acceptingOrders,
                    feeSchedule {rate, exponent, takerOnly}
  Kalshi            GET https://api.elections.kalshi.com/trade-api/v2/markets/<ticker>
                    yes_bid_dollars / yes_ask_dollars / no_ask_dollars, status

Pairs are NEVER matched automatically. Two venues' "same" question often resolves on
different rules, dates or sources, and an arbitrage across mismatched rules is a bet,
not a hedge. You list confirmed pairs in a JSON file:
  [{"polymarket_slug": "...", "kalshi_ticker": "...", "note": "both resolve on the FOMC statement of Oct 28"}]

For each pair, two locked trades exist: YES on one venue + NO on the other. Payoff is $1
per pair at resolution if the rules really match. Gross edge = 1 - cost of both legs;
net edge subtracts each venue's taker fee. Fees follow each venue's published shape,
fee per contract = rate x (p x (1 - p))^exponent (Kalshi's general taker rate is 0.07,
exponent 1); check your own fee tier before believing a small edge.
"""
from __future__ import annotations

import json
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Callable

POLY = "https://gamma-api.polymarket.com/markets"
KALSHI = "https://api.elections.kalshi.com/trade-api/v2/markets"
KALSHI_TAKER_RATE = 0.07
Get = Callable[[str], object]


def _get(url: str):
    req = urllib.request.Request(url, headers={"User-Agent": "tradebot-research/0.1", "Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read())


def fee(p: float, rate: float, exponent: float = 1.0) -> float:
    return rate * (p * (1 - p)) ** exponent


@dataclass(frozen=True)
class Quote:
    venue: str
    market: str
    title: str
    yes_bid: float
    yes_ask: float
    no_ask: float
    fee_rate: float
    fee_exp: float
    tradable: bool


def parse_polymarket(m: dict) -> Quote:
    fs = m.get("feeSchedule") or {}
    bid, ask = float(m.get("bestBid") or 0), float(m.get("bestAsk") or 0)
    if not (bid or ask):
        prices = json.loads(m.get("outcomePrices") or "[]")
        bid = ask = float(prices[0]) if prices else 0.0
    rate = float(fs.get("rate", 0.0)) if m.get("feesEnabled") else 0.0
    return Quote("polymarket", m.get("slug", ""), m.get("question", ""), bid, ask, 1 - bid if bid else 1.0, rate,
                 float(fs.get("exponent", 1.0)), bool(m.get("acceptingOrders")) and not m.get("closed", False))


def parse_kalshi(m: dict) -> Quote:
    yb, ya, na = (float(m.get(k) or 0) for k in ("yes_bid_dollars", "yes_ask_dollars", "no_ask_dollars"))
    return Quote("kalshi", m.get("ticker", ""), m.get("title", ""), yb, ya, na, KALSHI_TAKER_RATE, 1.0,
                 m.get("status") in ("active", "open") and 0 < ya < 1)


def legs_edge(yes: Quote, no: Quote, contracts: float) -> dict:
    cost = yes.yes_ask + no.no_ask
    fees = fee(yes.yes_ask, yes.fee_rate, yes.fee_exp) + fee(no.no_ask, no.fee_rate, no.fee_exp)
    gross, net = 1 - cost, 1 - cost - fees
    return {"buy_yes_on": yes.venue, "yes_ask": yes.yes_ask, "buy_no_on": no.venue, "no_ask": no.no_ask,
            "gross_per_contract": gross, "fees_per_contract": fees, "net_per_contract": net,
            "net_usd": net * contracts, "locked": net > 0}


def complete_set(q: Quote) -> dict:
    """Inside one binary market: YES ask + NO ask below $1 (after fees) is a locked payoff."""
    cost = q.yes_ask + q.no_ask
    f = fee(q.yes_ask, q.fee_rate, q.fee_exp) + fee(q.no_ask, q.fee_rate, q.fee_exp)
    return {"market": q.market, "gross": 1 - cost, "net": 1 - cost - f, "locked": 1 - cost - f > 0}


def scan(pairs: list[dict], get: Get = _get, contracts: float = 100.0) -> list[dict]:
    out = []
    for pr in pairs:
        row = {"pair": pr, "error": None}
        try:
            pm = get(f"{POLY}?slug={urllib.parse.quote(pr['polymarket_slug'])}")
            pm = pm[0] if isinstance(pm, list) else pm
            km = get(f"{KALSHI}/{urllib.parse.quote(pr['kalshi_ticker'])}")
            km = km.get("market", km)
            p, k = parse_polymarket(pm), parse_kalshi(km)
        except Exception as e:  # one broken pair never stops the scan
            row["error"] = f"{type(e).__name__}: {e}"[:200]
            out.append(row)
            continue
        row.update(polymarket=p.__dict__, kalshi=k.__dict__, tradable=p.tradable and k.tradable,
                   options=[legs_edge(p, k, contracts), legs_edge(k, p, contracts)],
                   polymarket_complete_set=complete_set(p))
        row["best"] = max(row["options"], key=lambda o: o["net_per_contract"])
        out.append(row)
    return out
