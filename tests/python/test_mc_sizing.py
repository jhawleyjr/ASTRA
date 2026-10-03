"""astra.mc_sizing and ``astra mc size`` (M6 PR 7, docs/plans/m6-pr7-mc-sizing.md; M6 plan R11, R12
and R15's mc size case; C-09 and N-04).

Every expected value is computed independently of the module: from scipy's ``binomtest``, ``binom``
and ``t``, from stdlib ``statistics`` and ``fractions``, from closed forms, or by loops in the test.
Synthetic draws use ``astra.montecarlo.Rng`` with stated seeds (numpy's generators are unpinned).
The coverage tests (D7) use K = 1047 seeded batches of n = 100 at C = 0.95 and pass at 978 or
more, which fails an interval whose true coverage is 0.92 with probability 0.95 and a correct one
with probability 0.0096. The one real batch, PR 6's descent batch, is built once per module and
removed by a finalizer that also runs on failure.
"""

from __future__ import annotations

import dataclasses
import itertools
import math
import shutil
import statistics
from collections.abc import Callable, Iterator
from fractions import Fraction
from pathlib import Path
from typing import Any, cast

import numpy as np
import pyarrow as pa  # pyright: ignore[reportMissingTypeStubs]
import pyarrow.parquet as pq  # pyright: ignore[reportMissingTypeStubs]
import pytest
import scipy.stats  # pyright: ignore[reportMissingTypeStubs]

from astra import cli, mc_analysis, mc_sizing, montecarlo
from astra.mc_sizing import SizingError

REPO = Path(__file__).resolve().parents[2]
DESCENT_SPEC = REPO / "tests" / "python" / "mc_analysis_fixtures" / "descent_batch.yaml"
CLAIMS = REPO / "tests" / "python" / "mc_sizing_fixtures" / "claims.yaml"
F = Fraction
stats: Any = scipy.stats

K = 1047  # D7: detect 0.92 against 0.95 with power 0.95 and a 1 % false-fail rate
PASS = 978
N = 100
C = 0.95


# --- helpers --------------------------------------------------------------------------------------


@pytest.fixture(scope="module")
def descent(tmp_path_factory: pytest.TempPathFactory) -> Iterator[Path]:
    """PR 6's descent batch (24 runs: ok, halt and error rows), removed afterwards, failures too."""
    root = tmp_path_factory.mktemp("mc_sizing_descent")
    try:
        out = root / "batch"
        montecarlo.run(montecarlo.load(DESCENT_SPEC, root=REPO), out, workers=4)
        yield out
    finally:
        shutil.rmtree(root, ignore_errors=True)


def _synthetic(
    columns: dict[str, list[Any]], parameters: tuple[str, ...] = ()
) -> mc_analysis.Batch:
    n = len(next(iter(columns.values())))
    base: dict[str, list[Any]] = {"run": list(range(n))}
    if "status" not in columns:
        base["status"] = ["ok"] * n
    return mc_analysis.from_columns({**base, **columns}, parameters)


def _outcome_batch(k: int, n: int, errors: int = 0) -> mc_analysis.Batch:
    """k halts among n analyzed rows, then ``errors`` error rows (default criterion)."""
    return _synthetic({"status": ["halt"] * k + ["ok"] * (n - k) + ["error"] * errors})


def _metric_batch(values: list[float], name: str = "x") -> mc_analysis.Batch:
    return _synthetic({name: values})


def _cp_less(k: int, n: int, level: float = C) -> float:
    """One-sided exact upper bound, by scipy's binomtest."""
    return float(stats.binomtest(k, n, alternative="less").proportion_ci(level, "exact").high)


def _cp_greater(k: int, n: int, level: float = C) -> float:
    return float(stats.binomtest(k, n, alternative="greater").proportion_ci(level, "exact").low)


def _cp_two(k: int, n: int, level: float = C) -> tuple[float, float]:
    ci = stats.binomtest(k, n).proportion_ci(level, "exact")
    return float(ci.low), float(ci.high)


def _normal(rng: montecarlo.Rng, n: int) -> np.ndarray:
    return np.array([rng.normal() for _ in range(n)])


def _exponential(rng: montecarlo.Rng, n: int) -> np.ndarray:
    return np.array([-math.log(1.0 - rng.uniform01()) for _ in range(n)])


def _t5(rng: montecarlo.Rng, n: int) -> np.ndarray:
    out = []
    for _ in range(n):
        z = rng.normal()
        chi = sum(rng.normal() ** 2 for _ in range(5))
        out.append(z / math.sqrt(chi / 5))
    return np.array(out)


def _rare(rng: montecarlo.Rng, n: int) -> np.ndarray:
    out = []
    for _ in range(n):
        magnitude = 100.0 if rng.uniform01() < 0.01 else 1.0
        out.append(magnitude if rng.uniform01() < 0.5 else -magnitude)
    return np.array(out)


def _covered(trial: Callable[[montecarlo.Rng], bool], seed: int) -> int:
    return sum(bool(trial(montecarlo.Rng(montecarlo.run_seed(seed, k)))) for k in range(K))


def _bonett(x: np.ndarray, level: float = C) -> tuple[float, float]:
    """Bonett (2006), written out independently."""
    n = x.size
    z = statistics.NormalDist().inv_cdf((1 + level) / 2)
    cut = math.floor(n / (2 * math.sqrt(n - 4)))
    trimmed = float(np.sort(x)[cut : n - cut].mean())
    ss = float(((x - x.mean()) ** 2).sum())
    g4 = n * float(((x - trimmed) ** 4).sum()) / ss**2
    c = n / (n - z)
    se = c * math.sqrt((g4 - (n - 3) / n) / (n - 1))
    s2 = ss / (n - 1)
    return math.sqrt(c * s2 * math.exp(-z * se)), math.sqrt(c * s2 * math.exp(z * se))


def _jackknife(x: np.ndarray, y: np.ndarray) -> tuple[float, float]:
    """Spearman's rho and the jackknife variance of atanh(rho), by an explicit loop."""
    rho = float(stats.spearmanr(x, y).statistic)
    z = np.array(
        [
            math.atanh(float(stats.spearmanr(np.delete(x, i), np.delete(y, i)).statistic))
            for i in range(x.size)
        ]
    )
    n = x.size
    return rho, (n - 1) / n * float(((z - z.mean()) ** 2).sum())


def _expected_half_width(n: int, p0: float, level: float = C) -> float:
    """``max(U - p0, p0 - L)`` of the Clopper-Pearson bounds at the real count ``k = n p0``, as
    beta quantiles, written out here independently of the module (review A-004)."""
    k = n * p0
    tail = (1 - level) / 2
    lower = 0.0 if k == 0 else float(stats.beta.ppf(tail, k, n - k + 1))
    upper = 1.0 if k == n else float(stats.beta.ppf(1 - tail, k + 1, n - k))
    return max(upper - p0, p0 - lower)


def _abs_bounds(lower: float, upper: float) -> tuple[float, float]:
    spans = lower <= 0.0 <= upper
    return (0.0 if spans else min(abs(lower), abs(upper))), max(abs(lower), abs(upper))


# --- R11: formula mode ----------------------------------------------------------------------------


def test_mc_sizing_r11a_probability_runs_meets_and_n_minus_1_does_not() -> None:
    assert mc_sizing.probability_runs(0.01, 0.95) == 299
    for p, level in (("0.01", "0.95"), ("0.05", "0.9"), ("0.001", "0.99"), ("0.2", "0.5")):
        n = mc_sizing.probability_runs(p, level)
        assert (1 - F(p)) ** n <= 1 - F(level) < (1 - F(p)) ** (n - 1)
        # The same question as one-sided first-order Wilks at coverage 1 - p.
        assert n == mc_sizing.percentile_runs(1 - F(p), level)


def test_mc_sizing_r11b_percentile_runs_match_wilks() -> None:
    assert mc_sizing.percentile_runs(0.95, 0.95) == 59
    assert mc_sizing.percentile_runs(0.95, 0.95, two_sided=True) == 93
    assert mc_sizing.percentile_runs(0.95, 0.95, order=2) == 93
    assert mc_sizing.percentile_runs(0.95, 0.95, order=3) == 124
    assert mc_sizing.percentile_runs(0.95, 0.95, two_sided=True, order=2) == 153
    g, level = F(95, 100), F(95, 100)
    one = 59
    assert 1 - g**one >= level > 1 - g ** (one - 1)
    two = 93

    def two_sided(n: int) -> Fraction:
        return 1 - g**n - n * (1 - g) * g ** (n - 1)

    assert two_sided(two) >= level > two_sided(two - 1)


def test_mc_sizing_r11c_clopper_pearson_equals_binomtest() -> None:
    cases = [(100, 0), (100, 5), (299, 0), (1000, 20)]
    rng = montecarlo.Rng(711)
    for _ in range(40):
        n = 1 + rng.bits53() % 500
        cases.append((n, rng.bits53() % (n + 1)))
    for n, k in cases:
        lower, upper = mc_sizing.clopper_pearson(k, n, 0.95)
        expected = _cp_two(k, n)
        assert abs(lower - expected[0]) <= 1e-12, (n, k)
        assert abs(upper - expected[1]) <= 1e-12, (n, k)


def test_mc_sizing_r11c_precision_runs_closed_form_monotone_and_minimal() -> None:
    # p0 = 0: U = 1 - ((1 - C)/2)^(1/n), so n = ceil(ln((1 - C)/2) / ln(1 - h)).
    assert mc_sizing.precision_runs(0.0, 0.03, 0.95) == math.ceil(math.log(0.025) / math.log(0.97))
    assert mc_sizing.precision_runs(0.0, 0.03, 0.95) == 122
    for p0, h in ((0.05, 0.02), (0.3, 0.05), (0.5, 0.1), (0.9, 0.04)):
        n = mc_sizing.precision_runs(p0, h)
        # Review A-004: the objective is recomputed here, not through the module.
        assert _expected_half_width(n, p0) <= h < _expected_half_width(n - 1, p0)
        widths = [_expected_half_width(size, p0) for size in range(1, 400)]
        assert all(b <= a + 1e-15 for a, b in itertools.pairwise(widths))
        assert all(
            abs(mc_sizing.half_width_at(size, p0) - _expected_half_width(size, p0)) <= 1e-12
            for size in (1, 7, 50, 399)
        )


def test_mc_sizing_r11c_change4_the_observed_count_can_exceed_the_half_width() -> None:
    assert mc_sizing.precision_runs(0.05, 0.02) == 632
    for n in range(632, 636):
        k = round(n * 0.05)
        low, high = _cp_two(k, n)
        estimate = k / n
        assert max(high - estimate, estimate - low) > 0.02, n


def test_mc_sizing_r11d_ranking_runs_fisher_z() -> None:
    assert mc_sizing.ranking_runs(0.3, 1) == 85  # Cohen's table
    assert mc_sizing.ranking_runs(0.3, 4) == 120
    previous = 0
    normal = statistics.NormalDist()
    for m in range(1, 11):
        z = normal.inv_cdf(1 - 0.05 / (2 * m)) + normal.inv_cdf(0.8)
        expected = math.ceil((z / math.atanh(0.3)) ** 2 + 3)
        n = mc_sizing.ranking_runs(0.3, m)
        assert n == expected
        assert n >= previous
        assert mc_sizing.ranking_runs(-0.3, m) == n
        previous = n
    for bad in (0, 0.0, 1, -1, 1.5, math.nan, "x"):
        with pytest.raises(SizingError, match="r"):
            mc_sizing.ranking_runs(bad, 4)  # pyright: ignore[reportArgumentType]


def test_mc_sizing_refusals_name_the_argument(monkeypatch: pytest.MonkeyPatch) -> None:
    with pytest.raises(SizingError, match="p_max"):
        mc_sizing.probability_runs(1.0)
    with pytest.raises(SizingError, match="confidence"):
        mc_sizing.probability_runs(0.01, 0.0)
    with pytest.raises(SizingError, match="p_max"):
        mc_sizing.probability_runs(True)  # pyright: ignore[reportArgumentType]
    with pytest.raises(SizingError, match="p_max"):
        mc_sizing.probability_runs(math.nan)
    with pytest.raises(SizingError, match="order"):
        mc_sizing.percentile_runs(0.9, order=0)
    with pytest.raises(SizingError, match="half_width"):
        mc_sizing.precision_runs(0.1, 0.5)
    with pytest.raises(SizingError, match="parameters"):
        mc_sizing.ranking_runs(0.3, 0)
    with pytest.raises(SizingError, match="not a decimal"):
        mc_sizing.exact("p", "1/2")
    monkeypatch.setattr(mc_sizing, "MAX_RUNS", 5000)
    with pytest.raises(SizingError, match="needs more than 5000"):
        mc_sizing.precision_runs(0.5, 0.005)


# --- D2: one non-strict convention, decided exactly -----------------------------------------------


def test_mc_sizing_d2_exact_rational_boundaries() -> None:
    # (1/2)^2 = 1/4 = 1 - 3/4: n = 2 meets exactly, and k = 0 of 2 is met at p_max = 1/2.
    assert mc_sizing.probability_runs("0.5", "0.75") == 2
    claim = mc_sizing.ProbabilityClaim("0.5", confidence="0.75")
    assert mc_sizing.check_one(_outcome_batch(0, 2), claim).verdict == "met"
    # k = 2 of 2: L = 1/2 at C = 3/4, not strictly above p_max = 1/2.
    assert mc_sizing.check_one(_outcome_batch(2, 2), claim).verdict == "undetermined"
    assert (
        mc_sizing.check_one(
            _outcome_batch(2, 2), mc_sizing.ProbabilityClaim("0.49", confidence="0.75")
        ).verdict
        == "violated"
    )
    # P(Bin(3, 1/2) <= 0) = 1/8 = 1 - 7/8; P(Bin(4, 1/2) <= 1) = 5/16 = 1 - 11/16.
    half = F(1, 2)
    assert mc_sizing.upper_meets(0, 3, half, F(1, 8))
    assert not mc_sizing.upper_meets(0, 3, half, F(1, 8) - F(1, 10**12))
    assert mc_sizing.upper_meets(1, 4, half, F(5, 16))
    # P(Bin(3, 1/2) >= 3) = 1/8: not < 1/8, so not violated; at p = 0.49 it is.
    assert not mc_sizing.lower_exceeds(3, 3, half, F(1, 8))
    assert mc_sizing.lower_exceeds(3, 3, F(49, 100), F(1, 8))


def test_mc_sizing_v1_inputs_as_written_api_and_cli_agree_at_gamma_0_9(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    # 0.9^2 = 0.81 = 1 - 0.19 exactly; a float complement 1 - 0.9 would make it 3.
    assert mc_sizing.percentile_runs(0.9, 0.19) == 2
    assert mc_sizing.exact("g", 0.9) == F(9, 10)  # the shortest repr, never Fraction(0.9)
    assert mc_sizing.exact("g", 0.9) != F(0.9)
    claim = mc_sizing.PercentileClaim("x", 0.9, 1.0, confidence=0.19)
    api = mc_sizing.check_one(_metric_batch([0.25, 0.5]), claim)
    assert api.verdict == "met"
    path = tmp_path / "batch"
    path.mkdir()
    table = pa.table(
        {
            "run": pa.array([0, 1], pa.int64()),
            "status": pa.array(["ok", "ok"]),
            "error_type": pa.array([None, None], pa.string()),
            "x": pa.array([0.25, 0.5], pa.float64()),
        }
    )
    pq.write_table(table, path / montecarlo.SUMMARY)
    argv = ["mc", "size", "percentile", "--quantile", "0.9", "--confidence", "0.19"]
    assert cli.main(argv) == 0
    assert "runs: 2" in capsys.readouterr().out
    argv += ["--metric", "x", "--limit", "1.0", "--batch", str(path)]
    assert cli.main(argv) == 0
    assert "verdict: met" in capsys.readouterr().out


# --- D3: probability ------------------------------------------------------------------------------


def test_mc_sizing_d3_probability_verdicts_and_bounds() -> None:
    for (n, k, p_max), verdict in (
        ((299, 0, "0.01"), "met"),
        ((100, 5, "0.01"), "violated"),
        ((100, 1, "0.03"), "undetermined"),
    ):
        check = mc_sizing.check_one(
            _outcome_batch(k, n, errors=3), mc_sizing.ProbabilityClaim(p_max)
        )
        assert check.verdict == verdict, (n, k, p_max)
        assert check.n == n
        assert check.errors == 3
        assert check.tier == "calibrated"
        assert abs(check.upper - _cp_less(k, n)) <= 1e-12
        assert abs(check.lower - (_cp_greater(k, n) if k else 0.0)) <= 1e-12
        assert check.estimate == k / n


def _brute_producer(p_max: float, level: float, rate: float, risk: float) -> int:
    """The producer-risk N by a plain scan: k* by binomtest, the probability by binom.cdf."""
    n = 0
    while True:
        n += 1
        k_star = -1
        while k_star + 1 <= n and _cp_less(k_star + 1, n, level) <= p_max:
            k_star += 1
        if k_star >= 0 and float(stats.binom.cdf(k_star, n, rate)) >= 1 - risk:
            return n


def test_mc_sizing_d3_producer_risk_against_a_brute_force_scan() -> None:
    assert mc_sizing.producer_runs(F(0), 0.01) == 299
    for p_max, level, rate, risk in (
        (0.05, 0.95, F(1, 100), 0.1),
        (0.1, 0.9, F(3, 100), 0.2),
        (0.2, 0.8, F(1, 10), 0.05),
        (0.3, 0.95, F(1, 10), 0.1),
    ):
        expected = _brute_producer(p_max, level, float(rate), risk)
        assert mc_sizing.producer_runs(rate, p_max, level, risk) == expected, (p_max, rate)
    assert mc_sizing.producer_runs(F(1, 50), 0.01) is None
    check = mc_sizing.check_one(_outcome_batch(5, 100), mc_sizing.ProbabilityClaim("0.01"))
    assert check.needed is None
    assert any("not below" in note for note in check.notes)


# --- D4: percentile as exceedance -----------------------------------------------------------------


def test_mc_sizing_d4_exceedance_sides_ties_and_violation_below_the_minimum() -> None:
    # A value equal to the limit is not an exceedance: an atom at the limit, upper side.
    at_limit = _metric_batch([1.0] * 59)
    upper = mc_sizing.check_one(at_limit, mc_sizing.PercentileClaim("x", 0.95, 1.0))
    assert upper.estimate == 0.0
    assert upper.verdict == "met"  # 59 runs, no exceedance: the 95/95 Wilks case
    lower = mc_sizing.check_one(at_limit, mc_sizing.PercentileClaim("x", 0.95, 1.0, "lower"))
    assert lower.estimate == 0.0
    below = mc_sizing.check_one(at_limit, mc_sizing.PercentileClaim("x", 0.95, 1.5, "lower"))
    assert below.estimate == 1.0
    assert below.verdict == "violated"
    # gamma = C = 0.95, ten values all above the limit: violated below the Wilks minimum of 59.
    ten = mc_sizing.check_one(_metric_batch([2.0] * 10), mc_sizing.PercentileClaim("x", 0.95, 1.0))
    assert ten.verdict == "violated"
    assert ten.minimum == 59
    assert abs(ten.lower - _cp_greater(10, 10)) <= 1e-12
    assert any("unreachable below 59" in note for note in ten.notes)


def test_mc_sizing_d4_exceedance_is_the_quantile_claim_on_a_discrete_population() -> None:
    """``F(L) >= gamma`` iff ``P(X > L) <= 1 - gamma``, with ``q_gamma = inf{x: F(x) >= gamma}``,
    on discrete populations with atoms, checked by enumeration (the module's stated convention)."""
    populations = [
        {0.0: F(1, 2), 1.0: F(1, 2)},
        {0.0: F(9, 10), 5.0: F(1, 10)},
        {-1.0: F(1, 4), 0.0: F(1, 4), 2.0: F(1, 2)},
    ]
    for population in populations:
        atoms = sorted(population)
        for gamma in (F(1, 2), F(9, 10), F(3, 4)):
            cumulative = F(0)
            q = atoms[-1]
            for atom in atoms:
                cumulative += population[atom]
                if cumulative >= gamma:
                    q = atom
                    break
            for limit in (*atoms, -0.5, 0.5, 1.5, 3.0):
                exceed = sum(prob for atom, prob in population.items() if atom > limit)
                assert (q <= limit) == (exceed <= 1 - gamma), (population, gamma, limit)


def test_mc_sizing_d4_printed_order_statistic_matches_a_binomial_search() -> None:
    rng = montecarlo.Rng(712)
    values = _normal(rng, 80)
    check = mc_sizing.check_one(
        _metric_batch(values.tolist()), mc_sizing.PercentileClaim("x", 0.9, 5.0)
    )
    r = next(r for r in range(1, 81) if float(stats.binom.cdf(r - 1, 80, 0.9)) >= 0.95)
    expected = float(np.sort(values)[r - 1])
    assert any(f"X_({r}) = {expected:.6g}" in note for note in check.notes)


# --- endpoint oracles (finding 11) ----------------------------------------------------------------


def test_mc_sizing_endpoint_mean_t_interval() -> None:
    values = [1.0, 2.0, 4.0, 7.0, 11.0]
    check = mc_sizing.check_one(
        _metric_batch(values), mc_sizing.PrecisionClaim("mean", 1.0, metric="x")
    )
    mean = statistics.fmean(values)
    hw = float(stats.t.ppf(0.975, 4)) * statistics.stdev(values) / math.sqrt(5)
    assert check.tier == "nominal"
    assert check.verdict is None
    assert abs(check.lower - (mean - hw)) <= 1e-12
    assert abs(check.upper - (mean + hw)) <= 1e-12
    assert check.needed == math.ceil(5 * (hw / 1.0) ** 2)
    assert mc_sizing.NOMINAL in check.text()


def test_mc_sizing_endpoint_std_bonett_flags_and_bootstrap() -> None:
    rng = montecarlo.Rng(5)
    values = _normal(rng, 100)
    claim = mc_sizing.PrecisionClaim("std", 0.05, metric="x")
    check = mc_sizing.check_one(_metric_batch(values.tolist()), claim)
    lower, upper = _bonett(values)
    assert abs(check.lower - lower) <= 1e-12
    assert abs(check.upper - upper) <= 1e-12
    assert any("bootstrap half-width" in note for note in check.notes)
    assert not any("flag:" in note for note in check.notes)
    outlier = values.copy()
    outlier[0] = 12.0
    flagged = mc_sizing.check_one(_metric_batch(outlier.tolist()), claim)
    assert any("flag: bootstrap disagrees" in note for note in flagged.notes)
    assert any("flag: heavy-tailed" in note for note in flagged.notes)
    assert any("flag: skewed" in note for note in flagged.notes)
    short = mc_sizing.check_one(_metric_batch(values[:99].tolist()), claim)
    assert short.needed is None
    assert any("needs 100" in note for note in short.notes)


def test_mc_sizing_endpoint_percentile_interval_and_zero_width() -> None:
    rng = montecarlo.Rng(713)
    values = _exponential(rng, 60)
    check = mc_sizing.check_one(
        _metric_batch(values.tolist()),
        mc_sizing.PrecisionClaim("percentile", 0.1, metric="x", quantile=0.5),
    )
    lo = max(r for r in range(1, 61) if float(stats.binom.cdf(r - 1, 60, 0.5)) <= 0.025)
    hi = min(r for r in range(1, 61) if float(stats.binom.cdf(r - 1, 60, 0.5)) >= 0.975)
    ordered = np.sort(values)
    assert check.lower == float(ordered[lo - 1])
    assert check.upper == float(ordered[hi - 1])
    assert check.estimate == float(ordered[29])
    atom = mc_sizing.check_one(
        _metric_batch([3.0] * 60),
        mc_sizing.PrecisionClaim("percentile", 0.1, metric="x", quantile=0.5),
    )
    assert atom.needed is None
    assert any("zero-width" in note for note in atom.notes)


def test_mc_sizing_endpoint_probability_two_sided() -> None:
    check = mc_sizing.check_one(
        _outcome_batch(7, 90), mc_sizing.PrecisionClaim("probability", 0.02)
    )
    expected = _cp_two(7, 90)
    assert abs(check.lower - expected[0]) <= 1e-12
    assert abs(check.upper - expected[1]) <= 1e-12
    rate = 7 / 90
    expected = next(n for n in itertools.count(1) if _expected_half_width(n, rate) <= 0.02)
    assert check.needed == expected  # review A-004: an independent search


def _correlated(seed: int, n: int, rho: float) -> tuple[np.ndarray, np.ndarray]:
    rng = montecarlo.Rng(seed)
    x = _normal(rng, n)
    e = _normal(rng, n)
    return x, rho * x + math.sqrt(1 - rho * rho) * e


def test_mc_sizing_endpoint_jackknife_correlation_and_ranking_bounds() -> None:
    x1, y = _correlated(714, 60, 0.6)
    rng = montecarlo.Rng(715)
    x2 = _normal(rng, 60)
    x3 = np.round(_normal(rng, 60))  # ties
    batch = _synthetic(
        {"p1": x1.tolist(), "p2": x2.tolist(), "p3": x3.tolist(), "y": y.tolist()},
        ("p1", "p2", "p3"),
    )
    precision = mc_sizing.check_one(
        batch, mc_sizing.PrecisionClaim("correlation", 0.05, metric="y", parameter="p1")
    )
    rho, v = _jackknife(x1, y)
    z = statistics.NormalDist().inv_cdf(0.975)
    assert abs(precision.estimate - rho) <= 1e-12
    assert abs(precision.lower - math.tanh(math.atanh(rho) - z * math.sqrt(v))) <= 1e-12
    assert abs(precision.upper - math.tanh(math.atanh(rho) + z * math.sqrt(v))) <= 1e-12
    ranking = mc_sizing.check_one(batch, mc_sizing.RankingClaim("y"))
    c = statistics.NormalDist().inv_cdf(1 - 0.05 / 6)
    for row, column in zip(ranking.parameters, (x1, x2, x3), strict=True):
        rho, v = _jackknife(column, y)
        lower = math.tanh(math.atanh(rho) - c * math.sqrt(v))
        upper = math.tanh(math.atanh(rho) + c * math.sqrt(v))
        assert abs(row.rho - rho) <= 1e-12
        assert abs(row.lower - lower) <= 1e-12
        assert abs(row.upper - upper) <= 1e-12
        abs_lower, abs_upper = _abs_bounds(lower, upper)
        assert abs(row.abs_lower - abs_lower) <= 1e-12
        assert abs(row.abs_upper - abs_upper) <= 1e-12
    assert any(row.lower <= 0.0 <= row.upper and row.abs_lower == 0.0 for row in ranking.parameters)
    assert ranking.verdict is None


def test_mc_sizing_endpoint_ranking_refuses_degenerate_cases() -> None:
    x = [float(i) for i in range(40)]
    perfect = _synthetic({"p": x, "y": [2.0 * v for v in x]}, ("p",))
    row = mc_sizing.check_one(perfect, mc_sizing.RankingClaim("y")).parameters[0]
    assert math.isnan(row.lower)
    assert "|rho| = 1" in row.note
    swapped = [100.0, *x[1:]]  # leaving out point 0 gives a perfect ranking
    loo = _synthetic({"p": x, "y": swapped}, ("p",))
    row = mc_sizing.check_one(loo, mc_sizing.RankingClaim("y")).parameters[0]
    assert "leave-one-out" in row.note
    few = _synthetic({"p": x[:29], "y": [math.sin(v) for v in x[:29]]}, ("p",))
    check = mc_sizing.check_one(few, mc_sizing.RankingClaim("y"))
    assert "30 runs needed" in check.parameters[0].note
    assert check.needed is None
    constant = _synthetic({"p": [1.0] * 40, "q": x, "y": [math.sin(v) for v in x]}, ("p", "q"))
    check = mc_sizing.check_one(constant, mc_sizing.RankingClaim("y"))
    assert [row.parameter for row in check.parameters] == ["q"]
    empty = _synthetic({"p": [1.0] * 40, "y": x}, ("p",))
    check = mc_sizing.check_one(empty, mc_sizing.RankingClaim("y"))
    assert any("nothing to rank" in note for note in check.notes)


# --- minimality (finding 11) ----------------------------------------------------------------------


def test_mc_sizing_minimality_of_every_projection() -> None:
    # std: Bonett's half-width at N meets the tolerance and at N - 1 does not.
    rng = montecarlo.Rng(716)
    values = _normal(rng, 100)
    claim = mc_sizing.PrecisionClaim("std", 0.05, metric="x")
    check = mc_sizing.check_one(_metric_batch(values.tolist()), claim, bootstrap=False)
    s = float(values.std(ddof=1))
    g4 = mc_sizing.bonett_kurtosis(values)
    z = statistics.NormalDist().inv_cdf(0.975)

    def bonett_width(n: int) -> float:
        c = n / (n - z)
        se = c * math.sqrt((g4 - (n - 3) / n) / (n - 1))
        lo = math.sqrt(c * s * s * math.exp(-z * se))
        hi = math.sqrt(c * s * s * math.exp(z * se))
        return max(hi - s, s - lo)

    assert check.needed is not None
    assert bonett_width(check.needed) <= 0.05 < bonett_width(check.needed - 1)
    # Correlation precision: the jackknife half-width scaled as n / N.
    x, y = _correlated(717, 50, 0.5)
    batch = _synthetic({"p": x.tolist(), "y": y.tolist()}, ("p",))
    check = mc_sizing.check_one(
        batch, mc_sizing.PrecisionClaim("correlation", 0.1, metric="y", parameter="p")
    )
    rho, v = _jackknife(x, y)

    def corr_width(size: int) -> float:
        half = z * math.sqrt(v * 50 / size)
        return max(math.tanh(math.atanh(rho) + half) - rho, rho - math.tanh(math.atanh(rho) - half))

    assert check.needed is not None
    assert corr_width(check.needed) <= 0.1 < corr_width(check.needed - 1)


def test_mc_sizing_minimality_of_ranking_top_and_null() -> None:
    x1, y = _correlated(718, 60, 0.7)
    rng = montecarlo.Rng(719)
    x2 = _normal(rng, 60)
    batch = _synthetic({"p1": x1.tolist(), "p2": x2.tolist(), "y": y.tolist()}, ("p1", "p2"))
    c = statistics.NormalDist().inv_cdf(1 - 0.05 / 4)
    fits = [_jackknife(x1, y), _jackknife(x2, y)]

    def bounds(size: int) -> list[tuple[float, float]]:
        out = []
        for rho, v in fits:
            half = c * math.sqrt(v * 60 / size)
            out.append(
                _abs_bounds(math.tanh(math.atanh(rho) - half), math.tanh(math.atanh(rho) + half))
            )
        return out

    top = mc_sizing.check_one(batch, mc_sizing.RankingClaim("y")).needed
    assert top is not None
    resolved = lambda size: bounds(size)[0][0] > bounds(size)[1][1]  # noqa: E731
    if top > mc_sizing.RANK_MIN_N:
        assert resolved(top)
        assert not resolved(top - 1)
    else:
        assert resolved(mc_sizing.RANK_MIN_N)
    null_batch = _synthetic({"p2": x2.tolist(), "y": y.tolist()}, ("p2",))
    null = mc_sizing.check_one(null_batch, mc_sizing.RankingClaim("y", "null", 0.5)).needed
    assert null is not None
    c1 = statistics.NormalDist().inv_cdf(1 - 0.05 / 2)
    rho, v = fits[1]

    def upper_at(size: int) -> float:
        half = c1 * math.sqrt(v * 60 / size)
        return _abs_bounds(math.tanh(math.atanh(rho) - half), math.tanh(math.atanh(rho) + half))[1]

    assert upper_at(null) < 0.5 <= upper_at(null - 1)


# --- D7: coverage ---------------------------------------------------------------------------------


@pytest.mark.parametrize("p", [0.01, 0.1, 0.5])
def test_mc_sizing_coverage_calibrated_probability_bounds(p: float) -> None:
    def trial(rng: montecarlo.Rng) -> tuple[float, float]:
        k = sum(rng.uniform01() < p for _ in range(N))
        return mc_sizing.one_sided_bounds(k, N, C)

    seed = 7200 + int(p * 100)
    assert _covered(lambda rng: trial(rng)[1] >= p, seed) >= PASS
    assert _covered(lambda rng: trial(rng)[0] <= p, seed + 50) >= PASS


@pytest.mark.parametrize(
    ("label", "draw", "limit", "exceed"),
    [
        ("normal", _normal, 1.2815515655446004, 0.1),
        ("exponential", _exponential, -math.log(0.1), 0.1),
        (
            "atom",
            lambda rng, n: np.array([1.0 if rng.uniform01() < 0.9 else 2.0 for _ in range(n)]),
            1.0,
            0.1,
        ),
    ],
)
def test_mc_sizing_coverage_calibrated_percentile_exceedance(
    label: str, draw: Callable[[montecarlo.Rng, int], np.ndarray], limit: float, exceed: float
) -> None:
    claim = mc_sizing.PercentileClaim("x", 0.9, limit)

    def trial(rng: montecarlo.Rng) -> bool:
        values = draw(rng, N)
        k = int((values > limit).sum())
        upper = mc_sizing.one_sided_bounds(k, N, C)[1]
        return upper >= exceed

    assert claim.side == "upper"
    assert _covered(trial, 7300 + len(label)) >= PASS, label


@pytest.mark.parametrize(
    ("label", "draw", "mean"), [("normal", _normal, 0.0), ("exponential", _exponential, 1.0)]
)
def test_mc_sizing_coverage_nominal_mean(
    label: str, draw: Callable[[montecarlo.Rng, int], np.ndarray], mean: float
) -> None:
    claim = mc_sizing.PrecisionClaim("mean", 1.0, metric="x")

    def trial(rng: montecarlo.Rng) -> bool:
        check = mc_sizing.check_one(_metric_batch(draw(rng, N).tolist()), claim)
        return check.lower <= mean <= check.upper

    assert _covered(trial, 7400 + len(label)) >= PASS, label


STD_REGIMES = [
    # label, draw, sigma, seed, the coverage the docstring states (pooled over 5 x 1047 batches)
    ("normal", _normal, 1.0, 7101, 0.948),
    ("exponential", _exponential, 1.0, 7102, 0.914),
    ("t5", _t5, math.sqrt(5 / 3), 7103, 0.909),
    ("rare tail", _rare, math.sqrt(0.99 + 0.01 * 10000), 7104, 0.625),
]


@pytest.mark.parametrize(("label", "draw", "sigma", "seed", "stated"), STD_REGIMES)
def test_mc_sizing_coverage_nominal_std_bonett_and_its_stated_limits(
    label: str,
    draw: Callable[[montecarlo.Rng, int], np.ndarray],
    sigma: float,
    seed: int,
    stated: float,
) -> None:
    def trial(rng: montecarlo.Rng) -> bool:
        values = draw(rng, N)
        interval = mc_sizing.std_interval(
            float(values.std(ddof=1)), mc_sizing.bonett_kurtosis(values), N, C
        )
        return interval is not None and interval[0] <= sigma <= interval[1]

    covered = _covered(trial, seed)
    # Normal must pass the D7 test; the others are stated limits, held to a floor (the stated value
    # less a one-sided 99 % binomial allowance), never to a pinned count.
    floor = PASS if label == "normal" else int(stats.binom.ppf(0.01, K, stated))
    assert covered >= floor, (label, covered, floor)
    if label == "rare tail":
        assert covered / K <= 0.80  # the stated limit shows: the tail is often never sampled
    assert f"{stated:.3f}".rstrip("0") in (mc_sizing.__doc__ or "")


@pytest.mark.parametrize(
    ("label", "draw", "q"),
    [("normal", _normal, 1.2815515655446004), ("exponential", _exponential, -math.log(0.1))],
)
def test_mc_sizing_coverage_nominal_percentile(
    label: str, draw: Callable[[montecarlo.Rng, int], np.ndarray], q: float
) -> None:
    claim = mc_sizing.PrecisionClaim("percentile", 0.1, metric="x", quantile=0.9)

    def trial(rng: montecarlo.Rng) -> bool:
        check = mc_sizing.check_one(_metric_batch(draw(rng, N).tolist()), claim)
        return check.lower <= q <= check.upper

    assert _covered(trial, 7500 + len(label)) >= PASS, label


@pytest.mark.parametrize("p", [0.01, 0.1, 0.5])
def test_mc_sizing_coverage_nominal_probability(p: float) -> None:
    def trial(rng: montecarlo.Rng) -> bool:
        k = sum(rng.uniform01() < p for _ in range(N))
        lower, upper = mc_sizing.clopper_pearson(k, N, C)
        return lower <= p <= upper

    assert _covered(trial, 7600 + int(p * 100)) >= PASS


def _signflip(rng: montecarlo.Rng) -> tuple[np.ndarray, np.ndarray]:
    x = np.array([2.0 * rng.uniform01() - 1.0 for _ in range(N)])
    sign = np.array([1.0 if rng.uniform01() < 0.5 else -1.0 for _ in range(N)])
    return x, sign * x


def _ties(rng: montecarlo.Rng) -> tuple[np.ndarray, np.ndarray]:
    return _normal(rng, N), np.round(np.clip(_normal(rng, N), -2.0, 2.0))  # 5 levels


def _binormal(rho: float) -> Callable[[montecarlo.Rng], tuple[np.ndarray, np.ndarray]]:
    def draw(rng: montecarlo.Rng) -> tuple[np.ndarray, np.ndarray]:
        x = _normal(rng, N)
        return x, rho * x + math.sqrt(1 - rho * rho) * _normal(rng, N)

    return draw


@pytest.mark.parametrize(
    ("label", "draw", "rho_s"),
    [
        ("binormal 0", _binormal(0.0), 0.0),
        ("binormal 0.5", _binormal(0.5), 6 / math.pi * math.asin(0.25)),
        ("sign flip", _signflip, 0.0),
        ("ties", _ties, 0.0),
    ],
)
def test_mc_sizing_coverage_nominal_correlation(
    label: str, draw: Callable[[montecarlo.Rng], tuple[np.ndarray, np.ndarray]], rho_s: float
) -> None:
    claim = mc_sizing.PrecisionClaim("correlation", 0.1, metric="y", parameter="p")

    def trial(rng: montecarlo.Rng) -> bool:
        x, y = draw(rng)
        batch = _synthetic({"p": x.tolist(), "y": y.tolist()}, ("p",))
        check = mc_sizing.check_one(batch, claim)
        return check.lower <= rho_s <= check.upper

    assert _covered(trial, 7700 + len(label)) >= PASS, label


def test_mc_sizing_coverage_nominal_ranking_family() -> None:
    truth = [6 / math.pi * math.asin(0.25), 0.0, 0.0, 0.0]

    def trial(rng: montecarlo.Rng) -> bool:
        xs = [_normal(rng, N) for _ in range(4)]
        y = 0.5 * xs[0] + math.sqrt(0.75) * _normal(rng, N)
        columns = {f"p{i}": x.tolist() for i, x in enumerate(xs)}
        batch = _synthetic({**columns, "y": y.tolist()}, tuple(columns))
        rows = mc_sizing.check_one(batch, mc_sizing.RankingClaim("y")).parameters
        return all(row.lower <= rho <= row.upper for row, rho in zip(rows, truth, strict=True))

    assert _covered(trial, 7800) >= PASS


# --- R12: convergence -----------------------------------------------------------------------------


def test_mc_sizing_r12_convergence_calibrated_every_prefix() -> None:
    statuses = ["halt", "ok", "error", "ok", "halt", "ok", "ok", "error", "halt", "ok", "ok", "ok"]
    batch = _synthetic({"status": statuses})
    result = mc_sizing.convergence(batch, mc_sizing.ProbabilityClaim("0.5"))
    assert len(result.rows) == len(statuses)
    for j, row in enumerate(result.rows):
        prefix = statuses[: j + 1]
        k = prefix.count("halt")
        n = k + prefix.count("ok")
        assert row.n == n
        assert row.errors == prefix.count("error")
        assert row.estimate == k / n
        assert abs(row.upper - _cp_less(k, n)) <= 1e-12
        verdict = (
            "met"
            if _cp_less(k, n) <= 0.5
            else ("violated" if k and _cp_greater(k, n) > 0.5 else "undetermined")
        )
        assert row.verdict == verdict
    text = result.text(every=4)
    assert "met reachable" in text


def test_mc_sizing_r12_v2_bounds_below_the_option_minimum() -> None:
    result = mc_sizing.convergence(
        _metric_batch([2.0] * 10), mc_sizing.PercentileClaim("x", 0.95, 1.0)
    )
    for j, row in enumerate(result.rows):
        assert not math.isnan(row.lower)
        assert abs(row.lower - _cp_greater(j + 1, j + 1)) <= 1e-12
    assert result.rows[-1].verdict == "violated"
    assert abs(result.rows[-1].lower - 0.7411344491069477) <= 1e-12


def test_mc_sizing_r12_convergence_nominal_every_prefix() -> None:
    rng = montecarlo.Rng(720)
    values = _exponential(rng, 40).tolist()
    statuses = ["error" if i in (3, 17) else "ok" for i in range(40)]
    batch = _synthetic(
        {
            "status": statuses,
            "x": [None if s == "error" else v for s, v in zip(statuses, values, strict=True)],
        }
    )
    result = mc_sizing.convergence(batch, mc_sizing.PrecisionClaim("mean", 0.5, metric="x"))
    for j, row in enumerate(result.rows):
        seen = [v for s, v in zip(statuses[: j + 1], values[: j + 1], strict=True) if s != "error"]
        if len(seen) < 2:
            assert math.isnan(row.lower)
            continue
        hw = (
            float(stats.t.ppf(0.975, len(seen) - 1)) * statistics.stdev(seen) / math.sqrt(len(seen))
        )
        assert abs(row.lower - (statistics.fmean(seen) - hw)) <= 1e-12
    x, y = _correlated(721, 35, 0.5)
    ranking = mc_sizing.convergence(
        _synthetic({"p": x.tolist(), "y": y.tolist()}, ("p",)), mc_sizing.RankingClaim("y")
    )
    for j, row in enumerate(ranking.rows):
        if j + 1 < mc_sizing.RANK_MIN_N:
            assert all(math.isnan(p.lower) for p in row.parameters)  # empty at a constant prefix
        else:
            rho, v = _jackknife(x[: j + 1], y[: j + 1])
            c = statistics.NormalDist().inv_cdf(0.975)
            assert (
                abs(row.parameters[0].lower - math.tanh(math.atanh(rho) - c * math.sqrt(v)))
                <= 1e-12
            )
    assert mc_sizing.NOMINAL in ranking.text()


def test_mc_sizing_r12_convergence_on_the_descent_batch(descent: Path) -> None:
    batch = mc_analysis.load_batch(descent)
    result = mc_sizing.convergence(batch, mc_sizing.ProbabilityClaim("0.5"))
    summary = cast(dict[str, list[Any]], pq.read_table(descent / montecarlo.SUMMARY).to_pydict())
    statuses = summary["status"]
    assert statuses.count("halt") > 0
    for j, row in enumerate(result.rows):
        prefix = statuses[: j + 1]
        k = prefix.count("halt")
        n = k + prefix.count("ok")
        if n:
            assert abs(row.upper - _cp_less(k, n)) <= 1e-12


# --- D8: the pilot only sizes ---------------------------------------------------------------------


def test_mc_sizing_d8_pilot_is_independent_and_the_table_records_the_seed(
    descent: Path, tmp_path: Path
) -> None:
    spec = montecarlo.load(DESCENT_SPEC, root=REPO)
    pilot = dataclasses.replace(spec, seed=spec.seed + 1)
    assert [montecarlo.draw(pilot, i) for i in range(4)] != [
        montecarlo.draw(spec, i) for i in range(4)
    ]
    montecarlo.run(pilot, tmp_path / "pilot", workers=4)
    montecarlo.run(spec, tmp_path / "final", workers=4)
    shutil.rmtree(tmp_path / "pilot")
    after = pq.read_table(tmp_path / "final" / montecarlo.SUMMARY)
    alone = pq.read_table(descent / montecarlo.SUMMARY)
    assert after.equals(alone)  # the final batch does not depend on a pilot having run
    table = mc_sizing.check(mc_analysis.load_batch(descent), [mc_sizing.ProbabilityClaim("0.5")])
    assert table.seed == spec.seed
    assert f"master seed {spec.seed}" in table.text()
    assert mc_sizing.PROTOCOL in table.text()
    for row in table.rows:
        assert row.needed is None or row.needed >= row.minimum


# --- claims files ---------------------------------------------------------------------------------


def test_mc_sizing_load_claims_and_its_refusals(tmp_path: Path) -> None:
    claims = mc_sizing.load_claims(CLAIMS)
    assert [mc_sizing.option_of(claim) for claim in claims] == [
        "probability",
        "percentile",
        "precision",
        "ranking",
    ]
    for text, message in (
        ("claims: [{probability: {p_max: 0.1, nope: 1}}]", "unknown key 'nope'"),
        ("claims: [{probability: {}}]", "missing key 'p_max'"),
        ("claims: [{guess: {p_max: 0.1}}]", "unknown option 'guess'"),
        ("claims: []", "non-empty"),
        ("other: 1", "single key 'claims'"),
        ("claims: [{probability: {p_max: 1.5}}]", "p_max"),
    ):
        path = tmp_path / "claims.yaml"
        path.write_text(text, encoding="utf-8")
        with pytest.raises(SizingError, match=message):
            mc_sizing.load_claims(path)


# --- R15: astra mc size --------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("argv", "runs"),
    [
        (["probability", "--p-max", "0.01"], 299),
        (["percentile", "--quantile", "0.95"], 59),
        (["precision", "--p", "0", "--half-width", "0.03"], 122),
        (["ranking", "--r", "0.3", "--parameters", "4"], 120),
    ],
)
def test_mc_sizing_r15_cli_formula_modes(
    argv: list[str], runs: int, capsys: pytest.CaptureFixture[str]
) -> None:
    assert cli.main(["mc", "size", *argv]) == 0
    out = capsys.readouterr().out
    assert f"runs: {runs}\n" in out
    assert "method: " in out


def test_mc_sizing_r15_cli_data_modes_check_and_convergence(
    descent: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    batch = str(descent)
    for argv, expected in (
        (["probability", "--p-max", "0.5"], "verdict: "),
        (
            ["percentile", "--quantile", "0.9", "--metric", "frames_run", "--limit", "400"],
            "verdict: ",
        ),
        (
            ["precision", "--statistic", "mean", "--metric", "frames_run", "--tolerance", "10"],
            "needed: ",
        ),
        (["ranking", "--metric", "frames_run", "--top"], "needed: "),
    ):
        assert cli.main(["mc", "size", *argv, "--batch", batch]) == 0, argv
        out = capsys.readouterr().out
        assert expected in out
        assert "master seed 606" in out
    assert (
        cli.main(
            [
                "mc",
                "size",
                "probability",
                "--p-max",
                "0.5",
                "--batch",
                batch,
                "--convergence",
                "--every",
                "6",
            ]
        )
        == 0
    )
    assert "convergence: " in capsys.readouterr().out
    assert cli.main(["mc", "size", "check", batch, str(CLAIMS)]) == 0
    out = capsys.readouterr().out
    for option in (
        "probability (calibrated)",
        "percentile (calibrated)",
        "precision (nominal)",
        "ranking (nominal)",
    ):
        assert option in out


@pytest.mark.parametrize(
    "argv",
    [
        [],
        ["guess"],
        ["probability"],
        ["probability", "--p-max", "1.5"],
        ["probability", "--p-max", "abc"],
        ["probability", "--p-max", "0.1", "--criterion", "status == halt"],
        ["ranking", "--r", "0"],
        ["ranking", "--r", "0", "--parameters", "4"],
        ["ranking", "--metric", "x", "--top", "--null", "0.3", "--batch", "."],
        ["percentile", "--quantile", "0.9", "--order", "2", "--batch", "."],
        ["precision", "--p", "0.1"],
        ["ranking", "--r", "0.3", "--parameters", "4", "--confidence", "0.99"],  # review G-005
        ["ranking", "--r", "0.3", "--parameters", "4", "--confidence", "1.5"],
    ],
)
def test_mc_sizing_r15_cli_usage_errors_exit_64(
    argv: list[str], capsys: pytest.CaptureFixture[str]
) -> None:
    assert cli.main(["mc", "size", *argv]) == cli.EX_USAGE
    capsys.readouterr()


def test_mc_sizing_r15_cli_refusals_exit_65(
    descent: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    batch = str(descent)
    bad_claims = tmp_path / "claims.yaml"
    bad_claims.write_text("claims: [{probability: {p_max: 0.1, nope: 1}}]", encoding="utf-8")
    for argv, message in (
        (
            ["probability", "--p-max", "0.5", "--batch", str(tmp_path / "none")],
            "cannot read the summary",
        ),
        (
            ["probability", "--p-max", "0.5", "--criterion", "nope > 1", "--batch", batch],
            "unknown column",
        ),
        (
            [
                "precision",
                "--statistic",
                "mean",
                "--metric",
                "nope",
                "--tolerance",
                "1",
                "--batch",
                batch,
            ],
            "unknown column",
        ),
        (["check", batch, str(bad_claims)], "unknown key"),
    ):
        assert cli.main(["mc", "size", *argv]) == cli.EX_DATAERR, argv
        assert message in capsys.readouterr().err


# --- review round 1 regressions (docs/reviews/m6-pr7-mc-sizing-review.md) -------------------------


def _write_summary(path: Path, values: list[float]) -> Path:
    path.mkdir()
    n = len(values)
    table = pa.table(
        {
            "run": pa.array(list(range(n)), pa.int64()),
            "status": pa.array(["ok"] * n),
            "error_type": pa.array([None] * n, pa.string()),
            "x": pa.array(values, pa.float64()),
        }
    )
    pq.write_table(table, path / montecarlo.SUMMARY)
    return path


def test_mc_sizing_g001_decimal_ties_are_not_exceedances(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    upper = mc_sizing.check_one(
        _metric_batch([0.1] * 59), mc_sizing.PercentileClaim("x", 0.95, 0.1)
    )
    assert upper.estimate == 0.0
    assert upper.verdict == "met"
    lower = mc_sizing.check_one(
        _metric_batch([0.3] * 59), mc_sizing.PercentileClaim("x", 0.95, 0.3, "lower")
    )
    assert lower.estimate == 0.0
    assert lower.verdict == "met"
    # A different double just above the written limit is an exceedance.
    above = mc_sizing.check_one(
        _metric_batch([0.30000000000000004] * 59), mc_sizing.PercentileClaim("x", 0.95, 0.3)
    )
    assert above.estimate == 1.0
    batch = _write_summary(tmp_path / "ties", [0.1] * 59)
    argv = ["mc", "size", "percentile", "--quantile", "0.95", "--metric", "x", "--limit", "0.1"]
    assert cli.main([*argv, "--batch", str(batch)]) == 0
    assert "verdict: met" in capsys.readouterr().out


def test_mc_sizing_g002_a_constant_metric_has_no_std_interval(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    claim = mc_sizing.PrecisionClaim("std", 0.1, metric="x")
    check = mc_sizing.check_one(_metric_batch([1.0] * 100), claim)
    assert math.isnan(check.lower)
    assert check.needed is None
    assert any("constant" in note for note in check.notes)
    assert math.isnan(mc_sizing.bonett_kurtosis(np.ones(100)))
    rng = montecarlo.Rng(722)
    values = [1.0] * 100 + _normal(rng, 20).tolist()
    result = mc_sizing.convergence(_metric_batch(values), claim)
    assert math.isnan(result.rows[99].lower)
    assert not math.isnan(result.rows[-1].lower)
    batch = _write_summary(tmp_path / "constant", [1.0] * 100)
    argv = ["mc", "size", "precision", "--statistic", "std", "--metric", "x", "--tolerance", "0.1"]
    assert cli.main([*argv, "--batch", str(batch)]) == 0
    assert "the metric is constant" in capsys.readouterr().out


def test_mc_sizing_g003_convergence_runs_no_sizing_search() -> None:
    batch = _outcome_batch(1, 100)  # run 0 halts, 99 pass
    claim = mc_sizing.ProbabilityClaim(".3334")
    assert mc_sizing.check_one(batch, claim).needed == 8
    result = mc_sizing.convergence(batch, claim)
    assert all(row.needed is None for row in result.rows)
    assert abs(result.rows[2].upper - _cp_less(1, 3)) <= 1e-12
    assert result.rows[-1].verdict == "met"


def test_mc_sizing_g004_claim_fields_are_typed(tmp_path: Path) -> None:
    with pytest.raises(SizingError, match="relative"):
        mc_sizing.PrecisionClaim("mean", 0.1, relative="false", metric="x")  # pyright: ignore[reportArgumentType]
    with pytest.raises(SizingError, match="metric"):
        mc_sizing.RankingClaim(3)  # pyright: ignore[reportArgumentType]
    with pytest.raises(SizingError, match="criterion"):
        mc_sizing.ProbabilityClaim(0.1, criterion=5)  # pyright: ignore[reportArgumentType]
    path = tmp_path / "claims.yaml"
    path.write_text(
        'claims: [{precision: {statistic: mean, metric: x, tolerance: 0.1, relative: "false"}}]',
        encoding="utf-8",
    )
    with pytest.raises(SizingError, match="relative"):
        mc_sizing.load_claims(path)
