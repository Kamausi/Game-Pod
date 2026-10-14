# Nightly review prompt (paste into Claude Code in this repo after `tradebot review`)

Read `runs/paper/review.md` and the last 24 hours of `runs/paper/journal.jsonl`.

1. Summarise every fill: the entry reason, the exit reason, and net P&L after fees.
2. List the five most expensive misses: confident directional calls (p_win in the
   top decile) that hit the stop.
3. Compare raw Brier, calibrated Brier, and base-rate Brier. If raw is not
   clearly below base rate, say "no measurable signal" and stop there. Don't tune
   thresholds to fit one day of data.
4. Propose at most ONE change, to the reflex prompt (`prompts/reflex_system.md`),
   a snapshot feature (`src/tradebot/state.py`), or a policy parameter. Explain
   why, and say what result over the next 7 days would show the change was wrong.
5. Make the change on a branch with a test, run
   `PYTHONPATH=src python3 -m unittest discover -s tests`, and re-run the
   backtest. Don't merge it: the operator approves.

Hard rules: never loosen anything in `RiskLimits`, never raise `kelly_multiplier`
above 0.25, never lower `min_confidence`, and never let a change go live the same
night it was made.
