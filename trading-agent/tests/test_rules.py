"""The seven survival rules, the Jev adapter, the news overlay and the run log."""
import json
import os
import tempfile
import unittest
from dataclasses import asdict, replace
from pathlib import Path

from tradebot.backtest import run_backtest, synthetic_candles
from tradebot.engines import EnsembleEngine, EscalatingEngine, RuleEngine
from tradebot.experiments import DataSnapshot, Register, Spec, _h
from tradebot.journal import read_journal
from tradebot.policy import Calibrator, PolicyParams, decide
from tradebot.portfolio import vol_regime_scale
from tradebot.risk import RiskLimits, RiskManager
from tradebot.schema import Decision
from tradebot.state import PortfolioView, StateEngine
from tradebot.types import OrderBook


def snap_of(candles, inv=0.0):
    c = candles[-1]
    book = OrderBook(c.close_ts, ((c.close * 0.9999, 1.0),), ((c.close * 1.0001, 1.0),))
    return StateEngine().snapshot("BTC_USD", c.close_ts, candles, book, PortfolioView(inv, c.close, 10_000, 10_000, 10_000))


GOOD = Decision("trending", "long", 0.9, 0.7, False, 3, "safe", "", "test")
CAL = Calibrator(points=[(0, 0), (1, 1)], n_samples=10_000)


class Rule1FixedExits(unittest.TestCase):
    def test_bracket_is_fixed_and_model_cannot_exit(self):
        s = snap_of(synthetic_candles(200))
        i = decide(GOOD, s, None, PolicyParams(vol_target_daily=1.0), CAL)
        self.assertEqual((i.action, i.stop_pct, i.take_pct), ("enter", 0.02, 0.04))
        crisis = replace(GOOD, regime="crisis", direction="short")
        self.assertEqual(decide(crisis, s, "long", PolicyParams(), CAL).action, "hold")
        self.assertEqual(decide(crisis, s, "long", PolicyParams(model_exits=True), CAL).action, "exit")
        atr = decide(GOOD, s, None, PolicyParams(exit_mode="atr", vol_target_daily=1.0), CAL)
        self.assertAlmostEqual(atr.take_pct, 2 * atr.stop_pct)
        with self.assertRaises(ValueError):
            PolicyParams(fixed_stop_pct=0.05, fixed_take_pct=0.04)


class Rule2VolScaledSize(unittest.TestCase):
    def test_wilder_market_smaller_size(self):
        self.assertEqual(vol_regime_scale(0.8), 1.0)
        self.assertEqual(vol_regime_scale(2.0), 0.5)
        self.assertEqual(vol_regime_scale(10.0), 0.25)
        s = snap_of(synthetic_candles(200))
        calm = decide(GOOD, replace(s, rv_ratio=1.0), None, PolicyParams(vol_target_daily=1.0), CAL)
        wild = decide(GOOD, replace(s, rv_ratio=2.0), None, PolicyParams(vol_target_daily=1.0), CAL)
        self.assertAlmostEqual(wild.size_frac, calm.size_frac / 2)


class Rule4Ablation(unittest.TestCase):
    def test_disable_and_impact(self):
        c = synthetic_candles(260, seed=5)
        e = EnsembleEngine(disabled=("flow",))
        for k in range(60, 260):
            e.decide(snap_of(c[: k + 1]))
        rep = e.impact_report()
        self.assertNotIn("flow", rep)  # a disabled signal is never active
        self.assertTrue(all(0 <= v["changed_pct"] <= 1 for v in rep.values()))
        self.assertEqual(e.last_trace["disabled"], ["flow"])

    def test_ablate_records_every_run(self):
        from tradebot.validation import ablate

        tmp = Path(tempfile.mkdtemp())
        reg = Register(tmp / "r")
        c = synthetic_candles(500, seed=9)
        snap = DataSnapshot.of(c, "synthetic", "BTC_USD", "1h")
        out = ablate(reg, Spec(name="abl", engine="ensemble"), c, snap, 0, 500, tmp / "w", signals=("flow", "breakout"))
        self.assertEqual(set(out["signals"]), {"flow", "breakout"})
        self.assertEqual(len([r for r in reg.records() if r.get("kind") == "explore"]), 3)
        for v in out["signals"].values():
            self.assertIn(v["verdict"], ("does nothing: delete", "hurts: removing it improved return", "keep"))

    def test_old_candidate_ids_unchanged(self):
        sp = Spec(name="x", params={"reward_risk": 2})
        d = asdict(sp)
        del d["engine_opts"]
        self.assertEqual(sp.id, "cand_" + _h(d)[:12])
        self.assertNotEqual(replace(sp, engine_opts={"fee_mult": 2}).id, sp.id)


class Rule5Reviewer(unittest.TestCase):
    def order(self, **kw):
        o = {"side": "long", "size_frac": 0.1, "stop_pct": 0.02, "take_pct": 0.04, "atr_pct": 0.005, "rv_ratio": 1.0, "rv_24": 0.003,
             "spread_bps": 2, "direction_confidence": 0.9, "p_win": 0.6, "regime": "trending", "drawdown": 0.0, "mid": 100}
        o.update(kw)
        return o

    def test_flags_top_decile_after_warmup(self):
        from tradebot.order_review import OrderReviewer

        r = OrderReviewer(warmup=50)
        self.assertFalse(r.review(self.order(rv_ratio=3.0, regime="high_vol")).flagged)  # warm-up: hard ceiling only
        for k in range(100):
            r.review(self.order(rv_ratio=1 + (k % 10) / 10))
        v = r.review(self.order(rv_ratio=3.0, regime="high_vol", atr_pct=0.03, direction_confidence=0.55))
        self.assertTrue(v.flagged and v.blocked)
        self.assertFalse(r.review(self.order()).flagged)
        sh = OrderReviewer(mode="shadow", warmup=0, hard_ceiling=0.0)
        v = sh.review(self.order())
        self.assertTrue(v.flagged and not v.blocked)

    def test_second_model_failure_is_neutral(self):
        from tradebot.order_review import OrderReviewer

        def boom(o):
            raise TimeoutError("slow")

        v = OrderReviewer(model=boom).review(self.order())
        self.assertIsNone(v.model_score)
        self.assertIn("TimeoutError", v.model_error)
        v2 = OrderReviewer(model=lambda o: 1.0).review(self.order())
        self.assertGreater(v2.score, v.score)

    def test_agent_blocks_and_journals(self):
        from tradebot.order_review import OrderReviewer

        tmp = Path(tempfile.mkdtemp())
        rev = OrderReviewer(warmup=0, hard_ceiling=0.0)  # flags everything
        res = run_backtest(synthetic_candles(600, seed=4), EscalatingEngine(EnsembleEngine(), None,
                           RiskManager(RiskLimits(), tmp / "K")), tmp, reviewer=rev)
        recs = read_journal(tmp / "journal.jsonl")
        reviews = [r for r in recs if r["kind"] == "review"]
        self.assertEqual(res.trades, 0)
        self.assertTrue(all(r["verdict"]["blocked"] for r in reviews))
        if reviews:
            from tradebot.review import reviewer_effect

            self.assertEqual(reviewer_effect(recs)["blocked"], len(reviews))


class Rule6And7TraceReplay(unittest.TestCase):
    def test_trace_saved_and_replay(self):
        from tradebot.replay import cost_of, replay

        tmp = Path(tempfile.mkdtemp())
        run_backtest(synthetic_candles(400, seed=2), EscalatingEngine(EnsembleEngine(), None, RiskManager(RiskLimits(), tmp / "K")), tmp)
        decs = [r for r in read_journal(tmp / "journal.jsonl") if r["kind"] == "decision"]
        self.assertTrue(all(r["trace"] and "signals" in r["trace"] for r in decs))
        same = replay(tmp, EnsembleEngine())
        self.assertEqual(same["direction_agreement"], 1.0)  # deterministic engine on identical inputs
        other = replay(tmp, RuleEngine())
        self.assertLessEqual(other["direction_agreement"], 1.0)
        t, usd = cost_of({"engine": "claude:x", "model_served": "claude-opus-5-5",
                          "usage": {"input_tokens": 1_000_000, "output_tokens": 100_000, "cache_read_input_tokens": 0}})
        self.assertAlmostEqual(usd, 4.0 + 2.0)
        _, cheap = cost_of({"engine": "jev", "model_served": "jev-1.13.0", "usage": {"input_tokens": 1_000_000, "output_tokens": 5}})
        self.assertAlmostEqual(cheap, 0.042)


def fake_jev(answers_for, calls=None, statuses=None):
    statuses = list(statuses or [])

    def t(url, headers, body, timeout):
        if calls is not None:
            calls.append(body)
        if statuses:
            st = statuses.pop(0)
            if st != 200:
                return st, {"error": st}
        return 200, {"model": "jev-1.13.0", "answers": answers_for(body), "usage": {"input_tokens": 300, "output_tokens": 20}}

    return t


def engine_answers(body):
    return {"regime": {"type": "choice", "choice": "trending", "probabilities": {"trending": 0.7, "mean_reverting": 0.1, "high_vol": 0.1,
                                                                                    "crisis": 0.1}, "confidence": 0.6},
            "direction": {"type": "choice", "choice": "long", "probabilities": {"long": 0.83, "short": 0.07, "neutral": 0.1}, "confidence": 0.7},
            "up_first": {"type": "noul", "noul": 0.61}, "down_first": {"type": "noul", "noul": 0.2},
            "toxic_flow": {"type": "noul", "noul": 0.1},
            "setup_quality": {"type": "score", "score": 2.4, "legend": {}, "probabilities": {}, "confidence": 0.8}}


class Jev(unittest.TestCase):
    def test_engine_maps_typed_answers(self):
        from tradebot.jev import JevClient, JevEngine

        calls = []
        e = JevEngine(JevClient("k", "jev-1.13.0", transport=fake_jev(engine_answers, calls)))
        d = e.decide(snap_of(synthetic_candles(200)))
        self.assertEqual((d.regime, d.direction, d.setup_quality, d.toxic_flow), ("trending", "long", 2, False))
        self.assertAlmostEqual(d.direction_confidence, 0.83)
        self.assertAlmostEqual(d.p_win, 0.61)
        q = calls[0]["questions"]
        self.assertEqual(q["regime"]["type"], "choice")
        self.assertEqual(q["up_first"]["type"], "noul")
        self.assertEqual(q["setup_quality"]["type"], "score")
        self.assertEqual(calls[0]["model"], "jev-1.13.0")
        self.assertEqual(e.last_trace["model_served"], "jev-1.13.0")  # what served it, logged
        self.assertIn("bracket", e.last_trace["state"])

    def test_errors(self):
        from tradebot.jev import JevClient, JevError

        calls = []
        c = JevClient("k", transport=fake_jev(engine_answers, calls, [422]), sleep=lambda s: None)
        with self.assertRaises(JevError):
            c.ask({}, {"a": {"type": "noul", "instructions": "x"}})
        self.assertEqual(len(calls), 1)  # a 422 is never retried
        calls.clear()
        c = JevClient("k", transport=fake_jev(lambda b: {"a": {"type": "noul", "noul": 0.5}}, calls, [429, 200]), sleep=lambda s: None)
        self.assertEqual(c.ask({}, {"a": {"type": "noul", "instructions": "x"}})["answers"]["a"]["noul"], 0.5)
        self.assertEqual(len(calls), 2)
        with self.assertRaises(ValueError):
            JevClient("")

    def test_engine_error_becomes_hold(self):
        from tradebot.engines import safe_decide
        from tradebot.jev import JevClient, JevEngine

        e = JevEngine(JevClient("k", transport=fake_jev(engine_answers, statuses=[401])))
        d, _ = safe_decide(e, snap_of(synthetic_candles(200)))
        self.assertEqual(d.source, "failsafe")

    def test_headline_scorer(self):
        from tradebot.jev import JevClient, JevHeadlineScorer

        ans = lambda b: {"asset": {"type": "choice", "choice": "BTC_USD", "probabilities": {}, "confidence": 0.9},
                         "sentiment": {"type": "score", "score": 0.5, "legend": {}, "probabilities": {}, "confidence": 0.9},
                         "material": {"type": "noul", "noul": 0.9}, "regulatory": {"type": "noul", "noul": 0.95}}
        r = JevHeadlineScorer(["BTC_USD", "ETH_USD"], JevClient("k", transport=fake_jev(ans))).classify("SEC sues exchange")
        self.assertEqual(r["asset"], "BTC_USD")
        self.assertAlmostEqual(r["sentiment"], -0.75)


class News(unittest.TestCase):
    def setUp(self):
        from tradebot.sentiment import SentimentStore

        self.tmp = Path(tempfile.mkdtemp())
        self.store = SentimentStore(self.tmp / "s.sqlite")

    def test_lexicon_classes(self):
        from tradebot.news import lexicon_classify

        r = lexicon_classify("SEC sues Binance over unregistered bitcoin trading")
        self.assertGreaterEqual(r["regulatory"], 0.6)
        self.assertGreaterEqual(r["material"], 0.6)
        self.assertLess(r["sentiment"], 0)
        self.assertLess(lexicon_classify("Why bitcoin could rally this week?")["material"], 0.5)

    def test_import_veto_and_bias(self):
        from tradebot.news import NewsOverlay, classify, import_csv

        c = synthetic_candles(200)
        t = c[-1].close_ts
        p = self.tmp / "h.csv"
        p.write_text("time,title,source\n"
                     f"{t - 3_600_000},SEC sues bitcoin exchange over fraud,wire\n"
                     f"{t - 60_000},Bitcoin ETF inflows surge to record,wire\n")
        rep = import_csv(self.store, p, seen_delay_ms=300_000)
        self.assertEqual(rep["new"], 2)
        classify(self.store)

        class Fixed:
            name, last_trace = "fixed", None

            def decide(self, s):
                return GOOD

        s = snap_of(c)
        d = NewsOverlay(Fixed(), self.store, "veto").decide(s)
        self.assertEqual(d.direction, "neutral")  # material regulatory headline seen 55 min ago
        self.assertIn("regulatory veto", d.rationale)
        self.assertEqual(NewsOverlay(Fixed(), self.store, "none").decide(s).direction, "long")
        # the positive headline was published 1 min before decision time: with the 5-min seen delay it is not visible yet
        early = replace(s, as_of=t - 3_600_000 + 60_000)
        self.assertEqual(NewsOverlay(Fixed(), self.store, "veto").decide(early).direction, "long")
        b = NewsOverlay(Fixed(), self.store, "bias").decide(s)
        self.assertNotEqual(b.direction_confidence, GOOD.direction_confidence)

    def test_news_ablation_runs_and_records(self):
        from tradebot.news import classify, import_csv, news_ablation

        c = synthetic_candles(500, seed=3, bar_ms=14_400_000)
        rows = ["time,title"] + [f"{c[k].close_ts},{'SEC sues bitcoin exchange' if k % 3 else 'Bitcoin adoption surges'}" for k in range(80, 480, 20)]
        (self.tmp / "h.csv").write_text("\n".join(rows) + "\n")
        import_csv(self.store, self.tmp / "h.csv")
        classify(self.store)
        reg = Register(self.tmp / "r")
        snap = DataSnapshot.of(c, "synthetic", "BTC_USD", "4h")
        out = news_ablation(reg, Spec(name="news", engine="ensemble", timeframe="4h", params={"bars_per_day": 6}), c, snap,
                            self.tmp / "s.sqlite", self.tmp / "w", fee_mults=(1.0, 2.0))
        self.assertEqual(len(out["table"]), 8)
        self.assertEqual(set(out["verdict"]), {"bias", "veto", "both"})
        self.assertEqual(len([r for r in reg.records() if r.get("kind") == "explore"]), 8)


class RunLog(unittest.TestCase):
    def test_append_and_review_writes_entry(self):
        from tradebot import runlog
        from tradebot.review import run_review

        tmp = Path(tempfile.mkdtemp())
        runlog.append(tmp, ["swept 9 configs"], ["froze rr=2"], ["holdout not run"], ["rr=3: overfit"])
        run_backtest(synthetic_candles(300), EscalatingEngine(RuleEngine(), None, RiskManager(RiskLimits(), tmp / "K")), tmp)
        run_review(tmp)
        text = runlog.recent(tmp)
        self.assertIn("Tried and rejected", text)
        self.assertIn("nightly review", text)
        self.assertEqual(text.count("## "), 2)


class MarketMaking(unittest.TestCase):
    def test_formula(self):
        import math

        from tradebot.marketmaking import MMParams, as_quotes

        p = MMParams(gamma=1e-4, kappa=0.5)
        r, s = as_quotes(0, 60.0, 60, p)
        self.assertEqual(r, 0)
        self.assertAlmostEqual(s, 1e-4 * 3600 * 60 + (2 / 1e-4) * math.log(1 + 1e-4 / 0.5))
        r_long, _ = as_quotes(2, 60.0, 60, p)
        self.assertLess(r_long, 0)  # long inventory leans quotes down to sell it off

    def test_simulation_is_honest(self):
        from tradebot.marketmaking import MMParams, simulate

        c = synthetic_candles(1500, seed=4, bar_ms=60_000)
        cheap = simulate(c, MMParams(maker_fee_bps=0.0))
        dear = simulate(c, MMParams(maker_fee_bps=5.0, quote_cost_bps=2.0))
        self.assertLessEqual(cheap["max_abs_inventory"], 5)
        self.assertGreater(cheap["fills"], 0)
        self.assertEqual(cheap["fills"], dear["fills"])  # same quotes, same fills: only costs differ
        self.assertLess(dear["pnl_usd"], cheap["pnl_usd"])
        h = dear["honesty"]
        self.assertAlmostEqual(h["net_edge_bps"], h["captured_bps"] - h["fees_and_quote_costs_bps"] - h["adverse_selection_bps"])


class PredictionMarkets(unittest.TestCase):
    # shapes copied from live responses on 2026-10-14
    POLY = {"slug": "fed-oct-no-change", "question": "Will there be no change in Fed interest rates after the October 2026 meeting?",
            "outcomes": "[\"Yes\", \"No\"]", "outcomePrices": "[\"0.705\", \"0.295\"]", "bestBid": 0.7, "bestAsk": 0.71,
            "acceptingOrders": True, "closed": False, "feesEnabled": True,
            "feeSchedule": {"exponent": 1, "rate": 0.05, "takerOnly": True, "rebateRate": 0.25}}
    KAL = {"market": {"ticker": "KXFED-26OCT-T0", "title": "Fed holds in October", "status": "active", "yes_bid_dollars": "0.6200",
                      "yes_ask_dollars": "0.6300", "no_ask_dollars": "0.3800", "no_bid_dollars": "0.3700"}}

    def get(self, url):
        return [self.POLY] if "gamma-api" in url else self.KAL

    def test_scan_finds_and_prices_the_gap(self):
        from tradebot.predmarkets import fee, scan

        rows = scan([{"polymarket_slug": "fed-oct-no-change", "kalshi_ticker": "KXFED-26OCT-T0"}], get=self.get)
        r = rows[0]
        self.assertIsNone(r["error"])
        b = r["best"]
        # YES on Kalshi at 0.63 + NO on Polymarket at 1 - 0.70 = 0.30 -> $0.93 for a $1 payoff
        self.assertEqual((b["buy_yes_on"], b["buy_no_on"]), ("kalshi", "polymarket"))
        self.assertAlmostEqual(b["gross_per_contract"], 0.07, places=6)
        self.assertAlmostEqual(b["fees_per_contract"], fee(0.63, 0.07) + fee(0.30, 0.05), places=9)
        self.assertTrue(b["locked"])
        self.assertFalse(r["polymarket_complete_set"]["locked"])  # 0.71 + 0.30 > 1

    def test_broken_pair_does_not_stop_scan(self):
        from tradebot.predmarkets import scan

        def get(url):
            if "bad" in url:
                raise OSError("404")
            return self.get(url)

        rows = scan([{"polymarket_slug": "bad", "kalshi_ticker": "X"}, {"polymarket_slug": "ok", "kalshi_ticker": "Y"}], get=get)
        self.assertIn("OSError", rows[0]["error"])
        self.assertIsNone(rows[1]["error"])


if __name__ == "__main__":
    unittest.main()
