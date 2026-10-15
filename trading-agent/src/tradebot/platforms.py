"""Open-source platforms, plugged in at the edges (each one an optional dependency, imported lazily):

  ccxt       one interface to 100+ crypto exchanges. Market data from any of them; orders ONLY on
             an exchange's sandbox / testnet. Verified against ccxt 4.5.88 on 2026-10-15:
             set_sandbox_mode(enable), fetch_ticker(symbol), fetch_order_book(symbol, limit),
             fetch_ohlcv(symbol, timeframe, since, limit) -> [[ms, o, h, l, c, v]],
             create_order(symbol, type, side, amount, price, params), cancel_order(id, symbol),
             fetch_balance(). Exchanges without a sandbox (kraken, coinbase) have urls["test"] = None, and
             ccxt's set_sandbox_mode then fails with a TypeError, so this checks first and refuses.
  OpenBB     daily equity bars from whichever provider you configure (yfinance needs no key).
             Verified against openbb 4 / openbb-core on 2026-10-15:
             obb.equity.price.historical(symbol, start_date, end_date, provider) -> OBBject whose
             .results rows carry date, open, high, low, close, volume.
  freqtrade  export_freqtrade() writes an IStrategy (INTERFACE_VERSION 3) with the desk's stop and
             time exit, so a rule can be backtested and dry-run inside freqtrade too. freqtrade
             trades crypto only: the insider desk's stock signals do not port; trend200 and any
             crypto pair,date signal list do.
"""
from __future__ import annotations

import csv
import os
from datetime import date, datetime, timezone
from pathlib import Path

from .insider import DayBar


def _optional(module: str, extra: str):
    import importlib
    try:
        return importlib.import_module(module)
    except ImportError:
        raise SystemExit(f"{module} is not installed: pip install '.[{extra}]'") from None


class SandboxUnavailable(RuntimeError):
    pass


class CcxtVenue:
    """sandbox=True (default): orders go to the exchange's testnet. sandbox=False: market data
    only; every order method refuses. Going live through ccxt is a code change you make yourself."""

    def __init__(self, exchange_id: str, sandbox: bool = True, client=None):
        if client is None:
            ccxt = _optional("ccxt", "ccxt")
            if exchange_id not in ccxt.exchanges:
                raise ValueError(f"ccxt has no exchange '{exchange_id}'")
            env = exchange_id.upper()
            client = getattr(ccxt, exchange_id)({"apiKey": os.environ.get(f"CCXT_{env}_KEY"), "secret": os.environ.get(f"CCXT_{env}_SECRET"),
                                                 "enableRateLimit": True})
        self.ex, self.id, self.sandbox = client, exchange_id, sandbox
        if sandbox:
            if not (getattr(client, "urls", None) or {}).get("test"):
                raise SandboxUnavailable(f"{exchange_id} has no sandbox/testnet in ccxt: use sandbox=False for market data only")
            client.set_sandbox_mode(True)

    def ticker(self, symbol: str) -> dict:
        t = self.ex.fetch_ticker(symbol)
        return {"symbol": symbol, "bid": t.get("bid"), "ask": t.get("ask"), "last": t.get("last"), "time": t.get("datetime")}

    def book(self, symbol: str, depth: int = 10) -> dict:
        b = self.ex.fetch_order_book(symbol, depth)
        return {"bids": b["bids"][:depth], "asks": b["asks"][:depth], "time": b.get("datetime")}

    def bars(self, symbol: str, timeframe: str = "1d", since: str | None = None, limit: int | None = None) -> list[DayBar]:
        ms = int(datetime.fromisoformat(since).replace(tzinfo=timezone.utc).timestamp() * 1000) if since else None
        rows = self.ex.fetch_ohlcv(symbol, timeframe, ms, limit)
        return [DayBar(datetime.fromtimestamp(r[0] / 1000, timezone.utc).date().isoformat(), float(r[1]), float(r[2]), float(r[3]), float(r[4]), float(r[5] or 0))
                for r in rows]

    def balance(self) -> dict:
        return {k: v for k, v in (self.ex.fetch_balance().get("total") or {}).items() if v}

    def _orders_allowed(self):
        if not self.sandbox:
            raise PermissionError(f"ccxt {self.id}: orders are sandbox-only in this package")

    def place_limit(self, symbol: str, side: str, amount: float, price: float, post_only: bool = True) -> dict:
        self._orders_allowed()
        o = self.ex.create_order(symbol, "limit", side, amount, price, {"postOnly": True} if post_only else {})
        return {"id": o.get("id"), "status": o.get("status"), "symbol": symbol, "side": side, "amount": amount, "price": price}

    def cancel(self, order_id: str, symbol: str) -> None:
        self._orders_allowed()
        self.ex.cancel_order(order_id, symbol)


class OpenBBPrices:
    """Same .bars(symbol) interface as CsvPrices / AlpacaBars."""

    def __init__(self, start: str, end: str | None = None, provider: str = "yfinance", obb=None):
        if obb is None:
            obb = _optional("openbb", "openbb").obb
        self.obb, self.start, self.end, self.provider, self._cache = obb, start, end or date.today().isoformat(), provider, {}

    def bars(self, symbol: str) -> list[DayBar]:
        if symbol not in self._cache:
            try:
                res = self.obb.equity.price.historical(symbol=symbol, start_date=self.start, end_date=self.end, provider=self.provider)
                rows = res.results or []
            except Exception as e:  # OpenBB raises EmptyDataError (an OpenBBError) for no rows
                if type(e).__name__ not in ("EmptyDataError", "OpenBBError"):
                    raise
                rows = []
            self._cache[symbol] = sorted((DayBar(str(getattr(r, "date"))[:10], float(r.open), float(r.high), float(r.low), float(r.close), float(r.volume or 0))
                                          for r in rows), key=lambda b: b.d)
        return self._cache[symbol]


def write_bars_csv(bars: list[DayBar], path: str | Path) -> int:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["date", "open", "high", "low", "close", "volume"])
        for b in bars:
            w.writerow([b.d, b.o, b.h, b.l, b.c, b.v])
    return len(bars)


# ------------------------------------------------------------------ freqtrade

_FT = '''"""Exported by `tradebot export freqtrade` on {today}. {what}
Stop {stop:.1%} below entry (freqtrade stoploss), time exit after {hold} days (custom_exit),
no ROI ladder. Put this file in user_data/strategies/ and run, for example:
  freqtrade backtesting --strategy {name} --timeframe {tf} --timerange 20200101-
"""
from datetime import datetime, timedelta

from pandas import DataFrame

from freqtrade.strategy import IStrategy

{signals_block}

class {name}(IStrategy):
    INTERFACE_VERSION = 3
    timeframe = "{tf}"
    can_short = False
    stoploss = -{stop}
    minimal_roi = {{"0": 100.0}}  # never take profit by ROI: the exit is the stop or the clock
    use_exit_signal = {use_exit}
    process_only_new_candles = True
    startup_candle_count = {startup}

    def populate_indicators(self, dataframe: DataFrame, metadata: dict) -> DataFrame:
{indicators}
        return dataframe

    def populate_entry_trend(self, dataframe: DataFrame, metadata: dict) -> DataFrame:
{entry}
        return dataframe

    def populate_exit_trend(self, dataframe: DataFrame, metadata: dict) -> DataFrame:
{exit}
        return dataframe

    def custom_exit(self, pair: str, trade, current_time: datetime, current_rate: float, current_profit: float, **kwargs):
        if current_time - trade.open_date_utc >= timedelta(days={hold}):
            return "time_exit_{hold}d"
        return None
'''


def export_freqtrade(kind: str = "trend200", signals: list[tuple[str, str]] | None = None, stop_pct: float = 0.08, hold_days: int = 5,
                     timeframe: str = "1d", name: str = "TradebotExport") -> str:
    if not name.isidentifier():
        raise ValueError("name must be a Python identifier")
    if not 0 < stop_pct < 1:
        raise ValueError("stop_pct is a fraction, e.g. 0.08")
    if kind == "trend200":
        return _FT.format(today=date.today().isoformat(), what="trend200: long while the close is above its 200-candle average.",
                          stop=stop_pct, hold=10_000 if hold_days <= 0 else hold_days, name=name, tf=timeframe, signals_block="",
                          use_exit="True", startup=200,
                          indicators='        dataframe["sma200"] = dataframe["close"].rolling(200).mean()',
                          entry='        dataframe.loc[dataframe["close"] > dataframe["sma200"], "enter_long"] = 1',
                          exit='        dataframe.loc[dataframe["close"] < dataframe["sma200"], "exit_long"] = 1')
    if kind == "signals":
        if not signals:
            raise ValueError("kind=signals needs (pair, date) rows")
        by: dict[str, list[str]] = {}
        for pair, d in signals:
            by.setdefault(pair, []).append(date.fromisoformat(d[:10]).isoformat())
        block = "SIGNALS = {\n" + "".join(f"    {p!r}: {sorted(set(ds))!r},\n" for p, ds in sorted(by.items())) + "}"
        return _FT.format(today=date.today().isoformat(), what=f"Entry on the candle of each listed signal day ({len(signals)} signals, {len(by)} pairs).",
                          stop=stop_pct, hold=hold_days, name=name, tf=timeframe, signals_block=block, use_exit="False", startup=1,
                          indicators='        days = set(SIGNALS.get(metadata["pair"], []))\n'
                                     '        dataframe["signal"] = dataframe["date"].dt.strftime("%Y-%m-%d").isin(days).astype(int)',
                          entry='        dataframe.loc[dataframe["signal"] == 1, "enter_long"] = 1',
                          exit='        dataframe["exit_long"] = 0')
    raise ValueError(f"unknown kind {kind}")
