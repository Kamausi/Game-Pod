"""Crypto playbook (regime, levels, FOMO, 1% sizing, swing, DCA, tokenomics, tax lots) on real Yahoo
history, and the whale-copying safeguards."""
import json
import math

import pytest

from tradebot import insider as ins, playbook as pb, whales as wh
from test_universe_whales import FX, Chain, Trader, trade

D = lambda d, o, h, l, c: ins.DayBar(d, o, h, l, c, 1e6)


def real(sym):
    return ins.CsvPrices(FX).bars(f"yahoo_{sym}_USD_daily")[:-1]  # drop the day in progress


def test_regime_on_real_btc_and_no_look_ahead():
    btc = real("BTC")
    r = pb.regime([b.c for b in btc])
    assert r["regime"] == "UP" and r["sma50"] > r["sma200"] and r["alts_allowed"]
    series = pb.regime_series(btc)
    for k in (300, 900, 1500):
        assert series[btc[k].d] == pb.regime([b.c for b in btc[:k + 1]])["regime"]


def test_levels_fomo_and_size():
    bars, px = [], 11.0
    for i in range(120):  # a range market: bounces between ~10 and ~12
        px = 10.2 + 1.6 * abs(math.sin(i / 5))
        bars.append(D(f"2026-{1 + i // 28:02d}-{1 + i % 28:02d}", px, px * 1.01, px * 0.99, px))
    lv = pb.levels(bars)
    assert any(abs(l["price"] - 10.1) < 0.3 for l in lv) and any(abs(l["price"] - 11.9) < 0.3 for l in lv)
    pump = bars[:-1] + [D("2026-12-01", bars[-2].c, bars[-2].c * 1.3, bars[-2].c, bars[-2].c * 1.25)]
    assert pb.fomo(pump)["blocked"] and "pullback" in pb.fomo(pump)["why"]
    s = pb.size(5000, 100, 95)
    assert s["risk_usd"] == pytest.approx(50) and pb.size(5000, 100, 95, risk_pct=10)["risk_pct"] == 2.0


def uptrend_pullback():
    bars, px = [], 50.0
    for i in range(200):  # steady climb with dips
        px *= 1.004
        dip = 0.94 if i % 25 in (20, 21, 22) else 1.0
        c = px * dip
        bars.append(D(f"d{i:03d}", c * 0.998, c * 1.01, c * 0.99, c))
    return bars


def test_swing_setup_plans_the_exit_first_and_respects_the_regime():
    bars = real("SOL")
    assert pb.swing_setup(bars, "DOWN")["why"].startswith("market regime DOWN")
    for k in range(400, len(bars)):
        st = pb.swing_setup(bars[:k], "UP")
        if st["setup"]:
            assert st["stop"] < st["entry_ref"] < st["target"] and st["rr"] >= 2
            break
    else:
        pytest.fail("no setup found in five years")


def test_real_backtests_are_recorded_honestly():
    btc, sol = real("BTC"), real("SOL")
    gated = pb.backtest(sol, pb.regime_series(btc), alt=True)
    ungated = pb.backtest(sol, alt=False)
    assert gated["trades"] == 10 and gated["win_rate"] == pytest.approx(0.1) and gated["avg_r"] < 0  # the textbook swing rule lost on SOL
    assert gated["total_at_1pct_risk"] > ungated["total_at_1pct_risk"]  # ...and the BTC regime filter cut the loss
    head = pb.backtest(sol[:1200], pb.regime_series(btc), alt=True)
    full_early = [t for t in gated["trades_list"] if t["exit_date"] <= sol[1199].d]
    assert head["trades_list"][:len(full_early)] == full_early  # later data never changes earlier trades


def test_dca_on_real_history():
    btc = real("BTC")
    r = pb.dca(btc, 100, 7)
    assert r["invested"] == 100 * math.ceil(len(btc) / 7) and r["return"] > 0 and r["worst_drawdown_vs_invested"] < -0.3


def test_tokenomics_from_the_verified_coingecko_response():
    arb = {"id": "arbitrum", "symbol": "arb", "market_data": {"circulating_supply": 6785574605.0, "total_supply": 1e10, "max_supply": 1e10,
                                                             "market_cap": {"usd": 1177614971}, "fully_diluted_valuation": {"usd": 1735468312}}}
    t = pb.tokenomics(arb)
    assert t["circulating_share"] == pytest.approx(0.6786, abs=1e-4) and t["fdv_to_mcap"] == pytest.approx(1.4737, abs=1e-3)
    assert t["verdict"] == "DILUTION RISK" and "32% still to unlock" in t["flags"][0]
    assert "no max supply" in pb.tokenomics({"market_data": {"circulating_supply": 1, "max_supply": None, "total_supply": 1}})["flags"][0]


def test_tax_lots_fifo_terms_and_fees():
    fills = [{"time": "2024-01-01", "asset": "SOL", "side": "buy", "qty": 1, "price": 100, "fee": 1},
             {"time": "2025-06-01", "asset": "SOL", "side": "buy", "qty": 1, "price": 200, "fee": 2},
             {"time": "2025-06-02", "asset": "SOL", "side": "sell", "qty": 1.5, "price": 300, "fee": 3},  # a swap's sell leg
             {"time": "2025-06-02", "asset": "BONK", "side": "buy", "qty": 1000, "price": 0.447, "fee": 0}]
    out = pb.tax_lots(fills)
    a, b = out["rows"]
    assert (a["term"], a["cost_basis"], a["proceeds"]) == ("long", 101.0, 298.0)
    assert (b["term"], b["cost_basis"], b["proceeds"]) == ("short", 101.0, 149.0)
    assert out["long_term_gain"] == 197.0 and out["short_term_gain"] == 48.0 and out["open_lots"] == {"SOL": 0.5, "BONK": 1000.0}
    assert pb.tax_csv(out["rows"]).splitlines()[0].startswith("description,date_acquired")
    assert pb.tax_lots([{"time": "2025-01-01", "asset": "X", "side": "sell", "qty": 1, "price": 5}])["rows"][0]["term"].startswith("UNKNOWN")


# ------------------------------------------------------------------ whale safeguards


def test_flags_for_wash_trading_distributors_exchange_inflows_and_new_wallets():
    c = Chain()
    for i in range(6):
        trade(c, "WASH", 1000 + i * 4000, 0.0, 600)  # round trips at the same price within the hour
    for i in range(4):
        c.add("VC", 100 + i, 1000, "TOK")  # tokens arrive, nothing paid (vesting)
        x = c.txs[c.sigs["VC"][0]["signature"]]
        x["meta"]["preTokenBalances"].append({"accountIndex": 2, "mint": "TOK", "owner": "TREASURY", "uiTokenAmount": {"uiAmountString": "5000"}})
    for i in range(5):
        c.add("VC", 500 + i, -500, "TOK", sol_delta=0.5)
    c.add("VC", 900, -100, "TOK")
    c.txs[c.sigs["VC"][0]["signature"]]["meta"]["postTokenBalances"].append({"accountIndex": 2, "mint": "TOK", "owner": "EXCH1", "uiTokenAmount": {"uiAmountString": "100"}})
    trs = []
    vc = wh.score("VC", wh.history(c, "VC", transfers=trs), transfers=trs, labels={"EXCH1": "Binance deposit"}, born=0, now=10 * 86400)
    assert any("vesting distributor" in f for f in vc.flags) and any("to Binance deposit" in f for f in vc.flags)
    assert any("days old" in f for f in vc.flags)
    wash = wh.score("WASH", wh.history(c, "WASH"))
    assert any("wash trading" in f for f in wash.flags)


class PricedTrader(Trader):
    def __init__(self, px):
        super().__init__()
        self.px = px

    def price_usd(self, mint, usdc):
        return self.px


def test_copy_sleeve_cap_and_chase_guard(tmp_path):
    c = Chain()
    trade(c, "W", 1000, 0.1, 3600)
    st = tmp_path / "s.json"
    wh.watch(c, Trader(), ["W"], st, now=lambda: 5000)
    c.add("W", 6000, 1000, "A", usdc_delta=-100)  # whale pays $0.10 per token
    t = PricedTrader(0.12)  # we'd pay 20% more
    ev = wh.watch(c, t, ["W"], st, now=lambda: 6010)
    assert t.calls == [] and "exit liquidity" in ev[0]["copy"]["why"]
    c.add("W", 7000, 1000, "B", usdc_delta=-100)
    c.add("W", 7001, 1000, "C", usdc_delta=-100)
    t = PricedTrader(0.101)
    ev = wh.watch(c, t, ["W"], st, now=lambda: 7010, sleeve_usd=60)
    assert t.calls == [("buy", "B")] and "sleeve full" in ev[-1]["copy"]["why"]


def gmgn_fixture(now):
    """Same keys as a real gmgn-ai/crypto/wallet_rank response (2026-10-15); addresses and names made up."""
    def w(addr, tags, txs, hold, pnl, wr, active=now - 600):
        return {"address": addr, "tags": tags, "txs_7d": txs, "avg_holding_period_7d": hold, "pnl_7d": pnl, "winrate_7d": wr,
                "last_active": active, "source_url": f"https://gmgn.ai/sol/address/{addr}", "daily_profit_7d": [], "twitter_username": None}
    return {"data": {"alexandria": [{"provider": "gmgn-ai", "capability": "crypto/wallet_rank", "data": {"chain": "sol", "count": 6, "wallets": [
        w("BOT1", ["kol", "wash_trader", "arbitrager"], 5646, 213591, 0.22, 0.68),
        w("ARB1", ["arbitrager"], 900, 40000, 0.15, 0.7),
        w("FAST", ["smart_degen"], 300, 90, 0.4, 0.6),
        w("OLD1", ["smart_degen"], 100, 90000, 0.3, 0.5, active=now - 10 * 86400),
        w("KOL1", ["kol", "fomo"], 210, 200000, 0.11, 0.38),
        w("GOOD", ["smart_degen"], 120, 300000, 0.09, 0.45)]}}]}}


def test_discover_drops_what_cannot_be_copied_and_keeps_the_screening_count(tmp_path, capsys):
    from tradebot.cli import main
    out = wh.discover(gmgn_fixture(1_000_000), lag_s=30, now=1_000_000)
    assert out["screened"] == 6 and [c["wallet"] for c in out["candidates"]] == ["KOL1", "GOOD"]
    why = {d["wallet"]: " ".join(d["why"]) for d in out["dropped"]}
    assert "wash trader" in why["BOT1"] and "bot" in why["BOT1"] and "arbitrager" in why["ARB1"]
    assert "too fast" in why["FAST"] and "inactive" in why["OLD1"]
    assert any("KOL" in n for n in out["candidates"][0]["notes"]) and any("fomo" in n for n in out["candidates"][0]["notes"])
    import time as _t
    f = tmp_path / "rank.json"
    f.write_text(json.dumps(gmgn_fixture(_t.time())))
    main(["whales", "discover", str(f), "--out", str(tmp_path / "w.csv")])
    res = json.loads(capsys.readouterr().out)
    lines = (tmp_path / "w.csv").read_text().splitlines()
    assert res["screened"] == 6 and len(lines) == 3 and lines[1].split(",")[1] == "6"  # the survivors carry the full count
