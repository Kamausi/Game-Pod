-- PostgreSQL / TimescaleDB version of src/tradebot/storage.py (same column names).
CREATE EXTENSION IF NOT EXISTS timescaledb CASCADE;

CREATE TABLE IF NOT EXISTS assets (
  asset_id TEXT PRIMARY KEY, symbol TEXT NOT NULL, asset_class TEXT NOT NULL, tier TEXT NOT NULL,
  base_currency TEXT, quote_currency TEXT, min_order_size NUMERIC(24, 8), price_precision INT);

CREATE TABLE IF NOT EXISTS market_ticks (
  ts TIMESTAMPTZ NOT NULL, asset_id TEXT NOT NULL, bid NUMERIC(24, 10) NOT NULL, ask NUMERIC(24, 10) NOT NULL,
  volume NUMERIC(30, 8), recv_ts TIMESTAMPTZ);
SELECT create_hypertable('market_ticks', 'ts', if_not_exists => TRUE);
CREATE INDEX IF NOT EXISTS idx_ticks_asset_time ON market_ticks (asset_id, ts DESC);

CREATE TABLE IF NOT EXISTS orders (
  client_order_id TEXT PRIMARY KEY, asset_id TEXT NOT NULL, asset_class TEXT NOT NULL, side TEXT NOT NULL,
  order_type TEXT NOT NULL, requested_qty NUMERIC(30, 10) NOT NULL, filled_qty NUMERIC(30, 10) DEFAULT 0,
  limit_price NUMERIC(24, 10), avg_fill_price NUMERIC(24, 10), status TEXT NOT NULL, fee NUMERIC(18, 8) DEFAULT 0,
  venue TEXT, created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(), updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW());
CREATE INDEX IF NOT EXISTS idx_orders_status ON orders (status);

CREATE TABLE IF NOT EXISTS fills (
  client_order_id TEXT NOT NULL REFERENCES orders (client_order_id), ts TIMESTAMPTZ NOT NULL,
  qty NUMERIC(30, 10) NOT NULL, price NUMERIC(24, 10) NOT NULL, fee NUMERIC(18, 8) NOT NULL);
SELECT create_hypertable('fills', 'ts', if_not_exists => TRUE);
