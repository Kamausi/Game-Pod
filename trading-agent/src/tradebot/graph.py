"""The desk as a function: external + internal context in; what, how much and when out.

INGEST   filings, prices, SEC fundamentals (insider.py, desk.scout / desk.analyst)
REMEMBER positions, limits, and every past thesis with how it ended (memory below)
MAP      the decision broken into nodes, each with inputs, outputs and one route
ROUTE    code for explicit rules, ML for statistical relationships, LLM for hard text,
         a person for judgment. The route is written down, so it can be argued with
EVALUATE each node has a measurable target. Without one, swapping a model or a prompt is
         just turning knobs. evaluate() scores every node from the journal
DECIDE   selection (skeptic), size (risk), timing (PM approval + day-5 exit), each
         traceable to the files the earlier nodes wrote

Memory answers "do we remember why yesterday's thesis failed?": every name the desk sees is
stored with its thesis (role, size, valuation gap, the verdict and the reason) and, once the
journal knows it, the outcome in R. The analyst reads similar past theses before writing a
card; the skeptic kills a pattern that has lost money at least 3 times.
"""
from __future__ import annotations

import csv
import json
from dataclasses import asdict, dataclass
from pathlib import Path


@dataclass(frozen=True)
class Node:
    name: str
    route: str  # code | ml | llm | human
    inputs: tuple[str, ...]
    outputs: tuple[str, ...]
    metric: str
    target: float
    better: str  # "higher" or "lower"
    what: str


GRAPH = (
    Node("ingest", "code", ("EDGAR Form 4 feed", "daily bars", "SEC XBRL EPS"), ("filings", "prices", "fundamentals"),
         "source_error_rate", 0.05, "lower", "share of filings the scout could not load"),
    Node("scout", "code", ("filings", "prices"), ("names",), "avg_r_of_names", 0.0, "higher",
         "average R of every name scouted, taken or not (hypothetical for kills): does the 1% filter find anything?"),
    Node("analyst", "code", ("names", "fundamentals", "memory"), ("fair value cards",), "range_minus_expensive_r", 0.0, "higher",
         "average R of names priced RANGE minus names priced EXPENSIVE: does valuation separate winners?"),
    Node("skeptic", "code", ("names", "cards", "memory"), ("PASS / KILL",), "pass_minus_kill_r", 0.0, "higher",
         "average R of passed names minus killed names: do the kills save money?"),
    Node("risk", "code", ("PASS names", "account", "limits.json"), ("tickets",), "worst_loss_r", -1.5, "higher",
         "worst realized loss in R: a stop that holds keeps this near -1"),
    Node("pm", "human", ("tickets", "brief"), ("approved tickets",), "approved_minus_rejected_r", 0.0, "higher",
         "average R of tickets you approved minus those you rejected: does your judgment add anything?"),
    Node("execution", "code", ("approved tickets",), ("fills",), "fill_rate", 0.6, "higher",
         "share of sent tickets that filled (a limit 0.5% above the close)"),
    Node("journal", "code", ("fills", "kills", "backtest"), ("lessons", "decay flag"), "live_over_backtest_r", 0.5, "higher",
         "live average R divided by the backtest's: below 0.5 after 20 trades = decaying"),
)


def _rows(desk) -> list[dict]:
    p = desk.path("journal", "trades.csv")
    return list(csv.DictReader(p.open())) if p.exists() else []


def _r(row) -> float | None:
    v = row.get("pnl_r")
    return float(v) if v not in (None, "", "None") else None


def _mean(xs):
    xs = [x for x in xs if x is not None]
    return (sum(xs) / len(xs), len(xs)) if xs else (None, 0)


def evaluate(desk, min_n: int = 10) -> list[dict]:
    rows = _rows(desk)
    cards = {}
    for p in sorted(desk.path("inbox").glob("*-analyst.json")) if desk.path("inbox").exists() else []:
        d = p.name[:10]
        for c in json.loads(p.read_text()).get("cards", []):
            cards[(d, c["symbol"])] = c["verdict"]
    scouts, errs = 0, 0
    for p in sorted(desk.path("inbox").glob("*-scout.json")) if desk.path("inbox").exists() else []:
        s = json.loads(p.read_text())
        scouts += s.get("scanned_filings", 0) + len(s.get("errors", []))
        errs += len(s.get("errors", []))
    by = lambda pred: [_r(r) for r in rows if pred(r)]
    val = {}
    val["source_error_rate"] = (errs / scouts if scouts else None, scouts)
    val["avg_r_of_names"] = _mean(by(lambda r: True))
    rng, n1 = _mean(by(lambda r: cards.get((r["date"], r["ticker"])) == "RANGE"))
    exp, n2 = _mean(by(lambda r: cards.get((r["date"], r["ticker"])) == "EXPENSIVE"))
    val["range_minus_expensive_r"] = (rng - exp if rng is not None and exp is not None else None, min(n1, n2))
    ps, n3 = _mean(by(lambda r: r["outcome"] in ("FILLED", "REJECTED BY ME", "FAILED RISK CHECK")))
    ks, n4 = _mean(by(lambda r: r["outcome"] == "KILLED BY SKEPTIC"))
    val["pass_minus_kill_r"] = (ps - ks if ps is not None and ks is not None else None, min(n3, n4))
    filled = [x for x in by(lambda r: r["outcome"] == "FILLED") if x is not None]
    val["worst_loss_r"] = (min(filled) if filled else None, len(filled))
    ap, n5 = _mean(by(lambda r: r["outcome"] == "FILLED"))
    rj, n6 = _mean(by(lambda r: r["outcome"] == "REJECTED BY ME"))
    val["approved_minus_rejected_r"] = (ap - rj if ap is not None and rj is not None else None, min(n5, n6))
    sent = [r for r in rows if r["outcome"] in ("FILLED", "NOT FILLED")]
    val["fill_rate"] = (sum(r["outcome"] == "FILLED" for r in sent) / len(sent) if sent else None, len(sent))
    lp = desk.path("quant", "latest.json")
    bt = json.loads(lp.read_text()).get("build", {}).get("avg_r") if lp.exists() else None
    val["live_over_backtest_r"] = (ap / bt if ap is not None and bt else None, n5)
    out = []
    for nd in GRAPH:
        v, n = val[nd.metric]
        if v is None or n < min_n:
            status = f"NOT ENOUGH DATA ({n} of {min_n})"
        else:
            ok = v >= nd.target if nd.better == "higher" else v <= nd.target
            status = "MEETS TARGET" if ok else "MISSES TARGET"
        out.append({**asdict(nd), "value": v, "n": n, "status": status})
    return out


# ------------------------------------------------------------------ memory


def gap_bucket(gap: float | None) -> str:
    if gap is None:
        return "no range"
    return "deep value (>30% below base)" if gap > 0.3 else "below base" if gap > 0 else "at or above base"


def remember(desk, d: str) -> int:
    """Store today's theses (called after the skeptic). Idempotent per date and symbol."""
    sc = {n["symbol"]: n for n in (desk.read(d, "scout") or {"names": []})["names"]}
    an = {c["symbol"]: c for c in (desk.read(d, "analyst") or {"cards": []})["cards"]}
    sk = {v["symbol"]: v for v in (desk.read(d, "skeptic") or {"verdicts": []})["verdicts"]}
    p = desk.path("memory", "theses.jsonl")
    p.parent.mkdir(parents=True, exist_ok=True)
    have = {(t["date"], t["symbol"]) for t in recall_all(desk)}
    n = 0
    with p.open("a") as fh:
        for sym, s in sc.items():
            if (d, sym) in have:
                continue
            c, v = an.get(sym, {}), sk.get(sym, {})
            fh.write(json.dumps({"date": d, "symbol": sym, "role": s["role"], "usd": s["usd"], "plan": s.get("plan_10b5_1"), "batch": s.get("batch"),
                                 "valuation": c.get("verdict"), "gap": c.get("gap_to_base"), "pattern": f"{s['role']} buy, {gap_bucket(c.get('gap_to_base'))}",
                                 "decision": v.get("verdict"), "why": v.get("why"), "outcome_r": None}) + "\n")
            n += 1
    return n


def recall_all(desk) -> list[dict]:
    p = desk.path("memory", "theses.jsonl")
    return [json.loads(x) for x in p.read_text().splitlines() if x.strip()] if p.exists() else []


def settle_outcomes(desk) -> int:
    """Copy journal outcomes (actual or hypothetical R) into the theses they belong to."""
    rows = {(r["date"], r["ticker"]): _r(r) for r in _rows(desk)}
    ts, n = recall_all(desk), 0
    for t in ts:
        r = rows.get((t["date"], t["symbol"]))
        if r is not None and t.get("outcome_r") != r:
            t["outcome_r"], n = r, n + 1
    if n:
        desk.path("memory", "theses.jsonl").write_text("".join(json.dumps(t) + "\n" for t in ts))
    return n


def similar(desk, symbol: str, pattern: str, before: str) -> dict:
    past = [t for t in recall_all(desk) if t["date"] < before and t.get("outcome_r") is not None]
    same_sym = [t for t in past if t["symbol"] == symbol]
    same_pat = [t for t in past if t["pattern"] == pattern]
    m = lambda xs: sum(x["outcome_r"] for x in xs) / len(xs) if xs else None
    return {"pattern": pattern, "same_pattern_n": len(same_pat), "same_pattern_avg_r": m(same_pat),
            "same_symbol": [{"date": t["date"], "outcome_r": t["outcome_r"], "decision": t["decision"], "why": t["why"]} for t in same_sym[-3:]]}
