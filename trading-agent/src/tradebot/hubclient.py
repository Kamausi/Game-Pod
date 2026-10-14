"""Python side of the Go execution hub (services/exechub).

`HubGuard` has the same `submit(...)` signature as GuardedBroker/OrderManager,
so the Agent can route orders through the hub with `--exechub host:port`.
Python's own risk check still runs first; the hub then applies its own,
independent checks. Either one can refuse.
"""
from __future__ import annotations

import time

import grpc

from .broker import PaperBroker
from .pb import trading_pb2 as pb
from .pb import trading_pb2_grpc as rpc
from .risk import RiskManager, Verdict
from .types import Fill

SIDES = {"buy": pb.BUY, "sell": pb.SELL}
CLASSES = {"crypto": pb.CRYPTO, "stock": pb.STOCK, "forex": pb.FOREX}


class HubClient:
    def __init__(self, addr: str = "127.0.0.1:50051", timeout_s: float = 2.0):
        self.channel = grpc.insecure_channel(addr)
        self.stub = rpc.ExecutionHubStub(self.channel)
        self.timeout = timeout_s

    def submit(self, client_order_id: str, asset_id: str, asset_class: str, tier: str, side: str, qty: float,
               bid: float, ask: float, quote_ts_ms: int, equity: float, order_type: str = "market",
               limit_price: float = 0.0, reduce_only: bool = False, strategy_id: str = "tradebot") -> pb.OrderResult:
        return self.stub.Submit(pb.OrderIntent(
            client_order_id=client_order_id, asset_id=asset_id, asset_class=CLASSES[asset_class], tier=tier,
            side=SIDES[side], order_type=pb.LIMIT if order_type == "limit" else pb.MARKET, quantity=qty,
            limit_price=limit_price, ref_bid=bid, ref_ask=ask, quote_ts_ms=quote_ts_ms, equity=equity,
            reduce_only=reduce_only, strategy_id=strategy_id, sent_ns=time.time_ns()), timeout=self.timeout)

    def halt(self, reason: str):
        return self.stub.Halt(pb.HaltRequest(reason=reason), timeout=self.timeout)

    def resume(self, operator: str):
        return self.stub.Resume(pb.ResumeRequest(operator=operator), timeout=self.timeout)

    def state(self):
        return self.stub.State(pb.StateRequest(), timeout=self.timeout)

    def events(self):
        return self.stub.StreamEvents(pb.StreamRequest())


class HubGuard:
    """Drop-in order path for Agent: local risk check, then the hub, then mirror the fill locally."""

    def __init__(self, client: HubClient, broker: PaperBroker, risk: RiskManager, asset_class: str = "crypto", tier: str = "crypto_major"):
        self.client, self.broker, self.risk = client, broker, risk
        self.asset_class, self.tier = asset_class, tier

    def submit(self, ts, instrument, side, qty, ref_price, equity, data_ts, spread_bps, reason, client_id=None, bracket=None):
        pos = self.broker.position(instrument)
        v = self.risk.check_order(side, qty, ref_price, pos.qty, equity, ts, data_ts, spread_bps)
        if not v.allowed:
            return v, None
        half = ref_price * spread_bps / 2e4
        bid, ask = (ref_price - 2 * half, ref_price) if side == "buy" else (ref_price, ref_price + 2 * half)
        cid = client_id or f"{instrument}:{ts}:{side}"
        reducing = pos.qty != 0 and (pos.qty > 0) == (side == "sell") and v.qty <= abs(pos.qty) + 1e-12
        try:
            r = self.client.submit(cid, instrument, self.asset_class, self.tier, side, v.qty, bid, ask, data_ts, equity, reduce_only=reducing)
        except grpc.RpcError as e:
            return Verdict(False, 0.0, (f"execution hub unreachable: {e.code().name}",)), None
        if r.status == pb.DUPLICATE:
            return Verdict(False, 0.0, (f"duplicate client id {cid}; hub did not resend",)), None
        if r.status != pb.FILLED:
            return Verdict(False, 0.0, tuple(r.reasons) or ("hub did not fill",)), None
        # mirror the hub's fill into the local paper book so equity/stops stay in sync
        fill = self.broker.market(ts, instrument, side, r.filled_qty, r.avg_price, reason, slip_bps=0.0)  # hub price already includes slippage
        self.risk.record_order()
        return Verdict(True, r.filled_qty, v.reasons + (f"hub {r.hub_latency_us} µs",)), fill
