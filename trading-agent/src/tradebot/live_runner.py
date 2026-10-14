"""24/7 live loop. Same decision path as paper; real orders through LiveVenue.

On top of the paper loop:
  - refuses to start without a current go-live authorization for the venue,
    and re-checks it every 5 minutes (expiry, edited caps/limits/engine -> stop)
  - capital the bot may use = min(venue cash, canary max position), set once
  - reconciles the local book against the venue every `reconcile_every` ticks;
    any mismatch trips the kill switch
  - on kill: cancel all open orders, flatten the bot's position (reduce-only),
    alert, and keep watching. It never re-opens until a human runs `unkill`
  - alerts on start, fills, rejects, kills and a daily summary
  - respects market hours for stocks and forex
"""
from __future__ import annotations

import json
import time
from pathlib import Path

from .agent import Agent
from .alerts import Alerts
from .assets import AssetClass, Instrument, classify_stock, is_open
from .broker import PaperBroker
from .engines import build_engine
from .execution import OrderManager
from .journal import Journal
from .live import NotAuthorized, require_live
from .loop import MAX_CONSECUTIVE_ERRORS, _load_broker, _save_broker
from .policy import Calibrator, PolicyParams
from .risk import RiskLimits, RiskManager
from .venues_live import CryptoComLive, LiveVenue, build_live_client

REAUTH_MS = 5 * 60_000
VENUE_CLASS = {"cryptocom": AssetClass.CRYPTO, "alpaca": AssetClass.STOCK, "oanda": AssetClass.FOREX}


def make_feed(venue: str, instrument: str, timeframe: str):
    from .feed import AlpacaStockFeed, CryptoComFeed, OandaFxFeed

    return {"cryptocom": CryptoComFeed, "alpaca": AlpacaStockFeed, "oanda": OandaFxFeed}[venue](instrument, timeframe)


def make_instrument(venue: str, instrument: str, price: float) -> Instrument:
    if venue == "alpaca":
        return classify_stock(instrument, price)
    if venue == "oanda":
        return Instrument(instrument, AssetClass.FOREX, "forex_major", qty_step=1.0)
    major = instrument.split("_")[0] in ("BTC", "ETH", "SOL")
    return Instrument(instrument, AssetClass.CRYPTO, "crypto_major" if major else "crypto_high_vol")


def venue_cash(client, instrument: str) -> float:
    if isinstance(client, CryptoComLive):
        quote = instrument.split("_")[1]
        return client.balances().get(quote, 0.0)
    if hasattr(client, "cash"):
        return client.cash()
    return float(client._call("GET", "/summary")["account"]["balance"])  # OANDA


def flatten(om: OrderManager, instrument: str, ts: int, bid: float, ask: float, data_ts: int, spread: float, why: str):
    pos = om.broker.position(instrument).qty
    if abs(pos) < 1e-12:
        return None
    side = "sell" if pos > 0 else "buy"
    ref = bid if side == "sell" else ask
    return om.submit(ts, instrument, side, abs(pos), ref, om.broker.equity({instrument: ref}), data_ts, spread,
                     "kill_flatten", client_id=f"{instrument}:{ts}:flatten:{side}")


def run_live(venue: str, instrument: str, timeframe: str, engine_kind: str, run_dir: Path, params: PolicyParams,
             limits: RiskLimits, release: dict, poll_s: float = 15.0, reconcile_every: int = 8,
             max_ticks: int | None = None, client=None, feed=None, alerts: Alerts | None = None,
             now_fn=lambda: int(time.time() * 1000), sleep=time.sleep):
    auth = require_live(venue)  # raises NotAuthorized
    run_dir.mkdir(parents=True, exist_ok=True)
    journal = Journal(run_dir / "journal.jsonl")
    alerts = alerts or Alerts(journal=journal, tag=f"{venue}:{instrument}")
    client = client or build_live_client(venue, auth)
    feed = feed or make_feed(venue, instrument, timeframe)
    risk = RiskManager(limits, run_dir / "KILL", run_dir / "risk_state.json")

    bpath = run_dir / "broker.json"
    if not bpath.exists():
        capital = min(venue_cash(client, instrument), auth.caps.max_position_usd)
        if capital <= 0:
            raise NotAuthorized(f"no cash available on {venue}")
        _save_broker(PaperBroker(cash=capital, fee_bps=0.0, slippage_bps=0.0), bpath)
    broker = _load_broker(bpath, 0.0)
    broker.fee_bps = broker.slippage_bps = 0.0  # real fees come from the venue

    first_book = feed.book()
    inst = make_instrument(venue, instrument, first_book.mid)
    on_event = lambda kind, data: (journal.write(kind, now_fn(), **data),
                                   alerts("info" if kind == "fill" else "warn", f"{kind} {instrument}", **data))
    lv = LiveVenue(client, instrument, broker, run_dir / "baseline.json", inst=None if venue == "cryptocom" else inst,
                   on_event=on_event)
    om = OrderManager(lv, risk, run_dir / "orders.jsonl")
    rec = om.recover(instrument)
    rec_venue = lv.reconcile()
    if rec_venue and not risk.is_killed():
        risk.kill("startup reconciliation: " + "; ".join(rec_venue))
    journal.write("startup", now_fn(), live=True, venue=venue, release=release, reconciliation=rec,
                  venue_reconciliation=rec_venue, caps=auth.record["caps"], unproven_accepted=auth.record["unproven_accepted"],
                  authorization_expires_ms=auth.record["expires_ms"], capital=broker.cash)
    alerts("critical" if auth.record["unproven_accepted"] else "info", f"LIVE start {venue} {instrument}",
           release=release.get("candidate_id"), capital=round(broker.cash, 2), caps=auth.record["caps"],
           unproven=auth.record["unproven_accepted"] or "no", reconciliation="ok" if rec["ok"] and not rec_venue else "FAILED")

    engine = build_engine(engine_kind, risk)
    from .order_review import build_reviewer

    agent = Agent(instrument, engine, broker, risk, params, Calibrator.load(run_dir / "calibration.json"), journal, guard=om,
                  reviewer=build_reviewer("code", run_dir / "reviewer_scores.json", "block"))
    last_bar, errors, tick, last_auth = 0, 0, 0, now_fn()
    flattened_for = None
    day = now_fn() // 86_400_000
    print(f"LIVE loop: {venue} {instrument} {timeframe} release={release.get('candidate_id')} caps={auth.record['caps']}"
          f"  (touch {run_dir / 'KILL'} to stop and flatten)")
    while max_ticks is None or tick < max_ticks:
        tick += 1
        now = now_fn()
        try:
            if now - last_auth >= REAUTH_MS:
                try:
                    require_live(venue)
                except NotAuthorized as e:
                    risk.kill(f"live authorization lost: {e}")
                last_auth = now
            book = feed.book()
            now = max(now, book.ts)
            spread = (book.best_ask - book.best_bid) / book.mid * 1e4
            m = book.mid
            if risk.is_killed():
                reason = risk.kill_reason()
                pos = broker.position(instrument).qty
                if flattened_for != reason or abs(pos) > 1e-12:  # retry every tick until flat
                    lv.cancel_all()
                    out = flatten(om, instrument, now, book.best_bid, book.best_ask, book.ts, spread, reason)
                    journal.write("kill_flatten", now, reason=reason, result=str(out)[:300])
                    if flattened_for != reason:
                        alerts("critical", f"KILL {instrument}: cancelled all, flattening", reason=reason,
                               position=broker.position(instrument).qty, equity=round(agent.equity(m), 2))
                    flattened_for = reason
            elif is_open(inst, now):
                agent.check_exits(now, m, m, m, (book.best_ask - book.best_bid) / 2, book.ts, spread)
                bar_close = now - now % feed.bar_ms
                if bar_close > last_bar:
                    candles = feed.closed_candles(now)
                    if candles and candles[-1].close_ts == bar_close:
                        _, d, intent = agent.observe(now, candles, book)
                        agent.act(intent, now, book.best_bid, book.best_ask, book.ts, spread)
                        last_bar = bar_close
            if tick % reconcile_every == 0:
                problems = lv.reconcile()
                if problems and not risk.is_killed():
                    risk.kill("reconciliation: " + "; ".join(problems))
                    alerts("critical", f"reconciliation failed {instrument}", problems=problems)
            if now // 86_400_000 != day:
                day = now // 86_400_000
                alerts("info", f"daily summary {instrument}", equity=round(agent.equity(m), 2),
                       position=broker.position(instrument).qty, killed=risk.kill_reason() or "no",
                       orders=sum(1 for r in om.ledger.values() if r.get("state") in ("filled", "partial")))
            _save_broker(broker, bpath)
            errors = 0
        except KeyboardInterrupt:
            raise
        except Exception as e:
            errors += 1
            journal.write("error", now, error=f"{type(e).__name__}: {e}"[:500], consecutive=errors)
            if isinstance(e, TimeoutError) or (errors >= MAX_CONSECUTIVE_ERRORS and not risk.is_killed()):
                risk.kill(f"{type(e).__name__}: {e}"[:200])
                alerts("critical", f"live loop halted {instrument}", error=f"{type(e).__name__}: {e}"[:300])
        sleep(poll_s)
    return {"ticks": tick, "equity": agent.equity(feed.book().mid) if max_ticks else None,
            "killed": risk.kill_reason(), "ledger": len(om.ledger)}


def json_dump(x) -> str:
    return json.dumps(x, indent=2, default=str)
