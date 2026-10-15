"""The desk's routines on New York time, for a container where systemd timers are not available.

Same table as deploy/systemd/tradebot-desk-*.timer. Each routine runs as its own subprocess
(`tradebot desk ... <seat>`), so a crash in one seat cannot take the scheduler down, and a
non-zero exit sends an alert (TRADEBOT_ALERT_WEBHOOK) like systemd's OnFailure does.
A routine whose time passed while the process was down is not run late: the next morning's
chain-check reports the gap. Market holidays are not skipped (the scout then finds nothing).
"""
from __future__ import annotations

import json
import subprocess
import sys
import time
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

NY = ZoneInfo("America/New_York")
WEEKDAYS = (0, 1, 2, 3, 4)
JOBS = (  # seat, weekdays (Mon=0), "HH:MM" New York
    ("morning", WEEKDAYS, "05:30"),
    ("chain-check", WEEKDAYS, "08:30"),
    ("exit-due", WEEKDAYS, "15:45"),
    ("journal", WEEKDAYS, "16:30"),
    ("weekly", (4,), "17:00"),
    ("audit", (6,), "18:00"),
)


def next_run(now: datetime, jobs=JOBS) -> tuple[str, datetime]:
    """The next (seat, time) strictly after `now` (any tz-aware datetime), in New York time."""
    now = now.astimezone(NY)
    best = None
    for seat, days, hm in jobs:
        h, m = map(int, hm.split(":"))
        for k in range(8):
            d = (now + timedelta(days=k)).date()
            if d.weekday() not in days:
                continue
            t = datetime(d.year, d.month, d.day, h, m, tzinfo=NY)
            if t > now:
                if best is None or t < best[1]:
                    best = (seat, t)
                break
    return best


def command(seat: str, desk_dir: str, prices: str, llm: bool, effort: str) -> list[str]:
    return [sys.executable, "-m", "tradebot", "desk", "--dir", desk_dir, "--prices", prices] + (["--llm", "--llm-effort", effort] if llm else []) + [seat]


def loop(desk_dir: str, prices: str, llm: bool = False, effort: str = "high", clock=lambda: datetime.now(NY),
         sleep=time.sleep, run=subprocess.run, alert=None, max_runs: int | None = None) -> int:
    from pathlib import Path
    log = Path(desk_dir) / "schedule.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    runs = 0
    while max_runs is None or runs < max_runs:
        seat, at = next_run(clock())
        while (left := (at - clock()).total_seconds()) > 0:
            sleep(min(left, 60))  # short naps: survives suspend and clock changes
        t0 = time.monotonic()
        p = run(command(seat, desk_dir, prices, llm, effort), capture_output=True, text=True, timeout=3600)
        rec = {"seat": seat, "scheduled": at.isoformat(), "exit": p.returncode, "seconds": round(time.monotonic() - t0, 1),
               "stdout_tail": (p.stdout or "")[-400:], "stderr_tail": (p.stderr or "")[-400:]}
        with log.open("a") as fh:
            fh.write(json.dumps(rec) + "\n")
        print(json.dumps(rec), flush=True)
        if p.returncode != 0 and alert:
            alert("warn" if seat == "chain-check" else "critical", f"desk {seat} exited {p.returncode}", tail=rec["stderr_tail"] or rec["stdout_tail"])
        runs += 1
    return 0
