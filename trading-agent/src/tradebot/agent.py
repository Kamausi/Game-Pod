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
        guard=None,
        reviewer=None,
    ):
        self.instrument = instrument
        self.engine = engine
        self.broker = broker
        # The only order path. Defaults to GuardedBroker; the paper loop passes an
        # OrderManager (idempotent client ids, persistent ledger, recovery).
        self.guard = guard or GuardedBroker(broker, risk)
        self.risk = risk
        self.params = params
        self.calibrator = calibrator
        self.journal = journal
        self.state = StateEngine()
        self.reviewer = reviewer  # order_review.OrderReviewer: second check on every entry
        self._last: tuple[Snapshot, Decision] | None = None

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
        trace = getattr(self.engine, "last_trace", None) if not self.risk.is_killed() else None
        # Exactly what the model saw and said, so a bad trade can be traced to the model, the prompt or the data.
        self.journal.write(
            "decision", as_of, snapshot=snap, decision=d, intent=intent, equity=self.equity(book.mid), trace=trace
        )
        self._last = (snap, d)
        return snap, d, intent

    def order_state(self, intent: Intent) -> dict:
        snap, d = self._last
        return {"instrument": self.instrument, "side": intent.side, "size_frac": intent.size_frac, "stop_pct": intent.stop_pct,
                "take_pct": intent.take_pct, "atr_pct": snap.atr_pct, "rv_ratio": snap.rv_ratio, "rv_24": snap.rv_24,
                "spread_bps": snap.spread_bps, "direction_confidence": d.direction_confidence, "p_win": d.p_win,
                "regime": d.regime, "drawdown": snap.drawdown, "mid": snap.mid}

    def act(self, intent: Intent, ts: int, bid: float, ask: float, data_ts: int, spread_bps: float):
        pos = self.broker.position(self.instrument)
        mid = (bid + ask) / 2
        eq = self.equity(mid)
        if intent.action == "exit" and pos.qty:
            self._close(ts, bid, ask, data_ts, spread_bps, intent.reason)
        elif intent.action == "enter" and not pos.qty:
            if self.reviewer is not None and self._last is not None:
                o = self.order_state(intent)
                rv = self.reviewer.review(o)
                self.journal.write("review", ts, order=o, verdict=rv)
                if rv.blocked:
                    return
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
