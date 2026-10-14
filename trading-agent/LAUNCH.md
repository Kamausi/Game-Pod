# Launch runbook

The path is: **research → paper (30+ days) → canary live → scale.** Each step has a gate that is enforced in code, not just written here.

## 0. Host

- A small always-on Linux VM (2 vCPU / 2 GB is enough) close to the exchange region, NTP enabled (`timedatectl` shows `System clock synchronized: yes`). The stale-data guard rejects quotes older than the limit, so clock drift stops trading.
- Either Docker (`docker compose`) or systemd units in `deploy/systemd/`.
- `cp .env.example .env && chmod 600 .env`. Fill in only what you use.
- **Exchange keys: trade-only, withdrawals disabled, IP allowlisted to this host.** No key in this system ever needs withdrawal rights.
- Set `TRADEBOT_ALERT_WEBHOOK` (Slack or Discord) and run `tradebot alert --level info "hello"` to confirm alerts arrive.

## 1. Research → approved release

```
tradebot data snapshot ...            # provenance-stamped data
tradebot research sweep ...           # every variant recorded
tradebot research freeze --run ...    # one candidate
tradebot research final --candidate . # the single holdout look
tradebot evidence --run ...           # reconciled evidence package
tradebot opsgate                      # 7 fault scenarios for this engine build
tradebot init-limits                  # config/risk_limits.json + config/live_caps.json
tradebot approve --candidate ... --approver you
```

## 2. Paper (minimum 30 days)

`docker compose up -d` or `systemctl enable --now exechub tradebot-paper`.

Check daily: `tradebot status`, `tradebot review --run-dir runs/paper`, the exechub dashboard on `127.0.0.1:8080` (SSH tunnel; it is never exposed publicly).

## 3. Go live (canary)

```
tradebot go-live --gates-only --operator you --venue cryptocom   # see where you stand
tradebot live-check --venue cryptocom                            # after go-live: keys + balances, no orders
```

`go-live` passes only if **all** of these hold on the paper run:

| Gate | Threshold |
|---|---|
| paper run from an approved release | not `--dev` |
| duration | ≥ 30 days |
| scored calls | ≥ 200 |
| calibration | model Brier ≤ base-rate Brier − 0.01 |
| return after fees | > 0 |
| max drawdown | < 10% |
| failsafe holds | < 5% of decisions |
| ops gate | passed for the current engine source |

It then asks you to type `GO LIVE` and writes `releases/live_authorization.json`, bound to the release, the limits file, the canary caps file and the engine source hash. **It expires after 7 days**; any edit to those files voids it immediately (the live loop re-checks every 5 minutes and kill-flattens on loss of authorization).

`--accept-unproven` exists for people who will go live anyway. It needs the phrase `I ACCEPT REAL LOSSES ON AN UNPROVEN STRATEGY`, and the flag is written into the authorization, the journal and the start alert.

Canary caps (`config/live_caps.json`, defaults): $100 per order, $500 position, $50 daily loss, 50 orders/day. The bot's capital is `min(venue cash, max position)`. Funds you already held at first start are recorded as a baseline and never treated as the bot's position.

Start: `docker compose --profile live up -d` or `systemctl enable --now tradebot-live@cryptocom`.

**First hour:** watch the alert channel. You should see `LIVE start` with `reconciliation: ok`. After the first fill, compare the exchange's order history to `runs/live_cryptocom_BTC_USD/orders.jsonl` by client id.

## 4. Scaling

Raise caps only after **two weeks of live** with live results within the paper results' range (fill slippage within 2× the modelled 2 bps, no reconciliation alerts). Raise at most 2× per step, then `go-live` again (the edit voids the authorization by design).

## Kill procedures

| Situation | Do this |
|---|---|
| Stop new risk, keep watching | `tradebot kill --run-dir runs/live_<venue>_<inst> --reason "..."` or `touch .../KILL`. The loop cancels open orders and flattens the bot's position (reduce-only), then alerts. |
| Stop the process entirely | `systemctl stop tradebot-live@<venue>` / `docker compose stop live`. Positions stay open; flatten by hand on the exchange if needed. |
| Exchange or API incident | Kill (above). If the exchange is unreachable the loop counts errors and kill-switches itself after 5. |
| Suspected key leak | Revoke the key on the exchange first, then kill. |
| Solana | `tradebot sol-sell <mint>`; exits skip the canary, rug check and impact cap. |

Resuming after a kill is always manual: `tradebot unkill --run-dir ...` (re-anchors the drawdown peak). Find out why it fired first: `tradebot status`, the journal, the alert.

## Incidents the code already handles

- **Restart mid-order**: the ledger has the order as `sent` with no outcome → reconciliation fails at startup → kill switch on. Check the exchange, then fix the ledger or `unkill`.
- **Order neither filled nor cancelled after 20 polls**: cancel-all, kill, alert.
- **Position drift** (manual trade, missed fill): periodic reconciliation trips the kill.
- **3 consecutive rejections**: kill.
- **Daily loss limit or 15% drawdown**: new risk blocked; latching across restarts.
- **Systemd crash loop**: `tradebot-live@` stops after 3 failures in 10 minutes and the `OnFailure` unit pages you.

## What is not verified from the build environment

The venue request formats (Crypto.com private API and its signature, Alpaca live, OANDA live, Jupiter `/swap`, Jito `sendBundle`) are implemented from the public docs and tested against fakes. They have not been sent to the real services from here. That is why step 3 starts with `live-check` (read-only) and a $100 cap.
