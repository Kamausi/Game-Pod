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
    if spec == "alpaca":
        end = date.today()
        return ins.AlpacaBars((end - timedelta(days=365 * years)).isoformat(), end.isoformat())
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
    return 2


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
    if a.seat == "weekly":
        _print(dk.weekly(d, day))
        return 0
    prices = _prices(a.prices)
    if a.seat in ("scout", "morning"):
        out = dk.scout(d, day, prices=prices)
        print(f"scout: {len(out['names'])} names ({out['scanned_filings']} filings scanned)" + (f"; source problems: {len(out['errors'])}" if out["errors"] else ""))
        if a.seat == "scout":
            return 0
    if a.seat in ("analyst", "morning"):
        out = dk.analyst(d, day, prices=prices)
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
        out = {k: v for k, v in res.items() if k != "trades_list"}
        out.update(signals_build=len(build), signals_held_out=len(hold), holdout_starts=cut, data=stats, run=n,
                   verdict=ins.verdict(res, None),
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
    if a.what == "scan":
        out = wx.scan(a.slug, model=wx.ErrorModel(a.log), bankroll=a.bankroll)
        pend.parent.mkdir(parents=True, exist_ok=True)
        with pend.open("a") as fh:
            fh.write(json.dumps({"station": out["station"], "day": out["day"], "forecast_f": out["forecast"]["max_f"], "lead_h": out["forecast"]["lead_h"]}) + "\n")
        _print(out)
        return 0
    rows = [json.loads(x) for x in pend.read_text().splitlines() if x.strip()] if pend.exists() else []
    keep, done = [], []
    for r in rows:
        if r["day"] >= date.today().isoformat():
            keep.append(r)
            continue
        try:
            done.append(wx.settle(a.log, r["station"], r["day"], r["forecast_f"], r["lead_h"]))
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
    if a.what == "secrets":
        hits = audit.scan_secrets(a.path)
        _print({"hits": hits, "rule": "never use a key you find, never publish it; tell the owner"})
        return 1 if hits else 0
    return 2
