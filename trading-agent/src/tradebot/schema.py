"""Typed decision schema returned by the reflex model.

The model only *judges*. It never sets sizes, prices, stops, or limits.
Anything that fails validation is treated as HOLD by the caller.
"""
from __future__ import annotations

from dataclasses import dataclass

SCHEMA_VERSION = "1"

REGIMES = ("trending", "mean_reverting", "high_vol", "crisis")
DIRECTIONS = ("long", "short", "neutral")
# The original spec says "mean_limit"; that reads as a typo for "near_limit".
RISK_STATES = ("safe", "near_limit", "reduce")


class SchemaError(ValueError):
    pass


@dataclass(frozen=True)
class Decision:
    regime: str
    direction: str
    direction_confidence: float  # model's confidence in `direction`, [0, 1]
    p_win: float  # model's probability the trade hits take-profit before stop, [0, 1]
    toxic_flow: bool
    setup_quality: int  # 0..3
    risk_state: str
    rationale: str = ""  # logged for review; never read by the policy
    source: str = ""  # which engine produced it (set by code, not the model)

    @classmethod
    def parse(cls, d: dict, source: str = "") -> "Decision":
        try:
            regime = d["regime"]
            direction = d["direction"]
            conf = float(d["direction_confidence"])
            p_win = float(d["p_win"])
            toxic = d["toxic_flow"]
            quality = d["setup_quality"]
            risk_state = d["risk_state"]
        except (KeyError, TypeError, ValueError) as e:
            raise SchemaError(f"missing/invalid field: {e}") from e
        if regime not in REGIMES:
            raise SchemaError(f"bad regime {regime!r}")
        if direction not in DIRECTIONS:
            raise SchemaError(f"bad direction {direction!r}")
        if risk_state not in RISK_STATES:
            raise SchemaError(f"bad risk_state {risk_state!r}")
        if not isinstance(toxic, bool):
            raise SchemaError("toxic_flow must be bool")
        if isinstance(quality, bool) or not isinstance(quality, int) or not 0 <= quality <= 3:
            raise SchemaError(f"setup_quality must be int 0..3, got {quality!r}")
        for name, v in (("direction_confidence", conf), ("p_win", p_win)):
            if not 0.0 <= v <= 1.0:
                raise SchemaError(f"{name} out of [0,1]: {v}")
        return cls(
            regime=regime,
            direction=direction,
            direction_confidence=conf,
            p_win=p_win,
            toxic_flow=toxic,
            setup_quality=quality,
            risk_state=risk_state,
            rationale=str(d.get("rationale", ""))[:280],
            source=source,
        )

    @classmethod
    def hold(cls, why: str, source: str = "failsafe") -> "Decision":
        return cls("crisis", "neutral", 0.0, 0.0, True, 0, "reduce", why, source)


# JSON schema handed to the model API as a structured-output constraint.
# Numeric ranges are validated in `Decision.parse`, not here.
JSON_SCHEMA = {
    "type": "object",
    "properties": {
        "regime": {"type": "string", "enum": list(REGIMES)},
        "direction": {"type": "string", "enum": list(DIRECTIONS)},
        "direction_confidence": {"type": "number"},
        "p_win": {"type": "number"},
        "toxic_flow": {"type": "boolean"},
        "setup_quality": {"type": "integer"},
        "risk_state": {"type": "string", "enum": list(RISK_STATES)},
        "rationale": {"type": "string"},
    },
    "required": [
        "regime",
        "direction",
        "direction_confidence",
        "p_win",
        "toxic_flow",
        "setup_quality",
        "risk_state",
        "rationale",
    ],
    "additionalProperties": False,
}
