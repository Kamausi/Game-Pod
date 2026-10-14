import json
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path

from tradebot import arbitrage as arb
from tradebot.assets import AssetClass, Instrument, classify_stock, is_open, normalize_qty, pre_trade
from tradebot.brokers import AlpacaPaper, ExecutionHub, LiveHostRefused, OandaPractice, OrderRequest
from tradebot.datasets import load_csv
from tradebot.marketstream import LatencyStats, parse_book_message
from tradebot.onchain import DexPaperTrader, Jupiter, RugGuard, SolanaRPC
from tradebot.sentiment import SentimentStore, assets_mentioned, lexicon_score, parse_rss, refresh
from tradebot.storage import Store


def ms(s):
    return int(datetime.fromisoformat(s).replace(tzinfo=timezone.utc).timestamp() * 1000)


class Assets(unittest.TestCase):
    def test_sessions(self):
        stock = classify_stock("AAPL", 230)
        fx = Instrument("EUR_USD", AssetClass.FOREX, "forex_major", fx_lot_units=1000)
        self.assertTrue(is_open(stock, ms("2026-10-14T15:00:00")))  # Wed 11:00 New York
        self.assertFalse(is_open(stock, ms("2026-10-14T21:00:00")))  # 17:00 New York
        self.assertFalse(is_open(stock, ms("2026-10-17T15:00:00")))  # Saturday
        self.assertFalse(is_open(fx, ms("2026-10-17T15:00:00")))  # Saturday
        self.assertTrue(is_open(fx, ms("2026-10-18T22:00:00")))  # Sunday 18:00 New York

    def test_penny_rules_and_rounding(self):
        p = classify_stock("XYZ", 0.45)
        self.assertEqual(p.tier, "penny_stock")
        chk = pre_trade(p, ms("2026-10-14T15:00:00"), 0.45, 0.46, 2e5, 0.05)
        self.assertFalse(chk.ok)
        self.assertEqual(chk.order_type, "limit")
        self.assertEqual(len(chk.reasons), 3)  # spread, volume, size
        self.assertEqual(normalize_qty(p, 123.9), 123)
        self.assertEqual(normalize_qty(Instrument("EUR_USD", AssetClass.FOREX, "forex_major"), 12_345), 12_000)
        self.assertEqual(normalize_qty(classify_stock("AAPL", 230), 1.23456789), 1.234567)


class Brokers(unittest.TestCase):
    def setUp(self):
        self.calls = []

        def t(method, url, headers, body):
            self.calls.append((method, url, body))
            if "oanda" in url:
                return 201, {"orderFillTransaction": {"id": "77"}}
            return 200, {"id": "abc", "status": "accepted"}

        self.t = t

    def test_paper_only(self):
        with self.assertRaises(LiveHostRefused):
            AlpacaPaper(base="https://api.alpaca.markets")
        with self.assertRaises(LiveHostRefused):
            OandaPractice(base="https://api-fxtrade.oanda.com")

    def test_alpaca_bracket_and_trailing(self):
        b = AlpacaPaper("k", "s", transport=self.t)
        inst = classify_stock("AAPL", 230)
        b.submit(OrderRequest("c1", inst, "buy", 3.5, stop_loss=220, take_profit=245))
        body = self.calls[-1][2]
        self.assertEqual(self.calls[-1][1], "https://paper-api.alpaca.markets/v2/orders")
        self.assertEqual((body["order_class"], body["client_order_id"], body["qty"]), ("bracket", "c1", "3.5"))
        self.assertEqual(body["stop_loss"], {"stop_price": "220.00"})
        b.submit(OrderRequest("c2", inst, "sell", 3, trail_pct=1.5))
        self.assertEqual(self.calls[-1][2]["type"], "trailing_stop")

    def test_oanda_units_and_attached_orders(self):
        b = OandaPractice("acct", "tok", transport=self.t)
        fx = Instrument("EUR_USD", AssetClass.FOREX, "forex_major")
        ack = b.submit(OrderRequest("fx1", fx, "sell", 25_500, stop_loss=1.09, take_profit=1.07))
        o = self.calls[-1][2]["order"]
        self.assertEqual((o["units"], o["type"], o["stopLossOnFill"]["price"]), ("-25000", "MARKET", "1.09000"))
        self.assertEqual(ack.status, "filled")

    def test_hub_routes_and_dedupes_and_enforces_limit_only(self):
        hub = ExecutionHub({AssetClass.STOCK: AlpacaPaper("k", "s", transport=self.t)})
        o = OrderRequest("d1", classify_stock("AAPL", 230), "buy", 1)
        hub.route(o)
        hub.route(o)
        self.assertEqual(len(self.calls), 1)
        with self.assertRaises(ValueError):
            hub.route(OrderRequest("d2", classify_stock("XYZ", 0.4), "buy", 100))  # penny stock market order
        with self.assertRaises(ValueError):
            hub.route(OrderRequest("d3", Instrument("EUR_USD", AssetClass.FOREX, "forex_major"), "buy", 1000))  # no FX broker


RSS = """<?xml version="1.0"?><rss><channel>
<item><title>Bitcoin surges to record high as ETF inflows jump</title><link>https://x/1</link><pubDate>Wed, 14 Oct 2026 10:00:00 GMT</pubDate></item>
<item><title>Solana exchange hacked, funds drained</title><link>https://x/2</link><pubDate>Wed, 14 Oct 2026 10:05:00 GMT</pubDate></item>
<item><title>Regulator does not ban Ethereum staking</title><link>https://x/3</link></item>
<item><title>Weekly gardening tips</title><link>https://x/4</link></item>
</channel></rss>"""


class Sentiment(unittest.TestCase):
    def test_lexicon_and_mentions(self):
        self.assertGreater(lexicon_score("Bitcoin surges to record high"), 0.5)
        self.assertLess(lexicon_score("Exchange hacked, funds drained"), 0)
        self.assertGreater(lexicon_score("Regulator does not ban staking"), 0)  # negation flips
        self.assertEqual(assets_mentioned("Solana and Bitcoin rally"), ["BTC_USD", "SOL_USD"])

    def test_refuses_dtd(self):
        with self.assertRaises(ValueError):
            parse_rss('<?xml version="1.0"?><!DOCTYPE x [<!ENTITY a "b">]><rss></rss>', "evil", 0)

    def test_signal_is_causal_on_seen_time(self):
        st = SentimentStore(Path(tempfile.mkdtemp()) / "s.sqlite")
        seen = ms("2026-10-14T12:00:00")
        rep = refresh(st, feeds=["f"], fetch=lambda u: RSS, now_ms=seen)
        self.assertEqual(rep["new"], 4)
        self.assertEqual(rep["scored"], 3)  # the gardening item mentions no asset
        # headlines were published at 10:00 but SEEN at 12:00: invisible before 12:00
        self.assertEqual(st.signal("BTC_USD", seen - 1)[1], 0)
        s, n = st.signal("BTC_USD", seen + 60_000)
        self.assertEqual(n, 1)
        self.assertGreater(s, 0)
        self.assertLess(st.signal("SOL_USD", seen + 60_000)[0], 0)
        self.assertEqual(refresh(st, feeds=["f"], fetch=lambda u: RSS, now_ms=seen + 1)["new"], 0)  # no duplicates


class Arbitrage(unittest.TestCase):
    def test_scan_and_paper_book(self):
        now = 1_000_000
        qs = [arb.Quote("cryptocom", "BTC_USD", 83_000, 83_001, now, now), arb.Quote("kraken", "BTC_USD", 83_900, 83_901, now, now),
              arb.Quote("coinbase", "BTC_USD", 83_010, 83_011, now - 60_000, now - 60_000)]  # stale
        opps = arb.scan(qs, now)
        self.assertEqual([(o.buy_venue, o.sell_venue) for o in opps], [("cryptocom", "kraken")])
        self.assertAlmostEqual(opps[0].net_bps, opps[0].gross_bps - 7.5 - 40 - 3)
        book = arb.PaperArbBook({"cryptocom": {"USD": 10_000, "BTC_USD": 0}, "kraken": {"USD": 0, "BTC_USD": 0.05}})
        t = book.execute(opps[0], max_usd=2_000)
        self.assertGreater(t["pnl"], 0)
        self.assertAlmostEqual(book.balances["kraken"]["BTC_USD"], 0.05 - t["qty"])
        tiny = arb.Opportunity("BTC_USD", "cryptocom", "kraken", 83_000, 83_010, 1.2, -49.0)
        self.assertIsNone(book.execute(tiny, 1000))  # negative after fees: never traded

    def test_parsers(self):
        q = arb.parse_kraken({"error": [], "result": {"XXBTZUSD": {"a": ["83001.0", "1", "1"], "b": ["83000.0", "1", "1"]}}}, "BTC_USD", 5)
        self.assertEqual((q.bid, q.ask), (83000.0, 83001.0))
        q = arb.parse_coinbase({"bid": "1", "ask": "2", "time": "2026-10-14T11:00:00.123Z"}, "BTC_USD", 5)
        self.assertEqual(q.ts_ms, ms("2026-10-14T11:00:00") + 123)


class FakeChain:
    def __init__(self, mint_auth=None, freeze_auth=None, holders=(("pool", 600.0), ("w1", 50.0), ("w2", 40.0))):
        self.mint = {"mintAuthority": mint_auth, "freezeAuthority": freeze_auth, "supply": "1000000000", "decimals": 6}
        self.holders = holders

    def post(self, url, body):
        m, p = body["method"], body["params"]
        if m == "getAccountInfo" and p[0] == "MINT":
            return {"result": {"value": {"data": {"parsed": {"type": "mint", "info": self.mint}}}}}
        if m == "getAccountInfo":
            return {"result": {"value": {"data": {"parsed": {"info": {"owner": p[0].split(":")[1]}}}}}}
        if m == "getTokenLargestAccounts":
            return {"result": {"value": [{"address": f"acct:{o}", "uiAmount": a} for o, a in self.holders]}}
        return {"error": {"message": "unknown"}}


class OnChain(unittest.TestCase):
    def test_rug_guard(self):
        good = RugGuard(SolanaRPC(post=FakeChain().post)).assess("MINT", pool_owner="pool")
        self.assertTrue(good.checks["mint_authority_revoked"][0])
        self.assertTrue(good.checks["holder_concentration"][0])
        self.assertFalse(good.passed)  # liquidity lock not verified -> fail closed
        bad = RugGuard(SolanaRPC(post=FakeChain(mint_auth="Dev", freeze_auth="Dev").post)).assess("MINT", pool_owner="pool")
        self.assertFalse(bad.checks["mint_authority_revoked"][0])
        self.assertFalse(bad.checks["freeze_authority_revoked"][0])
        whale = RugGuard(SolanaRPC(post=FakeChain(holders=(("w1", 600.0),)).post)).assess("MINT")
        self.assertFalse(whale.checks["holder_concentration"][0])
        broken = RugGuard(SolanaRPC(post=lambda u, b: {"error": {"message": "rpc down"}})).assess("MINT")
        self.assertFalse(broken.passed)

    def test_dex_paper_trader(self):
        chain = FakeChain()

        class Guard(RugGuard):
            def assess(self, mint, pool_owner=None, lp_mint=None):
                rep = super().assess(mint, pool_owner, lp_mint)
                rep.checks["liquidity_locked"] = (True, "test")
                rep.passed = all(ok for ok, _ in rep.checks.values())
                return rep

        quotes = {"impact": "0.002"}
        jup = Jupiter(get=lambda url: {"outAmount": "5000000000", "priceImpactPct": quotes["impact"]})
        tr = DexPaperTrader(Guard(SolanaRPC(post=chain.post)), jup)
        r = tr.buy("MINT", 100, pool_owner="pool")
        self.assertEqual(r["action"], "paper_buy")
        self.assertEqual(r["tokens_raw"], int(5_000_000_000 * 0.995))
        quotes["impact"] = "0.05"
        self.assertEqual(tr.buy("MINT", 100, pool_owner="pool")["action"], "blocked")
        self.assertFalse(hasattr(tr, "send"))  # no transaction sending exists


class DataAndStorage(unittest.TestCase):
    def test_csv_import_report(self):
        p = Path(tempfile.mkdtemp()) / "spy.csv"
        rows = ["Date,Open,High,Low,Close,Volume"]
        for i in range(60):
            d = f"2001-01-{1 + i % 28:02d}" if i < 28 else f"2001-02-{1 + (i - 28) % 28:02d}" if i < 56 else f"2001-04-{i - 50:02d}"
            rows.append(f"{d},100,101,99,100.5,1000")
        rows.append("2001-01-01,100,101,99,100.5,1000")  # duplicate
        rows.append("2001-04-20,100,99,101,100,1")  # high < low
        p.write_text("\n".join(rows))
        bars, rep = load_csv(p, adjusted="split+dividend")
        self.assertEqual(rep.duplicates, 1)
        self.assertEqual(rep.broken, 1)
        self.assertEqual(len(rep.gaps), 1)  # Feb -> Apr
        self.assertEqual(rep.kept, len(bars))
        self.assertEqual(rep.first, "2001-01-01")

    def test_store(self):
        st = Store(Path(tempfile.mkdtemp()) / "t.sqlite")
        st.order("o1", "BTC_USD", "crypto", "buy", "market", 0.02, "PENDING", 1)
        st.fill("o1", 2, 0.01, 100.0, 0.06)
        st.fill("o1", 3, 0.01, 102.0, 0.06)
        st.commit()
        filled, avg, fee = st.db.execute("SELECT filled_qty, avg_fill_price, fee FROM orders").fetchone()
        self.assertAlmostEqual(filled, 0.02)
        self.assertAlmostEqual(avg, 101.0)
        self.assertAlmostEqual(fee, 0.12)


class Stream(unittest.TestCase):
    def test_parse_and_latency(self):
        msg = {"result": {"channel": "book", "instrument_name": "BTC_USD", "data": [{"t": 1000, "bids": [["83000", "0.5", "2"]], "asks": [["83001", "1", "1"]]}]}}
        b = parse_book_message(msg)
        self.assertEqual((b["instrument"], b["bids"][0], b["asks"][0][0]), ("BTC_USD", (83000.0, 0.5), 83001.0))
        self.assertIsNone(parse_book_message({"method": "public/heartbeat"}))
        st = LatencyStats()
        for i in range(100):
            st.add(float(i), 0.01)
        self.assertEqual(st.report()["wire_p50_ms"], 50.0)


if __name__ == "__main__":
    unittest.main()
