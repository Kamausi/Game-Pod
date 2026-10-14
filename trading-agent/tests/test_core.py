import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

from tradebot.agent import Agent
from tradebot.backtest import round_trips, run_backtest, synthetic_candles
from tradebot.broker import GuardedBroker, PaperBroker
from tradebot.engines import EscalatingEngine, RuleEngine, safe_decide
from tradebot.journal import Journal
from tradebot.policy import Calibrator, PolicyParams, decide, kelly_fraction
from tradebot.review import brier, pav, run_review
from tradebot.risk import RiskLimits, RiskManager
from tradebot.schema import Decision, SchemaError
from tradebot.state import LookaheadError, PortfolioView, StateEngine
from tradebot.types import OrderBook

GOOD = dict(
    regime="trending", direction="long", direction_confidence=0.9, p_win=0.6,
    toxic_flow=False, setup_quality=3, risk_state="safe", rationale="x",
)


def book_at(ts, px, spread_bps=2.0):
    hs = px * spread_bps / 2e4
    return OrderBook(ts, ((px - hs, 5.0), (px - 2 * hs, 5.0)), ((px + hs, 3.0), (px + 2 * hs, 3.0)))


def pf(eq=10_000.0):
    return PortfolioView(0.0, 0.0, eq, eq, eq)


class TestState(unittest.TestCase):
    def setUp(self):
        self.c = synthetic_candles(120)
        self.as_of = self.c[-1].close_ts

    def test_snapshot_fits_token_budget(self):
        s = StateEngine().snapshot("BTC_USD", self.as_of, self.c, book_at(self.as_of, self.c[-1].close), pf())
        text = s.to_prompt()
        self.assertLess(len(text) / 4, 400)
        self.assertGreater(s.imbalance, 0)  # bids heavier in book_at

    def test_future_candle_rejected(self):
        with self.assertRaises(LookaheadError):
            StateEngine().snapshot("BTC_USD", self.c[-2].close_ts, self.c, book_at(self.c[-2].close_ts, 1.0), pf())

    def test_future_book_rejected(self):
        with self.assertRaises(LookaheadError):
            StateEngine().snapshot("BTC_USD", self.as_of, self.c, book_at(self.as_of + 1, 1.0), pf())

    def test_snapshot_unchanged_by_future_data(self):
        """Values at time t must not depend on bars after t."""
        eng = StateEngine()
        t = self.c[99].close_ts
        a = eng.snapshot("X", t, self.c[:100], book_at(t, self.c[99].close), pf())
        mutated = self.c[:100] + [replace(x, close=x.close * 3) for x in self.c[100:]]
        b = eng.snapshot("X", t, [x for x in mutated if x.close_ts <= t], book_at(t, self.c[99].close), pf())
        self.assertEqual(a, b)


class TestSchema(unittest.TestCase):
    def test_parse_ok(self):
        self.assertEqual(Decision.parse(GOOD).setup_quality, 3)

    def test_rejects(self):
        for k, v in [("regime", "moon"), ("setup_quality", 4), ("setup_quality", True),
                     ("direction_confidence", 1.2), ("toxic_flow", "no"), ("risk_state", "mean_limit")]:
            with self.subTest(k=k, v=v), self.assertRaises(SchemaError):
                Decision.parse({**GOOD, k: v})
        with self.assertRaises(SchemaError):
            Decision.parse({k: v for k, v in GOOD.items() if k != "p_win"})


class TestPolicy(unittest.TestCase):
    def setUp(self):
        c = synthetic_candles(120)
        self.snap = StateEngine().snapshot("X", c[-1].close_ts, c, book_at(c[-1].close_ts, c[-1].close), pf())
        self.fitted = Calibrator(points=[(0.0, 0.0), (1.0, 1.0)], n_samples=10_000)

    def test_kelly(self):
        self.assertAlmostEqual(kelly_fraction(0.5, 2.0), 0.25)
        self.assertEqual(kelly_fraction(0.3, 2.0), 0.0)

    def test_gates(self):
        P = PolicyParams()
        for k, v in [("direction_confidence", 0.80), ("setup_quality", 1), ("risk_state", "near_limit"),
                     ("toxic_flow", True), ("direction", "neutral"), ("direction", "short")]:
            with self.subTest(k=k, v=v):
                i = decide(Decision.parse({**GOOD, k: v}), self.snap, None, P, self.fitted)
                self.assertEqual(i.action, "hold")
        self.assertEqual(decide(Decision.parse(GOOD), self.snap, None, P, self.fitted).action, "enter")

    def test_quarter_kelly_cap(self):
        with self.assertRaises(ValueError):
            PolicyParams(kelly_multiplier=0.5)
        P = PolicyParams()
        i = decide(Decision.parse({**GOOD, "p_win": 0.99}), self.snap, None, P, self.fitted)
        loss = i.stop_pct + P.round_trip_cost_pct + self.snap.spread_bps / 1e4
        self.assertLessEqual(i.size_frac * loss, 0.25 + 1e-9)  # risked fraction <= quarter of full Kelly (<=1)

    def test_uncalibrated_shrinks(self):
        cal = Calibrator()
        self.assertAlmostEqual(cal(0.9), 0.7)
        # p_win=0.6 shrinks to 0.55: still positive edge at 2:1, but much smaller size
        a = decide(Decision.parse(GOOD), self.snap, None, PolicyParams(), cal)
        b = decide(Decision.parse(GOOD), self.snap, None, PolicyParams(), self.fitted)
        self.assertLess(a.size_frac, b.size_frac)

    def test_failsafe_never_enters(self):
        i = decide(Decision.hold("boom"), self.snap, None, PolicyParams(), self.fitted)
        self.assertEqual(i.action, "hold")


class TestRisk(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.r = RiskManager(RiskLimits(), self.tmp / "KILL", self.tmp / "state.json")
        self.r.update(10_000, 0)

    def check(self, side="buy", qty=0.01, pos=0.0, eq=10_000, now=1000, data=1000, spread=2.0):
        return self.r.check_order(side, qty, 80_000, pos, eq, now, data, spread)

    def test_limits_cannot_be_loosened(self):
        with self.assertRaises(ValueError):
            RiskLimits(max_drawdown=0.30)
        with self.assertRaises(ValueError):
            RiskLimits(max_position_frac=2.0)

    def test_position_clamp(self):
        v = self.check(qty=1.0)  # $80k order on $10k equity
        self.assertTrue(v.allowed)
        self.assertAlmostEqual(v.qty * 80_000, 3_000)

    def test_drawdown_latches_and_persists(self):
        self.r.update(8_400, 1000)  # -16%
        self.assertTrue(self.r.is_killed())
        self.assertFalse(self.check().allowed)
        self.assertTrue(self.check(side="sell", qty=0.01, pos=0.02).allowed)  # exits still allowed
        r2 = RiskManager(RiskLimits(), self.tmp / "KILL2", self.tmp / "state.json")
        self.assertTrue(r2.is_killed())  # survives restart

    def test_kill_file(self):
        (self.tmp / "KILL").write_text("operator")
        self.assertFalse(self.check().allowed)

    def test_daily_loss_reduce_only(self):
        self.r.update(9_650, 1000)
        self.assertFalse(self.check().allowed)
        self.assertFalse(self.r.is_killed())
        self.r.update(9_650, 86_400_000 + 5)  # next UTC day resets
        self.assertTrue(self.check(now=86_400_000 + 5, data=86_400_000 + 5).allowed)

    def test_stale_data_and_wide_spread(self):
        self.assertFalse(self.check(now=10**6, data=0).allowed)
        self.assertFalse(self.check(spread=50).allowed)

    def test_flip_counts_as_new_risk(self):
        self.r.kill("x")
        self.assertFalse(self.check(side="sell", qty=0.05, pos=0.02).allowed)

    def test_guarded_broker_is_the_only_path(self):
        self.r.kill("x")
        g = GuardedBroker(PaperBroker(cash=10_000), self.r)
        v, f = g.submit(1, "X", "buy", 0.01, 80_000, 10_000, 1, 2.0, "t")
        self.assertIsNone(f)


class TestBrokerAndBacktest(unittest.TestCase):
    def test_costs(self):
        b = PaperBroker(cash=10_000, fee_bps=10, slippage_bps=0)
        b.market(0, "X", "buy", 1, 100, "e")
        b.market(1, "X", "sell", 1, 100, "x")
        self.assertAlmostEqual(b.cash, 10_000 - 0.2)
        self.assertAlmostEqual(round_trips(b.fills)[0], -0.2)

    def test_backtest_runs_and_respects_limits(self):
        tmp = Path(tempfile.mkdtemp())
        risk = RiskManager(RiskLimits(), tmp / "eKILL")
        res = run_backtest(synthetic_candles(1500), EscalatingEngine(RuleEngine(), None, risk), tmp)
        self.assertLess(res.max_drawdown, 0.20)  # 15% kill + one bar of gap risk
        self.assertGreater(res.trades, 0)
        report = run_review(tmp)
        self.assertIn("Brier", report)
        self.assertTrue((tmp / "calibration.json").exists())


class TestEngines(unittest.TestCase):
    def test_engine_error_is_failsafe(self):
        class Boom:
            name = "boom"

            def decide(self, s):
                raise TimeoutError("slow")

        d, _ = safe_decide(Boom(), None)
        self.assertEqual(d.source, "failsafe")

    def test_claude_engine_parses_and_handles_refusal(self):
        import json
        from types import SimpleNamespace as NS
        from tradebot.engines import ClaudeEngine

        c = synthetic_candles(120)
        snap = StateEngine().snapshot("X", c[-1].close_ts, c, book_at(c[-1].close_ts, c[-1].close), pf())
        sent = {}

        def create(**kw):
            sent.update(kw)
            return NS(stop_reason="end_turn", content=[NS(type="text", text=json.dumps(GOOD))])

        e = ClaudeEngine.__new__(ClaudeEngine)
        e.client = NS(messages=NS(create=create))
        e.model, e.effort, e.server_fallback, e.name, e.system = "claude-haiku-5-5", "low", False, "claude:test", "sys"
        d = e.decide(snap)
        self.assertEqual((d.direction, d.source), ("long", "claude:test"))
        self.assertEqual(sent["output_config"]["format"]["type"], "json_schema")
        e.client = NS(messages=NS(create=lambda **kw: NS(stop_reason="refusal", content=[])))
        self.assertEqual(e.decide(snap).source, "failsafe")


class TestReview(unittest.TestCase):
    def test_brier(self):
        self.assertAlmostEqual(brier([1.0, 0.0], [1, 0]), 0.0)
        self.assertAlmostEqual(brier([0.5, 0.5], [1, 0]), 0.25)

    def test_pav_monotone(self):
        fit = pav([0.1, 0.2, 0.3, 0.4, 0.5, 0.6], [0, 1, 0, 0, 1, 1])
        ys = [y for _, y in fit]
        self.assertEqual(ys, sorted(ys))


if __name__ == "__main__":
    unittest.main()
