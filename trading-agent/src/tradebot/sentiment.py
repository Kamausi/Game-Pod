"""News sentiment as one more scored signal.

Pipeline: headline sources -> store (with the time WE first saw each headline)
-> scorer (Claude, or a deterministic lexicon fallback) -> per-asset signal.

Causality: a headline counts only from the moment it was first seen, never
from its claimed publish time. RSS feeds back-date and edit items; using the
publish time in a backtest would leak the future.

The signal is not trusted on arrival. It goes into the ensemble like every other
signal and earns weight only if its calls beat costs in each regime.
"""
from __future__ import annotations

import email.utils
import json
import math
import re
import sqlite3
import time
import urllib.request
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable

DEFAULT_FEEDS = (
    "https://www.coindesk.com/arc/outboundfeeds/rss/",
    "https://cointelegraph.com/rss",
    "https://decrypt.co/feed",
)
ASSET_KEYWORDS = {
    "BTC_USD": ("bitcoin", "btc"),
    "ETH_USD": ("ethereum", "ether", "eth"),
    "SOL_USD": ("solana", "sol"),
    "DOGE_USD": ("dogecoin", "doge"),
    "PEPE_USD": ("pepe",),
}


@dataclass(frozen=True)
class Headline:
    id: str
    title: str
    url: str
    published_ms: int | None
    seen_ms: int
    source: str


def parse_rss(xml_text: str, source: str, seen_ms: int) -> list[Headline]:
    # Feeds are untrusted. RSS never needs a DTD, so refuse any document that
    # declares one: that blocks external-entity (XXE) and entity-expansion attacks.
    head = xml_text[:4096].upper()
    if "<!DOCTYPE" in head or "<!ENTITY" in xml_text.upper():
        raise ValueError(f"refusing feed with a DTD/entity declaration: {source}")
    out = []
    root = ET.fromstring(xml_text)
    for item in root.iter("item"):
        title = (item.findtext("title") or "").strip()
        link = (item.findtext("link") or "").strip()
        pub = item.findtext("pubDate")
        pub_ms = None
        if pub:
            try:
                pub_ms = int(email.utils.parsedate_to_datetime(pub).timestamp() * 1000)
            except (TypeError, ValueError):
                pub_ms = None
        if title:
            out.append(Headline(link or title, title, link, pub_ms, seen_ms, source))
    return out


def fetch_rss(url: str, timeout: float = 10) -> str:
    req = urllib.request.Request(url, headers={"User-Agent": "tradebot-research/0.1"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read().decode("utf-8", errors="replace")


def assets_mentioned(title: str) -> list[str]:
    words = set(re.findall(r"[a-z]+", title.lower()))
    return [a for a, kws in ASSET_KEYWORDS.items() if words & set(kws)]


# ---------- scorers ----------
POS = {"surge", "surges", "rally", "rallies", "soar", "soars", "gain", "gains", "jump", "jumps", "record", "high", "approve",
       "approved", "approval", "inflow", "inflows", "bullish", "upgrade", "adopt", "adoption", "partnership", "launch", "beat", "rebound"}
NEG = {"crash", "crashes", "plunge", "plunges", "drop", "drops", "fall", "falls", "hack", "hacked", "exploit", "lawsuit", "sue",
       "sued", "ban", "bans", "outflow", "outflows", "bearish", "liquidation", "liquidations", "fraud", "selloff", "dump", "reject",
       "rejected", "delay", "delays", "probe", "fine", "fined", "low", "slump"}
NEGATORS = {"not", "no", "never", "denies", "deny", "without"}


def lexicon_score(title: str) -> float:
    toks = re.findall(r"[a-z]+", title.lower())
    s = 0.0
    for i, t in enumerate(toks):
        v = 1.0 if t in POS else -1.0 if t in NEG else 0.0
        if v and any(w in NEGATORS for w in toks[max(0, i - 3):i]):
            v = -v
        s += v
    return math.tanh(s / 2)


class ClaudeScorer:
    """Batch-scores headlines with a strict JSON schema. Requires `pip install anthropic`."""

    SCHEMA = {
        "type": "object",
        "properties": {"scores": {"type": "array", "items": {"type": "object", "properties": {
            "id": {"type": "integer"}, "score": {"type": "number"}, "relevant": {"type": "boolean"}},
            "required": ["id", "score", "relevant"], "additionalProperties": False}}},
        "required": ["scores"], "additionalProperties": False,
    }

    def __init__(self, model: str = "claude-haiku-5-5"):
        import anthropic

        self.client = anthropic.Anthropic(timeout=30, max_retries=1)
        self.model = model

    def score(self, titles: list[str]) -> list[float | None]:
        listing = "\n".join(f"{i}. {t}" for i, t in enumerate(titles))
        resp = self.client.messages.create(
            model=self.model, max_tokens=2048,
            system="You rate crypto news headlines for likely short-term price impact on the assets they mention. "
                   "score is -1 (clearly negative) to 1 (clearly positive); 0 when neutral or unclear. "
                   "relevant is false for opinion pieces, price recaps of moves that already happened, and ads. "
                   "Judge only from the headline text.",
            messages=[{"role": "user", "content": listing}],
            output_config={"effort": "low", "format": {"type": "json_schema", "schema": self.SCHEMA}},
        )
        if resp.stop_reason == "refusal":
            return [None] * len(titles)
        data = json.loads(next(b.text for b in resp.content if b.type == "text"))
        out: list[float | None] = [None] * len(titles)
        for s in data["scores"]:
            if 0 <= s["id"] < len(titles) and s["relevant"]:
                out[s["id"]] = max(-1.0, min(1.0, float(s["score"])))
        return out


# ---------- store + signal ----------
class SentimentStore:
    def __init__(self, path: str | Path = "runs/sentiment.sqlite"):
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(str(path))
        self.db.execute("""CREATE TABLE IF NOT EXISTS headlines (
            id TEXT PRIMARY KEY, title TEXT, url TEXT, source TEXT, published_ms INTEGER,
            seen_ms INTEGER NOT NULL, assets TEXT, score REAL, scorer TEXT)""")

    def add(self, items: Iterable[Headline]) -> list[Headline]:
        new = []
        for h in items:
            cur = self.db.execute("INSERT OR IGNORE INTO headlines (id,title,url,source,published_ms,seen_ms,assets) VALUES (?,?,?,?,?,?,?)",
                                  (h.id, h.title, h.url, h.source, h.published_ms, h.seen_ms, json.dumps(assets_mentioned(h.title))))
            if cur.rowcount:
                new.append(h)
        self.db.commit()
        return new

    def unscored(self, limit: int = 50) -> list[tuple[str, str]]:
        return self.db.execute("SELECT id, title FROM headlines WHERE score IS NULL AND assets != '[]' LIMIT ?", (limit,)).fetchall()

    def set_scores(self, rows: list[tuple[str, float | None]], scorer: str):
        self.db.executemany("UPDATE headlines SET score=?, scorer=? WHERE id=?", [(s if s is not None else 0.0, scorer, i) for i, s in rows])
        self.db.commit()

    def signal(self, asset: str, as_of_ms: int, window_ms: int = 6 * 3_600_000, half_life_ms: int = 2 * 3_600_000) -> tuple[float, int]:
        """Time-decayed mean score of headlines SEEN in (as_of - window, as_of]."""
        rows = self.db.execute("SELECT seen_ms, score, assets FROM headlines WHERE seen_ms <= ? AND seen_ms > ? AND score IS NOT NULL",
                               (as_of_ms, as_of_ms - window_ms)).fetchall()
        num = den = 0.0
        n = 0
        for seen, score, assets in rows:
            if asset not in json.loads(assets):
                continue
            w = 0.5 ** ((as_of_ms - seen) / half_life_ms)
            num += w * score
            den += w
            n += 1
        return (num / den if den else 0.0), n


def refresh(store: SentimentStore, feeds=DEFAULT_FEEDS, fetch: Callable[[str], str] = fetch_rss,
            scorer: Callable[[list[str]], list[float | None]] | None = None, now_ms: int | None = None) -> dict:
    """Pull feeds, store new headlines, score what's unscored. Failures per feed are reported, not fatal."""
    now_ms = now_ms or int(time.time() * 1000)
    report = {"new": 0, "errors": {}}
    for url in feeds:
        try:
            report["new"] += len(store.add(parse_rss(fetch(url), url, now_ms)))
        except Exception as e:
            report["errors"][url] = f"{type(e).__name__}: {e}"[:200]
    pending = store.unscored()
    if pending:
        titles = [t for _, t in pending]
        name = "lexicon"
        scores: list[float | None]
        if scorer is not None:
            try:
                scores, name = scorer(titles), "claude"
            except Exception as e:
                report["errors"]["scorer"] = f"{type(e).__name__}: {e}"[:200]
                scores = [lexicon_score(t) for t in titles]
        else:
            scores = [lexicon_score(t) for t in titles]
        store.set_scores([(i, s) for (i, _), s in zip(pending, scores)], name)
        report["scored"] = len(pending)
    return report
