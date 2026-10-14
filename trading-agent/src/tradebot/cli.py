"""tradebot CLI.

Research (no broker access, no limits/release writes):
  data snapshot | research sweep | research freeze | research final | evidence | register
Operations:
  opsgate | init-limits | approve | paper | review | kill | unkill | status
Real money (only after launch gates, see LAUNCH.md):
  go-live | live | sol-buy | sol-sell | live-check
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

    wf = rs.add_parser("walkforward", help="anchored walk-forward: explore on the past, test once on each next fold")
    wf.add_argument("--family", required=True)
    wf.add_argument("--data", required=True)
    wf.add_argument("--folds", type=int, default=4)
    wf.add_argument("--grid", required=True)
    wf.add_argument("--engine", default="rules", choices=["rules", "ensemble"])

    ab = rs.add_parser("ablate", help="turn each ensemble signal off in turn; a signal that changes no trades does nothing")
    ab.add_argument("--family", required=True)
    ab.add_argument("--data", required=True)
    ab.add_argument("--range", default=None, help="bar range, e.g. 0:2000 (default: all)")
    ab.add_argument("--base-params", default="{}")
    na = rs.add_parser("news-ablation", help="strategy without news, + sentiment bias, + regulatory veto, both; at several fee levels")
    na.add_argument("--family", required=True)
    na.add_argument("--data", required=True)
    na.add_argument("--news-db", default="runs/sentiment.sqlite")
    na.add_argument("--engine", default="ensemble", choices=["rules", "ensemble"])
    na.add_argument("--fees", default="1,2,3", help="fee multipliers, e.g. 1,2,3")
    na.add_argument("--instrument", default="BTC_USD")
    na.add_argument("--timeframe", default="4h")
    na.add_argument("--base-params", default="{}")

    mm = rs.add_parser("mm", help="Avellaneda-Stoikov market making simulation with a fee / adverse-selection honesty check")
    mm.add_argument("--data", required=True)
    mm.add_argument("--gamma", type=float, default=1e-4)
    mm.add_argument("--kappa", type=float, default=0.5)
    mm.add_argument("--horizon", type=int, default=60)
    mm.add_argument("--maker-fee-bps", type=float, default=2.0)
    mm.add_argument("--quote-cost-bps", type=float, default=0.0, help="per requote, e.g. gas as bps of order notional")
    mm.add_argument("--max-inventory", type=int, default=5)

    nw = sub.add_parser("news", help="headline history for the news overlay: import a CSV, classify")
    nws = nw.add_subparsers(dest="ncmd", required=True)
    ni = nws.add_parser("import", help="CSV with columns time,title[,source]")
    ni.add_argument("csv")
    ni.add_argument("--db", default="runs/sentiment.sqlite")
    ni.add_argument("--seen-delay-min", type=float, default=5.0)
    nc = nws.add_parser("classify", help="asset, sentiment, materiality, regulatory risk for every unclassified headline")
    nc.add_argument("--db", default="runs/sentiment.sqlite")
    nc.add_argument("--by", default="lexicon", choices=["lexicon", "claude", "jev"])

    rp = sub.add_parser("replay", help="re-run saved decisions on another engine: agreement, Brier, cost")
    rp.add_argument("--run-dir", default="runs/paper")
    rp.add_argument("--engine", required=True, help="rules | ensemble | jev | claude:<model id>, e.g. claude:claude-haiku-5-5")
    rp.add_argument("--limit", type=int, default=None)

    rl = sub.add_parser("runlog", help="show or append to the run log the next run reads first")
    rl.add_argument("--run-dir", default="runs/paper")
    rl.add_argument("--add", action="store_true")
    rl.add_argument("--done", action="append", default=[])
    rl.add_argument("--decided", action="append", default=[])
    rl.add_argument("--open", dest="open_items", action="append", default=[])
    rl.add_argument("--rejected", action="append", default=[])
    rl.add_argument("-n", type=int, default=5)

    sub.add_parser("jev-check", help="one Jev call to prove the key, model version and latency")

    pmk = sub.add_parser("predmarkets", help="read-only Polymarket vs Kalshi gap scan over pairs YOU confirmed resolve identically")
    pmk.add_argument("--pairs", required=True, help='JSON: [{"polymarket_slug": ..., "kalshi_ticker": ..., "note": ...}]')
    pmk.add_argument("--contracts", type=float, default=100.0)

    imp = sub.add_parser("import-csv", help="import a long OHLCV history (e.g. 20+ years of daily bars) with validation")
    imp.add_argument("path")
    imp.add_argument("--out", required=True, help="where to write the candle JSON for research --data")
    imp.add_argument("--bar", default="1d", choices=["1m", "1h", "1d"])
    imp.add_argument("--adjusted", default="unknown", help="e.g. 'split+dividend' or 'none'; recorded in the report")

    sn = sub.add_parser("sentiment", help="pull news feeds, score headlines, show per-asset sentiment")
    sn.add_argument("--claude", action="store_true", help="score with Claude (needs ANTHROPIC_API_KEY); default: built-in lexicon")
    sn.add_argument("--db", default="runs/sentiment.sqlite")

    ab = sub.add_parser("arb", help="scan Crypto.com, Coinbase and Kraken for net-of-fee price gaps")
    ab.add_argument("--symbols", default="BTC_USD,ETH_USD,SOL_USD,DOGE_USD")

    rg = sub.add_parser("rugcheck", help="Solana token safety checks (read-only, no keys)")
    rg.add_argument("mint")
    rg.add_argument("--pool-owner")
    rg.add_argument("--lp-mint")
    rg.add_argument("--rpc", default="https://api.mainnet-beta.solana.com")

    bk = sub.add_parser("brokers", help="check paper broker connectivity (Alpaca paper / OANDA practice)")
    bk.add_argument("--check", action="store_true")

    hb = sub.add_parser("hub-bench", help="measure round-trip latency to a running Go execution hub")
    hb.add_argument("--addr", default="127.0.0.1:50051")
    hb.add_argument("-n", type=int, default=1000)

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
    p.add_argument("--engine", default="rules", choices=["rules", "ensemble", "claude", "jev", "jev>opus"])
    p.add_argument("--reviewer", default="code", choices=["off", "code", "jev", "claude"],
                   help="second check on every entry before it is sent (default: deterministic code)")
    p.add_argument("--reviewer-mode", default="block", choices=["block", "shadow"])
    p.add_argument("--instrument", default="BTC_USD")
    p.add_argument("--timeframe", default="1h")
    p.add_argument("--run-dir", default="runs/paper")
    p.add_argument("--cash", type=float, default=10_000.0)
    p.add_argument("--exechub", help="route orders through the Go execution hub at host:port")

    gl = sub.add_parser("go-live", help="operator only: check launch gates and authorize live trading for 7 days")
    gl.add_argument("--paper-dir", default="runs/paper")
    gl.add_argument("--operator", required=True)
    gl.add_argument("--venue", action="append", required=True, choices=["cryptocom", "alpaca", "oanda", "solana"])
    gl.add_argument("--accept-unproven", action="store_true", help="authorize despite failed gates (longer phrase, flagged everywhere)")
    gl.add_argument("--gates-only", action="store_true", help="print the launch gates and exit")

    lv = sub.add_parser("live", help="run the live loop on an authorized venue")
    lv.add_argument("--venue", required=True, choices=["cryptocom", "alpaca", "oanda"])
    lv.add_argument("--instrument", help="defaults to the release's instrument (crypto) or must be given (stocks/FX)")
    lv.add_argument("--release", default="releases/approved.json")
    lv.add_argument("--run-dir", default=None)

    lc = sub.add_parser("live-check", help="read-only: verify live keys, balances and authorization; sends no orders")
    lc.add_argument("--venue", required=True, choices=["cryptocom", "alpaca", "oanda", "solana"])

    sb = sub.add_parser("sol-buy", help="live Solana swap USDC -> token (rug check, simulate, Jito bundle)")
    sb.add_argument("mint")
    sb.add_argument("--usdc", type=float, required=True)
    sb.add_argument("--pool-owner")
    sb.add_argument("--lp-mint")
    sb.add_argument("--no-jito", action="store_true", help="send through RPC instead of a Jito bundle")
    ss = sub.add_parser("sol-sell", help="live Solana swap: sell the whole token balance to USDC")
    ss.add_argument("mint")
    ss.add_argument("--no-jito", action="store_true")

    al = sub.add_parser("alert", help="send one alert to TRADEBOT_ALERT_WEBHOOK (used by systemd OnFailure)")
    al.add_argument("message")
    al.add_argument("--level", default="critical", choices=["info", "warn", "critical"])

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

    if args.cmd == "import-csv":
        from .datasets import load_csv, save_report

        bar_ms = {"1m": 60_000, "1h": 3_600_000, "1d": 86_400_000}[args.bar]
        bars, rep = load_csv(args.path, bar_ms=bar_ms, adjusted=args.adjusted)
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text(json.dumps([c.__dict__ for c in bars]))
        save_report(rep, args.out + ".import-report.json")
        print(rep.summary())
        for g in rep.gaps[:10]:
            print(f"  gap: {g[0]} -> {g[1]}")
        for o in rep.outliers[:10]:
            print(f"  outlier (kept, check it): {o[0]} log-return {o[1]:+.3f}")
        return 0

    if args.cmd == "sentiment":
        import time as _t
        from .sentiment import ASSET_KEYWORDS, ClaudeScorer, SentimentStore, refresh

        st = SentimentStore(args.db)
        scorer = ClaudeScorer().score if args.claude else None
        rep = refresh(st, scorer=scorer)
        print(json.dumps(rep, indent=2))
        now = int(_t.time() * 1000)
        for a in ASSET_KEYWORDS:
            s_, n = st.signal(a, now)
            print(f"{a:10} sentiment {s_:+.2f} from {n} headlines (last 6h, seen-time weighted)")
        return 0

    if args.cmd == "arb":
        import time as _t
        from .arbitrage import fetch_quotes, scan

        for sym in args.symbols.split(","):
            qs, errs = fetch_quotes(sym)
            for q in qs:
                print(f"{sym:9} {q.venue:10} bid {q.bid:<14g} ask {q.ask:<14g}")
            for v, e in errs.items():
                print(f"{sym:9} {v:10} unavailable: {e}")
            opps = scan(qs, int(_t.time() * 1000))
            best = opps[0] if opps else None
            print(f"{sym:9} best gap: " + (f"buy {best.buy_venue} sell {best.sell_venue} gross {best.gross_bps:+.1f} bps, net {best.net_bps:+.1f} bps after fees"
                                          if best else "none") + "\n")
        return 0

    if args.cmd == "rugcheck":
        from .onchain import RugGuard, SolanaRPC

        rep = RugGuard(SolanaRPC(args.rpc)).assess(args.mint, args.pool_owner, args.lp_mint)
        for k, (ok, why) in rep.checks.items():
            print(f"{'PASS' if ok else 'FAIL'}  {k}: {why}")
        print("VERDICT:", "passed" if rep.passed else "blocked (fails closed: anything unverified counts as a failure)")
        return 0 if rep.passed else 1

    if args.cmd == "brokers":
        import os
        from .brokers import AlpacaPaper, OandaPractice

        if os.environ.get("APCA_API_KEY_ID"):
            try:
                print("Alpaca paper: market open =", AlpacaPaper().clock_open())
            except Exception as e:
                print("Alpaca paper: FAILED", e)
        else:
            print("Alpaca paper: set APCA_API_KEY_ID and APCA_API_SECRET_KEY (paper keys)")
        if os.environ.get("OANDA_TOKEN"):
            try:
                print("OANDA practice: EUR_USD position =", OandaPractice().position_qty("EUR_USD"))
            except Exception as e:
                print("OANDA practice: FAILED", e)
        else:
            print("OANDA practice: set OANDA_ACCOUNT_ID and OANDA_TOKEN (practice account)")
        return 0

    if args.cmd == "hub-bench":
        import statistics
        import time as _t
        from .hubclient import HubClient

        c = HubClient(args.addr)
        rtt, hub = [], []
        tag = int(_t.time())
        for i in range(args.n):
            t0 = _t.perf_counter()
            r = c.submit(f"bench-{tag}-{i}", "BTC_USD", "crypto", "crypto_major", "buy" if i % 2 == 0 else "sell", 0.0001,
                         83000.0, 83001.0, int(_t.time() * 1000), 10_000.0)
            rtt.append((_t.perf_counter() - t0) * 1e6)
            hub.append(r.hub_latency_us)
        q = lambda xs, p: sorted(xs)[int(p * (len(xs) - 1))]
        print(f"{args.n} orders. round trip p50 {q(rtt, .5):.0f} µs, p99 {q(rtt, .99):.0f} µs; inside hub p50 {q(hub, .5)} µs, p99 {q(hub, .99)} µs")
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
        if args.rcmd == "walkforward":
            from .validation import walk_forward

            candles, source = _load_data(args.data)
            snap = DataSnapshot.of(candles, source, "BTC_USD", "1h")
            reg.save_data(candles, snap)
            folds = walk_forward(reg, Spec(name=args.family, engine=args.engine), json.loads(args.grid), candles, snap, args.folds,
                                 Path("runs/research/work"))
            for f in folds:
                if "error" in f:
                    print(f"fold {f['fold']}: {f['error']}")
                    continue
                print(f"fold {f['fold']}: train bars {f['train']}, test bars {f['test']}, chose {json.dumps(f['params'])}: "
                      f"train excess {f['train_excess']:+.2%} -> out-of-sample excess {f['test_excess']:+.2%}")
            oos = [f["test_excess"] for f in folds if "test_excess" in f]
            if oos:
                print(f"out-of-sample excess vs benchmark: mean {sum(oos) / len(oos):+.2%}, positive in {sum(x > 0 for x in oos)}/{len(oos)} folds")
            return 0
        if args.rcmd == "ablate":
            from .validation import ablate

            candles, source = _load_data(args.data)
            snap = DataSnapshot.of(candles, source, "BTC_USD", "1h")
            reg.save_data(candles, snap)
            lo, hi = _range(args.range) if args.range else (0, len(candles))
            out = ablate(reg, Spec(name=args.family, engine="ensemble", params=json.loads(args.base_params)), candles, snap, lo, hi,
                         Path("runs/research/work"))
            print(f"all signals on: {out['baseline']['trades']} trades, return {out['baseline']['total_return']:+.2%}")
            for k, v in out["signals"].items():
                print(f"  without {k:<9} decisions changed {v['decisions_changed']:6.1%}  trades {v['trades']:4d}  "
                      f"return {v['return_without']:+.2%} ({v['return_delta_vs_all']:+.2%})  -> {v['verdict']}")
            return 0
        if args.rcmd == "news-ablation":
            from .news import news_ablation

            candles, source = _load_data(args.data)
            snap = DataSnapshot.of(candles, source, args.instrument, args.timeframe)
            reg.save_data(candles, snap)
            base = Spec(name=args.family, engine=args.engine, instrument=args.instrument, timeframe=args.timeframe,
                        params=json.loads(args.base_params))
            out = news_ablation(reg, base, candles, snap, Path(args.news_db), Path("runs/research/work"),
                                fee_mults=tuple(float(x) for x in args.fees.split(",")))
            print(f"{out['headlines_used']} classified headlines (db sha {out['news_sha']}); every run recorded")
            for k, v in out["table"].items():
                print(f"  {k:<22} " + (f"return {v['total_return']:+.2%}  trades {v['trades']:4d}  max DD {v['max_drawdown']:.1%}" if v else "FAILED"))
            for mode, v in out["verdict"].items():
                diffs = ", ".join(f"{d:+.2%}" for d in v["improvement_by_fee"])
                print(f"  {mode:<5} vs no news, by fee level: {diffs}  -> {'helps at every fee level' if v['helps_at_every_fee'] else 'NO consistent improvement'}")
            return 0
        if args.rcmd == "mm":
            from .marketmaking import MMParams, simulate

            candles, source = _load_data(args.data)
            r = simulate(candles, MMParams(gamma=args.gamma, kappa=args.kappa, horizon_bars=args.horizon, max_inventory=args.max_inventory,
                                           maker_fee_bps=args.maker_fee_bps, quote_cost_bps=args.quote_cost_bps))
            h = r["honesty"]
            print(f"{source}: {r['fills']} fills ({r['buys']} buys / {r['sells']} sells), inventory peak {r['max_abs_inventory']}, "
                  f"P&L ${r['pnl_usd']:,.2f} on ${r['volume_usd']:,.0f} volume ({r['pnl_bps_of_volume']:+.1f} bps)")
            print(f"quoted spread {r['avg_spread_quoted_bps']:.1f} bps; captured {h['captured_bps']:.2f} bps per fill; "
                  f"fees + quote costs {h['fees_and_quote_costs_bps']:.2f}; adverse selection (5-bar markout) {h['adverse_selection_bps']:.2f}")
            print(f"net edge {h['net_edge_bps']:+.2f} bps per fill -> {h['verdict']}")
            print("research only: there is no live path for market making in this system")
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
        from .live import init_caps
        from .release import init_limits

        print(f"limits at {init_limits()}; edit to tighten (code refuses anything looser than the hard caps)")
        print(f"live canary caps at {init_caps()}; dollars per order / position / day. Start small.")
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
                      PolicyParams(), RiskLimits(), None, exechub=args.exechub, reviewer=args.reviewer,
                      reviewer_mode=args.reviewer_mode)
            return 0
        try:
            spec, limits, rel = verify_release(Path(args.release))
        except ReleaseError as err:
            print(f"refusing to start: {err}\n(use --dev for an unpromotable test run)")
            return 2
        params = PolicyParams(**{**asdict(PolicyParams()), **spec.params})
        run_paper(spec.instrument, spec.timeframe, spec.engine, Path(args.run_dir), args.cash, params, limits, rel, exechub=args.exechub,
                  reviewer=args.reviewer, reviewer_mode=args.reviewer_mode)
        return 0

    if args.cmd == "go-live":
        from .experiments import Register
        from .live import authorize_live, launch_gates

        reg = Register()
        if args.gates_only:
            gates = launch_gates(Path(args.paper_dir), reg)
            for g in gates:
                print(f"{'PASS' if g.passed else 'FAIL'}  {g.name}: {g.detail}")
            return 0 if all(g.passed for g in gates) else 1
        auth = authorize_live(reg, Path(args.paper_dir), args.operator, args.venue, accept_unproven=args.accept_unproven)
        for g in auth["gates"]:
            print(f"{'PASS' if g['passed'] else 'FAIL'}  {g['name']}: {g['detail']}")
        flag = f"  UNPROVEN: {auth['unproven_accepted']}" if auth["unproven_accepted"] else ""
        print(f"live authorized for {auth['venues']} until {auth['expires_ms']} (7 days), caps {auth['caps']}{flag}")
        return 0

    if args.cmd == "live":
        from .live import require_live
        from .live_runner import run_live
        from .policy import PolicyParams
        from .release import verify_release

        require_live(args.venue)
        spec, limits, rel = verify_release(Path(args.release))
        params = PolicyParams(**{**asdict(PolicyParams()), **spec.params})
        inst = args.instrument or spec.instrument
        run_live(args.venue, inst, spec.timeframe, spec.engine, Path(args.run_dir or f"runs/live_{args.venue}_{inst}"),
                 params, limits, rel)
        return 0

    if args.cmd == "live-check":
        from .live import require_live

        auth = require_live(args.venue)
        print(f"authorization ok for {args.venue}; expires {auth.record['expires_ms']}; caps {auth.record['caps']}")
        if args.venue == "solana":
            from .solana_live import SolanaLiveTrader

            t = SolanaLiveTrader.from_env()
            from .onchain import USDC_MINT

            print(f"wallet {t.keypair.pubkey()}: USDC {t.token_balance(USDC_MINT) / 1e6:,.2f}")
            return 0
        from .venues_live import build_live_client

        c = build_live_client(args.venue, auth)
        if args.venue == "cryptocom":
            bal = c.balances()
            print("balances:", {k: v for k, v in bal.items() if v})
        elif args.venue == "alpaca":
            print(f"cash {c.cash():,.2f}; market open {c.clock_open()}")
        else:
            print("account summary:", c._call("GET", "/summary")["account"].get("balance"))
        return 0

    if args.cmd in ("sol-buy", "sol-sell"):
        from .solana_live import SolanaLiveTrader

        t = SolanaLiveTrader.from_env(use_jito=not args.no_jito)
        out = t.buy(args.mint, args.usdc, args.pool_owner, args.lp_mint) if args.cmd == "sol-buy" else t.sell_all(args.mint)
        print(json.dumps(out, indent=2, default=str))
        return 0

    if args.cmd == "alert":
        from .alerts import Alerts

        ok = Alerts()(args.level, args.message)
        print("sent" if ok else "not sent (TRADEBOT_ALERT_WEBHOOK unset or unreachable)")
        return 0 if ok else 1

    if args.cmd == "news":
        from .news import classify, import_csv
        from .sentiment import SentimentStore

        store = SentimentStore(args.db)
        if args.ncmd == "import":
            print(import_csv(store, args.csv, int(args.seen_delay_min * 60_000)))
        else:
            print(classify(store, args.by))
        return 0

    if args.cmd == "replay":
        from .replay import build_replay_engine, replay

        out = replay(Path(args.run_dir), build_replay_engine(args.engine), limit=args.limit)
        print(json.dumps(out, indent=2))
        return 0

    if args.cmd == "runlog":
        from . import runlog

        if args.add:
            if not args.done:
                raise ValueError("--add needs at least one --done")
            print(runlog.append(Path(args.run_dir), args.done, args.decided, args.open_items, args.rejected))
        print(runlog.recent(Path(args.run_dir), args.n) or "(empty)")
        return 0

    if args.cmd == "predmarkets":
        from .predmarkets import scan

        rows = scan(json.loads(Path(args.pairs).read_text()), contracts=args.contracts)
        for r in rows:
            pr = r["pair"]
            if r["error"]:
                print(f"{pr['polymarket_slug']} / {pr['kalshi_ticker']}: {r['error']}")
                continue
            b = r["best"]
            flag = "LOCKED (if the rules truly match)" if b["locked"] and r["tradable"] else "no locked edge"
            print(f"{pr['polymarket_slug'][:40]} / {pr['kalshi_ticker']}: YES on {b['buy_yes_on']} @{b['yes_ask']:.3f} + NO on "
                  f"{b['buy_no_on']} @{b['no_ask']:.3f}: gross {b['gross_per_contract']:+.4f}, fees {b['fees_per_contract']:.4f}, "
                  f"net {b['net_per_contract']:+.4f}/contract (${b['net_usd']:+.2f} on {args.contracts:g}) -> {flag}")
        print("read-only: nothing was ordered. Check both venues' resolution rules before treating any gap as risk-free.")
        return 0

    if args.cmd == "jev-check":
        import time as _t

        from .jev import JevClient, noul

        c = JevClient(deadline_s=10)
        t0 = _t.perf_counter()
        r = c.ask("Payouts have been failing for three days.", {"urgent": noul("This conveys urgency")})
        print(f"ok: model {r['model']}, urgent={r['answers']['urgent']['noul']:.2f}, {(_t.perf_counter() - t0) * 1000:.0f} ms, "
              f"usage {r.get('usage')}")
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
    except (FrozenError, ReleaseError, ValueError, KeyError, RuntimeError) as e:
        print(f"refused: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(entry())
