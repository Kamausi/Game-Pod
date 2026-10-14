"""Deterministic state engine.

Turns raw candles + order book + portfolio into one compact numeric snapshot.
This snapshot is the ONLY thing the decision model sees. Every number is
computed here, in code, never by the model.

Causality is enforced, not assumed: any input stamped after `as_of` raises
LookaheadError instead of being silently used.
"""
from __future__ import annotations

import json
import math
import statistics
from dataclasses import asdict, dataclass

from . import indicators as ind
from .types import Candle, OrderBook

# Rough upper bound: ~4 chars per token. 400 tokens -> 1600 chars; keep margin.
MAX_PROMPT_CHARS = 1400


class LookaheadError(ValueError):
    """An input carries a timestamp later than the decision time."""


@dataclass(frozen=True)
class PortfolioView:
    position_qty: float  # signed; >0 long, <0 short
    avg_entry: float
    equity: float
    peak_equity: float
    day_start_equity: float


@dataclass(frozen=True)
class Snapshot:
    instrument: str
    as_of: int
    mid: float
    spread_bps: float
    imbalance: float  # (bid_qty - ask_qty) / (bid_qty + ask_qty) over top levels, in [-1, 1]
    ret_1: float  # log return of last bar
    ret_6: float
    ret_24: float
    rv_24: float  # stdev of 1-bar log returns, last 24 bars
    rv_ratio: float  # rv_6 / rv_24, >1 means volatility expanding
    atr_pct: float  # ATR(14) / close
    z_20: float  # (close - SMA20) / stdev20
    range_pos_48: float  # where close sits in the 48-bar high/low range, [0, 1]
    vol_ratio: float  # last bar volume / mean volume of previous 24
    inventory_frac: float  # signed position notional / equity
    unrealized_pct: float
    drawdown: float  # 1 - equity / peak_equity, >= 0
    day_pnl_pct: float
    # technical indicators (computed in code, multi-timeframe)
    rsi_14: float = 50.0
    macd_hist_atr: float = 0.0  # MACD histogram in ATR units
    pct_b: float = 0.5  # Bollinger %B (20, 2)
    breakout_30: int = 0  # +1 / -1 / 0
    trend_fast: int = 0  # EMA 9/21 on this timeframe
    trend_slow: int = 0  # EMA 8/21 on bars aggregated x4

    def to_prompt(self) -> str:
        d = asdict(self)
        out = {}
        for k, v in d.items():
            if isinstance(v, float):
                out[k] = round(v, 6) if abs(v) < 1 else round(v, 2)
            else:
                out[k] = v
        text = json.dumps(out, separators=(",", ":"))
        if len(text) > MAX_PROMPT_CHARS:  # pragma: no cover - guarded by tests
            raise ValueError(f"snapshot too large for the token budget: {len(text)} chars")
        return text


def _log_returns(closes: list[float]) -> list[float]:
    return [math.log(b / a) for a, b in zip(closes, closes[1:])]


def _stdev(xs: list[float]) -> float:
    return statistics.pstdev(xs) if len(xs) > 1 else 0.0


class StateEngine:
    MIN_BARS = 50
    BOOK_LEVELS = 10

    def snapshot(
        self,
        instrument: str,
        as_of: int,
        candles: list[Candle],
        book: OrderBook,
        pf: PortfolioView,
    ) -> Snapshot:
        if book.ts > as_of:
            raise LookaheadError(f"book ts {book.ts} > as_of {as_of}")
        for c in candles:
            if c.close_ts > as_of:
                raise LookaheadError(f"candle closing {c.close_ts} > as_of {as_of}")
        if any(b.close_ts >= a.close_ts for a, b in zip(candles[1:], candles)):
            raise ValueError("candles must be strictly increasing in time")
        if len(candles) < self.MIN_BARS:
            raise ValueError(f"need >= {self.MIN_BARS} closed candles, got {len(candles)}")
        if not book.bids or not book.asks or book.best_bid >= book.best_ask:
            raise ValueError("crossed or empty book")

        closes = [c.close for c in candles]
        rets = _log_returns(closes)
        last = candles[-1]

        mid = book.mid
        spread_bps = (book.best_ask - book.best_bid) / mid * 1e4
        bq = sum(q for _, q in book.bids[: self.BOOK_LEVELS])
        aq = sum(q for _, q in book.asks[: self.BOOK_LEVELS])
        imbalance = (bq - aq) / (bq + aq) if bq + aq > 0 else 0.0

        rv_24 = _stdev(rets[-24:])
        rv_6 = _stdev(rets[-6:])
        trs = []
        for prev, c in zip(candles[-15:-1], candles[-14:]):
            trs.append(max(c.high - c.low, abs(c.high - prev.close), abs(c.low - prev.close)))
        atr_pct = (sum(trs) / len(trs)) / last.close

        w20 = closes[-20:]
        sd20 = _stdev(w20)
        z_20 = (last.close - sum(w20) / 20) / sd20 if sd20 > 0 else 0.0
        hi48 = max(c.high for c in candles[-48:])
        lo48 = min(c.low for c in candles[-48:])
        range_pos = (last.close - lo48) / (hi48 - lo48) if hi48 > lo48 else 0.5
        prev_vol = [c.volume for c in candles[-25:-1]]
        mean_vol = sum(prev_vol) / len(prev_vol)
        vol_ratio = last.volume / mean_vol if mean_vol > 0 else 1.0

        equity = pf.equity
        inventory = pf.position_qty * mid / equity if equity > 0 else 0.0
        unreal = (
            (mid / pf.avg_entry - 1) * (1 if pf.position_qty > 0 else -1)
            if pf.position_qty and pf.avg_entry
            else 0.0
        )
        dd = max(0.0, 1 - equity / pf.peak_equity) if pf.peak_equity > 0 else 0.0
        day = equity / pf.day_start_equity - 1 if pf.day_start_equity > 0 else 0.0

        return Snapshot(
            instrument=instrument,
            as_of=as_of,
            mid=mid,
            spread_bps=spread_bps,
            imbalance=imbalance,
            ret_1=rets[-1],
            ret_6=math.log(closes[-1] / closes[-7]),
            ret_24=math.log(closes[-1] / closes[-25]),
            rv_24=rv_24,
            rv_ratio=rv_6 / rv_24 if rv_24 > 0 else 1.0,
            atr_pct=atr_pct,
            z_20=z_20,
            range_pos_48=range_pos,
            vol_ratio=vol_ratio,
            inventory_frac=inventory,
            unrealized_pct=unreal,
            drawdown=dd,
            day_pnl_pct=day,
            rsi_14=ind.rsi(closes),
            macd_hist_atr=ind.macd_hist(closes) / (atr_pct * last.close) if atr_pct > 0 else 0.0,
            pct_b=ind.bollinger_pct_b(closes),
            breakout_30=ind.breakout(candles),
            trend_fast=ind.trend(closes, 9, 21),
            trend_slow=ind.trend([c.close for c in ind.resample(candles, 4)], 8, 21),
        )
