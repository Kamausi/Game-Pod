"""News as an overlay on a strategy: the model reads the headlines, code controls the trades.

Each headline is classified once: which asset, sentiment, materiality, regulatory risk
(lexicon, Claude Haiku, or Jev). Then the overlay can do two separate things:

  bias  shift the strategy's entry confidence by +-k x recent sentiment, so good news
        makes an entry a little easier and bad news a little harder
  veto  block new LONG entries for 24h after a material, non-positive regulatory
        headline about that asset (SEC action, lawsuit, ban...)

`news_ablation` is the test that matters: the same strategy run four ways (no news,
bias only, veto only, both) at several fee levels, every run recorded in the register.
News "helps" only if a variant beats no-news at EVERY fee level. If it doesn't, the
news layer is making the bot more expensive, not better.

Causality: a headline counts from when it was SEEN. For an imported history that
only has publish times, `import_csv` adds a delay (default 5 minutes) on top.
"""
from __future__ import annotations

import csv
import json
import re
from dataclasses import replace
from datetime import datetime, timezone
from pathlib import Path

from .schema import Decision
from .sentiment import ASSET_KEYWORDS, Headline, SentimentStore, lexicon_score
from .state import Snapshot

REG_WORDS = {"sec", "cftc", "doj", "regulator", "regulators", "regulatory", "lawsuit", "lawsuits", "sue", "sues", "sued", "ban",
             "bans", "banned", "enforcement", "sanction", "sanctions", "sanctioned", "fine", "fined", "probe", "investigation",
             "subpoena", "charges", "charged", "court", "judge", "ruling", "settlement", "crackdown", "illegal", "delist",
             "delisted", "delisting", "indictment", "indicted", "cease", "desist", "seized", "seizure"}
ACTION_VERBS = {"approve", "approves", "approved", "reject", "rejects", "rejected", "launch", "launches", "launched", "hack",
                "hacked", "ban", "bans", "banned", "sue", "sues", "sued", "file", "files", "filed", "list", "lists", "listed",
                "delist", "delists", "delisted", "halt", "halts", "halted", "cut", "cuts", "raise", "raises", "hike", "hikes",
                "charge", "charges", "charged", "fine", "fines", "fined", "seize", "seizes", "seized", "acquire", "acquires",
                "acquired", "buy", "buys", "sell", "sells", "freeze", "freezes", "froze", "exploit", "exploited", "drain",
                "drained", "settle", "settles", "settled", "suspend", "suspends", "suspended", "pause", "pauses", "paused"}
OPINION = {"could", "might", "may", "why", "what", "how", "should", "opinion", "analysis", "prediction", "predicts", "forecast",
           "explained", "guide", "price", "recap", "weekly"}


def lexicon_classify(title: str, source: str = "") -> dict:
    """Deterministic fallback. Material = a named asset (or a regulator / central bank)
    within eight words of an action verb, and not an opinion or recap piece."""
    toks = re.findall(r"[a-z]+", title.lower())
    names = {w for kws in ASSET_KEYWORDS.values() for w in kws} | {"sec", "fed", "fomc", "cftc", "doj", "crypto", "bitcoin"}
    near = any(t in ACTION_VERBS and any(n in names for n in toks[max(0, i - 8):i + 9]) for i, t in enumerate(toks))
    opinion = bool(set(toks) & OPINION) or title.strip().endswith("?")
    reg_hits = sum(t in REG_WORDS for t in toks)
    return {"sentiment": lexicon_score(title), "material": 0.8 if near and not opinion else 0.3 if near else 0.1,
            "regulatory": min(1.0, 0.45 * reg_hits) if reg_hits else 0.0}


class ClaudeNewsClassifier:
    """Claude Haiku 5.5, strict JSON, batched. Requires `pip install anthropic`."""

    SCHEMA = {"type": "object", "properties": {"items": {"type": "array", "items": {"type": "object", "properties": {
        "id": {"type": "integer"}, "sentiment": {"type": "number"}, "material": {"type": "number"},
        "regulatory": {"type": "number"}}, "required": ["id", "sentiment", "material", "regulatory"], "additionalProperties": False}}},
        "required": ["items"], "additionalProperties": False}
    SYSTEM = ("You classify market news headlines. For each: sentiment -1 (clearly negative for the asset) to 1 (clearly positive), "
              "0 if neutral; material 0-1 = probability it is new, material information (not opinion, not a recap of a move "
              "that already happened, not an ad); regulatory 0-1 = probability it concerns regulatory or legal action "
              "(enforcement, lawsuits, bans, sanctions, a regulator's decision). Each line gives the source and when it was "
              "first seen. Judge only from the text.")

    def __init__(self, model: str = "claude-haiku-5-5"):
        import anthropic

        self.client = anthropic.Anthropic(timeout=30, max_retries=1)
        self.model = model

    def classify_batch(self, rows: list[tuple[str, str, int]]) -> list[dict | None]:
        listing = "\n".join(f"{i}. [{src} | seen {datetime.fromtimestamp(seen / 1000, timezone.utc):%Y-%m-%d %H:%M}Z] {t}"
                            for i, (t, src, seen) in enumerate(rows))
        resp = self.client.messages.create(
            model=self.model, max_tokens=4096,
            system=[{"type": "text", "text": self.SYSTEM, "cache_control": {"type": "ephemeral"}}],
            messages=[{"role": "user", "content": listing}],
            output_config={"effort": "low", "format": {"type": "json_schema", "schema": self.SCHEMA}},
        )
        if resp.stop_reason == "refusal":
            return [None] * len(rows)
        out: list[dict | None] = [None] * len(rows)
        for it in json.loads(next(b.text for b in resp.content if b.type == "text"))["items"]:
            if 0 <= it["id"] < len(rows):
                out[it["id"]] = {k: max(-1.0 if k == "sentiment" else 0.0, min(1.0, float(it[k]))) for k in ("sentiment", "material", "regulatory")}
        return out


def classify(store: SentimentStore, how: str = "lexicon", limit: int = 2000) -> dict:
    """Classify every unclassified headline. how: lexicon | claude | jev."""
    rows = store.unclassified(limit)
    if not rows:
        return {"classified": 0}
    if how == "lexicon":
        store.set_classes([(i, lexicon_classify(t, src)) for i, t, src, _ in rows], "lexicon")
    elif how == "claude":
        c = ClaudeNewsClassifier()
        for k in range(0, len(rows), 40):
            chunk = rows[k:k + 40]
            res = c.classify_batch([(t, src, seen) for _, t, src, seen in chunk])
            store.set_classes([(i, r or lexicon_classify(t, src)) for (i, t, src, _), r in zip(chunk, res)], "claude")
    elif how == "jev":
        from .jev import JevHeadlineScorer

        j = JevHeadlineScorer(list(ASSET_KEYWORDS))
        out = []
        for i, t, src, seen in rows:
            r = j.classify(t, src, datetime.fromtimestamp(seen / 1000, timezone.utc).isoformat())
            out.append((i, r))
        store.set_classes(out, "jev")
    else:
        raise ValueError("classifier must be lexicon, claude or jev")
    return {"classified": len(rows), "by": how}


def _ts(x: str) -> int:
    x = x.strip()
    if re.fullmatch(r"\d+(\.\d+)?", x):
        v = float(x)
        return int(v if v > 1e11 else v * 1000)
    return int(datetime.fromisoformat(x.replace("Z", "+00:00")).timestamp() * 1000)


def import_csv(store: SentimentStore, path: str | Path, seen_delay_ms: int = 300_000) -> dict:
    """CSV columns: time, title[, source]. `time` is ISO-8601 or epoch (s or ms). Each headline
    counts from time + seen_delay, because a publish time is not when a bot could have read it."""
    n = bad = 0
    items = []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            try:
                pub = _ts(row["time"])
                title = row["title"].strip()
            except (KeyError, ValueError):
                bad += 1
                continue
            if not title:
                bad += 1
                continue
            src = (row.get("source") or "import").strip()
            items.append(Headline(f"{src}:{pub}:{title[:80]}", title, "", pub, pub + seen_delay_ms, src))
            n += 1
    new = store.add(items)
    return {"rows": n, "skipped": bad, "new": len(new)}


class NewsOverlay:
    """Wraps any engine. The inner engine decides; news may only nudge entry confidence
    (bias) or block new longs (veto). It never opens a trade by itself."""

    def __init__(self, engine, store: SentimentStore, mode: str = "both", bias_k: float = 0.10,
                 veto_window_ms: int = 24 * 3_600_000):
        if mode not in ("none", "bias", "veto", "both"):
            raise ValueError("mode must be none, bias, veto or both")
        self.engine, self.store, self.mode, self.k, self.window = engine, store, mode, bias_k, veto_window_ms
        self.name = f"{engine.name}+news:{mode}"
        self.last_trace: dict | None = None
        self.vetoes = 0

    def decide(self, s: Snapshot) -> Decision:
        d = self.engine.decide(s)
        info = {"mode": self.mode}
        if self.mode in ("bias", "both") and d.direction != "neutral":
            sent, n = self.store.signal(s.instrument, s.as_of)
            if n:
                sgn = 1 if d.direction == "long" else -1
                new_conf = max(0.0, min(1.0, d.direction_confidence + self.k * sgn * sent))
                info.update(sentiment=sent, headlines=n, conf_before=d.direction_confidence, conf_after=new_conf)
                d = replace(d, direction_confidence=new_conf)
        if self.mode in ("veto", "both") and d.direction == "long":
            hits = self.store.regulatory_hits(s.instrument, s.as_of, self.window)
            if hits:
                self.vetoes += 1
                info["veto"] = hits[:3]
                d = replace(d, direction="neutral", rationale=f"regulatory veto: {hits[0][:120]}")
        self.last_trace = {"inner": getattr(self.engine, "last_trace", None), "news": info}
        return d


def news_ablation(reg, base_spec, candles, snap, store_path: Path, work: Path, fee_mults=(1.0, 2.0, 3.0),
                  modes=("none", "bias", "veto", "both")) -> dict:
    """Same strategy, same data, four news variants x several fee levels. Every run recorded."""
    from .validation import run_spec, summarize

    store = SentimentStore(store_path)
    sha = store.sha256()[:16]
    table = {}
    for m in fee_mults:
        for mode in modes:
            opts = {**base_spec.engine_opts, "news": mode, "news_db": str(store_path), "news_sha": sha, "fee_mult": m}
            spec = replace(base_spec, engine_opts=opts)
            try:
                r = summarize(run_spec(spec, candles, work / spec.id))
                reg.record_run("explore", spec, snap, snap.slice_note(0, len(candles)), r)
                table[(m, mode)] = r
            except Exception as e:
                reg.record_run("explore", spec, snap, snap.slice_note(0, len(candles)), None, f"{type(e).__name__}: {e}")
                table[(m, mode)] = None
    verdict = {}
    for mode in modes[1:]:
        diffs = [table[(m, mode)]["total_return"] - table[(m, "none")]["total_return"]
                 for m in fee_mults if table.get((m, mode)) and table.get((m, "none"))]
        verdict[mode] = {"improvement_by_fee": diffs, "helps_at_every_fee": bool(diffs) and all(x > 0 for x in diffs)}
    return {"table": {f"fees x{m} / {mode}": v for (m, mode), v in table.items()}, "verdict": verdict, "news_sha": sha,
            "headlines_used": store.db.execute("SELECT COUNT(*) FROM headlines WHERE regulatory IS NOT NULL").fetchone()[0]}
