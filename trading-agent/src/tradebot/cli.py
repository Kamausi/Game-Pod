"""tradebot CLI: backtest | paper | review | kill | unkill | status"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .backtest import load_candles, run_backtest, synthetic_candles
from .engines import build_engine
from .review import run_review
from .risk import RiskLimits, RiskManager


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="tradebot")
    sub = ap.add_subparsers(dest="cmd", required=True)

    b = sub.add_parser("backtest")
    b.add_argument("--data", help="JSON list of candles (open_ts, close_ts, open, high, low, close, volume)")
    b.add_argument("--synthetic", type=int, default=0, help="use N synthetic bars instead (plumbing test only)")
    b.add_argument("--engine", default="rules", choices=["rules", "claude"])
    b.add_argument("--run-dir", default="runs/backtest")

    p = sub.add_parser("paper")
    p.add_argument("--instrument", default="BTC_USD")
    p.add_argument("--timeframe", default="1h")
    p.add_argument("--engine", default="rules", choices=["rules", "claude"])
    p.add_argument("--run-dir", default="runs/paper")
    p.add_argument("--cash", type=float, default=10_000.0)

    r = sub.add_parser("review")
    r.add_argument("--run-dir", default="runs/paper")

    for name in ("kill", "unkill", "status"):
        k = sub.add_parser(name)
        k.add_argument("--run-dir", default="runs/paper")
        if name == "kill":
            k.add_argument("--reason", default="operator kill")
        if name == "status":
            k.add_argument("--probe", action="store_true", help="hit the exchange API once")

    a = ap.parse_args(argv)
    run_dir = Path(a.run_dir)

    if a.cmd == "backtest":
        candles = load_candles(a.data) if a.data else synthetic_candles(a.synthetic or 3000)
        risk = RiskManager(RiskLimits(), run_dir / "_engine_KILL")  # engine-side only
        print(run_backtest(candles, build_engine(a.engine, risk), run_dir))
        return 0
    if a.cmd == "paper":
        from .loop import run_paper

        run_paper(a.instrument, a.timeframe, a.engine, run_dir, a.cash)
        return 0
    if a.cmd == "review":
        print(run_review(run_dir))
        return 0

    risk = RiskManager(RiskLimits(), run_dir / "KILL", run_dir / "risk_state.json")
    if a.cmd == "kill":
        risk.kill(a.reason)
        print(f"KILLED: {a.reason}. New risk is blocked; exits still allowed.")
    elif a.cmd == "unkill":
        # Deliberately manual: clearing a drawdown kill is a human decision.
        risk.latched = None
        risk.peak_equity = 0.0  # re-anchor the drawdown peak, or the next update re-trips the kill
        risk.kill_file.unlink(missing_ok=True)
        risk._save()
        print("kill switch cleared")
    elif a.cmd == "status":
        print(json.dumps({"killed": risk.kill_reason(), "peak_equity": risk.peak_equity,
                          "halted_today": risk.halted_today, "orders_today": risk.orders_today}, indent=2))
        if a.probe:
            from .feed import CryptoComFeed

            f = CryptoComFeed("BTC_USD", "1h")
            bk = f.book()
            print(f"book ok: bid={bk.best_bid} ask={bk.best_ask}; candles ok: {len(f.closed_candles(bk.ts))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
