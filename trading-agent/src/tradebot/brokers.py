"""Direct broker adapters for stocks (Alpaca) and forex (OANDA), plus a router.

Both adapters are locked to the brokers' PAPER / PRACTICE hosts. Pointing them
at a live host raises, by design: going live is a separate, reviewed change
behind the release gate, not a config flag.

Order shapes follow the brokers' public REST docs (Alpaca Trading API v2,
OANDA v20). They are covered by tests against a fake transport but have NOT
been exercised against the real services from this build environment. Run the
`tradebot brokers --check` command with your paper keys first.

Supported order types:
  Alpaca: market, limit, bracket (take-profit + stop-loss), trailing stop (%)
  OANDA:  market (FOK), limit (GTC) with stop-loss / take-profit / trailing
          stop attached on fill
Every order carries the caller's idempotent client id.
"""
from __future__ import annotations

import json
import os
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from typing import Callable, Protocol

from .assets import AssetClass, Instrument, normalize_qty

ALPACA_PAPER = "https://paper-api.alpaca.markets"
OANDA_PRACTICE = "https://api-fxpractice.oanda.com"

Transport = Callable[[str, str, dict, dict | None], tuple[int, dict]]


class LiveHostRefused(RuntimeError):
    pass


class BrokerError(RuntimeError):
    def __init__(self, status: int, body: dict):
        super().__init__(f"broker returned {status}: {body}")
        self.status, self.body = status, body


def urllib_transport(method: str, url: str, headers: dict, body: dict | None) -> tuple[int, dict]:
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method, headers={"Content-Type": "application/json", **headers})
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            raw = r.read()
            return r.status, json.loads(raw) if raw else {}
    except urllib.error.HTTPError as e:
        raw = e.read()
        try:
            return e.code, json.loads(raw)
        except ValueError:
            return e.code, {"raw": raw.decode(errors="replace")[:500]}


@dataclass(frozen=True)
class OrderRequest:
    client_id: str
    inst: Instrument
    side: str  # "buy" | "sell"
    qty: float  # shares / units / coins, positive
    order_type: str = "market"  # "market" | "limit"
    limit_price: float | None = None
    stop_loss: float | None = None
    take_profit: float | None = None
    trail_pct: float | None = None  # e.g. 1.5 for 1.5%


@dataclass
class OrderAck:
    client_id: str
    broker_order_id: str
    status: str
    raw: dict = field(repr=False, default_factory=dict)


class Broker(Protocol):
    asset_class: AssetClass

    def submit(self, o: OrderRequest) -> OrderAck: ...
    def position_qty(self, symbol: str) -> float: ...


class AlpacaPaper:
    asset_class = AssetClass.STOCK

    def __init__(self, key_id: str | None = None, secret: str | None = None, base: str = ALPACA_PAPER,
                 transport: Transport = urllib_transport):
        if base.rstrip("/") != ALPACA_PAPER:
            raise LiveHostRefused(f"Alpaca adapter is paper-only; refusing {base}")
        self.base, self.t = base.rstrip("/"), transport
        self.h = {"APCA-API-KEY-ID": key_id or os.environ.get("APCA_API_KEY_ID", ""),
                  "APCA-API-SECRET-KEY": secret or os.environ.get("APCA_API_SECRET_KEY", "")}

    def _call(self, method, path, body=None):
        status, js = self.t(method, self.base + path, self.h, body)
        if status >= 300:
            raise BrokerError(status, js)
        return js

    def build(self, o: OrderRequest) -> dict:
        qty = normalize_qty(o.inst, o.qty)
        if qty <= 0:
            raise ValueError("quantity rounds to zero for this instrument")
        body = {"symbol": o.inst.symbol, "qty": str(qty), "side": o.side, "client_order_id": o.client_id,
                "time_in_force": "day"}
        if o.trail_pct is not None:
            body.update(type="trailing_stop", trail_percent=str(o.trail_pct))
            return body
        body["type"] = o.order_type
        if o.order_type == "limit":
            body["limit_price"] = f"{o.limit_price:.4f}" if o.limit_price < 1 else f"{o.limit_price:.2f}"
        if o.stop_loss is not None and o.take_profit is not None:
            body.update(order_class="bracket", take_profit={"limit_price": f"{o.take_profit:.2f}"},
                        stop_loss={"stop_price": f"{o.stop_loss:.2f}"})
            body["time_in_force"] = "gtc"
        return body

    def submit(self, o: OrderRequest) -> OrderAck:
        js = self._call("POST", "/v2/orders", self.build(o))
        return OrderAck(o.client_id, js.get("id", ""), js.get("status", "unknown"), js)

    def position_qty(self, symbol: str) -> float:
        status, js = self.t("GET", f"{self.base}/v2/positions/{urllib.parse.quote(symbol)}", self.h, None)
        if status == 404:
            return 0.0
        if status >= 300:
            raise BrokerError(status, js)
        return float(js.get("qty", 0))

    def clock_open(self) -> bool:
        return bool(self._call("GET", "/v2/clock").get("is_open"))


class OandaPractice:
    asset_class = AssetClass.FOREX

    def __init__(self, account_id: str | None = None, token: str | None = None, base: str = OANDA_PRACTICE,
                 transport: Transport = urllib_transport):
        if base.rstrip("/") != OANDA_PRACTICE:
            raise LiveHostRefused(f"OANDA adapter is practice-only; refusing {base}")
        self.base, self.t = base.rstrip("/"), transport
        self.account = account_id or os.environ.get("OANDA_ACCOUNT_ID", "")
        self.h = {"Authorization": "Bearer " + (token or os.environ.get("OANDA_TOKEN", ""))}

    @staticmethod
    def _px(p: float) -> str:
        return f"{p:.5f}" if p < 50 else f"{p:.3f}"  # JPY pairs quote to 3 decimals

    def build(self, o: OrderRequest) -> dict:
        units = normalize_qty(o.inst, o.qty)
        if units <= 0:
            raise ValueError("units round to zero; minimum is one lot of this instrument")
        signed = int(units) if o.side == "buy" else -int(units)
        order = {"instrument": o.inst.symbol, "units": str(signed), "positionFill": "DEFAULT",
                 "clientExtensions": {"id": o.client_id}}
        if o.order_type == "limit":
            order.update(type="LIMIT", price=self._px(o.limit_price), timeInForce="GTC")
        else:
            order.update(type="MARKET", timeInForce="FOK")
        if o.stop_loss is not None:
            order["stopLossOnFill"] = {"price": self._px(o.stop_loss)}
        if o.take_profit is not None:
            order["takeProfitOnFill"] = {"price": self._px(o.take_profit)}
        if o.trail_pct is not None and o.limit_price:
            order["trailingStopLossOnFill"] = {"distance": self._px(o.limit_price * o.trail_pct / 100)}
        return {"order": order}

    def submit(self, o: OrderRequest) -> OrderAck:
        status, js = self.t("POST", f"{self.base}/v3/accounts/{self.account}/orders", self.h, self.build(o))
        if status >= 300:
            raise BrokerError(status, js)
        tx = js.get("orderFillTransaction") or js.get("orderCreateTransaction") or {}
        return OrderAck(o.client_id, str(tx.get("id", "")), "filled" if "orderFillTransaction" in js else "accepted", js)

    def position_qty(self, symbol: str) -> float:
        status, js = self.t("GET", f"{self.base}/v3/accounts/{self.account}/positions/{symbol}", self.h, None)
        if status == 404:
            return 0.0
        if status >= 300:
            raise BrokerError(status, js)
        p = js.get("position", {})
        return float(p.get("long", {}).get("units", 0)) + float(p.get("short", {}).get("units", 0))


class ExecutionHub:
    """Routes an order to the broker for its asset class. One idempotency ledger for all."""

    def __init__(self, brokers: dict[AssetClass, Broker]):
        self.brokers = brokers
        self.sent: dict[str, OrderAck] = {}

    def route(self, o: OrderRequest) -> OrderAck:
        if o.client_id in self.sent:
            return self.sent[o.client_id]  # duplicate: return the original acknowledgement, send nothing
        b = self.brokers.get(o.inst.asset_class)
        if b is None:
            raise ValueError(f"no broker registered for {o.inst.asset_class.value}")
        if o.inst.rules.limit_only and o.order_type != "limit":
            raise ValueError(f"{o.inst.tier} instruments accept limit orders only")
        ack = b.submit(o)
        self.sent[o.client_id] = ack
        return ack
