"""Technical indicators over closed bars. Pure functions; no look-ahead by construction
(every function only reads the list it is given, and callers pass bars <= decision time)."""
from __future__ import annotations

import statistics

from .types import Candle


def ema(xs: list[float], n: int) -> list[float]:
    k = 2 / (n + 1)
    out = [xs[0]]
    for x in xs[1:]:
        out.append(x * k + out[-1] * (1 - k))
    return out


def rsi(closes: list[float], n: int = 14) -> float:
    w = closes[-(n + 1):]
    gain = sum(max(b - a, 0) for a, b in zip(w, w[1:]))
    loss = sum(max(a - b, 0) for a, b in zip(w, w[1:]))
    if loss == 0:
        return 50.0 if gain == 0 else 100.0
    return 100 - 100 / (1 + gain / loss)


def macd_hist(closes: list[float], fast: int = 12, slow: int = 26, signal: int = 9) -> float:
    f, s = ema(closes, fast), ema(closes, slow)
    line = [a - b for a, b in zip(f, s)]
    return line[-1] - ema(line, signal)[-1]


def bollinger_pct_b(closes: list[float], n: int = 20, k: float = 2.0) -> float:
    w = closes[-n:]
    m, sd = statistics.fmean(w), statistics.pstdev(w)
    return 0.5 if sd == 0 else (closes[-1] - (m - k * sd)) / (2 * k * sd)


def breakout(candles: list[Candle], n: int = 30, vol_mult: float = 1.5) -> int:
    """+1 / -1 when the last close leaves the prior n-bar range on above-average volume."""
    prev, last = candles[-(n + 1):-1], candles[-1]
    avg_v = statistics.fmean(c.volume for c in candles[-21:-1])
    if avg_v <= 0 or last.volume < vol_mult * avg_v:
        return 0
    if last.close > max(c.high for c in prev):
        return 1
    if last.close < min(c.low for c in prev):
        return -1
    return 0


def resample(candles: list[Candle], k: int) -> list[Candle]:
    """Aggregate k consecutive closed bars into one (multi-timeframe view). Incomplete tail dropped."""
    out = []
    usable = len(candles) - len(candles) % k
    for i in range(0, usable, k):
        ch = candles[i:i + k]
        out.append(Candle(ch[0].open_ts, ch[-1].close_ts, ch[0].open, max(c.high for c in ch),
                          min(c.low for c in ch), ch[-1].close, sum(c.volume for c in ch)))
    return out


def trend(closes: list[float], fast: int, slow: int) -> int:
    if len(closes) < slow:
        return 0
    d = ema(closes, fast)[-1] - ema(closes, slow)[-1]
    return (d > 0) - (d < 0)
