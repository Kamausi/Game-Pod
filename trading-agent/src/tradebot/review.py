"""Nightly review: score every directional call, measure calibration, refit
the calibrator, and write a report + a change proposal for a human to approve.

What updates automatically: the probability calibrator (it is a measurement,
and before 200 outcomes it only ever shrinks probabilities toward 0.5).
What never updates automatically: thresholds, risk limits, the prompt, the
schema. Those come out as a proposal file for the operator to approve.
"""
from __future__ import annotations

from collections import Counter
from pathlib import Path

from .journal import read_journal
from .policy import Calibrator, PolicyParams, bracket


def brier(ps: list[float], ys: list[int]) -> float:
    return sum((p - y) ** 2 for p, y in zip(ps, ys)) / len(ps) if ps else float("nan")


def pav(xs: list[float], ys: list[int]) -> list[tuple[float, float]]:
    """Isotonic (pool-adjacent-violators) fit; returns (mean_x, fitted_y) per block."""
    pairs = sorted(zip(xs, ys))
    blocks: list[list[float]] = []  # [sum_x, sum_y, n]
    for x, y in pairs:
        blocks.append([x, float(y), 1])
        while len(blocks) > 1 and blocks[-2][1] / blocks[-2][2] > blocks[-1][1] / blocks[-1][2]:
            sx, sy, n = blocks.pop()
            blocks[-1][0] += sx
            blocks[-1][1] += sy
            blocks[-1][2] += n
    return [(sx / n, sy / n) for sx, sy, n in blocks]


def label_outcomes(records: list[dict], params: PolicyParams, horizon: int = 24) -> list[tuple[dict, int]]:
    """For each directional decision: did price reach the take-profit before the
    stop within `horizon` later decisions? Uses later snapshot mids only, so it
    never peeks at anything the agent did not also see later."""
    decs = [r for r in records if r["kind"] == "decision"]
    out = []
    for i, r in enumerate(decs):
        d, s = r["decision"], r["snapshot"]
        if d["direction"] == "neutral" or d["source"] == "failsafe":
            continue
        future = decs[i + 1 : i + 1 + horizon]
        if len(future) < horizon:
            continue  # not resolved yet; scored in a later review
        stop, take = bracket(params, s["atr_pct"])
        sgn = 1 if d["direction"] == "long" else -1
        y = 0
        for f in future:
            move = sgn * (f["snapshot"]["mid"] / s["mid"] - 1)
            if move <= -stop:
                break
            if move >= take:
                y = 1
                break
        out.append((r, y))
    return out


def reviewer_effect(records: list[dict], horizon: int = 48) -> dict | None:
    """Did the entries the pre-trade reviewer flagged stop out more often than the rest?
    Each reviewed entry is scored hypothetically on later snapshot mids, flagged or not,
    so blocked orders are judged on the same footing as the ones that were sent."""
    reviews = [r for r in records if r["kind"] == "review"]
    if not reviews:
        return None
    decs = [r for r in records if r["kind"] == "decision"]
    out = {True: [0, 0], False: [0, 0]}  # flagged -> [resolved, stopped out]
    for rv in reviews:
        o, flagged = rv["order"], rv["verdict"]["flagged"]
        future = [d for d in decs if d["ts"] > rv["ts"]][:horizon]
        sgn = 1 if o["side"] == "long" else -1
        res = None
        for f in future:
            move = sgn * (f["snapshot"]["mid"] / o["mid"] - 1)
            if move <= -o["stop_pct"]:
                res = 1
                break
            if move >= o["take_pct"]:
                res = 0
                break
        if res is None:
            continue
        out[flagged][0] += 1
        out[flagged][1] += res
    rate = lambda k: out[k][1] / out[k][0] if out[k][0] else None
    return {"reviewed": len(reviews), "flagged": sum(r["verdict"]["flagged"] for r in reviews),
            "blocked": sum(r["verdict"]["blocked"] for r in reviews),
            "flagged_resolved": out[True][0], "flagged_stop_rate": rate(True),
            "passed_resolved": out[False][0], "passed_stop_rate": rate(False)}


def run_review(run_dir: Path, params: PolicyParams | None = None) -> str:
    params = params or PolicyParams()
    records = read_journal(run_dir / "journal.jsonl")
    labeled = label_outcomes(records, params)
    ps = [r["decision"]["p_win"] for r, _ in labeled]
    ys = [y for _, y in labeled]

    old_cal = Calibrator.load(run_dir / "calibration.json")
    cal_ps = [old_cal(p) for p in ps]
    base = sum(ys) / len(ys) if ys else float("nan")
    new_cal = Calibrator(points=pav(ps, ys) if ps else [], n_samples=len(ps))
    new_cal.save(run_dir / "calibration.json")

    decisions = [r for r in records if r["kind"] == "decision"]
    orders = [r for r in records if r["kind"] == "order"]
    fills = [o["fill"] for o in orders if o["fill"]]
    blocked = Counter(", ".join(o["verdict"]["reasons"]) for o in orders if not o["verdict"]["allowed"])
    failsafe = Counter(
        r["decision"]["rationale"].split(":")[0] for r in decisions if r["decision"]["source"] == "failsafe"
    )
    holds = Counter(r["intent"]["reason"] for r in decisions if r["intent"]["action"] == "hold")
    sources = Counter(r["decision"]["source"] for r in decisions)
    eq = [r["equity"] for r in decisions]

    lines = [
        f"# Nightly review: {run_dir}",
        "",
        f"- decisions: {len(decisions)}  fills: {len(fills)}  fees: {sum(f['fee'] for f in fills):.2f}",
        f"- equity: {eq[0]:.2f} -> {eq[-1]:.2f}" if eq else "- equity: n/a",
        f"- engines: {dict(sources)}",
        f"- failsafe holds: {dict(failsafe)}",
        f"- risk-blocked orders: {dict(blocked)}",
        "",
        "## Calibration (directional calls, resolved only)",
        f"- resolved calls: {len(ys)}  hit rate: {base:.3f}" if ys else "- resolved calls: 0",
        f"- Brier raw p_win: {brier(ps, ys):.4f}",
        f"- Brier calibrated (yesterday's map): {brier(cal_ps, ys):.4f}",
        f"- Brier constant base rate: {brier([base] * len(ys), ys):.4f}",
        "  (if raw Brier is not below the base-rate Brier, the model's probabilities carry no information)",
        "",
        "| p_win bucket | n | mean p | hit rate |",
        "|---|---|---|---|",
    ]
    for lo in [i / 10 for i in range(10)]:
        idx = [i for i, p in enumerate(ps) if lo <= p < lo + 0.1 or (lo == 0.9 and p == 1.0)]
        if idx:
            mp = sum(ps[i] for i in idx) / len(idx)
            hr = sum(ys[i] for i in idx) / len(idx)
            lines.append(f"| {lo:.1f}-{lo + 0.1:.1f} | {len(idx)} | {mp:.3f} | {hr:.3f} |")

    rv = reviewer_effect(records)
    if rv:
        lines += ["", "## Pre-trade reviewer (second check before the exchange)",
                  f"- entries reviewed: {rv['reviewed']}  flagged: {rv['flagged']}  blocked: {rv['blocked']}",
                  f"- stop-out rate, flagged: {rv['flagged_stop_rate']} over {rv['flagged_resolved']} resolved",
                  f"- stop-out rate, passed: {rv['passed_stop_rate']} over {rv['passed_resolved']} resolved",
                  "  (the reviewer earns its place only if flagged entries stop out clearly more often)"]
    lines += ["", "## Top hold reasons", *[f"- {k}: {v}" for k, v in holds.most_common(8)], ""]
    lines += ["## Proposals (NOT applied; operator approval required)"]
    proposals = []
    if ys and brier(ps, ys) >= brier([base] * len(ys), ys):
        proposals.append("Model p_win is no better than the base rate. Do not go live; revise the reflex prompt or features.")
    if len(ys) < new_cal.min_samples:
        proposals.append(f"Only {len(ys)} resolved calls; calibrator stays in shrink mode until {new_cal.min_samples}.")
    if sum(failsafe.values()) > 0.05 * max(1, len(decisions)):
        proposals.append("Failsafe rate > 5%: fix engine reliability before anything else.")
    if rv and rv["flagged_resolved"] >= 20 and rv["passed_stop_rate"] is not None and rv["flagged_stop_rate"] is not None \
            and rv["flagged_stop_rate"] <= rv["passed_stop_rate"]:
        proposals.append("Flagged entries stop out no more often than passed ones: the reviewer is blocking trades for nothing; "
                         "switch it to shadow mode or retune it.")
    lines += [f"- {p}" for p in proposals] or ["- none"]
    report = "\n".join(lines) + "\n"
    (run_dir / "review.md").write_text(report)
    from . import runlog

    runlog.append(run_dir,
                  done=[f"nightly review: {len(decisions)} decisions, {len(ys)} resolved calls, calibrator refit on {len(ps)} points"],
                  decided=[f"calibrator {'still shrinking toward 0.5' if len(ys) < new_cal.min_samples else 'now active'} "
                           f"(Brier raw {brier(ps, ys):.4f} vs base {brier([base] * len(ys), ys):.4f})" if ys else
                           "calibrator unchanged: no resolved calls yet"],
                  open_items=proposals or None)
    return report
