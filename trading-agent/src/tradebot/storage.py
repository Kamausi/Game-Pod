"""Persistent store for ticks, orders and fills.

SQLite by default (stdlib, zero setup). The same tables exist for
PostgreSQL/TimescaleDB in sql/timescale.sql for when one machine's disk is
not enough; the column names match so exports move across unchanged.
"""
from __future__ import annotations

import sqlite3
from pathlib import Path

SCHEMA = """
CREATE TABLE IF NOT EXISTS assets (
  asset_id TEXT PRIMARY KEY, symbol TEXT NOT NULL, asset_class TEXT NOT NULL, tier TEXT NOT NULL,
  base_currency TEXT, quote_currency TEXT, min_order_size REAL, price_precision INTEGER);
CREATE TABLE IF NOT EXISTS market_ticks (
  ts_ms INTEGER NOT NULL, asset_id TEXT NOT NULL, bid REAL NOT NULL, ask REAL NOT NULL, volume REAL, recv_ms INTEGER);
CREATE INDEX IF NOT EXISTS idx_ticks_asset_time ON market_ticks (asset_id, ts_ms DESC);
CREATE TABLE IF NOT EXISTS orders (
  client_order_id TEXT PRIMARY KEY, asset_id TEXT NOT NULL, asset_class TEXT NOT NULL, side TEXT NOT NULL,
  order_type TEXT NOT NULL, requested_qty REAL NOT NULL, filled_qty REAL DEFAULT 0, limit_price REAL,
  avg_fill_price REAL, status TEXT NOT NULL, fee REAL DEFAULT 0, venue TEXT, created_ms INTEGER NOT NULL, updated_ms INTEGER NOT NULL);
CREATE INDEX IF NOT EXISTS idx_orders_status ON orders (status);
CREATE TABLE IF NOT EXISTS fills (
  client_order_id TEXT NOT NULL, ts_ms INTEGER NOT NULL, qty REAL NOT NULL, price REAL NOT NULL, fee REAL NOT NULL,
  FOREIGN KEY (client_order_id) REFERENCES orders (client_order_id));
"""


class Store:
    def __init__(self, path: str | Path = "runs/tradebot.sqlite"):
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(str(path))
        self.db.executescript(SCHEMA)

    def tick(self, ts_ms, asset_id, bid, ask, volume=None, recv_ms=None):
        self.db.execute("INSERT INTO market_ticks VALUES (?,?,?,?,?,?)", (ts_ms, asset_id, bid, ask, volume, recv_ms))

    def order(self, cid, asset_id, asset_class, side, order_type, qty, status, ts_ms, limit_price=None, venue=None):
        self.db.execute("""INSERT INTO orders (client_order_id, asset_id, asset_class, side, order_type, requested_qty, limit_price,
                           status, venue, created_ms, updated_ms) VALUES (?,?,?,?,?,?,?,?,?,?,?)
                           ON CONFLICT(client_order_id) DO UPDATE SET status=excluded.status, updated_ms=excluded.updated_ms""",
                        (cid, asset_id, asset_class, side, order_type, qty, limit_price, status, venue, ts_ms, ts_ms))

    def fill(self, cid, ts_ms, qty, price, fee):
        self.db.execute("INSERT INTO fills VALUES (?,?,?,?,?)", (cid, ts_ms, qty, price, fee))
        self.db.execute("""UPDATE orders SET filled_qty = (SELECT SUM(qty) FROM fills WHERE client_order_id=?),
                           avg_fill_price = (SELECT SUM(qty*price)/SUM(qty) FROM fills WHERE client_order_id=?),
                           fee = (SELECT SUM(fee) FROM fills WHERE client_order_id=?), updated_ms=? WHERE client_order_id=?""",
                        (cid, cid, cid, ts_ms, cid))

    def commit(self):
        self.db.commit()
