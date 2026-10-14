"""tradebot CLI.

Research (no broker access, no limits/release writes):
  data snapshot | research sweep | research freeze | research final | evidence | register
Operations:
  opsgate | init-limits | approve | paper | review | kill | unkill | status
Quick look:
  backtest
"""
from __future__ import annotations

import argparse
import json
import sys
from dataclasses import asdict
from pathlib import Path

from .backtest import load_candles, run_backtest, synthetic_candles
from .engines import build_engine
from .review import run_review
from .risk import RiskLimits, RiskManager


def _load_data(arg: str):
    """'synthetic:N[:seed]' or a JSON candle file. Returns (candles, source)."""
    if arg.startswith("synthetic:"):
        parts = arg.split(":")
        n, seed = int(parts[1]), int(parts[2]) if len(parts) > 2 else 7
        return synthetic_candles(n, seed=seed), f"synthetic(seed={seed}, n={n}) - no real edge exists in this data"
    return load_candles(arg), f"file:{arg}"


def _range(s: str) -> tuple[int, int]:
    a, b = s.split(":")
    return int(a), int(b)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="tradebot")
    sub = ap.add_subparsers(dest="cmd", required=True)

    b = sub.add_parser("backtest", help="quick look; not recorded as evidence")
    b.add_argument("--data", default="synthetic:3000")
    b.add_argument("--engine", default="rules", choices=["rules", "ensemble", "claude"])
    b.add_argument("--run-dir", default="runs/backtest")

    d = sub.add_parser("data", help="snapshot market data with provenance")
    d.add_argument("action", choices=["snapshot"])
    d.add_argument("--instrument", default="BTC_USD")
    d.add_argument("--timeframe", default="1h")
    d.add_argument("--out", required=True)

    r = sub.add_parser("research")
    rs = r.add_subparsers(dest="rcmd", required=True)
    sw = rs.add_parser("sweep", help="explore a parameter grid on the training window; every run is recorded")
    sw.add_argument("--family", required=True)
    sw.add_argument("--data", required=True)
    sw.add_argument("--train", required=True, help="bar range, e.g. 0:2000")
    sw.add_argument("--grid", required=True, help='JSON, e.g. {"reward_risk":[1.5,2,3],"stop_atr_mult":[1,1.5,2]}')
    sw.add_argument("--base-params", default="{}")
    sw.add_argument("--engine", default="rules", choices=["rules", "ensemble"])
    fr = rs.add_parser("freeze", help="freeze one explored configuration as a candidate")
    fr.add_argument("--run", required=True)
    fr.add_argument("--reason", required=True)
    fi = rs.add_parser("final", help="the candidate's one and only holdout evaluation")
    fi.add_argument("--candidate", required=True)
    fi.add_argument("--data", required=True)
    fi.add_argument("--holdout", required=True)

    e = sub.add_parser("evidence", help="build and reconcile an evidence package for a run")
    e.add_argument("--run", required=True)
    e.add_argument("--external", help="JSON results from a second implementation to reconcile against")

    sub.add_parser("register", help="list every recorded experiment")
    sub.add_parser("opsgate", help="run the operational fault scenarios for this engine version")
    sub.add_parser("init-limits", help="write config/risk_limits.json with default (maximum) limits")

    a = sub.add_parser("approve", help="operator only: approve a candidate for the paper runner")
    a.add_argument("--candidate", required=True)
    a.add_argument("--approver", required=True)
    a.add_argument("--accept-weak-evidence", action="store_true",
                   help="approve for paper even if the holdout lost to the benchmark or DSR < 0.95 (recorded in the release)")

    p = sub.add_parser("paper")
    p.add_argument("--release", default="releases/approved.json")
    p.add_argument("--dev", action="store_true", help="run without a release (journal is labelled DEV)")
    p.add_argument("--engine", default="rules", choices=["rules", "ensemble", "claude"])
    p.add_argument("--instrument", default="BTC_USD")
    p.add_argument("--timeframe", default="1h")
    p.add_argument("--run-dir", default="runs/paper")
    p.add_argument("--cash", type=float, default=10_000.0)

    rv = sub.add_parser("review")
    rv.add_argument("--run-dir", default="runs/paper")

    for name in ("kill", "unkill", "status"):
        k = sub.add_parser(name)
        k.add_argument("--run-dir", default="runs/paper")
        if name == "kill":
            k.add_argument("--reason", default="operator kill")
        if name == "status":
            k.add_argument("--probe", action="store_true", help="hit the exchange API once")

    args = ap.parse_args(argv)

    if args.cmd == "backtest":
        candles, _ = _load_data(args.data)
        risk = RiskManager(RiskLimits(), Path(args.run_dir) / "_engine_KILL")
        print(run_backtest(candles, build_engine(args.engine, risk), Path(args.run_dir)))
        return 0

    if args.cmd == "data":
        from .feed import CryptoComFeed
        import time

        now = int(time.time() * 1000)
        cs = CryptoComFeed(args.instrument, args.timeframe).closed_candles(now, count=300)
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text(json.dumps([c.__dict__ for c in cs]))
        Path(args.out + ".provenance.json").write_text(json.dumps({
            "source": "Crypto.com Exchange public get-candlestick", "instrument": args.instrument,
            "timeframe": args.timeframe, "fetched_at_ms": now, "bars": len(cs),
            "availability": "each bar usable from its close_ts; the forming bar was dropped",
            "adjustments": "none (spot crypto: no splits/dividends)"}, indent=2))
        print(f"wrote {len(cs)} closed bars to {args.out}")
        return 0

    if args.cmd in ("research", "evidence", "register", "opsgate", "approve"):
        from .experiments import DataSnapshot, Register, Spec

        reg = Register()

    if args.cmd == "research":
        from .validation import assess, final_evaluation, plateau, sweep

        if args.rcmd == "sweep":
            candles, source = _load_data(args.data)
            snap = DataSnapshot.of(candles, source, "BTC_USD", "1h")
            reg.save_data(candles, snap)
            lo, hi = _range(args.train)
            base = Spec(name=args.family, engine=args.engine, params=json.loads(args.base_params))
            recs = sweep(reg, base, json.loads(args.grid), candles, snap, lo, hi, Path("runs/research/work"))
            ok = [x for x in recs if x["status"] == "ok"]
            ok.sort(key=lambda x: x["result"]["excess_vs_benchmark"], reverse=True)
            print(f"{len(recs)} configurations run ({len(recs) - len(ok)} failed), all recorded.")
            print(f"share beating the cost-adjusted benchmark: {plateau(recs):.0%}")
            for x in ok[:10]:
                st = assess(reg, x)
                print(f"{x['run_id']}  {json.dumps(x['spec']['params'])}  excess={x['result']['excess_vs_benchmark']:+.2%}  "
                      f"DSR={st['deflated_sharpe_prob']:.3f}")
            return 0
        if args.rcmd == "freeze":
            rec = reg.find(args.run)
            out = reg.freeze(Spec(**rec["spec"]), args.reason)
            print(f"frozen {out['candidate_id']}. It gets exactly one final evaluation; any change makes a new candidate.")
            return 0
        if args.rcmd == "final":
            candles, source = _load_data(args.data)
            snap = DataSnapshot.of(candles, source, "BTC_USD", "1h")
            reg.save_data(candles, snap)
            lo, hi = _range(args.holdout)
            rec = final_evaluation(reg, args.candidate, candles, snap, lo, hi, Path("runs/research/work"))
            if rec["status"] != "ok":
                print(f"final evaluation failed: {rec['error']}")
                return 1
            print(json.dumps({"run_id": rec["run_id"], **{k: rec["result"][k] for k in
                  ("total_return", "benchmark_net_return", "excess_vs_benchmark", "max_drawdown", "trades")},
                  **assess(reg, rec)}, indent=2))
            return 0

    if args.cmd == "evidence":
        from .evidence import build_package

        ext = json.loads(Path(args.external).read_text()) if args.external else None
        out = build_package(reg, args.run, ext)
        print((out / "evidence.md").read_text())
        print(f"written to {out}")
        return 0

    if args.cmd == "register":
        for x in reg.records():
            res = x.get("result") or {}
            print(x.get("run_id", "-"), x["kind"], x["candidate_id"], x.get("status", ""), json.dumps(x.get("spec", {}).get("params", {})),
                  f"excess={res['excess_vs_benchmark']:+.2%}" if "excess_vs_benchmark" in res else x.get("error") or x.get("reason", ""))
        return 0

    if args.cmd == "opsgate":
        from .ops_gate import run_gate

        g = run_gate(reg)
        for name, r in g["scenarios"].items():
            print(f"{'PASS' if r['passed'] else 'FAIL'}  {name}: {r['detail']}")
        print("GATE PASSED" if g["passed"] else "GATE FAILED")
        return 0 if g["passed"] else 1

    if args.cmd == "init-limits":
        from .release import init_limits

        print(f"limits at {init_limits()}; edit to tighten (code refuses anything looser than the hard caps)")
        return 0

    if args.cmd == "approve":
        from .release import approve

        rel = approve(reg, args.candidate, args.approver, accept_weak_evidence=args.accept_weak_evidence)
        print(f"approved {rel['candidate_id']} by {rel['approved_by']}; runner will verify limits and engine hashes at start")
        return 0

    if args.cmd == "paper":
        from .loop import run_paper
        from .policy import PolicyParams
        from .release import ReleaseError, verify_release

        if args.dev:
            run_paper(args.instrument, args.timeframe, args.engine, Path(args.run_dir), args.cash,
                      PolicyParams(), RiskLimits(), None)
            return 0
        try:
            spec, limits, rel = verify_release(Path(args.release))
        except ReleaseError as err:
            print(f"refusing to start: {err}\n(use --dev for an unpromotable test run)")
            return 2
        params = PolicyParams(**{**asdict(PolicyParams()), **spec.params})
        run_paper(spec.instrument, spec.timeframe, spec.engine, Path(args.run_dir), args.cash, params, limits, rel)
        return 0

    if args.cmd == "review":
        print(run_review(Path(args.run_dir)))
        return 0

    run_dir = Path(args.run_dir)
    risk = RiskManager(RiskLimits(), run_dir / "KILL", run_dir / "risk_state.json")
    if args.cmd == "kill":
        risk.kill(args.reason)
        print(f"KILLED: {args.reason}. New risk is blocked; exits still allowed.")
    elif args.cmd == "unkill":
        # Deliberately manual: clearing a drawdown kill is a human decision.
        risk.latched = None
        risk.peak_equity = 0.0  # re-anchor the drawdown peak, or the next update re-trips the kill
        risk.kill_file.unlink(missing_ok=True)
        risk._save()
        print("kill switch cleared")
    elif args.cmd == "status":
        print(json.dumps({"killed": risk.kill_reason(), "peak_equity": risk.peak_equity,
                          "halted_today": risk.halted_today, "orders_today": risk.orders_today}, indent=2))
        if args.probe:
            from .feed import CryptoComFeed

            f = CryptoComFeed("BTC_USD", "1h")
            bk = f.book()
            print(f"book ok: bid={bk.best_bid} ask={bk.best_ask}; candles ok: {len(f.closed_candles(bk.ts))}")
    return 0


def entry() -> int:
    from .experiments import FrozenError
    from .release import ReleaseError

    try:
        return main()
    except (FrozenError, ReleaseError, ValueError, KeyError) as e:
        print(f"refused: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(entry())
