"""Decision engines. Each maps a Snapshot to a validated Decision.

- RuleEngine: deterministic baseline. Runs offline, used for backtests and as
  the benchmark any model has to beat.
- ClaudeEngine: calls the Claude API with a strict JSON schema.
- EscalatingEngine: cheap fast model by default; escalates to a deep model
  when confidence is low or the regime reads as crisis.

Any engine error becomes Decision.hold(...) with source="failsafe".
"""
from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Callable, Protocol

from .risk import RiskManager
from .schema import JSON_SCHEMA, Decision, SchemaError
from .state import Snapshot

PROMPTS = Path(__file__).resolve().parents[2] / "prompts"


class DecisionEngine(Protocol):
    name: str

    def decide(self, snap: Snapshot) -> Decision: ...


class RuleEngine:
    """Trend-following baseline with a volatility filter. Not an edge claim."""

    name = "rules"

    def decide(self, s: Snapshot) -> Decision:
        if s.rv_ratio > 2.5 or abs(s.ret_1) > 6 * max(s.rv_24, 1e-9):
            regime = "crisis"
        elif s.rv_ratio > 1.6:
            regime = "high_vol"
        elif abs(s.z_20) > 1.0 and s.ret_24 * s.z_20 > 0:
            regime = "trending"
        else:
            regime = "mean_reverting"

        score = 0.0
        score += 1.0 if s.ret_24 > 0 else -1.0
        score += 1.0 if s.ret_6 > 0 else -1.0
        score += 1.0 if s.z_20 > 0.5 else (-1.0 if s.z_20 < -0.5 else 0.0)
        score += 0.5 if s.imbalance > 0.2 else (-0.5 if s.imbalance < -0.2 else 0.0)
        direction = "long" if score >= 2.5 else "short" if score <= -2.5 else "neutral"
        conf = min(0.95, 0.5 + abs(score) / 7)
        quality = 0
        if regime == "trending":
            quality += 2
        if 0.2 < s.range_pos_48 < 0.9 and direction == "long":
            quality += 1
        if 0.1 < s.range_pos_48 < 0.8 and direction == "short":
            quality += 1
        risk_state = "safe" if s.drawdown < 0.05 and s.day_pnl_pct > -0.015 else "near_limit"
        return Decision(
            regime=regime,
            direction=direction,
            direction_confidence=conf,
            p_win=0.5 if direction == "neutral" else 0.40 + 0.05 * min(quality, 3),
            toxic_flow=s.spread_bps > 15 or s.vol_ratio > 5,
            setup_quality=min(quality, 3),
            risk_state=risk_state,
            rationale=f"score={score:+.1f}",
            source=self.name,
        )


class ClaudeEngine:
    """Claude as a typed classifier. Requires `pip install anthropic`."""

    def __init__(self, model: str, effort: str, timeout_s: float, server_fallback: bool = False):
        import anthropic  # lazy: the rest of the package runs without it

        self.client = anthropic.Anthropic(timeout=timeout_s, max_retries=1)
        self.model = model
        self.effort = effort
        self.server_fallback = server_fallback
        self.name = f"claude:{model}"
        self.system = (PROMPTS / "reflex_system.md").read_text()

    def decide(self, snap: Snapshot) -> Decision:
        kwargs = dict(
            model=self.model,
            max_tokens=1024,
            system=self.system,
            messages=[{"role": "user", "content": snap.to_prompt()}],
            output_config={
                "effort": self.effort,
                "format": {"type": "json_schema", "schema": JSON_SCHEMA},
            },
        )
        if self.server_fallback:
            # Server-side refusal fallback (Claude API only).
            resp = self.client.beta.messages.create(
                betas=["server-side-fallback-2026-07-01"], fallbacks="default", **kwargs
            )
        else:
            resp = self.client.messages.create(**kwargs)
        if resp.stop_reason == "refusal":
            return Decision.hold("model refused", source="failsafe")
        text = next(b.text for b in resp.content if b.type == "text")
        return Decision.parse(json.loads(text), source=self.name)


def safe_decide(engine: DecisionEngine, snap: Snapshot) -> tuple[Decision, float]:
    t0 = time.monotonic()
    try:
        d = engine.decide(snap)
    except SchemaError as e:
        d = Decision.hold(f"{engine.name} schema error: {e}")
    except Exception as e:  # network, timeout, parse: never trade on an error
        d = Decision.hold(f"{engine.name} error: {type(e).__name__}: {e}"[:280])
    return d, (time.monotonic() - t0) * 1000


class EscalatingEngine:
    """Fast model every bar; deep model only when it matters, rate-limited.

    A low-confidence *neutral* read is just "nothing to do" and is not
    escalated; a low-confidence directional read or a crisis read is.
    """

    def __init__(
        self,
        fast: DecisionEngine,
        deep: DecisionEngine | None,
        risk: RiskManager,
        min_interval_s: float = 300.0,
    ):
        self.fast, self.deep, self.risk = fast, deep, risk
        self.min_interval_s = min_interval_s
        self._last_escalation = float("-inf")
        self.name = f"{fast.name}>{deep.name if deep else '-'}"
        self.on_escalate: Callable[[Decision], None] | None = None

    def decide(self, snap: Snapshot) -> Decision:
        d, _ = safe_decide(self.fast, snap)
        if (
            d.source != "failsafe"
            and self.deep is not None
            and self.risk.needs_escalation(d)
            and (d.direction != "neutral" or d.regime == "crisis")
        ):
            now = time.monotonic()
            if now - self._last_escalation < self.min_interval_s:
                # Can't afford a deep read right now: don't act on a shaky fast read.
                return Decision.hold("escalation rate-limited", source="failsafe")
            self._last_escalation = now
            if self.on_escalate:
                self.on_escalate(d)
            d, _ = safe_decide(self.deep, snap)
        return d


def build_engine(kind: str, risk: RiskManager) -> DecisionEngine:
    if kind == "rules":
        return EscalatingEngine(RuleEngine(), None, risk)
    if kind == "claude":
        fast = ClaudeEngine("claude-haiku-5-5", effort="low", timeout_s=10)
        deep = ClaudeEngine("claude-opus-5-5", effort="high", timeout_s=120, server_fallback=True)
        return EscalatingEngine(fast, deep, risk)
    raise ValueError(f"unknown engine {kind!r}")
