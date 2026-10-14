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
from .engines import EnsembleEngine, EscalatingEngine, RuleEngine
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
    if spec.engine not in ("rules", "ensemble"):
        raise ValueError("research runs use deterministic engines (rules, ensemble); model engines are evaluated on paper")
    params, limits = build(spec)
    opts = spec.engine_opts or {}
    fee_mult = float(opts.get("fee_mult", 1.0))
    if fee_mult != 1.0:
        params = replace(params, round_trip_cost_pct=params.round_trip_cost_pct * fee_mult)
    risk = RiskManager(limits, run_dir / "_engine_KILL")
    bracket_ = (params.fixed_stop_pct, params.fixed_take_pct) if params.exit_mode == "fixed" else None
    if spec.engine == "rules":
        if opts.get("disable"):
            raise ValueError("signal ablation needs the ensemble engine")
        base = RuleEngine()
    else:
        base = EnsembleEngine(stop_atr=params.stop_atr_mult, rr=params.reward_risk, fixed_bracket=bracket_,
                              disabled=tuple(opts.get("disable", ())))
    if opts.get("news", "none") != "none":
        from .news import NewsOverlay
        from .sentiment import SentimentStore

        store = SentimentStore(opts["news_db"])
        if opts.get("news_sha") and store.sha256()[:16] != opts["news_sha"]:
            raise ValueError("news database changed since this spec was written")
        base = NewsOverlay(base, store, opts["news"])
    reviewer = None
    if opts.get("reviewer"):
        from .order_review import OrderReviewer

        if opts["reviewer"] not in ("code", "code-shadow"):
            raise ValueError("research runs use the deterministic reviewer only")
        reviewer = OrderReviewer(mode="shadow" if opts["reviewer"] == "code-shadow" else "block")
    return run_backtest(candles, EscalatingEngine(base, None, risk), run_dir, spec.instrument, params=params, limits=limits,
                        fee_bps=6.0 * fee_mult, slip_bps=2.0 * fee_mult, reviewer=reviewer)


def decisions_changed(base_dir: Path, other_dir: Path) -> float:
    """Share of bars where the intended action (enter long / enter short / exit / hold) differs."""
    from .journal import read_journal

    key = lambda r: (r["ts"], r["intent"]["action"], r["intent"].get("side"))
    a = [key(r) for r in read_journal(base_dir / "journal.jsonl") if r["kind"] == "decision"]
    b = {r[0]: r for r in (key(x) for x in read_journal(other_dir / "journal.jsonl") if x["kind"] == "decision")}
    if not a:
        return 0.0
    return sum(b.get(ts) != (ts, act, side) for ts, act, side in a) / len(a)


def ablate(reg: Register, base: Spec, candles: list[Candle], snap: DataSnapshot, lo: int, hi: int, work: Path,
           signals: tuple[str, ...] | None = None) -> dict:
    """Turn each signal (tool) off in turn. If the trades don't change, it is doing nothing:
    delete it. Every run is recorded as an exploration of the family."""
    if base.engine != "ensemble":
        raise ValueError("ablation runs on the ensemble engine")
    window = candles[lo:hi]
    signals = signals or EnsembleEngine.SIGNALS
    base_res = summarize(run_spec(base, window, work / base.id))
    reg.record_run("explore", base, snap, snap.slice_note(lo, hi), base_res)
    rows = {}
    for sig in signals:
        spec = replace(base, engine_opts={**base.engine_opts, "disable": sorted(set(base.engine_opts.get("disable", [])) | {sig})})
        r = summarize(run_spec(spec, window, work / spec.id))
        reg.record_run("explore", spec, snap, snap.slice_note(lo, hi), r)
        changed = decisions_changed(work / base.id, work / spec.id)
        rows[sig] = {"decisions_changed": changed, "trades": r["trades"], "return_without": r["total_return"],
                     "return_delta_vs_all": r["total_return"] - base_res["total_return"],
                     "verdict": "does nothing: delete" if changed < 0.01 else
                                "hurts: removing it improved return" if r["total_return"] > base_res["total_return"] else "keep"}
    return {"baseline": {"trades": base_res["trades"], "total_return": base_res["total_return"]}, "signals": rows}


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


def walk_forward(reg: Register, base: Spec, grid: dict[str, list], candles: list[Candle], snap: DataSnapshot,
                 folds: int, work: Path, min_train: int | None = None) -> list[dict]:
    """Anchored walk-forward: for each fold, explore on everything before it, pick the
    best configuration by excess return, then test it ONCE on the fold. Every run of
    every fold is recorded. The out-of-sample folds never inform each other's choice."""
    n = len(candles)
    test_len = n // (folds + 1)
    min_train = min_train or test_len
    out = []
    for k in range(folds):
        train_end = min_train + k * test_len
        test_end = min(n, train_end + test_len)
        if test_end - train_end < 120:
            break
        fold_spec = replace(base, name=f"{base.name}@wf{k}")
        recs = sweep(reg, fold_spec, grid, candles, snap, 0, train_end, work)
        ok = [r for r in recs if r["status"] == "ok"]
        if not ok:
            out.append({"fold": k, "error": "no successful configuration"})
            continue
        best = max(ok, key=lambda r: r["result"]["excess_vs_benchmark"])
        spec = Spec(**best["spec"])
        try:
            reg.freeze(spec, f"walk-forward fold {k}: best training excess")
        except Exception:
            pass  # identical spec already frozen by an earlier fold
        test = reg.record_run("explore", replace(spec, name=f"{base.name}@wf{k}-oos"), snap,
                              snap.slice_note(train_end, test_end), summarize(run_spec(spec, candles[train_end:test_end], work / f"wf{k}")))
        out.append({"fold": k, "train": [0, train_end], "test": [train_end, test_end], "params": spec.params,
                    "train_excess": best["result"]["excess_vs_benchmark"], "test_excess": test["result"]["excess_vs_benchmark"],
                    "test_return": test["result"]["total_return"], "test_benchmark": test["result"]["benchmark_net_return"]})
    return out
