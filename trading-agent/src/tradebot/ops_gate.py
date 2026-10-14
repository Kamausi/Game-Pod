"""Operational gate: fault scenarios the runner must survive before any release.

This tests machinery, not profitability. Each scenario runs against a fresh
paper venue with faults injected, and the whole gate is recorded with the
engine source hash it ran against. A code change invalidates it.
"""
from __future__ import annotations

import json
import tempfile
import time
from pathlib import Path

from .broker import PaperBroker
from .execution import OrderManager, SimVenue
from .experiments import Register, engine_version
from .risk import RiskLimits, RiskManager

INST, PX, EQ = "BTC_USD", 80_000.0, 10_000.0


def _setup(tmp: Path, liquidity=None):
    tmp.mkdir(parents=True, exist_ok=True)
    risk = RiskManager(RiskLimits(), tmp / "KILL", tmp / "risk.json")
    risk.update(EQ, 0)
    broker = PaperBroker(cash=EQ)
    venue = SimVenue(broker, liquidity)
    return risk, broker, venue, OrderManager(venue, risk, tmp / "orders.jsonl")


def _buy(om, ts=1000, qty=0.01, data_ts=None, cid=None):
    return om.submit(ts, INST, "buy", qty, PX, EQ, data_ts if data_ts is not None else ts, 2.0, "entry", client_id=cid)


def duplicate_orders(tmp: Path) -> str | None:
    risk, broker, venue, om = _setup(tmp)
    _buy(om, cid="X:1")
    v, f = _buy(om, cid="X:1")
    if f is not None or len(venue.sent) != 1:
        return "duplicate client id was sent twice"
    om2 = OrderManager(venue, risk, tmp / "orders.jsonl")  # restart
    v, f = _buy(om2, cid="X:1")
    if f is not None or len(venue.sent) != 1:
        return "duplicate was resent after restart"
    return None


def partial_fill(tmp: Path) -> str | None:
    risk, broker, venue, om = _setup(tmp, liquidity=0.005)
    v, f = _buy(om, qty=0.02)
    if f is None or abs(broker.position(INST).qty - 0.005) > 1e-12:
        return f"expected a 0.005 partial fill, position is {broker.position(INST).qty}"
    if len(venue.sent) != 1:
        return "remainder was chased with extra orders"
    if om.ledger["BTC_USD:1000:open:buy"]["state"] != "partial":
        return "ledger did not record the partial"
    return None


def stale_data(tmp: Path) -> str | None:
    risk, broker, venue, om = _setup(tmp)
    v, f = _buy(om, ts=10_000_000, data_ts=0)
    if f is not None or venue.sent:
        return "order sent on stale data"
    return None


def rejected_orders(tmp: Path) -> str | None:
    risk, broker, venue, om = _setup(tmp)
    venue.reject_next = 3
    for k in range(3):
        _buy(om, ts=1000 + k)
    if not risk.is_killed():
        return "three consecutive rejects did not trip the kill switch"
    if broker.position(INST).qty != 0:
        return "a rejected order changed the position"
    return None


def restart_recovery(tmp: Path) -> str | None:
    risk, broker, venue, om = _setup(tmp)
    _buy(om, cid="A")
    if not om.recover(INST)["ok"]:
        return "clean state failed reconciliation"
    # crash after send: a 'sent' record with no reply
    om._write({"client_id": "B", "ts": 2000, "side": "buy", "qty": 0.01, "state": "sent"})
    rep = OrderManager(venue, risk, tmp / "orders.jsonl").recover(INST)
    if rep["ok"] or not risk.is_killed():
        return "unknown-outcome order did not stop trading"
    # position drift between ledger and venue
    risk2, broker2, venue2, om2 = _setup(tmp / "drift")
    _buy(om2, cid="C")
    broker2.market(3000, INST, "buy", 0.01, PX, "untracked")  # someone traded outside the system
    rep2 = OrderManager(venue2, risk2, tmp / "drift" / "orders.jsonl").recover(INST)
    if rep2["ok"] or not risk2.is_killed():
        return "position mismatch did not stop trading"
    return None


def kill_switch(tmp: Path) -> str | None:
    risk, broker, venue, om = _setup(tmp)
    _buy(om, cid="E")
    t0 = time.perf_counter()
    (tmp / "KILL").write_text("operator")
    v, f = _buy(om, ts=1500, cid="F")
    ms = (time.perf_counter() - t0) * 1000
    if f is not None:
        return "new order accepted with kill switch on"
    v, f = om.submit(1600, INST, "sell", broker.position(INST).qty, PX, EQ, 1600, 2.0, "kill switch", client_id="G")
    if f is None or broker.position(INST).qty != 0:
        return "could not flatten with kill switch on"
    if ms > 50:
        return f"kill switch took {ms:.1f} ms to take effect"
    return None


def limits_cannot_loosen(tmp: Path) -> str | None:
    for bad in ({"max_drawdown": 0.30}, {"max_position_frac": 2.0}):
        try:
            RiskLimits(**bad)
            return f"RiskLimits accepted {bad}"
        except ValueError:
            pass
    return None


SCENARIOS = [duplicate_orders, partial_fill, stale_data, rejected_orders, restart_recovery, kill_switch, limits_cannot_loosen]


def run_gate(reg: Register | None = None) -> dict:
    results = {}
    for s in SCENARIOS:
        with tempfile.TemporaryDirectory() as d:
            try:
                err = s(Path(d))
            except Exception as e:  # a crash is a failure, not a pass
                err = f"crashed: {type(e).__name__}: {e}"
        results[s.__name__] = {"passed": err is None, "detail": err or "ok"}
    out = {"passed": all(r["passed"] for r in results.values()), "scenarios": results,
           "engine_version": engine_version(), "ran_at": int(time.time() * 1000)}
    if reg is not None:
        (reg.root / "opsgate.json").write_text(json.dumps(out, indent=2) + "\n")
    return out
