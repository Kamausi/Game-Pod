import json
import os
import tempfile
import unittest
from pathlib import Path

from tradebot.backtest import synthetic_candles
from tradebot.evidence import build_package, compare_external
from tradebot.experiments import DataSnapshot, FrozenError, Register, Spec
from tradebot.ops_gate import run_gate
from tradebot.release import ReleaseError, approve, init_limits, verify_release
from tradebot.validation import deflated_sharpe, expected_max_sharpe, final_evaluation, run_spec, sweep


class Workflow(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp())
        cls.cwd = os.getcwd()
        os.chdir(cls.tmp)
        cls.reg = Register(cls.tmp / "runs/research")
        cls.candles = synthetic_candles(1400, seed=3)
        cls.snap = DataSnapshot.of(cls.candles, "synthetic", "BTC_USD", "1h")
        cls.reg.save_data(cls.candles, cls.snap)
        cls.recs = sweep(cls.reg, Spec(name="fam"), {"reward_risk": [1.5, 2.5]}, cls.candles, cls.snap, 0, 800, cls.tmp / "w")

    @classmethod
    def tearDownClass(cls):
        os.chdir(cls.cwd)

    def test_every_run_recorded(self):
        self.assertEqual(len(self.reg.family_trials("fam", self.snap.sha256)), 2)

    def test_research_cannot_loosen_limits(self):
        with self.assertRaises(ValueError):
            run_spec(Spec(name="x", limits={"max_position_frac": 0.5}), self.candles[:300], self.tmp / "l")

    def test_freeze_final_once_and_no_overlap(self):
        spec = Spec(**self.recs[0]["spec"])
        reg = Register(self.tmp / "runs/r2")
        reg.append({**self.recs[0]})  # copy exploration record
        with self.assertRaises(FrozenError):
            final_evaluation(reg, spec.id, self.candles, self.snap, 800, 1400, self.tmp / "w")  # not frozen
        reg.freeze(spec, "test")
        with self.assertRaises(ValueError):
            final_evaluation(reg, spec.id, self.candles, self.snap, 700, 1400, self.tmp / "w")  # overlaps train
        rec = final_evaluation(reg, spec.id, self.candles, self.snap, 800, 1400, self.tmp / "w")
        self.assertEqual(rec["status"], "ok")
        with self.assertRaises(FrozenError):
            final_evaluation(reg, spec.id, self.candles, self.snap, 800, 1400, self.tmp / "w")
        self.assertNotEqual(Spec(name="fam", params={"reward_risk": 1.6}).id, spec.id)

    def test_dsr_penalises_more_trials(self):
        self.assertGreater(expected_max_sharpe(100, 0.01), expected_max_sharpe(5, 0.01))
        a = deflated_sharpe(0.05, 1000, 0, 3, 2, 0.001)
        b = deflated_sharpe(0.05, 1000, 0, 3, 200, 0.001)
        self.assertGreater(a, b)

    def test_evidence_reconciles(self):
        out = build_package(self.reg, self.recs[0]["run_id"])
        pkg = json.loads((out / "evidence.json").read_text())
        self.assertTrue(pkg["reconciliation"]["reconciled"])
        self.assertEqual(len(pkg["all_variations_tried"]), 2)
        bad = compare_external({"total_return": 0.1, "trades": 3, "fills": []}, {"total_return": 0.2, "trades": 3})
        self.assertFalse(bad["reconciled"])

    def test_release_gate(self):
        reg = Register(self.tmp / "runs/r3")
        spec = Spec(**self.recs[1]["spec"])
        reg.append({**self.recs[1]})
        reg.save_data(self.candles, self.snap)
        reg.freeze(spec, "t")
        with self.assertRaises(ReleaseError):
            approve(reg, spec.id, "op", confirm=lambda c: True)  # no final yet
        fin = final_evaluation(reg, spec.id, self.candles, self.snap, 800, 1400, self.tmp / "w")
        build_package(reg, fin["run_id"])
        run_gate(reg)
        lp = init_limits(self.tmp / "config/risk_limits.json")
        rp = self.tmp / "releases/approved.json"
        with self.assertRaises(ReleaseError):
            approve(reg, spec.id, "op", lp, rp, confirm=lambda c: False, accept_weak_evidence=True)
        approve(reg, spec.id, "op", lp, rp, confirm=lambda c: True, accept_weak_evidence=True)
        s, limits, rel = verify_release(rp)
        self.assertEqual(s.id, spec.id)
        lp.write_text(lp.read_text().replace('"max_daily_loss": 0.03', '"max_daily_loss": 0.02'))
        with self.assertRaises(ReleaseError):
            verify_release(rp)  # limits changed after approval


class OpsGate(unittest.TestCase):
    def test_gate_passes(self):
        g = run_gate()
        self.assertTrue(g["passed"], g["scenarios"])


if __name__ == "__main__":
    unittest.main()
