"""Going live: the launch gates, canary caps, and the authorization every live
adapter requires before it will send a real order.

Flow:
  1. Paper-trade an approved release (`tradebot paper`).
  2. `tradebot go-live` checks the launch gates against that paper run:
       - at least 30 days of paper trading
       - at least 200 scored calls, with model Brier at least 0.01 better than base rate
       - paper return after fees > 0, and paper max drawdown < 10%
       - failsafe holds under 5% of decisions
       - operational gate passed for the current engine source
       - the approved release still verifies (limits file and engine unchanged)
     then asks you to type a confirmation, and writes a LiveAuthorization bound to
     the release, the limits file and the engine source hash. It expires in 7 days.
  3. Live adapters refuse to construct without a valid authorization.

`--accept-unproven` lets you go live when gates fail. It requires typing a longer
phrase and is written into the authorization, the journal and every alert.

Canary caps (config/live_caps.json) apply on top of the percentage limits and are
checked before every live order: max order size, max position, max daily loss in
dollars. Start small; raising them means editing the file, which invalidates the
authorization until you run go-live again.
"""
from __future__ import annotations

import hashlib
import json
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Callable

from .experiments import Register, engine_version
from .journal import read_journal
from .policy import PolicyParams
from .release import DEFAULT_LIMITS_FILE, DEFAULT_RELEASE_FILE, ReleaseError, file_sha, verify_release
from .review import brier, label_outcomes

AUTH_FILE = Path("releases/live_authorization.json")
CAPS_FILE = Path("config/live_caps.json")
AUTH_TTL_MS = 7 * 86_400_000
CONFIRM = "GO LIVE"
CONFIRM_UNPROVEN = "I ACCEPT REAL LOSSES ON AN UNPROVEN STRATEGY"


class NotAuthorized(RuntimeError):
    pass


@dataclass(frozen=True)
class LiveCaps:
    max_order_usd: float = 100.0
    max_position_usd: float = 500.0
    max_daily_loss_usd: float = 50.0
    max_orders_per_day: int = 50

    def __post_init__(self):
        if min(self.max_order_usd, self.max_position_usd, self.max_daily_loss_usd) <= 0:
            raise ValueError("live caps must be positive")
        if self.max_order_usd > self.max_position_usd:
            raise ValueError("max_order_usd cannot exceed max_position_usd")


def init_caps(path: Path = CAPS_FILE) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        path.write_text(json.dumps(asdict(LiveCaps()), indent=2) + "\n")
    return path


def load_caps(path: Path = CAPS_FILE) -> LiveCaps:
    if not Path(path).exists():
        raise NotAuthorized(f"{path} missing; run `tradebot init-limits`")
    return LiveCaps(**json.loads(Path(path).read_text()))


@dataclass(frozen=True)
class GateResult:
    name: str
    passed: bool
    detail: str


def launch_gates(paper_dir: Path, reg: Register, params: PolicyParams | None = None) -> list[GateResult]:
    params = params or PolicyParams()
    out = []
    jpath = Path(paper_dir) / "journal.jsonl"
    recs = read_journal(jpath) if jpath.exists() else []
    decs = [r for r in recs if r["kind"] == "decision"]
    starts = [r for r in recs if r["kind"] == "startup"]
    dev = any(r.get("release") == "DEV (not promotable)" for r in starts)
    out.append(GateResult("paper run is from an approved release", bool(decs) and not dev,
                          "DEV run; not promotable" if dev else f"{len(decs)} decisions journaled"))
    days = (decs[-1]["ts"] - decs[0]["ts"]) / 86_400_000 if len(decs) > 1 else 0.0
    out.append(GateResult("at least 30 days of paper trading", days >= 30, f"{days:.1f} days"))
    labeled = label_outcomes(recs, params)
    ps, ys = [r["decision"]["p_win"] for r, _ in labeled], [y for _, y in labeled]
    base = sum(ys) / len(ys) if ys else 0.0
    b_model, b_base = (brier(ps, ys), brier([base] * len(ys), ys)) if ys else (1.0, 0.0)
    out.append(GateResult("at least 200 scored calls", len(ys) >= 200, f"{len(ys)} scored"))
    out.append(GateResult("model Brier beats base rate by 0.01", bool(ys) and b_model <= b_base - 0.01,
                          f"model {b_model:.4f} vs base {b_base:.4f}" if ys else "no scored calls"))
    eq = [r["equity"] for r in decs]
    ret = eq[-1] / eq[0] - 1 if eq else 0.0
    peak, mdd = (eq[0] if eq else 0.0), 0.0
    for e in eq:
        peak = max(peak, e)
        mdd = max(mdd, 1 - e / peak if peak else 0.0)
    out.append(GateResult("paper return after fees > 0", ret > 0, f"{ret:+.2%}"))
    out.append(GateResult("paper max drawdown < 10%", bool(eq) and mdd < 0.10, f"{mdd:.2%}"))
    fs = sum(1 for r in decs if r["decision"]["source"] == "failsafe")
    out.append(GateResult("failsafe holds < 5% of decisions", bool(decs) and fs <= 0.05 * len(decs), f"{fs}/{len(decs)}"))
    gate = reg.root / "opsgate.json"
    g = json.loads(gate.read_text()) if gate.exists() else None
    ok = bool(g and g["passed"] and g["engine_version"]["source_sha256"] == engine_version()["source_sha256"])
    out.append(GateResult("operational gate passed for this engine", ok, "passed" if ok else "run `tradebot opsgate`"))
    return out


def _tty_phrase(prompt: str) -> str:
    if not sys.stdin.isatty():
        raise NotAuthorized("go-live needs an interactive terminal; it can't be scripted or piped")
    return input(prompt).strip()


def authorize_live(reg: Register, paper_dir: Path, operator: str, venues: list[str], accept_unproven: bool = False,
                   release_path: Path = DEFAULT_RELEASE_FILE, caps_path: Path = CAPS_FILE,
                   ask: Callable[[str], str] = _tty_phrase, now_ms: int | None = None, auth_path: Path = AUTH_FILE) -> dict:
    spec, limits, rel = verify_release(release_path)  # raises if limits or engine changed
    caps = load_caps(caps_path)
    gates = launch_gates(paper_dir, reg, PolicyParams(**{**asdict(PolicyParams()), **spec.params}))
    failed = [g for g in gates if not g.passed]
    if failed and not accept_unproven:
        raise NotAuthorized("launch gates failed:\n  " + "\n  ".join(f"{g.name}: {g.detail}" for g in failed))
    phrase = CONFIRM_UNPROVEN if failed else CONFIRM
    typed = ask(f"Live trading on {', '.join(venues)} with caps {asdict(caps)}.\nType '{phrase}' to authorize: ")
    if typed != phrase:
        raise NotAuthorized("confirmation did not match; nothing authorized")
    now = now_ms or int(time.time() * 1000)
    auth = {
        "operator": operator, "venues": sorted(venues), "issued_ms": now, "expires_ms": now + AUTH_TTL_MS,
        "release_candidate": rel["candidate_id"], "release_sha256": file_sha(release_path),
        "limits_sha256": rel["limits_sha256"], "caps_sha256": file_sha(caps_path), "caps": asdict(caps),
        "engine_source_sha256": engine_version()["source_sha256"],
        "gates": [asdict(g) for g in gates], "unproven_accepted": [g.name for g in failed],
    }
    auth_path = Path(auth_path)
    auth_path.parent.mkdir(parents=True, exist_ok=True)
    auth_path.write_text(json.dumps(auth, indent=2) + "\n")
    return auth


@dataclass(frozen=True)
class LiveAuthorization:
    venue: str
    caps: LiveCaps
    record: dict


def require_live(venue: str, auth_path: Path = AUTH_FILE, release_path: Path = DEFAULT_RELEASE_FILE,
                 caps_path: Path = CAPS_FILE, now_ms: int | None = None) -> LiveAuthorization:
    """Every live adapter calls this. Any drift since go-live voids the authorization."""
    if not Path(auth_path).exists():
        raise NotAuthorized("no live authorization; run `tradebot go-live`")
    a = json.loads(Path(auth_path).read_text())
    now = now_ms or int(time.time() * 1000)
    if now > a["expires_ms"]:
        raise NotAuthorized("live authorization expired (7 days); run `tradebot go-live` again")
    if venue not in a["venues"]:
        raise NotAuthorized(f"live authorization does not cover {venue}")
    try:
        verify_release(release_path)
    except ReleaseError as e:
        raise NotAuthorized(f"release no longer valid: {e}") from e
    if file_sha(release_path) != a["release_sha256"]:
        raise NotAuthorized("release changed since go-live")
    if file_sha(caps_path) != a["caps_sha256"]:
        raise NotAuthorized("live caps changed since go-live; run `tradebot go-live` again")
    if engine_version()["source_sha256"] != a["engine_source_sha256"]:
        raise NotAuthorized("engine source changed since go-live")
    return LiveAuthorization(venue, LiveCaps(**a["caps"]), a)


class CanaryGuard:
    """Dollar caps checked before every live order, on top of the percentage limits."""

    def __init__(self, caps: LiveCaps):
        self.caps = caps
        self.day = -1
        self.day_start_equity = 0.0
        self.orders_today = 0

    def check(self, side: str, qty: float, price: float, position_qty: float, equity: float, now_ms: int) -> list[str]:
        day = now_ms // 86_400_000
        if day != self.day:
            self.day, self.day_start_equity, self.orders_today = day, equity, 0
        signed = qty if side == "buy" else -qty
        reducing = abs(position_qty + signed) < abs(position_qty) and (position_qty + signed) * position_qty >= 0
        if reducing:
            return []
        why = []
        if qty * price > self.caps.max_order_usd + 1e-9:
            why.append(f"order ${qty * price:,.2f} over canary cap ${self.caps.max_order_usd:,.2f}")
        if abs(position_qty + signed) * price > self.caps.max_position_usd + 1e-9:
            why.append(f"position would be ${abs(position_qty + signed) * price:,.2f}, cap ${self.caps.max_position_usd:,.2f}")
        if self.day_start_equity - equity >= self.caps.max_daily_loss_usd:
            why.append(f"daily loss ${self.day_start_equity - equity:,.2f} reached cap ${self.caps.max_daily_loss_usd:,.2f}")
        if self.orders_today >= self.caps.max_orders_per_day:
            why.append("daily live order cap reached")
        return why

    def record(self):
        self.orders_today += 1


def caps_fingerprint(caps: LiveCaps) -> str:
    return hashlib.sha256(json.dumps(asdict(caps), sort_keys=True).encode()).hexdigest()[:12]
