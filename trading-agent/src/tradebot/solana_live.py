"""Live Solana swaps through Jupiter, sent by RPC or as a Jito bundle.

Order of operations for a buy, every step fail-closed:
  1. require_live("solana") and the canary dollar caps
  2. RugGuard (mint/freeze authority, holder concentration, LP burn)
  3. Jupiter quote, refused above the price-impact cap
  4. Jupiter builds the swap transaction; we check its fee payer is our wallet
     and its input/output mints match what we asked for before signing
  5. sign locally (the key never leaves this process and is never logged)
  6. simulateTransaction with signature verification; any error aborts
  7. send: plain RPC, or a Jito bundle [swap, tip] so it lands atomically or not at all
  8. wait for confirmation, then read the real token balance from chain

Sells skip the canary caps, the rug check and the impact cap: exits are always allowed.

The keypair is loaded from the file named by SOLANA_KEYPAIR_PATH (solana-keygen
JSON: 64 numbers). Use a dedicated hot wallet holding only what you are willing
to lose; the canary caps assume that.

Covered by tests with fake RPC/Jupiter/Jito transports and real local signing
(solders). Not exercised against mainnet from the build environment.
"""
from __future__ import annotations

import base64
import json
import os
import random
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from .live import CanaryGuard, LiveAuthorization, require_live
from .onchain import USDC_MINT, Jupiter, RugGuard, SolanaRPC, _post

JITO_API = "https://mainnet.block-engine.jito.wtf/api/v1"  # sendBundle -> /bundles; other methods -> /<method> (docs.jito.wtf)
MIN_JITO_TIP = 1_000  # lamports
Post = Callable[[str, dict], dict]


class SwapRefused(RuntimeError):
    pass


def load_keypair(path: str | None = None):
    from solders.keypair import Keypair

    p = Path(path or os.environ.get("SOLANA_KEYPAIR_PATH", ""))
    if not p.is_file():
        raise SwapRefused("SOLANA_KEYPAIR_PATH does not point to a keypair file")
    mode = p.stat().st_mode & 0o077
    if mode:
        raise SwapRefused(f"{p} is readable by other users; chmod 600 it")
    raw = json.loads(p.read_text())
    return Keypair.from_bytes(bytes(raw))


def _b64(tx) -> str:
    return base64.b64encode(bytes(tx)).decode()


class RpcSender:
    name = "rpc"

    def __init__(self, rpc: SolanaRPC):
        self.rpc = rpc

    def send(self, signed_tx, keypair, blockhash) -> str:
        return self.rpc.call("sendTransaction", [_b64(signed_tx), {"encoding": "base64", "skipPreflight": True, "maxRetries": 3}])


class JitoSender:
    """Sends [swap, tip] as one bundle: both land in the same block or neither does."""

    name = "jito"

    def __init__(self, tip_lamports: int = 10_000, url: str = JITO_API, post: Post = _post):
        if tip_lamports < MIN_JITO_TIP:
            raise ValueError(f"Jito tip must be at least {MIN_JITO_TIP} lamports")
        self.tip, self.url, self.post = tip_lamports, url, post
        self.urls_called: list[str] = []
        self.last_bundle_id: str | None = None

    def _rpc(self, method: str, params: list):
        path = "bundles" if method == "sendBundle" else method
        self.urls_called.append(path)
        js = self.post(f"{self.url.rstrip('/')}/{path}", {"jsonrpc": "2.0", "id": 1, "method": method, "params": params})
        if "error" in js:
            raise RuntimeError(f"jito {method}: {js['error']}")
        return js["result"]

    def tip_tx(self, keypair, blockhash):
        from solders.message import MessageV0
        from solders.pubkey import Pubkey
        from solders.system_program import TransferParams, transfer
        from solders.transaction import VersionedTransaction

        tip_account = Pubkey.from_string(random.choice(self._rpc("getTipAccounts", [])))
        ix = transfer(TransferParams(from_pubkey=keypair.pubkey(), to_pubkey=tip_account, lamports=self.tip))
        return VersionedTransaction(MessageV0.try_compile(keypair.pubkey(), [ix], [], blockhash), [keypair])

    def send(self, signed_tx, keypair, blockhash) -> str:
        tip = self.tip_tx(keypair, blockhash)
        self.last_bundle_id = self._rpc("sendBundle", [[_b64(signed_tx), _b64(tip)], {"encoding": "base64"}])
        return str(signed_tx.signatures[0])


@dataclass
class SolanaLiveTrader:
    auth: LiveAuthorization
    keypair: object
    rpc: SolanaRPC
    jupiter: Jupiter
    guard: RugGuard
    sender: RpcSender | JitoSender
    state_path: Path = Path("runs/solana_live/state.json")
    max_price_impact: float = 0.01
    slippage_bps: int = 100
    priority_max_lamports: int = 200_000
    confirm_polls: int = 40
    sleep: Callable[[float], None] = time.sleep
    post: Post = _post
    log: list[dict] = field(default_factory=list)

    def __post_init__(self):
        if self.auth.venue != "solana":
            raise ValueError("authorization is for a different venue")
        self.canary = CanaryGuard(self.auth.caps)
        self.state = json.loads(self.state_path.read_text()) if self.state_path.exists() else {"cost_usd": {}, "equity_usd": 0.0}

    def __repr__(self):
        return f"SolanaLiveTrader(wallet={self.keypair.pubkey()}, sender={self.sender.name})"

    @classmethod
    def from_env(cls, rpc_url: str | None = None, use_jito: bool = True, **kw) -> "SolanaLiveTrader":
        from .onchain import DEFAULT_RPC

        auth = require_live("solana")
        rpc = SolanaRPC(rpc_url or os.environ.get("SOLANA_RPC_URL", DEFAULT_RPC))
        sender = JitoSender(int(os.environ.get("JITO_TIP_LAMPORTS", "10000"))) if use_jito else RpcSender(rpc)
        return cls(auth, load_keypair(), rpc, Jupiter(), RugGuard(rpc), sender, **kw)

    def _save(self):
        self.state_path.parent.mkdir(parents=True, exist_ok=True)
        self.state_path.write_text(json.dumps(self.state))

    def token_balance(self, mint: str) -> int:
        owner = str(self.keypair.pubkey())
        res = self.rpc.call("getTokenAccountsByOwner", [owner, {"mint": mint}, {"encoding": "jsonParsed"}])
        return sum(int(a["account"]["data"]["parsed"]["info"]["tokenAmount"]["amount"]) for a in res.get("value", []))

    # ---------------------------------------------------------------- swaps
    def _swap(self, input_mint: str, output_mint: str, amount: int, check_impact: bool) -> dict:
        from solders.transaction import VersionedTransaction

        q = self.jupiter.quote(input_mint, output_mint, amount, self.slippage_bps)
        impact = float(q.get("priceImpactPct", 1))
        if check_impact and impact > self.max_price_impact:
            raise SwapRefused(f"price impact {impact:.2%} over cap {self.max_price_impact:.2%}")
        if q.get("inputMint") != input_mint or q.get("outputMint") != output_mint or int(q.get("inAmount", -1)) != amount:
            raise SwapRefused("Jupiter quote does not match the requested swap")
        me = self.keypair.pubkey()
        js = self.post(f"{self.jupiter.base}/swap", {
            "quoteResponse": q, "userPublicKey": str(me), "wrapAndUnwrapSol": True, "dynamicComputeUnitLimit": True,
            "prioritizationFeeLamports": {"priorityLevelWithMaxLamports": {"maxLamports": self.priority_max_lamports,
                                                                          "priorityLevel": "high"}}})
        unsigned = VersionedTransaction.from_bytes(base64.b64decode(js["swapTransaction"]))
        msg = unsigned.message
        if msg.account_keys[0] != me:
            raise SwapRefused("swap transaction fee payer is not this wallet; refusing to sign")
        signed = VersionedTransaction(msg, [self.keypair])
        sim = self.rpc.call("simulateTransaction", [_b64(signed), {"encoding": "base64", "sigVerify": True,
                                                                  "replaceRecentBlockhash": False}])
        if sim.get("value", {}).get("err") is not None:
            raise SwapRefused(f"simulation failed: {sim['value']['err']}")
        sig = self.sender.send(signed, self.keypair, msg.recent_blockhash)
        status = self._confirm(str(signed.signatures[0]))
        return {"signature": sig, "status": status, "quote_out": int(q["outAmount"]), "impact": impact,
                "sender": self.sender.name, "bundle": getattr(self.sender, "last_bundle_id", None)}

    def _confirm(self, sig: str) -> str:
        for _ in range(self.confirm_polls):
            v = self.rpc.call("getSignatureStatuses", [[sig], {"searchTransactionHistory": True}])["value"][0]
            if v:
                if v.get("err") is not None:
                    raise SwapRefused(f"transaction {sig} failed on chain: {v['err']}")
                if v.get("confirmationStatus") in ("confirmed", "finalized"):
                    return v["confirmationStatus"]
            self.sleep(1.0)
        raise TimeoutError(f"{sig} not confirmed; check the explorer before retrying (never resend blindly)")

    def buy(self, mint: str, usdc_amount: float, pool_owner: str | None = None, lp_mint: str | None = None,
            now_ms: int | None = None) -> dict:
        now = now_ms or int(time.time() * 1000)
        pos_usd = self.state["cost_usd"].get(mint, 0.0)
        why = self.canary.check("buy", usdc_amount, 1.0, pos_usd, self.state.get("equity_usd", 0.0), now)
        if why:
            raise SwapRefused("canary cap: " + "; ".join(why))
        rep = self.guard.assess(mint, pool_owner, lp_mint)
        if not rep.passed:
            raise SwapRefused("rug check failed: " + "; ".join(f"{k}: {v[1]}" for k, v in rep.checks.items() if not v[0]))
        before = self.token_balance(mint)
        res = self._swap(USDC_MINT, mint, int(round(usdc_amount * 1e6)), check_impact=True)
        self.canary.record()
        got = self.token_balance(mint) - before
        self.state["cost_usd"][mint] = pos_usd + usdc_amount
        self._save()
        out = {"action": "live_buy", "mint": mint, "usdc": usdc_amount, "tokens_raw": got, **res}
        self.log.append(out)
        return out

    def sell_all(self, mint: str) -> dict:
        amt = self.token_balance(mint)
        if amt <= 0:
            return {"action": "noop", "mint": mint}
        usdc_before = self.token_balance(USDC_MINT)
        res = self._swap(mint, USDC_MINT, amt, check_impact=False)  # exits are never blocked by the impact cap
        got = (self.token_balance(USDC_MINT) - usdc_before) / 1e6
        cost = self.state["cost_usd"].pop(mint, 0.0)
        self.state["equity_usd"] = self.state.get("equity_usd", 0.0) + got - cost
        self._save()
        out = {"action": "live_sell", "mint": mint, "usdc": got, "pnl_usd": got - cost, **res}
        self.log.append(out)
        return out
