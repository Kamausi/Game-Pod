"""Solana meme-coin safety checks and paper DEX trading on real quotes.

RugGuard reads the chain (no keys) and FAILS CLOSED: any check it cannot
complete counts as a failure. Checks:
  - mint authority revoked (nobody can print more supply)
  - freeze authority revoked (nobody can freeze your tokens: the classic honeypot)
  - top-10 holder concentration, excluding the liquidity pool and burn addresses
  - optionally: share of LP tokens burned or held by the incinerator

DexPaperTrader gets a real route quote from Jupiter, refuses trades whose price
impact exceeds the cap, and records a paper fill at the quoted output minus
extra slippage. It never builds, signs or sends a transaction. Real swaps
(signing, simulation, RPC or Jito bundle sending) live in solana_live.py and
require a go-live authorization like every other live path.

Not tested against a live RPC or Jupiter from the build environment.
"""
from __future__ import annotations

import json
import time
import urllib.request
from dataclasses import dataclass, field
from typing import Callable

BURN_OWNERS = {"1nc1nerator11111111111111111111111111111111", "11111111111111111111111111111111"}
USDC_MINT = "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v"
DEFAULT_RPC = "https://api.mainnet-beta.solana.com"
DEFAULT_JUPITER = "https://lite-api.jup.ag/swap/v1"

Post = Callable[[str, dict], dict]
Get = Callable[[str], dict]


def _post(url: str, body: dict) -> dict:
    req = urllib.request.Request(url, data=json.dumps(body).encode(), headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read())


def _get(url: str) -> dict:
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "tradebot-research/0.1"}), timeout=10) as r:
        return json.loads(r.read())


class SolanaRPC:
    def __init__(self, url: str = DEFAULT_RPC, post: Post = _post):
        self.url, self.post = url, post
        self._id = 0

    def call(self, method: str, params: list) -> dict:
        self._id += 1
        js = self.post(self.url, {"jsonrpc": "2.0", "id": self._id, "method": method, "params": params})
        if "error" in js:
            raise RuntimeError(f"{method}: {js['error']}")
        return js["result"]

    def mint_info(self, mint: str) -> dict:
        v = self.call("getAccountInfo", [mint, {"encoding": "jsonParsed"}])["value"]
        if not v or v["data"].get("parsed", {}).get("type") != "mint":
            raise ValueError(f"{mint} is not an SPL token mint")
        return v["data"]["parsed"]["info"]

    def largest_accounts(self, mint: str) -> list[dict]:
        return self.call("getTokenLargestAccounts", [mint])["value"]

    def token_account_owner(self, address: str) -> str:
        v = self.call("getAccountInfo", [address, {"encoding": "jsonParsed"}])["value"]
        return v["data"]["parsed"]["info"]["owner"]


@dataclass
class RugReport:
    mint: str
    passed: bool
    checks: dict[str, tuple[bool, str]]


class RugGuard:
    def __init__(self, rpc: SolanaRPC, max_top10_share: float = 0.30, min_lp_burned: float = 0.95, ttl_s: int = 600):
        self.rpc, self.max_top10, self.min_lp_burned, self.ttl = rpc, max_top10_share, min_lp_burned, ttl_s
        self.cache: dict[str, tuple[float, RugReport]] = {}

    def assess(self, mint: str, pool_owner: str | None = None, lp_mint: str | None = None) -> RugReport:
        hit = self.cache.get(mint)
        if hit and time.time() - hit[0] < self.ttl:
            return hit[1]
        checks: dict[str, tuple[bool, str]] = {}

        def run(name, fn):
            try:
                checks[name] = fn()
            except Exception as e:  # fail closed
                checks[name] = (False, f"could not verify: {type(e).__name__}: {e}"[:160])

        info = {}

        def authorities():
            info.update(self.rpc.mint_info(mint))
            return (True, "ok")

        run("mint_readable", authorities)
        if info:
            checks["mint_authority_revoked"] = (info.get("mintAuthority") is None, f"mintAuthority={info.get('mintAuthority')}")
            checks["freeze_authority_revoked"] = (info.get("freezeAuthority") is None, f"freezeAuthority={info.get('freezeAuthority')}")

            def concentration():
                supply = float(info["supply"]) / 10 ** int(info["decimals"])
                held, counted = 0.0, 0
                for acct in self.rpc.largest_accounts(mint):  # RPC returns up to 20, largest first
                    owner = self.rpc.token_account_owner(acct["address"])
                    if owner in BURN_OWNERS or (pool_owner and owner == pool_owner):
                        continue
                    held += float(acct["uiAmount"] or 0)
                    counted += 1
                    if counted == 10:
                        break
                share = held / supply if supply > 0 else 1.0
                return (share <= self.max_top10, f"largest non-pool holders own {share:.1%} (cap {self.max_top10:.0%})")

            run("holder_concentration", concentration)
        if lp_mint:
            def lp_burned():
                lp = self.rpc.mint_info(lp_mint)
                supply = float(lp["supply"]) / 10 ** int(lp["decimals"])
                burned = sum(float(a["uiAmount"] or 0) for a in self.rpc.largest_accounts(lp_mint)
                             if self.rpc.token_account_owner(a["address"]) in BURN_OWNERS)
                share = burned / supply if supply > 0 else 0.0
                return (share >= self.min_lp_burned, f"{share:.1%} of LP tokens burned (need {self.min_lp_burned:.0%})")

            run("liquidity_locked", lp_burned)
        else:
            checks["liquidity_locked"] = (False, "no LP mint given; liquidity lock not verified")
        rep = RugReport(mint, all(ok for ok, _ in checks.values()), checks)
        self.cache[mint] = (time.time(), rep)
        return rep


class Jupiter:
    def __init__(self, base: str = DEFAULT_JUPITER, get: Get = _get):
        self.base, self.get = base.rstrip("/"), get

    def quote(self, input_mint: str, output_mint: str, amount: int, slippage_bps: int = 50) -> dict:
        return self.get(f"{self.base}/quote?inputMint={input_mint}&outputMint={output_mint}&amount={amount}&slippageBps={slippage_bps}")


@dataclass
class DexPaperTrader:
    guard: RugGuard
    jupiter: Jupiter
    max_price_impact: float = 0.01
    extra_slip_bps: float = 50.0
    usdc: float = 10_000.0
    network_fee_usd: float = 0.02  # per swap: base fee + priority fee, an estimate; set your own
    holdings: dict[str, int] = field(default_factory=dict)  # mint -> raw token amount
    log: list[dict] = field(default_factory=list)

    def buy(self, mint: str, usdc_amount: float, pool_owner: str | None = None, lp_mint: str | None = None) -> dict:
        rep = self.guard.assess(mint, pool_owner, lp_mint)
        if not rep.passed:
            out = {"action": "blocked", "mint": mint, "failed": {k: v[1] for k, v in rep.checks.items() if not v[0]}}
            self.log.append(out)
            return out
        if usdc_amount > self.usdc:
            raise ValueError("not enough paper USDC")
        q = self.jupiter.quote(USDC_MINT, mint, int(usdc_amount * 1e6))
        impact = float(q.get("priceImpactPct", 1))
        if impact > self.max_price_impact:
            out = {"action": "blocked", "mint": mint, "failed": {"price_impact": f"{impact:.2%} > {self.max_price_impact:.2%}"}}
            self.log.append(out)
            return out
        got = int(int(q["outAmount"]) * (1 - self.extra_slip_bps / 1e4))
        self.usdc -= usdc_amount + self.network_fee_usd
        self.holdings[mint] = self.holdings.get(mint, 0) + got
        out = {"action": "paper_buy", "mint": mint, "usdc": usdc_amount, "tokens_raw": got, "price_impact": impact}
        self.log.append(out)
        return out

    def price_usd(self, mint: str, usdc_amount: float) -> float | None:
        """What buying usdc_amount would pay per whole token now (Jupiter quote, token decimals from chain)."""
        try:
            dec = int(self.guard.rpc.mint_info(mint)["decimals"])
            q = self.jupiter.quote(USDC_MINT, mint, int(usdc_amount * 1e6))
            return usdc_amount / (int(q["outAmount"]) / 10 ** dec)
        except Exception:
            return None

    def sell_all(self, mint: str) -> dict:
        amt = self.holdings.get(mint, 0)
        if not amt:
            return {"action": "noop"}
        q = self.jupiter.quote(mint, USDC_MINT, amt)
        got = int(q["outAmount"]) * (1 - self.extra_slip_bps / 1e4) / 1e6
        self.holdings[mint] = 0
        self.usdc += got - self.network_fee_usd
        out = {"action": "paper_sell", "mint": mint, "usdc": got, "price_impact": float(q.get("priceImpactPct", 0))}
        self.log.append(out)
        return out
