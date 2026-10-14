"""Evidence package: everything needed to check a result without trusting a narrative.

`build_package(run_id)` writes `runs/research/evidence/<run_id>/`:
  evidence.json  - spec, data snapshot, engine version, assumptions, cost
                   model, benchmark definition, all variations tried in the
                   family, the run's fills and per-bar equity, statistics,
                   and reconciliation results
  evidence.md    - the same, readable, generated from evidence.json only

Reconciliation (each must pass for the package to say "reconciled"):
  1. determinism   - re-running the same spec on the same bytes gives
                     identical fills
  2. ledger        - an independent cash/position replay of the fills
                     reproduces the engine's final equity
  3. external      - optional: a second implementation's results (e.g. LEAN,
                     QuantConnect, QuantGPT) compared trade by trade
"""
from __future__ import annotations

import hashlib
import json
from dataclasses import asdict
from pathlib import Path

from .backtest import load_candles
from .experiments import ASSUMPTIONS, Register, Spec
from .validation import assess, run_spec


def _fills_hash(fills) -> str:
    rows = [[f.ts, f.side, round(f.qty, 10), round(f.price, 6), round(f.fee, 8)] for f in fills]
    return hashlib.sha256(json.dumps(rows).encode()).hexdigest()


def ledger_replay(fills, start_cash: float, final_mark: float) -> float:
    """Independent equity recomputation from the fill ledger alone."""
    cash, qty = start_cash, 0.0
    for f in fills:
        signed = f.qty if f.side == "buy" else -f.qty
        cash -= signed * f.price + f.fee
        qty += signed
    return cash + qty * final_mark


def compare_external(ours: dict, external: dict, tol: float = 0.005) -> dict:
    """external = {"total_return": float, "trades": int, "fills": [{"ts","side","price"}...]} (any subset)."""
    diffs = []
    if "total_return" in external and abs(external["total_return"] - ours["total_return"]) > tol:
        diffs.append(f"total_return ours={ours['total_return']:.4f} external={external['total_return']:.4f}")
    if "trades" in external and external["trades"] != ours["trades"]:
        diffs.append(f"trades ours={ours['trades']} external={external['trades']}")
    if "fills" in external:
        o = [(f["ts"], f["side"]) for f in ours["fills"]]
        e = [(f["ts"], f["side"]) for f in external["fills"]]
        only_o, only_e = sorted(set(o) - set(e)), sorted(set(e) - set(o))
        if only_o or only_e:
            diffs.append(f"fills only in ours: {only_o[:10]}; only in external: {only_e[:10]}")
    return {"material_differences": diffs, "reconciled": not diffs}


def build_package(reg: Register, run_id: str, external: dict | None = None) -> Path:
    rec = reg.find(run_id)
    if rec["status"] != "ok":
        raise ValueError(f"{run_id} failed ({rec['error']}); a failed run has no evidence package")
    spec = Spec(**rec["spec"])
    data_path = reg.root / "data" / f"{rec['data']['sha256'][:16]}.json"
    candles = load_candles(data_path)
    lo, hi = (int(x) for x in rec["split"].split("[")[1].split("]")[0].split(":"))
    window = candles[lo:hi]

    out = reg.root / "evidence" / run_id
    a = run_spec(spec, window, out / "rerun_a")
    b = run_spec(spec, window, out / "rerun_b")
    determinism = _fills_hash(a.fills) == _fills_hash(b.fills)
    replayed = ledger_replay(a.fills, a.start_equity, window[-1].close)
    ledger_ok = abs(replayed - a.end_equity) < 1e-6 * max(1.0, a.start_equity)
    result_matches_register = abs(a.total_return - rec["result"]["total_return"]) < 1e-9

    ours = {"total_return": a.total_return, "trades": a.trades,
            "fills": [{"ts": f.ts, "side": f.side, "price": f.price, "qty": f.qty, "fee": f.fee, "reason": f.reason} for f in a.fills]}
    ext = compare_external(ours, external) if external else None
    family = [
        {"run_id": r["run_id"], "candidate_id": r["candidate_id"], "params": r["spec"]["params"], "status": r["status"],
         "excess_vs_benchmark": (r["result"] or {}).get("excess_vs_benchmark"), "error": r.get("error")}
        for r in reg.family_trials(rec["family"], rec["data"]["sha256"])
    ]
    pkg = {
        "run_id": run_id,
        "kind": rec["kind"],
        "candidate_id": rec["candidate_id"],
        "spec": rec["spec"],
        "data": {**rec["data"], "split": rec["split"], "file": str(data_path)},
        "engine_version": rec["engine_version"],
        "assumptions": ASSUMPTIONS,
        "cost_model": {"fee_bps_per_side": 6.0, "slippage_bps_per_side": 2.0, "spread_bps": 2.0},
        "benchmark": "buy-and-hold, same execution model and costs",
        "result": rec["result"],
        "statistics": assess(reg, rec),
        "all_variations_tried": family,
        "orders": ours["fills"],
        "equity_curve": a.equity_curve,
        "reconciliation": {
            "determinism_identical_fills": determinism,
            "ledger_replay_equity": replayed,
            "engine_final_equity": a.end_equity,
            "ledger_matches": ledger_ok,
            "rerun_matches_register": result_matches_register,
            "external": ext,
            "reconciled": determinism and ledger_ok and result_matches_register and (ext is None or ext["reconciled"]),
            "not_checked": "signal logic is not independently re-implemented here; supply an external engine's results for that",
        },
    }
    out.mkdir(parents=True, exist_ok=True)
    (out / "evidence.json").write_text(json.dumps(pkg, indent=2, default=str))
    (out / "evidence.md").write_text(render_md(pkg))
    return out


def render_md(p: dict) -> str:
    r, s, rc = p["result"], p["statistics"], p["reconciliation"]
    tried = p["all_variations_tried"]
    lines = [
        f"# Evidence package {p['run_id']}",
        "",
        f"- Kind: **{p['kind']}**  Candidate: `{p['candidate_id']}`  Family: `{p['spec']['name']}`",
        f"- Data: {p['data']['source']} {p['data']['instrument']} {p['data']['timeframe']}, {p['data']['split']}, sha256 `{p['data']['sha256'][:16]}`",
        f"- Engine: {p['engine_version']}",
        "",
        "## Result",
        f"- Return {r['total_return']:+.2%} vs benchmark (same costs) {r['benchmark_net_return']:+.2%}: excess {r['excess_vs_benchmark']:+.2%}",
        f"- Max drawdown {r['max_drawdown']:.2%}, round trips {r['trades']}, fees ${r['fees']:.2f}",
        f"- Sharpe per bar {r['sharpe_per_bar']:.4f}, skew {r['skew']:.2f}, kurtosis {r['kurtosis']:.2f}, n={r['n_obs']}",
        "",
        "## Multiple testing",
        f"- Configurations tried in this family on this data: **{s['n_trials']}** (all listed below, failures included)",
        f"- Sharpe expected from the best of {s['n_trials']} zero-edge tries: {s['expected_max_sharpe_null']:.4f}",
        f"- Deflated Sharpe probability (true Sharpe > 0): **{s['deflated_sharpe_prob']:.3f}**",
        "",
        "## Reconciliation",
        f"- Identical fills on re-run: {rc['determinism_identical_fills']}",
        f"- Ledger replay equity {rc['ledger_replay_equity']:.2f} vs engine {rc['engine_final_equity']:.2f}: {rc['ledger_matches']}",
        f"- Re-run matches register: {rc['rerun_matches_register']}",
        f"- External engine: {rc['external'] if rc['external'] else 'not supplied'}",
        f"- **Reconciled: {rc['reconciled']}** ({rc['not_checked']})",
        "",
        "## Assumptions",
        *[f"- {k}: {v}" for k, v in p["assumptions"].items()],
        "",
        "## Every variation tried",
        "| candidate | params | status | excess vs benchmark |",
        "|---|---|---|---|",
        *[f"| `{t['candidate_id']}` | `{json.dumps(t['params'])}` | {t['status']} | "
          f"{'' if t['excess_vs_benchmark'] is None else format(t['excess_vs_benchmark'], '+.2%')} |" for t in tried],
    ]
    return "\n".join(lines) + "\n"
