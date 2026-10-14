"""Experiment register: every research run is recorded, failures included.

Three rules this module enforces:

1. Every backtest that research runs is appended to an append-only register
   (`runs/research/register.jsonl`) with its exact spec, data fingerprint,
   engine version and assumptions. Nothing is deleted, so the number of
   things tried is always known (Deflated Sharpe needs it).
2. A candidate is a *frozen* spec. Its id is the hash of the spec, so
   changing any parameter makes a different candidate.
3. A candidate gets exactly one final evaluation on the holdout. A second
   attempt is refused: looking again after tweaking is not out-of-sample.
"""
from __future__ import annotations

import hashlib
import json
import subprocess
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path

from . import __version__
from .types import Candle

SRC = Path(__file__).resolve().parent


class FrozenError(RuntimeError):
    pass


def _h(obj) -> str:
    return hashlib.sha256(json.dumps(obj, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def engine_version() -> dict:
    """Package version, git commit if available, and a hash of the engine source."""
    src_hash = hashlib.sha256()
    for p in sorted(SRC.glob("*.py")):
        src_hash.update(p.name.encode())
        src_hash.update(p.read_bytes())
    commit = None
    try:
        commit = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=SRC, capture_output=True, text=True, timeout=5
        ).stdout.strip() or None
    except Exception:
        pass
    return {"package": __version__, "git_commit": commit, "source_sha256": src_hash.hexdigest()[:16]}


@dataclass(frozen=True)
class Spec:
    """Everything that defines a strategy. Hash = candidate id."""

    name: str  # family name: all variations of one idea share it
    engine: str = "rules"
    instrument: str = "BTC_USD"
    timeframe: str = "1h"
    params: dict = field(default_factory=dict)  # PolicyParams overrides
    limits: dict = field(default_factory=dict)  # RiskLimits overrides (may only tighten)

    @property
    def id(self) -> str:
        return "cand_" + _h(asdict(self))[:12]


@dataclass(frozen=True)
class DataSnapshot:
    source: str
    instrument: str
    timeframe: str
    first_close_ts: int
    last_close_ts: int
    bars: int
    sha256: str
    note: str = ""

    @classmethod
    def of(cls, candles: list[Candle], source: str, instrument: str, timeframe: str, note: str = "") -> "DataSnapshot":
        rows = [[c.open_ts, c.close_ts, c.open, c.high, c.low, c.close, c.volume] for c in candles]
        return cls(source, instrument, timeframe, candles[0].close_ts, candles[-1].close_ts, len(candles), _h(rows), note)

    def slice_note(self, lo: int, hi: int) -> str:
        return f"bars[{lo}:{hi}] of {self.sha256[:12]}"


ASSUMPTIONS = {
    "decision_time": "close of bar t (only bars that closed at or before t are visible)",
    "fill_time": "open of bar t+1; signal observation and fill are different instants",
    "fill_price": "touch (mid +/- half spread) worsened by slippage",
    "spread_bps": 2.0,
    "slippage_bps_per_side": 2.0,
    "fee_bps_per_side": 6.0,
    "stops": "checked on bar high/low; if stop and target both touched, stop assumed first; gaps fill at the open",
    "liquidity": "unlimited at the touch in backtests (a known optimism; see ops gate for partial fills)",
    "dividends_funding": "none modelled (spot crypto); add funding for perps, total return for equities",
    "benchmark": "buy-and-hold under the same execution model and costs (benchmark_net_return)",
}


class Register:
    def __init__(self, root: str | Path = "runs/research"):
        self.root = Path(root)
        self.root.mkdir(parents=True, exist_ok=True)
        self.path = self.root / "register.jsonl"

    def records(self) -> list[dict]:
        if not self.path.exists():
            return []
        return [json.loads(l) for l in self.path.read_text().splitlines() if l.strip()]

    def append(self, rec: dict) -> dict:
        rec = {"recorded_at": int(time.time() * 1000), **rec}
        with self.path.open("a") as f:
            f.write(json.dumps(rec, sort_keys=True) + "\n")
        return rec

    def save_data(self, candles: list[Candle], snap: DataSnapshot) -> Path:
        """Keep the exact bars, so any run can be reproduced byte for byte."""
        d = self.root / "data"
        d.mkdir(exist_ok=True)
        p = d / f"{snap.sha256[:16]}.json"
        if not p.exists():
            p.write_text(json.dumps([c.__dict__ for c in candles]))
        return p

    def record_run(self, kind: str, spec: Spec, data: DataSnapshot, split: str, result: dict | None, error: str | None = None) -> dict:
        ev = engine_version()
        body = {
            "kind": kind,  # "explore" | "final"
            "spec": asdict(spec),
            "candidate_id": spec.id,
            "family": spec.name,
            "data": asdict(data),
            "split": split,
            "engine_version": ev,
            "assumptions": ASSUMPTIONS,
        }
        run_id = "run_" + _h(body)[:12]
        return self.append({"run_id": run_id, **body, "result": result, "error": error, "status": "failed" if error else "ok"})

    def family_trials(self, family: str, data_sha: str) -> list[dict]:
        """Every exploration run of this idea on this data, winners and losers."""
        return [r for r in self.records() if r["kind"] == "explore" and r["family"] == family and r["data"]["sha256"] == data_sha]

    # --- freeze / final evaluation ---
    def freeze(self, spec: Spec, reason: str) -> dict:
        if any(r["kind"] == "freeze" and r["candidate_id"] == spec.id for r in self.records()):
            raise FrozenError(f"{spec.id} is already frozen")
        return self.append({"kind": "freeze", "candidate_id": spec.id, "family": spec.name, "spec": asdict(spec), "reason": reason})

    def frozen_spec(self, candidate_id: str) -> Spec:
        for r in self.records():
            if r["kind"] == "freeze" and r["candidate_id"] == candidate_id:
                return Spec(**r["spec"])
        raise FrozenError(f"{candidate_id} is not frozen; freeze it before a final evaluation")

    def assert_final_allowed(self, candidate_id: str) -> None:
        self.frozen_spec(candidate_id)
        if any(r["kind"] == "final" and r["candidate_id"] == candidate_id for r in self.records()):
            raise FrozenError(
                f"{candidate_id} already had its one final evaluation. Changing anything makes a new candidate; "
                "re-running the same one on the same holdout is not out-of-sample evidence."
            )

    def find(self, run_id: str) -> dict:
        for r in self.records():
            if r.get("run_id") == run_id:
                return r
        raise KeyError(run_id)
