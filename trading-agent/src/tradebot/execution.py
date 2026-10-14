"""Order management for the paper (and any future live) runner.

- Idempotency: every order carries a client id derived from the decision
  (instrument, decision time, action). The id is written to a persistent
  ledger *before* the order is sent, so a retry, a duplicate event or a
  restart can never send it twice.
- Partial fills are accepted as-is; the remainder is cancelled, never chased.
- Rejections are logged and not retried on the same bar; repeated rejections
  trip the kill switch.
- Restart recovery: the ledger is compared with the venue's actual position.
  Any mismatch, or an order whose outcome is unknown (sent, then crash),
  trips the kill switch until a human reconciles it.

`SimVenue` is a paper venue with fault injection, used by the paper loop and
by the operational gate (`ops_gate.py`).
"""
from __future__ import annotations

import json
from dataclasses import asdict, dataclass
from pathlib import Path

from .broker import PaperBroker
from .risk import RiskManager, Verdict
from .types import Fill

MAX_CONSECUTIVE_REJECTS = 3


@dataclass
class VenueReply:
    status: str  # "filled" | "partial" | "rejected"
    filled_qty: float
    fill: Fill | None
    reason: str = ""


class SimVenue:
    """Paper venue. Faults are injected by tests and the ops gate."""

    def __init__(self, broker: PaperBroker, liquidity_qty: float | None = None):
        self.broker = broker
        self.liquidity_qty = liquidity_qty  # max qty fillable per order at the touch
        self.reject_next = 0
        self.sent: list[str] = []

    def place(self, client_id: str, ts: int, instrument: str, side: str, qty: float, ref: float, reason: str) -> VenueReply:
        self.sent.append(client_id)
        if self.reject_next > 0:
            self.reject_next -= 1
            return VenueReply("rejected", 0.0, None, "venue rejected the order")
        q = qty if self.liquidity_qty is None else min(qty, self.liquidity_qty)
        fill = self.broker.market(ts, instrument, side, q, ref, reason)
        return VenueReply("filled" if q >= qty - 1e-12 else "partial", q, fill)

    def position_qty(self, instrument: str) -> float:
        return self.broker.position(instrument).qty


class OrderManager:
    """Drop-in for GuardedBroker with idempotency, a persistent ledger and recovery."""

    def __init__(self, venue: SimVenue, risk: RiskManager, ledger_path: Path):
        self.venue = venue
        self.broker = venue.broker  # Agent reads positions through this
        self.risk = risk
        self.ledger_path = Path(ledger_path)
        self.ledger: dict[str, dict] = {}
        self.consecutive_rejects = 0
        if self.ledger_path.exists():
            for line in self.ledger_path.read_text().splitlines():
                if line.strip():
                    rec = json.loads(line)
                    self.ledger[rec["client_id"]] = {**self.ledger.get(rec["client_id"], {}), **rec}

    def _write(self, rec: dict):
        self.ledger[rec["client_id"]] = {**self.ledger.get(rec["client_id"], {}), **rec}
        self.ledger_path.parent.mkdir(parents=True, exist_ok=True)
        with self.ledger_path.open("a") as f:
            f.write(json.dumps(rec, default=str) + "\n")

    @staticmethod
    def client_id(instrument: str, ts: int, side: str, reason: str) -> str:
        kind = "open" if reason in ("entry",) or reason.startswith("p_raw") else "close"
        return f"{instrument}:{ts}:{kind}:{side}"

    def submit(self, ts, instrument, side, qty, ref_price, equity, data_ts, spread_bps, reason, client_id: str | None = None):
        cid = client_id or self.client_id(instrument, ts, side, reason)
        if cid in self.ledger:
            return Verdict(False, 0.0, (f"duplicate client id {cid}; not resent",)), None
        pos = self.venue.position_qty(instrument)
        v = self.risk.check_order(side, qty, ref_price, pos, equity, ts, data_ts, spread_bps)
        if not v.allowed:
            self._write({"client_id": cid, "ts": ts, "side": side, "qty": qty, "state": "blocked", "reasons": list(v.reasons)})
            return v, None
        self._write({"client_id": cid, "ts": ts, "side": side, "qty": v.qty, "state": "sent"})  # before sending
        reply = self.venue.place(cid, ts, instrument, side, v.qty, ref_price, reason)
        self._write({"client_id": cid, "state": reply.status, "filled_qty": reply.filled_qty,
                     "fill": asdict(reply.fill) if reply.fill else None, "venue_reason": reply.reason})
        self.risk.record_order()
        if reply.status == "rejected":
            self.consecutive_rejects += 1
            if self.consecutive_rejects >= MAX_CONSECUTIVE_REJECTS and not self.risk.is_killed():
                self.risk.kill(f"{self.consecutive_rejects} consecutive order rejections")
            return Verdict(False, 0.0, (reply.reason,)), None
        self.consecutive_rejects = 0
        note = ("partial fill; remainder cancelled",) if reply.status == "partial" else ()
        return Verdict(True, reply.filled_qty, v.reasons + note), reply.fill

    def expected_position(self, instrument: str) -> float:
        """Position implied by the ledger alone."""
        qty = 0.0
        for rec in self.ledger.values():
            if rec.get("state") in ("filled", "partial") and rec.get("fill") and rec["fill"]["instrument"] == instrument:
                qty += rec["filled_qty"] if rec["side"] == "buy" else -rec["filled_qty"]
        return qty

    def recover(self, instrument: str) -> dict:
        """Run at startup, before any new order."""
        unknown = [cid for cid, r in self.ledger.items() if r.get("state") == "sent"]
        expected = self.expected_position(instrument)
        actual = self.venue.position_qty(instrument)
        problems = []
        if unknown:
            problems.append(f"orders with unknown outcome (sent, no reply recorded): {unknown}")
        if abs(expected - actual) > 1e-9:
            problems.append(f"position mismatch: ledger says {expected}, venue says {actual}")
        if problems and not self.risk.is_killed():
            self.risk.kill("restart reconciliation failed: " + "; ".join(problems))
        return {"ok": not problems, "problems": problems, "expected": expected, "actual": actual}
