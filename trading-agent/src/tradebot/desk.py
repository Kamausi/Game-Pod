"""The desk: Jane Street's seven rules as seats, each a step that writes a file the next one reads.

  SCOUT    1% of the data: C-suite open-market Form 4 buys of $100K+ since the last close,
           liquid names only, at most 5. "NO TRADE TODAY" is a good result.
  ANALYST  one question per name: what is it worth? Fair value from the stock's OWN history of
           price-to-earnings (25th / median / 75th percentile x trailing EPS from SEC XBRL).
           Every number carries its source. No positive earnings or no history = NO RANGE.
  SKEPTIC  never proposes, only kills: unsourced key number, batch, 10b5-1 plan, EXPENSIVE,
           NO RANGE, earnings likely within 5 trading days, already moved. Unsure = KILL.
  RISK     turns PASS names into tickets: DAY limit buy 0.5% above the last close, stop
           2 x ATR(14) below entry, shares = equity x 1% / (entry - stop), checked by
           risk_check() against limits.json. KILL or PAUSE file = no tickets.
  PM       you. Nothing is placed until you approve tickets by number, the same day.
  JOURNAL  every ticket and every killed name, with what the killed ones would have made;
           a weekly review; DECAYING after 20+ trades if live R < half the backtest's.

Orders go to the Alpaca PAPER account only: the broker class refuses any other host, and
limits.json must say paper_only: true. A live account is a code change you make yourself.

Files (default ./desk):
  limits.json  lessons.md  KILL  PAUSE
  inbox/YYYY-MM-DD-{scout,analyst,skeptic}.{md,json}  inbox/YYYY-MM-DD-tickets.json  inbox/YYYY-MM-DD-brief.md
  journal/open.json  journal/trades.csv  journal/week-YYYY-WW.md
  quant/changes.md  quant/latest.json  quant/holdout.json
"""
from __future__ import annotations

import csv
import json
import math
import re
import statistics
import subprocess
import sys
from dataclasses import asdict
from datetime import date, datetime, timedelta
from pathlib import Path
from zoneinfo import ZoneInfo

from . import insider as ins
from .brokers import ALPACA_PAPER, BrokerError, LiveHostRefused, urllib_transport

NY = ZoneInfo("America/New_York")
DEFAULT_LIMITS = {"risk_per_trade_pct": 1.0, "max_open_positions": 5, "max_position_pct": 10,
                  "max_daily_loss_pct": 3, "paper_only": True}
CONCEPT = "https://data.sec.gov/api/xbrl/companyconcept/CIK{cik:0>10}/us-gaap/EarningsPerShareDiluted.json"


def today_ny() -> str:
    return datetime.now(NY).date().isoformat()


def prev_weekday(d: str) -> str:
    x = date.fromisoformat(d) - timedelta(days=1)
    while x.weekday() >= 5:
        x -= timedelta(days=1)
    return x.isoformat()


class Desk:
    def __init__(self, root: str | Path = "desk"):
        self.root = Path(root)

    def init(self) -> list[str]:
        made = []
        for d in ("inbox", "journal", "quant"):
            (self.root / d).mkdir(parents=True, exist_ok=True)
        for name, body in (("limits.json", json.dumps(DEFAULT_LIMITS, indent=2)), ("lessons.md", "# Lessons\n\n"),
                           ("quant/changes.md", "# Changes\n\nOne change at a time. Five at most before the holdout.\n\n")):
            p = self.root / name
            if not p.exists():
                p.write_text(body)
                made.append(str(p))
        return made

    def path(self, *parts) -> Path:
        return self.root.joinpath(*parts)

    def inbox(self, d: str, seat: str, ext: str) -> Path:
        return self.path("inbox", f"{d}-{seat}.{ext}")

    def limits(self) -> dict:
        p = self.path("limits.json")
        return json.loads(p.read_text()) if p.exists() else dict(DEFAULT_LIMITS)

    def lessons(self) -> str:
        p = self.path("lessons.md")
        return p.read_text() if p.exists() else ""

    def halted(self) -> str | None:
        for f in ("KILL", "PAUSE"):
            if self.path(f).exists():
                return f
        return None

    def write(self, d: str, seat: str, data, md: str) -> Path:
        self.path("inbox").mkdir(parents=True, exist_ok=True)
        self.inbox(d, seat, "json").write_text(json.dumps(data, indent=1, default=str))
        p = self.inbox(d, seat, "md")
        p.write_text(md)
        return p

    def read(self, d: str, seat: str):
        p = self.inbox(d, seat, "json")
        return json.loads(p.read_text()) if p.exists() else None


# ------------------------------------------------------------------ SCOUT


def scout(desk: Desk, d: str, get=ins.sec_get, prices=None, pages: int = 3, min_usd: float = 100_000,
          min_adv: float = 20e6, max_names: int = 5) -> dict:
    since = prev_weekday(d)
    filings, errors = ins.fetch_recent(get, pages=pages, since=since)
    sigs = ins.signals_from(filings, min_usd)
    rejected, names = [], []
    for s in sorted(sigs, key=lambda x: -x.usd):
        row = {**asdict(s), "issuer_cik": next((f.issuer_cik for f in filings if f.accession == s.accession), "")}
        if prices is not None:
            bars = prices.bars(s.symbol)
            liq = ins.adv(bars, len(bars) - 1) if bars else 0.0
            row["adv_usd"] = liq
            if liq < min_adv:
                rejected.append({**row, "why": "average daily dollar volume under $20M" if bars else "no price data: liquidity UNVERIFIED"})
                continue
        else:
            row["adv_usd"] = None
            row["flags"].append("liquidity UNVERIFIED (no price source)")
        names.append(row)
    passed, overflow = names[:max_names], names[max_names:]
    rejected += [{**r, "why": f"beyond the {max_names}-name limit"} for r in overflow]
    out = {"date": d, "since": since, "names": passed, "rejected": rejected, "errors": errors,
           "scanned_filings": len(filings), "c_suite_buys": len(sigs)}
    lines = [f"# Scout {d}", ""]
    if errors:
        lines += ["**Sources with problems:** " + "; ".join(errors[:5]), ""]
    lines.append(f"Scanned {len(filings)} Form 4 filings since {since}; {len(sigs)} C-suite open-market buys of ${min_usd:,.0f}+.")
    if not passed:
        lines += ["", "NO TRADE TODAY"]
    for r in passed:
        lines += ["", f"## {r['symbol']}: {r['owner']} ({r['role']}) bought ${r['usd']:,.0f}",
                  f"- filed {r['filed']}, filing: {r['url'] or r['accession']}",
                  f"- average daily dollar volume: {'$%.1fM' % (r['adv_usd'] / 1e6) if r['adv_usd'] else 'UNVERIFIED'}"]
        lines += [f"- FLAG: {f}" for f in r["flags"]]
    if rejected:
        lines += ["", "## Rejected", *[f"- {r['symbol']} ({r['owner']}): {r['why']}" for r in rejected]]
    desk.write(d, "scout", out, "\n".join(lines) + "\n")
    return out


# ------------------------------------------------------------------ ANALYST


def quarters(concept: dict) -> dict[tuple[int, int], dict]:
    """Discrete diluted-EPS quarters keyed (year, q). Companies report Q4 only inside the full
    year (frame CYyyyy), so Q4 = FY - Q1 - Q2 - Q3, sourced to the 10-K's accession number."""
    units = concept.get("units", {}).get("USD/shares", [])
    q, fy = {}, {}
    for f in units:
        fr = f.get("frame", "")
        if re.fullmatch(r"CY\d{4}Q[1-4]", fr):
            q[(int(fr[2:6]), int(fr[7]))] = {"val": f["val"], "end": f["end"], "source": f"accn {f['accn']} ({f['form']}, filed {f['filed']})"}
        elif re.fullmatch(r"CY\d{4}", fr):
            fy[int(fr[2:6])] = f
    for y, f in fy.items():
        if (y, 4) not in q and all((y, k) in q for k in (1, 2, 3)):
            q[(y, 4)] = {"val": round(f["val"] - sum(q[(y, k)]["val"] for k in (1, 2, 3)), 4), "end": f["end"],
                         "source": f"FY accn {f['accn']} minus Q1-Q3 (derived)"}
    return q


def _consecutive(keys: list[tuple[int, int]]) -> bool:
    return all((b[0] * 4 + b[1]) - (a[0] * 4 + a[1]) == 1 for a, b in zip(keys, keys[1:]))


def ttm_eps(concept: dict) -> tuple[float, list[dict]] | None:
    """Trailing twelve months diluted EPS: the four latest CONSECUTIVE quarters, each sourced."""
    q = quarters(concept)
    keys = sorted(q)[-4:]
    if len(keys) < 4 or not _consecutive(keys):
        return None
    return sum(q[k]["val"] for k in keys), [{"quarter": f"{k[0]}Q{k[1]}", **q[k]} for k in keys]


def eps_history(concept: dict) -> list[tuple[str, float]]:
    """(quarter end, TTM EPS) wherever four consecutive quarters are known."""
    q = quarters(concept)
    keys, out = sorted(q), []
    for i in range(3, len(keys)):
        w = keys[i - 3:i + 1]
        if _consecutive(w):
            out.append((q[w[-1]]["end"], sum(q[k]["val"] for k in w)))
    return out


def next_report_estimate(concept: dict) -> str | None:
    """Estimated next 10-Q/10-K filing date from the company's own cadence (ESTIMATED)."""
    filed = sorted({f["filed"] for f in concept.get("units", {}).get("USD/shares", []) if f.get("form") in ("10-Q", "10-K")})
    if len(filed) < 3:
        return None
    gaps = [(date.fromisoformat(b) - date.fromisoformat(a)).days for a, b in zip(filed, filed[1:])][-4:]
    return (date.fromisoformat(filed[-1]) + timedelta(days=int(statistics.median(gaps)))).isoformat()


def fair_value(concept: dict, bars: list[ins.DayBar], url: str) -> dict:
    tt = ttm_eps(concept)
    if not tt or tt[0] <= 0:
        return {"verdict": "NO RANGE, NO TRADE", "why": "no positive trailing EPS from SEC filings", "sources": [url]}
    hist = eps_history(concept)
    closes = {b.d: b.c for b in bars}
    pes = []
    for end, eps in hist[-12:]:
        if eps <= 0:
            continue
        px = next((closes[d] for d in sorted(closes, reverse=True) if d <= end), None)
        if px:
            pes.append(px / eps)
    if len(pes) < 4:
        return {"verdict": "NO RANGE, NO TRADE", "why": "fewer than 4 past quarters with positive EPS and a price", "sources": [url]}
    pes.sort()
    qn = lambda p: pes[min(len(pes) - 1, max(0, round(p * (len(pes) - 1))))]
    eps = tt[0]
    low, base, high = eps * qn(0.25), eps * statistics.median(pes), eps * qn(0.75)
    price = bars[-1].c
    return {"verdict": "EXPENSIVE" if price > base else "RANGE", "method": "own-history P/E (25th / median / 75th percentile, last 12 quarters) x trailing EPS",
            "low": low, "base": base, "high": high, "price": price, "gap_to_base": base / price - 1, "ttm_eps": eps,
            "facts": [{"claim": f"diluted EPS {f['quarter']}", "value": f["val"], "source": f"{url} {f['source']}"} for f in tt[1]]
            + [{"claim": "last close", "value": price, "source": f"daily bars {bars[-1].d}"}],
            "wrong_if": "earnings fall, or the market re-rates the stock below its own history (a lower P/E for a reason)"}


def analyst(desk: Desk, d: str, get=ins.sec_get, prices=None) -> dict:
    sc = desk.read(d, "scout") or {"names": []}
    cards, lines = [], [f"# Analyst {d}", "", "Lessons read first: " + ("yes" if desk.lessons().strip() else "none yet"), ""]
    for n in sc["names"]:
        card = {"symbol": n["symbol"]}
        try:
            url = CONCEPT.format(cik=str(n.get("issuer_cik") or "").lstrip("0"))
            if not n.get("issuer_cik"):
                raise ValueError("issuer CIK unknown")
            concept = json.loads(get(url))
            bars = prices.bars(n["symbol"]) if prices is not None else []
            if not bars:
                card.update(verdict="NO RANGE, NO TRADE", why="no price data", sources=[url])
            else:
                card.update(fair_value(concept, bars, url))
            card["next_report_estimate"] = next_report_estimate(concept)
        except Exception as e:
            card.update(verdict="NO RANGE, NO TRADE", why=f"source failed: {type(e).__name__}: {e}"[:160])
        cards.append(card)
        lines.append(f"## {n['symbol']}: {card['verdict']}")
        if "base" in card:
            lines += [f"- fair value {card['low']:.2f} / {card['base']:.2f} / {card['high']:.2f} ({card['method']})",
                      f"- price {card['price']:.2f}, gap to base {card['gap_to_base']:+.1%}",
                      *[f"- {f['claim']}: {f['value']} — {f['source']}" for f in card["facts"]], f"- wrong if: {card['wrong_if']}"]
        else:
            lines.append(f"- {card.get('why')}")
        lines.append(f"- next 10-Q/10-K (ESTIMATED from filing cadence): {card.get('next_report_estimate') or 'UNVERIFIED'}")
        lines.append("")
    out = {"date": d, "cards": cards}
    desk.write(d, "analyst", out, "\n".join(lines))
    return out


# ------------------------------------------------------------------ SKEPTIC


def skeptic(desk: Desk, d: str, prices=None, max_move: float = 0.05) -> dict:
    sc = desk.read(d, "scout") or {"names": []}
    an = {c["symbol"]: c for c in (desk.read(d, "analyst") or {"cards": []})["cards"]}
    verdicts = []
    for n in sc["names"]:
        c, why = an.get(n["symbol"]), None
        if c is None:
            why = "no analyst card"
        elif n.get("batch"):
            why = "batch filing (pay plan or program, not conviction)"
        elif n.get("plan_10b5_1"):
            why = "10b5-1 plan purchase (scheduled in advance)"
        elif n.get("adv_usd") is None:
            why = "liquidity UNVERIFIED"
        elif c["verdict"] != "RANGE":
            why = c["verdict"] + (": " + c["why"] if c.get("why") else "")
        else:
            est = c.get("next_report_estimate")
            if not est:
                why = "next earnings date UNVERIFIED"
            elif -3 <= (date.fromisoformat(est) - date.fromisoformat(d)).days <= 10:
                why = f"earnings likely within 5 trading days (estimated {est})"
            elif prices is not None:
                bars = prices.bars(n["symbol"])
                at = next((b.c for b in reversed(bars) if b.d <= n["filed"]), None)
                if at and bars and bars[-1].c / at - 1 > max_move:
                    why = f"already moved {bars[-1].c / at - 1:+.1%} since the filing: in the price"
            else:
                why = "move since filing UNVERIFIED"
        verdicts.append({"symbol": n["symbol"], "verdict": "KILL" if why else "PASS", "why": why or "every check passed"})
    out = {"date": d, "verdicts": verdicts}
    md = [f"# Skeptic {d}", ""] + [f"- {v['symbol']}: **{v['verdict']}** — {v['why']}" for v in verdicts]
    desk.write(d, "skeptic", out, "\n".join(md or ["NO NAMES"]) + "\n")
    return out


# ------------------------------------------------------------------ broker (paper only)


class PaperDeskBroker:
    """The few Alpaca PAPER calls the desk needs. Refuses any host but paper-api.alpaca.markets."""

    def __init__(self, base: str = ALPACA_PAPER, transport=urllib_transport, key_id: str | None = None, secret: str | None = None):
        import os
        if base.rstrip("/") != ALPACA_PAPER:
            raise LiveHostRefused(f"the desk trades the paper account only; refusing {base}")
        self.base, self.t = base.rstrip("/"), transport
        self.h = {"APCA-API-KEY-ID": key_id or os.environ.get("APCA_API_KEY_ID", ""),
                  "APCA-API-SECRET-KEY": secret or os.environ.get("APCA_API_SECRET_KEY", "")}
        self.paper = True

    def _call(self, method, path, body=None):
        status, js = self.t(method, self.base + path, self.h, body)
        if status >= 300:
            raise BrokerError(status, js)
        return js

    def account(self) -> dict:
        return self._call("GET", "/v2/account")

    def positions(self) -> list[dict]:
        return self._call("GET", "/v2/positions")

    def open_orders(self) -> list[dict]:
        return self._call("GET", "/v2/orders?status=open&nested=true&limit=500")

    def order(self, order_id: str) -> dict:
        return self._call("GET", f"/v2/orders/{order_id}?nested=true")

    def place_oto(self, client_id: str, symbol: str, qty: int, limit: float, stop: float) -> dict:
        """DAY limit buy with the stop attached (order_class oto, documented by Alpaca): the stop
        goes live the moment the entry fills. Unfilled by the close = cancelled by Alpaca."""
        fmt = lambda x: f"{x:.2f}" if x >= 1 else f"{x:.4f}"
        return self._call("POST", "/v2/orders", {"symbol": symbol, "qty": str(int(qty)), "side": "buy", "type": "limit",
                                                 "limit_price": fmt(limit), "time_in_force": "day", "order_class": "oto",
                                                 "stop_loss": {"stop_price": fmt(stop)}, "client_order_id": client_id})

    def cancel(self, order_id: str) -> None:
        self._call("DELETE", f"/v2/orders/{order_id}")

    def cancel_all(self) -> None:
        self._call("DELETE", "/v2/orders")

    def sell_market(self, client_id: str, symbol: str, qty: float) -> dict:
        return self._call("POST", "/v2/orders", {"symbol": symbol, "qty": str(qty), "side": "sell", "type": "market",
                                                 "time_in_force": "day", "client_order_id": client_id})


# ------------------------------------------------------------------ RISK


def make_ticket(no: int, symbol: str, bars: list[ins.DayBar], equity: float, limits: dict, d: str, notes: dict) -> dict | None:
    a = ins.atr(bars, len(bars) - 1)
    if not bars or a is None:
        return None
    entry = round(bars[-1].c * 1.005, 2)
    stop = round(entry - 2 * a, 2)
    per_share = entry - stop
    shares = math.floor(equity * limits["risk_per_trade_pct"] / 100 / per_share) if per_share > 0 else 0
    shares = min(shares, math.floor(equity * limits["max_position_pct"] / 100 / entry))
    return {"no": no, "date": d, "symbol": symbol, "shares": shares, "limit": entry, "stop": stop,
            "risk_usd": round(shares * per_share, 2), "value_usd": round(shares * entry, 2), "notes": notes}


def risk_check(t: dict, account: dict, positions: list[dict], open_orders: list[dict], limits: dict, desk: Desk, paper: bool) -> list[str]:
    """Every reason this ticket may not go out. Empty list = PASS. The bots can't edit this."""
    fails = []
    if (h := desk.halted()):
        fails.append(f"{h} file exists")
    if limits.get("paper_only", True) and not paper:
        fails.append("limits say paper_only and this broker is not the paper account")
    eq, last = float(account.get("equity", 0)), float(account.get("last_equity", 0) or 0)
    if t["shares"] < 1:
        fails.append("size rounds to 0 shares")
    if not t["stop"] < t["limit"]:
        fails.append("stop is not below the entry")
    if eq <= 0:
        fails.append("account equity unknown")
    else:
        if t["risk_usd"] > eq * limits["risk_per_trade_pct"] / 100 + 0.01:
            fails.append(f"risk ${t['risk_usd']:.2f} above {limits['risk_per_trade_pct']}% of equity")
        if t["value_usd"] > eq * limits["max_position_pct"] / 100 + 0.01:
            fails.append(f"position ${t['value_usd']:.2f} above {limits['max_position_pct']}% of equity")
        if last > 0 and (eq / last - 1) * 100 <= -limits["max_daily_loss_pct"]:
            fails.append(f"daily loss limit hit ({eq / last - 1:+.2%})")
    held = {p["symbol"] for p in positions}
    pending = {o["symbol"] for o in open_orders if o.get("side") == "buy"}
    if t["symbol"] in held | pending:
        fails.append("already held or an entry is working")
    if len(held | pending) >= limits["max_open_positions"]:
        fails.append(f"{limits['max_open_positions']} positions or entries already open")
    return fails


def risk(desk: Desk, d: str, broker, prices) -> dict:
    sk = desk.read(d, "skeptic") or {"verdicts": []}
    sc = {n["symbol"]: n for n in (desk.read(d, "scout") or {"names": []})["names"]}
    limits, halted = desk.limits(), desk.halted()
    tickets, failed = [], []
    if not halted:
        acct, pos, oo = broker.account(), broker.positions(), broker.open_orders()
        no = 0
        for v in sk["verdicts"]:
            if v["verdict"] != "PASS":
                continue
            no += 1
            t = make_ticket(no, v["symbol"], prices.bars(v["symbol"]), float(acct["equity"]), limits, d,
                            {"scout": str(desk.inbox(d, "scout", "md")), "analyst": str(desk.inbox(d, "analyst", "md")),
                             "skeptic": str(desk.inbox(d, "skeptic", "md")), "filing": sc.get(v["symbol"], {}).get("url")})
            if t is None:
                failed.append({"symbol": v["symbol"], "fails": ["no ATR(14): too little price history"]})
                continue
            f = risk_check(t, acct, pos, oo, limits, desk, getattr(broker, "paper", False))
            (failed.append({"symbol": v["symbol"], "fails": f}) if f else tickets.append(t))
    kills = [v for v in sk["verdicts"] if v["verdict"] == "KILL"]
    seen = len(sk["verdicts"])
    brief = [f"Desk saw {seen} name{'s' if seen != 1 else ''}" + (f" ({', '.join(v['symbol'] for v in sk['verdicts'])})" if seen else "") + ".",
             f"Killed {len(kills)}: " + ("; ".join(f"{v['symbol']} ({v['why']})" for v in kills) if kills else "none") + (f". Failed risk: {len(failed)}." if failed else "."),
             (f"No tickets: {halted} is on." if halted else f"Wants {len(tickets)} ticket{'s' if len(tickets) != 1 else ''}: "
              + ("; ".join(f"#{t['no']} {t['symbol']} {t['shares']} @ {t['limit']} stop {t['stop']} risk ${t['risk_usd']}" for t in tickets) or "none") + ".")]
    out = {"date": d, "tickets": tickets, "failed": failed, "brief": brief, "halted": halted}
    desk.path("inbox").mkdir(parents=True, exist_ok=True)
    desk.inbox(d, "tickets", "json").write_text(json.dumps(out, indent=1))
    desk.inbox(d, "brief", "md").write_text("\n".join(brief) + "\n\nApprove with: tradebot desk approve <numbers>\n")
    return out


def approve(desk: Desk, d: str, numbers: list[int], broker) -> list[dict]:
    """The PM's tap. Today's tickets only; each is re-checked against the live account first."""
    tk = desk.read(d, "tickets")
    if not tk:
        raise SystemExit(f"no tickets for {d}")
    limits, results = desk.limits(), []
    open_p = desk.path("journal", "open.json")
    book = json.loads(open_p.read_text()) if open_p.exists() else []
    for t in tk["tickets"]:
        if t["no"] not in numbers:
            continue
        f = risk_check(t, broker.account(), broker.positions(), broker.open_orders(), limits, desk, getattr(broker, "paper", False))
        if f:
            results.append({"no": t["no"], "symbol": t["symbol"], "status": "FAILED RISK CHECK", "fails": f})
            continue
        cid = f"desk-{d}-{t['no']}-{t['symbol']}"[:48]
        try:
            o = broker.place_oto(cid, t["symbol"], t["shares"], t["limit"], t["stop"])
            book.append({**t, "client_id": cid, "order_id": o.get("id"), "status": "SENT", "approved_at": datetime.now(NY).isoformat()})
            results.append({"no": t["no"], "symbol": t["symbol"], "status": "SENT", "order_id": o.get("id")})
        except BrokerError as e:
            results.append({"no": t["no"], "symbol": t["symbol"], "status": "REJECTED BY BROKER", "fails": [str(e)[:200]]})
    desk.path("journal").mkdir(parents=True, exist_ok=True)
    open_p.write_text(json.dumps(book, indent=1))
    tk.setdefault("approved", []).extend(numbers)
    desk.inbox(d, "tickets", "json").write_text(json.dumps(tk, indent=1))
    return results


# ------------------------------------------------------------------ exits, kill, pause


def trading_days_between(bars: list[ins.DayBar], start: str, end: str) -> int:
    """Trading days from start to end inclusive, counted on the daily bars (bars only exist on trading days)."""
    return sum(1 for b in bars if start <= b.d <= end)


def exit_due(desk: Desk, d: str, broker, prices, hold: int = 5) -> list[dict]:
    open_p = desk.path("journal", "open.json")
    book = json.loads(open_p.read_text()) if open_p.exists() else []
    positions = {p["symbol"]: p for p in broker.positions()}
    legs = [leg for o in broker.open_orders() for leg in (o.get("legs") or []) + [o]]
    done = []
    for t in book:
        if t["status"] in ("CLOSED", "NOT FILLED"):
            continue
        try:
            o = broker.order(t["order_id"]) if t.get("order_id") else {}
        except BrokerError:
            o = {}
        if t["status"] == "SENT":
            filled = float(o.get("filled_qty") or 0)
            if o.get("status") in ("canceled", "expired", "rejected", "done_for_day") and filled == 0:
                t["status"] = "NOT FILLED"
                done.append({"symbol": t["symbol"], "action": "entry not filled by the close: cancelled"})
                continue
            if filled > 0:
                t.update(status="FILLED", fill=float(o.get("filled_avg_price") or t["limit"]), fill_date=(o.get("filled_at") or d)[:10], qty=filled)
        if t["status"] == "FILLED":
            if t["symbol"] not in positions:
                t["status"] = "CLOSED"
                t.setdefault("exit_reason", "stop (or closed outside the desk)")
                done.append({"symbol": t["symbol"], "action": "position gone: stop filled or closed by hand"})
                continue
            n = trading_days_between(prices.bars(t["symbol"]), t["fill_date"], d)
            if n >= hold:
                for leg in legs:
                    if leg.get("symbol") == t["symbol"] and leg.get("side") == "sell" and leg.get("type") in ("stop", "stop_limit"):
                        broker.cancel(leg["id"])
                qty = float(positions[t["symbol"]]["qty"])
                broker.sell_market(f"{t['client_id']}-x"[:48], t["symbol"], qty)
                t.update(status="CLOSED", exit_reason=f"time exit (trading day {n})", exit_date=d)
                done.append({"symbol": t["symbol"], "action": f"day {n}: stop cancelled, sold {qty} at market"})
    open_p.write_text(json.dumps(book, indent=1))
    return done


def kill(desk: Desk, broker=None) -> dict:
    desk.root.mkdir(parents=True, exist_ok=True)
    desk.path("KILL").write_text(datetime.now(NY).isoformat() + "\n")
    out = {"kill_file": str(desk.path("KILL")), "orders_cancelled": False, "positions": []}
    if broker is not None:
        broker.cancel_all()
        out["orders_cancelled"] = True
        out["positions"] = [{"symbol": p["symbol"], "qty": p["qty"], "unrealized_pl": p.get("unrealized_pl")} for p in broker.positions()]
    return out


def pause(desk: Desk, on: bool) -> str:
    p = desk.path("PAUSE")
    if on:
        desk.root.mkdir(parents=True, exist_ok=True)
        p.write_text(datetime.now(NY).isoformat() + "\n")
        return "PAUSE on: no new tickets until RESUME"
    p.unlink(missing_ok=True)
    return "PAUSE off"


def chain_check(desk: Desk, d: str) -> str | None:
    missing = [s for s in ("scout", "analyst", "skeptic", "tickets") if not desk.inbox(d, s, "json").exists()]
    return f"CHAIN BROKEN: missing {', '.join(f'inbox/{d}-{m}.json' for m in missing)}" if missing else None


# ------------------------------------------------------------------ JOURNAL


FIELDS = ["date", "ticker", "outcome", "entry", "exit", "pnl_usd", "pnl_r", "hypothetical", "why"]


def journal(desk: Desk, d: str, prices) -> list[dict]:
    """One row per ticket and per killed name. Killed and rejected names get the trade they
    would have made (same rules, simulated) once 5 trading days have passed."""
    rows = []
    sk = desk.read(d, "skeptic") or {"verdicts": []}
    tk = desk.read(d, "tickets") or {"tickets": [], "failed": []}
    sc = {n["symbol"]: n for n in (desk.read(d, "scout") or {"names": []})["names"]}
    book = {(t["date"], t["symbol"]): t for t in (json.loads(desk.path("journal", "open.json").read_text()) if desk.path("journal", "open.json").exists() else [])}

    def hypo(sym):
        n = sc.get(sym)
        if not n:
            return None
        r = ins.simulate(ins.Signal(sym, n["filed"], n["owner"], n["role"], n["usd"], n["accession"]), prices.bars(sym), min_adv=0)
        return None if isinstance(r, str) else r

    for v in sk["verdicts"]:
        if v["verdict"] == "KILL":
            h = hypo(v["symbol"])
            rows.append({"date": d, "ticker": v["symbol"], "outcome": "KILLED BY SKEPTIC", "entry": h and h.entry, "exit": h and h.exit,
                         "pnl_usd": None, "pnl_r": h and round(h.r, 3), "hypothetical": True, "why": v["why"]})
    for f in tk.get("failed", []):
        h = hypo(f["symbol"])
        rows.append({"date": d, "ticker": f["symbol"], "outcome": "FAILED RISK CHECK", "entry": h and h.entry, "exit": h and h.exit,
                     "pnl_usd": None, "pnl_r": h and round(h.r, 3), "hypothetical": True, "why": "; ".join(f["fails"])})
    for t in tk.get("tickets", []):
        b = book.get((d, t["symbol"]))
        if b is None:
            h = hypo(t["symbol"])
            rows.append({"date": d, "ticker": t["symbol"], "outcome": "REJECTED BY ME", "entry": h and h.entry, "exit": h and h.exit,
                         "pnl_usd": None, "pnl_r": h and round(h.r, 3), "hypothetical": True, "why": "not approved"})
        elif b["status"] in ("FILLED", "CLOSED") and b.get("fill"):
            bars = prices.bars(t["symbol"])
            ex = next((x.c for x in bars if x.d == b.get("exit_date")), None) if b["status"] == "CLOSED" else None
            if b["status"] == "CLOSED" and ex is None and str(b.get("exit_reason", "")).startswith("stop"):
                ex = b["stop"]
            pnl = (ex - b["fill"]) * b.get("qty", b["shares"]) if ex else None
            rows.append({"date": d, "ticker": t["symbol"], "outcome": "FILLED", "entry": b["fill"], "exit": ex,
                         "pnl_usd": pnl and round(pnl, 2), "pnl_r": round((ex - b["fill"]) / (b["fill"] - b["stop"]), 3) if ex else None,
                         "hypothetical": False, "why": b.get("exit_reason", "open")})
        else:
            rows.append({"date": d, "ticker": t["symbol"], "outcome": "NOT FILLED" if b["status"] == "NOT FILLED" else "SENT",
                         "entry": None, "exit": None, "pnl_usd": None, "pnl_r": None, "hypothetical": False, "why": b["status"]})
    p = desk.path("journal", "trades.csv")
    p.parent.mkdir(parents=True, exist_ok=True)
    old = [r for r in csv.DictReader(p.open())] if p.exists() else []
    old = [r for r in old if r["date"] != d]  # re-running a day replaces its rows
    with p.open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=FIELDS)
        w.writeheader()
        for r in old + rows:
            w.writerow({k: r.get(k) for k in FIELDS})
    return rows


def weekly(desk: Desk, d: str) -> dict:
    p = desk.path("journal", "trades.csv")
    rows = [r for r in csv.DictReader(p.open())] if p.exists() else []
    f = lambda r: float(r["pnl_r"]) if r.get("pnl_r") not in (None, "", "None") else None
    taken = [f(r) for r in rows if r["outcome"] == "FILLED" and f(r) is not None]
    killed = [f(r) for r in rows if r["outcome"] == "KILLED BY SKEPTIC" and f(r) is not None]
    rejected = [f(r) for r in rows if r["outcome"] == "REJECTED BY ME" and f(r) is not None]
    mean = lambda xs: sum(xs) / len(xs) if xs else None
    lp = desk.path("quant", "latest.json")
    bt = json.loads(lp.read_text()).get("build", {}).get("avg_r") if lp.exists() else None
    live = mean(taken)
    status = "UNKNOWN"
    if len(taken) >= 20 and bt is not None:
        status = "DECAYING" if live < 0.5 * bt else "HOLDING UP"
    lessons = []
    if killed and taken and mean(killed) > mean(taken):
        lessons.append(f"Skeptic's kills averaged {mean(killed):+.2f}R vs {mean(taken):+.2f}R taken: the filters cost money this period. Find which rule.")
    elif killed and taken:
        lessons.append(f"Skeptic's kills averaged {mean(killed):+.2f}R vs {mean(taken):+.2f}R taken: the kills saved money.")
    if rejected and taken and mean(rejected) > mean(taken):
        lessons.append(f"Tickets you rejected averaged {mean(rejected):+.2f}R, better than the ones you approved.")
    if status == "DECAYING":
        lessons.append(f"Live average {live:+.2f}R after {len(taken)} trades is under half the backtest's {bt:+.2f}R: strategy DECAYING.")
    iso = date.fromisoformat(d).isocalendar()
    md = [f"# Week {iso[0]}-{iso[1]:02d}", "",
          f"- trades taken: {len(taken)}, average {live:+.2f}R" if live is not None else "- trades taken: 0",
          f"- backtest average: {bt:+.2f}R" if bt is not None else "- backtest average: no quant/latest.json yet",
          f"- skeptic kills (hypothetical): {len(killed)}, average {mean(killed):+.2f}R" if killed else "- skeptic kills with an outcome: 0",
          f"- rejected by you (hypothetical): {len(rejected)}" + (f", average {mean(rejected):+.2f}R" if rejected else ""),
          f"- strategy status: {status}", "", "## Lessons", *[f"- {x}" for x in lessons or ["none this week"]]]
    desk.path("journal", f"week-{iso[0]}-{iso[1]:02d}.md").write_text("\n".join(md) + "\n")
    if lessons:
        with desk.path("lessons.md").open("a") as fh:
            fh.write("".join(f"- {d}: {x}\n" for x in lessons[:3]))
    return {"taken": len(taken), "live_avg_r": live, "backtest_avg_r": bt, "killed_avg_r": mean(killed), "status": status, "lessons": lessons}


# ------------------------------------------------------------------ QUANT bookkeeping + SKEPTIC audit


def _git(cwd: Path, *args) -> str:
    try:
        return subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True, timeout=30).stdout.strip()
    except Exception:
        return ""


def log_change(desk: Desk, params: dict, result: dict, repo: Path) -> int:
    """Append one run to quant/changes.md with what changed since the last run and the commit."""
    lp = desk.path("quant", "latest.json")
    prev = json.loads(lp.read_text()).get("params", {}) if lp.exists() else {}
    diff = {k: (prev.get(k), v) for k, v in params.items() if prev.get(k) != v}
    n = sum(1 for line in desk.path("quant", "changes.md").read_text().splitlines() if line.startswith("## run")) + 1 if desk.path("quant", "changes.md").exists() else 1
    with desk.path("quant", "changes.md").open("a") as fh:
        fh.write(f"## run {n}: {datetime.now(NY).isoformat(timespec='seconds')} commit {_git(repo, 'rev-parse', '--short', 'HEAD') or 'unknown'}\n"
                 f"- changed: {json.dumps(diff) if prev else 'first run'}\n"
                 f"- build: trades {result['trades']}, avg R {result['avg_r']}, win rate {result['win_rate']}, excess vs SPY {result['excess_vs_bench']}\n\n")
    lp.write_text(json.dumps({"params": params, "build": {k: v for k, v in result.items() if k != 'trades_list'}}, indent=1, default=str))
    return n


def holdout_lock(desk: Desk, params: dict, data_id: str) -> None:
    p = desk.path("quant", "holdout.json")
    if p.exists():
        prev = json.loads(p.read_text())
        raise SystemExit(f"the holdout was already run on {prev['when']} (params {prev['params']}). It runs once. "
                         "A new idea needs new data the build never saw.")
    p.write_text(json.dumps({"when": datetime.now(NY).isoformat(timespec="seconds"), "params": params, "data": data_id}, indent=1))


def audit_quant(repo: Path, desk: Desk, tests: str = "tests") -> list[dict]:
    """Fresh eyes on the Quant's work: run the tests, read the git history of the tests for
    deleted, skipped or weakened tests, count changes, and check the holdout was used once."""
    out = []
    r = subprocess.run([sys.executable, "-m", "pytest", "-q", tests], cwd=repo, capture_output=True, text=True, timeout=900)
    tail = (r.stdout.strip().splitlines() or [""])[-1]
    out.append({"check": "tests run", "rating": "PASS" if r.returncode == 0 else ("INCONCLUSIVE" if r.returncode in (4, 5) else "FAIL"), "why": tail[:160]})
    log = _git(repo, "log", "-p", "--format=@@commit %h %s", "--", tests)
    if not log:
        out.append({"check": "test history", "rating": "INCONCLUSIVE", "why": "no git history for the tests"})
    else:
        bad, commit = [], ""
        for line in log.splitlines():
            if line.startswith("@@commit"):
                commit = line[9:]
            elif line.startswith("-") and not line.startswith("---") and line[1:].lstrip().startswith("def test_"):
                bad.append(f"{commit}: removed {line[1:].strip()[:60]}")
            elif line.startswith("+") and not line.startswith("+++") and any(k in line for k in ("pytest.mark.skip", "pytest.skip(", "xfail", "unittest.skip")):
                bad.append(f"{commit}: added a skip ({line[1:].strip()[:60]})")
        out.append({"check": "test history", "rating": "FAIL" if bad else "PASS", "why": "; ".join(bad[:5]) or "no test deleted or skipped"})
    ch = desk.path("quant", "changes.md")
    n = sum(1 for line in ch.read_text().splitlines() if line.startswith("## run")) if ch.exists() else 0
    out.append({"check": "changes before holdout", "rating": "FAIL" if n > 6 else "PASS", "why": f"{n} runs logged (first run + up to 5 changes)"})
    out.append({"check": "holdout used once", "rating": "PASS" if desk.path("quant", "holdout.json").exists() else "INCONCLUSIVE",
                "why": "locked after one run" if desk.path("quant", "holdout.json").exists() else "holdout not run yet"})
    return out
