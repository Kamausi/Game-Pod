"""Portfolio-level risk: sizing scalers, correlation clusters, and stress tests.

All pure functions so the multi-asset desk, the paper runner and the tests
share one definition. Limits here only ever shrink exposure.
"""
from __future__ import annotations

import math
import statistics

CLUSTER_RHO = 0.7
CLUSTER_CAP = 0.40
STRESS_GATE = 0.12

STRESS_SCENARIOS = {
    "crypto_crash": "majors -20%, high-volatility tier -40%",
    "flash_crash_gapped_stops": "everything -10%, stops fill 3% further",
    "correlation_to_one": "every position moves -3 daily sigma together",
    "liquidity_crunch": "spreads x10 plus 1% exit slippage",
}


def vol_target_cap(rv_per_bar: float, bars_per_day: float, target_daily: float) -> float:
    """Largest position (fraction of equity) whose daily volatility stays at the target."""
    daily = max(rv_per_bar, 1e-9) * math.sqrt(bars_per_day)
    return target_daily / daily


def vol_regime_scale(rv_ratio: float, floor: float = 0.25) -> float:
    """Crazier market, smaller size. rv_ratio is short-window vol over its 24-bar norm:
    at or below 1 this is 1x; at 2x the usual vol it halves; never below `floor`.
    Computed in code every bar, never left to the model."""
    return max(floor, min(1.0, 1.0 / max(rv_ratio, 1e-9)))


def drawdown_scale(drawdown: float, pause_at: float = 0.10, floor: float = 0.25) -> float:
    """Linear de-risking: full size at 0 drawdown, `floor` near `pause_at`, zero beyond."""
    if drawdown >= pause_at:
        return 0.0
    return max(floor, 1 - drawdown / pause_at)


def performance_scale(recent_pnls: list[float], min_trades: int = 5) -> float:
    """0.5x after a cold streak, 1x otherwise; never scales above 1."""
    if len(recent_pnls) < min_trades:
        return 1.0
    wr = sum(p > 0 for p in recent_pnls) / len(recent_pnls)
    return max(0.5, min(1.0, 0.5 + wr))


def correlation_matrix(returns: dict[str, list[float]]) -> dict[str, dict[str, float]]:
    names = list(returns)
    n = min(len(r) for r in returns.values())
    z = {}
    for k in names:
        x = returns[k][-n:]
        m, sd = statistics.fmean(x), statistics.pstdev(x) or 1e-12
        z[k] = [(v - m) / sd for v in x]
    return {a: {b: sum(p * q for p, q in zip(z[a], z[b])) / n for b in names} for a in names}


def cluster_exposure(asset: str, notional_frac: float, positions: dict[str, float],
                     corr: dict[str, dict[str, float]], rho: float = CLUSTER_RHO) -> float:
    """Exposure of the proposed position plus same-side open positions correlated above rho."""
    total = abs(notional_frac)
    for other, frac in positions.items():
        if other != asset and frac and (frac > 0) == (notional_frac > 0) and corr[asset][other] > rho:
            total += abs(frac)
    return total


def stress_losses(positions: dict[str, float], tiers: dict[str, str], daily_vol: dict[str, float],
                  spread_bps: dict[str, float]) -> dict[str, float]:
    """Loss as a fraction of equity per scenario. positions = signed notional / equity."""
    out = {}
    out["crypto_crash"] = -sum(f * (-0.40 if tiers[a] == "high_vol" else -0.20) for a, f in positions.items())
    out["flash_crash_gapped_stops"] = sum(abs(f) * 0.13 for f in positions.values())
    out["correlation_to_one"] = sum(abs(f) * min(0.5, 3 * daily_vol[a]) for a, f in positions.items())
    out["liquidity_crunch"] = sum(abs(f) * (0.01 + 10 * spread_bps[a] / 2e4) for a, f in positions.items())
    return {k: max(0.0, v) for k, v in out.items()}
