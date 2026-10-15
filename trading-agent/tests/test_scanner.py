"""Market-wide scanner on real CoinGecko and Crypto.com responses saved 2026-10-15."""
import json

import pytest

from tradebot import scanner as sc
from test_universe_whales import FX

L = lambda f: json.loads((FX / f).read_text())


@pytest.fixture(scope="module")
def src():
    return dict(markets=L("coingecko_markets_2026-10-15.json"), trending=L("coingecko_trending_2026-10-15.json"),
                glob=L("coingecko_global_2026-10-15.json"), tickers=L("cryptocom_tickers_full_2026-10-15.json")["data"])


def test_every_criterion_is_its_own_list(src):
    ls = sc.lists(**src)
    assert set(sc.CG_CRITERIA + sc.CC_CRITERIA) == set(ls)
    g = [r["value"] for r in ls["gainers_24h"]]
    assert g == sorted(g, reverse=True) and ls["losers_24h"][0]["value"] < 0
    assert ls["most_traded"][0]["symbol"] == "BTC" and ls["market_cap"][0]["symbol"] == "BTC"
    assert [r["symbol"] for r in ls["most_searched"][:3]] == ["ZEREBRO", "BTC", "NEAR"]  # CoinGecko's trending order
    assert ls["dominance"][0]["value"] == pytest.approx(60.1, abs=0.1)
    assert ls["cc_most_traded"][0]["symbol"] == "BTC" and ls["cc_open_interest"][0]["symbol"] == "XRP"
    everything = {r["symbol"] for rs in ls.values() for r in rs}
    assert not everything & {"USDT", "USDC", "USDS", "STETH", "WBTC", "WSTETH", "USYC", "EURCV"}  # nothing that cannot move


def test_global_dominance_and_alt_season(src):
    g = sc.global_view(src["glob"], src["markets"])
    assert g["btc_dominance"] == pytest.approx(60.03, abs=0.01) and g["stablecoin_dominance"] == pytest.approx(9.45, abs=0.01)
    a = g["alt_season_7d"]
    assert a["of"] == 50 and a["reading"] == "bitcoin season" and "concentrated in bitcoin" in g["dominance_note"]


def test_watchlist_is_diversified_tradable_and_liquid(src):
    res = sc.scan(**src, n=12, order_usd=500, min_volume_usd=100_000, max_majors=1)
    wl = res["watchlist"]
    syms = [p["symbol"] for p in wl]
    assert len(wl) == 12 and len(set(syms)) == 12
    assert sum(s in ("BTC", "ETH", "SOL") for s in syms) == 1  # the majors cap
    assert len({p["picked_by"] for p in wl}) >= 10  # round-robin: no single list fills it
    cc = sc.cc_spot(src["tickers"])
    assert all(cc[s]["volume_usd"] >= 100_000 and 500 <= 0.01 * cc[s]["volume_usd"] for s in syms)
    assert "not listed" in res["skipped"]["ZEREBRO"] and "traded in 24 h" in res["skipped"]["ZK"]  # trending, but not tradable here
    assert all(p["instrument"] == p["symbol"] + "_USD" and p["cost_bps"] > 0 for p in wl)


def test_criteria_order_and_cli(tmp_path, capsys):
    from tradebot.cli import main
    out = tmp_path / "scan.json"
    main(["scan", "--markets", str(FX / "coingecko_markets_2026-10-15.json"), "--trending", str(FX / "coingecko_trending_2026-10-15.json"),
          "--global", str(FX / "coingecko_global_2026-10-15.json"), "--tickers", str(FX / "cryptocom_tickers_full_2026-10-15.json"),
          "--criteria", "cc_gainers,turnover", "--n", "4", "--out", str(out)])
    text = capsys.readouterr().out
    res = json.loads(out.read_text())
    assert "BTC dominance 60.0%" in text and res["criteria"] == ["cc_gainers", "turnover"]
    by = [p["picked_by"] for p in res["watchlist"]]
    assert by[:2] == ["cc_gainers", "turnover"] and set(by) == {"cc_gainers", "turnover"}  # a list with nothing liquid left yields its turn


def test_stablecoin_and_derivative_detection():
    assert sc.skip_reason("USDT") == "stablecoin" and sc.skip_reason("XYZ", "", 1.0, 1.001, 0.999).startswith("stablecoin")
    assert "derivative" in sc.skip_reason("JITOSOL") and "derivative" in sc.skip_reason("ABC", "Wrapped ABC")
    assert sc.skip_reason("NEAR", "NEAR Protocol", 4.5, 5.0, 4.4) is None
    assert sc.cc_base("XRPUSDPERP") == ("XRP", "perp") and sc.cc_base("XRP_USD") == ("XRP", "spot") and sc.cc_base("BTC-USD") is None
