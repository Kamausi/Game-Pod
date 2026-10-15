"""Auditing a trading claim before trusting it: the parts of the "reverse engineer any system"
roadmap that are about evidence, done in code we own.

  headline_check  a return series judged from EVERY start date, by calendar year, by worst
                  fall, and after your own costs and tax. One big number hides all of these.
  finding         DATA VALIDATED / TRADE-OFF / NO EDGE against a benchmark, split by TIME
                  (never shuffled): validated only if it beats the benchmark on both growth
                  (CAGR) and risk-adjusted return (Sharpe) in the training window AND the
                  held-out window.
  plateau         does the chosen setting sit on a plateau of good neighbours, or a lone peak?
  trend200        the most boring rival: in the market when the close is 1% above its 200-day
                  average, out when 3% below. If a fancy system can't beat it, that's the answer.
  red flags       one-regime curves, martingale sizing, hard-coded secrets: checks a script can run.
  spec            the claim written as rules, every undisclosed field UNKNOWN.
"""
from __future__ import annotations

import csv
import math
import re
from datetime import date
from pathlib import Path

SPEC_FIELDS = ["universe", "signal", "entry", "exit", "position sizing", "rebalance timing", "costs assumed",
               "performance claim (number, period, benchmark)"]


def spec(known: dict) -> dict:
    return {f: known.get(f) or "UNKNOWN" for f in SPEC_FIELDS}


def read_series(path: str | Path) -> list[tuple[str, float]]:
    """CSV with date and either 'return' (per period, decimal) or 'value' (equity / price)."""
    rows = list(csv.DictReader(Path(path).open()))
    if not rows:
        return []
    k = {c.lower(): c for c in rows[0]}
    dk = k.get("date") or list(rows[0])[0]
    if "return" in k:
        return [(r[dk][:10], float(r[k["return"]])) for r in rows]
    vk = k.get("value") or k.get("close") or k.get("equity")
    vals = [(r[dk][:10], float(r[vk])) for r in rows]
    return [(d, v / p - 1) for (d, v), (_, p) in zip(vals[1:], vals)]


def _ppy(dates: list[str]) -> float:
    if len(dates) < 2:
        return 12.0
    days = (date.fromisoformat(dates[-1]) - date.fromisoformat(dates[0])).days / (len(dates) - 1)
    return 252.0 if days < 4 else 52.0 if days < 10 else 12.0 if days < 45 else 4.0 if days < 120 else 1.0


def cagr(rets: list[float], ppy: float) -> float:
    g = 1.0
    for r in rets:
        g *= 1 + r
    yrs = len(rets) / ppy
    return g ** (1 / yrs) - 1 if yrs > 0 and g > 0 else -1.0


def sharpe(rets: list[float], ppy: float, rf: float = 0.0) -> float:
    if len(rets) < 2:
        return 0.0
    ex = [r - rf / ppy for r in rets]
    m = sum(ex) / len(ex)
    sd = math.sqrt(sum((x - m) ** 2 for x in ex) / (len(ex) - 1))
    return m / sd * math.sqrt(ppy) if sd > 0 else 0.0


def max_drawdown(rets: list[float]) -> float:
    eq = peak = 1.0
    mdd = 0.0
    for r in rets:
        eq *= 1 + r
        peak = max(peak, eq)
        mdd = max(mdd, 1 - eq / peak)
    return mdd


def headline_check(series: list[tuple[str, float]], annual_cost_pct: float = 0.0, tax_pct: float = 0.0, slippage_pct_per_period: float = 0.0) -> dict:
    dates, rets = [d for d, _ in series], [r for _, r in series]
    ppy = _ppy(dates)
    net = [r - slippage_pct_per_period / 100 - annual_cost_pct / 100 / ppy for r in rets]
    years: dict[str, float] = {}
    for d, r in zip(dates, net):
        years[d[:4]] = (1 + years.get(d[:4], 0.0)) * (1 + r) - 1
    after_tax = {y: (v * (1 - tax_pct / 100) if v > 0 else v) for y, v in years.items()}
    starts = [cagr(net[i:], ppy) for i in range(0, max(0, len(net) - int(ppy)))]  # at least a year left
    s_sorted = sorted(starts)
    best = max(years.values()) if years else 0.0
    total = 1.0
    for v in years.values():
        total *= 1 + v
    return {
        "periods": len(rets), "periods_per_year": ppy, "first": dates[0] if dates else None, "last": dates[-1] if dates else None,
        "cagr_gross": cagr(rets, ppy), "cagr_after_costs": cagr(net, ppy), "max_drawdown": max_drawdown(net),
        "sharpe_after_costs": sharpe(net, ppy),
        "every_start_date": {"starts": len(starts), "worst_cagr": s_sorted[0] if starts else None,
                             "median_cagr": s_sorted[len(s_sorted) // 2] if starts else None, "best_cagr": s_sorted[-1] if starts else None,
                             "share_of_starts_losing": sum(1 for x in starts if x < 0) / len(starts) if starts else None},
        "calendar_years": years, "calendar_years_after_tax": after_tax,
        "losing_years": sorted(y for y, v in years.items() if v < 0),
        # one regime: the single best year delivers more than half of all the growth (in log terms)
        "one_regime": bool(years) and total > 1 and math.log(1 + best) > 0.5 * math.log(total),
    }


def finding(strat: list[tuple[str, float]], bench: list[tuple[str, float]], split: str = "2015-01-01") -> dict:
    b = dict(bench)
    pairs = [(d, r, b[d]) for d, r in strat if d in b]
    ppy = _ppy([d for d, *_ in pairs])
    out = {}
    for name, rows in (("train", [p for p in pairs if p[0] < split]), ("test", [p for p in pairs if p[0] >= split])):
        s, m = [p[1] for p in rows], [p[2] for p in rows]
        out[name] = {"periods": len(rows), "cagr": cagr(s, ppy) if s else None, "bench_cagr": cagr(m, ppy) if m else None,
                     "sharpe": sharpe(s, ppy) if s else None, "bench_sharpe": sharpe(m, ppy) if m else None}
    beats = lambda w: (out[w]["periods"] > 0 and out[w]["cagr"] > out[w]["bench_cagr"], out[w]["periods"] > 0 and out[w]["sharpe"] > out[w]["bench_sharpe"])
    tr, te = beats("train"), beats("test")
    if all(tr) and all(te):
        status = "DATA VALIDATED"
    elif not any(te):
        status = "NO EDGE"
    else:
        status = "TRADE-OFF"
    out.update(status=status, split=split, rule="validated = beats the benchmark on CAGR and Sharpe in both windows; the split is by time")
    return out


def plateau(grid: dict[tuple, float], chosen: tuple, keep: float = 0.5) -> dict:
    """Neighbours differ from the chosen setting by one grid step in one parameter. A plateau
    keeps most of the chosen score next door; a lone peak collapses one step away."""
    axes = [sorted({k[i] for k in grid}) for i in range(len(chosen))]
    nbrs = []
    for i, ax in enumerate(axes):
        j = ax.index(chosen[i])
        for jj in (j - 1, j + 1):
            if 0 <= jj < len(ax):
                k = tuple(ax[jj] if n == i else v for n, v in enumerate(chosen))
                if k in grid:
                    nbrs.append((k, grid[k]))
    c = grid[chosen]
    good = [k for k, v in nbrs if c > 0 and v >= keep * c]
    kind = "no neighbours" if not nbrs else "plateau" if c > 0 and len(good) >= len(nbrs) / 2 else "lone peak"
    return {"chosen": chosen, "score": c, "neighbours": nbrs, "kept_by": len(good), "verdict": kind}


def trend200(closes: list[tuple[str, float]], up: float = 0.01, down: float = 0.03, cash_rate: float = 0.0) -> list[tuple[str, float]]:
    """Daily returns of the 200-day rule. Decided at the close, earned from the next day."""
    out, inside = [], False
    for i in range(1, len(closes)):
        d, c = closes[i]
        r = c / closes[i - 1][1] - 1
        out.append((d, r if inside else cash_rate / 252))
        if i >= 200:
            sma = sum(x for _, x in closes[i - 199:i + 1]) / 200
            if c >= sma * (1 + up):
                inside = True
            elif c <= sma * (1 - down):
                inside = False
    return out


def martingale(sizes_and_pnls: list[tuple[float, float]]) -> dict:
    """Size growing after losses is the martingale tell: fraction of losses followed by a bigger bet."""
    after_loss = [(a, b) for (a, pa), (b, _) in zip(sizes_and_pnls, sizes_and_pnls[1:]) if pa < 0]
    up = sum(1 for a, b in after_loss if b > a * 1.2)
    return {"losses": len(after_loss), "size_up_after_loss": up, "flag": bool(after_loss) and up / len(after_loss) > 0.5}


SECRET_PATTERNS = [
    ("AWS key", r"AKIA[0-9A-Z]{16}"), ("private key", r"-----BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY-----"),
    ("Alpaca key id", r"\b(PK|AK)[A-Z0-9]{18}\b"), ("generic secret", r"(?i)(api[_-]?secret|secret[_-]?key|private[_-]?key)\s*[:=]\s*['\"][A-Za-z0-9/+=_\-]{16,}['\"]"),
    ("Solana/hex key material", r"\b[1-9A-HJ-NP-Za-km-z]{87,88}\b"),
]


def scan_secrets(root: str | Path, exts=(".py", ".js", ".ts", ".json", ".env", ".yml", ".yaml", ".toml", ".cfg", ".ini", ".html")) -> list[dict]:
    """Hard-coded keys in someone's bot: never use, never publish. Reports file and line only, redacted."""
    hits = []
    for p in Path(root).rglob("*"):
        if not p.is_file() or (p.suffix not in exts and p.name != ".env") or any(x in p.parts for x in (".git", "node_modules", ".venv")):
            continue
        try:
            text = p.read_text(errors="ignore")
        except OSError:
            continue
        for n, line in enumerate(text.splitlines(), 1):
            for name, pat in SECRET_PATTERNS:
                if re.search(pat, line):
                    hits.append({"file": str(p), "line": n, "kind": name, "redacted": True})
    return hits
