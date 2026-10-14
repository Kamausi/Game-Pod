# tradebot: a 24/7 multi-asset trading agent that starts on paper

A complete, tested system: a deterministic state engine, a typed model-decision
schema, code-owned gates and sizing, a hard risk layer with a kill switch, a paper
broker that charges realistic costs, a 24/7 loop, and a nightly calibration review.

It **starts on paper**. Real-money trading (Crypto.com, Alpaca, OANDA, Solana via
Jupiter and Jito) is built in, but every live adapter refuses to construct until
`tradebot go-live` has checked the launch gates against your paper record and you
have typed a confirmation. That authorization runs under small dollar "canary" caps
and expires after 7 days. Deployment and kill procedures: [LAUNCH.md](LAUNCH.md).

---

## 0. What changed from the viral prompt, and why

| Prompt said | What this build does | Why |
|---|---|---|
| Build through "AgenKit" at agenkit.xyz | Not used. The six phases are run as plain Claude Code steps in this repo (section 6). | A web search on 2026-10-14 turned up only the promotional post itself: no docs, no source, no install instructions. Installing an unverified harness into your coding agent hands it your shell. |
| "Jev" from console.typesafe.ai as the reflex model | Available as `--engine jev` (or `jev>opus`, escalating to Claude Opus 5.5), alongside the deterministic `RuleEngine` (default) and `ClaudeEngine`. | TypeSafe has since published API docs (docs.typesafe.ai/api), so the adapter is written against the documented shapes (section 15). It still has to earn its place: `tradebot replay` compares it with the other engines on your own saved decisions. |
| Use the model's "calibrated probability" for Kelly sizing | Probabilities are **shrunk toward 0.5** until at least 200 scored outcomes exist, then mapped through an isotonic calibration fitted nightly. | A model saying it is calibrated doesn't make it calibrated. Calibration has to be measured. |
| Execute automatically, live | Paper first. Live trading unlocks only through `tradebot go-live` after the launch gates pass (section 9, section 14), with canary dollar caps. | The prompt itself says to prioritise a system that survives. |
| 5 to 10 asymmetric altcoin setups | Not delivered as picks. A sourced research prompt is in `prompts/research.md`, and the bot trades **BTC_USD only** to start. | An honest version needs live unlock schedules, fees and holder data for each token. Making up numbers is ruled out by the prompt, and the per-bar bot shouldn't trade a thesis anyway. |
| risk_state `mean_limit` | `near_limit` | Most likely a typo. |

## 1. Architecture

```
            ┌────────── slow: BRAIN (you + Claude Opus 5.5, by hand / nightly) ─────────┐
            │  prompts/research.md   prompts/nightly_review.md   tradebot review         │
            └───────────────────────────────┬────────────────────────────────────────────┘
                                            │ proposals (operator approves; never auto-merged)
 exchange ──► feed.py ──► state.py ──► snapshot (≤400 tokens, causal) ──► engines.py
 (public REST)            all math in code                                  │ typed Decision
                                                                            ▼
                                       policy.py: gates + quarter-Kelly ──► Intent
                                                                            ▼
                         risk.py: kill switch, DD 15%, daily loss, caps ──► GuardedBroker ──► PaperBroker
                                                                            ▼
                                                              journal.jsonl ──► review.py (Brier, calibrator)
```

The model **only judges**. Code owns every threshold, size, stop and side effect.

## 2. Market regime snapshot (live data, labelled)

Source: Crypto.com Exchange public daily candles (`get-candlestick`, `BTC_USD`
and `ETH_USD`, timeframe `1D`), fetched **2026-10-14**. The 2026-10-14 bar was
still forming when fetched. The numbers below are computed from those 50 bars.

| | BTC_USD | ETH_USD |
|---|---|---|
| Latest print | 83,200 | 2,496 |
| 50-day change (from Aug 26) | +5.3% | −0.4% |
| 20-day change | −1.4% | −7.1% |
| 7-day change | −0.1% | −3.0% |
| 50-day range | 75,587 – 86,603 | 2,392 – 2,776 |
| 20-day SMA | 83,891 (price just below) | 2,616 (price below) |
| 20-day realised vol (annualised) | ~24% | ~33% |
| ETH/BTC over 50 days | | −5.4% |

**Read (this is an interpretation, not data):** BTC is in a range with a
sharp mid-September recovery (75.6k on Sep 15 to 86.6k on Sep 21). It is now
chopping just under its 20-day mean on moderate volatility. ETH is
underperforming BTC. For the bot that points to `mean_reverting` / range rather
than `trending`, so the trend-following baseline *should* mostly sit out, and it
does.

**Not covered here, because I didn't fetch it:** BTC dominance, stablecoin
supply, funding, open interest, macro, narrative rotation. Run
`prompts/research.md` with web search to get those with sources.

## 3. Decision schema (`src/tradebot/schema.py`)

```json
{ "regime": "trending|mean_reverting|high_vol|crisis",
  "direction": "long|short|neutral",
  "direction_confidence": 0.0-1.0,
  "p_win": 0.0-1.0,            // P(target before stop) for a 1.5×ATR stop, 2:1 target
  "toxic_flow": true|false,
  "setup_quality": 0|1|2|3,
  "risk_state": "safe|near_limit|reduce",
  "rationale": "one sentence (logged, never read by code)" }
```

The schema is sent to the API as a strict JSON-schema output constraint and
validated again in code. Any violation, timeout, refusal or exception becomes a
**failsafe HOLD**: no new risk is taken, and existing stops keep working.

Every gate is enforced in `policy.py`. A trade fires only if `setup_quality ≥ 2`
**and** `direction_confidence > 0.80` **and** `risk_state == safe` **and** the
flow isn't toxic **and** the regime isn't crisis **and** (long, or shorts are
enabled).

Sizing works like this:
`calibrated p` → Kelly `f* = p − (1−p)/b`, with `b` = (target − costs) / (stop + costs).
Then `0.25 × f*` (the multiplier is hard-capped at 0.25). That gives the fraction
of equity *risked*, which is divided by the stop distance to get notional. The
risk layer then clamps it to a maximum of 30% of equity.

## 4. Engines (`src/tradebot/engines.py`)

- `rules` (default): a deterministic trend and volatility baseline. Free and
  offline. It is **the benchmark any model has to beat**, not a claim of edge.
- `claude`: Claude Haiku 5.5 (`effort: low`, 10 s timeout) on every bar.
  It escalates to Claude Opus 5.5 (`effort: high`, server-side refusal fallback
  on) when confidence is below 0.60 on a directional call, or the read is
  crisis. Escalations are rate-limited to one per 5 minutes, and when the limit
  is hit the bar is a HOLD.
- To plug in any other model, write a class with `name` and
  `decide(snapshot) -> Decision`. `Decision.parse()` validates the model's JSON.

The system prompt is `prompts/reflex_system.md`.

## 5. Risk layer (`src/tradebot/risk.py`): limits the model can't touch

| Limit | Default | Behaviour |
|---|---|---|
| Max drawdown from peak | 15% (can't be set higher) | **Latches the kill switch**, which persists across restarts. Clearing it takes a manual `tradebot unkill`. |
| Max daily loss | 3% | Reduce-only for the rest of the UTC day. |
| Max position | 30% of equity (no leverage allowed) | Orders are clamped. |
| Max single order | 30% of equity | Orders are clamped. |
| Orders per day | 24 | New risk is blocked once reached. |
| Stale data | > 120 s | New risk is blocked. |
| Spread | > 25 bps | New risk is blocked. |
| Kill switch | `touch runs/paper/KILL` or `tradebot kill` | New risk is blocked and the position is flattened. |
| Loop errors | 5 in a row | Trips the kill switch. |

Exits (reduce-only orders) are always allowed. A position flip counts as new
risk. `GuardedBroker.submit` is the only order path, and it runs
`check_order` first.

## 6. The six-phase build plan (done in this repo with Claude Code; no extra harness)

| Phase | Gate (you approve) | Files |
|---|---|---|
| 1. Spec | You agree with section 0 and section 5 | `README.md` |
| 2. Architecture | Brain/reflex split, data flow | `README.md` §1 |
| 3. Plan | Module boundaries | `src/tradebot/{types,state,schema,policy,risk,broker,engines,agent,journal,feed,loop,backtest,review,cli}.py` |
| 4. Test-first build | `python3 -m unittest` is green | `tests/test_core.py` (25 tests: causality, schema, gates, Kelly cap, every risk limit, kill persistence, costs, review) |
| 5. Review | Run `/code-review` in Claude Code; read `risk.py` yourself line by line | (all of the above) |
| 6. Ship (to paper) | Section 9 gates | `runs/paper/` |

## 7. State engine (`src/tradebot/state.py`)

The snapshot contains mid, spread (bps), book imbalance, 1-, 6- and 24-bar
returns, realised volatility and its ratio, ATR%, z-score, range position,
volume ratio, inventory, unrealised P&L, drawdown, and day P&L. About 340
characters (roughly 100 tokens), well under 400.

Causality is enforced with exceptions. Any candle closing after `as_of`, or any
book stamped after it, raises `LookaheadError`. The live feed drops the
still-forming bar. In the backtest, the decision at the close of bar *i* fills
at the **open of bar i+1**. If a stop and a target are both touched in one bar,
the stop is assumed to fill first, and gaps fill at the open.

## 8. Overnight review loop (`src/tradebot/review.py`)

`tradebot review` does five things:
1. It scores every directional call: did price reach the target before the stop
   within 24 bars? Only later snapshots are used for this.
2. It reports the Brier score for raw p, for yesterday's calibrated p, and for a
   constant base rate. Raw has to beat base rate, or the model's probabilities
   carry no information.
3. It refits the isotonic calibrator (`calibration.json`), which the loop loads
   on its next start.
4. It summarises failsafe holds, risk-blocked orders and hold reasons.
5. It writes `review.md` with **proposals only**.

After that, run `prompts/nightly_review.md` in Claude Code. It may make **one**
change, on a branch, with a test. You merge it, or you don't. Nothing loosens a
risk limit automatically, ever.

Cron example (UTC):
```
0 23 * * *  cd /path/trading-agent && PYTHONPATH=src python3 -m tradebot review --run-dir runs/paper
```

## 9. Launch gates: don't skip any

1. Tests green. The backtest on **real** downloaded candles (not synthetic)
   beats buy-and-hold *risk-adjusted* after fees, and holds up across at least
   3 separate time periods.
2. At least **30 days of paper trading**, with 0 unexplained failsafe bursts and
   0 risk-layer bugs.
3. At least 200 resolved calls, with raw Brier clearly below base-rate Brier
   (for example by 0.01 or more).
4. Paper P&L after fees is positive, and max drawdown stayed under 10%.
5. Only then run `tradebot go-live`. It checks gates 2–4 plus the operational
   gate against your paper journal, in code. Use an exchange API key with
   **trade-only, no-withdrawal** permissions and an IP allowlist, and keep the
   canary caps until live results match paper (LAUNCH.md).

## 10. What the included backtest shows

On 3,000 synthetic hourly bars (a random walk with no edge to find):
return −1.2%, fees $427 on a $10k account, 118 round trips, win rate 36%,
max drawdown 5.4%. The review then flagged: *"Model p_win is no better than the
base rate. Do not go live."*

**That is the system working.** With no edge, it loses roughly its costs,
risk stays bounded, and the review catches it. A big profit on that data would
mean a bug.

## 11. Research, validation, release, execution: kept separate

Each stage is its own module with a hard boundary. A research agent (Claude or anything else) can run the first four rows but holds no broker credentials and can't approve its own work.

| Stage | Module | What it does | Boundary enforced in code |
|---|---|---|---|
| Data | `experiments.DataSnapshot`, `tradebot data snapshot` | Fingerprints every dataset (sha256 of the exact bars) and keeps a copy; snapshots record source, fetch time and availability rule | A run can't cite data that isn't stored byte for byte |
| Explore | `validation.sweep` | Runs a parameter grid on a training window | **Every** configuration goes into the append-only register (`runs/research/register.jsonl`), failures included |
| Freeze | `Register.freeze` | Turns one configuration into a candidate whose id is the hash of its spec | Changing any parameter makes a new candidate |
| Final | `validation.final_evaluation` | One evaluation on a holdout window | Refused if the candidate isn't frozen, if the holdout overlaps any training window of that family, or if it was already evaluated once |
| Statistics | `validation.assess` | Sharpe, skew, kurtosis, and the **Deflated Sharpe Ratio** using the number of configurations actually tried | Trial count comes from the register, not from what the researcher reports |
| Benchmark | `backtest.benchmark_net` | Buy-and-hold under the *same* execution model and costs | Strategy and benchmark pay the same spread, slippage and fees |
| Evidence | `tradebot evidence --run ID` | Writes `evidence.json` + `evidence.md`: spec, data hash, engine version (package, git commit, source hash), assumptions, cost model, benchmark, every variation tried, orders, equity curve | Reconciles before it says "reconciled": identical fills on re-run, independent ledger replay of equity, match with the register, and optionally a second engine's results (`--external`, e.g. LEAN/QuantConnect/QuantGPT output) compared trade by trade |
| Ops gate | `tradebot opsgate` | Fault scenarios: duplicate orders (including across a restart), partial fills, stale data, rejected orders, restart recovery (unknown-outcome orders and position drift), kill-switch latency, limits that try to loosen | Recorded against the engine source hash; any code change invalidates it |
| Approve | `tradebot approve` | Operator only. Needs a successful final run, a reconciled evidence package, a passed ops gate for the current code, and a typed confirmation in an interactive terminal | Can't be piped or scripted; weak evidence (lost to the benchmark, or DSR < 0.95) is refused unless `--accept-weak-evidence`, which is written into the release |
| Execute | `tradebot paper`, `execution.OrderManager` | Starts only if the release, the risk-limits file hash and the engine source hash all still match. Orders carry idempotent client ids written to a ledger before sending; restart reconciles the ledger with the venue and stops on any mismatch | Research output can't change limits (`config/risk_limits.json` is hashed into the release) or redeploy code (source hash) without a new approval |

Code can't stop someone who controls the whole machine. In production, run the trader as a separate OS user that owns `config/` and `releases/`, and keep broker keys only in that user's environment.

**Not done yet:** an independent re-implementation of the *signal logic* (reconciliation currently checks determinism and accounting; supply an external engine's results to check the signals), equity-specific data handling (total-return series, survivorship-free universes, delisting returns), and a live broker adapter. Paper trading tests the machinery (scheduling, orders, restarts), not profitability: a month of 1-hour bars is far too few independent decisions to prove an edge.

## 12. Multi-asset desk and adaptive ensemble

| Feature | Where | What it really does |
|---|---|---|
| Adaptive signal weighting | `engines.EnsembleEngine`, desk app | Six signals (trend, MACD momentum, RSI/Bollinger mean reversion, volume breakout, order-book flow, index gap). Each signal's calls are scored 15 bars later **per regime**; its weight moves from ×0.1 to ×3 with its measured hit rate. A signal that keeps failing in choppy markets fades out there by itself. |
| Multi-timeframe analysis | `indicators.py`, `state.py` | RSI 14, MACD 12/26/9, Bollinger 20/2, 30-bar breakout with volume, EMA trend on the bar timeframe and on bars aggregated ×4 (desk: 1m plus 15m from the exchange). |
| Scanner | desk app | Per asset: price, 24h volume, spread, gap vs Crypto.com's multi-exchange index, RSI, %B, trend on both timeframes, regime, signal, flags (illiquid, wide spread, index gap, volume spike, breakout, book imbalance). |
| Dynamic sizing | `portfolio.py`, `policy.py`, desk | Smallest of quarter Kelly (from *measured* win rate), a daily volatility target (1% of equity per position) and the tier cap; then × drawdown scale (100% → 25% as drawdown approaches 10%, entries paused beyond) × recent-results scale (0.5× after a cold streak). |
| Correlation / concentration | `portfolio.py`, desk | Rolling correlation matrix; same-side positions correlated above 0.7 share one 40% cap; gross exposure capped at 60%. |
| Stress testing | `portfolio.py`, desk | Every bar: crypto crash (majors −20%, meme −40%), equity gap down (stocks −10%, penny −35%), USD spike (FX −3%), all three at once, flash crash with gapped stops, correlations → 1, liquidity crunch. A trade that would push any scenario's loss over 12% of equity is blocked. |
| Stocks and FX in the desk | desk app | AAPL, SPY, SNDL (penny tier), EUR/USD, GBP/USD, AUD/USD. Live: Yahoo Finance one-minute closes through your Firecrawl connector, every 2 minutes, 1 credit each (toggle "Live stocks & FX"). Real hours: NYSE session from Yahoo (so holidays count), FX Sunday 17:00 to Friday 17:00 New York; no trades and no stop checks while closed; simulated prices gap at the open. Each asset decides once per its own new bar. Correlation is pairwise on shared bar times. Yahoo gives no order book: spreads are estimates and the flow and breakout signals stay at 0 for these. |
| Order types | desk | Bracket orders (stop + target), trailing stops, limit-only entries for the meme tier (must trade through the limit, expire after 3 bars, never chased), TWAP in 3 slices above 10% of equity. |
| Meme-coin guardrails | desk | Separate tier: 5% max position, limit orders only, spread < 50 bps, $1M+ daily volume. PEPE (≈$130k/day on this venue) is blocked as illiquid. |

## 13. Execution services, more markets, sentiment, arbitrage, on-chain

| Piece | Where | What it does | Tested how |
|---|---|---|---|
| **Go execution hub** | `services/exechub/` | gRPC service (`proto/trading.proto`) that receives order intents from Python, applies its **own** risk checks (tier caps, spread, stale quotes, limit-only tiers, daily order cap, halt), fills on a paper venue, journals every event (replayed on restart so idempotency and halts survive), streams events over gRPC and WebSocket, and serves a local control dashboard with an emergency halt. | `go test` (5), plus 4 end-to-end tests that build the binary and drive it from Python over gRPC and HTTP |
| **Latency** | `tradebot hub-bench` | Measured on the build machine: Python → Go → Python round trip **0.52 ms p50, 1.7 ms p99**; decision inside the hub **~1 µs**. | 2,000-order benchmark |
| **Streaming data** | `marketstream.py` | Crypto.com WebSocket order-book stream with heartbeat handling, reconnect, and p50/p99 of exchange→us and us→decision. | parser + stats tests; live stream not reachable from the build machine |
| **Stocks, penny stocks, forex** | `assets.py`, `brokers.py` | Asset classes with market hours (NYSE regular session; FX Sunday 17:00 to Friday 17:00 New York), share/lot rounding (always down), penny-stock tier (under $5: limit only, spread ≤ 1.5%, $500k+ daily volume, 2% max position). Broker adapters for **Alpaca paper** (market, limit, bracket, trailing stop) and **OANDA practice** (market, limit, stop-loss / take-profit / trailing stop on fill), locked to paper hosts: pointing them at live hosts raises. One router with a shared idempotency ledger. | request shapes tested against a fake transport; run `tradebot brokers --check` with your paper keys before relying on them |
| **News sentiment** | `sentiment.py`, desk app | RSS feeds (CoinDesk, Cointelegraph, Decrypt) or, in the desk, Firecrawl news search. Scored by Claude (strict JSON) or a built-in word list with negation. Counts each headline from when it was **first seen**, not its claimed publish time. Enters the ensemble as one more signal and earns weight only if it beats costs per regime. Feeds with a DTD are refused (XML attack guard). | causality, de-duplication, scoring tests |
| **Cross-exchange arbitrage** | `arbitrage.py`, desk app | Crypto.com vs Coinbase vs Kraken (Python) or vs Kraken via Firecrawl (desk). Net of both taker fees and a latency allowance; stale or out-of-sync quotes are ignored. Paper book with per-venue inventory shows that inventory, not detection, is what limits arbitrage. | scan, parser and book tests |
| **Solana safety + DEX trading** | `onchain.py`, `solana_live.py`, `tradebot rugcheck / sol-buy / sol-sell` | Read-only rug-pull checks that **fail closed**: mint authority revoked, freeze authority revoked (honeypots), top-10 holder share excluding pool and burn addresses, LP tokens burned. Paper DEX trader on real Jupiter quotes. Live: Jupiter builds the swap, the bot checks the fee payer and mints, signs locally, simulates with signature verification, sends as a **Jito bundle** [swap, tip] (or plain RPC), waits for confirmation and reads the real balance change. Refuses price impact over 1% on buys; exits are never blocked. | fake RPC/Jupiter/Jito with real local signing |
| **Deep history** | `datasets.py`, `tradebot import-csv`, `research walkforward` | Import any OHLCV CSV (e.g. 20+ years of daily stock bars): duplicates and broken bars dropped and counted, gaps and outliers listed, nothing clipped, adjusted/unadjusted recorded. Anchored walk-forward: explore on the past, test once on each next fold, every run recorded. | import and walk-forward tests |
| **Storage** | `storage.py`, `sql/timescale.sql` | SQLite tables for ticks, orders and fills; the same schema for PostgreSQL/TimescaleDB hypertables. | fill aggregation test |

**Not built:** a Rust engine or a separate Next.js frontend. The Go hub already does execution in microseconds, and the hosted desk plus the hub's dashboard cover monitoring and the kill switch.

**Running the local stack:**
```bash
cd services/exechub && go build -o exechub . && ./exechub        # gRPC :50051, dashboard http://127.0.0.1:8080
python3 -m tradebot hub-bench                                     # latency through the hub
python3 -m tradebot paper --dev --exechub 127.0.0.1:50051         # paper loop routed through the hub
python3 -m tradebot sentiment            # or --claude
python3 -m tradebot arb
python3 -m tradebot rugcheck <mint> --pool-owner <amm authority> --lp-mint <lp mint>
python3 -m tradebot import-csv spy_daily.csv --out data/spy.json --adjusted split+dividend
python3 -m tradebot research walkforward --family spy_v1 --data data/spy.json --folds 5 --grid '{"reward_risk":[1.5,2,3]}'
```
Optional Python packages: `pip install -e ".[all]"` (grpcio, websockets, anthropic, solders).

## 14. Live trading and deployment

| Piece | Where | What it does |
|---|---|---|
| **Launch gates in code** | `live.py`, `tradebot go-live` | Checks your paper journal: approved release (not `--dev`), ≥ 30 days, ≥ 200 scored calls, Brier ≥ 0.01 better than base rate, return > 0, max drawdown < 10%, failsafe < 5%, ops gate passed for this engine build. Then you type `GO LIVE`. The authorization is bound to the release, limits, caps and engine hashes, and it expires in 7 days. `--accept-unproven` needs a longer phrase and is flagged in the journal and every start alert. |
| **Canary caps** | `config/live_caps.json` | $100 per order, $500 position, $50 daily loss, 50 orders/day by default, on top of the percentage limits. Reducing a position is always allowed. Editing the file voids the authorization. |
| **Crypto.com live** | `venues_live.py` | Private API with HMAC-SHA256 signing. Marketable **IOC limit** orders capped 15 bps through the touch, never unbounded market orders. Polls for the real outcome and mirrors only confirmed fills (real price and fee, including fees taken in the coin). Spot is long-only. Funds you held before the first start are a baseline, never the bot's position. |
| **Alpaca / OANDA live** | `venues_live.py` | IOC limit orders on the live hosts; an IOC partial fill keeps its filled part. These classes take a live authorization; the paper classes still refuse live hosts. |
| **Stock and FX data** | `feed.py` | `AlpacaStockFeed` (IEX bars and quotes) and `OandaFxFeed` (mid candles and pricing), both dropping the forming bar. |
| **Live loop** | `live_runner.py`, `tradebot live` | The paper decision path with real orders. It reconciles at start and every 8 ticks (any mismatch trips the kill switch) and re-checks the authorization every 5 minutes. On kill it cancels all orders, flattens the bot's position and alerts. It respects market hours. An order with no final state after 20 polls triggers cancel-all and a kill. |
| **Alerts** | `alerts.py`, `tradebot alert` | Slack or Discord webhook on start, fills, rejects, kills, reconciliation failures, loop halts and a daily summary. A failed alert never blocks trading or the kill path. |
| **Deployment** | `Dockerfile`, `docker-compose.yml`, `deploy/systemd/`, `.github/workflows/tradebot.yml`, `.env.example` | Images for the Go hub (distroless, non-root) and the trader. Compose runs paper by default, with `live` and TimescaleDB as opt-in profiles. Systemd units stop after 3 crashes in 10 minutes and page you via `OnFailure`. CI runs the Go and Python tests and builds both images. |

What has **not** been verified from the build machine: real requests to Crypto.com's private API, Alpaca live, OANDA live, Jupiter `/swap` and Jito. They are written from the public docs and tested against fakes, and the Crypto.com signature is tested against the documented algorithm. The Docker build was also not run, because no Docker daemon was available here; CI builds it. That is why the runbook starts with the read-only `tradebot live-check` and a $100 cap.

## 15. Survival rules, news overlay, Jev, run memory

Seven rules from a study that put 3,505 agents on real money, implemented in code:

| Rule | Where | What it does here |
|---|---|---|
| 1. Code owns exits | `policy.py` (`exit_mode="fixed"`, default), desk | A hard 2% stop and 4% target attach at entry. The model cannot close a position early unless `model_exits=True`; the kill switch and the risk layer still can. The desk trades 1-minute bars, so each tier gets the same 1:2 bracket at a width that fits its noise (BTC 0.4%/0.8%). |
| 2. Vol-scaled size | `portfolio.vol_regime_scale`, desk | Size × `1/rv_ratio` (short-window vol over its norm), floored at 0.25×, on top of the vol target. Twice the usual vol means half the size. |
| 3. The list is the strategy | desk | Every asset is decided first, then acted on strongest signal first with a random tie-break, so shared caps don't go to whatever is listed first. Entries per list position are shown so you can check. |
| 4. Turn tools off | `research ablate`, `EnsembleEngine.impact_report`, desk column | Runs the strategy with each signal removed and reports the share of decisions that changed. A signal that changes under 1% of decisions is marked "does nothing: delete". On the synthetic practice data, breakout and order flow changed 0.0%. |
| 5. Second model before the exchange | `order_review.py`, `--reviewer code|jev|claude`, desk gate | Every entry gets a risk score (code, optionally averaged with Jev or Claude Haiku 5.5). The riskiest 10% are blocked, or logged only in shadow mode. The nightly review checks whether flagged entries actually stop out more often. Exits are never reviewed. |
| 6. Save what the model saw | `trace` on every journal decision, desk "Exactly what this decision saw" | The exact prompt or state, the system-prompt hash, the model that served it, the raw answer, tokens and latency. |
| 7. Replay before upgrading | `tradebot replay --engine claude:claude-haiku-5-5` | Re-runs saved inputs on another engine: direction and action agreement, Brier on resolved calls, tokens and dollars for both. |

**News overlay** (`news.py`, `tradebot news ...`, `research news-ablation`, desk "News use"):
- **Classification.** Every headline gets its asset, sentiment, materiality and regulatory risk, from a word list, Claude Haiku 5.5 or Jev.
- **Sentiment bias.** Shifts entry confidence by ±0.10 × recent sentiment.
- **Regulatory veto.** Blocks new longs for 24h after a material, non-positive regulatory headline about that asset.
- **Sources.** The feeds now include Fed and SEC press releases. Fed items attach to every asset, and SEC items that mention crypto attach to every crypto asset.
- **The test that matters.** `research news-ablation` runs no news, bias, veto and both, at several fee levels. A variant only "helps" if it wins at every fee level. Every run is recorded, so it counts against the Deflated Sharpe.
- **Example on random synthetic headlines.** "Both" happened to win at all three fee levels and "veto" at two of three. That is exactly the kind of noise this test, and the trial count, exist to catch.
- **History.** Import headline history from a CSV (`time,title[,source]`). Each item counts from its publish time plus 5 minutes.

**Jev** (`jev.py`):
- **Shapes.** Written against the documented `POST /v1/systemone` request and answer shapes.
- **One call per decision.** Six atomic questions per bar:
  - regime (choice);
  - direction (choice);
  - up-first and down-first for the bracket (two nouls);
  - toxic flow (noul);
  - setup quality (score).

  Code builds the Decision; risk state is arithmetic.
- **Failures.** A late answer misses its deadline and becomes a hold. 429/529 are retried within the deadline; 422 is never retried.
- **Traceability.** The model version that served each call is logged. Pin `TYPESAFE_MODEL` once thresholds are calibrated.
- **Not live-tested.** It has not been called live (no key here). `tradebot jev-check` makes one call to prove the key, version and latency.

**Run memory** (`runlog.py`):
- **What it is.** Each run directory has an append-only `run_log.md`: what was done, what was decided and why, what is open, and what was tried and rejected.
- **Who writes it.** Paper sessions and nightly reviews append to it automatically. The nightly review prompt reads it first.
- **Caching.** The Claude engine's system prompt goes first with `cache_control`, and the volatile snapshot goes last.

**Not built, and why:**
- **Unpaid TradingView "premium indicators".** That gets around a paywall and TradingView's terms.
- **"Leaked" Polymarket arbitrage repos, copy-trading of wallets, referral-link bot desks.** One of the pasted posts itself describes a hijacked org shipping key-stealing bots. Claims like "98% win rate" and "$89 → $7,769" can't be checked.
- **Avellaneda-Stoikov market making.** The posts' own numbers show naive cancel-replace loses to fees. It needs post-only maker access and a queue model before it is worth testing.
- **GPT-6 Astra, Kimi K3, Grok Bot desks.** Engines are pluggable. `replay` is how to decide whether a pricier model earns its bill.
- **AgenKit.** Still unverifiable.

## WHAT COULD I BE WRONG ABOUT?

- **There may be no edge at all.** Per-bar classification of BTC from 18 numbers
  is a crowded, mostly efficient game. The likeliest real outcome is
  "roughly fees lost". The paper period exists to find that out cheaply.
- **Costs could dominate.** 6 bps fee plus 2 bps slippage per side, plus spread:
  on 1-hour bars with 1.5×ATR stops, that's a meaningful share of each target.
  Your actual fee tier and fill quality matter more than any prompt.
- **LLM probabilities aren't calibrated.** Self-reported confidence clusters
  near round numbers and drifts across model versions. That's why sizing waits
  for 200+ measured outcomes, and why a model upgrade should reset the
  calibrator (delete `calibration.json`).
- **Stops are checked in a polling loop**, every 15 s in live paper and on bar
  high/low in the backtest. Real gaps and outages can skip far past a stop. The
  15% drawdown limit is checked when equity updates, so a large gap can overshoot
  it.
- **The data feed is untested end to end.** The build environment blocked
  outbound traffic to the exchange. Run `tradebot status --probe` first.
- **The review labels outcomes using snapshot mids at decision times**, not
  full tick paths, so it under-counts stop touches between bars.
- **The regime read in section 2 comes from one venue and 50 daily bars.** It's
  a sketch, not a macro view.
- **Survivorship and overfitting.** Each nightly "improvement" is a chance to
  fit noise. One change per night, a 7-day falsification window, and no change
  goes live the same night.
- **I couldn't verify AgenKit.** Jev turned out to be real and documented; its adapter
  is built from the docs but has not been called with a real key from here.
- **The 2%/4% bracket is the study's, not yours.** It beat model-chosen exits for
  their agents on their markets. Run `research sweep` over `fixed_stop_pct` /
  `fixed_take_pct` on your own data (every run is counted) before trusting it.
- **The pre-trade reviewer can block good trades.** It blocks the riskiest 10% by
  construction. Start it in shadow mode if you doubt it; the nightly review says
  whether flagged entries really stop out more.
- **Live venue code has only met fakes.** A field-name or rounding mismatch
  with a real API would surface as a rejected order. The ledger, reject kill and
  reconciliation are built to fail safe on exactly that, but expect a first-day
  fix. `live-check` and the $100 canary exist for this.
- **Yahoo data in the desk is unofficial and can lag.** It is fine for
  watching a paper strategy, not for live stock or FX execution. The live loop
  uses Alpaca's and OANDA's own feeds instead.

## START HERE FIRST

**Fastest:** open the hosted desk app (`app/tradebot.html`, published on claude.ai). It trades paper money on live Crypto.com prices through your Crypto.com connector.

**The full research → release workflow, from the `trading-agent` folder:**

```bash
export PYTHONPATH=src
python3 -m unittest discover -s tests                      # 92 tests (4 need Go)

# 1. Data with provenance (or use synthetic:N:seed to practise; it has no real edge)
python3 -m tradebot data snapshot --instrument BTC_USD --timeframe 1h --out data/btc_1h.json

# 2. Explore on the training window only; every run is recorded
python3 -m tradebot research sweep --family trend_v1 --data synthetic:4000:11 --train 0:2500 \
  --grid '{"reward_risk":[1.5,2,3],"stop_atr_mult":[1.0,1.5,2.0]}'
python3 -m tradebot register

# 3. Freeze one configuration, then evaluate it once on the untouched holdout
python3 -m tradebot research freeze --run <run_id> --reason "why this one"
python3 -m tradebot research final --candidate <cand_id> --data synthetic:4000:11 --holdout 2500:4000

# 4. Evidence package + reconciliation, then the operational gate
python3 -m tradebot evidence --run <final_run_id>
python3 -m tradebot opsgate

# 5. Operator: limits file, approval (interactive), paper runner
python3 -m tradebot init-limits
python3 -m tradebot approve --candidate <cand_id> --approver "your name"
python3 -m tradebot paper                                   # refuses without a valid release
python3 -m tradebot paper --dev                             # test run, labelled DEV, never promotable

# Stop trading instantly (closing positions is still allowed)
python3 -m tradebot kill --run-dir runs/paper

# Survival checks: which signals do nothing, does news help after fees, is a cheaper model as good
python3 -m tradebot research ablate --family trend_v1 --data synthetic:3000:11
python3 -m tradebot news import headlines.csv && python3 -m tradebot news classify --by lexicon
python3 -m tradebot research news-ablation --family news_v1 --data data/btc_4h.json --fees 1,2,3
python3 -m tradebot replay --run-dir runs/paper --engine claude:claude-haiku-5-5
python3 -m tradebot runlog --run-dir runs/paper

# 6. After 30+ days of paper: real money under canary caps (full runbook: LAUNCH.md)
python3 -m tradebot go-live --gates-only --operator you --venue cryptocom
python3 -m tradebot go-live --operator you --venue cryptocom     # interactive: type GO LIVE
python3 -m tradebot live-check --venue cryptocom                 # keys and balances, sends nothing
python3 -m tradebot live --venue cryptocom
```

On the synthetic practice data the best of 9 configurations beat buy-and-hold on its training window. Its one holdout evaluation then trailed buy-and-hold by about 45 points after costs, and approval refuses it as weak evidence. That's the workflow doing its job.
