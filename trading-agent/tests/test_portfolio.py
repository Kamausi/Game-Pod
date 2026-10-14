import math
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

from tradebot import indicators as ind
from tradebot.backtest import run_backtest, synthetic_candles
from tradebot.engines import EnsembleEngine, EscalatingEngine
from tradebot.policy import Calibrator, PolicyParams, decide
from tradebot.portfolio import (cluster_exposure, correlation_matrix, drawdown_scale, performance_scale,
                                stress_losses, vol_target_cap)
from tradebot.risk import RiskLimits, RiskManager
from tradebot.schema import Decision
from tradebot.state import PortfolioView, StateEngine
from tradebot.types import OrderBook


class Indicators(unittest.TestCase):
    def test_rsi_bounds_and_extremes(self):
        self.assertEqual(ind.rsi(list(range(1, 30))), 100.0)
        self.assertEqual(ind.rsi(list(range(30, 1, -1))), 0.0)
        self.assertEqual(ind.rsi([5.0] * 20), 50.0)

    def test_pct_b_and_resample(self):
        self.assertAlmostEqual(ind.bollinger_pct_b([1.0] * 19 + [1.0]), 0.5)
        c = synthetic_candles(40)
        r = ind.resample(c, 4)
        self.assertEqual(len(r), 10)
        self.assertEqual(r[0].close, c[3].close)
        self.assertEqual(r[0].high, max(x.high for x in c[:4]))

    def test_snapshot_indicators_causal(self):
        c = synthetic_candles(160)
        t = c[119].close_ts
        book = OrderBook(t, ((c[119].close - 1, 1.0),), ((c[119].close + 1, 1.0),))
        pf = PortfolioView(0, 0, 1e4, 1e4, 1e4)
        a = StateEngine().snapshot("X", t, c[:120], book, pf)
        b = StateEngine().snapshot("X", t, c[:120], book, pf)
        self.assertEqual(a, b)
        self.assertTrue(0 <= a.rsi_14 <= 100)
        self.assertLess(len(a.to_prompt()) / 4, 400)


class Sizing(unittest.TestCase):
    def test_vol_target(self):
        # 1% per-bar vol on hourly bars = ~4.9% daily; a 1% daily target allows ~20% of equity
        self.assertAlmostEqual(vol_target_cap(0.01, 24, 0.01), 0.01 / (0.01 * math.sqrt(24)))

    def test_drawdown_and_performance_scaling(self):
        self.assertEqual(drawdown_scale(0.0), 1.0)
        self.assertAlmostEqual(drawdown_scale(0.05), 0.5)
        self.assertEqual(drawdown_scale(0.09), 0.25)
        self.assertEqual(drawdown_scale(0.10), 0.0)
        self.assertEqual(performance_scale([-1] * 10), 0.5)
        self.assertEqual(performance_scale([1] * 10), 1.0)

    def test_policy_pauses_entries_in_drawdown(self):
        c = synthetic_candles(120)
        t = c[-1].close_ts
        book = OrderBook(t, ((c[-1].close - 1, 1.0),), ((c[-1].close + 1, 1.0),))
        snap = StateEngine().snapshot("X", t, c, book, PortfolioView(0, 0, 8_900, 10_000, 10_000))
        d = Decision.parse(dict(regime="trending", direction="long", direction_confidence=0.9, p_win=0.7,
                                toxic_flow=False, setup_quality=3, risk_state="safe"))
        i = decide(d, snap, None, PolicyParams(), Calibrator(points=[(0, 0), (1, 1)], n_samples=10_000))
        self.assertEqual(i.action, "hold")
        self.assertIn("drawdown", i.reason)


class Portfolio(unittest.TestCase):
    def test_correlation_and_cluster_cap(self):
        base = [math.sin(i / 3) for i in range(60)]
        corr = correlation_matrix({"BTC": base, "ETH": [2 * x for x in base], "X": [math.cos(i * 1.7) for i in range(60)]})
        self.assertAlmostEqual(corr["BTC"]["ETH"], 1.0, places=6)
        self.assertLess(abs(corr["BTC"]["X"]), 0.7)
        # long ETH 20% + long BTC 25% with rho=1 -> 45% cluster; uncorrelated X doesn't count
        self.assertAlmostEqual(cluster_exposure("ETH", 0.20, {"BTC": 0.25, "X": 0.3}, corr), 0.45)
        # opposite side doesn't stack
        self.assertAlmostEqual(cluster_exposure("ETH", -0.20, {"BTC": 0.25}, corr), 0.20)

    def test_stress(self):
        s = stress_losses({"BTC": 0.25, "DOGE": 0.05}, {"BTC": "major", "DOGE": "high_vol"}, {"BTC": 0.03, "DOGE": 0.08}, {"BTC": 1, "DOGE": 3})
        self.assertAlmostEqual(s["crypto_crash"], 0.25 * 0.20 + 0.05 * 0.40)
        self.assertTrue(all(v >= 0 for v in s.values()))


class Ensemble(unittest.TestCase):
    def test_ensemble_backtest_runs_and_learns(self):
        tmp = Path(tempfile.mkdtemp())
        eng = EnsembleEngine()
        res = run_backtest(synthetic_candles(1500, seed=5), EscalatingEngine(eng, None, RiskManager(RiskLimits(), tmp / "K")), tmp)
        self.assertLess(res.max_drawdown, 0.20)
        self.assertGreater(sum(n for n, _ in eng.stats.values()), 100)  # signals were scored
        self.assertGreater(len(eng.scored), 10)  # ensemble calls were scored
        ws = [eng.weight(r, k) for (r, k) in eng.stats]
        self.assertTrue(all(0.1 <= w <= 3.0 for w in ws))


if __name__ == "__main__":
    unittest.main()
