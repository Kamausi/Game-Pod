"""Published statistics the desk was missing, in plain Python (stdlib only, so the bot needs no
numpy at run time). Each one is checked in tests/test_quant.py against an independent reference
implementation (arch, statsmodels, scikit-learn, PyPortfolioOpt, scipy) or a published value.

Accuracy of the edge itself
  psr / dsr / expected_max_sharpe / min_track_record
        Bailey & Lopez de Prado (2012, 2014): the probability that a Sharpe ratio is real, after
        skew, fat tails, a short record and the number of configurations tried. The insider
        backtest already logs every change it runs; dsr() turns that count into a penalty.
  stationary_bootstrap_ci
        Politis & Romano (1994): a confidence interval for a mean that keeps runs of correlated
        trades together (trades from one news week are not independent).
  pbo_cscv
        Bailey, Borwein, Lopez de Prado & Zhu (2015): probability of backtest overfitting by
        combinatorially symmetric cross-validation, for a T x N matrix of N configurations.
  bh_fdr
        Benjamini & Hochberg (1995): which of many tested rules survive a false-discovery rate.
  market_model / abnormal_return
        Event-study abnormal returns (MacKinlay 1997): beat what the stock's beta to the market
        predicts, not just the raw index, on the same days.
  variance_ratio
        Lo & MacKinlay (1988): is a series trending (VR > 1) or mean reverting (VR < 1), with the
        heteroskedasticity-robust z statistic.

Risk, sizing and cost
  parkinson / garman_klass / rogers_satchell / yang_zhang
        Range-based volatility from daily OHLC: Yang & Zhang (2000) is unbiased with opening gaps
        and about 7x as efficient as close-to-close, so stops and size use fewer days of data.
  ewma_vol / garch11
        RiskMetrics (1996) lambda = 0.94, and GARCH(1,1) (Bollerslev 1986) by maximum likelihood,
        for a vol forecast that reacts to clustering.
  sqrt_impact_bps
        Square-root market impact (Almgren et al. 2005; Toth et al. 2011):
        cost ~ Y * sigma_daily * sqrt(Q / ADV), Y ~ 1, plus half the spread.
  ledoit_wolf / hrp_weights
        Ledoit & Wolf (2004) shrinkage covariance; Hierarchical Risk Parity (Lopez de Prado 2016),
        which spreads risk without inverting a noisy covariance matrix.

Calibration of probabilities (LLM p_win, weather buckets)
  brier / log_loss / reliability / isotonic_fit
        Brier (1950); reliability diagram and expected calibration error; isotonic regression by
        pool-adjacent-violators to re-map stated probabilities to observed frequencies.

Live monitoring
  cusum
        Page (1954) one-sided CUSUM: flags a drop in live mean R below the backtest's as soon as
        it is statistically visible, instead of waiting for a fixed trade count.
"""
from __future__ import annotations

import itertools
import math
import random
from statistics import NormalDist

N01 = NormalDist()
EULER_GAMMA = 0.5772156649015329


def _mean(x):
    return sum(x) / len(x)


def _var(x, ddof=1):
    m = _mean(x)
    return sum((v - m) ** 2 for v in x) / (len(x) - ddof)


def moments(x: list[float]) -> tuple[float, float, float, float]:
    """mean, std (ddof=0), skewness, kurtosis (NOT excess; normal = 3), population moments."""
    n, m = len(x), _mean(x)
    m2 = sum((v - m) ** 2 for v in x) / n
    if m2 == 0:
        return m, 0.0, 0.0, 3.0
    m3 = sum((v - m) ** 3 for v in x) / n
    m4 = sum((v - m) ** 4 for v in x) / n
    return m, math.sqrt(m2), m3 / m2 ** 1.5, m4 / m2 ** 2


# ------------------------------------------------------------------ Sharpe statistics


def sharpe(x: list[float]) -> float:
    m, s, _, _ = moments(x)
    return m / s if s else 0.0


def sharpe_se2(sr: float, n: int, skew: float, kurt: float) -> float:
    """Variance of the Sharpe estimator under non-normal returns (Mertens 2002; Bailey & LdP 2012)."""
    return (1 - skew * sr + (kurt - 1) / 4 * sr * sr) / (n - 1)


def psr(x: list[float], sr_star: float = 0.0) -> float:
    """Probabilistic Sharpe Ratio: P(true per-period SR > sr_star)."""
    n = len(x)
    if n < 3:
        return float("nan")
    _, s, g3, g4 = moments(x)
    if s == 0:
        return float("nan")
    sr = _mean(x) / s
    v = sharpe_se2(sr, n, g3, g4)
    return N01.cdf((sr - sr_star) / math.sqrt(v)) if v > 0 else float("nan")


def expected_max_sharpe(n_trials: int, var_sr: float, mean_sr: float = 0.0) -> float:
    """E[max SR] of n_trials independent tries whose SRs have this mean and variance (Bailey &
    Lopez de Prado 2014, eq. 1). With mean 0 and variance 1: N=100 -> 2.53, N=1000 -> 3.26."""
    if n_trials <= 1:
        return mean_sr
    z = (1 - EULER_GAMMA) * N01.inv_cdf(1 - 1 / n_trials) + EULER_GAMMA * N01.inv_cdf(1 - 1 / (n_trials * math.e))
    return mean_sr + math.sqrt(var_sr) * z


def dsr(x: list[float], n_trials: int, var_trials_sr: float | None = None) -> dict:
    """Deflated Sharpe Ratio: PSR against the Sharpe the best of n_trials pure-noise tries would
    show. var_trials_sr is the variance of the per-period Sharpe across the trials actually run;
    unknown -> the estimator's own variance (a lower bound on the penalty)."""
    n = len(x)
    _, s, g3, g4 = moments(x)
    sr = _mean(x) / s if s else 0.0
    v = var_trials_sr if var_trials_sr is not None else sharpe_se2(sr, n, g3, g4)
    sr0 = expected_max_sharpe(max(1, n_trials), v)
    return {"sharpe": sr, "n": n, "skew": g3, "kurtosis": g4, "trials": n_trials, "sr_benchmark": sr0, "dsr": psr(x, sr0)}


def min_track_record(x: list[float], sr_star: float = 0.0, conf: float = 0.95) -> float:
    """Observations needed before PSR(sr_star) reaches conf (Bailey & Lopez de Prado 2012)."""
    _, s, g3, g4 = moments(x)
    sr = _mean(x) / s if s else 0.0
    if sr <= sr_star:
        return math.inf
    return 1 + (1 - g3 * sr + (g4 - 1) / 4 * sr * sr) * (N01.inv_cdf(conf) / (sr - sr_star)) ** 2


# ------------------------------------------------------------------ resampling and overfitting


def stationary_bootstrap_ci(x: list[float], stat=_mean, reps: int = 2000, mean_block: float | None = None,
                            alpha: float = 0.05, seed: int = 7) -> tuple[float, float]:
    """Politis-Romano stationary bootstrap percentile interval. Blocks start at random points and
    have geometric lengths with mean `mean_block` (default n^(1/3))."""
    n = len(x)
    if n < 2:
        return (float("nan"), float("nan"))
    b = mean_block or max(1.0, n ** (1 / 3))
    p, rng, out = 1 / b, random.Random(seed), []
    for _ in range(reps):
        i, sample = rng.randrange(n), []
        for _ in range(n):
            sample.append(x[i])
            i = rng.randrange(n) if rng.random() < p else (i + 1) % n
        out.append(stat(sample))
    out.sort()
    lo = out[max(0, int(math.floor(alpha / 2 * reps)))]
    hi = out[min(reps - 1, int(math.ceil((1 - alpha / 2) * reps)) - 1)]
    return lo, hi


def pbo_cscv(matrix: list[list[float]], s: int = 10, metric=sharpe) -> dict:
    """matrix: T rows (periods) x N columns (configurations' returns). Split rows into s blocks;
    for every half/half split pick the in-sample best and see where it ranks out of sample.
    PBO = share of splits where the in-sample winner is below the out-of-sample median."""
    t, n = len(matrix), len(matrix[0])
    if s % 2 or t < 2 * s or n < 2:
        raise ValueError("need an even s, at least 2*s rows and 2+ configurations")
    size = t // s
    blocks = [list(range(i * size, (i + 1) * size if i < s - 1 else t)) for i in range(s)]
    logits = []
    for combo in itertools.combinations(range(s), s // 2):
        ins = [r for b in combo for r in blocks[b]]
        oos = [r for b in range(s) if b not in combo for r in blocks[b]]
        is_perf = [metric([matrix[r][j] for r in ins]) for j in range(n)]
        oos_perf = [metric([matrix[r][j] for r in oos]) for j in range(n)]
        best = max(range(n), key=lambda j: is_perf[j])
        rank = sum(1 for v in oos_perf if v < oos_perf[best]) + 0.5 * (sum(1 for v in oos_perf if v == oos_perf[best]) - 1) + 1
        w = rank / (n + 1)
        logits.append(math.log(w / (1 - w)))
    return {"pbo": sum(1 for v in logits if v <= 0) / len(logits), "splits": len(logits), "median_logit": sorted(logits)[len(logits) // 2]}


def bh_fdr(pvalues: list[float], q: float = 0.05) -> tuple[list[bool], list[float]]:
    """Benjamini-Hochberg: (rejected?, adjusted p) in the input order."""
    m = len(pvalues)
    order = sorted(range(m), key=lambda i: pvalues[i])
    adj, prev = [0.0] * m, 1.0
    for rank in range(m, 0, -1):
        i = order[rank - 1]
        prev = min(prev, pvalues[i] * m / rank)
        adj[i] = prev
    return [a <= q for a in adj], adj


# ------------------------------------------------------------------ event study, trend tests


def ols(x: list[float], y: list[float]) -> tuple[float, float, float]:
    """y = a + b x: (a, b, residual std with n-2 dof)."""
    mx, my = _mean(x), _mean(y)
    sxx = sum((v - mx) ** 2 for v in x)
    b = sum((u - mx) * (v - my) for u, v in zip(x, y)) / sxx if sxx else 0.0
    a = my - b * mx
    res = [v - a - b * u for u, v in zip(x, y)]
    return a, b, math.sqrt(sum(r * r for r in res) / (len(x) - 2)) if len(x) > 2 else 0.0


def market_model(stock: list[float], market: list[float]) -> tuple[float, float, float]:
    return ols(market, stock)


def abnormal_return(est_stock, est_market, ev_stock, ev_market) -> dict:
    """Market-model CAR over the event window and its t statistic (MacKinlay 1997)."""
    a, b, sd = market_model(est_stock, est_market)
    ar = [r - (a + b * m) for r, m in zip(ev_stock, ev_market)]
    car = sum(ar)
    return {"alpha": a, "beta": b, "car": car, "t": car / (sd * math.sqrt(len(ar))) if sd else float("nan")}


def variance_ratio(prices: list[float], q: int = 2) -> dict:
    """Lo-MacKinlay VR(q) on log prices, overlapping, bias-corrected, with the
    heteroskedasticity-robust z* (matches arch.unitroot.VarianceRatio(robust=True))."""
    lp = [math.log(p) for p in prices]
    r = [b - a for a, b in zip(lp, lp[1:])]
    n = len(r)
    mu = _mean(r)
    s1 = sum((v - mu) ** 2 for v in r) / (n - 1)
    m = q * (n - q + 1) * (1 - q / n)
    sq = sum((lp[t + q] - lp[t] - q * mu) ** 2 for t in range(n - q + 1)) / m
    vr = sq / s1
    dev2 = [(v - mu) ** 2 for v in r]
    den = sum(dev2) ** 2
    theta = 0.0
    for j in range(1, q):
        delta = n * sum(dev2[t] * dev2[t - j] for t in range(j, n)) / den
        theta += (2 * (q - j) / q) ** 2 * delta
    z = (vr - 1) * math.sqrt(n) / math.sqrt(theta) if theta > 0 else float("nan")  # theta is the asymptotic variance of sqrt(n)(VR-1)
    return {"vr": vr, "z": z, "p": 2 * (1 - N01.cdf(abs(z))) if z == z else float("nan"), "q": q}


# ------------------------------------------------------------------ volatility


def _ohlc(bars):
    return [(b.o, b.h, b.l, b.c) if hasattr(b, "o") else tuple(b) for b in bars]


def parkinson(bars) -> float:
    x = _ohlc(bars)
    return math.sqrt(sum(math.log(h / l) ** 2 for _, h, l, _ in x) / (4 * math.log(2) * len(x)))


def garman_klass(bars) -> float:
    x = _ohlc(bars)
    return math.sqrt(sum(0.5 * math.log(h / l) ** 2 - (2 * math.log(2) - 1) * math.log(c / o) ** 2 for o, h, l, c in x) / len(x))


def rogers_satchell(bars) -> float:
    x = _ohlc(bars)
    return math.sqrt(max(0.0, sum(math.log(h / c) * math.log(h / o) + math.log(l / c) * math.log(l / o) for o, h, l, c in x) / len(x)))


def yang_zhang(bars) -> float:
    """Per-period volatility from n+1 bars (the first bar supplies only its close)."""
    x = _ohlc(bars)
    n = len(x) - 1
    if n < 2:
        raise ValueError("need at least 3 bars")
    on = [math.log(x[i][0] / x[i - 1][3]) for i in range(1, n + 1)]
    oc = [math.log(x[i][3] / x[i][0]) for i in range(1, n + 1)]
    k = 0.34 / (1.34 + (n + 1) / (n - 1))
    rs = rogers_satchell(x[1:]) ** 2
    return math.sqrt(_var(on) + k * _var(oc) + (1 - k) * rs)


def ewma_vol(returns: list[float], lam: float = 0.94) -> float:
    v = returns[0] ** 2
    for r in returns[1:]:
        v = lam * v + (1 - lam) * r * r
    return math.sqrt(v)


def _nelder_mead(f, x0, step=0.1, iters=2000, tol=1e-10):
    n = len(x0)
    pts = [list(x0)] + [[x0[j] + (step if j == i else 0) for j in range(n)] for i in range(n)]
    vals = [f(p) for p in pts]
    for _ in range(iters):
        order = sorted(range(n + 1), key=lambda i: vals[i])
        pts, vals = [pts[i] for i in order], [vals[i] for i in order]
        if abs(vals[-1] - vals[0]) < tol:
            break
        c = [sum(p[j] for p in pts[:-1]) / n for j in range(n)]
        xr = [c[j] + (c[j] - pts[-1][j]) for j in range(n)]
        fr = f(xr)
        if fr < vals[0]:
            xe = [c[j] + 2 * (c[j] - pts[-1][j]) for j in range(n)]
            fe = f(xe)
            pts[-1], vals[-1] = (xe, fe) if fe < fr else (xr, fr)
        elif fr < vals[-2]:
            pts[-1], vals[-1] = xr, fr
        else:
            xc = [c[j] + 0.5 * (pts[-1][j] - c[j]) for j in range(n)]
            fc = f(xc)
            if fc < vals[-1]:
                pts[-1], vals[-1] = xc, fc
            else:
                for i in range(1, n + 1):
                    pts[i] = [pts[0][j] + 0.5 * (pts[i][j] - pts[0][j]) for j in range(n)]
                    vals[i] = f(pts[i])
    i = min(range(n + 1), key=lambda i: vals[i])
    return pts[i], vals[i]


def garch11(returns: list[float]) -> dict:
    """Zero-mean GARCH(1,1), normal likelihood, variance targeting off; returns omega, alpha,
    beta, the next-period vol forecast and the long-run vol."""
    r = list(returns)
    var0 = _var(r, ddof=0)

    def unpack(p):
        a = 1 / (1 + math.exp(-p[1]))
        b = (1 - a) / (1 + math.exp(-p[2]))  # alpha + beta < 1
        return math.exp(p[0]) * var0, a, b

    def nll(p):
        w, a, b = unpack(p)
        h, s = var0, 0.0
        for x in r:
            s += math.log(h) + x * x / h
            h = w + a * x * x + b * h
        return 0.5 * s

    best = None
    for a0, b0 in ((0.05, 0.9), (0.1, 0.85), (0.2, 0.6)):
        p0 = [math.log(var0 * (1 - a0 - b0) / var0), math.log(a0 / (1 - a0)), math.log((b0 / (1 - a0)) / (1 - b0 / (1 - a0)))]
        p, v = _nelder_mead(nll, p0, step=0.5)
        if best is None or v < best[1]:
            best = (p, v)
    w, a, b = unpack(best[0])
    h = var0
    for x in r:
        h = w + a * x * x + b * h
    return {"omega": w, "alpha": a, "beta": b, "persistence": a + b, "next_vol": math.sqrt(h),
            "long_run_vol": math.sqrt(w / (1 - a - b)) if a + b < 1 else float("inf"), "loglik": -best[1] - 0.5 * len(r) * math.log(2 * math.pi)}


def sqrt_impact_bps(shares: float, price: float, adv_usd: float, sigma_daily: float, spread_bps: float = 2.0, y: float = 1.0) -> float:
    """Expected one-way cost of a parent order in basis points: half spread + Y sigma sqrt(Q/ADV)."""
    if adv_usd <= 0:
        return float("inf")
    return spread_bps / 2 + y * sigma_daily * math.sqrt(shares * price / adv_usd) * 1e4


# ------------------------------------------------------------------ covariance and allocation


def _cov(cols: list[list[float]]) -> list[list[float]]:
    n = len(cols[0])
    means = [_mean(c) for c in cols]
    return [[sum((a[t] - ma) * (b[t] - mb) for t in range(n)) / n for b, mb in zip(cols, means)] for a, ma in zip(cols, means)]


def ledoit_wolf(cols: list[list[float]]) -> tuple[list[list[float]], float]:
    """Shrink the sample covariance toward mu*I (Ledoit & Wolf 2004), the same estimator as
    sklearn.covariance.ledoit_wolf (population moments, demeaned data). Returns (cov, shrinkage)."""
    p, n = len(cols), len(cols[0])
    x = [[c[t] - _mean(c) for c in cols] for t in range(n)]  # n x p
    s = _cov(cols)
    mu = sum(s[i][i] for i in range(p)) / p
    d2 = sum((s[i][j] - (mu if i == j else 0.0)) ** 2 for i in range(p) for j in range(p)) / p
    b2 = sum(sum((x[t][i] * x[t][j] - s[i][j]) ** 2 for i in range(p) for j in range(p)) for t in range(n)) / (n * n) / p
    b2 = min(b2, d2)
    shrink = b2 / d2 if d2 else 0.0
    return [[shrink * (mu if i == j else 0.0) + (1 - shrink) * s[i][j] for j in range(p)] for i in range(p)], shrink


def hrp_weights(cols: list[list[float]], names: list[str] | None = None, cov=None) -> dict:
    """Hierarchical Risk Parity (Lopez de Prado 2016): single-linkage clustering on the
    correlation distance sqrt((1-rho)/2), quasi-diagonal ordering, recursive bisection with
    inverse-variance weights inside each half. Matches PyPortfolioOpt's HRPOpt."""
    p = len(cols)
    names = names or [str(i) for i in range(p)]
    s = cov or _cov(cols)
    sd = [math.sqrt(s[i][i]) for i in range(p)]
    corr = [[s[i][j] / (sd[i] * sd[j]) for j in range(p)] for i in range(p)]
    dist = [[math.sqrt(max(0.0, (1 - corr[i][j]) / 2)) for j in range(p)] for i in range(p)]
    dd = dist  # linkage on the correlation distance itself (scipy linkage of the condensed matrix)
    clusters = {i: [i] for i in range(p)}
    order_of = {i: [i] for i in range(p)}
    nxt = p
    while len(clusters) > 1:
        ks = list(clusters)
        a, b = min(((x, y) for i, x in enumerate(ks) for y in ks[i + 1:]),
                   key=lambda xy: (min(dd[u][v] for u in clusters[xy[0]] for v in clusters[xy[1]]), xy))
        clusters[nxt] = clusters.pop(a) + clusters.pop(b)
        order_of[nxt] = order_of[a] + order_of[b]
        nxt += 1
    order = order_of[nxt - 1]

    def cvar(items):
        iv = [1 / s[i][i] for i in items]
        t = sum(iv)
        w = [v / t for v in iv]
        return sum(w[a] * w[b] * s[items[a]][items[b]] for a in range(len(items)) for b in range(len(items)))
    w = {i: 1.0 for i in range(p)}
    stack = [order]
    while stack:
        c = stack.pop()
        if len(c) < 2:
            continue
        left, right = c[: len(c) // 2], c[len(c) // 2:]
        vl, vr = cvar(left), cvar(right)
        alpha = 1 - vl / (vl + vr)
        for i in left:
            w[i] *= alpha
        for i in right:
            w[i] *= 1 - alpha
        stack += [left, right]
    return {names[i]: w[i] for i in range(p)}


# ------------------------------------------------------------------ calibration


def brier(p: list[float], y: list[int]) -> float:
    return sum((a - b) ** 2 for a, b in zip(p, y)) / len(p)


def log_loss(p: list[float], y: list[int], eps: float = 1e-15) -> float:
    return -sum(b * math.log(min(1 - eps, max(eps, a))) + (1 - b) * math.log(min(1 - eps, max(eps, 1 - a))) for a, b in zip(p, y)) / len(p)


def reliability(p: list[float], y: list[int], bins: int = 10) -> dict:
    rows, ece = [], 0.0
    for k in range(bins):
        lo, hi = k / bins, (k + 1) / bins
        idx = [i for i, v in enumerate(p) if lo <= v < hi or (k == bins - 1 and v == 1.0)]
        if not idx:
            continue
        conf, freq = _mean([p[i] for i in idx]), _mean([y[i] for i in idx])
        rows.append({"bin": f"{lo:.1f}-{hi:.1f}", "n": len(idx), "stated": conf, "observed": freq})
        ece += len(idx) / len(p) * abs(conf - freq)
    return {"bins": rows, "ece": ece, "brier": brier(p, y), "base_rate": _mean(y)}


def isotonic_fit(x: list[float], y: list[float]):
    """Pool-adjacent-violators on (x sorted); returns a step function f(new_x) (sklearn-equivalent
    with out_of_bounds='clip', linear interpolation between fitted points)."""
    pts = sorted(zip(x, y))
    blocks = []  # [sum_y, count, x_min, x_max]
    for xi, yi in pts:
        blocks.append([yi, 1, xi, xi])
        while len(blocks) > 1 and blocks[-2][0] / blocks[-2][1] > blocks[-1][0] / blocks[-1][1]:
            s, c, a, b = blocks.pop()
            blocks[-1][0] += s
            blocks[-1][1] += c
            blocks[-1][3] = b
    fx, fy = [], []
    for s, c, a, b in blocks:
        for xv in ((a,) if a == b else (a, b)):
            fx.append(xv)
            fy.append(s / c)

    def f(v):
        if v <= fx[0]:
            return fy[0]
        if v >= fx[-1]:
            return fy[-1]
        for i in range(1, len(fx)):
            if v <= fx[i]:
                if fx[i] == fx[i - 1]:
                    return fy[i]
                return fy[i - 1] + (fy[i] - fy[i - 1]) * (v - fx[i - 1]) / (fx[i] - fx[i - 1])
        return fy[-1]
    return f


# ------------------------------------------------------------------ monitoring


def cusum(x: list[float], target: float, sigma: float, k: float = 0.5, h: float = 5.0) -> dict:
    """One-sided lower CUSUM (Page 1954) for a drop in the mean below `target`.
    S_t = max(0, S_{t-1} + (target - x_t)/sigma - k); alarm when S_t > h. k=0.5, h=5 detects a
    one-sigma drop with an in-control average run length of about 930 observations."""
    st, alarm_at = 0.0, None
    for i, v in enumerate(x):
        st = max(0.0, st + (target - v) / sigma - k)
        if alarm_at is None and st > h:
            alarm_at = i
    return {"statistic": st, "alarm": alarm_at is not None, "alarm_at": alarm_at, "k": k, "h": h}
