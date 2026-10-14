"""Market data from the Crypto.com Exchange public REST API (no key needed).

NOTE: written against the documented v1 public endpoints but not exercised
end-to-end from the build environment (outbound traffic was blocked there).
Run `tradebot status --probe` once on your machine before trusting it.
"""
from __future__ import annotations

import json
import time
import urllib.parse
import urllib.request

from .types import Candle, OrderBook

BASE = "https://api.crypto.com/exchange/v1/public"
TIMEFRAMES_MS = {"1m": 60_000, "5m": 300_000, "15m": 900_000, "1h": 3_600_000, "4h": 14_400_000, "1D": 86_400_000}


def _get(path: str, **params) -> dict:
    url = f"{BASE}/{path}?{urllib.parse.urlencode(params)}"
    with urllib.request.urlopen(url, timeout=10) as r:
        body = json.loads(r.read())
    if body.get("code", 0) != 0:
        raise RuntimeError(f"exchange error {body.get('code')}: {body.get('message')}")
    return body["result"]


class CryptoComFeed:
    def __init__(self, instrument: str, timeframe: str = "1h"):
        if timeframe not in TIMEFRAMES_MS:
            raise ValueError(f"timeframe must be one of {list(TIMEFRAMES_MS)}")
        self.instrument = instrument
        self.timeframe = timeframe
        self.bar_ms = TIMEFRAMES_MS[timeframe]

    def closed_candles(self, now_ms: int, count: int = 200) -> list[Candle]:
        res = _get("get-candlestick", instrument_name=self.instrument, timeframe=self.timeframe, count=count)
        out = []
        for c in res["data"]:
            t = int(c["t"])
            close_ts = t + self.bar_ms
            if close_ts > now_ms:
                continue  # the still-forming bar: using it would leak the future
            out.append(Candle(t, close_ts, float(c["o"]), float(c["h"]), float(c["l"]), float(c["c"]), float(c["v"])))
        out.sort(key=lambda c: c.close_ts)
        return out

    def book(self, depth: int = 10) -> OrderBook:
        res = _get("get-book", instrument_name=self.instrument, depth=depth)
        d = res["data"][0]
        ts = int(d.get("t", time.time() * 1000))
        bids = tuple((float(p), float(q)) for p, q, *_ in d["bids"])
        asks = tuple((float(p), float(q)) for p, q, *_ in d["asks"])
        return OrderBook(ts, bids, asks)


def _iso_ms(t: str) -> int:
    """RFC3339 (any fractional seconds, Z) -> epoch ms, UTC."""
    import calendar

    return calendar.timegm(time.strptime(t[:19], "%Y-%m-%dT%H:%M:%S")) * 1000


class _AuthedFeed:
    """Shared shape: closed_candles(now_ms) and book() with the CryptoComFeed interface."""

    bar_ms: int

    def __init__(self, transport=None):
        from .brokers import urllib_transport

        self.t = transport or urllib_transport

    def _get(self, url: str, headers: dict) -> dict:
        status, js = self.t("GET", url, headers, None)
        if status >= 300:
            raise RuntimeError(f"data feed returned {status}: {str(js)[:200]}")
        return js


ALPACA_TF = {"1m": "1Min", "5m": "5Min", "15m": "15Min", "1h": "1Hour", "1D": "1Day"}


class AlpacaStockFeed(_AuthedFeed):
    """Alpaca market data v2 (IEX feed on the free plan). Keys from APCA_* env vars."""

    def __init__(self, symbol: str, timeframe: str = "1h", transport=None, feed: str = "iex"):
        import os

        super().__init__(transport)
        if timeframe not in ALPACA_TF:
            raise ValueError(f"timeframe must be one of {list(ALPACA_TF)}")
        self.symbol, self.timeframe, self.bar_ms, self.feed = symbol, timeframe, TIMEFRAMES_MS[timeframe], feed
        self.h = {"APCA-API-KEY-ID": os.environ.get("APCA_LIVE_KEY_ID") or os.environ.get("APCA_API_KEY_ID", ""),
                  "APCA-API-SECRET-KEY": os.environ.get("APCA_LIVE_SECRET_KEY") or os.environ.get("APCA_API_SECRET_KEY", "")}
        self.base = "https://data.alpaca.markets/v2/stocks"

    def closed_candles(self, now_ms: int, count: int = 200) -> list[Candle]:
        start = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime((now_ms - self.bar_ms * count * 3) / 1000))
        js = self._get(f"{self.base}/{self.symbol}/bars?timeframe={ALPACA_TF[self.timeframe]}&start={start}"
                       f"&limit={count * 3}&feed={self.feed}&adjustment=raw", self.h)
        out = []
        for b in js.get("bars") or []:
            t = _iso_ms(b["t"])
            if t + self.bar_ms <= now_ms:
                out.append(Candle(t, t + self.bar_ms, float(b["o"]), float(b["h"]), float(b["l"]), float(b["c"]), float(b["v"])))
        return sorted(out, key=lambda c: c.close_ts)[-count:]

    def book(self, depth: int = 1) -> OrderBook:
        q = self._get(f"{self.base}/{self.symbol}/quotes/latest?feed={self.feed}", self.h)["quote"]
        ts = _iso_ms(q["t"])
        return OrderBook(ts, ((float(q["bp"]), float(q["bs"])),), ((float(q["ap"]), float(q["as"])),))


OANDA_TF = {"1m": "M1", "5m": "M5", "15m": "M15", "1h": "H1", "4h": "H4", "1D": "D"}


class OandaFxFeed(_AuthedFeed):
    """OANDA v20 candles (mid) and live pricing. Uses the live host when OANDA_LIVE_TOKEN is set."""

    def __init__(self, instrument: str, timeframe: str = "1h", transport=None):
        import os

        super().__init__(transport)
        if timeframe not in OANDA_TF:
            raise ValueError(f"timeframe must be one of {list(OANDA_TF)}")
        self.instrument, self.timeframe, self.bar_ms = instrument, timeframe, TIMEFRAMES_MS[timeframe]
        live = bool(os.environ.get("OANDA_LIVE_TOKEN"))
        self.base = "https://api-fxtrade.oanda.com/v3" if live else "https://api-fxpractice.oanda.com/v3"
        self.account = os.environ.get("OANDA_LIVE_ACCOUNT_ID" if live else "OANDA_ACCOUNT_ID", "")
        self.h = {"Authorization": "Bearer " + os.environ.get("OANDA_LIVE_TOKEN" if live else "OANDA_TOKEN", "")}

    def closed_candles(self, now_ms: int, count: int = 200) -> list[Candle]:
        js = self._get(f"{self.base}/instruments/{self.instrument}/candles?granularity={OANDA_TF[self.timeframe]}"
                       f"&count={count}&price=M", self.h)
        out = []
        for c in js.get("candles", []):
            if not c.get("complete"):
                continue  # forming bar
            t = int(float(c["time"]) * 1000) if c["time"].replace(".", "").isdigit() else _iso_ms(c["time"])
            m = c["mid"]
            if t + self.bar_ms <= now_ms:
                out.append(Candle(t, t + self.bar_ms, float(m["o"]), float(m["h"]), float(m["l"]), float(m["c"]), float(c["volume"])))
        return out

    def book(self, depth: int = 1) -> OrderBook:
        js = self._get(f"{self.base}/accounts/{self.account}/pricing?instruments={self.instrument}", self.h)
        p = js["prices"][0]
        ts = _iso_ms(p["time"])
        return OrderBook(ts, ((float(p["bids"][0]["price"]), float(p["bids"][0]["liquidity"])),),
                         ((float(p["asks"][0]["price"]), float(p["asks"][0]["liquidity"])),))
