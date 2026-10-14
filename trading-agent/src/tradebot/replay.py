"""Replay saved decisions on another engine before paying for a smarter model.

Every decision in a journal carries the exact snapshot (and, for model engines, the
exact prompt or state) the engine saw. `replay` feeds those same inputs to a different
engine (a cheaper Claude model, Jev, or the deterministic baselines) and reports:

  - how often it agrees with the original on direction and on the action taken
  - Brier score of both engines' win probabilities on the calls that have resolved
  - tokens and an estimated dollar cost for both

If the cheap engine makes equally good calls, the expensive one is not earning its bill.
Prices are list prices per million tokens (input, output, cache read) as of 2026-10;
check them before relying on the dollar figures.
"""
from __future__ import annotations

from dataclasses import fields
from pathlib import Path

from .engines import ClaudeEngine, EnsembleEngine, RuleEngine, safe_decide
from .journal import read_journal
from .policy import Calibrator, PolicyParams, decide
from .review import brier, label_outcomes
from .state import Snapshot

PRICES_PER_MTOK = {
    "claude-opus-5-5": (4.00, 20.00, 0.20),
    "claude-sonnet-5-5": (2.00, 10.00, 0.20),
    "claude-haiku-5-5": (0.10, 0.50, 0.01),
    "jev": (0.042, 0.0, 0.0),
}


def cost_of(trace: dict | None) -> tuple[int, float]:
    """(tokens, dollars) for one traced call."""
    if not trace or not trace.get("usage"):
        return 0, 0.0
    if trace.get("escalated_from"):
        t0, c0 = cost_of(trace["escalated_from"])
    else:
        t0, c0 = 0, 0.0
    u = trace["usage"]
    model = trace.get("model_served") or trace.get("model_requested") or ""
    key = "jev" if trace.get("engine") == "jev" or model.startswith("jev") else next((k for k in PRICES_PER_MTOK if model.startswith(k)), None)
    tin, tout, cr = int(u.get("input_tokens") or 0), int(u.get("output_tokens") or 0), int(u.get("cache_read_input_tokens") or 0)
    if key is None:
        return t0 + tin + tout + cr, c0
    pi, po, pc = PRICES_PER_MTOK[key]
    return t0 + tin + tout + cr, c0 + (tin * pi + tout * po + cr * pc) / 1e6


def build_replay_engine(kind: str):
    if kind == "rules":
        return RuleEngine()
    if kind == "ensemble":
        return EnsembleEngine()
    if kind == "jev":
        from .jev import JevEngine

        return JevEngine()
    if kind.startswith("claude:"):
        model = kind.split(":", 1)[1]
        return ClaudeEngine(model, effort="low" if "haiku" in model else "medium", timeout_s=30)
    raise ValueError("engine must be rules, ensemble, jev or claude:<model id>")


def snapshot_from(d: dict) -> Snapshot:
    names = {f.name for f in fields(Snapshot)}
    return Snapshot(**{k: v for k, v in d.items() if k in names})


def replay(run_dir: Path, engine, params: PolicyParams | None = None, limit: int | None = None) -> dict:
    params = params or PolicyParams()
    recs = read_journal(Path(run_dir) / "journal.jsonl")
    decs = [r for r in recs if r["kind"] == "decision" and r["decision"]["source"] != "failsafe"]
    if limit:
        decs = decs[-limit:]
    labeled = {r["ts"]: y for r, y in label_outcomes(recs, params)}
    agree_dir = agree_act = 0
    ps_orig, ps_new, ys = [], [], []
    tok_o = tok_n = 0
    usd_o = usd_n = 0.0
    errors = 0
    for r in decs:
        snap = snapshot_from(r["snapshot"])
        trace = r.get("trace") or {}
        if hasattr(engine, "call") and trace.get("prompt"):
            try:
                d = engine.call(trace["prompt"])  # the exact prompt the original model saw
            except Exception:
                d, _ = safe_decide(engine, snap)
        else:
            d, _ = safe_decide(engine, snap)
        if d.source == "failsafe":
            errors += 1
        side = None if abs(snap.inventory_frac) < 1e-12 else ("long" if snap.inventory_frac > 0 else "short")
        it = decide(d, snap, side, params, Calibrator())
        o = r["decision"]
        agree_dir += d.direction == o["direction"]
        agree_act += (it.action, it.side) == (r["intent"]["action"], r["intent"].get("side"))
        if r["ts"] in labeled and d.direction == o["direction"] and d.direction != "neutral":
            ps_orig.append(o["p_win"])
            ps_new.append(d.p_win)
            ys.append(labeled[r["ts"]])
        t, c = cost_of(trace)
        tok_o, usd_o = tok_o + t, usd_o + c
        t, c = cost_of(getattr(engine, "last_trace", None))
        tok_n, usd_n = tok_n + t, usd_n + c
    n = len(decs) or 1
    return {
        "decisions": len(decs), "replay_engine": getattr(engine, "name", type(engine).__name__), "replay_errors": errors,
        "direction_agreement": agree_dir / n, "action_agreement": agree_act / n,
        "resolved_calls_compared": len(ys),
        "brier_original": brier(ps_orig, ys) if ys else None, "brier_replay": brier(ps_new, ys) if ys else None,
        "base_rate_brier": brier([sum(ys) / len(ys)] * len(ys), ys) if ys else None,
        "tokens_original": tok_o, "usd_original": round(usd_o, 4), "tokens_replay": tok_n, "usd_replay": round(usd_n, 4),
        "cost_ratio": round(usd_o / usd_n, 1) if usd_n else None,
    }
