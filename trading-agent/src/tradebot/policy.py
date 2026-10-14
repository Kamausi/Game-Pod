"""Policy: turns a validated Decision into an Intent.

Every threshold and every size lives here, in code. The model's numbers are
inputs; they never bypass a gate.
"""
from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

from .portfolio import drawdown_scale, vol_target_cap
from .schema import Decision
from .state import Snapshot


@dataclass(frozen=True)
class PolicyParams:
    min_setup_quality: int = 2
    min_confidence: float = 0.80
    kelly_multiplier: float = 0.25  # fractional Kelly; hard-capped at 0.25 below
    reward_risk: float = 2.0  # take-profit distance / stop distance
    stop_atr_mult: float = 1.5
    min_stop_pct: float = 0.004
    round_trip_cost_pct: float = 0.0016  # fees + slippage both ways, as a fraction
    allow_short: bool = False
    vol_target_daily: float = 0.01  # cap a position's daily volatility at 1% of equity
    bars_per_day: float = 24.0  # 1h bars

    def __post_init__(self):
        if not 0 < self.kelly_multiplier <= 0.25:
            raise ValueError("kelly_multiplier must be in (0, 0.25] (quarter-Kelly cap)")
        if self.min_confidence < 0.5:
            raise ValueError("min_confidence below 0.5 is not a gate")


@dataclass(frozen=True)
class Intent:
    action: str  # "enter" | "exit" | "hold"
    side: str | None = None  # "long" | "short"
    size_frac: float = 0.0  # position notional / equity, before risk clamps
    stop_pct: float = 0.0
    take_pct: float = 0.0
    reason: str = ""


@dataclass
class Calibrator:
    """Maps the model's raw probability to an empirically calibrated one.

    Until enough outcomes exist it shrinks toward 0.5, so an uncalibrated
    model can never size itself up on self-reported confidence alone.
    Points are (raw, calibrated) pairs, fitted nightly by `review.py`.
    """

    points: list[tuple[float, float]] = field(default_factory=list)
    n_samples: int = 0
    min_samples: int = 200
    prior_shrink: float = 0.5

    def __call__(self, p: float) -> float:
        if self.n_samples < self.min_samples or len(self.points) < 2:
            return 0.5 + self.prior_shrink * (p - 0.5)
        pts = sorted(self.points)
        if p <= pts[0][0]:
            return pts[0][1]
        if p >= pts[-1][0]:
            return pts[-1][1]
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            if x0 <= p <= x1:
                return y0 if x1 == x0 else y0 + (y1 - y0) * (p - x0) / (x1 - x0)
        return 0.5  # unreachable

    def save(self, path: str | Path) -> None:
        Path(path).write_text(
            json.dumps({"points": self.points, "n_samples": self.n_samples}, indent=2)
        )

    @classmethod
    def load(cls, path: str | Path) -> "Calibrator":
        p = Path(path)
        if not p.exists():
            return cls()
        d = json.loads(p.read_text())
        return cls(points=[tuple(x) for x in d["points"]], n_samples=d["n_samples"])


def kelly_fraction(p: float, b: float) -> float:
    """Full-Kelly fraction of bankroll to *risk* on a bet paying b:1."""
    if b <= 0:
        return 0.0
    return max(0.0, p - (1 - p) / b)


def decide(
    decision: Decision,
    snap: Snapshot,
    position_side: str | None,
    params: PolicyParams,
    calibrate: Calibrator,
) -> Intent:
    if decision.source == "failsafe":
        # Engine failed: open no new risk, keep existing stops working.
        return Intent("hold", reason=f"failsafe: {decision.rationale}")

    if position_side is not None:
        if decision.regime == "crisis" or decision.risk_state == "reduce":
            return Intent("exit", reason=f"model flagged {decision.regime}/{decision.risk_state}")
        opposite = "short" if position_side == "long" else "long"
        if decision.direction == opposite and decision.direction_confidence >= params.min_confidence:
            return Intent("exit", reason="confident opposite signal")
        return Intent("hold", reason="in position; stops manage exit")

    gates = [
        (not decision.toxic_flow, "toxic flow"),
        (decision.direction != "neutral", "neutral"),
        (decision.regime != "crisis", "crisis regime"),
        (decision.direction_confidence > params.min_confidence, "confidence below gate"),
        (decision.setup_quality >= params.min_setup_quality, "setup quality below gate"),
        (decision.risk_state == "safe", f"model risk_state={decision.risk_state}"),
        (decision.direction == "long" or params.allow_short, "shorts disabled"),
    ]
    for ok, why in gates:
        if not ok:
            return Intent("hold", reason=why)

    stop_pct = max(params.min_stop_pct, params.stop_atr_mult * snap.atr_pct)
    take_pct = params.reward_risk * stop_pct
    cost = params.round_trip_cost_pct + snap.spread_bps / 1e4
    win, loss = take_pct - cost, stop_pct + cost
    if win <= 0:
        return Intent("hold", reason="costs exceed target")
    p = calibrate(decision.p_win)
    f_star = kelly_fraction(p, win / loss)
    if f_star <= 0:
        return Intent("hold", reason=f"no Kelly edge at calibrated p={p:.3f}")
    # Kelly says what fraction of equity to LOSE if stopped; convert to notional.
    kelly_size = params.kelly_multiplier * f_star / loss
    vol_cap = vol_target_cap(snap.rv_24, params.bars_per_day, params.vol_target_daily)
    dd = drawdown_scale(snap.drawdown)
    if dd <= 0:
        return Intent("hold", reason=f"drawdown protection: entries paused at {snap.drawdown:.1%} drawdown")
    size = min(kelly_size, vol_cap) * dd
    return Intent(
        "enter",
        side=decision.direction,
        size_frac=size,
        stop_pct=stop_pct,
        take_pct=take_pct,
        reason=f"p_raw={decision.p_win:.2f} p_cal={p:.3f} kelly={f_star:.3f} kelly_size={kelly_size:.3f} vol_cap={vol_cap:.3f} dd_scale={dd:.2f}",
    )
