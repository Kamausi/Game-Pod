"""The run log: the memory the next run reads first.

Every run that matters (a paper session, a nightly review, a research sweep) appends
one short entry: what was done, what was decided and why, what is still open, what
was tried and rejected. Plain text, one file per run directory, appended, never
rewritten. The nightly review prompt reads the last entries before anything else,
so settled questions are not re-litigated and rejected ideas are not retried.
"""
from __future__ import annotations

import time
from pathlib import Path

FILE = "run_log.md"


def append(run_dir: Path, done: list[str], decided: list[str] | None = None, open_items: list[str] | None = None,
           rejected: list[str] | None = None, now_ms: int | None = None) -> Path:
    p = Path(run_dir) / FILE
    p.parent.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y-%m-%dT%H:%MZ", time.gmtime((now_ms or time.time() * 1000) / 1000))
    lines = [f"## {stamp}"]
    for title, items in (("Done", done), ("Decided (and why)", decided), ("Open", open_items), ("Tried and rejected", rejected)):
        if items:
            lines.append(f"**{title}**")
            lines += [f"- {x}" for x in items]
    with p.open("a") as f:
        f.write("\n".join(lines) + "\n\n")
    return p


def recent(run_dir: Path, n: int = 5) -> str:
    p = Path(run_dir) / FILE
    if not p.exists():
        return ""
    entries = p.read_text().split("\n## ")
    tail = entries[-n:]
    return ("## " + "\n## ".join(e.lstrip("# ") for e in tail)).strip() if tail else ""
