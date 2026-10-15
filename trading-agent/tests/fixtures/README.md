Real responses captured 2026-10-14 through the Firecrawl connector (the build host can't reach these hosts), trimmed to the fields the code reads:

- `form4_pcor_0001628280-26-065924.xml`: SEC Form 4 (Procore, chair filed as isOther + otherText, aff10b5One=1). Derivative table, holdings and two footnotes removed.
- `edgar_current_form4.atom`: EDGAR current Form 4 feed, three entries (one filing appears as Reporting and as Issuer).
- `edgar_index_pcor.json`: the filing folder's index.json.
- `sec_concept_pcor_eps.json`: data.sec.gov companyconcept EarningsPerShareDiluted, frame-tagged facts only.
- `polymarket_nyc_high_2026-10-16.json`: gamma-api event "Highest temperature in NYC on October 16?" with its 11 bucket markets.
- `nws_okx_hourly_2026-10-14.json`: NWS hourly forecast for gridpoint OKX/37,39 (format sample; LaGuardia is OKX/37,46).
- `nws_klga_latest.json`, `nws_points_lga.json`: station observation and points lookup for KLGA.

## openmeteo_ensemble_klga.json (captured 2026-10-15)

`GET https://ensemble-api.open-meteo.com/v1/ensemble?latitude=40.7794&longitude=-73.8803&hourly=temperature_2m&models=gfs025,ecmwf_ifs025&temperature_unit=fahrenheit&timezone=America/New_York&forecast_days=3`, fetched through the Firecrawl connector (the host is blocked from the build machine). **Trimmed**: of the 82 runs (GEFS control + 30 members, ECMWF control + 50 members) it keeps the two controls and members 01–05 of each, and only the 24 hours of 2026-10-16; the values are copied unchanged. The key names (`temperature_2m_member01_ncep_gefs025`, `..._ecmwf_ifs025_ensemble`) and the grid snap to 40.75, −74.0 are as returned.

## cryptocom_tickers_2026-10-15.json (captured 2026-10-15 10:40 UTC)

Crypto.com connector `get_tickers` (all instruments), **trimmed** to 11 of 1,038 instruments; values unchanged. Note the spot naming (`BTCUSD`, not `BTC_USD`) and perpetuals (`SOLUSDPERP`) in this listing, while `get_ticker` / `get_candlestick` accept `XRP_USD`.

## yahoo_BTC_USD_daily.csv, yahoo_SOL_USD_daily.csv (captured 2026-10-15)

`GET https://query1.finance.yahoo.com/v8/finance/chart/{BTC-USD,SOL-USD}?range=5y&interval=1d` through the Firecrawl connector, converted to CSV (UTC dates; values rounded to 6 significant figures; days with a missing price dropped). The last row is the day in progress.

## coingecko_markets / coingecko_trending / coingecko_global _2026-10-15.json (captured 2026-10-15 11:42 UTC)

These were fetched through the Firecrawl connector:
- `GET https://api.coingecko.com/api/v3/coins/markets?vs_currency=usd&order=market_cap_desc&per_page=250&page=1&price_change_percentage=1h,24h,7d`. All 250 rows are kept; the fields are **trimmed** to the ones the scanner reads, with values unchanged.
- `GET /search/trending`. All 15 coins are kept, trimmed to id, name, symbol, rank and score, plus the price, the USD 24h change, and the market cap and volume strings, which CoinGecko formats like `"$47,448,272"`.
- `GET /global`. The USD totals and `market_cap_percentage` are kept unchanged; the other currencies are dropped.

## cryptocom_tickers_full_2026-10-15.json (captured 2026-10-15 11:43 UTC)

Crypto.com connector `get_tickers`: all 444 spot `...USD` pairs, plus the 40 perpetuals with the largest open interest. Fields are trimmed (sizes and timestamps dropped); values are unchanged.
