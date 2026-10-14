"""TypeSafe Jev: a System One model that returns typed, calibrated answers, not text.

API (docs.typesafe.ai/api): POST https://api.typesafe.ai/v1/systemone with
{"state", "model", "questions": {id: {"type": "noul"|"choice"|"score", "instructions", "criteria"}}}.
Answers come back under the same ids:
  noul   -> {"noul": p}
  choice -> {"choice", "probabilities", "confidence"}
  score  -> {"score", "legend", "probabilities", "confidence"}
Errors: 401 bad key, 422 malformed question (never retry), 429 / 529 back off.

How it is used here, following TypeSafe's own rule "atomic questions, composed in code":
  JevEngine          the reflex: regime, direction, flow toxicity, setup quality and the
                     two bracket outcomes, asked as separate questions in ONE call. Code
                     builds the Decision; sizing and exits stay in policy.py.
  JevHeadlineScorer  one call per headline: which asset, sentiment, materiality,
                     regulatory risk (the news overlay in news.py).
  JevOrderRisk       the second model in front of the exchange (order_review.py).

Every call records the exact state, the questions, the raw answers and the model
version that served them (rule: save exactly what the model saw).

Needs TYPESAFE_API_KEY. Pin TYPESAFE_MODEL to a version (e.g. jev-1.13.0) once you
have calibrated thresholds: an alias moves when a release ships. Tested against a
fake transport built from the documented response shapes; not called live from the
build machine (no key).
"""
from __future__ import annotations

import json
import os
import time
import urllib.error
import urllib.request
from typing import Callable

from .schema import REGIMES, Decision
from .state import Snapshot

API = "https://api.typesafe.ai/v1/systemone"
Transport = Callable[[str, dict, dict, float], tuple[int, dict]]


class JevError(RuntimeError):
    def __init__(self, status: int, body: dict):
        super().__init__(f"jev returned {status}: {str(body)[:300]}")
        self.status, self.body = status, body


def _post(url: str, headers: dict, body: dict, timeout: float) -> tuple[int, dict]:
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST",
                                 headers={"Content-Type": "application/json", **headers})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        raw = e.read()
        try:
            return e.code, json.loads(raw)
        except ValueError:
            return e.code, {"raw": raw.decode(errors="replace")[:300]}


def noul(instructions, true: str | None = None, false: str | None = None) -> dict:
    q = {"type": "noul", "instructions": instructions}
    if true or false:
        q["criteria"] = {k: v for k, v in (("true", true), ("false", false)) if v}
    return q


def choice(instructions, criteria: dict) -> dict:
    if not 2 <= len(criteria) <= 255:
        raise ValueError("a choice needs 2 to 255 options")
    return {"type": "choice", "instructions": instructions, "criteria": criteria}


def score(instructions, levels: list) -> dict:
    if not 2 <= len(levels) <= 10:
        raise ValueError("a score needs 2 to 10 levels")
    return {"type": "score", "instructions": instructions, "criteria": levels}


class JevClient:
    def __init__(self, api_key: str | None = None, model: str | None = None, url: str = API,
                 transport: Transport = _post, deadline_s: float = 2.0, sleep=time.sleep):
        self._key = api_key or os.environ.get("TYPESAFE_API_KEY", "")
        if not self._key:
            raise ValueError("TYPESAFE_API_KEY is not set")
        self.model = model or os.environ.get("TYPESAFE_MODEL", "jev-latest")
        self.url, self.t, self.deadline_s, self.sleep = url, transport, deadline_s, sleep
        self.usage = {"calls": 0, "input_tokens": 0, "output_tokens": 0}

    def __repr__(self):
        return f"JevClient(model={self.model})"

    def ask(self, state, questions: dict) -> dict:
        """One call, every question evaluated in parallel. Returns the raw response.
        Past the deadline it raises: a late decision is a missing decision."""
        start = time.monotonic()
        body = {"state": state, "model": self.model, "questions": questions}
        for attempt in range(3):
            left = self.deadline_s - (time.monotonic() - start)
            if left <= 0:
                raise TimeoutError("jev decision missed its deadline")
            status, js = self.t(self.url, {"Authorization": f"Bearer {self._key}"}, body, left)
            if status == 200:
                self.usage["calls"] += 1
                for k in ("input_tokens", "output_tokens"):
                    self.usage[k] += int((js.get("usage") or {}).get(k, 0))
                missing = set(questions) - set(js.get("answers", {}))
                if missing:
                    raise JevError(200, {"missing_answers": sorted(missing)})
                return js
            if status in (429, 529) and attempt < 2:
                self.sleep(min(0.25 * 2 ** attempt, max(0.0, self.deadline_s - (time.monotonic() - start))))
                continue
            raise JevError(status, js)  # 401 / 422 / exhausted retries: never loop on these
        raise JevError(status, js)


# ------------------------------------------------------------------ engine
REGIME_CRITERIA = {
    "trending": "Short and long trends agree (`trend_fast`, `trend_slow`) and the 24-bar move is large relative to `rv_24`.",
    "mean_reverting": "No persistent direction; price oscillates around its 20-bar mean (`z_20`, `pct_b`).",
    "high_vol": "Short-window volatility well above its norm (`rv_ratio` > ~1.6) without a disorderly break.",
    "crisis": "Disorderly: `rv_ratio` far above norm or a single-bar move many times `rv_24`.",
}


class JevEngine:
    """Jev answers six atomic questions about one snapshot; code composes the Decision."""

    def __init__(self, client: JevClient | None = None, horizon_bars: int = 24, stop_pct: float = 0.02, take_pct: float = 0.04,
                 toxic_at: float = 0.6):
        self.client = client or JevClient()
        self.horizon, self.stop, self.take, self.toxic_at = horizon_bars, stop_pct, take_pct, toxic_at
        self.name = f"jev:{self.client.model}"
        self.last_trace: dict | None = None

    def questions(self) -> dict:
        h, st, tk = self.horizon, f"{self.stop:.1%}", f"{self.take:.1%}"
        return {
            "regime": choice("Which market regime does this state describe?", REGIME_CRITERIA),
            "direction": choice(f"Which way is price more likely to move over the next {h} bars?",
                                {"long": "Up", "short": "Down", "neutral": "No usable bias"}),
            "up_first": noul(f"Starting from `mid`, price rises {tk} before it falls {st} within {h} bars."),
            "down_first": noul(f"Starting from `mid`, price falls {tk} before it rises {st} within {h} bars."),
            "toxic_flow": noul("Order flow is likely informed or the book is too thin to trade safely (`spread_bps`, `imbalance`, `vol_ratio`)."),
            "setup_quality": score("How clean is this setup for a directional entry?",
                                   ["No setup", "Weak, conflicting signals", "Decent, most signals agree", "Clean, trend and momentum aligned"]),
        }

    def state(self, s: Snapshot) -> dict:
        from dataclasses import asdict

        d = {k: (round(v, 6) if isinstance(v, float) else v) for k, v in asdict(s).items()}
        d["bracket"] = {"stop_pct": self.stop, "take_pct": self.take, "horizon_bars": self.horizon}
        return d

    def decide(self, s: Snapshot) -> Decision:
        state, qs = self.state(s), self.questions()
        self.last_trace = {"engine": "jev", "model_requested": self.client.model, "state": state, "questions": qs}
        t0 = time.monotonic()
        js = self.client.ask(state, qs)
        a = js["answers"]
        self.last_trace.update(model_served=js.get("model"), answers=a, usage=js.get("usage"), latency_ms=(time.monotonic() - t0) * 1000)
        direction = a["direction"]["choice"]
        conf = float(a["direction"]["probabilities"].get(direction, 0.0))
        p_win = float(a["up_first"]["noul"] if direction == "long" else a["down_first"]["noul"] if direction == "short" else 0.5)
        regime = a["regime"]["choice"]
        if regime not in REGIMES:
            raise ValueError(f"jev returned unknown regime {regime!r}")
        risk_state = "safe" if s.drawdown < 0.05 and s.day_pnl_pct > -0.015 else "near_limit"  # arithmetic: code, not model
        return Decision(regime=regime, direction=direction, direction_confidence=conf, p_win=p_win,
                        toxic_flow=float(a["toxic_flow"]["noul"]) > self.toxic_at,
                        setup_quality=max(0, min(3, round(float(a["setup_quality"]["score"])))),
                        risk_state=risk_state, rationale=f"jev {js.get('model')} dir_conf={conf:.2f}", source=self.name)


# ------------------------------------------------------------------ headlines
class JevHeadlineScorer:
    """One call per headline. Returns per headline: asset, sentiment [-1,1], material, regulatory."""

    def __init__(self, assets: list[str], client: JevClient | None = None):
        self.client = client or JevClient(deadline_s=10.0)
        self.assets = assets

    def questions(self) -> dict:
        crit = {a: None for a in self.assets}
        crit["none"] = "The headline is not about any listed asset"
        return {
            "asset": choice("Which asset is this headline mainly about?", crit),
            "sentiment": score("Likely short-term price impact on that asset",
                               ["Clearly negative", "Mildly negative", "Neutral or unclear", "Mildly positive", "Clearly positive"]),
            "material": noul("This is new, material information rather than opinion, a recap of a move that already happened, or an ad."),
            "regulatory": noul("This concerns regulatory or legal action: enforcement, lawsuits, bans, sanctions, or a regulator's decision."),
        }

    def classify(self, title: str, source: str = "", seen_iso: str = "") -> dict:
        js = self.client.ask({"headline": title, "source": source, "seen": seen_iso}, self.questions())
        a = js["answers"]
        return {"asset": None if a["asset"]["choice"] == "none" else a["asset"]["choice"],
                "sentiment": (float(a["sentiment"]["score"]) - 2) / 2, "material": float(a["material"]["noul"]),
                "regulatory": float(a["regulatory"]["noul"]), "model": js.get("model")}


class JevOrderRisk:
    """The second model in front of the exchange: one noul per proposed entry."""

    def __init__(self, client: JevClient | None = None):
        self.client = client or JevClient()
        self.name = f"jev:{self.client.model}"

    def __call__(self, order_state: dict) -> float:
        js = self.client.ask(order_state, {"stopped_out": noul(
            "This entry is likely to hit its stop (or be liquidated) before its target, given `size_frac`, "
            "`stop_pct`, `atr_pct`, `rv_ratio`, `spread_bps` and the regime.")})
        return float(js["answers"]["stopped_out"]["noul"])
