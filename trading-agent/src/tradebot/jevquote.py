"""The jev-trader loop as a dry run: read the book, ask buy-or-sell, post one post-only quote
one tick inside the touch, replace it next step, and count what a taker would have filled.

There is no order path in this module. It answers one question before anyone wires a key in:
does "decide every few hundred ms, quote inside the spread" earn the spread, or does adverse
selection eat it? Every step is logged (decision, probabilities, latency, quote, fill).

Fills are conservative. Our quote is not in the recorded book, so the only evidence that a
taker reached it is the other side coming to our price: a resting bid counts as filled only
if the NEXT snapshot's best ask is at or below it (sellers now offer where we bid), an ask only
if the next best bid is at or above it. No queue priority is assumed. Each fill is marked out 1 and 10 steps later against the mid.

Decision sources:
  momentum (default, no key): direction of the mid over the last k snapshots, probability
           from top-of-book imbalance. This is the stand-in jev-trader ships with.
  jev      one Jev "choice" question per step via JevClient (TYPESAFE_API_KEY).
"""
from __future__ import annotations

import json
import statistics
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable


@dataclass
class Book:
    ts: float
    bids: list[tuple[float, float]]
    asks: list[tuple[float, float]]

    @property
    def bid(self) -> float:
        return self.bids[0][0]

    @property
    def ask(self) -> float:
        return self.asks[0][0]

    @property
    def mid(self) -> float:
        return (self.bid + self.ask) / 2


def parse_cdc_book(js: dict, ts: float | None = None) -> Book:
    """Crypto.com public/get-book result: {data: [{bids: [[px, qty, n]], asks: [...]}]}."""
    d = js["data"][0] if "data" in js else js
    lv = lambda xs: [(float(x[0]), float(x[1])) for x in xs]
    return Book(ts if ts is not None else float(d.get("t", time.time() * 1000)), lv(d["bids"]), lv(d["asks"]))


def momentum_decider(k: int = 5):
    hist: list[float] = []

    def decide(b: Book) -> tuple[str, float, float]:
        t0 = time.perf_counter()
        hist.append(b.mid)
        drift = hist[-1] - hist[max(0, len(hist) - 1 - k)]
        bq, aq = b.bids[0][1], b.asks[0][1]
        imb = (bq - aq) / (bq + aq) if bq + aq else 0.0
        score = (1 if drift > 0 else -1 if drift < 0 else 0) * 0.5 + imb * 0.5
        p_buy = min(0.99, max(0.01, 0.5 + score / 2))
        return ("buy" if p_buy >= 0.5 else "sell"), p_buy, (time.perf_counter() - t0) * 1000

    return decide


def jev_decider(client=None):
    from .jev import JevClient, choice
    c = client or JevClient()
    q = {"next": choice("Given this order book, will the mid price be higher or lower a few seconds from now?",
                        {"buy": "higher", "sell": "lower"})}

    def decide(b: Book) -> tuple[str, float, float]:
        t0 = time.perf_counter()
        state = {"best_bid": b.bid, "best_ask": b.ask, "bids": b.bids[:10], "asks": b.asks[:10]}
        a = c.ask(state, q)["next"]
        p_buy = float(a.get("probabilities", {}).get("buy", 0.5))
        return a.get("choice", "buy" if p_buy >= 0.5 else "sell"), p_buy, (time.perf_counter() - t0) * 1000

    return decide


@dataclass
class Result:
    steps: int = 0
    quotes: int = 0
    fills: list[dict] = field(default_factory=list)
    inventory: float = 0.0
    cash: float = 0.0
    latencies: list[float] = field(default_factory=list)
    hits: int = 0
    called: int = 0


def run(books: Iterable[Book], decide: Callable[[Book], tuple[str, float, float]], tick: float, size: float,
        max_inventory: float, maker_fee_bps: float = 0.0, log_path: str | Path | None = None, horizon: int = 10) -> dict:
    books = list(books)
    res, log = Result(), []
    quote = None  # (side, price)
    for i, b in enumerate(books):
        res.steps += 1
        if quote is not None:  # did the last quote fill on this snapshot?
            side, px = quote
            filled = (side == "buy" and b.ask <= px) or (side == "sell" and b.bid >= px)
            if filled:
                sg = 1 if side == "buy" else -1
                fee = px * size * maker_fee_bps / 1e4
                res.inventory += sg * size
                res.cash -= sg * px * size + fee
                res.fills.append({"i": i, "side": side, "px": px, "mid_at_fill": b.mid})
        side, p_buy, ms = decide(b)
        res.latencies.append(ms)
        if i + horizon < len(books):
            res.called += 1
            res.hits += (books[i + horizon].mid > b.mid) == (side == "buy") and books[i + horizon].mid != b.mid
        if (side == "buy" and res.inventory >= max_inventory) or (side == "sell" and res.inventory <= -max_inventory):
            side = "sell" if side == "buy" else "buy"  # at the cap, only quote the side that reduces
        if side == "buy":
            px = b.bid + tick if b.bid + tick < b.ask else b.bid  # post-only: never cross
        else:
            px = b.ask - tick if b.ask - tick > b.bid else b.ask
        quote = (side, round(px, 10))
        res.quotes += 1
        log.append({"i": i, "ts": b.ts, "decision": side, "p_buy": round(p_buy, 4), "latency_ms": round(ms, 3),
                    "quote": quote[1], "bid": b.bid, "ask": b.ask, "inventory": res.inventory})
    if log_path:
        Path(log_path).write_text("\n".join(json.dumps(x) for x in log) + "\n")
    last = books[-1].mid if books else 0.0
    mo = lambda k: [((books[min(len(books) - 1, f["i"] + k)].mid / f["px"] - 1) * 1e4) * (1 if f["side"] == "buy" else -1) for f in res.fills]
    captured = [((f["mid_at_fill"] - f["px"]) if f["side"] == "buy" else (f["px"] - f["mid_at_fill"])) / f["mid_at_fill"] * 1e4 for f in res.fills]
    avg = lambda xs: sum(xs) / len(xs) if xs else 0.0
    pnl = res.cash + res.inventory * last
    lat = sorted(res.latencies)
    adverse = -avg(mo(horizon))
    net = avg(captured) - maker_fee_bps - adverse
    return {"steps": res.steps, "quotes": res.quotes, "fills": len(res.fills), "inventory": res.inventory, "pnl_quote_ccy": pnl,
            "decision_hit_rate": res.hits / res.called if res.called else None,
            "latency_ms_p50": lat[len(lat) // 2] if lat else None, "latency_ms_p95": lat[int(len(lat) * 0.95) - 1] if len(lat) >= 20 else None,
            "spread_captured_bps": avg(captured), "markout_1_bps": avg(mo(1)), f"markout_{horizon}_bps": avg(mo(horizon)),
            "verdict": ("earns: captured spread beats fees and adverse selection" if net > 0 else
                        "does not earn: adverse selection and fees eat the spread") if res.fills else "no fills: nothing to judge",
            "mode": "dry run: no orders exist in this code path"}


def read_replay(path: str | Path) -> list[Book]:
    out = []
    for line in Path(path).read_text().splitlines():
        if line.strip():
            j = json.loads(line)
            out.append(Book(j["ts"], [tuple(x) for x in j["bids"]], [tuple(x) for x in j["asks"]]))
    return out


def record_live(instrument: str, steps: int, interval_s: float, get: Callable[[str], dict], out: str | Path) -> int:
    """Poll Crypto.com's public book and save snapshots for replay (no key, no orders)."""
    n = 0
    with Path(out).open("a") as fh:
        for _ in range(steps):
            js = get(f"https://api.crypto.com/exchange/v1/public/get-book?instrument_name={instrument}&depth=10")
            b = parse_cdc_book(js.get("result", js), time.time() * 1000)
            fh.write(json.dumps({"ts": b.ts, "bids": b.bids, "asks": b.asks}) + "\n")
            n += 1
            time.sleep(interval_s)
    return n
