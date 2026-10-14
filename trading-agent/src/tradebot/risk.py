"""Hard risk layer. Deterministic, model-independent, checked before every order.

Nothing the model outputs can loosen these limits. The only way an order
reaches a broker is through `GuardedBroker.submit`, which calls
`RiskManager.check_order` first.
"""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from .schema import Decision

DAY_MS = 86_400_000


@dataclass(frozen=True)
class RiskLimits:
    max_drawdown: float = 0.15  # from equity peak; latches the kill switch
    max_daily_loss: float = 0.03  # from UTC-day start equity; reduce-only for the day
    max_position_frac: float = 0.30  # |position notional| / equity
    max_order_frac: float = 0.30  # single order notional / equity
    max_orders_per_day: int = 24
    max_spread_bps: float = 25.0
    max_data_age_ms: int = 120_000
    escalate_below_conf: float = 0.60

    def __post_init__(self):
        if not 0 < self.max_drawdown <= 0.15:
            raise ValueError("max_drawdown must be in (0, 0.15]")
        if not 0 < self.max_position_frac <= 1.0:
            raise ValueError("max_position_frac must be in (0, 1]; no leverage")


@dataclass(frozen=True)
class Verdict:
    allowed: bool
    qty: float
    reasons: tuple[str, ...]


class RiskManager:
    def __init__(self, limits: RiskLimits, kill_file: Path, state_file: Path | None = None):
        self.limits = limits
        self.kill_file = Path(kill_file)
        self.state_file = Path(state_file) if state_file else None
        self.peak_equity = 0.0
        self.day = -1
        self.day_start_equity = 0.0
        self.orders_today = 0
        self.latched: str | None = None
        self.halted_today = False
        self._load()

    # --- persistence: a restart must not forget a drawdown kill or the peak ---
    def _load(self):
        if self.state_file and self.state_file.exists():
            s = json.loads(self.state_file.read_text())
            self.peak_equity = s["peak_equity"]
            self.day = s["day"]
            self.day_start_equity = s["day_start_equity"]
            self.orders_today = s["orders_today"]
            self.latched = s["latched"]
            self.halted_today = s["halted_today"]

    def _save(self):
        if self.state_file:
            self.state_file.write_text(
                json.dumps(
                    {
                        "peak_equity": self.peak_equity,
                        "day": self.day,
                        "day_start_equity": self.day_start_equity,
                        "orders_today": self.orders_today,
                        "latched": self.latched,
                        "halted_today": self.halted_today,
                    }
                )
            )

    # --- kill switch ---
    def kill(self, reason: str):
        self.latched = reason
        self.kill_file.parent.mkdir(parents=True, exist_ok=True)
        self.kill_file.write_text(reason + "\n")
        self._save()

    def is_killed(self) -> bool:
        return self.latched is not None or self.kill_file.exists()

    def kill_reason(self) -> str | None:
        if self.latched:
            return self.latched
        if self.kill_file.exists():
            return self.kill_file.read_text().strip() or "kill file present"
        return None

    # --- equity tracking ---
    def update(self, equity: float, now_ms: int) -> None:
        day = now_ms // DAY_MS
        if day != self.day:
            self.day = day
            self.day_start_equity = equity
            self.orders_today = 0
            self.halted_today = False
        self.peak_equity = max(self.peak_equity, equity)
        dd = 1 - equity / self.peak_equity if self.peak_equity > 0 else 0.0
        if dd >= self.limits.max_drawdown and not self.latched:
            self.kill(f"max drawdown breached: {dd:.2%}")
        if self.day_start_equity > 0 and equity / self.day_start_equity - 1 <= -self.limits.max_daily_loss:
            self.halted_today = True
        self._save()

    def needs_escalation(self, d: Decision) -> bool:
        return d.direction_confidence < self.limits.escalate_below_conf or d.regime == "crisis"

    # --- the gate ---
    def check_order(
        self,
        side: str,
        qty: float,
        price: float,
        position_qty: float,
        equity: float,
        now_ms: int,
        data_ts: int,
        spread_bps: float,
    ) -> Verdict:
        if qty <= 0 or price <= 0 or equity <= 0:
            return Verdict(False, 0.0, ("non-positive qty/price/equity",))
        signed = qty if side == "buy" else -qty
        new_pos = position_qty + signed
        reduce_only = abs(new_pos) < abs(position_qty) and new_pos * position_qty >= 0
        if reduce_only:
            return Verdict(True, qty, ("reduce-only always allowed",))

        reasons = []
        if self.is_killed():
            reasons.append(f"killed: {self.kill_reason()}")
        if self.halted_today:
            reasons.append("daily loss limit hit")
        if now_ms - data_ts > self.limits.max_data_age_ms:
            reasons.append("stale market data")
        if spread_bps > self.limits.max_spread_bps:
            reasons.append(f"spread {spread_bps:.1f}bps too wide")
        if self.orders_today >= self.limits.max_orders_per_day:
            reasons.append("order count limit")
        if reasons:
            return Verdict(False, 0.0, tuple(reasons))

        max_pos_qty = self.limits.max_position_frac * equity / price
        room = max_pos_qty - abs(position_qty) if position_qty * signed >= 0 else max_pos_qty + abs(position_qty)
        clamped = min(qty, max(0.0, room), self.limits.max_order_frac * equity / price)
        if clamped <= 0:
            return Verdict(False, 0.0, ("position limit",))
        note = ("clamped",) if clamped < qty else ()
        return Verdict(True, clamped, note)

    def record_order(self):
        self.orders_today += 1
        self._save()
