"""24/7 paper-trading loop.

- Starts only from an approved release (or `--dev`, which is labelled as such
  in the journal and can never be promoted).
- Reconciles the order ledger with the venue before placing anything.
- Decides once per closed bar (never on a forming bar).
- Between bars, polls the book every `poll_s` to enforce stops/targets.
- Any exception -> no trade this tick; N consecutive failures trips the kill switch.
- Paper broker state is checkpointed so a restart resumes the same book.
"""
from __future__ import annotations

import json
import time
from pathlib import Path

from .agent import Agent
from .broker import PaperBroker, Position
from .engines import build_engine
from .execution import OrderManager, SimVenue
from .feed import CryptoComFeed
from .journal import Journal
from .policy import Calibrator, PolicyParams
from .risk import RiskLimits, RiskManager

MAX_CONSECUTIVE_ERRORS = 5


def _save_broker(b: PaperBroker, path: Path):
    path.write_text(json.dumps({"cash": b.cash, "positions": {k: v.__dict__ for k, v in b.positions.items()}}))


def _load_broker(path: Path, start_cash: float) -> PaperBroker:
    if not path.exists():
        return PaperBroker(cash=start_cash)
    d = json.loads(path.read_text())
    return PaperBroker(cash=d["cash"], positions={k: Position(**v) for k, v in d["positions"].items()})


def run_paper(instrument: str, timeframe: str, engine_kind: str, run_dir: Path, start_cash: float,
              params: PolicyParams, limits: RiskLimits, release: dict | None, poll_s: float = 15.0, exechub: str | None = None,
              reviewer: str = "code", reviewer_mode: str = "block"):
    run_dir.mkdir(parents=True, exist_ok=True)
    risk = RiskManager(limits, run_dir / "KILL", run_dir / "risk_state.json")
    broker = _load_broker(run_dir / "broker.json", start_cash)
    journal = Journal(run_dir / "journal.jsonl")
    om = OrderManager(SimVenue(broker), risk, run_dir / "orders.jsonl")
    rec = om.recover(instrument)
    journal.write("startup", int(time.time() * 1000), release=release or "DEV (not promotable)", reconciliation=rec)
    if not rec["ok"]:
        print("Restart reconciliation failed; kill switch is on until you resolve it:\n  " + "\n  ".join(rec["problems"]))
    engine = build_engine(engine_kind, risk)
    if hasattr(engine, "on_escalate"):
        engine.on_escalate = lambda d: journal.write("escalation", int(time.time() * 1000), fast=d)
    guard = om
    if exechub:
        from .hubclient import HubClient, HubGuard

        guard = HubGuard(HubClient(exechub), broker, risk)
        print(f"routing orders through the Go execution hub at {exechub}")
    from . import runlog
    from .order_review import build_reviewer

    rev = build_reviewer(reviewer, run_dir / "reviewer_scores.json", reviewer_mode)
    agent = Agent(instrument, engine, broker, risk, params, Calibrator.load(run_dir / "calibration.json"), journal, guard=guard,
                  reviewer=rev)
    runlog.append(run_dir, done=[f"paper session started: {instrument} {timeframe} engine={engine_kind} reviewer={reviewer}/{reviewer_mode}"
                                 f" exits={params.exit_mode} ({params.fixed_stop_pct:.1%}/{params.fixed_take_pct:.1%})"
                                 if params.exit_mode == "fixed" else f"paper session started: {instrument} engine={engine_kind}"],
                  decided=[f"release {release['candidate_id']}" if release else "DEV mode: not promotable"],
                  open_items=rec["problems"] or None)
    feed = CryptoComFeed(instrument, timeframe)
    last_bar = 0
    errors = 0
    label = f"release {release['candidate_id']}" if release else "DEV MODE"
    print(f"paper loop [{label}]: {instrument} {timeframe} engine={engine_kind} dir={run_dir}  (touch {run_dir/'KILL'} to stop trading)")
    while True:
        now = int(time.time() * 1000)
        try:
            book = feed.book()
            now = max(now, book.ts)  # decision time is after receipt; absorbs clock skew
            spread = (book.best_ask - book.best_bid) / book.mid * 1e4
            m = book.mid
            hs = (book.best_ask - book.best_bid) / 2
            agent.check_exits(now, m, m, m, hs, book.ts, spread)
            bar_close = now - now % feed.bar_ms
            if bar_close > last_bar:
                candles = feed.closed_candles(now)
                if candles and candles[-1].close_ts == bar_close:
                    _, d, intent = agent.observe(now, candles, book)
                    agent.act(intent, now, book.best_bid, book.best_ask, book.ts, spread)
                    last_bar = bar_close
                    print(f"{time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())} {d.source} {d.regime}/{d.direction} "
                          f"conf={d.direction_confidence:.2f} -> {intent.action} ({intent.reason}) eq={agent.equity(m):.2f}")
            _save_broker(broker, run_dir / "broker.json")
            errors = 0
        except KeyboardInterrupt:
            raise
        except Exception as e:
            errors += 1
            journal.write("error", now, error=f"{type(e).__name__}: {e}"[:500], consecutive=errors)
            if errors >= MAX_CONSECUTIVE_ERRORS and not risk.is_killed():
                risk.kill(f"{errors} consecutive loop errors; last: {type(e).__name__}")
        time.sleep(poll_s)
