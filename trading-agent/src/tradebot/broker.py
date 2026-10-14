"""Execution. PaperBroker simulates fills with fees and slippage.

Live venues (venues_live.py) mirror their real fills into a PaperBroker with
`apply_fill`, so the agent, risk layer and journal see one book either way.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from .risk import RiskManager, Verdict
from .types import Fill


@dataclass
class Position:
    qty: float = 0.0
    avg_entry: float = 0.0
    stop: float = 0.0
    take: float = 0.0

    @property
    def side(self) -> str | None:
        if self.qty > 0:
            return "long"
        if self.qty < 0:
            return "short"
        return None


@dataclass
class PaperBroker:
    cash: float
    fee_bps: float = 6.0  # taker fee per side
    slippage_bps: float = 2.0  # beyond the touch, per side
    positions: dict[str, Position] = field(default_factory=dict)
    fills: list[Fill] = field(default_factory=list)

    def position(self, instrument: str) -> Position:
        return self.positions.setdefault(instrument, Position())

    def equity(self, marks: dict[str, float]) -> float:
        return self.cash + sum(p.qty * marks[i] for i, p in self.positions.items() if p.qty)

    def market(self, ts: int, instrument: str, side: str, qty: float, ref_price: float, reason: str, slip_bps: float | None = None) -> Fill:
        """Fill at ref_price (the touch) worsened by slippage, plus fees."""
        slip = (self.slippage_bps if slip_bps is None else slip_bps) / 1e4
        price = ref_price * (1 + slip) if side == "buy" else ref_price * (1 - slip)
        fee = qty * price * self.fee_bps / 1e4
        return self.apply_fill(ts, instrument, side, qty, price, fee, reason)

    def apply_fill(self, ts: int, instrument: str, side: str, qty: float, price: float, fee: float, reason: str) -> Fill:
        """Book a fill at a known price and fee (paper fills, or a mirrored live fill)."""
        pos = self.position(instrument)
        signed = qty if side == "buy" else -qty
        new_qty = pos.qty + signed
        if pos.qty == 0 or pos.qty * signed > 0:  # open or add
            pos.avg_entry = (pos.avg_entry * abs(pos.qty) + price * qty) / abs(new_qty)
        elif new_qty * pos.qty < 0:  # flipped through zero
            pos.avg_entry = price
        pos.qty = 0.0 if abs(new_qty) < 1e-12 else new_qty
        if pos.qty == 0:
            pos.avg_entry = pos.stop = pos.take = 0.0
        self.cash -= signed * price + fee
        f = Fill(ts, instrument, side, qty, price, fee, reason)
        self.fills.append(f)
        return f


class GuardedBroker:
    """The only order path. Risk check first, always."""

    def __init__(self, broker: PaperBroker, risk: RiskManager):
        self.broker = broker
        self.risk = risk

    def submit(
        self,
        ts: int,
        instrument: str,
        side: str,
        qty: float,
        ref_price: float,
        equity: float,
        data_ts: int,
        spread_bps: float,
        reason: str,
        bracket: tuple[float, float] | None = None,  # used by venues that hold the stop on the exchange
    ) -> tuple[Verdict, Fill | None]:
        pos = self.broker.position(instrument)
        v = self.risk.check_order(side, qty, ref_price, pos.qty, equity, ts, data_ts, spread_bps)
        if not v.allowed:
            return v, None
        fill = self.broker.market(ts, instrument, side, v.qty, ref_price, reason)
        self.risk.record_order()
        return v, fill
