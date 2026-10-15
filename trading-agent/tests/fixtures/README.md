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
