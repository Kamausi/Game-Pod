Real responses captured 2026-10-14 through the Firecrawl connector (the build host can't reach these hosts), trimmed to the fields the code reads:

- `form4_pcor_0001628280-26-065924.xml`: SEC Form 4 (Procore, chair filed as isOther + otherText, aff10b5One=1). Derivative table, holdings and two footnotes removed.
- `edgar_current_form4.atom`: EDGAR current Form 4 feed, three entries (one filing appears as Reporting and as Issuer).
- `edgar_index_pcor.json`: the filing folder's index.json.
- `sec_concept_pcor_eps.json`: data.sec.gov companyconcept EarningsPerShareDiluted, frame-tagged facts only.
- `polymarket_nyc_high_2026-10-16.json`: gamma-api event "Highest temperature in NYC on October 16?" with its 11 bucket markets.
- `nws_okx_hourly_2026-10-14.json`: NWS hourly forecast for gridpoint OKX/37,39 (format sample; LaGuardia is OKX/37,46).
- `nws_klga_latest.json`, `nws_points_lga.json`: station observation and points lookup for KLGA.
