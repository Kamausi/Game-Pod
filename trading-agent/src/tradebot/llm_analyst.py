"""A Claude seat beside the code analyst: bull, bear, then a judge (the TradingAgents debate),
allowed to use only the numbers the desk hands it.

Why it is built this way:
  - The model never fetches anything. It gets a numbered fact table (SEC EPS by quarter, the last
    close, the own-history P/E range the code computed, the memory of similar theses) and every
    point it makes must cite fact ids from that table.
  - verify() checks the output in code: every point cites at least one real fact id, and every
    number written in a point matches a cited fact (or the fact's label, for quarter names).
    A number that comes from nowhere fails the whole opinion.
  - The judge says whether it agrees with the code's verdict. The skeptic kills the name if the
    judge disagrees, if verification fails, or if the call errors: unsure = KILL. The seat can
    only remove trades, never add one.

Request shape (claude-api skill, Python SDK 1.x): model claude-opus-5-5, adaptive thinking (on
by default on this model, so it is omitted), explicit effort, JSON output via
output_config.format, server-side refusal fallback via beta `server-side-fallback-2026-07-01`
with fallbacks="default", stop_reason "refusal" handled before reading content.
"""
from __future__ import annotations

import json
import re
import time
from dataclasses import dataclass, field

MODEL = "claude-opus-5-5"

_POINTS = {"type": "array", "items": {"type": "object", "additionalProperties": False, "required": ["point", "fact_ids"],
                                      "properties": {"point": {"type": "string"}, "fact_ids": {"type": "array", "items": {"type": "string"}}}}}
CASE_SCHEMA = {"type": "object", "additionalProperties": False, "required": ["points"], "properties": {"points": _POINTS}}
JUDGE_SCHEMA = {"type": "object", "additionalProperties": False,
                "required": ["verdict", "agrees_with_code", "deciding_points", "summary"],
                "properties": {"verdict": {"type": "string", "enum": ["RANGE", "EXPENSIVE", "NO VIEW"]},
                               "agrees_with_code": {"type": "boolean"},
                               "deciding_points": _POINTS,
                               "summary": {"type": "string"}}}

SYSTEM = """You sit on a paper-trading desk that buys after C-suite insiders buy their own stock.
You are given a FACT TABLE. It is the only source of numbers you may use.

Rules:
- Every point cites the fact ids it rests on, in fact_ids. A point with no fact id is not allowed.
- Do not write any number that is not in a cited fact. Do not compute new numbers (no new ratios,
  growth rates, or targets); describe direction in words instead ("EPS fell three quarters in a row").
- If the facts are not enough to judge, say so. "NO VIEW" is an acceptable verdict.
- Write plainly. One idea per point. At most 5 points."""

ROLE = {
    "bull": "Make the strongest honest case FOR buying this stock today, using only the fact table.",
    "bear": "Make the strongest honest case AGAINST buying this stock today, using only the fact table. Answer the bull's points where the facts allow.",
    "judge": ("You are the judge. Read the facts, the bull case and the bear case. Give your own verdict: RANGE (price at or below the "
              "fair value base and the case holds), EXPENSIVE, or NO VIEW. Say whether you agree with the code's verdict. "
              "deciding_points are the points that decided it, each with fact ids."),
}


def fact_table(name: dict, card: dict) -> dict[str, dict]:
    """Numbered facts from the scout's name and the analyst's card. Only these may be cited."""
    facts: dict[str, dict] = {}

    def add(claim, value, source):
        facts[f"F{len(facts) + 1}"] = {"claim": claim, "value": value, "source": source}
    add("insider role", name.get("role"), "SEC Form 4 " + str(name.get("accession") or name.get("url") or ""))
    add("insider purchase, US dollars", name.get("usd"), "SEC Form 4")
    add("filing date", name.get("filed"), "SEC Form 4")
    for f in card.get("facts", []):
        add(f["claim"], f["value"], f["source"])
    for k, label in (("low", "fair value low (25th pct own-history P/E x TTM EPS)"), ("base", "fair value base (median own-history P/E x TTM EPS)"),
                     ("high", "fair value high (75th pct own-history P/E x TTM EPS)"), ("ttm_eps", "trailing 12-month diluted EPS"),
                     ("gap_to_base", "gap from price to fair value base, as a fraction")):
        if card.get(k) is not None:
            add(label, round(card[k], 4), "computed by the desk's code from the facts above")
    add("code verdict", card.get("verdict"), "desk analyst (code)")
    mem = card.get("memory") or {}
    if mem.get("same_pattern_n"):
        add(f"past theses with the pattern '{mem['pattern']}' that have an outcome", mem["same_pattern_n"], "desk memory")
        add("their average outcome in R", round(mem["same_pattern_avg_r"], 3), "desk memory")
    for t in mem.get("same_symbol", []):
        add(f"past thesis on this symbol {t['date']}: {t['decision']} ({t['why']}), outcome in R", t["outcome_r"], "desk memory")
    if card.get("next_report_estimate"):
        add("next report date, ESTIMATED from filing cadence", card["next_report_estimate"], "SEC filing dates")
    return facts


_NUM = re.compile(r"(?<![A-Za-z])[-+]?\$?\d[\d,]*(?:\.\d+)?%?")


def _numbers(text: str) -> list[tuple[str, float, bool]]:
    out = []
    for m in _NUM.finditer(text):
        s = m.group(0)
        try:
            out.append((s, float(s.replace("$", "").replace(",", "").rstrip("%")), s.endswith("%")))
        except ValueError:
            continue
    return out


def _matches(x: float, pct: bool, fact: dict) -> bool:
    v = fact["value"]
    cands = []
    if isinstance(v, (int, float)) and not isinstance(v, bool):
        cands += [float(v), abs(float(v))]
        if pct or abs(v) < 2:
            cands += [float(v) * 100, abs(float(v)) * 100]
        if abs(v) >= 1000:  # "$1.2M", "$250K" style rounding
            cands += [float(v) / 1e3, float(v) / 1e6]
    for c in cands:
        tol = max(abs(c) * 0.01, 0.006 if abs(c) < 10 else 0.5)
        if abs(abs(x) - abs(c)) <= tol:
            return True
    # numbers that are part of the fact's own words or value (quarter names, dates, "12 quarters")
    words = f"{fact['claim']} {v}"
    return any(abs(abs(x) - n) < 1e-9 for _, n, _ in _numbers(words))


def verify(points: list[dict], facts: dict[str, dict]) -> list[str]:
    """Problems with an opinion; empty means every point is grounded in the fact table."""
    bad = []
    for i, p in enumerate(points, 1):
        ids = [f for f in p.get("fact_ids", []) if f in facts]
        unknown = [f for f in p.get("fact_ids", []) if f not in facts]
        if unknown:
            bad.append(f"point {i} cites unknown fact ids {unknown}")
        if not ids:
            bad.append(f"point {i} cites no fact: {p.get('point', '')[:80]}")
            continue
        for s, x, pct in _numbers(p.get("point", "")):
            if not any(_matches(x, pct, facts[f]) for f in ids):
                bad.append(f"point {i}: the number {s} is not in facts {ids}")
    return bad


@dataclass
class ClaudeSeat:
    """One structured call per role. `create` is the SDK method (injectable for tests)."""
    effort: str = "high"
    model: str = MODEL
    timeout_s: float = 120
    create: object = None
    traces: list = field(default_factory=list)

    def __post_init__(self):
        if self.create is None:
            from .platforms import _optional
            anthropic = _optional("anthropic", "claude")
            client = anthropic.Anthropic(timeout=self.timeout_s, max_retries=2)

            def create(**kw):
                return client.beta.messages.create(betas=["server-side-fallback-2026-07-01"], fallbacks="default", **kw)
            self.create = create

    def ask(self, role: str, user: str, schema: dict) -> dict:
        t0 = time.monotonic()
        resp = self.create(
            model=self.model, max_tokens=16000,
            system=[{"type": "text", "text": SYSTEM, "cache_control": {"type": "ephemeral"}}],
            messages=[{"role": "user", "content": f"{ROLE[role]}\n\n{user}"}],
            output_config={"effort": self.effort, "format": {"type": "json_schema", "schema": schema}},
        )
        self.traces.append({"role": role, "model_served": getattr(resp, "model", None), "stop_reason": resp.stop_reason,
                            "latency_ms": round((time.monotonic() - t0) * 1000)})
        if resp.stop_reason == "refusal":
            raise RuntimeError(f"{role}: model refused")
        if resp.stop_reason == "max_tokens":
            raise RuntimeError(f"{role}: output cut off at max_tokens")
        return json.loads(next(b.text for b in resp.content if b.type == "text"))


def debate(name: dict, card: dict, seat: ClaudeSeat) -> dict:
    """Bull, bear (sees the bull), judge (sees both). Returns the opinion with its check result.
    status: AGREES / DISAGREES / UNVERIFIED (a citation or number failed) / ERROR."""
    facts = fact_table(name, card)
    table = "FACT TABLE (cite by id):\n" + "\n".join(f"{k}: {f['claim']} = {f['value']}  [{f['source']}]" for k, f in facts.items())
    head = f"Symbol {name['symbol']}. The code's verdict is {card.get('verdict')}.\n\n{table}"
    out, t0 = {"model": seat.model, "facts": facts}, len(seat.traces)
    try:
        bull = seat.ask("bull", head, CASE_SCHEMA)["points"]
        bear = seat.ask("bear", f"{head}\n\nBULL CASE:\n{json.dumps(bull)}", CASE_SCHEMA)["points"]
        judge = seat.ask("judge", f"{head}\n\nBULL CASE:\n{json.dumps(bull)}\n\nBEAR CASE:\n{json.dumps(bear)}", JUDGE_SCHEMA)
    except Exception as e:
        out.update(status="ERROR", problems=[f"{type(e).__name__}: {e}"[:200]], traces=seat.traces[t0:])
        return out
    problems = [f"bull {p}" for p in verify(bull, facts)] + [f"bear {p}" for p in verify(bear, facts)] \
        + [f"judge {p}" for p in verify(judge["deciding_points"], facts)]
    agrees = judge["verdict"] == card.get("verdict") and judge["agrees_with_code"]
    out.update(bull=bull, bear=bear, judge=judge, problems=problems, traces=seat.traces[t0:],
               status="UNVERIFIED" if problems else "AGREES" if agrees else "DISAGREES")
    return out


def skeptic_reason(card: dict) -> str | None:
    """Why the skeptic should kill on the LLM seat's account (None: no objection or seat not run)."""
    op = card.get("llm")
    if not op:
        return None
    if op["status"] == "AGREES":
        return None
    if op["status"] == "DISAGREES":
        return f"Claude judge disagrees ({op['judge']['verdict']}): {op['judge']['summary'][:140]}"
    return f"Claude seat {op['status']}: {'; '.join(op.get('problems', []))[:160]}"
