"""Claude analyst seat (debate + citation check), ccxt, OpenBB, freqtrade export, SEC downloads, scheduler."""
import io
import json
import types
import zipfile
from datetime import datetime

import pytest

from tradebot import insider as ins, llm_analyst as llm, platforms as pf, schedule as sc
from test_desk import form4, run_chain


# ------------------------------------------------------------------ Claude seat


def fake_create(answers, stop="end_turn", calls=None):
    """Stands in for client.beta.messages.create: answers[role] -> the JSON the model returns."""
    def create(**kw):
        if calls is not None:
            calls.append(kw)
        text = kw["messages"][0]["content"]
        role = next(r for r, t in llm.ROLE.items() if text.startswith(t))
        a = answers[role]
        body = a(text) if callable(a) else a
        return types.SimpleNamespace(stop_reason=stop, model="claude-opus-5-5",
                                     content=[types.SimpleNamespace(type="text", text=json.dumps(body))])
    return create


def ids_for(text, claim):
    """Fact id whose claim contains `claim` (the test reads the table the model would see)."""
    for line in text.splitlines():
        if line.startswith("F") and claim in line:
            return line.split(":")[0]
    raise AssertionError(claim)


def good_answers(verdict="RANGE", agrees=True):
    return {
        "bull": lambda t: {"points": [{"point": "The CEO bought stock in the open market.", "fact_ids": [ids_for(t, "insider role")]},
                                      {"point": f"The last close sits below the fair value base.", "fact_ids": [ids_for(t, "last close"), ids_for(t, "fair value base")]}]},
        "bear": lambda t: {"points": [{"point": "One insider's purchase is a single data point.", "fact_ids": [ids_for(t, "insider purchase")]}]},
        "judge": lambda t: {"verdict": verdict, "agrees_with_code": agrees, "summary": "price below own-history value",
                            "deciding_points": [{"point": "Price is below the base value.", "fact_ids": [ids_for(t, "fair value base")]}]},
    }


def test_request_shape_follows_the_api_defaults(tmp_path):
    calls = []
    seat = llm.ClaudeSeat(create=fake_create(good_answers(), calls=calls))
    run_chain(tmp_path, form4(shares=4000), seat=seat)
    kw = calls[0]
    assert kw["model"] == "claude-opus-5-5" and kw["output_config"]["effort"] == "high"
    assert kw["output_config"]["format"]["type"] == "json_schema" and "thinking" not in kw  # adaptive is the default on this model
    assert kw["system"][0]["cache_control"] == {"type": "ephemeral"}
    assert [c["messages"][0]["content"].split(".")[0] for c in calls] == [llm.ROLE[r].split(".")[0] for r in ("bull", "bear", "judge")]


def test_agreeing_grounded_seat_lets_the_name_pass(tmp_path):
    _, _, _, _, an, sk = run_chain(tmp_path, form4(shares=4000), seat=llm.ClaudeSeat(create=fake_create(good_answers())))
    op = an["cards"][0]["llm"]
    assert op["status"] == "AGREES", op["problems"]
    assert sk["verdicts"][0]["verdict"] == "PASS"


def test_disagreeing_judge_kills(tmp_path):
    _, _, _, _, an, sk = run_chain(tmp_path, form4(shares=4000), seat=llm.ClaudeSeat(create=fake_create(good_answers("EXPENSIVE", False))))
    assert an["cards"][0]["llm"]["status"] == "DISAGREES"
    assert sk["verdicts"][0]["verdict"] == "KILL" and "Claude judge disagrees" in sk["verdicts"][0]["why"]


def test_invented_number_or_missing_citation_kills(tmp_path):
    a = good_answers()
    a["bull"] = lambda t: {"points": [{"point": "Fair value is 81.37 per share.", "fact_ids": [ids_for(t, "fair value base")]},
                                      {"point": "Insiders know best.", "fact_ids": []}]}
    _, _, _, _, an, sk = run_chain(tmp_path, form4(shares=4000), seat=llm.ClaudeSeat(create=fake_create(a)))
    op = an["cards"][0]["llm"]
    assert op["status"] == "UNVERIFIED"
    assert any("81.37" in p for p in op["problems"]) and any("cites no fact" in p for p in op["problems"])
    assert sk["verdicts"][0]["verdict"] == "KILL"


def test_refusal_or_error_kills(tmp_path):
    _, _, _, _, an, sk = run_chain(tmp_path, form4(shares=4000), seat=llm.ClaudeSeat(create=fake_create(good_answers(), stop="refusal")))
    assert an["cards"][0]["llm"]["status"] == "ERROR" and "refused" in an["cards"][0]["llm"]["problems"][0]
    assert sk["verdicts"][0]["verdict"] == "KILL"


def test_verify_accepts_cited_numbers_in_their_usual_forms():
    facts = {"F1": {"claim": "diluted EPS 2026Q2", "value": 1.0, "source": "s"}, "F2": {"claim": "gap", "value": 0.2345, "source": "s"},
             "F3": {"claim": "insider purchase, US dollars", "value": 250000.0, "source": "s"}}
    ok = [{"point": "EPS was $1.00 in 2026Q2", "fact_ids": ["F1"]}, {"point": "about 23.45% below base", "fact_ids": ["F2"]},
          {"point": "a $250,000 buy", "fact_ids": ["F3"]}]
    assert llm.verify(ok, facts) == []
    assert llm.verify([{"point": "EPS was 1.40", "fact_ids": ["F1"]}], facts)
    assert llm.verify([{"point": "x", "fact_ids": ["F9"]}], facts)


def test_names_the_code_already_killed_are_not_debated(tmp_path):
    calls = []
    _, _, _, _, an, _ = run_chain(tmp_path, form4(shares=4000), concept={"units": {"USD/shares": []}},
                                  seat=llm.ClaudeSeat(create=fake_create(good_answers(), calls=calls)))
    assert "llm" not in an["cards"][0] and calls == []


# ------------------------------------------------------------------ ccxt


class FakeExchange:
    def __init__(self, sandbox=True):
        self.urls = {"api": "https://api.x", **({"test": "https://testnet.x"} if sandbox else {})}
        self.sandboxed, self.orders = False, []

    def set_sandbox_mode(self, on):
        self.sandboxed = on

    def fetch_ticker(self, s):
        return {"bid": 99.0, "ask": 101.0, "last": 100.0, "datetime": "2026-10-15T00:00:00Z"}

    def fetch_order_book(self, s, limit=None):
        return {"bids": [[99, 1]] * 30, "asks": [[101, 1]] * 30, "datetime": None}

    def fetch_ohlcv(self, s, tf="1d", since=None, limit=None):
        return [[1760486400000, 1, 2, 0.5, 1.5, 10], [1760572800000, 1.5, 2, 1, 1.8, None]]

    def fetch_balance(self):
        return {"total": {"USDT": 1000.0, "BTC": 0.0}}

    def create_order(self, *a):
        self.orders.append(a)
        return {"id": "1", "status": "open"}


def test_ccxt_sandbox_orders_and_live_refusal():
    ex = FakeExchange()
    v = pf.CcxtVenue("binance", client=ex)
    assert ex.sandboxed and v.ticker("BTC/USDT")["ask"] == 101.0 and len(v.book("BTC/USDT", 5)["bids"]) == 5
    b = v.bars("BTC/USDT")
    assert b[0].d == "2025-10-15" and b[1].v == 0.0
    assert v.balance() == {"USDT": 1000.0}
    assert v.place_limit("BTC/USDT", "buy", 0.01, 99.0)["status"] == "open" and ex.orders[0][-1] == {"postOnly": True}
    live = pf.CcxtVenue("binance", sandbox=False, client=FakeExchange())
    with pytest.raises(PermissionError):
        live.place_limit("BTC/USDT", "buy", 0.01, 99.0)
    with pytest.raises(pf.SandboxUnavailable):
        pf.CcxtVenue("kraken", client=FakeExchange(sandbox=False))


def test_ccxt_real_library_kraken_has_no_sandbox():
    pytest.importorskip("ccxt")
    with pytest.raises(pf.SandboxUnavailable):
        pf.CcxtVenue("kraken")
    assert pf.CcxtVenue("binance").ex.urls["api"] == pf.CcxtVenue("binance").ex.urls["test"]  # really switched to the testnet


# ------------------------------------------------------------------ OpenBB


def test_openbb_prices_reads_results_and_treats_empty_as_no_bars():
    Row = lambda d, c: types.SimpleNamespace(date=d, open=c, high=c + 1, low=c - 1, close=c, volume=100)

    class EmptyDataError(Exception):
        pass

    def historical(symbol, start_date, end_date, provider):
        if symbol == "GONE":
            raise EmptyDataError("no results")
        return types.SimpleNamespace(results=[Row("2026-10-02", 11.0), Row("2026-10-01", 10.0)])
    obb = types.SimpleNamespace(equity=types.SimpleNamespace(price=types.SimpleNamespace(historical=historical)))
    p = pf.OpenBBPrices("2026-01-01", "2026-10-15", obb=obb)
    assert [b.d for b in p.bars("ACME")] == ["2026-10-01", "2026-10-02"] and p.bars("GONE") == []


# ------------------------------------------------------------------ freqtrade


def _load_strategy(src):
    """Execute the export against stand-in freqtrade/pandas modules if the real ones are absent."""
    import sys
    pd = pytest.importorskip("pandas")
    if "freqtrade.strategy" not in sys.modules:
        try:
            import freqtrade.strategy  # noqa: F401
        except ImportError:
            m = types.ModuleType("freqtrade.strategy")
            m.IStrategy = type("IStrategy", (), {"__init__": lambda self, config=None: None})
            sys.modules.setdefault("freqtrade", types.ModuleType("freqtrade"))
            sys.modules["freqtrade.strategy"] = m
    ns = {}
    exec(compile(src, "strategy.py", "exec"), ns)
    return pd, ns


def test_freqtrade_trend200_export_runs_on_a_dataframe():
    src = pf.export_freqtrade("trend200", stop_pct=0.1, hold_days=0, name="T200")
    pd, ns = _load_strategy(src)
    s = ns["T200"]({})
    assert s.INTERFACE_VERSION == 3 and s.stoploss == -0.1 and s.startup_candle_count == 200
    df = pd.DataFrame({"date": pd.date_range("2025-01-01", periods=260, freq="D", tz="UTC"),
                       "close": [100.0] * 200 + [120.0] * 30 + [80.0] * 30})
    df = s.populate_exit_trend(s.populate_entry_trend(s.populate_indicators(df, {"pair": "BTC/USDT"}), {}), {})
    assert df["enter_long"].iloc[210] == 1 and df["exit_long"].iloc[250] == 1 and pd.isna(df["enter_long"].iloc[150])


def test_freqtrade_signals_export_enters_on_listed_days_and_exits_by_clock():
    src = pf.export_freqtrade("signals", [("BTC/USDT", "2025-01-03"), ("ETH/USDT", "2025-01-05")], stop_pct=0.08, hold_days=5, name="Sig")
    pd, ns = _load_strategy(src)
    s = ns["Sig"]({})
    df = pd.DataFrame({"date": pd.date_range("2025-01-01", periods=10, freq="D", tz="UTC"), "close": 1.0})
    df = s.populate_entry_trend(s.populate_indicators(df, {"pair": "BTC/USDT"}), {})
    assert list(df.index[df["enter_long"] == 1]) == [2]
    from datetime import timezone
    t = types.SimpleNamespace(open_date_utc=datetime(2025, 1, 4, tzinfo=timezone.utc))
    assert s.custom_exit("BTC/USDT", t, datetime(2025, 1, 8, tzinfo=timezone.utc), 1, 0) is None
    assert s.custom_exit("BTC/USDT", t, datetime(2025, 1, 9, tzinfo=timezone.utc), 1, 0) == "time_exit_5d"
    with pytest.raises(ValueError):
        pf.export_freqtrade("signals", [])


# ------------------------------------------------------------------ SEC downloads


def test_quarter_ranges():
    assert ins.quarter_list(["2025q3-2026q2", "2025q4"]) == ["2025q3", "2025q4", "2026q1", "2026q2"]
    with pytest.raises(ValueError):
        ins.quarter_list(["2026q3-2026q1"])


def test_fetch_datasets_tries_both_paths_skips_existing_and_rejects_non_zips(tmp_path):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr("SUBMISSION.tsv", "ACCESSION_NUMBER\n")
    good, seen = buf.getvalue(), []

    def get(url, timeout=None):
        seen.append(url)
        if "2026q3" in url and "datastandardsinnovation" in url:
            return good
        if "2016q1" in url and "structureddata" in url:
            return good
        if "2016q2" in url:
            return b"<html>blocked</html>"
        raise OSError("404")
    rows = ins.fetch_datasets(["2016q1", "2016q2", "2026q3"], tmp_path, get=get)
    st = {r["quarter"]: r["status"] for r in rows}
    assert st == {"2016q1": "downloaded", "2016q2": "FAILED", "2026q3": "downloaded"}
    assert "datastandardsinnovation" in [u for u in seen if "2026q3" in u][0]  # newest path first for new quarters
    again = ins.fetch_datasets(["2016q1"], tmp_path, get=get)
    assert again[0]["status"] == "already there" and zipfile.is_zipfile(tmp_path / "2016q1_form345.zip")


def test_fetch_prices_writes_csvs_and_counts_the_survivorship_hole(tmp_path):
    class Src:
        def bars(self, s):
            return [] if s == "GONE" else [ins.DayBar("2026-10-01", 1, 2, 0.5, 1.5, 10)]
    rows = ins.fetch_prices(["ACME", "GONE"], Src(), tmp_path)
    assert {r["symbol"]: r["bars"] for r in rows} == {"ACME": 1, "GONE": 0, "SPY": 1}
    assert ins.CsvPrices(tmp_path).bars("ACME")[0].c == 1.5


# ------------------------------------------------------------------ scheduler


def test_next_run_matches_the_systemd_timers():
    at = lambda s: datetime.fromisoformat(s).replace(tzinfo=sc.NY)
    assert sc.next_run(at("2026-10-15T05:00")) == ("morning", at("2026-10-15T05:30"))  # Thursday
    assert sc.next_run(at("2026-10-15T05:30"))[0] == "chain-check"
    assert sc.next_run(at("2026-10-16T16:31")) == ("weekly", at("2026-10-16T17:00"))  # Friday
    assert sc.next_run(at("2026-10-16T17:01")) == ("audit", at("2026-10-18T18:00"))  # Sunday
    assert sc.next_run(at("2026-10-18T18:00")) == ("morning", at("2026-10-19T05:30"))


def test_schedule_loop_runs_the_seat_logs_and_alerts_on_failure(tmp_path):
    now = [datetime(2026, 10, 15, 15, 40, tzinfo=sc.NY)]

    def sleep(s):
        now[0] = now[0] + __import__("datetime").timedelta(seconds=s)
    ran, alerts = [], []

    def run(cmd, **kw):
        ran.append(cmd)
        return types.SimpleNamespace(returncode=0 if cmd[-1] == "exit-due" else 1, stdout="ok", stderr="boom")
    sc.loop(str(tmp_path), "alpaca", llm=True, clock=lambda: now[0], sleep=sleep, run=run,
            alert=lambda lvl, title, **f: alerts.append(title), max_runs=2)
    assert [c[-1] for c in ran] == ["exit-due", "journal"] and "--llm" in ran[0]
    assert alerts == ["desk journal exited 1"]
    log = [json.loads(x) for x in (tmp_path / "schedule.log").read_text().splitlines()]
    assert log[0]["scheduled"].startswith("2026-10-15T15:45")
