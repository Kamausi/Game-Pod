"""One agent, one instrument. Shared by the backtest and the live paper loop,
so what you backtest is exactly the code that runs.
"""
from __future__ import annotations

from .broker import GuardedBroker, PaperBroker
from .engines import DecisionEngine
from .journal import Journal
from .policy import Calibrator, Intent, PolicyParams, decide
from .risk import RiskManager
from .schema import Decision
from .state import PortfolioView, Snapshot, StateEngine
from .types import Candle, OrderBook


class Agent:
    def __init__(
        self,
        instrument: str,
        engine: DecisionEngine,
        broker: PaperBroker,
        risk: RiskManager,
        params: PolicyParams,
        calibrator: Calibrator,
        journal: Journal,
    ):
        self.instrument = instrument
        self.engine = engine
        self.broker = broker
        self.guard = GuardedBroker(broker, risk)
        self.risk = risk
        self.params = params
        self.calibrator = calibrator
        self.journal = journal
        self.state = StateEngine()

    def equity(self, mark: float) -> float:
        return self.broker.equity({self.instrument: mark})

    def portfolio(self, mark: float) -> PortfolioView:
        pos = self.broker.position(self.instrument)
        eq = self.equity(mark)
        return PortfolioView(
            position_qty=pos.qty,
            avg_entry=pos.avg_entry,
            equity=eq,
            peak_equity=max(self.risk.peak_equity, eq),
            day_start_equity=self.risk.day_start_equity or eq,
        )

    def observe(
        self, as_of: int, candles: list[Candle], book: OrderBook
    ) -> tuple[Snapshot, Decision, Intent]:
        self.risk.update(self.equity(book.mid), as_of)
        snap = self.state.snapshot(self.instrument, as_of, candles, book, self.portfolio(book.mid))
        pos = self.broker.position(self.instrument)
        if self.risk.is_killed():
            d = Decision.hold(f"kill switch: {self.risk.kill_reason()}")
            intent = Intent("exit", reason="kill switch") if pos.qty else Intent("hold", reason="killed")
        else:
            d = self.engine.decide(snap)
            intent = decide(d, snap, pos.side, self.params, self.calibrator)
        self.journal.write(
            "decision", as_of, snapshot=snap, decision=d, intent=intent, equity=self.equity(book.mid)
        )
        return snap, d, intent

    def act(self, intent: Intent, ts: int, bid: float, ask: float, data_ts: int, spread_bps: float):
        pos = self.broker.position(self.instrument)
        mid = (bid + ask) / 2
        eq = self.equity(mid)
        if intent.action == "exit" and pos.qty:
            self._close(ts, bid, ask, data_ts, spread_bps, intent.reason)
        elif intent.action == "enter" and not pos.qty:
            side = "buy" if intent.side == "long" else "sell"
            ref = ask if side == "buy" else bid
            qty = intent.size_frac * eq / ref
            v, fill = self.guard.submit(
                ts, self.instrument, side, qty, ref, eq, data_ts, spread_bps, intent.reason
            )
            self.journal.write("order", ts, side=side, qty=qty, verdict=v, fill=fill)
            if fill:
                sgn = 1 if side == "buy" else -1
                pos.stop = fill.price * (1 - sgn * intent.stop_pct)
                pos.take = fill.price * (1 + sgn * intent.take_pct)

    def check_exits(self, ts: int, o: float, h: float, l: float, half_spread: float, data_ts: int, spread_bps: float):
        """Stop / take-profit. If both are touched in one bar, assume the stop
        filled first (conservative). Gaps fill at the open, not the stop."""
        pos = self.broker.position(self.instrument)
        if not pos.qty:
            return
        if pos.qty > 0:
            if l <= pos.stop:
                px = min(o, pos.stop)
            elif h >= pos.take:
                px = max(o, pos.take)
            else:
                return
        else:
            if h >= pos.stop:
                px = max(o, pos.stop)
            elif l <= pos.take:
                px = min(o, pos.take)
            else:
                return
        why = "stop" if (pos.qty > 0) == (px <= pos.avg_entry) else "take"
        self._close(ts, px - half_spread, px + half_spread, data_ts, spread_bps, why)

    def _close(self, ts, bid, ask, data_ts, spread_bps, reason):
        pos = self.broker.position(self.instrument)
        side = "sell" if pos.qty > 0 else "buy"
        ref = bid if side == "sell" else ask
        eq = self.equity((bid + ask) / 2)
        v, fill = self.guard.submit(
            ts, self.instrument, side, abs(pos.qty), ref, eq, data_ts, spread_bps, reason
        )
        self.journal.write("order", ts, side=side, qty=abs(pos.qty), verdict=v, fill=fill)
