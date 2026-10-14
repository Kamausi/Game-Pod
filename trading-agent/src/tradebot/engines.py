"""Decision engines. Each maps a Snapshot to a validated Decision.

- RuleEngine: deterministic baseline. Runs offline, used for backtests and as
  the benchmark any model has to beat.
- ClaudeEngine: calls the Claude API with a strict JSON schema.
- EscalatingEngine: cheap fast model by default; escalates to a deep model
  when confidence is low or the regime reads as crisis.

Any engine error becomes Decision.hold(...) with source="failsafe".
"""
from __future__ import annotations

import hashlib
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
    last_trace: dict | None = None

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
        self.last_trace = {"engine": self.name, "score": score}
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


class EnsembleEngine:
    """Adaptive ensemble of indicator signals; weights adapt per regime from scored outcomes.

    Causal by construction: it only ever sees snapshots, in order. Each signal's
    call is scored `horizon` snapshots later against the mid it sees then; each
    ensemble call is scored target-before-stop on the mids it sees in between.
    Weight x1 = coin flip; the win probability it reports comes only from its own
    scored calls (shrunk toward no edge), never from a confidence it asserts.
    """

    name = "ensemble"
    SIGNALS = ("trend", "momentum", "meanrev", "breakout", "flow")

    def __init__(self, horizon: int = 15, call_horizon: int = 30, cost: float = 0.0016, stop_atr: float = 1.5, rr: float = 2.0,
                 extra_signals: Callable[[Snapshot], dict[str, float]] | None = None,
                 fixed_bracket: tuple[float, float] | None = (0.02, 0.04), disabled: tuple[str, ...] = ()):
        """extra_signals: optional causal source of more signals, e.g. news sentiment
        (`lambda s: {"sentiment": store.signal(s.instrument, s.as_of)[0]}`). They are
        weighted and scored exactly like the built-in ones.
        fixed_bracket: (stop, take) its calls are scored against; must match the policy's
        bracket (None = ATR-based, for exit_mode="atr").
        disabled: signals switched off, for ablation ("turn each tool off; if trades
        don't change, delete it")."""
        self.h, self.ch, self.cost, self.stop_atr, self.rr = horizon, call_horizon, cost, stop_atr, rr
        self.extra = extra_signals
        self.fixed = fixed_bracket
        self.disabled = set(disabled)
        self.last_trace: dict | None = None
        # live counterfactual: how often removing each signal would flip the direction
        self.impact: dict[str, list[int]] = {}  # signal -> [decisions where it was active, decisions it changed]
        self.step = 0
        self.stats = {}  # (regime, signal) -> [n, hits]
        self.sig_pending = []  # (step, regime, signal, dir, mid)
        self.call_pending = []  # dict(step, regime, dir, mid, stop, take)
        self.scored = []  # (regime, y)

    @staticmethod
    def signals(s: Snapshot) -> dict:
        return {
            "trend": (s.trend_fast + s.trend_slow) / 2,
            "momentum": max(-1.0, min(1.0, s.macd_hist_atr * 4)),
            "meanrev": 1.0 if s.rsi_14 < 30 and s.pct_b < 0.05 else -1.0 if s.rsi_14 > 70 and s.pct_b > 0.95 else 0.0,
            "breakout": float(s.breakout_30),
            "flow": max(-1.0, min(1.0, s.imbalance * 1.5)) * 0.6,
        }

    @staticmethod
    def regime(s: Snapshot) -> str:
        if s.rv_ratio > 2.5 or abs(s.ret_1) > 6 * max(s.rv_24, 1e-9):
            return "crisis"
        if s.rv_ratio > 1.6:
            return "high_vol"
        if s.trend_fast != 0 and s.trend_fast == s.trend_slow and abs(s.ret_24) > 1.5 * s.rv_24 * 24 ** 0.5:
            return "trending"
        return "mean_reverting"

    @staticmethod
    def _direction(score: float) -> str:
        return "long" if score > 0.2 else "short" if score < -0.2 else "neutral"

    def impact_report(self) -> dict[str, dict]:
        """Per signal: share of its active decisions whose direction it changed.
        Near 0 after hundreds of decisions = the signal is decoration; delete it."""
        return {k: {"active": a, "changed": c, "changed_pct": c / a if a else 0.0} for k, (a, c) in sorted(self.impact.items())}

    def weight(self, regime: str, sig: str) -> float:
        n, hits = self.stats.get((regime, sig), (0, 0))
        p = (hits + 2) / (n + 4)
        return max(0.1, min(3.0, 1 + 6 * (p - 0.5)))

    def _score_pending(self, mid: float):
        keep = []
        for st, reg, k, d, m in self.sig_pending:
            if self.step - st >= self.h:
                rec = self.stats.setdefault((reg, k), [0, 0])
                rec[0] += 1
                rec[1] += d * (mid / m - 1) > self.cost
            else:
                keep.append((st, reg, k, d, m))
        self.sig_pending = keep
        keep = []
        for c in self.call_pending:
            move = c["dir"] * (mid / c["mid"] - 1)
            if move <= -c["stop"]:
                self.scored.append((c["regime"], 0))
            elif move >= c["take"]:
                self.scored.append((c["regime"], 1))
            elif self.step - c["step"] >= self.ch:
                self.scored.append((c["regime"], 0))
            else:
                keep.append(c)
        self.call_pending = keep

    def decide(self, s: Snapshot) -> Decision:
        self.step += 1
        self._score_pending(s.mid)
        sig, reg = self.signals(s), self.regime(s)
        if self.extra is not None:
            sig.update({k: max(-1.0, min(1.0, float(v))) for k, v in self.extra(s).items()})
        for k in self.disabled:
            sig[k] = 0.0
        num = den = 0.0
        for k, v in sig.items():
            if v:
                w = self.weight(reg, k)
                num += w * v
                den += w
                if abs(v) >= 0.3:
                    self.sig_pending.append((self.step, reg, k, 1 if v > 0 else -1, s.mid))
        score = num / den if den else 0.0
        direction = self._direction(score)
        for k, v in sig.items():
            if v:
                w = self.weight(reg, k)
                rest = (num - w * v) / (den - w) if den - w > 0 else 0.0
                rec = self.impact.setdefault(k, [0, 0])
                rec[0] += 1
                rec[1] += self._direction(rest) != direction
        sgn = 1 if score > 0 else -1
        quality = min(3, sum(t == sgn for t in (s.trend_fast, s.trend_slow)) + (1 if s.breakout_30 or s.vol_ratio > 1.2 else 0))
        ys = [y for r, y in self.scored[-2000:] if r == reg][-200:]
        p_win = (sum(ys) + 3) / (len(ys) + 8)
        if direction != "neutral":
            stop, take = self.fixed or (max(0.004, self.stop_atr * s.atr_pct), self.rr * max(0.004, self.stop_atr * s.atr_pct))
            self.call_pending.append({"step": self.step, "regime": reg, "dir": sgn, "mid": s.mid, "stop": stop, "take": take})
        self.last_trace = {"engine": self.name, "signals": sig, "weights": {k: self.weight(reg, k) for k in sig},
                           "score": score, "disabled": sorted(self.disabled)}
        return Decision(
            regime=reg, direction=direction, direction_confidence=0.5 + 0.5 * abs(score), p_win=p_win,
            toxic_flow=s.spread_bps > 15 or s.vol_ratio > 5, setup_quality=quality,
            risk_state="safe" if s.drawdown < 0.05 and s.day_pnl_pct > -0.015 else "near_limit",
            rationale=f"score={score:+.2f} p_win from {len(ys)} scored calls", source=self.name,
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
        self.last_trace: dict | None = None

    @property
    def system_sha(self) -> str:
        return hashlib.sha256(self.system.encode()).hexdigest()[:16]

    def request(self, prompt: str) -> dict:
        """The exact request body. Stable content first (system prompt, cached), the
        volatile snapshot last, so every bar after the first reads the prefix from cache."""
        return dict(
            model=self.model,
            max_tokens=1024,
            system=[{"type": "text", "text": self.system, "cache_control": {"type": "ephemeral"}}],
            messages=[{"role": "user", "content": prompt}],
            output_config={
                "effort": self.effort,
                "format": {"type": "json_schema", "schema": JSON_SCHEMA},
            },
        )

    def decide(self, snap: Snapshot) -> Decision:
        return self.call(snap.to_prompt())

    def call(self, prompt: str) -> Decision:
        """One request, traced: exactly what the model saw, what it said, what it cost."""
        kwargs = self.request(prompt)
        trace = {"engine": self.name, "model_requested": self.model, "effort": self.effort,
                 "system_sha256": self.system_sha, "prompt": prompt}
        self.last_trace = trace
        t0 = time.monotonic()
        if self.server_fallback:
            # Server-side refusal fallback (Claude API only).
            resp = self.client.beta.messages.create(
                betas=["server-side-fallback-2026-07-01"], fallbacks="default", **kwargs
            )
        else:
            resp = self.client.messages.create(**kwargs)
        u = getattr(resp, "usage", None)
        trace.update(model_served=getattr(resp, "model", None), stop_reason=resp.stop_reason, latency_ms=(time.monotonic() - t0) * 1000,
                     usage={k: getattr(u, k, None) for k in ("input_tokens", "output_tokens", "cache_read_input_tokens",
                                                             "cache_creation_input_tokens")} if u else None)
        if resp.stop_reason == "refusal":
            return Decision.hold("model refused", source="failsafe")
        text = next(b.text for b in resp.content if b.type == "text")
        trace["raw_output"] = text
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
        self._trace: dict | None = None

    @property
    def last_trace(self) -> dict | None:
        return self._trace

    def decide(self, snap: Snapshot) -> Decision:
        self._trace = None
        d, _ = safe_decide(self.fast, snap)
        self._trace = getattr(self.fast, "last_trace", None)
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
            self._trace = {"escalated_from": self._trace, **(getattr(self.deep, "last_trace", None) or {})}
        return d


def build_engine(kind: str, risk: RiskManager) -> DecisionEngine:
    if kind == "rules":
        return EscalatingEngine(RuleEngine(), None, risk)
    if kind == "ensemble":
        return EscalatingEngine(EnsembleEngine(), None, risk)
    if kind == "jev":
        from .jev import JevEngine

        return EscalatingEngine(JevEngine(), None, risk)
    if kind == "jev>opus":
        from .jev import JevEngine

        deep = ClaudeEngine("claude-opus-5-5", effort="high", timeout_s=120, server_fallback=True)
        return EscalatingEngine(JevEngine(), deep, risk)
    if kind == "claude":
        fast = ClaudeEngine("claude-haiku-5-5", effort="low", timeout_s=10)
        deep = ClaudeEngine("claude-opus-5-5", effort="high", timeout_s=120, server_fallback=True)
        return EscalatingEngine(fast, deep, risk)
    raise ValueError(f"unknown engine {kind!r}")
