"""Command handlers for desk, insider, weather, jevquote and audit (kept out of cli.py)."""
from __future__ import annotations

import json
import os
from dataclasses import asdict
from datetime import date, timedelta
from pathlib import Path


def _print(x) -> None:
    print(json.dumps(x, indent=1, default=str))


def _prices(spec: str, years: int = 4):
    from . import insider as ins
    end = date.today()
    start = (end - timedelta(days=365 * years)).isoformat()
    if spec == "alpaca":
        return ins.AlpacaBars(start, end.isoformat())
    if spec == "openbb" or spec.startswith("openbb:"):
        from .platforms import OpenBBPrices
        return OpenBBPrices(start, end.isoformat(), provider=spec.partition(":")[2] or "yfinance")
    return ins.CsvPrices(spec)


def _alert(level: str, title: str, **fields) -> None:
    try:
        from .alerts import Alerts
        Alerts()(level, title, **fields)
    except Exception:
        pass  # alerts are best effort; the file and the exit code still say what happened


def run(args) -> int:
    if args.cmd == "desk":
        return _desk(args)
    if args.cmd == "insider":
        return _insider(args)
    if args.cmd == "weather":
        return _weather(args)
    if args.cmd == "jevquote":
        return _jevquote(args)
    if args.cmd == "audit":
        return _audit(args)
    if args.cmd == "tokencheck":
        return _tokencheck(args)
    if args.cmd == "export":
        return _export(args)
    if args.cmd == "ccxt":
        return _ccxt(args)
    if args.cmd == "universe":
        return _universe(args)
    if args.cmd == "whales":
        return _whales(args)
    if args.cmd == "playbook":
        return _playbook(args)
    if args.cmd == "scan":
        return _scan(args)
    return 2


def _scan(a) -> int:
    from . import scanner as sc
    load = lambda f: json.loads(Path(f).read_text()) if f else None
    if a.markets or a.trending or a.glob or a.tickers:
        src = {"markets": load(a.markets), "trending": load(a.trending), "glob": load(a.glob), "tickers": load(a.tickers)}
        if isinstance(src["tickers"], dict):
            src["tickers"] = (src["tickers"].get("result") or src["tickers"]).get("data")
    else:
        src = sc.fetch_all()
    kw = dict(n=a.n, venue=a.venue, maker=not a.taker, order_usd=a.order, min_volume_usd=a.min_volume, max_majors=a.max_majors)
    if a.criteria:
        kw["order"] = tuple(c.strip() for c in a.criteria.split(",") if c.strip())
    res = sc.scan(src["markets"], src["trending"], src["glob"], src["tickers"], top=a.top, **kw)
    print(sc.report(res))
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1))
    return 0


def _desk(a) -> int:
    from . import desk as dk
    d, day = dk.Desk(a.dir), a.date or dk.today_ny()
    if a.seat == "init":
        _print({"created": d.init(), "next": "put your Alpaca PAPER keys in the environment (APCA_API_KEY_ID / APCA_API_SECRET_KEY), set SEC_USER_AGENT, then: tradebot desk morning"})
        return 0
    if a.seat in ("pause", "resume"):
        print(dk.pause(d, a.seat == "pause"))
        return 0
    if a.seat == "chain-check":
        msg = dk.chain_check(d, day)
        if msg:
            _alert("warn", msg, desk=str(d.root))
            print(msg)
            return 1
        print("chain complete for", day)
        return 0
    if a.seat == "health":
        files = {s: d.inbox(day, s, "json").exists() for s in ("scout", "analyst", "skeptic", "tickets")}
        _print({"date": day, "files": files, "halted": d.halted(), "limits": d.limits(),
                "open_trades": len(json.loads(d.path("journal", "open.json").read_text())) if d.path("journal", "open.json").exists() else 0})
        return 0
    if a.seat == "audit":
        rep = dk.audit_quant(Path(a.repo), d)
        _print(rep)
        return 0 if all(r["rating"] != "FAIL" for r in rep) else 1
    if a.seat == "graph":
        from . import graph as gr
        for e in gr.evaluate(d):
            v = "—" if e["value"] is None else f"{e['value']:+.3f}"
            print(f"{e['name']:<10} {e['route']:<6} {e['metric']:<28} {v:>8} target {'>=' if e['better'] == 'higher' else '<='} {e['target']:<5} {e['status']}")
        return 0
    if a.seat == "memory":
        from . import graph as gr
        ts = [t for t in gr.recall_all(d) if not a.symbol or t["symbol"] == a.symbol.upper()]
        _print(ts[-30:])
        return 0
    if a.seat == "weekly":
        _print(dk.weekly(d, day))
        return 0
    if a.seat == "schedule":
        from . import schedule as sc
        if a.once:
            seat, at = sc.next_run(sc.datetime.now(sc.NY))
            print(f"next: {seat} at {at.isoformat()}")
            return 0
        return sc.loop(str(d.root), a.prices, a.llm, a.llm_effort, alert=_alert)
    prices = _prices(a.prices)
    if a.seat in ("scout", "morning"):
        out = dk.scout(d, day, prices=prices)
        print(f"scout: {len(out['names'])} names ({out['scanned_filings']} filings scanned)" + (f"; source problems: {len(out['errors'])}" if out["errors"] else ""))
        if a.seat == "scout":
            return 0
    if a.seat in ("analyst", "morning"):
        seat = None
        if a.llm:
            from .llm_analyst import ClaudeSeat
            seat = ClaudeSeat(effort=a.llm_effort)
        out = dk.analyst(d, day, prices=prices, seat=seat)
        print("analyst:", ", ".join(f"{c['symbol']} {c['verdict']}" for c in out["cards"]) or "nothing to price")
        if a.seat == "analyst":
            return 0
    if a.seat in ("skeptic", "morning"):
        out = dk.skeptic(d, day, prices)
        print("skeptic:", ", ".join(f"{v['symbol']} {v['verdict']}" for v in out["verdicts"]) or "nothing to judge")
        if a.seat == "skeptic":
            return 0
    if a.seat in ("risk", "morning"):
        out = dk.risk(d, day, dk.PaperDeskBroker(), prices)
        print("\n".join(out["brief"]))
        if out["tickets"]:
            print("approve with: tradebot desk approve", " ".join(str(t["no"]) for t in out["tickets"]))
            _alert("info", "desk brief", lines=" | ".join(out["brief"]))
        return 0
    if a.seat == "approve":
        _print(dk.approve(d, day, a.numbers, dk.PaperDeskBroker()))
        return 0
    if a.seat == "exit-due":
        _print(dk.exit_due(d, day, dk.PaperDeskBroker(), prices))
        return 0
    if a.seat == "kill":
        out = dk.kill(d, dk.PaperDeskBroker())
        _alert("critical", "desk KILL", positions=len(out["positions"]))
        _print(out)
        return 0
    if a.seat == "journal":
        _print(dk.journal(d, day, prices))
        return 0
    return 2


def _insider(a) -> int:
    from . import desk as dk
    from . import insider as ins
    if a.what == "fetch":
        rows = ins.fetch_datasets(ins.quarter_list(a.quarters), a.out)
        _print(rows)
        return 1 if any(r["status"] == "FAILED" for r in rows) else 0
    if a.what == "prices":
        syms = set()
        for src in a.data:
            syms |= {s.symbol for s in ins.load_form345(src, a.min_usd)[0]}
        if a.source == "alpaca":
            src_ = ins.AlpacaBars(a.start, date.today().isoformat())
        else:
            from .platforms import OpenBBPrices
            src_ = OpenBBPrices(a.start, provider=a.source.partition(":")[2] or "yfinance")
        rows = ins.fetch_prices(sorted(syms), src_, a.out)
        missing = [r["symbol"] for r in rows if not r["bars"]]
        _print({"symbols": len(rows), "with_bars": len(rows) - len(missing), "missing": missing,
                "note": "missing names are mostly delisted: the survivorship hole the backtest reports as 'unpriced'", "out": a.out})
        return 0
    sigs, stats = [], []
    for src in a.data:
        s, st = ins.load_form345(src, 0 if a.study_filters else a.min_usd, any_insider=a.study_filters)
        sigs += s
        stats.append({"source": src, **st})
    build, hold, cut = ins.split_holdout(sigs, a.holdout_months)
    params = {"min_usd": a.min_usd, "min_adv": a.min_adv, "atr_mult": a.atr_mult, "hold": a.hold, "cost": a.cost, "study_filters": a.study_filters}
    kw = dict(hold=a.hold, atr_mult=1e9 if a.study_filters else a.atr_mult, cost=a.cost, min_adv=0 if a.study_filters else a.min_adv,
              include_batches=a.study_filters, include_plans=a.study_filters)
    prices = _prices(a.prices, years=25)
    desk = dk.Desk(a.desk)
    desk.init()
    if a.what == "backtest":
        res = ins.backtest(build, prices, **kw)
        n = dk.log_change(desk, params, res, Path.cwd())
        dsr = dk.deflated(desk, res)
        out = {k: v for k, v in res.items() if k != "trades_list"}
        out.update(signals_build=len(build), signals_held_out=len(hold), holdout_starts=cut, data=stats, run=n, deflated_sharpe=dsr,
                   verdict=ins.verdict(res, None, dsr),
                   warning="more than 5 changes since the first run: you are fitting noise" if n > 6 else None,
                   bias="survivorship: delisted names can't be priced from today's symbols; the 'unpriced' count is the size of that hole; treat results as a best case")
        _print(out)
        return 0
    lp = desk.path("quant", "latest.json")
    if not lp.exists():
        raise SystemExit("run `tradebot insider backtest` first")
    latest = json.loads(lp.read_text())
    if latest.get("params") != params:
        raise SystemExit(f"the holdout runs the LAST built configuration only: {latest.get('params')}")
    dk.holdout_lock(desk, params, ",".join(a.data))
    res = ins.backtest(hold, prices, **kw)
    out = {k: v for k, v in res.items() if k != "trades_list"}
    out.update(verdict=ins.verdict(latest["build"], res), holdout_starts=cut, note="reported even if it kills the idea; it cannot be run again")
    _print(out)
    return 0


def _weather(a) -> int:
    from . import weather as wx
    pend = Path(a.log).with_name(Path(a.log).stem + "_pending.jsonl")
    if a.what == "calibration":
        _print(wx.calibration(a.log))
        return 0
    if a.what == "scan":
        out = wx.scan(a.slug, model=wx.ErrorModel(a.log), bankroll=a.bankroll, source=a.source)
        pend.parent.mkdir(parents=True, exist_ok=True)
        ens = out.get("ensemble") or {}
        with pend.open("a") as fh:
            fh.write(json.dumps({"station": out["station"], "day": out["day"], "forecast_f": out["forecast"]["max_f"], "lead_h": out["forecast"]["lead_h"],
                                 "ens_mean": ens.get("mean"), "ens_spread": ens.get("spread"),
                                 "buckets": [[b["lo"], b["hi"], b["model_p"]] for b in out["buckets"]]}) + "\n")
        _print(out)
        return 0
    rows = [json.loads(x) for x in pend.read_text().splitlines() if x.strip()] if pend.exists() else []
    keep, done = [], []
    for r in rows:
        if r["day"] >= date.today().isoformat():
            keep.append(r)
            continue
        try:
            done.append(wx.settle(a.log, r["station"], r["day"], r["forecast_f"], r["lead_h"], ens_mean=r.get("ens_mean"),
                                  ens_spread=r.get("ens_spread"), buckets=r.get("buckets")))
        except Exception as e:
            keep.append(r)
            done.append({"day": r["day"], "error": str(e)[:160]})
    pend.write_text("".join(json.dumps(r) + "\n" for r in keep))
    _print(done)
    return 0


def _jevquote(a) -> int:
    from . import jevquote as jq
    if a.record:
        from .predmarkets import _get
        path = a.replay or f"runs/book_{a.record}.jsonl"
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        print("recorded", jq.record_live(a.record, a.steps, a.interval, _get, path), "snapshots to", path)
        a.replay = path
    if not a.replay:
        raise SystemExit("give --replay FILE or --record INSTRUMENT")
    books = jq.read_replay(a.replay)
    decide = jq.jev_decider() if a.model == "jev" else jq.momentum_decider()
    Path(a.log).parent.mkdir(parents=True, exist_ok=True)
    _print(jq.run(books, decide, a.tick, a.size, a.max_inventory, a.maker_fee_bps, a.log))
    return 0


def _audit(a) -> int:
    from . import audit
    if a.what == "spec":
        _print(audit.spec({}))
        return 0
    if a.what == "headline":
        _print(audit.headline_check(audit.read_series(a.series), a.annual_cost_pct, a.tax_pct, a.slippage_pct))
        return 0
    if a.what == "finding":
        _print(audit.finding(audit.read_series(a.strategy), audit.read_series(a.benchmark), a.split))
        return 0
    if a.what == "trend200":
        import csv
        rows = list(csv.DictReader(open(a.prices)))
        k = {c.lower(): c for c in rows[0]}
        closes = [(r[k.get("date", list(rows[0])[0])][:10], float(r[k.get("close") or k.get("value")])) for r in rows]
        rule = audit.trend200(closes)
        out = {"trend200": audit.headline_check(rule)}
        if a.benchmark:
            bh = [(d, c / p - 1) for (d, c), (_, p) in zip(closes[1:], closes)]
            out["buy_and_hold"] = audit.headline_check(bh)
        _print(out)
        return 0
    if a.what in ("dsr", "pbo", "vr", "hrp", "calibration", "vol"):
        return _audit_quant(a)
    if a.what == "secrets":
        hits = audit.scan_secrets(a.path)
        _print({"hits": hits, "rule": "never use a key you find, never publish it; tell the owner"})
        return 1 if hits else 0
    return 2


def _tokencheck(a) -> int:
    import csv
    from . import bundlecheck as bc
    from .onchain import SolanaRPC
    if a.callouts and a.prices:
        prices = [(int(float(r[0])), float(r[1])) for r in csv.reader(open(a.prices)) if r and r[0][0].isdigit()]
        calls = [(int(float(r[0])), r[1] if len(r) > 1 else "") for r in csv.reader(open(a.callouts)) if r and r[0][0].isdigit()]
        _print(bc.callout_check(prices, calls))
        return 0
    rep = bc.check(a.mint, SolanaRPC(a.rpc), early_slots=a.early_slots, max_pages=a.max_pages)
    _print(asdict(rep))
    return 0 if rep.verdict == "NO BUNDLE FOUND" else 1


def _export(a) -> int:
    import csv
    from .platforms import export_freqtrade
    sig = None
    if a.kind == "signals":
        if not a.signals:
            raise SystemExit("--kind signals needs --signals pair,date CSV")
        sig = [(r[0], r[1]) for r in csv.reader(open(a.signals)) if len(r) >= 2 and r[1][:1].isdigit()]
    src = export_freqtrade(a.kind, sig, a.stop_pct, a.hold_days, a.timeframe, a.name)
    compile(src, a.out, "exec")
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_text(src)
    print(f"wrote {a.out}: freqtrade backtesting --strategy {a.name} --timeframe {a.timeframe}")
    return 0


def _ccxt(a) -> int:
    from .platforms import CcxtVenue
    if a.what == "exchanges":
        from .platforms import _optional
        ccxt = _optional("ccxt", "ccxt")
        print(len(ccxt.exchanges), "exchanges:", " ".join(ccxt.exchanges))
        return 0
    v = CcxtVenue(a.exchange, sandbox=a.what == "balance")
    if a.what == "ticker":
        _print(v.ticker(a.symbol))
    elif a.what == "book":
        _print(v.book(a.symbol, a.limit))
    elif a.what == "bars":
        _print([asdict(b) for b in v.bars(a.symbol, a.timeframe, limit=a.limit)])
    else:
        _print(v.balance())
    return 0


def _columns(path: str) -> tuple[list[str], list[list[float]]]:
    import csv
    rows = list(csv.DictReader(open(path)))
    names = [c for c in rows[0] if c.lower() not in ("date", "time", "timestamp")]
    return names, [[float(r[c]) for r in rows] for c in names]


def _audit_quant(a) -> int:
    import math
    from . import audit, quant
    if a.what == "dsr":
        rets = [r for _, r in audit.read_series(a.series)]
        out = quant.dsr(rets, a.trials, a.trials_sharpe_var)
        out.update(psr_vs_zero=quant.psr(rets), min_track_record=quant.min_track_record(rets),
                   reading="real at 95% only if dsr >= 0.95; the benchmark is what the best of that many pure-noise tries would show")
        _print(out)
        return 0 if out["dsr"] >= 0.95 else 1
    if a.what == "pbo":
        names, cols = _columns(a.matrix)
        out = quant.pbo_cscv([list(r) for r in zip(*cols)], s=a.blocks)
        out.update(configurations=len(names), reading="PBO = how often the in-sample winner ranks below median out of sample; above 0.5 means selection is worse than a coin")
        _print(out)
        return 0
    if a.what == "vr":
        import csv
        rows = list(csv.DictReader(open(a.prices)))
        k = {c.lower(): c for c in rows[0]}
        px = [float(r[k.get("close") or k.get("value")]) for r in rows]
        _print([quant.variance_ratio(px, q) for q in a.q])
        return 0
    if a.what == "hrp":
        names, cols = _columns(a.matrix)
        cov, shrink = quant.ledoit_wolf(cols)
        _print({"hrp_weights": quant.hrp_weights(cols, names), "hrp_weights_shrunk_cov": quant.hrp_weights(cols, names, cov=cov),
                "ledoit_wolf_shrinkage": shrink})
        return 0
    if a.what == "calibration":
        import csv
        rows = list(csv.DictReader(open(a.csv)))
        p, y = [float(r["p"]) for r in rows], [int(float(r["y"])) for r in rows]
        _print({**quant.reliability(p, y), "log_loss": quant.log_loss(p, y)})
        return 0
    from . import insider as ins
    folder, sym = str(Path(a.bars).parent), Path(a.bars).stem
    bars = ins.CsvPrices(folder).bars(sym)[-(a.window + 1):]
    rets = [math.log(b.c / p.c) for p, b in zip(bars, bars[1:])]
    cc = math.sqrt(sum(r * r for r in rets) / (len(rets) - 1))
    g = quant.garch11([math.log(b.c / p.c) for p, b in zip(ins.CsvPrices(folder).bars(sym), ins.CsvPrices(folder).bars(sym)[1:])])
    _print({"window": len(rets), "daily": {"close_to_close": cc, "parkinson": quant.parkinson(bars[1:]), "garman_klass": quant.garman_klass(bars[1:]),
                                          "rogers_satchell": quant.rogers_satchell(bars[1:]), "yang_zhang": quant.yang_zhang(bars), "ewma_0.94": quant.ewma_vol(rets)},
            "garch11": g, "annualize": "x sqrt(252)"})
    return 0


def _universe(a) -> int:
    from . import universe as u
    ex = tuple(x.strip() for x in a.exclude.split(",") if x.strip())
    kw = dict(min_volume_usd=a.min_volume, order_usd=a.order, exclude=ex, top=a.top)
    if a.what == "crypto":
        rows = u.crypto_universe(u.fetch_cryptocom_tickers(), a.venue, not a.taker, kinds=("spot", "perp") if a.perps else ("spot",), **kw)
        head = f"Crypto.com tickers, fees {a.venue} {'taker' if a.taker else 'maker'}"
    else:
        rows = u.stock_universe(u.fetch_alpaca_snapshots(), not a.taker, **kw)
        head = "Alpaca most-active stocks (commission-free; SEC/FINRA fees on sells)"
    print(f"{head}, ${a.order:,.0f} orders, >= ${a.min_volume / 1e6:.1f}M/24h" + (f", excluding {','.join(ex)}" if ex else ""))
    print(u.table(rows))
    return 0


def _whales(a) -> int:
    from . import whales as wh
    from .onchain import SolanaRPC
    if a.what == "discover":
        import csv
        out = wh.discover(json.loads(Path(a.rank_json).read_text()), a.lag)
        Path(a.out).parent.mkdir(parents=True, exist_ok=True)
        with open(a.out, "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["wallet", "screened", "pnl_7d", "winrate_7d", "trades_per_day", "avg_hold_h", "tags", "source"])
            for c in out["candidates"]:
                w.writerow([c["wallet"], out["screened"], c["pnl_7d"], c["winrate_7d"], c["trades_per_day"], c["avg_hold_h"], " ".join(c["tags"]), c["source"]])
        _print({"screened": out["screened"], "candidates": len(out["candidates"]), "dropped": len(out["dropped"]),
                "dropped_why": {k: sum(1 for d in out["dropped"] if any(k in x for x in d["why"])) for k in ("wash trader", "arbitrager", "bot", "too fast", "inactive")},
                "wrote": a.out, "next": f"tradebot whales score --wallets-file {a.out}"})
        return 0
    rpc = SolanaRPC(a.rpc)
    if a.what == "score":
        import csv
        wallets = list(a.wallets)
        screened = a.screened
        if a.wallets_file:
            rows = [r for r in csv.DictReader(open(a.wallets_file)) if (r.get("wallet") or "").strip()]
            wallets += [r["wallet"].strip() for r in rows]
            # a discover file carries how many leaderboard wallets were read, not just the survivors
            screened = screened or max([len(rows)] + [int(r["screened"]) for r in rows if (r.get("screened") or "").isdigit()])
        if not wallets:
            raise SystemExit("give wallets or --wallets-file")
        labels = {r[0].strip(): r[1].strip() for r in csv.reader(open(a.labels)) if len(r) >= 2} if a.labels else None
        scores = []
        for w in wallets:
            trs: list = []
            sw = wh.history(rpc, w, a.max_tx, transfers=trs)
            born = None if a.no_age else wh.first_seen(rpc, w)[0]
            scores.append(wh.score(w, sw, a.lag, trs, labels, born))
        gates = wh.gate(scores, screened, a.lag, a.cost_bps)
        rows = wh.scorecard_rows(scores, gates)
        Path(a.out).parent.mkdir(parents=True, exist_ok=True)
        Path(a.out).write_text(json.dumps({"lag_s": a.lag, "cost_bps": a.cost_bps, "screened": a.screened or len(a.wallets), "wallets": rows}, indent=1, default=str))
        _print(rows)
        return 0
    from .onchain import DexPaperTrader, Jupiter, RugGuard
    g = json.loads(Path(a.gate).read_text()) if Path(a.gate).exists() else None
    if not g:
        raise SystemExit("run `tradebot whales score <wallets>` first: only wallets that pass are copied")
    ok = [w["wallet"] for w in g["wallets"] if w.get("copy")]
    if not ok:
        print("no wallet passed the gate: nothing to copy (that is the gate working)")
        return 0
    jup = Jupiter()
    trader = DexPaperTrader(RugGuard(rpc), jup)

    def sol_usd():
        return int(jup.quote(wh.WSOL, wh.USDC, 10**9)["outAmount"]) / 1e6
    _print(wh.watch(rpc, trader, ok, a.state, a.usdc, sleeve_usd=a.sleeve, max_chase=a.max_chase, sol_usd=sol_usd))
    return 0


def _playbook(a) -> int:
    from . import insider as ins
    from . import playbook as pb
    if a.what == "tokenomics":
        _print(pb.tokenomics(pb.coingecko(a.coin_id)))
        return 0
    if a.what == "tax":
        import csv
        out = pb.tax_lots(list(csv.DictReader(open(a.fills))))
        if a.out:
            Path(a.out).write_text(pb.tax_csv(out["rows"]))
        _print({k: v for k, v in out.items() if k != "rows"} | {"rows": len(out["rows"]), "file": a.out,
                                                                 "note": "a record for your tax software or accountant, not tax advice"})
        return 0
    if a.bars:
        bars = ins.CsvPrices(str(Path(a.bars).parent)).bars(Path(a.bars).stem)
    else:
        bars = pb.yahoo_daily(a.symbol, a.range)
    alt = not a.symbol.upper().startswith("BTC")
    load = lambda f: ins.CsvPrices(str(Path(f).parent)).bars(Path(f).stem)
    btc = bars if not alt else load(a.btc_bars) if a.btc_bars else pb.yahoo_daily("BTC-USD", a.range)
    if a.what == "regime":
        _print(pb.regime([b.c for b in btc]))
    elif a.what == "levels":
        _print({"close": bars[-1].c, "levels": pb.levels(bars), "fomo": pb.fomo(bars)})
    elif a.what == "setup":
        r = pb.regime([b.c for b in btc])["regime"]
        st = pb.swing_setup(bars, r, alt, a.fee)
        if st.get("setup"):
            st["size_at_1pct_of_10k"] = pb.size(10_000, st["entry_ref"], st["stop"])
        _print({"regime": r, **st})
    elif a.what == "backtest":
        out = pb.backtest(bars, pb.regime_series(btc) if alt else None, alt, a.fee)
        _print({k: v for k, v in out.items() if k != "trades_list"})
    else:
        _print(pb.dca(bars, a.amount, a.every, a.fee))
    return 0
