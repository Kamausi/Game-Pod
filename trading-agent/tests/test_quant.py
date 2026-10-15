"""Every formula in quant.py against an independent reference: arch, statsmodels, scikit-learn,
PyPortfolioOpt and scipy where installed (tests skip without them), published values otherwise."""
import math
import random

import pytest

from tradebot import quant as q


def gbm_bars(n, sigma, seed=1, gap_sigma=0.0):
    """Daily OHLC from a fine-grained geometric random walk with true per-day vol sigma."""
    rng, px, out = random.Random(seed), 100.0, []
    steps = 200
    for _ in range(n):
        o = px * math.exp(rng.gauss(0, gap_sigma)) if gap_sigma else px
        h = lo = c = o
        for _ in range(steps):
            c *= math.exp(rng.gauss(0, sigma / math.sqrt(steps)))
            h, lo = max(h, c), min(lo, c)
        out.append((o, h, lo, c))
        px = c
    return out


# ------------------------------------------------------------------ Sharpe family


def test_expected_max_sharpe_matches_the_paper():
    assert q.expected_max_sharpe(100, 1.0) == pytest.approx(2.53, abs=0.01)   # marti.ai / paper exhibit: 2.5 empirically
    assert q.expected_max_sharpe(1000, 1.0) == pytest.approx(3.26, abs=0.01)  # quantdare quoting the paper
    assert q.expected_max_sharpe(1, 1.0) == 0.0


def test_expected_max_sharpe_against_simulation():
    rng = random.Random(3)
    sims = [max(rng.gauss(0, 1) for _ in range(50)) for _ in range(4000)]
    assert q.expected_max_sharpe(50, 1.0) == pytest.approx(sum(sims) / len(sims), abs=0.05)


def test_psr_and_dsr_behave():
    rng = random.Random(5)
    good = [rng.gauss(0.002, 0.01) for _ in range(1000)]   # true daily SR 0.2
    noise = [rng.gauss(0.0, 0.01) for _ in range(1000)]
    assert q.psr(good) > 0.99 and 0.02 < q.psr(noise) < 0.98
    d1, d100 = q.dsr(good, 1), q.dsr(good, 100)
    assert d1["dsr"] == pytest.approx(q.psr(good)) and d100["dsr"] < d1["dsr"]
    # trying 1000 noise configurations with a wide spread of Sharpes buries a real one
    assert q.dsr(good, 1000, var_trials_sr=0.01)["dsr"] < 0.5
    assert q.min_track_record(good) < 1000 < q.min_track_record([x - 0.0018 for x in good])


def test_psr_matches_scipy_moments():
    st = pytest.importorskip("scipy.stats")
    rng = random.Random(9)
    x = [rng.lognormvariate(0, 0.5) - 1.1 for _ in range(500)]
    m, s, g3, g4 = q.moments(x)
    assert g3 == pytest.approx(st.skew(x)) and g4 == pytest.approx(st.kurtosis(x, fisher=False))


# ------------------------------------------------------------------ resampling, multiple testing


def test_stationary_bootstrap_ci_matches_arch():
    pytest.importorskip("arch")
    import numpy as np
    from arch.bootstrap import StationaryBootstrap
    rng = random.Random(11)
    x, prev = [], 0.0
    for _ in range(400):  # AR(1): blocks matter
        prev = 0.5 * prev + rng.gauss(0.1, 1)
        x.append(prev)
    lo, hi = q.stationary_bootstrap_ci(x, reps=3000, mean_block=8)
    bs = StationaryBootstrap(8, np.array(x), seed=1)
    ref = bs.conf_int(np.mean, reps=3000, method="percentile").ravel()
    assert (hi - lo) == pytest.approx(ref[1] - ref[0], rel=0.15)
    assert lo < sum(x) / len(x) < hi


def test_bh_fdr_matches_statsmodels():
    mt = pytest.importorskip("statsmodels.stats.multitest")
    p = [0.001, 0.008, 0.039, 0.041, 0.042, 0.06, 0.074, 0.205, 0.212, 0.216]
    rej, adj = q.bh_fdr(p, 0.05)
    r2, a2, _, _ = mt.multipletests(p, alpha=0.05, method="fdr_bh")
    assert rej == list(r2) and adj == pytest.approx(list(a2))


def test_pbo_low_for_a_real_edge_and_about_half_for_noise():
    rng = random.Random(13)
    noise = [[rng.gauss(0, 1) for _ in range(20)] for _ in range(400)]
    assert 0.25 < q.pbo_cscv(noise, s=8)["pbo"] < 0.8
    edge = [[rng.gauss(0.3 if j == 0 else 0, 1) for j in range(20)] for _ in range(400)]
    assert q.pbo_cscv(edge, s=8)["pbo"] < 0.1


# ------------------------------------------------------------------ event study, variance ratio


def test_market_model_matches_statsmodels_ols():
    sm = pytest.importorskip("statsmodels.api")
    rng = random.Random(17)
    m = [rng.gauss(0, 0.01) for _ in range(120)]
    s = [0.0005 + 1.3 * v + rng.gauss(0, 0.01) for v in m]
    a, b, sd = q.market_model(s, m)
    fit = sm.OLS(s, sm.add_constant(m)).fit()
    assert (a, b) == pytest.approx(tuple(fit.params)) and sd == pytest.approx(math.sqrt(fit.scale))
    ev = q.abnormal_return(s, m, [0.05 + 1.3 * 0.01] * 5, [0.01] * 5)
    assert ev["car"] == pytest.approx(5 * (0.05 + 1.3 * 0.01 - (a + b * 0.01))) and ev["t"] > 5


def test_variance_ratio_matches_arch():
    pytest.importorskip("arch")
    import numpy as np
    from arch.unitroot import VarianceRatio
    rng, p = random.Random(19), [100.0]
    for _ in range(999):
        p.append(p[-1] * math.exp(rng.gauss(0, 0.01)))
    for lags in (2, 5):
        ours = q.variance_ratio(p, lags)
        ref = VarianceRatio(np.log(p), lags=lags, robust=True, debiased=True, overlap=True)
        assert ours["vr"] == pytest.approx(ref.vr, rel=1e-6) and ours["z"] == pytest.approx(ref.stat, rel=1e-6)


# ------------------------------------------------------------------ volatility


def test_range_estimators_recover_true_vol_and_yang_zhang_handles_gaps():
    bars = gbm_bars(400, 0.02)
    for f in (q.parkinson, q.garman_klass, q.rogers_satchell):
        assert f(bars) == pytest.approx(0.02, rel=0.1), f.__name__
    gapped = gbm_bars(400, 0.02, gap_sigma=0.01)  # total daily vol sqrt(0.02^2 + 0.01^2)
    assert q.yang_zhang(gapped) == pytest.approx(math.hypot(0.02, 0.01), rel=0.1)
    assert q.rogers_satchell(gapped) < 0.9 * math.hypot(0.02, 0.01)  # ignores the gap: why YZ exists


def test_yang_zhang_is_more_efficient_than_close_to_close():
    yz, cc = [], []
    for seed in range(40):
        b = gbm_bars(30, 0.02, seed=seed, gap_sigma=0.005)
        yz.append(q.yang_zhang(b))
        r = [math.log(b[i][3] / b[i - 1][3]) for i in range(1, len(b))]
        cc.append(math.sqrt(sum(v * v for v in r) / (len(r) - 1)))
    spread = lambda xs: math.sqrt(sum((v - sum(xs) / len(xs)) ** 2 for v in xs) / len(xs))
    assert spread(yz) < 0.6 * spread(cc)


def test_garch_matches_arch():
    pytest.importorskip("arch")
    import numpy as np
    from arch import arch_model
    rng, h, r = random.Random(23), 1e-4, []
    for _ in range(1500):
        x = rng.gauss(0, math.sqrt(h))
        r.append(x)
        h = 2e-6 + 0.08 * x * x + 0.9 * h
    ours = q.garch11(r)
    ref = arch_model(np.array(r) * 100, mean="Zero", vol="GARCH", p=1, q=1, dist="normal").fit(disp="off")
    assert ours["alpha"] == pytest.approx(ref.params["alpha[1]"], abs=0.02)
    assert ours["beta"] == pytest.approx(ref.params["beta[1]"], abs=0.03)
    nxt = math.sqrt(ref.forecast(horizon=1).variance.values[-1, 0]) / 100
    assert ours["next_vol"] == pytest.approx(nxt, rel=0.03)


def test_sqrt_impact():
    assert q.sqrt_impact_bps(1000, 50, 20e6, 0.02) == pytest.approx(1 + 0.02 * math.sqrt(50_000 / 20e6) * 1e4)
    assert q.sqrt_impact_bps(4000, 50, 20e6, 0.02) - 1 == pytest.approx(2 * (q.sqrt_impact_bps(1000, 50, 20e6, 0.02) - 1))


# ------------------------------------------------------------------ covariance and allocation


def _returns(seed=29, n=300):
    rng = random.Random(seed)
    out = [[], [], [], [], []]
    for _ in range(n):
        f = rng.gauss(0, 0.01)
        out[0].append(f + rng.gauss(0, 0.005))
        out[1].append(f + rng.gauss(0, 0.006))
        out[2].append(rng.gauss(0, 0.02))
        out[3].append(-0.5 * f + rng.gauss(0, 0.01))
        out[4].append(rng.gauss(0, 0.004))
    return out


def test_ledoit_wolf_matches_sklearn():
    pytest.importorskip("sklearn")
    import numpy as np
    from sklearn.covariance import ledoit_wolf
    cols = _returns()
    cov, shrink = q.ledoit_wolf(cols)
    ref, rs = ledoit_wolf(np.array(cols).T)
    assert shrink == pytest.approx(rs, rel=1e-6) and sum(cov, []) == pytest.approx(ref.ravel().tolist(), rel=1e-6)


def test_hrp_matches_pypfopt():
    pytest.importorskip("pypfopt")
    import pandas as pd
    from pypfopt import HRPOpt
    cols, names = _returns(), list("ABCDE")
    import scipy.cluster.hierarchy as sch
    import scipy.spatial.distance as ssd
    import numpy as np
    df = pd.DataFrame(dict(zip(names, cols)))
    # HRPOpt.optimize() itself calls a scipy attribute removed in scipy 1.18; run its own steps
    dist = ssd.squareform(np.sqrt(np.clip((1.0 - df.corr()) / 2.0, 0.0, 1.0)), checks=False)
    order = df.columns[HRPOpt._get_quasi_diag(sch.linkage(dist, "single"))].tolist()
    ref = HRPOpt._raw_hrp_allocation(df.cov(), order)
    ours = q.hrp_weights(cols, names)
    assert ours == pytest.approx(ref.to_dict(), rel=1e-6) and sum(ours.values()) == pytest.approx(1)


# ------------------------------------------------------------------ calibration and monitoring


def test_calibration_metrics_match_sklearn():
    pytest.importorskip("sklearn")
    from sklearn.isotonic import IsotonicRegression
    from sklearn.metrics import brier_score_loss, log_loss
    rng = random.Random(31)
    p = [rng.random() for _ in range(500)]
    y = [1 if rng.random() < min(1, v * 1.3) else 0 for v in p]  # under-confident forecaster
    assert q.brier(p, y) == pytest.approx(brier_score_loss(y, p)) and q.log_loss(p, y) == pytest.approx(log_loss(y, p))
    f = q.isotonic_fit(p, y)
    ir = IsotonicRegression(out_of_bounds="clip").fit(p, y)
    grid = [i / 50 for i in range(51)]
    assert [f(v) for v in grid] == pytest.approx(list(ir.predict(grid)), abs=1e-9)
    rel = q.reliability(p, y)
    assert rel["ece"] > 0.05 and q.reliability([f(v) for v in p], y)["ece"] < rel["ece"]


def test_cusum_flags_a_drop_but_not_noise():
    rng = random.Random(37)
    same = [rng.gauss(0.3, 1) for _ in range(200)]
    worse = [rng.gauss(0.3, 1) for _ in range(30)] + [rng.gauss(-0.7, 1) for _ in range(60)]
    assert not q.cusum(same, 0.3, 1.0)["alarm"]
    out = q.cusum(worse, 0.3, 1.0)
    assert out["alarm"] and 30 <= out["alarm_at"] < 60


def test_the_research_workflow_uses_the_same_checked_dsr():
    from tradebot import validation
    rng = random.Random(41)
    x = [rng.gauss(0.001, 0.01) for _ in range(800)]
    _, _, g3, g4 = q.moments(x)
    assert validation.deflated_sharpe(q.sharpe(x), len(x), g3, g4, 50, 0.002) == pytest.approx(q.dsr(x, 50, 0.002)["dsr"])
