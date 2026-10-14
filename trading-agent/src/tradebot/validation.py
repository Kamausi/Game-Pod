"""Research workflow: explore on a training window, freeze, evaluate once.

Statistics:
- Sharpe is computed per bar from the equity curve (not annualised inside the
  test, so it composes with the Deflated Sharpe formula).
- Deflated Sharpe Ratio (Bailey & Lopez de Prado, 2014) gives the probability
  that the true Sharpe is above zero, after allowing for the number of
  configurations tried and for skewed / fat-tailed returns.
"""
from __future__ import annotations

import itertools
import math
import statistics
from dataclasses import asdict, fields, replace
from pathlib import Path

from .backtest import Result, run_backtest
from .engines import EscalatingEngine, RuleEngine
from .experiments import DataSnapshot, Register, Spec
from .policy import PolicyParams
from .risk import RiskLimits, RiskManager
from .types import Candle

EULER_GAMMA = 0.5772156649
N = statistics.NormalDist()


# ---------- statistics ----------
def bar_returns(curve: list[float]) -> list[float]:
    return [b / a - 1 for a, b in zip(curve, curve[1:]) if a > 0]


def sharpe(rets: list[float]) -> float:
    if len(rets) < 2:
        return 0.0
    sd = statistics.pstdev(rets)
    return statistics.fmean(rets) / sd if sd > 0 else 0.0


def moments(rets: list[float]) -> tuple[float, float]:
    """Skewness and (non-excess) kurtosis."""
    m, sd = statistics.fmean(rets), statistics.pstdev(rets)
    if sd == 0:
        return 0.0, 3.0
    z = [(r - m) / sd for r in rets]
    return statistics.fmean(x**3 for x in z), statistics.fmean(x**4 for x in z)


def expected_max_sharpe(n_trials: int, var_trial_sharpes: float) -> float:
    """Expected maximum Sharpe among n_trials true-zero strategies."""
    if n_trials < 2:
        return 0.0
    return math.sqrt(max(var_trial_sharpes, 0.0)) * (
        (1 - EULER_GAMMA) * N.inv_cdf(1 - 1 / n_trials) + EULER_GAMMA * N.inv_cdf(1 - 1 / (n_trials * math.e))
    )


def deflated_sharpe(sr: float, n_obs: int, skew: float, kurt: float, n_trials: int, var_trial_sharpes: float) -> float:
    sr0 = expected_max_sharpe(n_trials, var_trial_sharpes)
    denom = 1 - skew * sr + (kurt - 1) / 4 * sr * sr
    if n_obs < 2 or denom <= 0:
        return 0.0
    return N.cdf((sr - sr0) * math.sqrt(n_obs - 1) / math.sqrt(denom))


# ---------- running a spec ----------
def build(spec: Spec) -> tuple[PolicyParams, RiskLimits]:
    params = PolicyParams(**{**asdict(PolicyParams()), **spec.params})
    base = RiskLimits()
    limits = RiskLimits(**{**asdict(base), **spec.limits})
    for f in fields(RiskLimits):
        a, b = getattr(base, f.name), getattr(limits, f.name)
        looser = b < a if f.name == "escalate_below_conf" else b > a
        if looser:
            raise ValueError(f"research may only tighten risk limits; {f.name}={b} is looser than {a}")
    return params, limits


def run_spec(spec: Spec, candles: list[Candle], run_dir: Path) -> Result:
    if spec.engine != "rules":
        raise ValueError("research runs use the deterministic rules engine; model engines are evaluated on paper")
    params, limits = build(spec)
    risk = RiskManager(limits, run_dir / "_engine_KILL")
    return run_backtest(candles, EscalatingEngine(RuleEngine(), None, risk), run_dir, spec.instrument, params=params, limits=limits)


def summarize(res: Result) -> dict:
    rets = bar_returns(res.equity_curve)
    skew, kurt = moments(rets) if len(rets) > 2 else (0.0, 3.0)
    return {
        **res.summary(),
        "excess_vs_benchmark": res.total_return - res.benchmark_net_return,
        "sharpe_per_bar": sharpe(rets),
        "skew": skew,
        "kurtosis": kurt,
        "n_obs": len(rets),
    }


# ---------- the workflow ----------
def sweep(reg: Register, base: Spec, grid: dict[str, list], candles: list[Candle], snap: DataSnapshot,
          lo: int, hi: int, work: Path) -> list[dict]:
    """Run every combination on bars[lo:hi]; record every run, failures included."""
    window = candles[lo:hi]
    keys = sorted(grid)
    out = []
    for combo in itertools.product(*(grid[k] for k in keys)):
        spec = replace(base, params={**base.params, **dict(zip(keys, combo))})
        try:
            r = summarize(run_spec(spec, window, work / spec.id))
            out.append(reg.record_run("explore", spec, snap, snap.slice_note(lo, hi), r))
        except Exception as e:
            out.append(reg.record_run("explore", spec, snap, snap.slice_note(lo, hi), None, f"{type(e).__name__}: {e}"))
    return out


def trial_stats(reg: Register, family: str, data_sha: str) -> tuple[int, float]:
    """Number of distinct configurations tried and the variance of their Sharpes."""
    seen = {}
    for r in reg.family_trials(family, data_sha):
        if r["status"] == "ok":
            seen[r["candidate_id"]] = r["result"]["sharpe_per_bar"]
    srs = list(seen.values())
    return len(srs), statistics.pvariance(srs) if len(srs) > 1 else 0.0


def assess(reg: Register, rec: dict) -> dict:
    """Deflated Sharpe of one run, given everything tried in its family on its data."""
    r = rec["result"]
    n_trials, var_sr = trial_stats(reg, rec["family"], rec["data"]["sha256"])
    dsr = deflated_sharpe(r["sharpe_per_bar"], r["n_obs"], r["skew"], r["kurtosis"], max(n_trials, 1), var_sr)
    return {"n_trials": n_trials, "var_trial_sharpe": var_sr, "expected_max_sharpe_null": expected_max_sharpe(n_trials, var_sr),
            "deflated_sharpe_prob": dsr}


def plateau(results: list[dict], key: str = "excess_vs_benchmark") -> float:
    ok = [r["result"][key] for r in results if r["status"] == "ok"]
    return sum(v > 0 for v in ok) / len(ok) if ok else 0.0


def final_evaluation(reg: Register, candidate_id: str, candles: list[Candle], snap: DataSnapshot,
                     lo: int, hi: int, work: Path) -> dict:
    """One shot, on data the exploration never touched."""
    reg.assert_final_allowed(candidate_id)
    spec = reg.frozen_spec(candidate_id)
    for r in reg.records():
        if r.get("kind") == "explore" and r["family"] == spec.name and r["data"]["sha256"] == snap.sha256:
            a, b = (int(x) for x in r["split"].split("[")[1].split("]")[0].split(":"))
            if a < hi and lo < b:
                raise ValueError(f"holdout bars[{lo}:{hi}] overlap exploration window bars[{a}:{b}] for this family")
    try:
        res = summarize(run_spec(spec, candles[lo:hi], work / ("final_" + candidate_id)))
        return reg.record_run("final", spec, snap, snap.slice_note(lo, hi), res)
    except Exception as e:
        return reg.record_run("final", spec, snap, snap.slice_note(lo, hi), None, f"{type(e).__name__}: {e}")
