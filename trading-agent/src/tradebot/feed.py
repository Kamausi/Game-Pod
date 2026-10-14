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
