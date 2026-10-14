"""Bar-replay backtest through the exact same Agent code as the live loop.

Causality: the decision for bar i uses only bars <= i (as_of = close of bar i)
and is executed at the OPEN of bar i+1, with spread, slippage, and fees.
Stops/targets are checked against bar i+1's high/low after that.
"""
from __future__ import annotations

import json
import math
import random
from dataclasses import dataclass
from pathlib import Path

from .agent import Agent
from .broker import PaperBroker
from .engines import DecisionEngine
from .journal import Journal
from .policy import Calibrator, PolicyParams
from .risk import RiskLimits, RiskManager
from .types import Candle, OrderBook


@dataclass
class Result:
    bars: int
    start_equity: float
    end_equity: float
    total_return: float
    buy_hold_return: float
    max_drawdown: float
    trades: int
    fees: float
    win_rate: float
    killed: str | None

    def __str__(self) -> str:
        return "\n".join(f"{k:>16}: {v:.4f}" if isinstance(v, float) else f"{k:>16}: {v}" for k, v in self.__dict__.items())


def synthetic_candles(n: int, seed: int = 7, start_price: float = 80_000.0, bar_ms: int = 3_600_000) -> list[Candle]:
    """Regime-switching random walk. For plumbing tests only: it has no edge
    to find, so a 'profitable' result on it is a red flag, not a win."""
    rng = random.Random(seed)
    out, px, t = [], start_price, 1_700_000_000_000
    drift, vol = 0.0, 0.006
    for _ in range(n):
        if rng.random() < 0.02:
            drift = rng.choice([-0.0008, 0.0, 0.0, 0.0008])
            vol = rng.choice([0.004, 0.006, 0.012])
        o = px
        path = [o]
        for _ in range(4):
            path.append(path[-1] * math.exp(rng.gauss(drift / 4, vol / 2)))
        px = path[-1]
        out.append(Candle(t, t + bar_ms, o, max(path), min(path), px, rng.uniform(50, 150)))
        t += bar_ms
    return out


def load_candles(path: str | Path) -> list[Candle]:
    rows = json.loads(Path(path).read_text())
    return [Candle(r["open_ts"], r["close_ts"], r["open"], r["high"], r["low"], r["close"], r["volume"]) for r in rows]


def round_trips(fills) -> list[float]:
    """Net P&L (after fees) of each flat-to-flat round trip."""
    out, pos, pnl = [], 0.0, 0.0
    for f in fills:
        signed = f.qty if f.side == "buy" else -f.qty
        pnl += -signed * f.price - f.fee
        pos += signed
        if abs(pos) < 1e-12:
            out.append(pnl)
            pos, pnl = 0.0, 0.0
    return out


def run_backtest(
    candles: list[Candle],
    engine: DecisionEngine,
    run_dir: Path,
    instrument: str = "BTC_USD",
    start_cash: float = 10_000.0,
    spread_bps: float = 2.0,
    params: PolicyParams | None = None,
    limits: RiskLimits | None = None,
    warmup: int = 60,
) -> Result:
    run_dir.mkdir(parents=True, exist_ok=True)
    for f in ("journal.jsonl", "risk_state.json", "KILL"):
        (run_dir / f).unlink(missing_ok=True)
    risk = RiskManager(limits or RiskLimits(), run_dir / "KILL", run_dir / "risk_state.json")
    broker = PaperBroker(cash=start_cash)
    agent = Agent(
        instrument, engine, broker, risk, params or PolicyParams(), Calibrator(), Journal(run_dir / "journal.jsonl")
    )
    hs = spread_bps / 2e4
    peak, max_dd = start_cash, 0.0
    for i in range(warmup, len(candles) - 1):
        bar, nxt = candles[i], candles[i + 1]
        book = OrderBook(bar.close_ts, ((bar.close * (1 - hs), 10.0),), ((bar.close * (1 + hs), 10.0),))
        _, _, intent = agent.observe(bar.close_ts, candles[max(0, i - 199) : i + 1], book)
        o = nxt.open
        agent.act(intent, nxt.open_ts, o * (1 - hs), o * (1 + hs), bar.close_ts, spread_bps)
        agent.check_exits(nxt.close_ts, o, nxt.high, nxt.low, o * hs, nxt.close_ts, spread_bps)
        eq = agent.equity(nxt.close)
        peak = max(peak, eq)
        max_dd = max(max_dd, 1 - eq / peak)

    end = agent.equity(candles[-1].close)
    trips = round_trips(broker.fills)
    n = len(trips)
    wins = sum(p > 0 for p in trips)
    return Result(
        bars=len(candles) - warmup - 1,
        start_equity=start_cash,
        end_equity=end,
        total_return=end / start_cash - 1,
        buy_hold_return=candles[-1].close / candles[warmup].close - 1,
        max_drawdown=max_dd,
        trades=n,
        fees=sum(f.fee for f in broker.fills),
        win_rate=wins / n if n else 0.0,
        killed=risk.kill_reason(),
    )
