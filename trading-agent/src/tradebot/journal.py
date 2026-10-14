"""Append-only JSONL journal: every decision, order, fill, and risk event."""
from __future__ import annotations

import json
from dataclasses import asdict, is_dataclass
from pathlib import Path


class Journal:
    def __init__(self, path: str | Path | None):
        self.path = Path(path) if path else None
        if self.path:
            self.path.parent.mkdir(parents=True, exist_ok=True)

    def write(self, kind: str, ts: int, **fields) -> None:
        if not self.path:
            return
        rec = {"kind": kind, "ts": ts}
        for k, v in fields.items():
            rec[k] = asdict(v) if is_dataclass(v) else v
        with self.path.open("a") as f:
            f.write(json.dumps(rec, separators=(",", ":")) + "\n")


def read_journal(path: str | Path) -> list[dict]:
    with Path(path).open() as f:
        return [json.loads(line) for line in f if line.strip()]
