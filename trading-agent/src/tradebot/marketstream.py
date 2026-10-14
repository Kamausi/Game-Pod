"""Streaming market data with latency measurement (Crypto.com WebSocket).

Polling REST every few seconds is fine for 1-minute bars. For faster
strategies this streams order-book updates and measures, per message:
  exchange -> us   (venue timestamp to receipt; includes clock skew)
  us -> decision   (receipt to the decision callback returning)
and reports p50/p99. Requires `pip install websockets`. Not exercised against
the live stream from the build environment.

Honest scope: decision latency here is microseconds to milliseconds in Python.
End-to-end order latency is dominated by the network path to the exchange,
which only co-location changes. This measures where the time goes; it does
not make a home connection fast.
"""
from __future__ import annotations

import asyncio
import json
import statistics
import time
from dataclasses import dataclass, field
from typing import Callable

WS_URL = "wss://stream.crypto.com/exchange/v1/market"


@dataclass
class LatencyStats:
    wire_ms: list[float] = field(default_factory=list)
    decide_ms: list[float] = field(default_factory=list)

    def add(self, wire: float, decide: float):
        self.wire_ms.append(wire)
        self.decide_ms.append(decide)
        if len(self.wire_ms) > 10_000:
            del self.wire_ms[:5_000], self.decide_ms[:5_000]

    @staticmethod
    def _pct(xs, q):
        if not xs:
            return float("nan")
        s = sorted(xs)
        return s[min(len(s) - 1, int(q * len(s)))]

    def report(self) -> dict:
        return {"n": len(self.wire_ms),
                "wire_p50_ms": self._pct(self.wire_ms, 0.5), "wire_p99_ms": self._pct(self.wire_ms, 0.99),
                "decide_p50_ms": self._pct(self.decide_ms, 0.5), "decide_p99_ms": self._pct(self.decide_ms, 0.99)}


def parse_book_message(msg: dict) -> dict | None:
    """Return {instrument, ts, bids, asks} for a book update, None for anything else."""
    r = msg.get("result") or {}
    if not str(r.get("channel", "")).startswith("book"):
        return None
    d = (r.get("data") or [{}])[0]
    return {"instrument": r.get("instrument_name"), "ts": int(d.get("t", 0)),
            "bids": [(float(p), float(q)) for p, q, *_ in d.get("bids", [])],
            "asks": [(float(p), float(q)) for p, q, *_ in d.get("asks", [])]}


async def stream_books(instruments: list[str], on_book: Callable[[dict], None], stats: LatencyStats,
                       depth: int = 10, stop: asyncio.Event | None = None):
    import websockets

    sub = {"id": 1, "method": "subscribe", "params": {"channels": [f"book.{i}.{depth}" for i in instruments]}}
    async for ws in websockets.connect(WS_URL, ping_interval=None):  # reconnects with backoff
        try:
            await asyncio.sleep(1)  # exchange asks clients to wait 1s after connecting before subscribing
            await ws.send(json.dumps(sub))
            async for raw in ws:
                recv = time.time() * 1000
                msg = json.loads(raw)
                if msg.get("method") == "public/heartbeat":
                    await ws.send(json.dumps({"id": msg.get("id"), "method": "public/respond-heartbeat"}))
                    continue
                book = parse_book_message(msg)
                if book is None:
                    continue
                t0 = time.perf_counter()
                on_book(book)
                stats.add(recv - book["ts"], (time.perf_counter() - t0) * 1000)
                if stop is not None and stop.is_set():
                    return
        except websockets.ConnectionClosed:
            continue
