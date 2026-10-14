"""A second, smaller check between the decision and the exchange.

Every proposed ENTRY gets a risk score in [0, 1] before it is sent. Exits are never
reviewed: getting out is always allowed.

  score = deterministic score (code, always on)
          averaged with an optional second model's probability that the order is
          stopped out or liquidated (JevOrderRisk, or ClaudeOrderRisk on Haiku 5.5)

An order is FLAGGED when its score is in the riskiest 10% of the scores seen so far
(after a 50-order warm-up), or above a hard ceiling. In "block" mode flagged entries
are not sent; in "shadow" mode they are sent and only logged. Either way the nightly
review checks whether flagged entries really did stop out more often than the rest,
which is the only evidence that the reviewer earns its place.

Source of the idea: a study of 3,505 live-money agents found that flagging just the
riskiest 10% of orders caught 71 of 205 liquidations.
"""
from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

ModelRisk = Callable[[dict], float]


def clip01(x: float) -> float:
    return max(0.0, min(1.0, x))


def deterministic_risk(o: dict) -> tuple[float, dict]:
    """Each part is in [0, 1]; weights sum to 1. Higher = more likely to be stopped out."""
    parts = {
        "stop_inside_noise": clip01(o["atr_pct"] / max(o["stop_pct"], 1e-9) / 2),  # stop within ~1 ATR of noise -> 0.5+
        "vol_shock": clip01((o["rv_ratio"] - 1) / 2),
        "spread": clip01(o["spread_bps"] / 25),
        "low_confidence": clip01((1 - o["direction_confidence"]) / 0.5),
        "unstable_regime": 1.0 if o["regime"] in ("high_vol", "crisis") else 0.0,
        "in_drawdown": clip01(o["drawdown"] / 0.10),
    }
    w = {"stop_inside_noise": 0.30, "vol_shock": 0.20, "spread": 0.10, "low_confidence": 0.15, "unstable_regime": 0.15, "in_drawdown": 0.10}
    return sum(w[k] * v for k, v in parts.items()), parts


@dataclass
class ReviewVerdict:
    score: float
    flagged: bool
    blocked: bool
    threshold: float
    parts: dict
    model_score: float | None = None
    model_error: str | None = None


@dataclass
class OrderReviewer:
    mode: str = "block"  # "block" | "shadow" | "off"
    top_fraction: float = 0.10
    warmup: int = 50
    hard_ceiling: float = 0.80
    model: ModelRisk | None = None
    path: Path | None = None  # persist score history across restarts
    history: list[float] = field(default_factory=list)

    def __post_init__(self):
        if self.mode not in ("block", "shadow", "off"):
            raise ValueError("mode must be block, shadow or off")
        if self.path and Path(self.path).exists():
            self.history = json.loads(Path(self.path).read_text())[-2000:]

    def threshold(self) -> float:
        if len(self.history) < max(self.warmup, 1):
            return self.hard_ceiling
        xs = sorted(self.history[-500:])
        q = xs[min(len(xs) - 1, int(len(xs) * (1 - self.top_fraction)))]
        return min(q, self.hard_ceiling)

    def review(self, order: dict) -> ReviewVerdict:
        det, parts = deterministic_risk(order)
        m, err = None, None
        if self.model is not None:
            try:
                m = clip01(float(self.model(order)))
            except Exception as e:  # the second model failing never blocks or passes on its own
                err = f"{type(e).__name__}: {e}"[:200]
        s = det if m is None else (det + m) / 2
        thr = self.threshold()
        flagged = s >= thr
        self.history.append(s)
        if self.path:
            Path(self.path).parent.mkdir(parents=True, exist_ok=True)
            Path(self.path).write_text(json.dumps(self.history[-2000:]))
        return ReviewVerdict(round(s, 4), flagged, flagged and self.mode == "block", round(thr, 4), parts, m, err)


class ClaudeOrderRisk:
    """Claude Haiku 5.5 as the small reviewing model. Requires `pip install anthropic`."""

    SCHEMA = {"type": "object", "properties": {"p_stopped_out": {"type": "number"}}, "required": ["p_stopped_out"],
              "additionalProperties": False}
    SYSTEM = ("You review one proposed trade entry before it is sent. Estimate the probability that it hits its stop "
              "(or is liquidated) before its target, from the numbers given. Return only the JSON.")

    def __init__(self, model: str = "claude-haiku-5-5"):
        import anthropic

        self.client = anthropic.Anthropic(timeout=10, max_retries=1)
        self.model = model
        self.name = f"claude:{model}"

    def __call__(self, order: dict) -> float:
        resp = self.client.messages.create(
            model=self.model, max_tokens=256,
            system=[{"type": "text", "text": self.SYSTEM, "cache_control": {"type": "ephemeral"}}],
            messages=[{"role": "user", "content": json.dumps(order, sort_keys=True)}],
            output_config={"effort": "low", "format": {"type": "json_schema", "schema": self.SCHEMA}},
        )
        if resp.stop_reason == "refusal":
            raise RuntimeError("reviewer refused")
        return float(json.loads(next(b.text for b in resp.content if b.type == "text"))["p_stopped_out"])


def build_reviewer(kind: str, path: Path | None = None, mode: str = "block") -> OrderReviewer | None:
    if kind == "off":
        return None
    model = None
    if kind == "jev":
        from .jev import JevOrderRisk

        model = JevOrderRisk()
    elif kind == "claude":
        model = ClaudeOrderRisk()
    elif kind != "code":
        raise ValueError("reviewer must be off, code, jev or claude")
    return OrderReviewer(mode=mode, model=model, path=path)
