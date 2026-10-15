"""Low-cost universe ranking (real Crypto.com tickers) and the whale scorer / copy gate (fake chain)."""
import json
import random
from pathlib import Path

import pytest

from tradebot import onchain, universe as u, whales as wh

FX = Path(__file__).parent / "fixtures"


# ------------------------------------------------------------------ universe


def tickers():
    return json.loads((FX / "cryptocom_tickers_2026-10-15.json").read_text())["data"]


def test_btc_and_eth_are_the_cheapest_to_cross_not_the_most_expensive():
    rows = {r.symbol: r for r in u.crypto_universe(tickers(), "cryptocom", maker=False, min_volume_usd=0, order_usd=100, top=50)}
    assert rows["BTCUSD"].spread_bps < 0.01 and rows["ETHUSD"].spread_bps < 0.1
    assert all(rows["BTCUSD"].spread_bps <= r.spread_bps for r in rows.values())


def test_fees_dominate_and_maker_orders_halve_the_cost():
    taker = {r.symbol: r for r in u.crypto_universe(tickers(), "cryptocom", maker=False, min_volume_usd=1e6, order_usd=500)}
    maker = {r.symbol: r for r in u.crypto_universe(tickers(), "cryptocom", maker=True, min_volume_usd=1e6, order_usd=500)}
    x = taker["XRPUSD"]
    assert x.fee_bps == 100 and x.cost_bps == pytest.approx(100 + x.spread_bps + x.impact_bps, abs=0.02)
    assert maker["XRPUSD"].fee_bps == 50 and maker["XRPUSD"].cost_bps < x.cost_bps - 50
    cro = {r.symbol: r for r in u.crypto_universe(tickers(), "cryptocom_cro", maker=True, min_volume_usd=1e6, order_usd=500)}
    assert cro["SOLUSD"].fee_bps == 0 and cro["SOLUSD"].score > 10 * maker["SOLUSD"].score


def test_filters_exclusions_stablecoins_and_liquidity():
    rows = u.crypto_universe(tickers(), "alpaca_crypto", maker=True, min_volume_usd=1e6, order_usd=500, exclude=("BTC", "ETH"))
    syms = [r.symbol for r in rows]
    assert "BTCUSD" not in syms and "ETHUSD" not in syms and "USDTUSD" not in syms and "XRPUSD" in syms
    assert all(r.volume_usd >= 1e6 for r in rows) and "SOLUSDPERP" not in syms  # perps only on request
    assert [r.score for r in rows] == sorted((r.score for r in rows), reverse=True)
    big = u.crypto_universe(tickers(), "alpaca_crypto", maker=True, min_volume_usd=0, order_usd=200_000)
    assert all(200_000 <= 0.01 * r.volume_usd for r in big)  # an order over 1% of daily volume is not offered


def test_rest_short_form_and_stock_snapshots():
    n = u.normalize_cryptocom({"i": "XRP_USD", "b": "1.30", "k": "1.31", "a": "1.305", "h": "1.38", "l": "1.28", "vv": "2e7"})
    assert n["kind"] == "spot" and n["ask"] == 1.31
    snaps = {"F": {"latestQuote": {"bp": 11.99, "ap": 12.0}, "prevDailyBar": {"o": 12, "h": 12.4, "l": 11.8, "c": 12.0, "v": 5e7}},
             "XYZ": {"latestQuote": {"bp": 0, "ap": 1}, "prevDailyBar": {"c": 1, "h": 1, "l": 1, "v": 1}}}
    rows = u.stock_universe(snaps, maker=False, min_volume_usd=1e6, order_usd=1200)
    assert [r.symbol for r in rows] == ["F"]
    sec = (u.SEC_FEE_PER_DOLLAR * 1200 + min(u.FINRA_TAF_CAP, u.FINRA_TAF_PER_SHARE * 100)) / 1200 * 1e4
    assert rows[0].fee_bps == pytest.approx(sec, abs=0.01) and rows[0].fee_bps < 1  # commission-free: fees under 1 bp


def test_usdc_mint_is_the_real_one():
    assert onchain.USDC_MINT == wh.USDC == "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v"


# ------------------------------------------------------------------ whales


class Chain:
    """Fake JSON-RPC: per-address signature lists (newest first) and transactions."""

    def __init__(self):
        self.sigs, self.txs, self.n = {}, {}, 0

    def add(self, wallet, t, token_delta=None, mint="MINT", sol_delta=0.0, usdc_delta=0.0, fee=5000, err=None):
        self.n += 1
        sig = f"s{self.n}"
        pre_tb, post_tb = [], []
        for m, d in ((mint, token_delta), (wh.USDC, usdc_delta)):
            if d:
                pre_tb.append({"accountIndex": 1, "mint": m, "owner": wallet, "uiTokenAmount": {"uiAmountString": "1000000"}})
                post_tb.append({"accountIndex": 1, "mint": m, "owner": wallet, "uiTokenAmount": {"uiAmountString": str(1000000 + d)}})
        lam = int(sol_delta * 1e9) - fee
        self.txs[sig] = {"slot": self.n, "blockTime": t, "meta": {"err": err, "fee": fee, "preBalances": [10**12, 0], "postBalances": [10**12 + lam, 0],
                                                                   "preTokenBalances": pre_tb, "postTokenBalances": post_tb},
                         "transaction": {"message": {"accountKeys": [wallet, "POOL"]}}}
        self.sigs.setdefault(wallet, []).insert(0, {"signature": sig, "slot": self.n, "err": err, "blockTime": t})
        return sig

    def call(self, method, params):
        if method == "getSignaturesForAddress":
            lst, opts = self.sigs.get(params[0], []), params[1]
            if opts.get("before"):
                lst = lst[[s["signature"] for s in lst].index(opts["before"]) + 1:]
            return lst[: opts.get("limit", 1000)]
        if method == "getTransaction":
            return self.txs[params[0]]
        raise KeyError(method)


def trade(c, w, t, ret, hold, size=1.0, mint="MINT", usd=False):
    if usd:
        c.add(w, t, 1000, mint, usdc_delta=-100 * size)
        c.add(w, t + hold, -1000, mint, usdc_delta=100 * size * (1 + ret))
    else:
        c.add(w, t, 1000, mint, sol_delta=-size)
        c.add(w, t + hold, -1000, mint, sol_delta=size * (1 + ret))


def test_swap_parsing_skips_transfers_and_reads_sol_and_usdc_prices():
    c = Chain()
    trade(c, "W", 1000, 0.5, 3600)
    trade(c, "W", 9000, -0.2, 600, usd=True, mint="M2")
    c.add("W", 20000, 500, "AIRDROP")  # tokens arrive, nothing paid: not a trade
    c.add("W", 21000, 10, "MINT", sol_delta=-0.1, err={"x": 1})  # failed transaction
    sw = wh.history(c, "W")
    assert [(s.side, s.quote) for s in sw] == [("buy", "SOL"), ("sell", "SOL"), ("buy", "USD"), ("sell", "USD")]
    assert sw[0].price == pytest.approx(1 / 1000) and sw[1].price == pytest.approx(1.5 / 1000)  # the fee is added back
    s = wh.score("W", sw)
    assert s.closed == 2 and s.avg_return == pytest.approx((0.5 - 0.2) / 2) and s.median_hold_s == pytest.approx((3600 + 600) / 2)


def wallet(c, w, n, mean, sd, hold, seed):
    rng = random.Random(seed)
    for i in range(n):
        trade(c, w, 10_000 + i * 50_000, rng.gauss(mean, sd), hold, mint=f"T{i % 7}")


def test_gate_copies_a_proven_wallet_and_refuses_flippers_and_lucky_ones():
    c = Chain()
    wallet(c, "GOOD", 40, 0.12, 0.10, 4 * 3600, 1)
    wallet(c, "FLIP", 40, 0.12, 0.10, 5, 2)
    wallet(c, "FEW", 8, 0.30, 0.10, 4 * 3600, 3)
    wallet(c, "MEH", 25, 0.025, 0.10, 4 * 3600, 4)
    scores = [wh.score(w, wh.history(c, w), lag_s=30) for w in ("GOOD", "FLIP", "FEW", "MEH")]
    g = {x["wallet"]: x for x in wh.gate(scores, screened=4, lag_s=30, cost_bps=100)}
    assert g["GOOD"]["copy"] and not g["FLIP"]["copy"] and not g["FEW"]["copy"]
    assert any("holds too briefly" in w for w in g["FLIP"]["why"]) and any("closed trades" in w for w in g["FEW"]["why"])
    assert any("gone before" in f for f in scores[1].flags)
    assert not g["MEH"]["copy"]
    alone = wh.gate([scores[0]], screened=1)[0]
    crowd = wh.gate(scores, screened=2000)
    assert alone["copy"] and {x["wallet"]: x for x in crowd}["GOOD"]["dsr"] <= alone["dsr"]  # screening more wallets raises the bar


class Trader:
    def __init__(self):
        self.usdc, self.holdings, self.calls = 1000.0, {}, []

    def buy(self, mint, usdc):
        self.calls.append(("buy", mint))
        self.usdc -= usdc
        self.holdings[mint] = self.holdings.get(mint, 0) + 1
        return {"action": "paper_buy", "mint": mint}

    def sell_all(self, mint):
        self.calls.append(("sell", mint))
        amt = self.holdings.pop(mint, 0)
        self.usdc += 60 * amt
        return {"action": "paper_sell" if amt else "noop", "mint": mint}


def test_watch_copies_new_trades_and_keeps_the_paper_account_between_passes(tmp_path):
    c = Chain()
    trade(c, "GOOD", 1000, 0.1, 3600)  # history before we start watching: not copied
    st = tmp_path / "state.json"
    ev = wh.watch(c, Trader(), ["GOOD"], st, now=lambda: 10_000)
    assert "baseline" in ev[0]["event"]
    c.add("GOOD", 20_000, 1000, "NEW", sol_delta=-1)
    t1 = Trader()
    ev = wh.watch(c, t1, ["GOOD"], st, now=lambda: 20_030)
    assert t1.calls == [("buy", "NEW")] and ev[0]["delay_s"] == 30
    c.add("GOOD", 30_000, -1000, "NEW", sol_delta=1.3)
    t2 = Trader()  # a fresh process: the holdings must come back from the state file
    wh.watch(c, t2, ["GOOD"], st, now=lambda: 30_010)
    assert t2.calls == [("sell", "NEW")] and t2.usdc == pytest.approx(1000 - 50 + 60)
    assert json.loads(st.read_text())["copies"] == {}
