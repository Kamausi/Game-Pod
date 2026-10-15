"""Market-wide scanner: many ranked lists across every coin, then one diversified watchlist.

A fixed list of majors and memes means the bot only ever looks at the same handful of coins. This
module ranks the whole market by several independent criteria and lets each one contribute picks:

  CoinGecko /coins/markets (top 250 by market cap; 1h/24h/7d change, volume, ATH distance)
    gainers_1h, gainers_24h, gainers_7d, losers_24h, most_traded, market_cap, turnover
    (volume / market cap: unusual activity), volatility (24 h high-low range), near_ath
  CoinGecko /search/trending   most_searched (the top 15 searches on CoinGecko in the last 24 h)
  CoinGecko /global            dominance (each coin's share of total crypto market cap, and BTC
                               dominance as a regime input) and an alt-season reading
  Crypto.com get-tickers       cc_gainers, cc_losers, cc_most_traded (every spot USD pair, so it
                               covers coins outside the CoinGecko top 250), cc_open_interest (perps)

Endpoints and field names verified 2026-10-15 (fixtures in tests/fixtures).

The watchlist is round-robin: criterion 1's best pick, criterion 2's best pick, ... so no single
list (and no single market-cap tier) fills it. A pick must be a real, tradable coin: stablecoins,
wrapped and staked derivatives are skipped, it must be listed on the venue, and the order must stay
under 1% of the venue's own 24 h volume. Being on a list is a reason to LOOK, not to buy: the desk's
regime, FOMO and 1% risk gates still decide every entry.
"""
from __future__ import annotations

import json
import re
import urllib.request

from . import universe as u

STABLE = {"usdt", "usdc", "dai", "usds", "usde", "fdusd", "pyusd", "tusd", "usd1", "rlusd", "usdd", "gusd", "usdp", "frax",
          "lusd", "susd", "eurc", "eurt", "usdy", "usdtb", "usdf", "bfusd", "susde", "syrupusdc", "usdg", "xaut", "paxg", "buidl"}
DERIVATIVE = {"steth", "wsteth", "weth", "wbtc", "cbbtc", "weeth", "reth", "meth", "jitosol", "msol", "bnsol", "cbeth", "lbtc",
              "solvbtc", "rseth", "ezeth", "wbeth", "jupsol", "bbsol", "tbtc", "wsol", "clbtc", "btcb", "sweth", "oseth", "lseth"}
DERIVATIVE_WORDS = re.compile(r"\b(wrapped|staked|bridged|restaked|liquid staking|binance-peg|tokenized|treasury|heloc|money market|yield fund)\b", re.I)

CG_CRITERIA = ("gainers_1h", "gainers_24h", "gainers_7d", "losers_24h", "most_traded", "market_cap", "turnover",
               "volatility", "near_ath", "most_searched", "dominance")
CC_CRITERIA = ("cc_gainers", "cc_losers", "cc_most_traded", "cc_open_interest")
DEFAULT_ORDER = ("most_searched", "gainers_24h", "cc_gainers", "turnover", "cc_most_traded", "gainers_7d", "volatility",
                 "losers_24h", "gainers_1h", "most_traded", "near_ath", "market_cap", "cc_losers", "cc_open_interest", "dominance")


def _n(x) -> float | None:
    if x in (None, ""):
        return None
    if isinstance(x, str):
        x = x.replace("$", "").replace(",", "").strip()
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def skip_reason(symbol: str, name: str = "", price: float | None = None, hi: float | None = None, lo: float | None = None) -> str | None:
    s = symbol.lower()
    if s in STABLE or (s.startswith("usd") and len(s) <= 6) or (s.endswith("usd") and len(s) <= 7) or (s.startswith("eur") and len(s) <= 6):
        return "stablecoin"
    if price and hi and lo and 0.97 < price < 1.03 and (hi - lo) / price < 0.004:
        return "stablecoin (pegged near $1)"
    if s in DERIVATIVE or DERIVATIVE_WORDS.search(name or ""):
        return "wrapped / staked derivative"
    if price and hi and lo and (hi - lo) / price < 0.003:
        return "does not move (tokenized fund, peg or dead market)"
    return None


def cc_base(name: str) -> tuple[str, str] | None:
    """'XRPUSD' -> ('XRP', 'spot'); 'XRPUSDPERP' -> ('XRP', 'perp'); 'XRP_USD' -> ('XRP', 'spot')."""
    if name.endswith("USDPERP"):
        return name[:-7], "perp"
    if name.endswith("_USD"):
        return name[:-4], "spot"
    if name.endswith("USD") and "_" not in name and "-" not in name:
        return name[:-3], "spot"
    return None


def cc_spot(tickers: list[dict]) -> dict[str, dict]:
    """Base symbol -> normalized spot ticker (with 24 h change), and the perps' open interest."""
    out, oi = {}, {}
    for t in tickers:
        name = t.get("instrument_name") or t.get("i") or ""
        b = cc_base(name)
        if not b:
            continue
        base, kind = b
        if kind == "perp":
            px = _n(t.get("last") or t.get("a")) or 0
            oi[base] = (_n(t.get("open_interest") or t.get("oi")) or 0) * px
            continue
        n = u.normalize_cryptocom(t)
        if n:
            n["change"] = _n(t.get("change") or t.get("c")) or 0.0
            out[base] = n
    for base, v in oi.items():
        if base in out:
            out[base]["open_interest_usd"] = v
        else:
            out.setdefault("_perp_only", {})[base] = v
    return out


def _cg_rows(markets: list[dict]) -> list[dict]:
    rows = []
    for m in markets:
        sym = (m.get("symbol") or "").upper()
        if not sym:
            continue
        rows.append({"symbol": sym, "id": m.get("id"), "name": m.get("name") or "", "price": _n(m.get("current_price")),
                     "market_cap": _n(m.get("market_cap")) or 0.0, "rank": m.get("market_cap_rank"), "volume": _n(m.get("total_volume")) or 0.0,
                     "hi": _n(m.get("high_24h")), "lo": _n(m.get("low_24h")),
                     "ch1h": _n(m.get("price_change_percentage_1h_in_currency")),
                     "ch24h": _n(m.get("price_change_percentage_24h_in_currency", m.get("price_change_percentage_24h"))),
                     "ch7d": _n(m.get("price_change_percentage_7d_in_currency")), "ath_pct": _n(m.get("ath_change_percentage"))})
    return rows


def global_view(glob: dict | None, markets: list[dict] | None = None) -> dict:
    """BTC/ETH dominance, total cap, and an alt-season reading: the share of the top-50 non-stable,
    non-derivative coins that beat BTC over 7 days (Blockchaincenter's index uses 90 days and calls
    75%+ alt season; 7 days is what /coins/markets gives, so this is a short-horizon version)."""
    d = (glob or {}).get("data", glob or {})
    pct = d.get("market_cap_percentage") or {}
    out = {"total_market_cap_usd": _n((d.get("total_market_cap") or {}).get("usd")),
           "total_volume_usd": _n((d.get("total_volume") or {}).get("usd")),
           "market_cap_change_24h_pct": _n(d.get("market_cap_change_percentage_24h_usd")),
           "btc_dominance": _n(pct.get("btc")), "eth_dominance": _n(pct.get("eth")),
           "stablecoin_dominance": round(sum(v for k, v in pct.items() if k in STABLE), 3) if pct else None,
           "dominance_top": {k.upper(): round(v, 3) for k, v in sorted(pct.items(), key=lambda kv: -kv[1])}}
    if markets:
        rows = _cg_rows(markets)
        btc = next((r for r in rows if r["id"] == "bitcoin"), None)
        alts = [r for r in rows if r["id"] != "bitcoin" and not skip_reason(r["symbol"], r["name"], r["price"], r["hi"], r["lo"])][:50]
        if btc and btc["ch7d"] is not None and alts:
            beat = sum(1 for r in alts if r["ch7d"] is not None and r["ch7d"] > btc["ch7d"])
            share = beat / len(alts)
            out["alt_season_7d"] = {"beat_btc": beat, "of": len(alts), "share": round(share, 3),
                                    "reading": "alt season" if share >= 0.75 else "bitcoin season" if share <= 0.25 else "mixed"}
    b = out["btc_dominance"]
    if b is not None:
        out["dominance_note"] = ("BTC dominance above 60%: money is concentrated in bitcoin; alts tend to lag" if b >= 60 else
                                 "BTC dominance under 45%: capital is spread into alts (and so is the risk)" if b < 45 else
                                 "BTC dominance mid-range")
    return out


def lists(markets: list[dict] | None = None, trending: dict | None = None, glob: dict | None = None,
          tickers: list[dict] | None = None, top: int = 15, min_volume_usd: float = 1e6) -> dict[str, list[dict]]:
    """Every criterion as its own ranked list. Stablecoins, wrapped tokens, coins with no 24 h range
    reported, and coins under $1M of 24 h volume across all exchanges are left out of all of them."""
    out: dict[str, list[dict]] = {}
    rows = [r for r in _cg_rows(markets or []) if r["hi"] and r["lo"] and r["volume"] >= min_volume_usd
            and not skip_reason(r["symbol"], r["name"], r["price"], r["hi"], r["lo"])]

    def by(key, rev=True, value=None, filt=None):
        rs = [r for r in rows if (filt or (lambda r: r.get(key) is not None))(r)]
        rs.sort(key=lambda r: (value(r) if value else r[key]), reverse=rev)
        return [{"symbol": r["symbol"], "id": r["id"], "name": r["name"], "value": round(value(r) if value else r[key], 4)} for r in rs[:top]]

    if rows:
        out["gainers_1h"] = by("ch1h")
        out["gainers_24h"] = by("ch24h")
        out["gainers_7d"] = by("ch7d")
        out["losers_24h"] = by("ch24h", rev=False)
        out["most_traded"] = by("volume")
        out["market_cap"] = by("market_cap")
        out["turnover"] = by("turnover", value=lambda r: r["volume"] / r["market_cap"],
                             filt=lambda r: r["market_cap"] > 0 and r["volume"] > 0)
        out["volatility"] = by("volatility", value=lambda r: (r["hi"] - r["lo"]) / r["price"] * 100,
                               filt=lambda r: r["price"] and r["hi"] and r["lo"])
        out["near_ath"] = by("ath_pct")
    if trending:
        tr = []
        for i, c in enumerate(trending.get("coins", [])):
            it = c.get("item", c)
            sym = (it.get("symbol") or "").upper()
            if sym and not skip_reason(sym, it.get("name", "")):
                d = it.get("data") or {}
                tr.append({"symbol": sym, "id": it.get("id"), "name": it.get("name", ""), "value": i + 1,
                           "ch24h": _n((d.get("price_change_percentage_24h") or {}).get("usd")), "volume": _n(d.get("total_volume"))})
        out["most_searched"] = tr[:top]
    if glob or rows:
        g = (glob or {}).get("data", glob or {})
        total = _n((g.get("total_market_cap") or {}).get("usd")) or sum(r["market_cap"] for r in rows) or 1.0
        out["dominance"] = [{"symbol": r["symbol"], "id": r["id"], "name": r["name"], "value": round(r["market_cap"] / total * 100, 4)}
                            for r in sorted(rows, key=lambda r: -r["market_cap"])[:top]]
    if tickers:
        cc = cc_spot(tickers)
        perp_only = cc.pop("_perp_only", {})
        sp = [(b, t) for b, t in cc.items() if not skip_reason(b, "", t["last"], t["high"], t["low"])]
        live = [(b, t) for b, t in sp if t["volume_usd"] > 0]
        mk = lambda b, v: {"symbol": b, "id": None, "name": "", "value": round(v, 6)}
        out["cc_gainers"] = [mk(b, t["change"] * 100) for b, t in sorted(live, key=lambda x: -x[1]["change"])[:top]]
        out["cc_losers"] = [mk(b, t["change"] * 100) for b, t in sorted(live, key=lambda x: x[1]["change"])[:top]]
        out["cc_most_traded"] = [mk(b, t["volume_usd"]) for b, t in sorted(sp, key=lambda x: -x[1]["volume_usd"])[:top]]
        oi = [(b, t.get("open_interest_usd", 0)) for b, t in sp if t.get("open_interest_usd")] + list(perp_only.items())
        out["cc_open_interest"] = [mk(b, v) for b, v in sorted(oi, key=lambda x: -x[1]) if v > 0][:top]
    return out


def watchlist(ls: dict[str, list[dict]], tickers: list[dict], n: int = 12, order: tuple[str, ...] = DEFAULT_ORDER,
              venue: str = "cryptocom", maker: bool = True, order_usd: float = 500, min_volume_usd: float = 100_000,
              max_majors: int = 1, majors: tuple[str, ...] = ("BTC", "ETH", "SOL")) -> dict:
    """Round-robin across the criteria; each pick must be listed and liquid on the venue."""
    cc = cc_spot(tickers)
    cc.pop("_perp_only", None)
    crit = [c for c in order if ls.get(c)]
    seen_in: dict[str, list[str]] = {}
    for c in crit:
        for i, r in enumerate(ls[c]):
            seen_in.setdefault(r["symbol"], []).append(f"{c} #{i + 1}")
    picks, skipped, taken, cursor = [], {}, set(), {c: 0 for c in crit}
    n_major = 0
    while len(picks) < n and any(cursor[c] < len(ls[c]) for c in crit):
        for c in crit:
            if len(picks) >= n:
                break
            while cursor[c] < len(ls[c]):
                r = ls[c][cursor[c]]
                cursor[c] += 1
                s = r["symbol"]
                if s in taken:
                    continue
                t = cc.get(s)
                why = None
                if t is None:
                    why = f"not listed as {s}USD on {venue}"
                elif t["volume_usd"] < min_volume_usd:
                    why = f"only ${t['volume_usd']:,.0f} traded in 24 h on {venue}"
                elif order_usd > 0.01 * t["volume_usd"]:
                    why = "the order would be over 1% of the venue's daily volume"
                elif s in majors and n_major >= max_majors:
                    why = "majors cap reached"
                if why:
                    skipped.setdefault(s, why)
                    continue
                cost = u.cost_row(s + "USD", "spot", t["bid"], t["ask"], t["last"], t["high"], t["low"], t["volume_usd"], venue, maker, order_usd)
                taken.add(s)
                n_major += s in majors
                picks.append({"symbol": s, "instrument": f"{s}_USD", "picked_by": c, "also_in": [x for x in seen_in.get(s, []) if not x.startswith(c + " ")],
                              "change_24h_pct": round(t["change"] * 100, 2), "volume_usd": round(t["volume_usd"]), "move_bps": cost.move_bps,
                              "cost_bps": cost.cost_bps, "move_over_cost": cost.score})
                break
    return {"watchlist": picks, "criteria": crit, "skipped": skipped}


def scan(markets=None, trending=None, glob=None, tickers=None, top: int = 15, **kw) -> dict:
    ls = lists(markets, trending, glob, tickers, top=top)
    out = {"global": global_view(glob, markets), "lists": ls}
    if tickers:
        out.update(watchlist(ls, tickers, **kw))
    return out


# ------------------------------------------------------------------ fetchers (public, no keys)

CG = "https://api.coingecko.com/api/v3"


def _get(url: str):
    req = urllib.request.Request(url, headers={"User-Agent": "tradebot-scanner", "Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.loads(r.read())


def fetch_all(get=_get, pages: int = 1) -> dict:
    """CoinGecko's free tier allows ~30 calls a minute; one scan is pages + 2 calls."""
    markets = []
    for p in range(1, pages + 1):
        markets += get(f"{CG}/coins/markets?vs_currency=usd&order=market_cap_desc&per_page=250&page={p}&price_change_percentage=1h,24h,7d")
    return {"markets": markets, "trending": get(f"{CG}/search/trending"), "glob": get(f"{CG}/global"),
            "tickers": u.fetch_cryptocom_tickers(get)}


def report(res: dict, top: int = 5) -> str:
    g = res["global"]
    lines = []
    if g.get("btc_dominance") is not None:
        lines.append(f"BTC dominance {g['btc_dominance']:.1f}%  ETH {g['eth_dominance']:.1f}%  stablecoins {g['stablecoin_dominance']:.1f}%  "
                     f"total cap ${(g['total_market_cap_usd'] or 0) / 1e12:.2f}T ({g['market_cap_change_24h_pct']:+.1f}% 24h)")
        lines.append(g.get("dominance_note", ""))
    if g.get("alt_season_7d"):
        a = g["alt_season_7d"]
        lines.append(f"alt season (7d): {a['beat_btc']}/{a['of']} top alts beat BTC -> {a['reading']}")
    for c, rs in res["lists"].items():
        lines.append(f"{c:<17}" + "  ".join(f"{r['symbol']}({r['value']:.4g})" for r in rs[:top]))
    if "watchlist" in res:
        lines.append("")
        lines.append(f"{'watchlist':<10}{'picked by':<17}{'24h':>8}{'move':>8}{'cost':>8}{'m/c':>6}  also in")
        for p in res["watchlist"]:
            lines.append(f"{p['symbol']:<10}{p['picked_by']:<17}{p['change_24h_pct']:>7.1f}%{p['move_bps']:>7.0f}b{p['cost_bps']:>7.1f}b"
                         f"{p['move_over_cost']:>6.1f}  {', '.join(p['also_in'][:4])}")
    return "\n".join(lines)
