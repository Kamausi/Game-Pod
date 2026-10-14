"""24/7 paper-trading loop.

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


def run_paper(instrument: str, timeframe: str, engine_kind: str, run_dir: Path, start_cash: float, poll_s: float = 15.0):
    run_dir.mkdir(parents=True, exist_ok=True)
    risk = RiskManager(RiskLimits(), run_dir / "KILL", run_dir / "risk_state.json")
    broker = _load_broker(run_dir / "broker.json", start_cash)
    journal = Journal(run_dir / "journal.jsonl")
    engine = build_engine(engine_kind, risk)
    if hasattr(engine, "on_escalate"):
        engine.on_escalate = lambda d: journal.write("escalation", int(time.time() * 1000), fast=d)
    agent = Agent(instrument, engine, broker, risk, PolicyParams(), Calibrator.load(run_dir / "calibration.json"), journal)
    feed = CryptoComFeed(instrument, timeframe)
    last_bar = 0
    errors = 0
    print(f"paper loop: {instrument} {timeframe} engine={engine_kind} dir={run_dir}  (touch {run_dir/'KILL'} to stop trading)")
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
