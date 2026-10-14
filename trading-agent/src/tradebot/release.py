"""Releases: the only bridge from research to the running trader.

Separation of duties, in code:
- Research (sweep / freeze / final / evidence) never reads or writes
  `config/risk_limits.json` or `releases/`, and holds no broker credentials.
- A release names one frozen candidate, the exact hash of the risk-limits
  file, and the engine source hash. `approve` requires a passed final
  evaluation, a reconciled evidence package for it, and a passed operational
  gate for the current engine source, then an interactive typed confirmation.
- The runner refuses to start unless the release, the limits file and the
  running engine source all still match. Changing code (including a model or
  prompt change), limits, or the strategy invalidates the release.

Code can't stop someone who controls the whole machine. In production, run the
trader as a separate OS user who owns `config/` and `releases/` read-only for
everyone else, and keep broker keys only in that user's environment.
"""
from __future__ import annotations

import hashlib
import json
import sys
import time
from dataclasses import asdict
from pathlib import Path
from typing import Callable

from .experiments import Register, Spec, engine_version
from .risk import RiskLimits

DEFAULT_LIMITS_FILE = Path("config/risk_limits.json")
DEFAULT_RELEASE_FILE = Path("releases/approved.json")


class ReleaseError(RuntimeError):
    pass


def file_sha(p: Path) -> str:
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def load_limits(path: Path = DEFAULT_LIMITS_FILE) -> RiskLimits:
    """RiskLimits validates hard caps (15% drawdown max, no leverage) in code."""
    if not Path(path).exists():
        raise ReleaseError(f"{path} not found; create it with `tradebot init-limits`")
    return RiskLimits(**json.loads(Path(path).read_text()))


def init_limits(path: Path = DEFAULT_LIMITS_FILE) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        path.write_text(json.dumps(asdict(RiskLimits()), indent=2) + "\n")
    return path


def _tty_confirm(candidate_id: str) -> bool:
    if not sys.stdin.isatty():
        raise ReleaseError("approval needs an interactive terminal; it can't be scripted or piped")
    typed = input(f"Type the candidate id ({candidate_id}) to approve it for trading: ").strip()
    return typed == candidate_id


def approve(reg: Register, candidate_id: str, approver: str, limits_path: Path = DEFAULT_LIMITS_FILE,
            release_path: Path = DEFAULT_RELEASE_FILE, confirm: Callable[[str], bool] = _tty_confirm,
            accept_weak_evidence: bool = False) -> dict:
    spec = reg.frozen_spec(candidate_id)
    finals = [r for r in reg.records() if r.get("kind") == "final" and r["candidate_id"] == candidate_id]
    if not finals or finals[-1]["status"] != "ok":
        raise ReleaseError("no successful final evaluation for this candidate")
    final = finals[-1]
    ev = reg.root / "evidence" / final["run_id"] / "evidence.json"
    if not ev.exists():
        raise ReleaseError(f"no evidence package for {final['run_id']}; run `tradebot evidence --run {final['run_id']}`")
    pkg = json.loads(ev.read_text())
    if not pkg["reconciliation"]["reconciled"]:
        raise ReleaseError("evidence package did not reconcile")
    weak = []
    if pkg["result"]["excess_vs_benchmark"] <= 0:
        weak.append(f"holdout excess vs benchmark is {pkg['result']['excess_vs_benchmark']:+.2%}")
    if pkg["statistics"]["deflated_sharpe_prob"] < 0.95:
        weak.append(f"deflated Sharpe probability is {pkg['statistics']['deflated_sharpe_prob']:.3f} (< 0.95)")
    if weak and not accept_weak_evidence:
        raise ReleaseError("weak evidence: " + "; ".join(weak) + ". Pass --accept-weak-evidence to approve for paper anyway")
    eng = engine_version()
    gate = reg.root / "opsgate.json"
    if not gate.exists():
        raise ReleaseError("operational gate has not been run; run `tradebot opsgate`")
    g = json.loads(gate.read_text())
    if not g["passed"] or g["engine_version"]["source_sha256"] != eng["source_sha256"]:
        raise ReleaseError("operational gate not passed for the current engine source; re-run `tradebot opsgate`")
    load_limits(limits_path)  # validates against hard caps
    if not confirm(candidate_id):
        raise ReleaseError("confirmation did not match; nothing approved")
    rel = {
        "candidate_id": candidate_id,
        "spec": asdict(spec),
        "final_run_id": final["run_id"],
        "limits_file": str(limits_path),
        "limits_sha256": file_sha(limits_path),
        "engine_version": eng,
        "approved_by": approver,
        "weak_evidence_accepted": weak,
        "approved_at": int(time.time() * 1000),
    }
    release_path.parent.mkdir(parents=True, exist_ok=True)
    release_path.write_text(json.dumps(rel, indent=2) + "\n")
    return rel


def verify_release(release_path: Path = DEFAULT_RELEASE_FILE) -> tuple[Spec, RiskLimits, dict]:
    if not Path(release_path).exists():
        raise ReleaseError(f"no approved release at {release_path}")
    rel = json.loads(Path(release_path).read_text())
    lp = Path(rel["limits_file"])
    if file_sha(lp) != rel["limits_sha256"]:
        raise ReleaseError("risk limits file changed since approval; a new release is required")
    if engine_version()["source_sha256"] != rel["engine_version"]["source_sha256"]:
        raise ReleaseError("engine source changed since approval (code, prompt or model wiring); re-run opsgate and re-approve")
    spec = Spec(**rel["spec"])
    if spec.id != rel["candidate_id"]:
        raise ReleaseError("release spec does not hash to its candidate id")
    return spec, load_limits(lp), rel
