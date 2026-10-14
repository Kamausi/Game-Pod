"""Import long price histories (decades of daily stock/FX bars, or any OHLCV CSV)
with validation and provenance, so deep backtests start from checked data.

Problems are REPORTED, never silently fixed: duplicates and broken bars are
dropped and counted; gaps and outliers are listed; nothing is clipped.
Adjusted vs unadjusted prices matter for stocks: record which you have.
"""
from __future__ import annotations

import csv
import json
import math
import statistics
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path

from .types import Candle

DAY_MS = 86_400_000


@dataclass
class ImportReport:
    source: str
    rows: int = 0
    kept: int = 0
    duplicates: int = 0
    broken: int = 0  # non-positive prices, high < low, close outside [low, high]
    gaps: list[tuple[str, str]] = field(default_factory=list)  # (from, to) longer than max_gap
    outliers: list[tuple[str, float]] = field(default_factory=list)  # (date, log return) beyond z threshold
    first: str = ""
    last: str = ""
    adjusted: str = "unknown"

    def summary(self) -> str:
        return (f"{self.source}: {self.kept}/{self.rows} bars kept, {self.duplicates} duplicates, {self.broken} broken, "
                f"{len(self.gaps)} gaps, {len(self.outliers)} outliers flagged, {self.first} to {self.last}, adjusted={self.adjusted}")


def _ts(s: str) -> int:
    s = s.strip()
    if s.isdigit():
        v = int(s)
        return v if v > 10**11 else v * 1000
    d = datetime.fromisoformat(s.replace("Z", "+00:00"))
    if d.tzinfo is None:
        d = d.replace(tzinfo=timezone.utc)
    return int(d.timestamp() * 1000)


def load_csv(path: str | Path, bar_ms: int = DAY_MS, columns: dict | None = None, adjusted: str = "unknown",
             max_gap_bars: float = 4.0, outlier_z: float = 8.0) -> tuple[list[Candle], ImportReport]:
    """Columns default to date,open,high,low,close,volume (case-insensitive). `date` is the bar's OPEN time."""
    cols = {"date": "date", "open": "open", "high": "high", "low": "low", "close": "close", "volume": "volume", **(columns or {})}
    rep = ImportReport(str(path), adjusted=adjusted)
    seen, bars = set(), []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        lower = {k.lower(): k for k in (reader.fieldnames or [])}
        def col(row, name):
            return row[lower[cols[name].lower()]]
        for row in reader:
            rep.rows += 1
            try:
                t = _ts(col(row, "date"))
                o, h, l, c = (float(col(row, k)) for k in ("open", "high", "low", "close"))
                v = float(col(row, "volume") or 0) if cols["volume"].lower() in lower else 0.0
            except (ValueError, KeyError):
                rep.broken += 1
                continue
            if t in seen:
                rep.duplicates += 1
                continue
            if min(o, h, l, c) <= 0 or h < l or not (l <= c <= h) or not (l <= o <= h):
                rep.broken += 1
                continue
            seen.add(t)
            bars.append(Candle(t, t + bar_ms, o, h, l, c, v))
    bars.sort(key=lambda b: b.open_ts)
    iso = lambda ms: datetime.fromtimestamp(ms / 1000, tz=timezone.utc).date().isoformat()
    for a, b in zip(bars, bars[1:]):
        if b.open_ts - a.open_ts > max_gap_bars * bar_ms:
            rep.gaps.append((iso(a.open_ts), iso(b.open_ts)))
    rets = [math.log(b.close / a.close) for a, b in zip(bars, bars[1:])]
    if len(rets) > 30:
        med = statistics.median(rets)
        mad = statistics.median(abs(r - med) for r in rets) * 1.4826 or 1e-12
        rep.outliers = [(iso(bars[i + 1].open_ts), r) for i, r in enumerate(rets) if abs(r - med) / mad > outlier_z]
    rep.kept = len(bars)
    if bars:
        rep.first, rep.last = iso(bars[0].open_ts), iso(bars[-1].open_ts)
    return bars, rep


def save_report(rep: ImportReport, path: str | Path):
    Path(path).write_text(json.dumps(asdict(rep), indent=2))
