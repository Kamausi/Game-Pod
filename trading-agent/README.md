# tradebot: a 24/7 crypto trading agent that starts on paper

A complete, tested system: a deterministic state engine, a typed model-decision
schema, code-owned gates and sizing, a hard risk layer with a kill switch, a paper
broker that charges realistic costs, a 24/7 loop, and a nightly calibration review.

It is **paper-only by design**. No code in this package can place a real order.

---

## 0. What changed from the viral prompt, and why

| Prompt said | What this build does | Why |
|---|---|---|
| Build through "AgenKit" at agenkit.xyz | Not used. The six phases are run as plain Claude Code steps in this repo (section 6). | A web search on 2026-10-14 turned up only the promotional post itself: no docs, no source, no install instructions. Installing an unverified harness into your coding agent hands it your shell. |
| "Jev" from console.typesafe.ai as the reflex model | Not used. The reflex is a pluggable `DecisionEngine`: a deterministic `RuleEngine` (default), or `ClaudeEngine` on Claude Haiku 5.5 with escalation to Claude Opus 5.5. | No public API docs could be found, so writing an adapter would mean inventing an API. If it turns out to be real, it fits behind the same interface in about 30 lines (section 4). |
| Use the model's "calibrated probability" for Kelly sizing | Probabilities are **shrunk toward 0.5** until at least 200 scored outcomes exist, then mapped through an isotonic calibration fitted nightly. | A model saying it is calibrated doesn't make it calibrated. Calibration has to be measured. |
| Execute automatically, live | Paper broker only. Going live is a separate step you take after passing the launch gates (section 9). | The prompt itself says to prioritise a system that survives. |
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
5. Only then write a live broker. Route it through `GuardedBroker`, use an
   exchange API key with **trade-only, no-withdrawal** permissions and an IP
   allowlist, and start with an amount you'd be fine losing entirely.

## 10. What the included backtest shows

On 3,000 synthetic hourly bars (a random walk with no edge to find):
return −1.2%, fees $427 on a $10k account, 118 round trips, win rate 36%,
max drawdown 5.4%. The review then flagged: *"Model p_win is no better than the
base rate. Do not go live."*

**That is the system working.** With no edge, it loses roughly its costs,
risk stays bounded, and the review catches it. A big profit on that data would
mean a bug.

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
- **I couldn't verify AgenKit or Jev.** If they're legitimate, I may be
  underselling them. The architecture doesn't depend on them either way.

## START HERE FIRST

```bash
cd trading-agent
PYTHONPATH=src python3 -m unittest discover -s tests

# 1. See the full pipeline on synthetic data (no network, no keys)
PYTHONPATH=src python3 -m tradebot backtest --synthetic 3000 --run-dir runs/bt
PYTHONPATH=src python3 -m tradebot review --run-dir runs/bt

# 2. Check the exchange feed from your machine
PYTHONPATH=src python3 -m tradebot status --probe

# 3. Paper trade 24/7 with the free rules engine (run under tmux/systemd)
PYTHONPATH=src python3 -m tradebot paper --instrument BTC_USD --timeframe 1h --engine rules

# 4. Optional: the Claude reflex (costs API credits on every bar)
pip install anthropic && export ANTHROPIC_API_KEY=...
PYTHONPATH=src python3 -m tradebot paper --engine claude --run-dir runs/paper-claude

# Stop trading instantly (exits still allowed):
PYTHONPATH=src python3 -m tradebot kill --run-dir runs/paper
```

Run `rules` and `claude` side by side on paper for 30 days. If Claude doesn't
beat the free baseline after fees, you've learned that for $0 of trading
capital.
