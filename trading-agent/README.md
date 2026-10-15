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

The venue request formats were checked against each venue's official docs (section 16). Three mismatches were found and fixed. Authenticated calls still haven't been sent, because there are no keys here. That is why the runbook starts with the read-only `tradebot live-check` and a $100 cap. The Docker images were built and run here.

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

## 16. Verification record (2026-10-14)

Everything that was marked "not verified" has been checked against primary sources, and live where no key was needed.

| Item | How it was checked | Result |
|---|---|---|
| Crypto.com request signing | Official v1 docs, reference JS/Python | Matches: keys sorted and recursed, `method+id+api_key+params+nonce`, HMAC-SHA256 hex |
| Crypto.com create-order / get-order-detail | Official docs | Matches: `IMMEDIATE_OR_CANCEL`; terminal statuses `FILLED/CANCELED/REJECTED/EXPIRED`; `cumulative_quantity`, `avg_price`, `cumulative_fee`, `fee_instrument_name`; `POST /exchange/v1/private/...` |
| Crypto.com balances | Official docs | Structure matches. **Fixed:** cash for sizing now subtracts `reserved_qty` |
| Crypto.com instruments | **Live** public call | BTC/ETH/SOL/DOGE/PEPE_USD listed with `price_tick_size` / `qty_tick_size`. PEPE trades in 10,000-coin lots, which the rounding handles |
| Alpaca orders | Official docs | IOC allowed, lookup path `orders:by_client_order_id` correct. **Fixed:** fractional quantities are only allowed on market/day orders, so IOC limits now round down to whole shares |
| OANDA orders | Official v20 docs | IOC valid for limit orders; `stopLossOnFill` / `takeProfitOnFill`; `@clientID` order lookup |
| Jupiter swap | Docs and SDK example | `POST /swap/v1/swap`, priority fee nested under `prioritizationFeeLamports.priorityLevelWithMaxLamports` |
| Jito bundles | Official docs | **Fixed:** `sendBundle` goes to `/api/v1/bundles`; other methods to `/api/v1/<method>`. Tip last in the bundle; 1,000-lamport minimum |
| TypeSafe Jev | Official API reference | Request and answer shapes match |
| Docker images | Built and run here | Both images build. The hub serves gRPC and the dashboard; the trader runs as uid 10001 and reaches the hub over gRPC (2 µs in the hub). Compose resolves all four services. Building here needed the sandbox's TLS CA, added through a throwaway Dockerfile; the shipped Dockerfile is unchanged |
| The 3,505-agent study | Read the paper: arXiv 2609.05663, DX Research Group, Sep 2026 | Real. Fixed 2%/4% bracket +39.0 bps per position (60% of it blow-up prevention); 5.0× leverage in every volatility band; 1.75× at the top-3 cut; research sub-agent null; frontier models indistinguishable on 416 replayed scenarios; **no edge in either fleet** |
| AgenKit | agenkit.xyz | Real now: an npm CLI and a paid licensed kit of agents and skills for Claude Code. It is a coding harness, not part of the bot; it needs your licence key |

**What the paper changed here**
- **The paper's "second model" is an activation probe, not an LLM opinion.** It is a small model (Qwen3.5-4B) whose internal activations are read after the order text is written. It caught 71/205 liquidations at a 10% flag budget; plain features caught 57/205. `order_review.py` is the plain-feature version, plus an optional LLM probability, which is a different thing. Judge it by the nightly reviewer report, not by the paper's number.
- **Its single highest-value fix is "an atomic open-with-protection order path". That is now built:**
  - **Crypto.com:** `private/advanced/create-otoco`, entry `FILL_OR_KILL` so the stop and target arm only on a full fill.
  - **OANDA:** stop and target attached on fill.
  - **Alpaca:** OCO exit right after the IOC fill, because Alpaca brackets must be day/gtc.

  The loop mirrors exchange-side exits into the local book, cancels the legs before any kill-flatten, and persists them across restarts.
- **Provenance.** Every decision now records the engine source hash and a policy-config hash alongside the full trace.

**Added this round**
- **Market making (research only).** `research mm` runs Avellaneda–Stoikov quoting on candles. A quote fills only if the next bar trades through it. It charges maker fees and per-requote costs, and checks captured half-spread against fees plus 5-bar adverse selection. On the synthetic data it captured 9.1 bps per fill against 28.6 bps of adverse selection: it does not earn. There is no live path.
- **Prediction-market gap scanner (read-only).** `predmarkets --pairs pairs.json` compares Polymarket and Kalshi on pairs you confirmed resolve identically, net of each venue's taker fee, plus Polymarket complete sets. It never matches pairs automatically and never orders.
- **Still declined.** Free TradingView "premium" access, and the "leaked" bot repos and copy-trading desks. The pasted FrondEnt repo could not even be found.

## 17. The desk, weather markets, the quote loop, and claim audits (2026-10-15)

The pasted posts boil down to one setup: research seats that argue, a fast yes/no before the order, code that places it, and a human tap. This round builds that around a strategy that can be tested on free public data: C-suite insider buying.

### The six-seat desk (`desk.py`, `insider.py`; `tradebot desk ...`)

Each Jane Street rule is a seat. Each seat writes a file the next one reads, so you can open any step and check it.

| Seat | What it does | Rule |
|---|---|---|
| Scout | Reads the live EDGAR Form 4 feed. Keeps open-market purchases (code P) of $100K+ by a CEO, CFO, COO, President or Chair, filed since the last close. Liquid names only, at most 5. "NO TRADE TODAY" is a good result | Ignore 99% of the data |
| Analyst | Fair value from SEC XBRL diluted EPS times the stock's **own** history of P/E (25th / median / 75th percentile). Every number is sourced to an accession number. No positive trailing EPS: NO RANGE | Every trade is "what is it worth?" |
| Skeptic | Never proposes, only kills: batch filings, 10b5-1 plan buys, EXPENSIVE, NO RANGE, an unverified key number, earnings likely within 5 trading days, already moved 5%+ since the filing. Unsure = KILL | Most ideas are bad; your AI will cheat |
| Risk | Tickets for PASS names: DAY limit 0.5% above the last close, stop 2×ATR(14) below, shares = equity × 1% / (entry − stop), capped at 10% of equity. `risk_check()` reads `limits.json`; a KILL or PAUSE file means no tickets | Code guards the money |
| You | `tradebot desk approve 1 3` places today's tickets on the **Alpaca paper** account as OTO orders (the stop goes live with the fill). The broker class refuses any other host | Humans run the weird days |
| Journal | One row per ticket and per killed name, with what each kill **would** have made. A weekly review, lessons appended to `lessons.md`, and DECAYING after 20+ trades below half the backtest's average R | Every model decays |

Routines are systemd timers in New York time (`deploy/systemd/tradebot-desk-*.timer`). Each is a different seat command:

| Time (New York) | Seat |
|---|---|
| Weekdays 05:30 | morning |
| Weekdays 08:30 | chain-check (alerts CHAIN BROKEN) |
| Weekdays 15:45 | exit-due (cancels the stop, sells on trading day 5) |
| Weekdays 16:30 | journal |
| Friday 17:00 | weekly |
| Sunday 18:00 | audit |

**What the build caught**
- **Chair as "other".** A chair is often filed as `isOther` + "Chairman of the Board", not as an officer, so titles are matched from both fields.
- **10b5-1 plans.** Form 4 has an `aff10b5One` flag for trades under a 10b5-1 plan. Those are scheduled, not conviction, so the Skeptic kills them.
- **No Q4 in XBRL.** XBRL has no Q4 frame: Q4 EPS only exists inside the full year. Taking "the last four quarter frames" silently skips a quarter. Q4 is now derived as FY − Q1 − Q2 − Q3, and four consecutive quarters are required.

**The Quant seat**
- `tradebot insider backtest --data 2024q1_form345.zip ... --prices <bars>` runs the build window and logs the run in `quant/changes.md` with the commit and what changed.
- `tradebot insider holdout` runs the last 12 months **once**. A second run is refused.
- Costs are 0.1% per side.
- A gap through the stop fills at the open.
- Entry is the first open **after** the filing date.
- Signals that can't be priced are counted as "unpriced" and reported as the survivorship hole, never dropped.
- `--study-filters` replicates a published study's looser filters first, to check the data.
- `tradebot desk audit` is the Skeptic auditing the Quant. It runs the tests and reads git history for deleted or skipped tests. The test suite plants a deleted test and checks that the audit catches it.

**Not run here.** sec.gov is blocked from this build host, so no real backtest was run here. Every request shape below was checked live through the Firecrawl connector, and the tests use those real responses.

### Weather markets, read-only (`weather.py`; `tradebot weather scan <slug>`)

This is the open-source weather-bot setup with the missing parts made explicit:
- **Forecast:** the NWS hourly forecast at the **resolution station**.
- **Buckets:** Gaussian probabilities on whole-degree readings.
- **Edge:** after Polymarket's taker fee (0.05 × p × (1 − p)).
- **Size:** quarter Kelly, capped at 5%.

**Learning.** `weather settle` records observed highs, and after 30 settled days per station the forecast bias and spread are learned from that station's own errors. Until then the error model is a labelled PRIOR.

**What the market actually resolves on.** The NYC market settles on **LaGuardia (KLGA)**, not Central Park. The scanner reads the station from the market's resolution source and refuses a market without one.

**First real run** (2026-10-15, October 16 market):

| Bucket | Model | Market ask | What it means |
|---|---|---|---|
| 74–75°F | 22% | 39¢ | Market is far more confident than the PRIOR |
| 80–81°F | 4.2% | 0.4¢ | "Edge" only because the PRIOR spread is wide |

NWS forecast 74°F, lead 40 h. The model's "edges" sit in the tails because its PRIOR spread (3.5°F) is wider than the market's. That is not an edge until the learned spread says so.

### The jev-trader loop as a dry run (`jevquote.py`; `tradebot jevquote --record BTC_USD --tick 0.01`)

This is the loop from the post, with no order path in the code:
1. Read the book and ask buy or sell (Jev, or the momentum stand-in).
2. Post one post-only quote one tick inside the touch.
3. Replace it on the next step.

A fill is counted only when the other side comes to the quote's price. The summary reports:
- spread captured;
- 1- and 10-step markouts;
- decision hit rate;
- latency p50 / p95;
- the same "does it earn after adverse selection" verdict as `research mm`.

Run it on 1,000 recorded steps before believing "earn the spread instead of paying it".

### Auditing a claim (`audit.py`; `tradebot audit ...`)

The evidence half of the "reverse engineer any system" roadmap, in code we own:
- **`headline`:** every start date, calendar years, worst fall, and after your costs and tax.
- **`finding`:** DATA VALIDATED / TRADE-OFF / NO EDGE, split by time. Validated means it beats the benchmark on both CAGR and Sharpe in both windows.
- **Plateau or lone peak:** for parameter sweeps.
- **`trend200`:** the boring rival.
- **`secrets`:** hard-coded keys, reported as file and line only, never the value.
- **`spec`:** the claim with every undisclosed field UNKNOWN.

On a synthetic 26-year series, the 200-day rule gave a lower CAGR than buy-and-hold (7.4% vs 9.7%) for a smaller worst fall (36.5% vs 42.1%). That is the trade-off it is known for, not an edge.

### Verified this round

| Item | How | Result |
|---|---|---|
| EDGAR current Form 4 feed | Live, via Firecrawl | Atom. Each filing appears twice (Reporting and Issuer), one accession number. Deduplicated |
| Form 4 XML | Live filing (Procore, 2026-10-14) | `reportingOwnerRelationship`, `officerTitle` / `otherText`, `aff10b5One`, `transactionCode` P/S/M, shares, price. Saved as a fixture |
| Insider Transactions Data Sets | sec.gov page + readme PDF | Quarterly `form345.zip`, 2006 onward. SUBMISSION / REPORTINGOWNER / NONDERIV_TRANS. Dates DD-MON-YYYY. The PDF did not render two REPORTINGOWNER column names, so the loader checks columns by name and refuses with the list it found |
| SEC XBRL companyconcept | Live | `units["USD/shares"]` with `frame`, `accn`, `form`, `filed`. No Q4 frame (derived) |
| Alpaca OTO | Official docs | `order_class: "oto"` with `stop_loss` only; day/gtc; stop at least $0.01 below a limit entry |
| Alpaca daily bars | Official OpenAPI | `GET data.alpaca.markets/v2/stocks/{symbol}/bars?timeframe=1Day`, paginated. Free accounts need `feed=iex` |
| Polymarket weather event | Live | One market per bucket. `groupItemTitle` "63°F or below" / "64-65°F" / "82°F or higher"; taker fee 0.05. Resolution source names the station |
| NWS | Live | `/stations/KLGA/observations/latest` (°C), `/points/{lat},{lon}` → `forecastHourly` + `timeZone`, hourly periods in °F |

### Declined or left out, and why

| Item | Why |
|---|---|
| Grok Bot "0.18 Reconstructed" / "Original" bundles | Rebuilt or unminified copies of a company's shipped app. Not using them |
| REA on other people's apps | Our system is our own source, so there's nothing to reverse. `audit` covers the evidence half |
| CrawlScan, CrawlHive and their tokens | Token promotions with self-reported track records |
| tradingview-mcp | Drives TradingView Desktop through unofficial interfaces; the author flags the ToS himself |
| Pump.fun snipers | Not part of this desk |
| QuantGPT, AgenKit | Paid services; nothing here depends on them |
| OpenBB, freqtrade, NautilusTrader, ccxt, TradingAgents, QuantDinger | Not added as dependencies. This repo already has data adapters, a backtest that shares code with the live loop, venue adapters, and argue-then-kill seats |
| Polymarket/agents | Archived (last commit Nov 2024), as the post says |

Most of the 23 memecoin repos are wallet hygiene (revoke.cash, Rabby, phishing lists): worth using, nothing to build. On "fake repos": an old account with thousands of stars and a Telegram link is the drainer pattern. `audit secrets` won't catch that; reading the code will.

## 18. Bundle check, the desk as a function, thesis memory (2026-10-15)

### `tradebot tokencheck <mint>`: was this launch bundled? (`bundlecheck.py`, read-only)

This is the "check before you ape" list from the pump.fun bundling post, done from the chain itself. It needs no keys and never sends a transaction.
- **Launch block.** It walks the mint's history back to the creation transaction and lists every wallet that bought in the first slots, with its share of supply.
- **Shared funders.** For each of those wallets it finds who funded it (the fee payer of the first transaction that raised its SOL balance). It clusters wallets by a shared funder, by funding from the deployer, or by funding from each other.
- **Holdings now.** It compares those wallets with today's largest holders: what the cluster and the deployer **still** hold is what can be sold into your buy.
- **Verdicts:**
  - DANGER: a cluster still holds 15% or more, or launch-block buyers still hold 20% or more.
  - CAUTION: there was a bundle, but it has mostly sold.
  - NO BUNDLE FOUND: not a safety guarantee.
  - UNVERIFIED: the launch block wasn't reached. It fails closed; busy tokens need an archive RPC.
- **Callouts.** `--callouts calls.csv --prices prices.csv` compares each callout with the price action: how far price had already run in the hour before the call (pre-positioning), and what it did 1 h and 24 h after.

The Solana RPC shapes (`getSignaturesForAddress` newest first with `before` paging, `getTransaction` token balances by owner) were checked against solana.com/docs. The tests run a fake chain: a 5-wallet launch bundle, 4 wallets sharing one funder, 1 funded by the deployer.

**Not built.** Bundlers, volume bots, holder bots, bump bots, or any shilling or callout automation. They exist to make the "dump on the new buyers" flow in that post work. This repo builds the side that detects it.

### The desk as a function (`graph.py`; `tradebot desk graph`)

Coatue's frame: external + internal context in; what, how much and when out.
- **Nodes.** The desk is written down as nodes (ingest → scout → analyst → skeptic → risk → PM → execution → journal). Each node has its inputs, outputs and **route**: code for explicit rules, a person for judgment. ML and LLM routes slot in when a node earns one.
- **Targets.** Each node has a measurable target, scored from the journal:

| Node | Target |
|---|---|
| Ingest | source error rate ≤ 5% |
| Scout | average R of everything scouted ≥ 0 |
| Analyst | RANGE names beat EXPENSIVE names |
| Skeptic | passed names beat killed names |
| Risk | worst loss ≥ −1.5R |
| You (PM) | approved beat rejected |
| Execution | fill rate ≥ 60% |
| Journal | live ≥ half the backtest's R |

  Under 10 outcomes, a node reads "NOT ENOUGH DATA" instead of a verdict.

### Thesis memory ("can it remember why yesterday's thesis failed?")

- **What is stored.** Every scouted name is kept in `desk/memory/theses.jsonl` with its thesis:
  - role and size;
  - a valuation bucket ("CFO buy, deep value");
  - the decision and its reason.

  The journal fills in the outcome in R, actual or what a kill would have made.
- **What reads it.** The Analyst writes similar past theses into each card. The Skeptic kills a pattern that lost money at least 3 times.
- **Look it up.** `tradebot desk memory ACME` shows the history for one symbol.

### Fixed

A local `import os` inside `cli.main` shadowed the module-level import, so the previous commit's `tradebot` command line failed on start. It's fixed, and a test now runs the command line end to end.

## 19. Claude analyst seat, open-source platforms, real-data tooling, the desk in Docker (2026-10-15)

### The Claude seat (`llm_analyst.py`; `tradebot desk --llm morning`)

A TradingAgents-style debate beside the code analyst, run only on names the code priced RANGE:
1. **Bull** makes the case for buying.
2. **Bear** makes the case against and answers the bull.
3. **Judge** gives its own verdict (RANGE / EXPENSIVE / NO VIEW) and says whether it agrees with the code.

**It may use only numbers it is handed.** The model fetches nothing. It gets a numbered fact table:
- the Form 4 role, size and date;
- SEC diluted EPS by quarter;
- the last close;
- the code's own-history P/E range;
- the desk's memory of similar theses.

Every point must cite fact ids. Then `verify()` checks the output **in code**: every point must cite a real fact, and every number written in a point must match a cited fact (rounding, %, $K/$M forms allowed). One invented number fails the whole opinion.

**It can only kill.** The skeptic kills the name if:
- the judge disagrees with the code;
- verification fails (UNVERIFIED);
- the call errors or the model refuses (ERROR).

Unsure = KILL. The seat never adds a trade.

**Request settings** (from the current Claude API docs):
- `claude-opus-5-5` with adaptive thinking (the default on this model, so it is omitted) and `effort` set explicitly (`--llm-effort`, default high);
- JSON output via `output_config.format`, with the system prompt cached;
- `stop_reason` "refusal" and "max_tokens" are handled before the content is read;
- **server-side refusal fallback is on** (`betas=["server-side-fallback-2026-07-01"]`, `fallbacks="default"`), and the model that actually answered is recorded in the card's traces.

**Cost:** 3 calls per RANGE name (at most 5 names a day).

Turn it on with `--llm` or `TRADEBOT_DESK_LLM=1`; it needs `ANTHROPIC_API_KEY`.

### Open-source platforms (`platforms.py`)

All three were checked against the installed libraries on 2026-10-15, not from memory:

| Platform | What it does here | Verified against |
|---|---|---|
| **ccxt** | `tradebot ccxt ticker\|book\|bars\|balance --exchange X`: market data from any of ccxt's 103 exchanges. Orders run **only on an exchange's sandbox/testnet**; with `sandbox=False` every order method refuses | ccxt 4.5.88. Kraken and Coinbase have `urls["test"] = None`, and ccxt's own `set_sandbox_mode` then crashes with a TypeError, so the adapter checks first and refuses by name |
| **OpenBB** | `--prices openbb[:provider]` for the desk, the backtest and `insider prices`. Same `.bars()` interface as the CSV and Alpaca sources | openbb-core 4: `obb.equity.price.historical(symbol, start_date, end_date, provider)`; result rows carry date, open, high, low, close, volume. An empty result (`EmptyDataError`) is treated as no bars |
| **freqtrade** | `tradebot export freqtrade --kind trend200\|signals --out ...` writes an `IStrategy` (INTERFACE_VERSION 3) with the same stop, a `custom_exit` time exit, and no ROI ladder | freqtrade 2026.9: both kinds loaded by freqtrade's `StrategyResolver` and produced entries and exits on a test frame. freqtrade trades crypto only, so the insider desk's stock signals don't port; trend200 and any crypto `pair,date` list do |

### The real insider backtest (run where sec.gov is reachable)

```
export SEC_USER_AGENT="your-name you@example.com"
tradebot insider fetch 2016q1-2026q3 --out data/sec          # quarterly form345.zips; skips ones already there
tradebot insider prices --data data/sec/*.zip --source alpaca --out data/bars   # or --source openbb
tradebot insider backtest --data data/sec/2016q1_form345.zip ... --prices data/bars
tradebot insider holdout  --data ... --prices data/bars      # once, ever
```

- **Downloads.** `fetch` tries both SEC paths (the newest quarters moved to `/files/datastandardsinnovation/` in 2026, so that path goes first from 2026q2 on). It refuses anything that isn't a valid zip and writes atomically.
- **Prices.** `prices` writes one CSV per symbol that had a qualifying buy, plus SPY. It lists the symbols with no bars: mostly delisted names, i.e. the survivorship hole the backtest reports as "unpriced".
- **Check your feed's history.** Free Alpaca IEX data may not reach back as far as the zips, so look at the `first` date per symbol.

**Not run.** None of this has run on real data yet: sec.gov is blocked from this build environment.

### The desk in Docker (`tradebot desk schedule`)

```
cp .env.example .env     # APCA paper keys, SEC_USER_AGENT; optional ANTHROPIC_API_KEY + TRADEBOT_DESK_LLM=1
docker compose run --rm desk desk --dir desk init
docker compose --profile desk up -d desk
docker compose run --rm desk desk --dir desk approve 1 2    # you, the same day
```

- **Schedule.** `schedule` runs the same New York table as the systemd timers: morning 05:30, chain-check 08:30, exit-due 15:45, journal 16:30 (Mon–Fri), weekly Fri 17:00, audit Sun 18:00.
- **Isolation and alerts.** Each routine runs as its own subprocess, logged to `desk/schedule.log`. A non-zero exit sends an alert.
- **Missed runs.** A routine missed while the container was down is not run late; chain-check reports the gap.
- **Holidays.** Market holidays aren't skipped; the scout just finds nothing.
- **Choosing a setup.** systemd hosts keep using `deploy/systemd/`; Docker hosts use this.

**Verification.** `docker compose config` validates, and the image's install step (`pip install ".[all]"`, now including ccxt and anthropic ≥ 1.0) was run in a clean Python 3.12 venv. `docker build` itself was not run, because there is no Docker daemon in the build environment.

## 20. The forensic pass: published statistics, an ensemble weather source, one implementation of each (2026-10-15)

### Where the bot was weak, and what fixes it

The approach was to find where the bot was weakest, then fix each gap with the standard published method. Each formula is implemented once, in `quant.py`, in plain Python (the bot still needs no numpy). Each is checked in `tests/test_quant.py` against an independent implementation or the paper's own numbers.

| Weakness found | Method (source) | Checked against | Now used by |
|---|---|---|---|
| A backtest that tried many configurations reports the best one as if it were the only one | Deflated Sharpe ratio, probabilistic Sharpe, minimum track record (Bailey & López de Prado 2012, 2014) | The paper's expected-max-Sharpe values (N=100 → 2.53, N=1000 → 3.26) and a 4,000-run simulation | `insider backtest`: every run is logged to `quant/trials.jsonl`, and the verdict says **NO EDGE (deflated)** when DSR < 0.95. Also `audit dsr` |
| "Average R > 0" with no idea how uncertain it is; trades in the same week aren't independent | Stationary block bootstrap (Politis & Romano 1994) | `arch.bootstrap.StationaryBootstrap`: CI width within 15% | Backtest report `avg_r_ci95`; the verdict says **NOT PROVEN** while the interval includes 0 |
| No way to tell whether picking the best configuration generalises | Probability of backtest overfitting via CSCV (Bailey, Borwein, López de Prado & Zhu 2015) | Pure noise gives PBO ≈ 0.5; a real edge gives < 0.1 | `audit pbo matrix.csv` |
| Testing many rules at once inflates false discoveries | Benjamini–Hochberg FDR (1995) | `statsmodels multipletests(fdr_bh)`: identical | `quant.bh_fdr` |
| "Beat SPY" ignored the stock's beta: a 1.5-beta stock beats SPY in any up week | Market-model event study (MacKinlay 1997): β from trading days −130..−11, gap before the event | `statsmodels` OLS: identical α, β, residual σ | Every backtest trade carries `car` and `beta`; the report shows `avg_car` and `car_t` |
| Trend rule without a test of whether the series trends at all | Lo–MacKinlay variance ratio, heteroskedasticity-robust z | `arch.unitroot.VarianceRatio`: identical to 1e-6 | `audit vr` |
| ATR from closes needs many days and ignores overnight gaps | Parkinson, Garman–Klass, Rogers–Satchell, **Yang–Zhang** (2000) range volatility | Simulated price paths recover the true vol; Yang–Zhang's spread is < 0.6× close-to-close's | Every ticket carries `vol_yz_daily`; also `audit vol` |
| Vol forecasts that ignore clustering | EWMA λ=0.94 (RiskMetrics) and GARCH(1,1) by maximum likelihood (Bollerslev 1986) | `arch.arch_model`: α, β within 0.02–0.03, next-day vol within 3% | `audit vol` |
| Size limits ignored what the order does to the price | Square-root market impact, cost ≈ ½ spread + σ·√(Q/ADV) (Almgren et al. 2005; Tóth et al. 2011) | The formula itself (doubling at 4× size) | Every ticket carries `impact_bps`; `risk_check` refuses above `max_impact_bps` (25, new in `limits.json`) |
| Equal or naive weights across correlated sleeves | Ledoit–Wolf shrinkage (2004); Hierarchical Risk Parity (López de Prado 2016) | `sklearn ledoit_wolf`: identical. PyPortfolioOpt HRP: identical to 1e-6 | `audit hrp returns.csv` |
| Probabilities stated, never scored | Brier score, log loss, reliability bins and ECE; isotonic regression by pool-adjacent-violators | `sklearn` Brier, log loss and `IsotonicRegression`: identical | `weather calibration`, `audit calibration p,y.csv` |
| "DECAYING" only after 20 trades and below half the backtest | Page's one-sided CUSUM (1954), k = 0.5σ, h = 5σ | No false alarm on 200 in-control trades; a 1σ drop is caught within 30 trades | `desk weekly`: **DECAYING (CUSUM)** as soon as it shows |

**Two bugs the reference checks caught:**
- **Variance ratio:** my first z statistic was missing a √n factor (θ is the variance of √n·(VR−1)). arch's number exposed it.
- **HRP clustering:** a common HRP variant clusters on the distances between rows of the distance matrix, while PyPortfolioOpt clusters on the correlation distance itself. Matching it to 1e-6 settled which one.

**Two implementations had grown up twice.** The crypto research workflow already had its own deflated Sharpe (`validation.py`) and Brier score (`review.py`). Both now call `quant.py`, and a test pins them equal.

### Weather: real ensemble members instead of an assumed σ

The scan's known weak spot (§17) was that tail "edges" came from a PRIOR σ. **Open-Meteo's free ensemble API** gives every run of GFS (31) and ECMWF (51), 82 forecasts of tomorrow's high. Verified from a live response on 2026-10-15:
- **Key names:** with two models the keys carry the model's own suffix, which is not the requested id. `gfs025` comes back as `temperature_2m_member01_ncep_gefs025` and `ecmwf_ifs025` as `..._ecmwf_ifs025_ensemble`; the control run has no member number.
- **Grid snap:** the point snaps to the 0.25° grid (40.75, −74.0 for KLGA).
- **Spread:** for 2026-10-16 the runs' highs span about 71.7–78.3 °F around the NWS point forecast of 74.

`weather scan --source blend` (the new default) averages the NWS probability with an **ensemble-dressed** one (Roulston & Smith 2003):
- **Dressing:** each run's high, minus the station's learned bias, becomes the centre of a 1.5 °F normal kernel. The kernel covers the grid-to-station offset and hourly sampling.
- **Learning:** after 30 settled days the bias and kernel are learned per station: kernel² = var(ensemble-mean error) − mean(spread²).
- **Fallback:** if the ensemble host is unreachable, the scan uses NWS alone and says so.
- **Calibration:** `weather settle` stores every bucket probability with its 0/1 outcome, and `weather calibration` scores them (Brier, reliability).

Open-Meteo is free for non-commercial use (open-meteo.com terms), and the server is open source if you need to self-host. The fixture is a trimmed copy of the real response.

### Efficiency

- **Parallel downloads:** `insider prices` now downloads with 4 threads behind a shared token-bucket limit, 180 requests/min (Alpaca's free tier allows 200). Slow requests overlap instead of queueing, so a few thousand symbols take minutes at the rate cap, not however long one-at-a-time takes.
- **Python is fast enough here.** Measured on this machine:

| Routine | Time |
|---|---|
| Bootstrap, 5,000 trades × 1,000 resamples | 0.7 s |
| GARCH fit, 10 years of daily bars | 0.2 s |
| Ledoit–Wolf + HRP, 40 assets × 3 years | 0.4 s |
| PBO, 50 configurations × 1,000 periods | 3.8 s |

  These run once per backtest or once a day, so numba, polars, a Rust port or moving them into the Go `exechub` would add a toolchain for no visible gain. The Go service stays where latency matters (order routing).

### Considered and not used

| Candidate | Why not |
|---|---|
| mlfinlab | Now commercially licensed. The methods above were implemented from the papers instead |
| TA-Lib | The indicators here are a few lines each, and TA-Lib's C build is a deployment burden |
| PyPortfolioOpt / arch / statsmodels / scikit-learn at run time | Used as test references only (`pip install '.[reference]'`); the bot keeps zero required dependencies. PyPortfolioOpt 1.6's `HRPOpt.optimize()` is broken on scipy 1.18 (it calls the removed `sch._LINKAGE_METHODS`), so the test runs its own steps |
| FRED, Quandl | They need keys, and no current strategy uses macro data |

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
python3 -m unittest discover -s tests                      # 100 tests (4 need Go)

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
