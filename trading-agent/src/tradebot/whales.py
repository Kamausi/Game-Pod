"""Follow big Solana wallets, but only the ones whose own record says copying them could pay.

Reads the chain only (JSON-RPC, no keys). Three steps:

  1. history   every swap a wallet made: from getTransaction, the wallet's token balance change
               per mint (pre/postTokenBalances by owner) against what it paid or received in SOL
               (native lamports + wrapped SOL, fee added back) or USDC/USDT. A balance change with
               no payment is a transfer or airdrop, not a trade, and is skipped.
  2. score     first-in-first-out matching of buys and sells per token gives closed trades with a
               return and a holding time. A wallet is judged on those, never on its unrealised
               bags, and never on its balance (big is not the same as good).
  3. gate      a wallet may be copied only if: 20+ closed trades; deflated Sharpe >= 0.95 with
               N = the number of wallets you screened (leaderboard wallets are winners by
               selection, so the more you looked at, the higher the bar); its median hold is at
               least 10x your follow delay (a wallet that flips in seconds is gone before you
               arrive); and its average trade beats your round-trip cost.

Then `watch` paper-copies gated wallets: their new buys become paper buys through the existing
DexPaperTrader (RugGuard checks + Jupiter quote + price-impact cap), their sells close the copy.
Nothing is signed or sent. Ethereum wallets are not covered (an archive RPC or indexer key is
needed to read swap history there).

Mints verified 2026-10-15: USDC EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v (circle.com, solana.com
docs), USDT Es9vMFrzaCERmJfrF4H2FYD4KCoNkY11McCe8BenwNYB, wrapped SOL So11111111111111111111111111111111111111112
(solana.com docs).
"""
from __future__ import annotations

import json
import statistics
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path

from .bundlecheck import TX_OPTS, _keys

WSOL = "So11111111111111111111111111111111111111112"
USDC = "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v"
USDT = "Es9vMFrzaCERmJfrF4H2FYD4KCoNkY11McCe8BenwNYB"
STABLES = {USDC, USDT}


@dataclass
class Swap:
    sig: str
    time: int  # unix seconds (blockTime)
    mint: str
    side: str  # buy / sell
    qty: float
    quote: str  # SOL or USD
    paid: float  # quote units spent (buy) or received (sell), positive
    price: float  # quote per token


def owner_mint_deltas(tx: dict, owner: str) -> dict[str, float]:
    meta, out = tx.get("meta") or {}, {}
    for side, sign in (("preTokenBalances", -1), ("postTokenBalances", 1)):
        for b in meta.get(side) or []:
            if b.get("owner") != owner:
                continue
            amt = float(b["uiTokenAmount"].get("uiAmountString") or b["uiTokenAmount"].get("uiAmount") or 0)
            out[b["mint"]] = out.get(b["mint"], 0.0) + sign * amt
    return {k: v for k, v in out.items() if abs(v) > 1e-12}


def swap_of(tx: dict, wallet: str, sig: str = "") -> Swap | None:
    if not tx or (tx.get("meta") or {}).get("err"):
        return None
    keys, meta = _keys(tx), tx["meta"]
    d = owner_mint_deltas(tx, wallet)
    sol = d.pop(WSOL, 0.0)
    if wallet in keys:
        i = keys.index(wallet)
        lam = meta["postBalances"][i] - meta["preBalances"][i] + (meta.get("fee", 0) if i == 0 else 0)
        sol += lam / 1e9
    usd = sum(d.pop(m, 0.0) for m in list(d) if m in STABLES)
    tokens = {m: v for m, v in d.items()}
    if len(tokens) != 1:
        return None  # nothing traded, or a multi-token route we do not attribute
    mint, qty = next(iter(tokens.items()))
    quote, q = ("USD", usd) if abs(usd) > 1e-9 else ("SOL", sol)
    if abs(q) < 1e-9 or (qty > 0) == (q > 0):
        return None  # tokens arrived with no payment (transfer, airdrop) or both moved the same way
    return Swap(sig, int(tx.get("blockTime") or 0), mint, "buy" if qty > 0 else "sell", abs(qty), quote, abs(q), abs(q) / abs(qty))


@dataclass
class Transfer:
    sig: str
    time: int
    mint: str  # token mint, or "SOL"
    direction: str  # in / out
    qty: float
    counterparty: str | None


def transfer_of(tx: dict, wallet: str, sig: str = "") -> Transfer | None:
    """A token moving in or out with nothing paid back: vesting, airdrops, exchange deposits."""
    if not tx or (tx.get("meta") or {}).get("err") or swap_of(tx, wallet, sig):
        return None
    d = owner_mint_deltas(tx, wallet)
    d.pop(WSOL, None)
    if len(d) != 1:
        return None
    mint, qty = next(iter(d.items()))
    meta, other = tx["meta"], None
    for side in ("postTokenBalances", "preTokenBalances"):
        for b in meta.get(side) or []:
            if b.get("mint") == mint and b.get("owner") not in (None, wallet):
                other = b["owner"]
                break
        if other:
            break
    return Transfer(sig, int(tx.get("blockTime") or 0), mint, "in" if qty > 0 else "out", abs(qty), other)


def history(rpc, wallet: str, max_tx: int = 300, transfers: list | None = None) -> list[Swap]:
    sigs, before = [], None
    while len(sigs) < max_tx:
        batch = rpc.call("getSignaturesForAddress", [wallet, {"limit": min(1000, max_tx - len(sigs)), **({"before": before} if before else {})}])
        sigs += batch
        if len(batch) < 1000 or not batch:
            break
        before = batch[-1]["signature"]
    out = []
    for s in sigs:
        if s.get("err"):
            continue
        tx = rpc.call("getTransaction", [s["signature"], TX_OPTS])
        sw = swap_of(tx, wallet, s["signature"])
        if sw:
            out.append(sw)
        elif transfers is not None and (t := transfer_of(tx, wallet, s["signature"])):
            transfers.append(t)
    return sorted(out, key=lambda x: x.time)


def first_seen(rpc, wallet: str, max_pages: int = 3) -> tuple[int | None, bool]:
    """Time of the oldest transaction reachable, and whether that is really the first one."""
    from .bundlecheck import oldest_signatures
    sigs, reached = oldest_signatures(rpc, wallet, max_pages=max_pages)
    return (sigs[0].get("blockTime") if sigs else None), reached


@dataclass
class Score:
    wallet: str
    swaps: int
    closed: int
    win_rate: float | None
    avg_return: float | None
    median_hold_s: float | None
    sharpe: float | None
    psr: float | None
    open_positions: int
    tokens: int
    swaps_per_day: float
    returns: list[float] = field(default_factory=list, repr=False)
    flags: list[str] = field(default_factory=list)


def score(wallet: str, swaps: list[Swap], lag_s: float = 30, transfers: list[Transfer] | None = None, labels: dict[str, str] | None = None,
          born: int | None = None, now: float | None = None) -> Score:
    """labels: address -> name (e.g. exchange deposit wallets from Solscan or Arkham), supplied by you."""
    from . import quant
    lots: dict[tuple[str, str], list[list[float]]] = {}  # (mint, quote) -> [[qty, price, time]]
    rets, holds = [], []
    for s in swaps:
        key = (s.mint, s.quote)
        if s.side == "buy":
            lots.setdefault(key, []).append([s.qty, s.price, s.time])
            continue
        left = s.qty
        q = lots.get(key, [])
        while left > 1e-12 and q:
            take = min(left, q[0][0])
            rets.append(s.price / q[0][1] - 1)
            holds.append(s.time - q[0][2])
            q[0][0] -= take
            left -= take
            if q[0][0] <= 1e-12:
                q.pop(0)
    span_days = max(1 / 24, (swaps[-1].time - swaps[0].time) / 86400) if len(swaps) > 1 else 1.0
    sc = Score(wallet, len(swaps), len(rets), sum(r > 0 for r in rets) / len(rets) if rets else None,
               statistics.mean(rets) if rets else None, statistics.median(holds) if holds else None,
               quant.sharpe(rets) if len(rets) >= 3 else None, quant.psr(rets) if len(rets) >= 3 else None,
               sum(1 for q in lots.values() if any(l[0] > 1e-12 for l in q)), len({s.mint for s in swaps}), len(swaps) / span_days, rets)
    if sc.closed < 20:
        sc.flags.append(f"only {sc.closed} closed trades: too few to judge")
    if sc.median_hold_s is not None and sc.median_hold_s < 10 * lag_s:
        sc.flags.append(f"median hold {sc.median_hold_s:.0f}s: gone before a {lag_s:.0f}s follower arrives")
    if sc.swaps_per_day > 200:
        sc.flags.append(f"{sc.swaps_per_day:.0f} swaps a day: a bot or market maker, not a conviction trader")
    if sc.open_positions > 2 * max(1, sc.closed):
        sc.flags.append("mostly open bags: its wins may be unrealised and unsellable")
    # wash trading: many quick round trips in one token that go nowhere
    churn = 0
    last_buy: dict[str, Swap] = {}
    for s_ in swaps:
        if s_.side == "buy":
            last_buy[s_.mint] = s_
        elif s_.mint in last_buy and s_.time - last_buy[s_.mint].time <= 3600 and abs(s_.price / last_buy[s_.mint].price - 1) < 0.01:
            churn += 1
    if churn >= 5:
        sc.flags.append(f"{churn} round trips within an hour at the same price: looks like wash trading (fake volume)")
    tr = transfers or []
    tin = [t for t in tr if t.direction == "in"]
    sells = sum(1 for s_ in swaps if s_.side == "sell")
    if len(tin) >= 3 and sells > sum(1 for s_ in swaps if s_.side == "buy"):
        sc.flags.append(f"receives tokens without paying ({len(tin)} times) and mostly sells: a VC, team or vesting distributor, not smart money")
    for t in tr:
        lab = (labels or {}).get(t.counterparty or "")
        if t.direction == "out" and lab:
            sc.flags.append(f"moved {t.qty:,.4g} of {t.mint[:6]}… to {lab}: exchange inflow, often a sale coming")
    if born is not None:
        age = ((now or time.time()) - born) / 86400
        if age < 30:
            sc.flags.append(f"wallet is {age:.0f} days old: too new to trust (fresh wallets are cheap to fake)")
    return sc


def gate(scores: list[Score], screened: int | None = None, lag_s: float = 30, cost_bps: float = 100) -> list[dict]:
    """Which wallets may be copied. screened = how many wallets were looked at to find these."""
    from . import quant
    n = max(len(scores), screened or 0)
    # The bar is what the luckiest of n skill-less wallets would show, from each record's own sampling
    # noise. Not the spread of Sharpes across these wallets: they are different traders, not repeated
    # tries of one strategy, and one standout would otherwise raise the bar for everyone else.
    v = None
    out = []
    for s in scores:
        why = []
        if s.closed < 20:
            why.append(f"{s.closed} closed trades (needs 20)")
        d = quant.dsr(s.returns, n, v) if len(s.returns) >= 3 else None
        if d is None or not d["dsr"] >= 0.95:
            why.append(f"deflated Sharpe {d['dsr']:.0%} after screening {n} wallets (needs 95%)" if d else "no record")
        if s.median_hold_s is None or s.median_hold_s < 10 * lag_s:
            why.append("holds too briefly to follow")
        if s.avg_return is None or s.avg_return <= cost_bps / 1e4:
            why.append(f"average trade {0 if s.avg_return is None else s.avg_return:+.2%} does not beat a {cost_bps:.0f} bp round trip")
        out.append({"wallet": s.wallet, "copy": not why, "why": why or ["passes every check"], "dsr": None if d is None else d["dsr"]})
    return out


# ------------------------------------------------------------------ paper copying


def watch(rpc, trader, wallets: list[str], state_path: str | Path, usdc_per_copy: float = 50.0, max_tx: int = 50, now=time.time,
          sleeve_usd: float | None = None, max_chase: float = 0.03, sol_usd=None) -> list[dict]:
    """One pass: new swaps of each gated wallet since the last pass become paper copies.
    sleeve_usd caps what copying may have deployed at once (keep it to 1-5% of your capital);
    max_chase skips a copy when our quoted price is already that much worse than the whale's fill
    (you would be the exit liquidity). sol_usd() prices SOL for SOL-quoted whale fills."""
    p = Path(state_path)
    st = json.loads(p.read_text()) if p.exists() else {"seen": {}, "copies": {}}
    if "paper" in st:  # the paper account carries over between passes
        trader.usdc, trader.holdings = st["paper"]["usdc"], dict(st["paper"]["holdings"])
    events = []
    for w in wallets:
        new = []
        for s in rpc.call("getSignaturesForAddress", [w, {"limit": max_tx}]):
            if s["signature"] == st["seen"].get(w):
                break
            new.append(s)
        if new:
            first_pass = w not in st["seen"]
            st["seen"][w] = new[0]["signature"]
            if first_pass:
                events.append({"wallet": w, "event": "baseline set; copying starts with its next trade"})
                continue
        for s in reversed(new):
            if s.get("err"):
                continue
            sw = swap_of(rpc.call("getTransaction", [s["signature"], TX_OPTS]), w, s["signature"])
            if not sw:
                continue
            delay = now() - sw.time if sw.time else None
            key = f"{w}:{sw.mint}"
            if sw.side == "buy" and key not in st["copies"]:
                deployed = sum(c["usdc"] for c in st["copies"].values())
                if sleeve_usd is not None and deployed + usdc_per_copy > sleeve_usd:
                    events.append({"wallet": w, "mint": sw.mint, "whale": "buy", "copy": {"action": "skipped", "why": f"copy sleeve full (${deployed:,.0f} of ${sleeve_usd:,.0f})"}})
                    continue
                whale_usd = sw.price if sw.quote == "USD" else (sw.price * sol_usd() if sol_usd else None)
                ours = trader.price_usd(sw.mint, usdc_per_copy) if hasattr(trader, "price_usd") else None
                if whale_usd and ours and ours / whale_usd - 1 > max_chase:
                    events.append({"wallet": w, "mint": sw.mint, "whale": "buy", "copy": {"action": "skipped", "why": f"price already {ours / whale_usd - 1:+.1%} above the whale's fill: you'd be its exit liquidity"}})
                    continue
                res = trader.buy(sw.mint, usdc_per_copy)
                if res.get("action") == "paper_buy":
                    st["copies"][key] = {"since": sw.time, "usdc": usdc_per_copy}
                events.append({"wallet": w, "mint": sw.mint, "whale": "buy", "delay_s": delay, "copy": res})
            elif sw.side == "sell" and key in st["copies"]:
                res = trader.sell_all(sw.mint)
                st["copies"].pop(key)
                events.append({"wallet": w, "mint": sw.mint, "whale": "sell", "delay_s": delay, "copy": res})
    st["paper"] = {"usdc": trader.usdc, "holdings": trader.holdings}
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(st, indent=1))
    return events


def scorecard_rows(scores: list[Score], gates: list[dict]) -> list[dict]:
    g = {x["wallet"]: x for x in gates}
    return [{**{k: v for k, v in asdict(s).items() if k != "returns"}, **g.get(s.wallet, {})} for s in scores]
