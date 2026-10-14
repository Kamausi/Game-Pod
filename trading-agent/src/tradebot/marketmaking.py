"""Avellaneda-Stoikov market making, as a research simulation with a fee honesty check.

Research only: there is no live order path for this strategy. A maker strategy needs
post-only access, a queue model and real fill data before any money should touch it,
and the pasted claims around it ("print money on every block") are exactly what the
honesty check below exists to test.

Model (Avellaneda & Stoikov 2008), in basis points of the mid so parameters travel
across instruments:
    reservation offset   r = -q * gamma * sigma^2 * tau
    total spread         s = gamma * sigma^2 * tau + (2 / gamma) * ln(1 + gamma / kappa)
    quotes               bid = mid * (1 + (r - s/2) / 1e4),  ask = mid * (1 + (r + s/2) / 1e4)
where q is inventory in order units, sigma is per-bar volatility in bps, tau is the
remaining horizon in bars, gamma is risk aversion and kappa the order-arrival decay.

Simulation on candles, conservative by construction:
  - quotes are set at a bar's close and live during the next bar
  - a bid fills only if the next bar trades THROUGH it (low < bid); same for the ask.
    No queue priority is assumed, which flatters nothing
  - every fill pays the maker fee; every requote pays `quote_cost_bps` of order
    notional (gas on-chain, 0 on most CEXs)
  - inventory is capped; the side that would exceed the cap is not quoted
  - each fill's markout is measured 1 and 5 bars later: how far the mid moved against
    us. That is adverse selection, the cost that kills most naive market makers

The honesty check: the strategy only earns if the average half-spread captured beats
maker fee + quote cost + adverse selection. `simulate` reports all three.
"""
from __future__ import annotations

import math
from dataclasses import asdict, dataclass

from .types import Candle


@dataclass(frozen=True)
class MMParams:
    gamma: float = 1e-4  # per bps^2: with 60 bps/bar vol and a 60-bar horizon, ~22 bps of spread and skew per unit
    kappa: float = 0.5  # (2/gamma)ln(1+gamma/kappa) ~ 2/kappa bps for small gamma
    horizon_bars: int = 60
    vol_window: int = 60
    max_inventory: int = 5
    order_usd: float = 100.0
    maker_fee_bps: float = 2.0  # negative for a rebate
    quote_cost_bps: float = 0.0  # per requote, e.g. gas as bps of order notional
    min_spread_bps: float = 0.0


def as_quotes(q: float, sigma_bps: float, tau: float, p: MMParams) -> tuple[float, float]:
    """(reservation offset bps, total spread bps)."""
    r = -q * p.gamma * sigma_bps ** 2 * tau
    s = p.gamma * sigma_bps ** 2 * tau + (2 / p.gamma) * math.log(1 + p.gamma / p.kappa)
    return r, max(s, p.min_spread_bps)


def _sigma_bps(closes: list[float]) -> float:
    rets = [math.log(b / a) * 1e4 for a, b in zip(closes, closes[1:])]
    if len(rets) < 2:
        return 0.0
    m = sum(rets) / len(rets)
    return math.sqrt(sum((x - m) ** 2 for x in rets) / (len(rets) - 1))


def simulate(candles: list[Candle], p: MMParams = MMParams()) -> dict:
    q = 0  # inventory in order units (what the model and the cap see)
    base = 0.0  # inventory in coins (what is actually marked to market)
    cash = 0.0
    fills, markouts1, markouts5, spreads, edge = [], [], [], [], []
    fees = quote_costs = 0.0
    start = p.vol_window + 1
    for i in range(start, len(candles) - 1):
        bar, nxt = candles[i], candles[i + 1]
        mid = bar.close
        sigma = _sigma_bps([c.close for c in candles[i - p.vol_window:i + 1]])
        tau = p.horizon_bars - (i - start) % p.horizon_bars
        r, s = as_quotes(q, sigma, tau, p)
        spreads.append(s)
        r = max(-s / 2, min(s / 2, r))  # skew can lean the quotes, never push a quote through the mid
        bid = mid * (1 + (r - s / 2) / 1e4)
        ask = mid * (1 + (r + s / 2) / 1e4)
        unit = p.order_usd / mid
        qc = p.quote_cost_bps / 1e4 * p.order_usd * ((q < p.max_inventory) + (q > -p.max_inventory))
        quote_costs += qc
        cash -= qc
        for side, px, ok in (("buy", bid, q < p.max_inventory and nxt.low < bid),
                             ("sell", ask, q > -p.max_inventory and nxt.high > ask)):
            if not ok:
                continue
            sgn = 1 if side == "buy" else -1
            q += sgn
            base += sgn * unit
            fee = p.maker_fee_bps / 1e4 * px * unit
            cash -= sgn * px * unit + fee
            fees += fee
            edge.append(sgn * (mid - px) / mid * 1e4)  # half-spread captured vs the mid we quoted around
            for k, out in ((1, markouts1), (5, markouts5)):
                j = min(len(candles) - 1, i + 1 + k)
                out.append(sgn * (candles[j].close / px - 1) * 1e4)  # positive = price moved in our favour
            fills.append({"ts": nxt.close_ts, "side": side, "price": px, "q_after": q})
    pnl = cash + base * candles[-1].close
    avg = lambda xs: sum(xs) / len(xs) if xs else 0.0
    notional = sum(f["price"] * (p.order_usd / f["price"]) for f in fills)
    captured, adverse = avg(edge), -avg(markouts5)
    cost_bps = p.maker_fee_bps + (quote_costs / notional * 1e4 if notional else 0.0)
    return {
        "params": asdict(p), "bars": len(candles) - start - 1, "fills": len(fills),
        "buys": sum(f["side"] == "buy" for f in fills), "sells": sum(f["side"] == "sell" for f in fills),
        "final_inventory": q, "max_abs_inventory": max((abs(f["q_after"]) for f in fills), default=0),
        "pnl_usd": pnl, "fees_usd": fees, "quote_costs_usd": quote_costs, "volume_usd": notional,
        "pnl_bps_of_volume": pnl / notional * 1e4 if notional else 0.0,
        "avg_spread_quoted_bps": avg(spreads), "avg_half_spread_captured_bps": captured,
        "markout_1bar_bps": avg(markouts1), "markout_5bar_bps": avg(markouts5),
        "honesty": {
            "captured_bps": captured, "fees_and_quote_costs_bps": cost_bps, "adverse_selection_bps": adverse,
            "net_edge_bps": captured - cost_bps - adverse,
            "verdict": ("earns: captured spread beats fees, quote costs and adverse selection"
                        if captured - cost_bps - adverse > 0 else
                        "does not earn: fees, quote costs and adverse selection eat the spread"),
        },
    }
