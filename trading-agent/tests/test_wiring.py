"""The quant tools wired into decisions: weather ensemble + calibration, backtest statistics and
the deflated verdict, the market-impact gate, the CUSUM decay alarm, parallel price downloads."""
import json
import math
import random
import types

import pytest

from tradebot import desk as dk, insider as ins, quant as q, weather as wx
from test_desk import FX, FakeBroker, form4, nws_get, run_chain


# ------------------------------------------------------------------ weather


def ens_get():
    base = nws_get()
    ens = json.loads((FX / "openmeteo_ensemble_klga.json").read_text())

    def get(u):
        return ens if u.startswith(wx.OPEN_METEO) else base(u)
    return get


def test_parse_real_ensemble_fixture():
    by = wx.parse_ensemble(json.loads((FX / "openmeteo_ensemble_klga.json").read_text()), "2026-10-16")
    assert sorted(by) == ["ecmwf_ifs025_ensemble", "ncep_gefs025"] and len(by["ncep_gefs025"]) == 6
    assert max(by["ncep_gefs025"]) == 78.3 and min(by["ecmwf_ifs025_ensemble"]) == 72.5
    assert wx.parse_ensemble({"hourly": {"time": ["2026-10-17T00:00"], "temperature_2m": [50]}}, "2026-10-16") == {}


def test_ensemble_dressing_is_a_proper_distribution():
    members = [71.7, 72.5, 75.5, 76.7, 77.4, 78.3]
    edges = [(-math.inf, 69), (70, 71), (72, 73), (74, 75), (76, 77), (78, 79), (80, math.inf)]
    ps = [wx.ensemble_bucket_prob(wx.Bucket("", lo, hi, 0, 0, 0, True, ""), members) for lo, hi in edges]
    assert sum(ps) == pytest.approx(1.0) and max(ps) == ps[4]


def test_blend_scan_uses_both_sources_and_falls_back(tmp_path):
    out = wx.scan("highest-temperature-in-nyc-on-october-16-2026", ens_get(), source="blend")
    assert out["source"] == "blend" and out["ensemble"]["by_model"] == {"ncep_gefs025": 6, "ecmwf_ifs025_ensemble": 6}
    assert out["prob_sum"] == pytest.approx(1.0, abs=1e-6)
    for r in out["buckets"]:
        assert r["model_p"] == pytest.approx(0.5 * (r["nws_p"] + r["ensemble_p"]), abs=1e-4)
    down = wx.scan("highest-temperature-in-nyc-on-october-16-2026", nws_get(), source="blend")  # ensemble host missing
    assert down["source"] == "nws" and "NWS only" in down["notes"][0]
    with pytest.raises(KeyError):
        wx.scan("highest-temperature-in-nyc-on-october-16-2026", nws_get(), source="ensemble")


def test_settle_feeds_calibration_and_the_ensemble_error_model(tmp_path, monkeypatch):
    log = tmp_path / "w.jsonl"
    monkeypatch.setattr(wx, "observed_max", lambda st, d, get=None: {"max_f": 75, "quality": "APPROX"})
    for i in range(30):
        wx.settle(log, "KLGA", f"2026-09-{i + 1:02d}", 74.0, 30, ens_mean=77.0, ens_spread=1.0,
                  buckets=[[None, 73, 0.2], [74, 75, 0.5], [76, None, 0.3]])
    prm = wx.ErrorModel(log).params("KLGA", 30, "ensemble")
    assert prm["source"] == "learned" and prm["bias"] == pytest.approx(2.0) and prm["kernel_sd"] == pytest.approx(0.5)
    assert wx.ErrorModel(log).params("KLGA", 30)["bias"] == pytest.approx(-1.0)  # the NWS rows are kept apart
    cal = wx.calibration(log)
    assert cal["n"] == 90 and cal["days"] == 30 and cal["base_rate"] == pytest.approx(1 / 3)
    assert cal["brier"] == pytest.approx((0.2 ** 2 + 0.5 ** 2 + 0.3 ** 2) / 3)


# ------------------------------------------------------------------ backtest statistics


def test_event_car_removes_the_market_part_of_the_return():
    rng = random.Random(3)
    n, beta = 200, 1.5
    m = [rng.gauss(0, 0.01) for _ in range(n)]
    spy, stk = [100.0], [50.0]
    for i in range(1, n):
        spy.append(spy[-1] * (1 + m[i]))
        stk.append(stk[-1] * (1 + beta * m[i] + rng.gauss(0, 0.002)))
    day = lambda i: f"2026-{1 + i // 28:02d}-{1 + i % 28:02d}"
    bars = [ins.DayBar(day(i), stk[i], stk[i] * 1.01, stk[i] * 0.99, stk[i], 1e6) for i in range(n)]
    bench = {day(i): ins.DayBar(day(i), spy[i], spy[i], spy[i], spy[i], 1e6) for i in range(n)}
    k, e = 150, 154
    car, b = ins._event_car(bars, k, e, bars[e].c, bench, cost=0.0)
    raw = bars[e].c / bars[k].o - 1
    assert b == pytest.approx(beta, abs=0.05) and abs(car) < 0.01 and abs(raw - car) > 0  # the move was the market's


def test_report_carries_psr_ci_and_the_deflated_verdict():
    T = lambda r: ins.TradeResult("X", "2026-01-01", f"2026-01-{1 + i % 28:02d}", 10, 9, "2026-02-01", 10, "t", r * 0.1, r, 0.0, None, None)
    rng = random.Random(5)
    trades = []
    for i in range(60):
        trades.append(T(rng.gauss(0.4, 1)))
    rep = ins.report(trades, {})
    assert 0 < rep["psr"] <= 1 and rep["avg_r_ci95"][0] < rep["avg_r"] < rep["avg_r_ci95"][1]
    rep.update(excess_vs_bench=0.01)
    rs = [t.r for t in trades]
    assert ins.verdict(rep, None, q.dsr(rs, 1)).startswith(("NOT FINISHED", "NOT PROVEN"))
    assert ins.verdict(rep, None, q.dsr(rs, 500, var_trials_sr=0.05)).startswith("NO EDGE (deflated)")


def test_backtest_logs_trials_for_the_deflated_sharpe(tmp_path):
    d = dk.Desk(tmp_path / "desk")
    d.init()
    res = {"trades": 3, "avg_r": 0.2, "win_rate": 0.6, "excess_vs_bench": 0.01, "sharpe_per_trade": 0.3,
           "trades_list": [{"r": r, "entry_date": f"2026-01-0{i + 1}"} for i, r in enumerate([0.5, -0.2, 0.4])]}
    dk.log_change(d, {"a": 1}, res, tmp_path)
    assert dk.deflated(d, res)["trials"] == 1
    dk.log_change(d, {"a": 2}, {**res, "sharpe_per_trade": 0.1}, tmp_path)
    out = dk.deflated(d, res)
    assert out["trials"] == 2 and out["dsr"] < q.psr([0.5, -0.2, 0.4])


# ------------------------------------------------------------------ tickets and live monitoring


def test_ticket_carries_yang_zhang_vol_and_the_impact_gate(tmp_path):
    d, day, prices, _, _, _ = run_chain(tmp_path, form4(shares=4000))
    t = dk.make_ticket(1, "ACME", prices.bars("ACME"), 100_000, d.limits(), day, {})
    assert t["vol_yz_daily"] is not None and 0 < t["impact_bps"] < 25
    acct = {"equity": "100000", "last_equity": "100000"}
    assert not any("impact" in f for f in dk.risk_check(t, acct, [], [], d.limits(), d, True))
    big = {**t, "impact_bps": 60.0}
    assert any("market impact 60 bps" in f for f in dk.risk_check(big, acct, [], [], d.limits(), d, True))


def test_weekly_flags_decay_by_cusum_before_20_trades(tmp_path):
    d = dk.Desk(tmp_path / "desk")
    d.init()
    d.path("quant", "latest.json").write_text(json.dumps({"params": {}, "build": {"avg_r": 0.5, "r_std": 1.0}}))
    fields = ["date", "ticker", "outcome", "pnl_r"]
    rows = [{"date": f"2026-09-{i + 1:02d}", "ticker": f"T{i}", "outcome": "FILLED", "pnl_r": "-1.0"} for i in range(12)]
    import csv
    with d.path("journal", "trades.csv").open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)
    out = dk.weekly(d, "2026-09-30")
    assert out["status"] == "DECAYING (CUSUM)" and any("CUSUM" in x for x in out["lessons"])


# ------------------------------------------------------------------ efficiency


def test_rate_limit_spaces_calls_and_fetch_prices_runs_in_parallel(tmp_path):
    now, slept = [0.0], []

    def sleep(s):
        slept.append(s)
        now[0] += s
    rl = ins.RateLimit(120, clock=lambda: now[0], sleep=sleep)
    for _ in range(3):
        rl.wait()
    assert slept == pytest.approx([0.5, 0.5])

    import threading
    seen = set()

    class Src:
        def bars(self, s):
            seen.add(threading.current_thread().name)
            return [ins.DayBar("2026-10-01", 1, 2, 0.5, 1.5, 10)]
    rows = ins.fetch_prices([f"S{i}" for i in range(12)], Src(), tmp_path, workers=4, per_minute=1e9)
    assert len(rows) == 13 and all(r["bars"] == 1 for r in rows) and [r["symbol"] for r in rows] == sorted(r["symbol"] for r in rows)


def test_audit_cli_quant_commands(tmp_path, capsys):
    from tradebot.cli import main
    rng = random.Random(7)
    (tmp_path / "s.csv").write_text("date,return\n" + "".join(f"2026-01-{1 + i % 28:02d},{rng.gauss(0.001, 0.01)}\n" for i in range(300)))
    rc = main(["audit", "dsr", str(tmp_path / "s.csv"), "--trials", "200"])
    out = json.loads(capsys.readouterr().out)
    assert out["trials"] == 200 and out["sr_benchmark"] > 0 and rc in (0, 1)
    (tmp_path / "m.csv").write_text("a,b,c\n" + "".join(f"{rng.gauss(0, 1)},{rng.gauss(0, 1)},{rng.gauss(0, 1)}\n" for _ in range(100)))
    main(["audit", "hrp", str(tmp_path / "m.csv")])
    assert sum(json.loads(capsys.readouterr().out)["hrp_weights"].values()) == pytest.approx(1)
    (tmp_path / "c.csv").write_text("p,y\n0.9,1\n0.8,1\n0.2,0\n0.3,1\n")
    main(["audit", "calibration", str(tmp_path / "c.csv")])
    assert json.loads(capsys.readouterr().out)["brier"] == pytest.approx((0.01 + 0.04 + 0.04 + 0.49) / 4)
