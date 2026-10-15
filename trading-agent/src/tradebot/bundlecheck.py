"""Before you ape: is this token's launch bundled, and do the same people still hold the bag?

The defensive half of the "how pump.fun tokens get bundled and shilled" playbook. It reads the
chain only (no keys, no transactions) and answers the checklist:
  - who bought in the creation block, and how much of the supply they took
  - which early buyers were funded by the same wallet (or by the deployer, or by each other)
  - how much those wallets and clusters STILL hold now, and what the deployer holds
  - whether a callout came after the price had already run (pre-positioning) and what
    happened after it

Solana JSON-RPC, verified against solana.com/docs 2026-10-15:
  getSignaturesForAddress(addr, {limit<=1000, before}) -> newest first: signature, slot, blockTime, err
  getTransaction(sig, {encoding: "json", maxSupportedTransactionVersion: 0}) -> slot, blockTime,
      meta.preBalances / postBalances (lamports per account index),
      meta.preTokenBalances / postTokenBalances (accountIndex, mint, owner, uiTokenAmount),
      transaction.message.accountKeys (+ meta.loadedAddresses for v0 transactions)
  getTokenLargestAccounts, getTokenSupply, getAccountInfo (jsonParsed)

It fails closed: if it cannot reach the token's first transactions (very busy tokens need an
archive RPC with a large history), the verdict is UNVERIFIED, never CLEAN.

Funding heuristic: a wallet's funder is the fee payer of the oldest transaction that raised its
SOL balance. That catches the common "one wallet sprays SOL to 15 fresh wallets" setup; it
misses funding routed through exchanges, which a CEX withdrawal can hide on purpose.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from .onchain import SolanaRPC

TX_OPTS = {"encoding": "json", "maxSupportedTransactionVersion": 0}


def _keys(tx: dict) -> list[str]:
    keys = list(tx["transaction"]["message"]["accountKeys"])
    keys = [k["pubkey"] if isinstance(k, dict) else k for k in keys]
    la = (tx.get("meta") or {}).get("loadedAddresses") or {}
    return keys + list(la.get("writable", [])) + list(la.get("readonly", []))


def token_deltas(tx: dict, mint: str) -> dict[str, float]:
    """Change in this mint's balance per OWNER wallet within one transaction."""
    meta, out = tx.get("meta") or {}, {}
    for side, sign in (("preTokenBalances", -1), ("postTokenBalances", 1)):
        for b in meta.get(side) or []:
            if b.get("mint") != mint or not b.get("owner"):
                continue
            amt = float(b["uiTokenAmount"].get("uiAmountString") or b["uiTokenAmount"].get("uiAmount") or 0)
            out[b["owner"]] = out.get(b["owner"], 0.0) + sign * amt
    return {k: v for k, v in out.items() if abs(v) > 1e-12}


def oldest_signatures(rpc: SolanaRPC, address: str, max_pages: int = 10, page: int = 1000) -> tuple[list[dict], bool]:
    """Walk back to the oldest signatures. Returns (oldest first, reached_the_start)."""
    before, sigs = None, []
    for _ in range(max_pages):
        opts = {"limit": page, **({"before": before} if before else {})}
        batch = rpc.call("getSignaturesForAddress", [address, opts])
        sigs += batch
        if len(batch) < page:
            return list(reversed(sigs)), True
        before = batch[-1]["signature"]
    return list(reversed(sigs)), False


def funder_of(rpc: SolanaRPC, wallet: str, max_pages: int = 3) -> str | None:
    sigs, _ = oldest_signatures(rpc, wallet, max_pages=max_pages)
    for s in sigs[:10]:
        tx = rpc.call("getTransaction", [s["signature"], TX_OPTS])
        if not tx:
            continue
        keys, meta = _keys(tx), tx["meta"]
        if wallet not in keys:
            continue
        i = keys.index(wallet)
        if meta["postBalances"][i] > meta["preBalances"][i] and keys[0] != wallet:
            return keys[0]
    return None


@dataclass
class BundleReport:
    mint: str
    verdict: str  # DANGER / CAUTION / NO BUNDLE FOUND / UNVERIFIED
    reasons: list[str] = field(default_factory=list)
    deployer: str | None = None
    creation_slot: int | None = None
    first_block_buyers: list[dict] = field(default_factory=list)
    clusters: list[dict] = field(default_factory=list)
    holdings_now: dict = field(default_factory=dict)
    reached_start: bool = False


def check(mint: str, rpc: SolanaRPC, early_slots: int = 2, max_pages: int = 10, danger_share: float = 0.20,
          cluster_share: float = 0.15, min_bundle_wallets: int = 5) -> BundleReport:
    rep = BundleReport(mint, "UNVERIFIED")
    sigs, reached = oldest_signatures(rpc, mint, max_pages=max_pages)
    rep.reached_start = reached
    if not sigs:
        rep.reasons.append("no transactions found for this mint")
        return rep
    if not reached:
        rep.reasons.append(f"history longer than {max_pages * 1000} transactions: the launch block was not reached (use an archive RPC)")
        return rep
    first = rpc.call("getTransaction", [sigs[0]["signature"], TX_OPTS])
    rep.deployer, rep.creation_slot = _keys(first)[0], first["slot"]
    supply = float(rpc.call("getTokenSupply", [mint])["value"]["uiAmountString"])
    bought: dict[str, float] = {}
    for s in sigs:
        if s["slot"] > rep.creation_slot + early_slots - 1:
            break
        if s.get("err"):
            continue
        tx = rpc.call("getTransaction", [s["signature"], TX_OPTS])
        for owner, d in token_deltas(tx, mint).items():
            if d > 0 and owner != rep.deployer:
                bought[owner] = bought.get(owner, 0.0) + d
    rep.first_block_buyers = sorted(({"wallet": w, "bought": a, "share": a / supply} for w, a in bought.items()), key=lambda x: -x["bought"])
    # who funded whom
    funders = {w: funder_of(rpc, w) for w in bought}
    parent = {w: w for w in bought}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    by_funder: dict[str, list[str]] = {}
    for w, f in funders.items():
        if f:
            by_funder.setdefault(f, []).append(w)
            if f in parent:
                parent[find(w)] = find(f)  # funded by another early buyer
    for f, ws in by_funder.items():
        for w in ws[1:]:
            parent[find(w)] = find(ws[0])
    # current holdings of early buyers
    now: dict[str, float] = {}
    for acct in rpc.call("getTokenLargestAccounts", [mint])["value"]:
        owner = rpc.token_account_owner(acct["address"])
        now[owner] = now.get(owner, 0.0) + float(acct.get("uiAmountString") or acct.get("uiAmount") or 0)
    groups: dict[str, list[str]] = {}
    for w in bought:
        groups.setdefault(find(w), []).append(w)
    for root, ws in groups.items():
        fs = {funders[w] for w in ws if funders[w]}
        dev_funded = rep.deployer in fs
        if len(ws) >= 2 or dev_funded:
            rep.clusters.append({"wallets": ws, "funders": sorted(fs), "funded_by_deployer": dev_funded,
                                 "bought_share": sum(bought[w] for w in ws) / supply, "holds_now_share": sum(now.get(w, 0.0) for w in ws) / supply})
    rep.clusters.sort(key=lambda c: -c["holds_now_share"])
    early_now = sum(now.get(w, 0.0) for w in bought) / supply
    rep.holdings_now = {"early_buyers_share": early_now, "deployer_share": now.get(rep.deployer, 0.0) / supply,
                        "note": "from the 20 largest token accounts: holdings below that are not counted, so these are lower bounds"}
    first_share = sum(bought.values()) / supply
    if len(bought) >= min_bundle_wallets and first_share >= danger_share:
        rep.reasons.append(f"{len(bought)} wallets took {first_share:.0%} of supply in the first {early_slots} slot(s)")
    big = [c for c in rep.clusters if c["holds_now_share"] >= cluster_share or (c["funded_by_deployer"] and c["holds_now_share"] > 0.02)]
    for c in big:
        rep.reasons.append(f"cluster of {len(c['wallets'])} wallets sharing a funder still holds {c['holds_now_share']:.0%}" + (" (funded by the deployer)" if c["funded_by_deployer"] else ""))
    if early_now >= danger_share:
        rep.reasons.append(f"launch-block buyers still hold at least {early_now:.0%}: they can sell into your buy")
    if rep.reasons:
        rep.verdict = "DANGER" if (big or early_now >= danger_share) else "CAUTION"
    else:
        rep.verdict = "NO BUNDLE FOUND"
        rep.reasons.append("no launch-block concentration or shared-funder cluster above the thresholds (not a safety guarantee)")
    return rep


def callout_check(prices: list[tuple[int, float]], callouts: list[tuple[int, str]], run_up_window_s: int = 3600) -> list[dict]:
    """For each callout (unix seconds, source): how far the price had already run in the hour
    before it (pre-positioning shows up here), and what it did 1 h and 24 h after."""
    prices = sorted(prices)

    def at(t):
        best = None
        for ts, p in prices:
            if ts <= t:
                best = p
            else:
                break
        return best
    out = []
    for t, src in callouts:
        p0, pre = at(t), at(t - run_up_window_s)
        after = [p for ts, p in prices if t <= ts <= t + 86400]
        out.append({"time": t, "source": src, "price": p0,
                    "run_up_before": (p0 / pre - 1) if p0 and pre else None,
                    "after_1h": (at(t + 3600) / p0 - 1) if p0 and at(t + 3600) else None,
                    "after_24h": (at(t + 86400) / p0 - 1) if p0 and at(t + 86400) else None,
                    "worst_24h": (min(after) / p0 - 1) if p0 and after else None})
    return out
