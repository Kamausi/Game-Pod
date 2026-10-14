"""Alerts to a webhook (Slack, Discord, or anything that accepts JSON).

Set TRADEBOT_ALERT_WEBHOOK. The payload carries both `text` (Slack) and
`content` (Discord). Sending never raises: a failed alert is written to the
journal instead, so an alerting outage can't stop the risk layer.

Levels: "critical" (kill switch, reconciliation failure, halted loop),
"warn" (rejects, canary blocks), "info" (fills, daily summary).
"""
from __future__ import annotations

import json
import os
import socket
import time
import urllib.request
from typing import Callable

Send = Callable[[str, dict], None]


def _http_send(url: str, payload: dict) -> None:
    req = urllib.request.Request(url, data=json.dumps(payload).encode(), headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=5) as r:
        r.read()


class Alerts:
    ICON = {"critical": "\U0001F6A8", "warn": "⚠️", "info": "ℹ️"}

    def __init__(self, url: str | None = None, send: Send = _http_send, journal=None, min_level: str = "info",
                 tag: str | None = None):
        self.url = url if url is not None else os.environ.get("TRADEBOT_ALERT_WEBHOOK", "")
        self.send, self.journal = send, journal
        self.min = ["info", "warn", "critical"].index(min_level)
        self.tag = tag or socket.gethostname()
        self.sent: list[dict] = []

    def __call__(self, level: str, title: str, **fields) -> bool:
        if ["info", "warn", "critical"].index(level) < self.min:
            return False
        lines = [f"{self.ICON[level]} [{self.tag}] {title}"] + [f"  {k}: {v}" for k, v in fields.items()]
        payload = {"text": "\n".join(lines), "content": "\n".join(lines)[:1900], "level": level, "title": title, "fields": fields}
        self.sent.append(payload)
        if not self.url:
            return False
        try:
            self.send(self.url, payload)
            return True
        except Exception as e:  # never let alerting break trading or the kill path
            if self.journal is not None:
                self.journal.write("alert_failed", int(time.time() * 1000), error=f"{type(e).__name__}: {e}"[:300], title=title)
            return False
