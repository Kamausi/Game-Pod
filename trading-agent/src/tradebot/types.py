"""Plain market-data types. All timestamps are epoch milliseconds (UTC)."""
from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Candle:
    open_ts: int
    close_ts: int  # the instant the bar is final; nothing may use it before this
    open: float
    high: float
    low: float
    close: float
    volume: float


@dataclass(frozen=True)
class OrderBook:
    ts: int
    bids: tuple[tuple[float, float], ...]  # (price, qty), best first
    asks: tuple[tuple[float, float], ...]

    @property
    def best_bid(self) -> float:
        return self.bids[0][0]

    @property
    def best_ask(self) -> float:
        return self.asks[0][0]

    @property
    def mid(self) -> float:
        return (self.best_bid + self.best_ask) / 2


@dataclass(frozen=True)
class Fill:
    ts: int
    instrument: str
    side: str  # "buy" | "sell"
    qty: float
    price: float
    fee: float
    reason: str
