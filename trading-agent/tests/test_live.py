import base64
import hashlib
import hmac
import json
import os
import tempfile
import unittest
from pathlib import Path

from tradebot.alerts import Alerts
from tradebot.backtest import synthetic_candles
from tradebot.broker import PaperBroker
from tradebot.evidence import build_package
from tradebot.execution import OrderManager
from tradebot.experiments import DataSnapshot, Register, Spec
from tradebot.live import (CONFIRM_UNPROVEN, CanaryGuard, LiveCaps, NotAuthorized, authorize_live, init_caps,
                           launch_gates, require_live)
from tradebot.ops_gate import run_gate
from tradebot.release import approve, init_limits
from tradebot.risk import RiskLimits, RiskManager
from tradebot.types import Candle, OrderBook
from tradebot.validation import final_evaluation, sweep
from tradebot.venues_live import CryptoComLive, LiveVenue, cryptocom_param_string, cryptocom_sign


def _release(tmp: Path) -> Register:
    reg = Register(tmp / "runs/research")
    candles = synthetic_candles(1400, seed=3)
    snap = DataSnapshot.of(candles, "synthetic", "BTC_USD", "1h")
    reg.save_data(candles, snap)
    recs = sweep(reg, Spec(name="fam"), {"reward_risk": [2.0]}, candles, snap, 0, 800, tmp / "w")
    spec = Spec(**recs[0]["spec"])
    reg.freeze(spec, "t")
    fin = final_evaluation(reg, spec.id, candles, snap, 800, 1400, tmp / "w")
    build_package(reg, fin["run_id"])
    run_gate(reg)
    init_limits()
    approve(reg, spec.id, "op", confirm=lambda c: True, accept_weak_evidence=True)
    init_caps()
    return reg


class FakeCryptoCom:
    """Speaks the Crypto.com Exchange v1 shapes. Verifies every signature."""

    def __init__(self, key="k", secret="s", base_bal=0.0, usd=1000.0, fill_frac=1.0, price=100.0):
        self.key, self.secret = key, secret
        self.bal = {"BTC": base_bal, "USD": usd}
        self.fill_frac, self.price = fill_frac, price
        self.orders, self.calls = {}, []

    def __call__(self, method, url, headers, body):
        self.calls.append((method, url, body))
        if url.endswith("public/get-instruments"):
            return 200, {"code": 0, "result": {"data": [{"symbol": "BTC_USD", "price_tick_size": "0.01", "qty_tick_size": "0.0001"}]}}
        m, p = body["method"], body["params"]
        expect = cryptocom_sign(self.secret, m, body["id"], body["api_key"], p, body["nonce"])
        if body["sig"] != expect:
            return 401, {"code": 40101, "message": "bad sig"}
        if m == "private/create-order":
            q = float(p["quantity"]) * self.fill_frac
            px = min(self.price, float(p["price"])) if p["side"] == "BUY" else max(self.price, float(p["price"]))
            self.orders[p["client_oid"]] = {"status": "FILLED" if q else "CANCELED", "cumulative_quantity": str(q),
                                            "avg_price": str(px), "cumulative_fee": str(q * px * 0.0005),
                                            "fee_instrument_name": "USD"}
            sgn = 1 if p["side"] == "BUY" else -1
            self.bal["BTC"] += sgn * q
            self.bal["USD"] -= sgn * q * px
            return 200, {"code": 0, "result": {"order_id": "1", "client_oid": p["client_oid"]}}
        if m == "private/get-order-detail":
            return 200, {"code": 0, "result": self.orders[p["client_oid"]]}
        if m == "private/user-balance":
            return 200, {"code": 0, "result": {"data": [{"position_balances": [
                {"instrument_name": k, "quantity": str(v)} for k, v in self.bal.items()]}]}}
        if m == "private/cancel-all-orders":
            return 200, {"code": 0, "result": {}}
        return 400, {"code": 1}


class LiveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cwd = os.getcwd()
        cls.tmp = Path(tempfile.mkdtemp())
        os.chdir(cls.tmp)
        cls.reg = _release(cls.tmp)

    @classmethod
    def tearDownClass(cls):
        os.chdir(cls.cwd)

    def _authorize(self, venues=("cryptocom",), phrase=CONFIRM_UNPROVEN):
        return authorize_live(self.reg, self.tmp / "runs/paper", "op", list(venues), accept_unproven=True, ask=lambda _: phrase)

    def test_gates_block_without_paper_record(self):
        gates = launch_gates(self.tmp / "runs/paper", self.reg)
        self.assertFalse(all(g.passed for g in gates))
        with self.assertRaises(NotAuthorized):
            authorize_live(self.reg, self.tmp / "runs/paper", "op", ["cryptocom"], ask=lambda _: "GO LIVE")

    def test_authorization_lifecycle(self):
        with self.assertRaises(NotAuthorized):
            self._authorize(phrase="GO LIVE")  # unproven needs the long phrase
        auth = self._authorize(("cryptocom", "solana"))
        self.assertTrue(auth["unproven_accepted"])
        self.assertEqual(require_live("cryptocom").caps, LiveCaps())
        with self.assertRaises(NotAuthorized):
            require_live("alpaca")
        with self.assertRaises(NotAuthorized):
            require_live("cryptocom", now_ms=auth["expires_ms"] + 1)
        caps = Path("config/live_caps.json")
        orig = caps.read_text()
        caps.write_text(orig.replace("100.0", "1000.0"))
        try:
            with self.assertRaises(NotAuthorized):
                require_live("cryptocom")
        finally:
            caps.write_text(orig)
        require_live("cryptocom")

    def test_canary_caps(self):
        g = CanaryGuard(LiveCaps(max_order_usd=100, max_position_usd=300, max_daily_loss_usd=50))
        self.assertTrue(g.check("buy", 2, 100, 0, 1000, 0))  # $200 order
        self.assertFalse(g.check("buy", 0.5, 100, 0, 1000, 0))
        self.assertTrue(g.check("buy", 0.5, 100, 2.8, 1000, 0))  # position would be $330
        self.assertFalse(g.check("sell", 5, 100, 5, 1000, 0))  # reduce-only: always allowed
        self.assertTrue(g.check("buy", 0.1, 100, 0, 940, 1))  # down $60 on the day

    def test_cryptocom_signature_is_the_documented_one(self):
        params = {"instrument_name": "BTC_USD", "side": "BUY", "quantity": "0.1", "nested": {"b": 1, "a": [1, {"z": None}]}}
        self.assertEqual(cryptocom_param_string(params), "instrument_nameBTC_USDnesteda1znullb1quantity0.1sideBUY")
        payload = "private/create-order" + "7" + "key" + cryptocom_param_string(params) + "1700000000000"
        self.assertEqual(cryptocom_sign("sec", "private/create-order", 7, "key", params, 1700000000000),
                         hmac.new(b"sec", payload.encode(), hashlib.sha256).hexdigest())

    def _venue(self, fake, rd: Path):
        self._authorize()
        client = CryptoComLive(require_live("cryptocom"), "k", "s", transport=fake)
        broker = PaperBroker(cash=500.0, fee_bps=0, slippage_bps=0)
        lv = LiveVenue(client, "BTC_USD", broker, rd / "baseline.json", sleep=lambda s: None)
        risk = RiskManager(RiskLimits(max_position_frac=1.0, max_order_frac=1.0), rd / "KILL", rd / "risk.json")
        return lv, OrderManager(lv, risk, rd / "orders.jsonl"), broker

    def test_live_venue_orders(self):
        rd = Path(tempfile.mkdtemp())
        fake = FakeCryptoCom(base_bal=0.25)  # funds held before the bot started
        lv, om, broker = self._venue(fake, rd)
        self.assertEqual(lv.position_qty("BTC_USD"), 0.0)  # baseline excluded
        v, fill = om.submit(0, "BTC_USD", "buy", 0.5, 100.0, 500.0, 0, 1.0, "entry")
        self.assertTrue(v.allowed)
        self.assertAlmostEqual(fill.qty, 0.5)
        sent = [b for _, _, b in fake.calls if b and b["method"] == "private/create-order"][0]["params"]
        self.assertEqual(sent["time_in_force"], "IMMEDIATE_OR_CANCEL")
        self.assertEqual(sent["type"], "LIMIT")
        self.assertLessEqual(float(sent["price"]), 100.0 * 1.0015)
        self.assertAlmostEqual(lv.position_qty("BTC_USD"), 0.5)
        self.assertEqual(lv.reconcile(), [])
        v, _ = om.submit(0, "BTC_USD", "buy", 0.5, 100.0, 500.0, 0, 1.0, "entry")
        self.assertFalse(v.allowed)  # same client id: never resent
        v, _ = om.submit(1, "BTC_USD", "buy", 2.0, 100.0, 500.0, 1, 1.0, "entry")
        self.assertFalse(v.allowed)
        self.assertIn("canary", " ".join(v.reasons))
        v, fill = om.submit(2, "BTC_USD", "sell", 0.5, 100.0, 500.0, 2, 1.0, "stop")
        self.assertTrue(v.allowed)
        v, _ = om.submit(3, "BTC_USD", "sell", 0.3, 100.0, 500.0, 3, 1.0, "p_raw short")
        self.assertFalse(v.allowed)  # spot: no shorting
        fake.bal["BTC"] += 0.1  # someone traded on the account by hand
        self.assertTrue(lv.reconcile())

    def test_partial_fill_mirrors_real_quantity(self):
        rd = Path(tempfile.mkdtemp())
        lv, om, broker = self._venue(FakeCryptoCom(fill_frac=0.4), rd)
        v, fill = om.submit(0, "BTC_USD", "buy", 0.5, 100.0, 500.0, 0, 1.0, "entry")
        self.assertAlmostEqual(fill.qty, 0.2)
        self.assertEqual(om.ledger["BTC_USD:0:open:buy"]["state"], "partial")
        self.assertEqual(lv.reconcile(), [])

    def test_alpaca_and_oanda_live_shapes(self):
        from tradebot.assets import AssetClass, Instrument, classify_stock
        from tradebot.live import LiveAuthorization
        from tradebot.venues_live import ALPACA_LIVE, OANDA_LIVE, AlpacaLive, OandaLive

        calls = []

        def alp(method, url, h, body):
            calls.append((method, url, body))
            if method == "POST":
                return 200, {"id": "o1", "status": "accepted"}
            return 200, {"status": "canceled", "filled_qty": "3", "filled_avg_price": "187.20"}

        a = AlpacaLive(LiveAuthorization("alpaca", LiveCaps(), {}), "id", "sec", transport=alp)
        with self.assertRaises(ValueError):
            AlpacaLive(LiveAuthorization("oanda", LiveCaps(), {}), "id", "sec", transport=alp)
        a.create_ioc_limit("cid", classify_stock("AAPL", 187.0, fractional=False), "buy", 3.7, 187.4)
        self.assertTrue(calls[0][1].startswith(ALPACA_LIVE))
        self.assertEqual(calls[0][2]["time_in_force"], "ioc")
        self.assertEqual(calls[0][2]["qty"], "3")
        self.assertEqual(a.order_detail("cid")["status"], "FILLED")  # IOC remainder cancelled, fill stands

        def oan(method, url, h, body):
            calls.append((method, url, body))
            return 201, {"orderFillTransaction": {"units": "-2000", "price": "1.08412", "commission": "0.0"}}

        o = OandaLive(LiveAuthorization("oanda", LiveCaps(), {}), "acct", "tok", transport=oan)
        o.create_ioc_limit("fx1", Instrument("EUR_USD", AssetClass.FOREX, "forex_major", qty_step=1.0), "sell", 2400, 1.0838)
        self.assertTrue(calls[-1][1].startswith(OANDA_LIVE))
        self.assertEqual(calls[-1][2]["order"]["timeInForce"], "IOC")
        self.assertEqual(calls[-1][2]["order"]["units"], "-2000")
        d = o.order_detail("fx1")
        self.assertEqual((d["status"], d["cumulative_quantity"], d["avg_price"]), ("FILLED", "2000.0", "1.08412"))

    def test_alerts_never_raise(self):
        class J:
            rows = []

            def write(self, kind, ts, **f):
                self.rows.append(kind)

        def boom(url, payload):
            raise OSError("down")

        a = Alerts("https://hooks.example/x", send=boom, journal=J())
        self.assertFalse(a("critical", "kill", reason="test"))
        self.assertEqual(J.rows, ["alert_failed"])
        got = []
        Alerts("u", send=lambda u, p: got.append(p))("info", "fill", qty=1)
        self.assertIn("fill", got[0]["text"])
        self.assertIn("content", got[0])

    def test_run_live_flattens_on_kill(self):
        from tradebot.live_runner import run_live
        from tradebot.policy import PolicyParams
        from tradebot.release import verify_release

        self._authorize()
        rd = Path(tempfile.mkdtemp())
        fake = FakeCryptoCom(base_bal=0.0, usd=400.0)
        client = CryptoComLive(require_live("cryptocom"), "k", "s", transport=fake)

        class Feed:
            bar_ms = 3_600_000

            def book(self):
                return OrderBook(now[0], ((99.99, 5.0),), ((100.01, 5.0),))

            def closed_candles(self, n):
                return [Candle(i * 3_600_000, (i + 1) * 3_600_000, 100, 101, 99, 100, 10) for i in range(n // 3_600_000 - 60, n // 3_600_000)]

        now = [1_800_000_000_000]
        spec, limits, rel = verify_release(Path("releases/approved.json"))
        sent = []
        alerts = Alerts("u", send=lambda u, p: sent.append(p))
        out = run_live("cryptocom", "BTC_USD", "1h", "rules", rd, PolicyParams(), limits, rel, poll_s=0, max_ticks=2,
                       client=client, feed=Feed(), alerts=alerts, now_fn=lambda: now[0], sleep=lambda s: None)
        self.assertIsNone(out["killed"])
        self.assertEqual(json.loads((rd / "broker.json").read_text())["cash"], 400.0)  # min(cash, cap $500)
        # give the bot a position on both sides of the ledger, then kill
        b = json.loads((rd / "broker.json").read_text())
        b["positions"] = {"BTC_USD": {"qty": 1.0, "avg_entry": 100.0, "stop": 0, "take": 0}}
        (rd / "broker.json").write_text(json.dumps(b))
        fake.bal["BTC"] = 1.0
        with (rd / "orders.jsonl").open("a") as f:
            f.write(json.dumps({"client_id": "seed", "ts": 0, "side": "buy", "qty": 1.0, "state": "filled", "filled_qty": 1.0,
                                "fill": {"ts": 0, "instrument": "BTC_USD", "side": "buy", "qty": 1.0, "price": 100.0, "fee": 0, "reason": "entry"}}) + "\n")
        (rd / "KILL").write_text("operator test")
        run_live("cryptocom", "BTC_USD", "1h", "rules", rd, PolicyParams(), limits, rel, poll_s=0, max_ticks=2,
                 client=client, feed=Feed(), alerts=alerts, now_fn=lambda: now[0], sleep=lambda s: None)
        self.assertAlmostEqual(fake.bal["BTC"], 0.0)
        self.assertTrue(any("KILL" in p["title"] for p in sent))


class SolanaLive(unittest.TestCase):
    def setUp(self):
        from solders.hash import Hash
        from solders.keypair import Keypair
        from solders.message import MessageV0
        from solders.signature import Signature
        from solders.system_program import TransferParams, transfer
        from solders.transaction import VersionedTransaction

        from tradebot.live import LiveAuthorization
        from tradebot.onchain import USDC_MINT

        self.kp, other = Keypair(), Keypair()
        self.tip_acct = str(Keypair().pubkey())

        def unsigned(payer):
            ix = transfer(TransferParams(from_pubkey=payer.pubkey(), to_pubkey=other.pubkey(), lamports=1))
            msg = MessageV0.try_compile(payer.pubkey(), [ix], [], Hash.default())
            return base64.b64encode(bytes(VersionedTransaction.populate(msg, [Signature.default()]))).decode()

        self.unsigned = unsigned
        self.payer = self.kp
        self.sim_err = None
        self.bundles, self.bal = [], {"MINT": 0, USDC_MINT: 1_000_000_000}
        test = self

        class Jup:
            base = "https://jup.test/swap/v1"

            def quote(self, i, o, amt, slip):
                return {"inputMint": i, "outputMint": o, "inAmount": str(amt), "outAmount": "5000", "priceImpactPct": "0.001"}

        def post(url, body):
            if url.endswith("/swap"):
                test.bal["MINT"] += 5000
                return {"swapTransaction": unsigned(test.payer)}
            m = body["method"]
            if m == "getTipAccounts":
                return {"result": [test.tip_acct]}
            if m == "sendBundle":
                test.bundles.append(body["params"][0])
                return {"result": "bundle-1"}
            if m == "simulateTransaction":
                return {"result": {"value": {"err": test.sim_err}}}
            if m == "getSignatureStatuses":
                return {"result": {"value": [{"confirmationStatus": "confirmed", "err": None}]}}
            if m == "getTokenAccountsByOwner":
                amt = test.bal[body["params"][1]["mint"]]
                return {"result": {"value": [{"account": {"data": {"parsed": {"info": {"tokenAmount": {"amount": str(amt)}}}}}}]}}
            raise AssertionError(m)

        class Guard:
            passed = True

            def assess(self, *a):
                from tradebot.onchain import RugReport

                return RugReport("MINT", self.passed, {"mint_authority": (self.passed, "revoked" if self.passed else "ACTIVE")})

        from tradebot.onchain import SolanaRPC
        from tradebot.solana_live import JitoSender, SolanaLiveTrader

        self.guard = Guard()
        auth = LiveAuthorization("solana", LiveCaps(), {})
        self.t = SolanaLiveTrader(auth, self.kp, SolanaRPC("rpc", post=post), Jup(), self.guard, JitoSender(10_000, "jito", post),
                                  state_path=Path(tempfile.mkdtemp()) / "s.json", sleep=lambda s: None, post=post)

    def test_buy_signs_simulates_and_bundles(self):
        from solders.signature import Signature
        from solders.transaction import VersionedTransaction

        out = self.t.buy("MINT", 50.0)
        self.assertEqual(out["tokens_raw"], 5000)
        self.assertEqual(out["bundle"], "bundle-1")
        swap, tip = (VersionedTransaction.from_bytes(base64.b64decode(x)) for x in self.bundles[0])
        self.assertNotEqual(swap.signatures[0], Signature.default())
        from solders.message import to_bytes_versioned

        self.assertTrue(swap.signatures[0].verify(self.kp.pubkey(), to_bytes_versioned(swap.message)))
        self.assertIn(self.tip_acct, [str(k) for k in tip.message.account_keys])

    def test_refusals(self):
        from tradebot.solana_live import SwapRefused

        with self.assertRaises(SwapRefused):
            self.t.buy("MINT", 500.0)  # over the $100 canary order cap
        self.guard.passed = False
        with self.assertRaises(SwapRefused):
            self.t.buy("MINT", 10.0)
        self.guard.passed = True
        from solders.keypair import Keypair

        self.payer = Keypair()  # a swap tx paid by someone else must never be signed
        with self.assertRaises(SwapRefused):
            self.t.buy("MINT", 10.0)
        self.payer = self.kp
        self.sim_err = {"InstructionError": [0, "Custom"]}
        with self.assertRaises(SwapRefused):
            self.t.buy("MINT", 10.0)
        self.assertEqual(self.bundles, [])

    def test_keypair_file_permissions(self):
        from solders.keypair import Keypair

        from tradebot.solana_live import SwapRefused, load_keypair

        p = Path(tempfile.mkdtemp()) / "id.json"
        kp = Keypair()
        p.write_text(json.dumps(list(bytes(kp))))
        p.chmod(0o644)
        with self.assertRaises(SwapRefused):
            load_keypair(str(p))
        p.chmod(0o600)
        self.assertEqual(load_keypair(str(p)).pubkey(), kp.pubkey())




class StockFxFeeds(unittest.TestCase):
    def test_alpaca_and_oanda_feeds_drop_forming_bars(self):
        from tradebot.feed import AlpacaStockFeed, OandaFxFeed

        def alp(method, url, h, body):
            if "/bars" in url:
                return 200, {"bars": [{"t": "2026-10-13T14:00:00Z", "o": 1, "h": 2, "l": 0.5, "c": 1.5, "v": 100},
                                      {"t": "2026-10-13T15:00:00Z", "o": 1.5, "h": 2, "l": 1, "c": 1.8, "v": 90}]}
            return 200, {"quote": {"t": "2026-10-13T15:30:01.123456Z", "bp": 1.79, "bs": 3, "ap": 1.81, "as": 2}}

        f = AlpacaStockFeed("AAPL", "1h", transport=alp)
        now = 1791905400000  # 2026-10-13T15:30:00Z
        c = f.closed_candles(now)
        self.assertEqual(len(c), 1)
        self.assertEqual(c[0].close_ts, 1791903600000)
        self.assertEqual(f.book().best_ask, 1.81)

        def oan(method, url, h, body):
            if "/candles" in url:
                return 200, {"candles": [{"time": "2026-10-13T14:00:00.000000000Z", "complete": True, "volume": 50,
                                          "mid": {"o": "1.08", "h": "1.09", "l": "1.07", "c": "1.085"}},
                                         {"time": "2026-10-13T15:00:00.000000000Z", "complete": False, "volume": 5,
                                          "mid": {"o": "1.085", "h": "1.09", "l": "1.08", "c": "1.086"}}]}
            return 200, {"prices": [{"time": "2026-10-13T15:30:00.5Z", "bids": [{"price": "1.0859", "liquidity": 1e6}],
                                     "asks": [{"price": "1.0861", "liquidity": 1e6}]}]}

        o = OandaFxFeed("EUR_USD", "1h", transport=oan)
        self.assertEqual(len(o.closed_candles(now)), 1)
        self.assertAlmostEqual(o.book().mid, 1.086)


if __name__ == "__main__":
    unittest.main()
