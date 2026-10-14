"""Real-money venues: Crypto.com Exchange (spot), Alpaca (stocks), OANDA (forex).

Every class here takes a `LiveAuthorization` for its venue, which only
`require_live(venue)` issues (see live.py), and the runner builds them through
`build_live_client`, which calls it. Each one is wrapped by `LiveVenue`, which:

  - checks the canary dollar caps before every order (reduce-only always allowed)
  - sends a marketable LIMIT order, immediate-or-cancel, capped at `max_slip_bps`
    through the touch (never an unbounded market order)
  - polls the venue for the real outcome and mirrors only the confirmed fill
    (real quantity, average price and fee) into the local book
  - reports position as venue balance minus the baseline recorded at first start,
    so funds you already held are never treated as the bot's position

`LiveVenue` has the same interface as `execution.SimVenue`, so the idempotent
ledger, partial-fill handling, reject kill and restart reconciliation in
`OrderManager` apply to live orders unchanged.

Request formats follow the venues' public API docs (Crypto.com Exchange v1,
Alpaca Trading API v2, OANDA v20). They are covered by tests against fake
transports, including the Crypto.com signature, but have NOT been exercised
against the real services from the build environment. `tradebot brokers --check`
and a one-order canary are part of the launch runbook (LAUNCH.md).
"""
from __future__ import annotations

import hashlib
import hmac
import json
import os
import time
import urllib.parse
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from .assets import AssetClass, Instrument, normalize_qty
from .broker import PaperBroker
from .brokers import BrokerError, OrderRequest, Transport, urllib_transport
from .execution import VenueReply
from .live import CanaryGuard, LiveAuthorization, require_live

CRYPTOCOM_API = "https://api.crypto.com/exchange/v1"
ALPACA_LIVE = "https://api.alpaca.markets"
OANDA_LIVE = "https://api-fxtrade.oanda.com"
TERMINAL = {"FILLED", "CANCELED", "REJECTED", "EXPIRED"}


def _num(x) -> str:
    """Decimal string without exponent or trailing zeros, as exchanges expect."""
    s = f"{x:.12f}".rstrip("0").rstrip(".")
    return s or "0"


def _round_step(x: float, step: float) -> float:
    return round(int(x / step + 1e-9) * step, 12) if step > 0 else x


# ----------------------------------------------------------------- Crypto.com


def cryptocom_param_string(obj) -> str:
    """The documented signing serialization: keys sorted, key+value concatenated,
    nested dicts and lists flattened recursively, None as the string 'null'."""
    if obj is None:
        return ""
    if isinstance(obj, dict):
        out = []
        for k in sorted(obj):
            v = obj[k]
            out.append(k)
            if v is None:
                out.append("null")
            elif isinstance(v, (dict, list)):
                out.append(cryptocom_param_string(v))
            else:
                out.append(str(v))
        return "".join(out)
    if isinstance(obj, list):
        return "".join(cryptocom_param_string(v) if isinstance(v, (dict, list)) else str(v) for v in obj)
    return str(obj)


def cryptocom_sign(secret: str, method: str, req_id: int, api_key: str, params: dict, nonce: int) -> str:
    payload = f"{method}{req_id}{api_key}{cryptocom_param_string(params)}{nonce}"
    return hmac.new(secret.encode(), payload.encode(), hashlib.sha256).hexdigest()


class CryptoComLive:
    venue = "cryptocom"
    asset_class = AssetClass.CRYPTO

    def __init__(self, auth: LiveAuthorization, api_key: str | None = None, secret: str | None = None,
                 base: str = CRYPTOCOM_API, transport: Transport = urllib_transport, clock: Callable[[], float] = time.time):
        if auth.venue != self.venue:
            raise ValueError("authorization is for a different venue")
        self.auth = auth
        self.key = api_key or os.environ.get("CRYPTOCOM_API_KEY", "")
        self._secret = secret or os.environ.get("CRYPTOCOM_API_SECRET", "")
        if not self.key or not self._secret:
            raise ValueError("CRYPTOCOM_API_KEY and CRYPTOCOM_API_SECRET must be set")
        self.base, self.t, self.clock = base.rstrip("/"), transport, clock
        self._id = int(clock() * 1000) % 1_000_000_000
        self._ticks: dict[str, tuple[float, float]] = {}

    def __repr__(self):  # never print the secret
        return f"CryptoComLive(key=...{self.key[-4:]})"

    def _private(self, method: str, params: dict) -> dict:
        self._id += 1
        nonce = int(self.clock() * 1000)
        body = {"id": self._id, "method": method, "api_key": self.key, "params": params, "nonce": nonce,
                "sig": cryptocom_sign(self._secret, method, self._id, self.key, params, nonce)}
        status, js = self.t("POST", f"{self.base}/{method}", {}, body)
        if status >= 300 or js.get("code", 0) != 0:
            raise BrokerError(status, js)
        return js.get("result", {})

    def ticks(self, instrument: str) -> tuple[float, float]:
        """(price_tick, qty_tick) from the public instrument list, cached."""
        if instrument not in self._ticks:
            status, js = self.t("GET", f"{self.base}/public/get-instruments", {}, None)
            if status >= 300:
                raise BrokerError(status, js)
            for d in js.get("result", {}).get("data", []):
                if d.get("symbol") == instrument:
                    self._ticks[instrument] = (float(d["price_tick_size"]), float(d["qty_tick_size"]))
                    break
            else:
                raise ValueError(f"{instrument} not listed on Crypto.com Exchange")
        return self._ticks[instrument]

    def create_ioc_limit(self, client_id: str, instrument: str, side: str, qty: float, limit_price: float) -> dict:
        ptick, qtick = self.ticks(instrument)
        q = _round_step(qty, qtick)
        if q <= 0:
            raise ValueError("quantity rounds to zero at this instrument's lot size")
        # round the limit away from the touch is NOT allowed: round toward it so the cap holds
        px = _round_step(limit_price, ptick) if side == "buy" else _round_step(limit_price + ptick - 1e-12, ptick)
        params = {"instrument_name": instrument, "side": side.upper(), "type": "LIMIT", "price": _num(px),
                  "quantity": _num(q), "client_oid": client_id[:36], "time_in_force": "IMMEDIATE_OR_CANCEL"}
        return self._private("private/create-order", params)

    def order_detail(self, client_id: str) -> dict:
        return self._private("private/get-order-detail", {"client_oid": client_id[:36]})

    # Protected entry (docs: private/advanced/create-otoco). The two trigger legs only take effect once the
    # primary is FULLY executed, so the primary is FILL_OR_KILL: either the whole position opens with its
    # stop and target already resting on the exchange, or nothing opens.
    @staticmethod
    def leg_ids(client_id: str) -> tuple[str, str, str]:
        base = client_id[:32]
        return base, base + "-sl", base + "-tp"

    def create_protected(self, client_id: str, instrument: str, side: str, qty: float, limit_price: float,
                         stop_price: float, take_price: float) -> dict:
        ptick, qtick = self.ticks(instrument)
        q = _round_step(qty, qtick)
        if q <= 0:
            raise ValueError("quantity rounds to zero at this instrument's lot size")
        rnd = lambda x: _num(_round_step(x, ptick))
        px = _round_step(limit_price, ptick) if side == "buy" else _round_step(limit_price + ptick - 1e-12, ptick)
        exit_side = "SELL" if side == "buy" else "BUY"
        primary, sl, tp = self.leg_ids(client_id)
        order_list = [
            {"instrument_name": instrument, "side": side.upper(), "type": "LIMIT", "price": _num(px), "quantity": _num(q),
             "client_oid": primary, "time_in_force": "FILL_OR_KILL"},
            {"instrument_name": instrument, "side": exit_side, "type": "STOP_LOSS", "quantity": _num(q), "ref_price": rnd(stop_price),
             "ref_price_type": "LAST_PRICE", "client_oid": sl},
            {"instrument_name": instrument, "side": exit_side, "type": "TAKE_PROFIT", "quantity": _num(q), "ref_price": rnd(take_price),
             "ref_price_type": "LAST_PRICE", "client_oid": tp},
        ]
        return self._private("private/advanced/create-otoco", {"order_list": order_list})

    def advanced_detail(self, client_oid: str) -> dict:
        return self._private("private/advanced/get-order-detail", {"client_oid": client_oid[:36]})

    def cancel_protection(self, instrument: str) -> None:
        self._private("private/advanced/cancel-all-orders", {"instrument_name": instrument, "type": "ALL"})

    def cancel_all(self, instrument: str | None = None) -> None:
        self._private("private/cancel-all-orders", {"instrument_name": instrument} if instrument else {})

    def balances(self) -> dict[str, float]:
        res = self._private("private/user-balance", {})
        data = (res.get("data") or [{}])[0]
        return {b["instrument_name"]: float(b["quantity"]) for b in data.get("position_balances", [])}

    def available(self) -> dict[str, float]:
        """Quantity minus reserved_qty (held by open orders): what new orders can actually use."""
        res = self._private("private/user-balance", {})
        data = (res.get("data") or [{}])[0]
        return {b["instrument_name"]: float(b["quantity"]) - float(b.get("reserved_qty") or 0)
                for b in data.get("position_balances", [])}


# ---------------------------------------------------------- Alpaca / OANDA live


class AlpacaLive:
    venue = "alpaca"
    asset_class = AssetClass.STOCK

    def __init__(self, auth: LiveAuthorization, key_id: str | None = None, secret: str | None = None,
                 transport: Transport = urllib_transport):
        if auth.venue != self.venue:
            raise ValueError("authorization is for a different venue")
        self.auth, self.base, self.t = auth, ALPACA_LIVE, transport
        self.h = {"APCA-API-KEY-ID": key_id or os.environ.get("APCA_LIVE_KEY_ID", ""),
                  "APCA-API-SECRET-KEY": secret or os.environ.get("APCA_LIVE_SECRET_KEY", "")}
        if not all(self.h.values()):
            raise ValueError("APCA_LIVE_KEY_ID and APCA_LIVE_SECRET_KEY must be set")

    def _call(self, method, path, body=None, ok404=False):
        status, js = self.t(method, self.base + path, self.h, body)
        if ok404 and status == 404:
            return None
        if status >= 300:
            raise BrokerError(status, js)
        return js

    def create_ioc_limit(self, client_id: str, inst: Instrument, side: str, qty: float, limit_price: float) -> dict:
        # Alpaca accepts fractional quantities only on market / day orders (docs: postorder), so an
        # IOC limit is always whole shares, rounded down.
        q = float(int(normalize_qty(inst, qty) + 1e-9))
        if q <= 0:
            raise ValueError("quantity rounds to zero whole shares; IOC limit orders can't be fractional on Alpaca")
        px = f"{limit_price:.4f}" if limit_price < 1 else f"{limit_price:.2f}"
        return self._call("POST", "/v2/orders", {"symbol": inst.symbol, "qty": _num(q), "side": side, "type": "limit",
                                                 "limit_price": px, "time_in_force": "ioc", "client_order_id": client_id})

    def attach_exit(self, client_id: str, inst: Instrument, exit_side: str, qty: float, stop_price: float, take_price: float) -> dict:
        """OCO exit (limit take-profit + stop-loss), sent the moment the IOC entry is confirmed. Alpaca
        only accepts bracket entries as day/gtc, so this is the closest it gets to open-with-protection."""
        fmt = lambda x: f"{x:.4f}" if x < 1 else f"{x:.2f}"
        return self._call("POST", "/v2/orders", {"symbol": inst.symbol, "qty": _num(float(int(qty + 1e-9))), "side": exit_side,
                                                 "type": "limit", "time_in_force": "gtc", "order_class": "oco",
                                                 "take_profit": {"limit_price": fmt(take_price)},
                                                 "stop_loss": {"stop_price": fmt(stop_price)}, "client_order_id": client_id + "-x"})

    def order_detail(self, client_id: str) -> dict:
        js = self._call("GET", "/v2/orders:by_client_order_id?client_order_id=" + urllib.parse.quote(client_id))
        st = {"filled": "FILLED", "canceled": "CANCELED", "expired": "EXPIRED", "rejected": "REJECTED"}.get(js.get("status"), "ACTIVE")
        if st == "CANCELED" and float(js.get("filled_qty") or 0) > 0:
            st = "FILLED"  # IOC partial: the remainder is cancelled, the filled part stands
        return {"status": st, "cumulative_quantity": js.get("filled_qty") or "0",
                "avg_price": js.get("filled_avg_price") or "0", "cumulative_fee": "0"}

    def cancel_all(self, instrument: str | None = None) -> None:
        self._call("DELETE", "/v2/orders")

    def position(self, symbol: str) -> float:
        js = self._call("GET", f"/v2/positions/{urllib.parse.quote(symbol)}", ok404=True)
        return float(js.get("qty", 0)) if js else 0.0

    def cash(self) -> float:
        return float(self._call("GET", "/v2/account").get("cash", 0))

    def clock_open(self) -> bool:
        return bool(self._call("GET", "/v2/clock").get("is_open"))


class OandaLive:
    venue = "oanda"
    asset_class = AssetClass.FOREX

    def __init__(self, auth: LiveAuthorization, account_id: str | None = None, token: str | None = None,
                 transport: Transport = urllib_transport):
        if auth.venue != self.venue:
            raise ValueError("authorization is for a different venue")
        self.auth, self.base, self.t = auth, OANDA_LIVE, transport
        self.account = account_id or os.environ.get("OANDA_LIVE_ACCOUNT_ID", "")
        tok = token or os.environ.get("OANDA_LIVE_TOKEN", "")
        if not self.account or not tok:
            raise ValueError("OANDA_LIVE_ACCOUNT_ID and OANDA_LIVE_TOKEN must be set")
        self.h = {"Authorization": "Bearer " + tok}
        self._fills: dict[str, dict] = {}

    def _call(self, method, path, body=None, ok404=False):
        status, js = self.t(method, f"{self.base}/v3/accounts/{self.account}{path}", self.h, body)
        if ok404 and status == 404:
            return None
        if status >= 300:
            raise BrokerError(status, js)
        return js

    @staticmethod
    def _px(p: float) -> str:
        return f"{p:.5f}" if p < 50 else f"{p:.3f}"

    def create_ioc_limit(self, client_id: str, inst: Instrument, side: str, qty: float, limit_price: float,
                         bracket: tuple[float, float] | None = None) -> dict:
        units = int(normalize_qty(inst, qty))
        if units <= 0:
            raise ValueError("units round to zero")
        order = {"type": "LIMIT", "instrument": inst.symbol, "units": str(units if side == "buy" else -units),
                 "price": self._px(limit_price), "timeInForce": "IOC", "positionFill": "DEFAULT",
                 "clientExtensions": {"id": client_id}}
        if bracket:  # (stop price, take price): attached by OANDA on fill, atomically with the entry
            order["stopLossOnFill"] = {"price": self._px(bracket[0])}
            order["takeProfitOnFill"] = {"price": self._px(bracket[1])}
        js = self._call("POST", "/orders", {"order": order})
        f = js.get("orderFillTransaction")
        self._fills[client_id] = (
            {"status": "FILLED", "cumulative_quantity": str(abs(float(f["units"]))), "avg_price": f["price"],
             "cumulative_fee": str(abs(float(f.get("commission", 0))) + abs(float(f.get("financing", 0))))}
            if f else {"status": "CANCELED", "cumulative_quantity": "0", "avg_price": "0", "cumulative_fee": "0"})
        return js

    def order_detail(self, client_id: str) -> dict:
        if client_id in self._fills:
            return self._fills[client_id]
        js = self._call("GET", f"/orders/@{urllib.parse.quote(client_id)}", ok404=True)
        if js is None:
            return {"status": "REJECTED", "cumulative_quantity": "0", "avg_price": "0", "cumulative_fee": "0"}
        o = js.get("order", {})
        st = {"FILLED": "FILLED", "CANCELLED": "CANCELED", "PENDING": "ACTIVE"}.get(o.get("state"), "ACTIVE")
        return {"status": st, "cumulative_quantity": "0", "avg_price": "0", "cumulative_fee": "0"}  # fill details unknown: reconcile

    def cancel_all(self, instrument: str | None = None) -> None:
        for o in self._call("GET", "/pendingOrders").get("orders", []):
            self._call("PUT", f"/orders/{o['id']}/cancel")

    def position(self, symbol: str) -> float:
        js = self._call("GET", f"/positions/{symbol}", ok404=True)
        if not js:
            return 0.0
        p = js.get("position", {})
        return float(p.get("long", {}).get("units", 0)) + float(p.get("short", {}).get("units", 0))


# ------------------------------------------------------------------- LiveVenue


@dataclass
class Baseline:
    """Holdings at first start. The bot's position is measured relative to this."""
    base_qty: float
    recorded_ms: int

    @staticmethod
    def load_or_record(path: Path, current_qty: float) -> "Baseline":
        if path.exists():
            return Baseline(**json.loads(path.read_text()))
        b = Baseline(current_qty, int(time.time() * 1000))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(b.__dict__))
        return b


class LiveVenue:
    """Adapts a live client to the SimVenue interface used by OrderManager."""

    def __init__(self, client, instrument: str, broker: PaperBroker, baseline_path: Path,
                 inst: Instrument | None = None, max_slip_bps: float = 15.0, poll_s: float = 0.5, max_polls: int = 20,
                 on_event: Callable[[str, dict], None] | None = None, sleep: Callable[[float], None] = time.sleep,
                 long_only: bool | None = None, protect: bool = True):
        self.client, self.instrument, self.broker, self.inst = client, instrument, broker, inst
        self.canary = CanaryGuard(client.auth.caps)
        self.max_slip_bps, self.poll_s, self.max_polls = max_slip_bps, poll_s, max_polls
        self.on_event = on_event or (lambda kind, data: None)
        self.sleep = sleep
        self.sent: list[str] = []
        self.long_only = long_only if long_only is not None else not isinstance(client, OandaLive)
        self.baseline = Baseline.load_or_record(baseline_path, self._raw_position())
        # Exchange-side protection: the stop and target rest on the venue from the moment the entry fills,
        # so a crash, a network partition or a dead loop cannot leave a position naked.
        self.protect = protect
        self.protection_path = Path(baseline_path).with_name("protection.json")
        self.protection: dict | None = json.loads(self.protection_path.read_text()) if self.protection_path.exists() else None

    def _save_protection(self):
        if self.protection is None:
            self.protection_path.unlink(missing_ok=True)
        else:
            self.protection_path.write_text(json.dumps(self.protection))

    def protected(self, instrument: str | None = None) -> bool:
        return self.protection is not None

    # positions -------------------------------------------------------------
    def _base_asset(self) -> str:
        return self.instrument.split("_")[0]

    def _raw_position(self) -> float:
        if isinstance(self.client, CryptoComLive):
            return self.client.balances().get(self._base_asset(), 0.0)
        return self.client.position(self.inst.symbol if self.inst else self.instrument)

    def position_qty(self, instrument: str) -> float:
        return self._raw_position() - self.baseline.base_qty

    # orders ----------------------------------------------------------------
    def place(self, client_id: str, ts: int, instrument: str, side: str, qty: float, ref: float, reason: str,
              bracket: tuple[float, float] | None = None) -> VenueReply:
        self.sent.append(client_id)
        pos = self.broker.position(instrument).qty
        equity = self.broker.equity({instrument: ref})
        why = self.canary.check(side, qty, ref, pos, equity, ts)
        if why:
            return VenueReply("rejected", 0.0, None, "canary cap: " + "; ".join(why))
        signed = qty if side == "buy" else -qty
        if self.long_only and pos + signed < -1e-12 and not (pos > 0):
            return VenueReply("rejected", 0.0, None, f"{self.client.venue} is long-only here (spot / no margin); short refused")
        # reduce-only orders may not exceed the bot's own position
        if pos and pos * signed < 0 and abs(signed) > abs(pos):
            qty = abs(pos)
        cap = self.max_slip_bps / 1e4
        limit = ref * (1 + cap) if side == "buy" else ref * (1 - cap)
        target = self.inst or instrument
        opening = pos == 0 or pos * signed > 0
        prot = None
        if self.protect and bracket and opening and self.protection is None:
            sgn = 1 if side == "buy" else -1
            prot = (ref * (1 - sgn * bracket[0]), ref * (1 + sgn * bracket[1]))  # stop price, take price
        detail = self.client.order_detail
        try:
            if prot and isinstance(self.client, CryptoComLive):
                self.client.create_protected(client_id, target, side, qty, limit, *prot)
                detail = lambda cid: self.client.advanced_detail(CryptoComLive.leg_ids(cid)[0])
            elif prot and isinstance(self.client, OandaLive):
                self.client.create_ioc_limit(client_id, target, side, qty, limit, bracket=prot)
            else:
                self.client.create_ioc_limit(client_id, target, side, qty, limit)
        except (BrokerError, ValueError) as e:
            self.on_event("order_rejected", {"client_id": client_id, "error": str(e)[:300]})
            return VenueReply("rejected", 0.0, None, str(e)[:200])
        self.canary.record()
        d = {}
        for _ in range(self.max_polls):
            d = detail(client_id)
            if d.get("status") in TERMINAL:
                break
            self.sleep(self.poll_s)
        else:
            # outcome unknown: leave the ledger in "sent" so reconciliation catches it
            self.client.cancel_all(instrument)
            raise TimeoutError(f"order {client_id} did not reach a final state; cancelled all, reconcile before trading")
        filled = float(d.get("cumulative_quantity") or 0)
        if filled <= 0:
            return VenueReply("rejected", 0.0, None, f"no fill ({d.get('status')}: IOC limit {limit:.6g} not reached)")
        px = float(d.get("avg_price") or ref)
        fee = abs(float(d.get("cumulative_fee") or 0))
        fee_ccy = d.get("fee_instrument_name")
        book_qty, fee_quote = filled, fee
        if fee and fee_ccy == self._base_asset():  # fee taken in the coin: you receive less of it
            fee_quote = fee * px
            book_qty = filled - fee if side == "buy" else filled
        fill = self.broker.apply_fill(ts, instrument, side, book_qty, px, fee_quote, reason)
        self.on_event("fill", {"client_id": client_id, "side": side, "qty": filled, "price": px, "fee": fee_quote})
        if prot:
            legs = {}
            if isinstance(self.client, CryptoComLive):
                _, sl, tp = CryptoComLive.leg_ids(client_id)
                legs = {"stop": sl, "take": tp}
            elif isinstance(self.client, AlpacaLive):
                try:
                    self.client.attach_exit(client_id, target, "sell" if side == "buy" else "buy", book_qty, *prot)
                    legs = {"oco": client_id + "-x"}
                except (BrokerError, ValueError) as e:  # no exchange stop: say so loudly; the loop's own stop still runs
                    self.on_event("protection_failed", {"client_id": client_id, "error": str(e)[:300]})
                    prot = None
            if prot:
                self.protection = {"instrument": instrument, "side": side, "qty": book_qty, "stop_px": prot[0], "take_px": prot[1],
                                   "legs": legs, "opened_ms": ts}
                self._save_protection()
                self.on_event("protected", {"client_id": client_id, "stop_px": prot[0], "take_px": prot[1]})
        return VenueReply("filled" if filled >= qty * (1 - 1e-6) else "partial", filled, fill)

    def sync_protection(self, ts: int, mark: float) -> dict | None:
        """Mirror an exchange-side stop or target fill into the local book. Crypto.com reports the leg's
        real fill; Alpaca and OANDA are detected from the position going flat (price approximated by mark)."""
        p = self.protection
        if p is None:
            return None
        exit_side = "sell" if p["side"] == "buy" else "buy"
        local = self.broker.position(p["instrument"]).qty
        if abs(local) < 1e-12:  # closed by us (kill / flatten): drop the stale record
            self.protection = None
            self._save_protection()
            return None
        if isinstance(self.client, CryptoComLive):
            for which in ("stop", "take"):
                d = self.client.advanced_detail(p["legs"][which])
                q = float(d.get("cumulative_quantity") or 0)
                if d.get("status") == "FILLED" and q > 0:
                    px = float(d.get("avg_price") or mark)
                    fee = abs(float(d.get("cumulative_fee") or 0))
                    if d.get("fee_instrument_name") == self._base_asset():
                        fee *= px
                    self.broker.apply_fill(ts, p["instrument"], exit_side, min(q, abs(local)), px, fee, f"exchange {which}")
                    self.protection = None
                    self._save_protection()
                    out = {"exit": which, "qty": q, "price": px, "fee": fee, "approximated": False}
                    self.on_event("protected_exit", out)
                    return out
            return None
        if abs(self.position_qty(p["instrument"])) <= max(1e-9, abs(local) * 1e-6):
            which = "stop" if (mark <= p["stop_px"]) == (p["side"] == "buy") else "take"
            px = p["stop_px"] if which == "stop" else p["take_px"]
            self.broker.apply_fill(ts, p["instrument"], exit_side, abs(local), px, 0.0, f"exchange {which} (price from bracket level)")
            self.protection = None
            self._save_protection()
            out = {"exit": which, "qty": abs(local), "price": px, "fee": 0.0, "approximated": True}
            self.on_event("protected_exit", out)
            return out
        return None

    def cancel_all(self):
        if self.protection is not None and isinstance(self.client, CryptoComLive):
            self.client.cancel_protection(self.instrument)  # trigger legs are advanced orders: cancelled separately
        self.client.cancel_all(self.instrument)

    def reconcile(self) -> list[str]:
        """Local mirror vs venue. Any gap above one lot is a problem."""
        local = self.broker.position(self.instrument).qty
        venue = self.position_qty(self.instrument)
        tol = max(1e-9, abs(local) * 1e-6, (self.inst.fx_lot_units if self.inst and self.inst.asset_class is AssetClass.FOREX else 0))
        return [] if abs(local - venue) <= tol else [f"position mismatch: local {local}, venue {venue} (net of baseline)"]


def build_live_client(venue: str, auth: LiveAuthorization | None = None):
    """The only constructor the runner uses. Authorization is checked here."""
    auth = auth or require_live(venue)
    if venue == "cryptocom":
        return CryptoComLive(auth)
    if venue == "alpaca":
        return AlpacaLive(auth)
    if venue == "oanda":
        return OandaLive(auth)
    raise ValueError(f"unknown live venue {venue}; choose cryptocom, alpaca or oanda")


__all__ = ["CryptoComLive", "AlpacaLive", "OandaLive", "LiveVenue", "Baseline", "build_live_client",
           "cryptocom_sign", "cryptocom_param_string", "OrderRequest"]
