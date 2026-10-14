"""Asset classes: what differs between crypto, stocks, penny stocks and forex.

The strategy only says "buy this much exposure". This module turns that into
something each market accepts (share counts, FX units, crypto steps), knows
when each market is open, and applies the stricter rules for penny stocks and
high-volatility tokens. US market holidays are NOT modelled; a broker rejects
those orders, and the order manager treats a rejection safely.
"""
from __future__ import annotations

import math
from dataclasses import dataclass
from datetime import datetime, timezone
from enum import Enum
from zoneinfo import ZoneInfo

NY = ZoneInfo("America/New_York")


class AssetClass(str, Enum):
    CRYPTO = "crypto"
    STOCK = "stock"
    FOREX = "forex"


@dataclass(frozen=True)
class TierRules:
    max_position_frac: float  # of equity
    max_spread_pct: float
    min_dollar_volume: float  # per day
    limit_only: bool


TIERS = {
    "crypto_major": TierRules(0.25, 0.0025, 5e6, False),
    "crypto_high_vol": TierRules(0.05, 0.005, 1e6, True),  # meme coins
    "stock": TierRules(0.20, 0.002, 2e7, False),
    "penny_stock": TierRules(0.02, 0.015, 5e5, True),  # under $5/share
    "forex_major": TierRules(0.30, 0.0003, 0.0, False),
}


@dataclass(frozen=True)
class Instrument:
    symbol: str  # venue symbol: "BTC_USD", "AAPL", "EUR_USD"
    asset_class: AssetClass
    tier: str
    qty_step: float = 1e-8  # crypto: exchange step; stocks: 1 (or fractional); forex: units
    fractional: bool = False  # stocks: broker supports fractional shares
    fx_lot_units: int = 1000  # forex: trade in micro lots by default

    @property
    def rules(self) -> TierRules:
        return TIERS[self.tier]


def classify_stock(symbol: str, price: float, fractional: bool = True) -> Instrument:
    """Stocks under $5 are treated as penny stocks with the strict tier."""
    return Instrument(symbol, AssetClass.STOCK, "penny_stock" if price < 5 else "stock", qty_step=1.0, fractional=fractional and price >= 5)


def is_open(inst: Instrument, ts_ms: int) -> bool:
    if inst.asset_class is AssetClass.CRYPTO:
        return True
    t = datetime.fromtimestamp(ts_ms / 1000, tz=timezone.utc).astimezone(NY)
    if inst.asset_class is AssetClass.STOCK:  # regular session only; no holidays modelled
        if t.weekday() >= 5:
            return False
        minutes = t.hour * 60 + t.minute
        return 9 * 60 + 30 <= minutes < 16 * 60
    # forex: Sunday 17:00 to Friday 17:00 New York time
    wd, h = t.weekday(), t.hour
    if wd == 5:
        return False
    if wd == 6:
        return h >= 17
    if wd == 4:
        return h < 17
    return True


def normalize_qty(inst: Instrument, qty: float) -> float:
    """Round DOWN to what the venue accepts, so rounding never increases risk."""
    q = abs(qty)
    if inst.asset_class is AssetClass.FOREX:
        out = math.floor(q / inst.fx_lot_units) * inst.fx_lot_units
    elif inst.asset_class is AssetClass.STOCK and not inst.fractional:
        out = math.floor(q)
    elif inst.asset_class is AssetClass.STOCK:
        out = math.floor(q * 1e6) / 1e6  # brokers accept ~6-9 decimal fractional shares
    else:
        out = math.floor(q / inst.qty_step) * inst.qty_step
    return math.copysign(out, qty) if out else 0.0


@dataclass(frozen=True)
class PreTradeCheck:
    ok: bool
    reasons: tuple[str, ...]
    order_type: str  # "market" | "limit"


def pre_trade(inst: Instrument, ts_ms: int, bid: float, ask: float, dollar_volume: float, notional_frac: float) -> PreTradeCheck:
    r = inst.rules
    reasons = []
    if not is_open(inst, ts_ms):
        reasons.append("market closed")
    mid = (bid + ask) / 2
    spread = (ask - bid) / mid if mid > 0 else 1.0
    if spread > r.max_spread_pct:
        reasons.append(f"spread {spread:.2%} > {r.max_spread_pct:.2%}")
    if dollar_volume < r.min_dollar_volume:
        reasons.append(f"daily volume ${dollar_volume:,.0f} < ${r.min_dollar_volume:,.0f}")
    if abs(notional_frac) > r.max_position_frac + 1e-12:
        reasons.append(f"position {abs(notional_frac):.1%} > tier cap {r.max_position_frac:.0%}")
    return PreTradeCheck(not reasons, tuple(reasons), "limit" if r.limit_only else "market")
