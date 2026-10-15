"""Insider signal, the six-seat desk, the weather scanner, the jev-trader dry run and the audit tools.

Fixtures in tests/fixtures are real responses captured 2026-10-14 (see its README)."""
import json
import math
import subprocess
from pathlib import Path

import pytest

from tradebot import audit, desk as dk, insider as ins, jevquote as jq, weather as wx
from tradebot.brokers import LiveHostRefused

FX = Path(__file__).parent / "fixtures"


def fx(name):
    return (FX / name).read_bytes()


def bars_csv(folder: Path, sym: str, rows):
    folder.mkdir(parents=True, exist_ok=True)
    with (folder / f"{sym}.csv").open("w") as fh:
        fh.write("date,open,high,low,close,volume\n")
        for r in rows:
            fh.write(",".join(str(x) for x in r) + "\n")


def flat_bars(start_day: int, n: int, px=50.0, vol=1_000_000, month="2026-09"):
    """n daily bars on consecutive days of a month (weekends ignored: tests count bars, not dates)."""
    return [(f"{month}-{d:02d}", px, px + 0.5, px - 0.5, px, vol) for d in range(start_day, start_day + n)]


# ------------------------------------------------------------------ insider parsing


def test_form4_fixture_chair_in_other_text_and_10b5_1_flag():
    f = ins.parse_form4(fx("form4_pcor_0001628280-26-065924.xml"), "0001628280-26-065924")
    assert f.symbol == "PCOR" and f.issuer_cik == "0001611052"
    assert f.role == "Chair"  # filed as isOther + otherText, not as an officer
    assert f.plan_10b5_1 is True
    assert f.purchase_usd == 0  # option exercise (M) and sales (S): no open-market buy
    assert [t.code for t in f.txns] == ["M", "S", "S"]


def test_role_matching():
    assert ins.role_of("President and Chief Executive Officer") == "CEO"
    assert ins.role_of("EVP, Chief Financial Officer") == "CFO"
    assert ins.role_of("Vice President, Sales") is None
    assert ins.role_of("", "Vice Chairman") is None
    assert ins.role_of("President") == "President"
    assert ins.role_of("", "Chairman of the Board") == "Chair"


def test_feed_dedupes_reporting_and_issuer_entries():
    es = ins.parse_feed(fx("edgar_current_form4.atom"))
    assert [e.accession for e in es] == ["0001628280-26-065924", "0001284309-26-000030"]
    assert ins.form4_xml_name(fx("edgar_index_pcor.json")) == "wk-form4_1792023621.xml"


def test_xml_with_entities_is_refused():
    with pytest.raises(ValueError):
        ins.parse_form4(b'<?xml version="1.0"?><!DOCTYPE x [<!ENTITY a "aaaa">]><ownershipDocument/>')


def form4(symbol="ACME", title="Chief Financial Officer", shares=3000, price=50.0, code="P", plan=0, owner="Doe Jane"):
    return f"""<?xml version="1.0"?><ownershipDocument><issuer><issuerCik>0000000123</issuerCik><issuerName>Acme</issuerName>
<issuerTradingSymbol>{symbol}</issuerTradingSymbol></issuer><reportingOwner><reportingOwnerId><rptOwnerCik>1</rptOwnerCik>
<rptOwnerName>{owner}</rptOwnerName></reportingOwnerId><reportingOwnerRelationship><isOfficer>1</isOfficer>
<officerTitle>{title}</officerTitle></reportingOwnerRelationship></reportingOwner><aff10b5One>{plan}</aff10b5One>
<nonDerivativeTable><nonDerivativeTransaction><transactionDate><value>2026-10-13</value></transactionDate>
<transactionCoding><transactionCode>{code}</transactionCode></transactionCoding><transactionAmounts>
<transactionShares><value>{shares}</value></transactionShares><transactionPricePerShare><value>{price}</value></transactionPricePerShare>
<transactionAcquiredDisposedCode><value>A</value></transactionAcquiredDisposedCode></transactionAmounts></nonDerivativeTransaction>
</nonDerivativeTable></ownershipDocument>"""


def test_signals_threshold_role_and_batches():
    a = ins.parse_form4(form4(shares=3000), "a1", filed="2026-10-14")  # $150K CFO
    small = ins.parse_form4(form4(shares=1000), "a2", filed="2026-10-14")  # $50K
    vp = ins.parse_form4(form4(title="Vice President"), "a3", filed="2026-10-14")
    sale = ins.parse_form4(form4(code="S"), "a4", filed="2026-10-14")
    sigs = ins.signals_from([a, small, vp, sale])
    assert [(s.symbol, s.role, s.usd) for s in sigs] == [("ACME", "CFO", 150000.0)]
    batch = [ins.parse_form4(form4(symbol="BAT", title=t, owner=o), o, filed="2026-10-14")
             for t, o in (("CEO", "A"), ("CFO", "B"), ("COO", "C"))]
    out = ins.signals_from(batch)
    assert len(out) == 3 and all(s.batch for s in out)


def test_fetch_recent_names_failing_sources():
    # the first entry is the Reporting one: EDGAR serves the same accession under each filer's CIK folder
    base = "https://www.sec.gov/Archives/edgar/data/1805735/000162828026065924/"
    pages = {ins.FEED.format(start=0): fx("edgar_current_form4.atom"), base + "index.json": fx("edgar_index_pcor.json"),
             base + "wk-form4_1792023621.xml": fx("form4_pcor_0001628280-26-065924.xml")}

    def get(url):
        if url not in pages:
            raise OSError("404")
        return pages[url]
    filings, errors = ins.fetch_recent(get, pages=1)
    assert [f.symbol for f in filings] == ["PCOR"] and filings[0].filed == "2026-10-14"
    assert len(errors) == 1 and "0001284309-26-000030" in errors[0]


def test_form345_loader_checks_columns_and_skips_amendments(tmp_path):
    (tmp_path / "SUBMISSION.tsv").write_text("ACCESSION_NUMBER\tFILING_DATE\tDOCUMENT_TYPE\tISSUERTRADINGSYMBOL\n"
                                             "x1\t14-OCT-2026\t4\tACME\nx2\t14-OCT-2026\t4/A\tACME\n")
    (tmp_path / "REPORTINGOWNER.tsv").write_text("ACCESSION_NUMBER\tRPTOWNERNAME\tRPTOWNER_RELATIONSHIP\tRPTOWNER_TITLE\n"
                                                 "x1\tDoe Jane\tOfficer\tChief Executive Officer\nx2\tDoe Jane\tOfficer\tChief Executive Officer\n")
    (tmp_path / "NONDERIV_TRANS.tsv").write_text("ACCESSION_NUMBER\tTRANS_CODE\tTRANS_SHARES\tTRANS_PRICEPERSHARE\tTRANS_ACQUIRED_DISP_CD\n"
                                                 "x1\tP\t2000\t60\tA\nx2\tP\t2000\t60\tA\n")
    sigs, stats = ins.load_form345(tmp_path)
    assert [(s.symbol, s.filed, s.role, s.usd) for s in sigs] == [("ACME", "2026-10-14", "CEO", 120000.0)]
    (tmp_path / "NONDERIV_TRANS.tsv").write_text("ACCESSION_NUMBER\tTRANS_CODE\n x1\tP\n")
    with pytest.raises(ValueError, match="missing columns"):
        ins.load_form345(tmp_path)


# ------------------------------------------------------------------ the simulated trade


def mk(rows):
    return [ins.DayBar(*r) for r in rows]


def test_entry_is_the_first_open_after_the_filing_day():
    rows = flat_bars(1, 20) + [("2026-09-21", 51, 52, 50.5, 51.5, 1e6)] + flat_bars(22, 6, px=52)
    s = ins.Signal("ACME", "2026-09-20", "Doe", "CFO", 2e5, "a")  # filed on the 20th (maybe after the close)
    r = ins.simulate(s, mk(rows), min_adv=0)
    assert r.entry_date == "2026-09-21" and r.entry == 51  # never the filing day's own bar
    assert r.reason == "time exit (day 5)" and r.exit_date == "2026-09-25"
    assert r.ret == pytest.approx(52 / 51 - 1 - 0.002)


def test_gap_through_the_stop_fills_at_the_open():
    rows = flat_bars(1, 20) + [("2026-09-21", 50, 50.5, 49.5, 50, 1e6), ("2026-09-22", 40, 41, 39, 40, 1e6)] + flat_bars(23, 5)
    r = ins.simulate(ins.Signal("ACME", "2026-09-20", "Doe", "CFO", 2e5, "a"), mk(rows), min_adv=0)
    assert r.reason == "stop (gapped through)" and r.exit == 40  # not the stop price


def test_unpriced_and_filtered_are_counted_not_dropped():
    s = ins.Signal("ACME", "2026-09-28", "Doe", "CFO", 2e5, "a")
    assert ins.simulate(s, mk(flat_bars(1, 20)), min_adv=0).startswith("unpriced")
    thin = mk(flat_bars(1, 30, vol=10))
    assert ins.simulate(ins.Signal("ACME", "2026-09-20", "D", "CFO", 2e5, "a"), thin).startswith("filtered")


def test_holdout_split_and_default_verdict():
    sigs = [ins.Signal("A", d, "x", "CEO", 2e5, d) for d in ("2024-01-02", "2025-06-01", "2026-09-01")]
    build, hold, cut = ins.split_holdout(sigs, 12, today="2026-10-14")
    assert [s.filed for s in hold] == ["2026-09-01"] and cut.startswith("2025-10")
    assert ins.verdict({"trades": 5, "avg_r": 1.0, "excess_vs_bench": 0.1}, None).startswith("NO EDGE")


# ------------------------------------------------------------------ the desk


class FakeBroker:
    paper = True

    def __init__(self, equity=100_000.0, last_equity=100_000.0, positions=(), orders=()):
        self.acct = {"equity": str(equity), "last_equity": str(last_equity)}
        self.pos, self.oo, self.placed, self.cancelled, self.sold, self.all_cancelled = list(positions), list(orders), [], [], [], False
        self.orders = {}

    def account(self):
        return self.acct

    def positions(self):
        return self.pos

    def open_orders(self):
        return self.oo

    def order(self, oid):
        return self.orders[oid]

    def place_oto(self, cid, sym, qty, limit, stop):
        self.placed.append((cid, sym, qty, limit, stop))
        return {"id": f"o{len(self.placed)}"}

    def cancel(self, oid):
        self.cancelled.append(oid)

    def cancel_all(self):
        self.all_cancelled = True

    def sell_market(self, cid, sym, qty):
        self.sold.append((sym, qty))
        return {"id": "s1"}


def test_paper_broker_refuses_live_and_sends_oto():
    with pytest.raises(LiveHostRefused):
        dk.PaperDeskBroker(base="https://api.alpaca.markets")
    sent = []
    b = dk.PaperDeskBroker(transport=lambda m, u, h, body: (sent.append((m, u, body)) or (200, {"id": "x"})), key_id="k", secret="s")
    b.place_oto("cid", "ACME", 10, 50.255, 48.0)
    m, u, body = sent[0]
    assert u == "https://paper-api.alpaca.markets/v2/orders" and body["order_class"] == "oto" and body["time_in_force"] == "day"
    assert body["stop_loss"] == {"stop_price": "48.00"} and body["limit_price"] == "50.26" and "take_profit" not in body


def test_ticket_size_formula_and_cap():
    bars = mk(flat_bars(1, 20, px=50.0))  # true range 1.0 -> ATR 1.0
    t = dk.make_ticket(1, "ACME", bars, 100_000, dict(dk.DEFAULT_LIMITS), "2026-10-15", {})
    assert t["limit"] == 50.25 and t["stop"] == 48.25
    assert t["shares"] == min(math.floor(1000 / 2.0), math.floor(10_000 / 50.25)) == 199
    assert t["risk_usd"] <= 1000


def test_risk_check_failures(tmp_path):
    d = dk.Desk(tmp_path)
    d.init()
    t = {"symbol": "ACME", "shares": 100, "limit": 50.0, "stop": 48.0, "risk_usd": 200.0, "value_usd": 5000.0}
    ok = dk.risk_check(t, {"equity": "100000", "last_equity": "100000"}, [], [], d.limits(), d, True)
    assert ok == []
    assert any("daily loss" in x for x in dk.risk_check(t, {"equity": "96000", "last_equity": "100000"}, [], [], d.limits(), d, True))
    assert any("already held" in x for x in dk.risk_check(t, {"equity": "1e5", "last_equity": "1e5"}, [{"symbol": "ACME"}], [], d.limits(), d, True))
    full = [{"symbol": s} for s in "ABCDE"]
    assert any("positions or entries" in x for x in dk.risk_check(t, {"equity": "1e5", "last_equity": "1e5"}, full, [], d.limits(), d, True))
    assert any("paper_only" in x for x in dk.risk_check(t, {"equity": "1e5", "last_equity": "1e5"}, [], [], d.limits(), d, False))
    (tmp_path / "KILL").write_text("x")
    assert any("KILL" in x for x in dk.risk_check(t, {"equity": "1e5", "last_equity": "1e5"}, [], [], d.limits(), d, True))


def positive_concept(eps_q=1.0, years=(2023, 2024, 2025, 2026)):
    facts, accn = [], 0
    for y in years:
        for q in (1, 2, 3):
            if y == 2026 and q > 2:
                continue
            accn += 1
            end = {1: "03-31", 2: "06-30", 3: "09-30"}[q]
            facts.append({"start": f"{y}-01-01", "end": f"{y}-{end}", "val": eps_q, "accn": f"a{accn}", "fy": y, "fp": f"Q{q}", "form": "10-Q",
                          "filed": f"{y}-{int(end[:2]) + 1:02d}-05", "frame": f"CY{y}Q{q}"})
        if y < 2026:
            facts.append({"start": f"{y}-01-01", "end": f"{y}-12-31", "val": eps_q * 4, "accn": f"k{y}", "fy": y, "fp": "FY", "form": "10-K",
                          "filed": f"{y + 1}-02-20", "frame": f"CY{y}"})
    return {"units": {"USD/shares": facts}}


def run_chain(tmp_path, xml, price=50.0, moved=0.0, concept=None):
    d = dk.Desk(tmp_path / "desk")
    d.init()
    folder = "https://www.sec.gov/Archives/edgar/data/1/000000000126000001/"
    atom = f"""<?xml version="1.0"?><feed xmlns="http://www.w3.org/2005/Atom"><entry><link href="{folder}x-index.htm"/>
<updated>2026-10-14T19:00:00-04:00</updated><id>urn:tag:sec.gov,2008:accession-number=0000000001-26-000001</id></entry></feed>"""
    concept = concept or positive_concept()
    pages = {ins.FEED.format(start=0): atom.encode(), folder + "index.json": json.dumps({"directory": {"item": [{"name": "f4.xml"}]}}).encode(),
             folder + "f4.xml": xml.encode(), dk.CONCEPT.format(cik="123"): json.dumps(concept).encode()}
    get = lambda u: pages[u] if u in pages else (_ for _ in ()).throw(OSError("404 " + u))
    rows = [(f"{y}-{m:02d}-15", price, price + 0.5, price - 0.5, price, 2_000_000) for y in (2023, 2024, 2025) for m in range(1, 13)]
    rows += [(f"2026-{m:02d}-15", price, price + 0.5, price - 0.5, price, 2_000_000) for m in range(1, 10)]
    rows += flat_bars(1, 15, px=price, vol=2_000_000, month="2026-10")  # through the scout day, the 15th
    rows[-1] = (rows[-1][0], price, price + 0.5, price - 0.5, price * (1 + moved), 2_000_000)
    bars_csv(tmp_path / "px", "ACME", rows)
    prices = ins.CsvPrices(tmp_path / "px")
    day = "2026-10-15"
    sc = dk.scout(d, day, get, prices, pages=1)
    an = dk.analyst(d, day, get, prices)
    sk = dk.skeptic(d, day, prices)
    return d, day, prices, sc, an, sk


def test_morning_chain_pass_then_ticket_then_approve(tmp_path):
    d, day, prices, sc, an, sk = run_chain(tmp_path, form4(shares=4000, price=50.0), price=50.0)
    assert [n["symbol"] for n in sc["names"]] == ["ACME"]
    card = an["cards"][0]
    assert card["verdict"] == "RANGE" and len(card["facts"]) == 5 and all(f["source"] for f in card["facts"])
    assert any("derived" in f["source"] for f in card["facts"])  # Q4 = FY - Q1..Q3
    assert sk["verdicts"][0]["verdict"] == "PASS", sk
    b = FakeBroker()
    out = dk.risk(d, day, b, prices)
    assert len(out["tickets"]) == 1 and out["brief"][2].startswith("Wants 1 ticket")
    res = dk.approve(d, day, [1], b)
    assert res[0]["status"] == "SENT" and b.placed[0][1] == "ACME"
    assert dk.chain_check(d, day) is None


def test_skeptic_kills_plans_expensive_and_moved(tmp_path):
    _, _, _, _, _, sk = run_chain(tmp_path / "a", form4(shares=4000, plan=1))
    assert sk["verdicts"][0]["verdict"] == "KILL" and "10b5-1" in sk["verdicts"][0]["why"]
    falling = positive_concept(eps_q=1.0)
    for f in falling["units"]["USD/shares"]:
        if f["frame"] in ("CY2026Q1", "CY2026Q2"):
            f["val"] = 0.25  # earnings fell: the stock's own P/E history now says it's worth less than the price
    _, _, _, _, an, sk = run_chain(tmp_path / "b", form4(shares=4000), concept=falling)
    assert an["cards"][0]["verdict"] == "EXPENSIVE" and an["cards"][0]["base"] < 50
    assert sk["verdicts"][0]["verdict"] == "KILL" and "EXPENSIVE" in sk["verdicts"][0]["why"]
    rising = positive_concept(eps_q=1.0)
    for f in rising["units"]["USD/shares"]:
        if f["frame"] in ("CY2026Q1", "CY2026Q2"):
            f["val"] = 2.0  # cheap on its own history, so only the post-filing jump can kill it
    _, _, _, _, _, sk = run_chain(tmp_path / "c", form4(shares=4000), moved=0.08, concept=rising)
    assert sk["verdicts"][0]["verdict"] == "KILL" and "already moved" in sk["verdicts"][0]["why"]
    _, _, _, _, an, sk = run_chain(tmp_path / "d", form4(shares=4000), concept=json.loads(fx("sec_concept_pcor_eps.json")))
    assert an["cards"][0]["verdict"] == "NO RANGE, NO TRADE" and sk["verdicts"][0]["verdict"] == "KILL"


def test_no_trade_today_and_pause_blocks_tickets(tmp_path):
    d, day, prices, sc, _, _ = run_chain(tmp_path, form4(shares=100))  # $5K: below the bar
    assert sc["names"] == [] and "NO TRADE TODAY" in d.inbox(day, "scout", "md").read_text()
    dk.pause(d, True)
    out = dk.risk(d, day, FakeBroker(), prices)
    assert out["tickets"] == [] and "PAUSE" in out["brief"][2]
    dk.pause(d, False)
    assert d.halted() is None


def test_exit_due_cancels_stop_and_sells_on_day_5(tmp_path):
    d = dk.Desk(tmp_path)
    d.init()
    bars_csv(tmp_path / "px", "ACME", flat_bars(1, 10, month="2026-10"))
    (tmp_path / "journal" / "open.json").write_text(json.dumps([{"date": "2026-10-01", "symbol": "ACME", "shares": 10, "limit": 50, "stop": 48,
                                                                   "client_id": "c1", "order_id": "o1", "status": "SENT"}]))
    b = FakeBroker(positions=[{"symbol": "ACME", "qty": "10"}],
                   orders=[{"id": "o1", "symbol": "ACME", "side": "buy", "legs": [{"id": "leg-stop", "symbol": "ACME", "side": "sell", "type": "stop"}]}])
    b.orders["o1"] = {"status": "filled", "filled_qty": "10", "filled_avg_price": "50.1", "filled_at": "2026-10-02T13:30:00Z"}
    assert dk.exit_due(d, "2026-10-05", b, ins.CsvPrices(tmp_path / "px")) == []  # day 4: hold
    done = dk.exit_due(d, "2026-10-06", b, ins.CsvPrices(tmp_path / "px"))
    assert b.cancelled == ["leg-stop"] and b.sold == [("ACME", 10.0)] and "day 5" in done[0]["action"]


def test_kill_cancels_and_lists(tmp_path):
    d = dk.Desk(tmp_path)
    b = FakeBroker(positions=[{"symbol": "ACME", "qty": "3", "unrealized_pl": "-1"}])
    out = dk.kill(d, b)
    assert (tmp_path / "KILL").exists() and b.all_cancelled and out["positions"][0]["symbol"] == "ACME"


def test_journal_logs_kills_with_what_they_would_have_made_and_weekly_flags_decay(tmp_path):
    d, day, prices, *_ = run_chain(tmp_path, form4(shares=4000, plan=1))
    rows = dk.journal(d, day, prices)
    assert rows[0]["outcome"] == "KILLED BY SKEPTIC" and rows[0]["hypothetical"]
    p = d.path("journal", "trades.csv")
    lines = ["date,ticker,outcome,entry,exit,pnl_usd,pnl_r,hypothetical,why"] + [f"2026-10-0{1 + i % 5},T{i},FILLED,1,1,0,-0.2,False,x" for i in range(20)]
    p.write_text("\n".join(lines) + "\n")
    d.path("quant", "latest.json").write_text(json.dumps({"build": {"avg_r": 0.4}}))
    w = dk.weekly(d, "2026-10-16")
    assert w["status"] == "DECAYING" and "DECAYING" in d.path("lessons.md").read_text()


def test_holdout_runs_once(tmp_path):
    d = dk.Desk(tmp_path)
    d.init()
    dk.holdout_lock(d, {"a": 1}, "data")
    with pytest.raises(SystemExit, match="once"):
        dk.holdout_lock(d, {"a": 1}, "data")


def test_audit_flags_a_deleted_test(tmp_path):
    repo = tmp_path / "r"
    (repo / "tests").mkdir(parents=True)
    git = lambda *a: subprocess.run(["git", *a], cwd=repo, check=True, capture_output=True)
    git("init", "-q")
    git("config", "user.email", "q@x")
    git("config", "user.name", "q")
    (repo / "tests" / "test_x.py").write_text("def test_a():\n    assert True\n\ndef test_b():\n    assert True\n")
    git("add", ".")
    git("commit", "-qm", "tests")
    (repo / "tests" / "test_x.py").write_text("def test_b():\n    assert True\n")
    git("commit", "-qam", "make it green")
    d = dk.Desk(repo / "desk")
    d.init()
    rep = {r["check"]: r for r in dk.audit_quant(repo, d)}
    assert rep["tests run"]["rating"] == "PASS"
    assert rep["test history"]["rating"] == "FAIL" and "test_a" in rep["test history"]["why"]


# ------------------------------------------------------------------ weather


def test_polymarket_event_fixture():
    ev = wx.parse_event(json.loads(fx("polymarket_nyc_high_2026-10-16.json")))
    assert ev.station == "KLGA" and ev.unit == "F" and len(ev.buckets) == 11
    assert (ev.buckets[0].lo, ev.buckets[0].hi) == (-math.inf, 63) and (ev.buckets[-1].lo, ev.buckets[-1].hi) == (82, math.inf)
    assert (ev.buckets[5].lo, ev.buckets[5].hi) == (72, 73) and ev.buckets[5].fee_rate == 0.05
    assert wx.target_date(ev.title, ev.end) == "2026-10-16"
    for mu in (60, 72.4, 90):
        assert sum(wx.bucket_prob(b, mu, 3.0) for b in ev.buckets) == pytest.approx(1.0)


def nws_get():
    pages = {f"{wx.NWS}/stations/KLGA/observations/latest": json.loads(fx("nws_klga_latest.json")),
             f"{wx.NWS}/points/40.7800,-73.8800": json.loads(fx("nws_points_lga.json")),
             "https://api.weather.gov/gridpoints/OKX/37,46/forecast/hourly": json.loads(fx("nws_okx_hourly_2026-10-14.json")),
             wx.GAMMA + "highest-temperature-in-nyc-on-october-16-2026": json.loads(fx("polymarket_nyc_high_2026-10-16.json"))}
    return lambda u: pages[u]


def test_forecast_max_uses_the_station_local_day():
    hourly = json.loads(fx("nws_okx_hourly_2026-10-14.json"))["properties"]["periods"]
    expect = max(p["temperature"] for p in hourly if p["startTime"].startswith("2026-10-16"))
    fc = wx.forecast_max("KLGA", "2026-10-16", nws_get())
    assert fc["max_f"] == expect and fc["hours"] == 24 and fc["lead_h"] > 24


def test_scan_end_to_end_is_read_only_and_fee_aware():
    out = wx.scan("highest-temperature-in-nyc-on-october-16-2026", nws_get(), bankroll=1000)
    assert out["station"] == "KLGA" and out["error_model"]["source"].startswith("PRIOR")
    assert out["prob_sum"] == pytest.approx(1.0, abs=1e-6)
    for r in out["buckets"]:
        if r["edge"] is not None:
            assert r["edge"] == pytest.approx(r["model_p"] - r["ask"] - 0.05 * r["ask"] * (1 - r["ask"]), abs=1e-3)
        assert r["stake_usd"] <= 50  # 5% cap
    assert "read-only" in out["note"]


def test_error_model_learns_station_bias(tmp_path):
    m = wx.ErrorModel(tmp_path / "w.jsonl")
    for i in range(30):
        m.record({"station": "KLGA", "day": f"d{i}", "lead_h": 20, "forecast_f": 70 + (i % 3), "actual_f": 66 + (i % 3)})
    p = wx.ErrorModel(tmp_path / "w.jsonl").params("KLGA", 20)
    assert p["source"] == "learned" and p["bias"] == pytest.approx(4.0) and p["sigma"] >= 1.0
    assert wx.kelly_yes(0.6, 0.5) == pytest.approx(0.2) and wx.kelly_yes(0.4, 0.5) == 0.0


# ------------------------------------------------------------------ jev-trader dry run


def book(ts, bid, ask, bq=1.0, aq=1.0):
    return jq.Book(ts, [(bid, bq), (bid - 1, 5)], [(ask, aq), (ask + 1, 5)])


def test_quote_never_crosses_and_fills_only_when_the_market_comes_to_us(tmp_path):
    always_buy = lambda b: ("buy", 0.9, 0.1)
    books = [book(0, 100, 102), book(1, 100, 102), book(2, 100, 101), book(3, 100, 101)]
    out = jq.run(books, always_buy, tick=1.0, size=1, max_inventory=5, log_path=tmp_path / "log.jsonl")
    log = [json.loads(x) for x in (tmp_path / "log.jsonl").read_text().splitlines()]
    assert log[0]["quote"] == 101 and log[2]["quote"] == 100  # one tick inside; at a 1-tick spread, join the bid
    assert out["fills"] == 1  # only step 2: ask fell to our 101 bid
    assert "dry run" in out["mode"]


def test_adverse_selection_verdict():
    books = [book(i, 100 - i, 101 - i) for i in range(40)]  # price walks down; buying the dip keeps losing
    out = jq.run(books, lambda b: ("buy", 0.6, 0.1), tick=0.5, size=1, max_inventory=100)
    assert out["fills"] > 0 and out["markout_10_bps"] < 0 and out["verdict"].startswith("does not earn")


def test_inventory_cap_flips_to_the_reducing_side():
    books = [book(i, 100 - i, 100.5 - i) for i in range(10)]
    out = jq.run(books, lambda b: ("buy", 0.9, 0.1), tick=0.25, size=1, max_inventory=2)
    assert out["inventory"] <= 2


# ------------------------------------------------------------------ audit


def monthly(rets, start_year=2010):
    return [(f"{start_year + i // 12}-{i % 12 + 1:02d}-28", r) for i, r in enumerate(rets)]


def test_headline_check_every_start_and_years():
    s = monthly([0.02] * 12 + [-0.03] * 12 + [0.01] * 12)
    h = audit.headline_check(s, annual_cost_pct=1.0)
    assert h["losing_years"] == ["2011"] and h["periods_per_year"] == 12
    assert h["cagr_after_costs"] < h["cagr_gross"] and h["every_start_date"]["starts"] == 24
    assert h["max_drawdown"] > 0.25


def test_finding_needs_both_metrics_in_both_windows():
    bench = monthly([0.005 + (0.01 if i % 2 else -0.01) for i in range(240)], 2005)
    good = [(d, r + 0.003) for d, r in bench]
    assert audit.finding(good, bench, "2015-01-01")["status"] == "DATA VALIDATED"
    fade = [(d, r + (0.003 if d < "2015" else -0.004)) for d, r in bench]
    assert audit.finding(fade, bench, "2015-01-01")["status"] == "NO EDGE"


def test_plateau_vs_lone_peak():
    flat = {(a, b): 1.0 for a in (10, 20, 30) for b in (1, 2, 3)}
    assert audit.plateau(flat, (20, 2))["verdict"] == "plateau"
    peak = {k: -0.5 for k in flat}
    peak[(20, 2)] = 2.0
    assert audit.plateau(peak, (20, 2))["verdict"] == "lone peak"


def test_trend200_steps_aside_in_a_crash():
    closes = [(f"d{i:04d}", 100 * 1.001 ** i) for i in range(260)] + [(f"d{260 + i:04d}", 100 * 1.001 ** 260 * 0.97 ** (i + 1)) for i in range(30)]
    rets = audit.trend200(closes)
    assert any(r != 0 for _, r in rets[200:250])  # in the market in the uptrend
    assert all(r == 0 for _, r in rets[-15:])  # out after the fall


def test_secrets_scan_and_martingale(tmp_path):
    (tmp_path / "bot.py").write_text("KEY='AKIAABCDEFGHIJKLMNOP'\nprint('hi')\n")
    hits = audit.scan_secrets(tmp_path)
    assert {h["kind"] for h in hits} >= {"AWS key"} and all(h["line"] == 1 and h["redacted"] for h in hits)
    assert audit.martingale([(1, -1), (2, -1), (4, -1), (8, 5)])["flag"]
    assert audit.spec({"entry": "50/200 cross"})["exit"] == "UNKNOWN"


# ------------------------------------------------------------------ bundle check (read-only)

from tradebot import bundlecheck as bc, graph as gr
from tradebot.onchain import SolanaRPC

M, DEV, F = "Mint1111", "Dev1111", "Funder1111"


def fake_chain(n_bundle=5, holds=40_000.0, many_pages=False):
    W = [f"W{i}" for i in range(n_bundle)]
    txs, mint_sigs = {}, []

    def tx(payer, slot, buyer=None, amount=0.0, fund=None):
        keys = [payer] + ([buyer] if buyer else []) + ([fund] if fund else [])
        pre, post = [10**10] + [0] * (len(keys) - 1), [10**10 - 10**9] + [10**9] * (len(keys) - 1)
        ptb = [{"accountIndex": 1, "mint": M, "owner": buyer, "uiTokenAmount": {"uiAmountString": str(amount)}}] if buyer and amount else []
        return {"slot": slot, "meta": {"preBalances": pre, "postBalances": post, "preTokenBalances": [], "postTokenBalances": ptb},
                "transaction": {"message": {"accountKeys": keys}}}
    txs["create"] = tx(DEV, 100)
    mint_sigs.append({"signature": "create", "slot": 100, "err": None})
    for i, w in enumerate(W):
        txs[f"buy{i}"] = tx(w, 100, w, 50_000.0)
        mint_sigs.append({"signature": f"buy{i}", "slot": 100, "err": None})
        funder = DEV if i == n_bundle - 1 else F
        txs[f"fund{i}"] = tx(funder, 90, fund=w)
    txs["late"] = tx("Late", 150, "Late", 1000.0)
    mint_sigs.append({"signature": "late", "slot": 150, "err": None})
    newest_first = list(reversed(mint_sigs))
    largest = [{"address": f"ta-{w}", "uiAmountString": str(holds / max(1, n_bundle - 1))} for w in W[:-1]] + [{"address": "ta-dev", "uiAmountString": "50000"}]
    owners = {f"ta-{w}": w for w in W} | {"ta-dev": DEV}

    def post(url, body):
        m, p = body["method"], body["params"]
        if m == "getSignaturesForAddress":
            if p[0] == M:
                if many_pages:
                    return {"result": [{"signature": f"x{i}", "slot": 999, "err": None} for i in range(1000)]}
                return {"result": newest_first}
            i = W.index(p[0]) if p[0] in W else None
            return {"result": [{"signature": f"fund{i}", "slot": 90, "err": None}] if i is not None else []}
        if m == "getTransaction":
            return {"result": txs.get(p[0])}
        if m == "getTokenSupply":
            return {"result": {"value": {"uiAmountString": "1000000"}}}
        if m == "getTokenLargestAccounts":
            return {"result": {"value": largest}}
        if m == "getAccountInfo":
            return {"result": {"value": {"data": {"parsed": {"info": {"owner": owners[p[0]]}}}}}}
        raise AssertionError(m)
    return SolanaRPC("http://fake", post)


def test_bundle_check_finds_shared_funder_and_dev_funded_wallets():
    rep = bc.check(M, fake_chain())
    assert rep.deployer == DEV and rep.creation_slot == 100 and len(rep.first_block_buyers) == 5
    assert rep.verdict == "CAUTION"  # a bundle that has mostly sold already
    shared = [c for c in rep.clusters if F in c["funders"]]
    assert shared and len(shared[0]["wallets"]) == 4 and shared[0]["holds_now_share"] == pytest.approx(0.04)
    assert any(c["funded_by_deployer"] for c in rep.clusters)
    assert any("took 25% of supply" in r for r in rep.reasons)
    still = bc.check(M, fake_chain(holds=300_000.0))  # the same cluster still holds 30%
    assert still.verdict == "DANGER" and any("still holds 30%" in r for r in still.reasons)


def test_bundle_check_fails_closed_without_the_launch_block():
    rep = bc.check(M, fake_chain(many_pages=True), max_pages=2)
    assert rep.verdict == "UNVERIFIED" and not rep.reached_start


def test_callout_check_spots_pre_positioning():
    prices = [(0, 1.0), (3600, 2.0), (7200, 2.1), (90000, 0.9)]
    out = bc.callout_check(prices, [(3600, "kol")])[0]
    assert out["run_up_before"] == pytest.approx(1.0) and out["after_24h"] < 0 and out["worst_24h"] == pytest.approx(-0.55)


# ------------------------------------------------------------------ decision graph and memory


def test_graph_every_node_has_a_route_and_target(tmp_path):
    d = dk.Desk(tmp_path)
    d.init()
    assert {n.route for n in gr.GRAPH} <= {"code", "ml", "llm", "human"} and any(n.route == "human" for n in gr.GRAPH)
    rows = ["date,ticker,outcome,entry,exit,pnl_r,pnl_usd,hypothetical,why"]
    rows += [f"2026-10-0{1 + i % 5},P{i},FILLED,1,1,0.5,1,False,x" for i in range(12)]
    rows += [f"2026-10-0{1 + i % 5},K{i},KILLED BY SKEPTIC,1,1,-0.8,,True,x" for i in range(12)]
    d.path("journal", "trades.csv").write_text("\n".join(rows) + "\n")
    ev = {e["name"]: e for e in gr.evaluate(d)}
    assert ev["skeptic"]["status"] == "MEETS TARGET" and ev["skeptic"]["value"] == pytest.approx(1.3)
    assert ev["pm"]["status"].startswith("NOT ENOUGH DATA")


def test_memory_kills_a_pattern_that_keeps_losing(tmp_path):
    d = dk.Desk(tmp_path / "desk")
    d.init()
    d.path("memory").mkdir(parents=True)
    pat = "CFO buy, at or above base"
    d.path("memory", "theses.jsonl").write_text("".join(json.dumps({"date": f"2026-09-0{i}", "symbol": f"X{i}", "pattern": pat, "decision": "PASS",
                                                                     "why": "every check passed", "outcome_r": -0.6}) + "\n" for i in range(1, 4)))
    d2, day, prices, sc, an, sk = run_chain(tmp_path, form4(shares=4000))
    assert an["cards"][0]["memory"]["same_pattern_n"] == 3
    assert sk["verdicts"][0]["verdict"] == "KILL" and sk["verdicts"][0]["why"].startswith("memory:")
    assert any(t["symbol"] == "ACME" and t["date"] == day for t in gr.recall_all(d2))  # today's thesis is remembered


def test_cli_parses_and_runs_the_new_commands(tmp_path, capsys):
    from tradebot.cli import main
    assert main(["desk", "--dir", str(tmp_path / "d"), "init"]) == 0
    assert main(["desk", "--dir", str(tmp_path / "d"), "graph"]) == 0
    assert main(["desk", "--dir", str(tmp_path / "d"), "pause"]) == 0
    assert main(["audit", "spec"]) == 0
    assert "NOT ENOUGH DATA" in capsys.readouterr().out
