"""astra.mc_sizing: how many runs a Monte Carlo batch needs, what a batch shows about a claim, and
whether its estimate has settled (M6 PR 7, docs/plans/m6-pr7-mc-sizing.md; M6 plan Q10, R11, R12).

Four options, each named for the claim it answers, each with two modes.

===========  ====================================  ============================================
option       claim                                 without a batch (formula mode, the minimum N)
===========  ====================================  ============================================
probability  P(fail) <= p_max                      ``probability_runs``: zero failures
percentile   P(metric > limit) <= 1 - gamma        ``percentile_runs``: Wilks order statistics
precision    a statistic known within a tolerance  ``precision_runs``: Clopper-Pearson
ranking      a top driver, or no |rho| >= r_min    ``ranking_runs``: Fisher z with Bonferroni
===========  ====================================  ============================================

With a batch (``check``), the options come in two tiers.

- **Calibrated verdicts** (probability, percentile): exact binomial (Clopper-Pearson) bounds, so a
  verdict holds at the stated confidence for a batch whose runs are independent of how its size was
  chosen. ``met`` when the one-sided upper bound is at most the limit, ``violated`` when the
  one-sided lower bound is strictly above it, ``undetermined`` otherwise; both bounds are always
  checked.
- **Nominal projections** (precision, ranking): an approximate interval and a projected N, labeled
  ``nominal, not calibrated``, and never a verdict. Their coverage depends on the output's
  distribution; the stated limits below carry measured coverage.

One non-strict convention, decided exactly. A claim's bound meets its limit when ``bound <= limit``.
Each decision uses the duality of the Clopper-Pearson bounds with the binomial tail: ``U(k, n) <=
p`` iff ``P(Bin(n, p) <= k) <= 1 - C``, and ``L(k, n) > p`` iff ``P(Bin(n, p) >= k) < 1 - C``,
computed in floating point and recomputed in exact rational arithmetic whenever it lies within 1e-9
(relative) of ``1 - C``. Inputs are taken *as written*: a string exactly (the CLI passes its
arguments' text), a Python float as its shortest repr (``Fraction(repr(x))``, so 0.95 is 19/20,
never ``Fraction(x)``), and every complement (``1 - C``, ``1 - gamma``) is formed in rational
arithmetic, never in floating point (where ``1 - 0.9`` is 0.09999999999999998).

Formula mode.

- ``probability_runs(p_max, confidence)``: the smallest n with ``(1 - p_max)^n <= 1 - C``, the runs
  that show ``P(fail) <= p_max`` when none fails (299 at 0.01 and 0.95).
- ``percentile_runs(coverage, confidence, two_sided=False, order=1)``: Wilks. One-sided, the
  ``order``-th largest of n runs bounds a ``coverage`` fraction from above with probability
  ``confidence``; two-sided, the ``order``-th smallest and largest bound it on both sides. Both are
  the smallest n with ``P(Bin(n, 1 - coverage) >= j) >= C``, ``j = order`` one-sided and ``2 order``
  two-sided, for a continuous outcome (95/95: 59 one-sided, 93 two-sided).
- ``precision_runs(p, half_width, confidence)``: the smallest n whose exact (Clopper-Pearson)
  interval at the *expected* count ``k = n p`` (a real number: the bounds are beta quantiles) has
  ``max(U - p, p - L) <= half_width``. A planning figure: the interval at an observed integer count
  can still exceed the half-width (p = 0.05 and 0.02 give 632, and at ``k = round(n p)`` the
  interval exceeds 0.02 for n = 632 to 635).
- ``ranking_runs(r, parameters, alpha=0.05, power=0.8)``: ``ceil(((z_(1 - alpha / (2 m)) + z_power)
  / atanh(|r|))^2 + 3)``. Fisher z is asymptotic for Pearson's r under bivariate normality, an
  approximation for Spearman's rho, and conservative in m (Bonferroni); it does not size PRCC, the
  KS test or the ranking. ``r`` and ``-r`` size alike; ``0 < |r| < 1``.

Calibrated tier (with a batch).

- ``ProbabilityClaim``: the failure probability under a criterion (``astra.mc_analysis``'s grammar,
  default ``status == halt``) is at most ``p_max``. ``error`` rows are excluded and counted. The
  minimum is ``probability_runs(p_max, C)``: below it ``met`` is unreachable (``violated`` is not).
  ``needed`` is the producer-risk N (after Hanson & Beard): the smallest N for which a *fresh* batch
  of N runs gives ``met`` with probability at least ``1 - producer_risk`` when the true rate is the
  batch's own ``k / n``: ``P(Bin(N, k/n) <= k*(N)) >= 1 - beta``, with ``k*(N)`` the largest count
  that still gives ``met``. The default producer risk is 0.10. It differs from the ranking option's
  power 0.8 because it answers another question: it is the chance that a fresh batch fails to
  confirm a claim that holds at the observed rate, a test's type II error at a stated alternative,
  while 0.8 is Fisher z's planning power for detecting a correlation. The objective is not monotone
  in N; ``needed`` is the smallest N that meets it (a larger N can fall short again). A rate at or
  above ``p_max`` has no ``needed``.
- ``PercentileClaim``: ``P(metric > limit) <= 1 - gamma`` (``side="upper"``) or ``P(metric < limit)
  <= 1 - gamma`` (``side="lower"``), the probability claim on the count of exceedances (a value
  equal to the limit is not one). With ``q_gamma = inf{x : F(x) >= gamma}``, the upper claim is
  exactly ``q_gamma <= limit``, atoms included. The order statistic that bounds the quantile at C is
  printed for information only; it decides nothing.

Nominal tier (with a batch). Estimators: the sample mean, ``s^2`` with ``n - 1``, central moments
``m_k = (1/n) sum (x - mean)^k``, kurtosis ``m4 / m2^2`` (not excess) and skewness ``m3 / m2^1.5``.
Each check gives the estimate, a nominal two-sided interval at C, its half-width (the larger
distance from the estimate), the tolerance (absolute, or relative to the estimate) and the projected
N, at least the statistic's minimum.

- ``mean``: the t interval; N = ``ceil(n (hw / tol)^2)``. Minimum 2.
- ``std``: Bonett's interval (Bonett, D. G. (2006), "Approximate confidence interval for standard
  deviation of nonnormal distributions", Computational Statistics & Data Analysis 50(3), 775-782):
  ``sqrt(c s^2 exp(+-z se))`` with ``c = n / (n - z)``, ``se = c sqrt([g4 - (n - 3) / n] / (n -
  1))`` and ``g4 = n sum (x - m)^4 / (sum (x - mean)^2)^2``, ``m`` the mean trimmed by ``floor(n /
  (2 sqrt(n - 4)))`` values from each tail. That se form, with its ``c``, is the paper's calibrated
  choice for nonnormal outputs; it is distinct from the exact ``Var(s^2) = sigma^4 [kappa - (n -
  3) / (n - 1)] / n`` and is not derived from it. N is the smallest whose half-width is at most
  the tolerance, ``s`` and ``g4`` held. Minimum ``STD_MIN_N`` = 100 for every output. Printed
  flags, never gates: ``skewed`` (|skewness| > 1), ``heavy-tailed`` (excess kurtosis > 2), and
  ``bootstrap disagrees`` (a percentile bootstrap, ``BOOTSTRAP`` resamples drawn with
  ``astra.montecarlo.Rng(0)``, whose half-width differs by more than 25 %).

  **Stated limits.** The std interval is approximate: not a verdict, and not for requirement
  compliance (use the probability or percentile option for that). Measured coverage of a nominal
  95 % interval at n = 100, over 5 x 1047 seeded batches each (seeds ``run_seed(7101 + 1000 b,
  k)`` and up, ``tests/python/test_mc_sizing.py``): normal 0.948, exponential 0.914, Student t
  with 5 degrees of freedom 0.909, and a rare tail (+-1 with probability 0.99, +-100 with
  probability 0.01) 0.625. A tail of probability p is missed entirely with probability ``(1 -
  p)^n`` (0.366 for the rare tail at n = 100), and then neither this interval nor the bootstrap can
  see it: agreement with the bootstrap does not make the interval calibrated.
- ``percentile``: the distribution-free two-sided order-statistic interval for ``q_gamma`` around
  ``X_(ceil(n gamma))``; N = ``ceil(n (hw / tol)^2)``, a projection that assumes a positive density
  at the quantile. A zero-width interval (an atom) has no projection. Minimum: the smallest n that
  has an interval at C.
- ``probability``: the two-sided Clopper-Pearson interval; N = ``precision_runs(k / n, tol, C)``.
- ``correlation``: Spearman's rho of a parameter against a metric, with the jackknife interval below
  (no Bonferroni); N from the jackknife variance scaled as ``n / N``.
- ``RankingClaim`` (``top``, or ``null`` at ``r_min``): Spearman's rho of every ranked parameter
  against a metric. The variance of ``atanh(rho)`` is the jackknife's, ``((n - 1) / n) sum (z_(-i) -
  mean z_(-i))^2`` over the leave-one-out values, so no distribution is assumed for it (Fisher's
  ``1.06 / (n - 3)`` holds only near normality). Simultaneous intervals ``tanh(z +- c sqrt(v))``,
  ``c = z_(1 - alpha / (2 m))``. No interval at |rho| = 1, at a leave-one-out |rho| = 1, or below
  ``RANK_MIN_N`` = 30 runs. ``top``: the smallest N at which the largest |rho|'s lower |rho| bound
  exceeds every other parameter's upper bound (none at a tie). ``null``: the smallest N at which
  every upper |rho| bound is below ``r_min`` (none when an observed |rho| reaches it).

Protocol. A pilot only sizes: run a pilot with its own master seed; ``check`` it for ``needed``;
delete the pilot (only its ``summary.parquet`` is read); run the final batch with another master
seed and ``runs: needed``; take the verdict from the final batch alone. The final batch is then
independent of the choice of its size, so a calibrated verdict holds at C and the producer risk is
the one computed. The tool cannot enforce this; a check of a batch prints the batch's master seed
(from the spec its summary records) and the reminder.

Convergence (``convergence``): the claim's statistic and its bounds or interval at every prefix of
the batch's rows in run order. Calibrated bounds and verdicts are given at every prefix with an
analyzed row, with a column saying whether ``met`` is reachable there; nominal intervals are NaN
below their minimum. The bootstrap is not run per prefix, and neither is any sizing search: a
prefix's ``needed`` is ``None``, so a prefix whose projection would exceed ``MAX_RUNS`` still shows
its bounds.

Every argument is checked: a value outside its domain, a non-finite number, a bool, a non-integer
count, or a string that is not a decimal is a ``SizingError`` (a ``ValueError``) naming it."""

from __future__ import annotations

import contextvars
import dataclasses
import importlib
import math
import re
from collections.abc import Callable, Sequence
from fractions import Fraction
from pathlib import Path
from typing import Any, Literal, cast

import numpy as np
import numpy.typing as npt
import yaml

from astra import mc_analysis, montecarlo

FloatArray = npt.NDArray[np.float64]
IntArray = npt.NDArray[np.int64]
BoolArray = npt.NDArray[np.bool_]
Number = float | int | str | Fraction

MAX_RUNS = 10_000_000  # every search stops here and refuses
STD_MIN_N = 100
RANK_MIN_N = 30
MEAN_MIN_N = 2
BOOTSTRAP = 2000
BOOTSTRAP_DISAGREEMENT = 0.25
DEFAULT_CONFIDENCE = "0.95"
DEFAULT_PRODUCER_RISK = "0.10"
_BOUNDARY = 1e-9  # relative distance from 1 - C within which a tail is recomputed exactly
_FIRST_CHUNK = 1024
_DECIMAL = re.compile(r"[-+]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][-+]?[0-9]+)?")

NOMINAL = "nominal, not calibrated"
# Convergence evaluates bounds and verdicts only: no sizing search runs per prefix (review G-003).
_PROJECT: contextvars.ContextVar[bool] = contextvars.ContextVar(
    "astra_mc_sizing_project", default=True
)
PROTOCOL = (
    "a pilot only sizes: run the final batch with another master seed and runs: needed, and take "
    "the verdict from it alone"
)
ASSUMPTIONS = {
    "probability": "zero failures in n independent runs: (1 - p_max)^n <= 1 - confidence",
    "percentile": "Wilks: distribution-free order statistics of a continuous outcome over "
    "independent runs",
    "precision": "exact Clopper-Pearson interval at the expected count n p; the interval at an "
    "observed integer count can exceed the half-width",
    "ranking": "Fisher z with a Bonferroni split over the parameters: asymptotic for Pearson's "
    "r, an approximation for Spearman's rho; it does not size PRCC, KS or the ranking",
}


class SizingError(ValueError):
    """An argument or a claim is refused; the message names it."""


def _stats() -> Any:
    # scipy ships no type information (no py.typed), so it is handled as Any at this boundary.
    return importlib.import_module("scipy.stats")


# --- inputs, as written ---------------------------------------------------------------------------


def exact(name: str, value: object) -> Fraction:
    """``value`` as written (module docstring): a decimal string exactly, a float as its shortest
    repr, an int as itself. Raises ``SizingError``."""
    if isinstance(value, bool):
        raise SizingError(f"{name}: must be a number, got {value!r}")
    if isinstance(value, Fraction):
        return value
    if isinstance(value, str):
        text = value.strip()
        if _DECIMAL.fullmatch(text) is None:
            raise SizingError(f"{name}: {value!r} is not a decimal number")
        return Fraction(text)
    if isinstance(value, int):
        return Fraction(value)
    if isinstance(value, float):
        if not math.isfinite(value):
            raise SizingError(f"{name}: must be finite, got {value!r}")
        return Fraction(repr(value))
    raise SizingError(f"{name}: must be a number, got {value!r}")


def _open_unit(name: str, value: object) -> Fraction:
    number = exact(name, value)
    if not 0 < number < 1:
        raise SizingError(f"{name}: must be in (0, 1), got {value!r}")
    return number


def _closed_unit(name: str, value: object) -> Fraction:
    number = exact(name, value)
    if not 0 <= number <= 1:
        raise SizingError(f"{name}: must be in [0, 1], got {value!r}")
    return number


def _text(name: str, value: object, *, optional: bool = False) -> None:
    if value is None and optional:
        return
    if not isinstance(value, str) or not value:
        raise SizingError(f"{name}: must be a non-empty string, got {value!r}")


def _count(name: str, value: object, lo: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise SizingError(f"{name}: must be an integer, got {value!r}")
    if value < lo:
        raise SizingError(f"{name}: must be at least {lo}, got {value!r}")
    return value


# --- exact binomial decisions ---------------------------------------------------------------------


def _exact_cdf(k: int, n: int, p: Fraction) -> Fraction:
    """``P(Bin(n, p) <= k)`` exactly."""
    if k < 0:
        return Fraction(0)
    if k >= n:
        return Fraction(1)
    a, b = p.numerator, p.denominator
    total = sum(math.comb(n, i) * a**i * (b - a) ** (n - i) for i in range(k + 1))
    return Fraction(total, b**n)


def _near(value: float, target: Fraction) -> bool:
    return abs(value - float(target)) <= _BOUNDARY * max(float(target), 1e-300)


def upper_meets(k: int, n: int, p: Fraction, alpha: Fraction) -> bool:
    """``U(k, n) <= p`` at one-sided confidence ``1 - alpha``: ``P(Bin(n, p) <= k) <= alpha``."""
    if k >= n:
        return p >= 1
    if p <= 0:
        return False
    value = float(_stats().binom.cdf(k, n, float(p)))
    if _near(value, alpha):
        return _exact_cdf(k, n, p) <= alpha
    return value <= float(alpha)


def lower_exceeds(k: int, n: int, p: Fraction, alpha: Fraction) -> bool:
    """``L(k, n) > p`` at one-sided confidence ``1 - alpha``: ``P(Bin(n, p) >= k) < alpha``."""
    if k <= 0:
        return False
    if p <= 0:
        return True
    if p >= 1:
        return False
    value = float(_stats().binom.sf(k - 1, n, float(p)))
    if _near(value, alpha):
        return 1 - _exact_cdf(k - 1, n, p) < alpha
    return value < float(alpha)


def _search(meets: Callable[[IntArray], BoolArray], start: int, what: str) -> int:
    """The smallest n >= ``start`` for which ``meets`` holds, over doubling chunks of n. Raises
    ``SizingError`` past ``MAX_RUNS``."""
    lo = start
    size = _FIRST_CHUNK
    while lo <= MAX_RUNS:
        hi = min(lo + size, MAX_RUNS + 1)
        n = np.arange(lo, hi, dtype=np.int64)
        hits = np.flatnonzero(meets(n))
        if hits.size:
            return int(n[hits[0]])
        lo = hi
        size *= 2
    raise SizingError(f"{what}: needs more than {MAX_RUNS} runs")


def _first_upper_meeting(k: int, p: Fraction, alpha: Fraction, start: int, what: str) -> int:
    """The smallest n >= ``start`` with ``U(k, n) <= p`` (monotone in n): a float search with a
    margin finds the candidate, then the exact decision steps forward from just below it."""
    binom = _stats().binom
    slack = float(alpha) * (1.0 + 2.0 * _BOUNDARY)

    def loose(n: IntArray) -> BoolArray:
        return np.asarray(binom.cdf(k, n, float(p)), dtype=np.float64) <= slack

    n = max(start, _search(loose, start, what) - 1)
    while not upper_meets(k, n, p, alpha):
        n += 1
    return n


# --- formula mode ---------------------------------------------------------------------------------


def probability_runs(p_max: Number, confidence: Number = DEFAULT_CONFIDENCE) -> int:
    """The runs that show ``P(fail) <= p_max`` at ``confidence`` when none fails."""
    p = _open_unit("p_max", p_max)
    alpha = 1 - _open_unit("confidence", confidence)
    return _first_upper_meeting(0, p, alpha, 1, "probability")


def percentile_runs(
    coverage: Number,
    confidence: Number = DEFAULT_CONFIDENCE,
    *,
    two_sided: bool = False,
    order: int = 1,
) -> int:
    """The runs for a Wilks tolerance bound (module docstring)."""
    gamma = _open_unit("coverage", coverage)
    alpha = 1 - _open_unit("confidence", confidence)
    order = _count("order", order, 1)
    j = order * (2 if two_sided else 1)
    # P(Bin(n, 1 - gamma) >= j) >= C  iff  P(Bin(n, 1 - gamma) <= j - 1) <= 1 - C,
    # that is, U(j - 1, n) <= 1 - gamma.
    return _first_upper_meeting(j - 1, 1 - gamma, alpha, j, "percentile")


def percentile_confidence(
    n: int, coverage: Number, *, two_sided: bool = False, order: int = 1
) -> float:
    """``P(Bin(n, 1 - coverage) >= j)``: the confidence of ``n`` runs' Wilks bound."""
    n = _count("n", n, 1)
    gamma = _open_unit("coverage", coverage)
    order = _count("order", order, 1)
    j = order * (2 if two_sided else 1)
    return float(_stats().binom.sf(j - 1, n, float(1 - gamma)))


def _bounds(
    k: FloatArray, n: FloatArray, lower_q: float, upper_q: float
) -> tuple[FloatArray, FloatArray]:
    """Clopper-Pearson bounds as beta quantiles, for real ``0 <= k <= n``: the lower at quantile
    ``lower_q`` (0 at k = 0), the upper at ``upper_q`` (1 at k = n)."""
    beta = _stats().beta
    with np.errstate(invalid="ignore", divide="ignore"):
        lower = np.where(k > 0.0, beta.ppf(lower_q, np.maximum(k, 1e-300), n - k + 1.0), 0.0)
        upper = np.where(k < n, beta.ppf(upper_q, k + 1.0, np.maximum(n - k, 1e-300)), 1.0)
    return np.asarray(lower, dtype=np.float64), np.asarray(upper, dtype=np.float64)


def clopper_pearson(
    k: float, n: int, confidence: Number = DEFAULT_CONFIDENCE
) -> tuple[float, float]:
    """The two-sided exact (Clopper-Pearson) interval for ``k`` of ``n``; ``k`` may be real."""
    n = _count("n", n, 1)
    checked = cast(object, k)  # callers are untyped Python; check the type at run time too
    if isinstance(checked, bool) or not isinstance(checked, int | float) or not 0 <= float(k) <= n:
        raise SizingError(f"k: must be a number in [0, n] = [0, {n}], got {k!r}")
    tail = float((1 - _open_unit("confidence", confidence)) / 2)
    lower, upper = _bounds(np.asarray([float(k)]), np.asarray([float(n)]), tail, 1.0 - tail)
    return float(lower[0]), float(upper[0])


def one_sided_bounds(
    k: int, n: int, confidence: Number = DEFAULT_CONFIDENCE
) -> tuple[float, float]:
    """The one-sided exact lower and upper bounds for ``k`` of ``n``, each at ``confidence``."""
    alpha = float(1 - _open_unit("confidence", confidence))
    lower, upper = _bounds(np.asarray([float(k)]), np.asarray([float(n)]), alpha, 1.0 - alpha)
    return float(lower[0]), float(upper[0])


def _half_widths(n: IntArray, p: float, confidence: Fraction) -> FloatArray:
    size = n.astype(np.float64)
    tail = float((1 - confidence) / 2)
    lower, upper = _bounds(size * p, size, tail, 1.0 - tail)
    return np.maximum(upper - p, p - lower)


def half_width_at(n: int, p: Number, confidence: Number = DEFAULT_CONFIDENCE) -> float:
    """``max(U - p, p - L)`` of the interval at the expected count ``n p``."""
    n = _count("n", n, 1)
    rate = float(_closed_unit("p", p))
    level = _open_unit("confidence", confidence)
    return float(_half_widths(np.asarray([n], dtype=np.int64), rate, level)[0])


def precision_runs(p: Number, half_width: Number, confidence: Number = DEFAULT_CONFIDENCE) -> int:
    """The runs to estimate a probability near ``p`` within ``half_width`` (module docstring)."""
    rate = float(_closed_unit("p", p))
    width = exact("half_width", half_width)
    if not 0 < width < Fraction(1, 2):
        raise SizingError(f"half_width: must be in (0, 0.5), got {half_width!r}")
    level = _open_unit("confidence", confidence)

    def meets(n: IntArray) -> BoolArray:
        return _half_widths(n, rate, level) <= float(width)

    return _search(meets, 1, "precision")


def ranking_runs(r: Number, parameters: int, alpha: Number = "0.05", power: Number = "0.8") -> int:
    """The runs to detect a rank correlation ``|r|`` across ``parameters`` (module docstring)."""
    size = abs(float(exact("r", r)))
    if not 0.0 < size < 1.0:
        raise SizingError(f"r: must satisfy 0 < |r| < 1, got {r!r}")
    m = _count("parameters", parameters, 1)
    level = float(_open_unit("alpha", alpha))
    strength = float(_open_unit("power", power))
    norm = _stats().norm
    z = float(norm.ppf(1.0 - level / (2.0 * m))) + float(norm.ppf(strength))
    return math.ceil((z / math.atanh(size)) ** 2 + 3.0)


def producer_runs(
    rate: Fraction,
    p_max: Number,
    confidence: Number = DEFAULT_CONFIDENCE,
    producer_risk: Number = DEFAULT_PRODUCER_RISK,
) -> int | None:
    """The producer-risk N (module docstring): the smallest N with ``P(Bin(N, rate) <= k*(N)) >= 1 -
    producer_risk``. ``None`` when ``rate >= p_max``."""
    p = _open_unit("p_max", p_max)
    alpha = 1 - _open_unit("confidence", confidence)
    beta = _open_unit("producer_risk", producer_risk)
    if not 0 <= rate <= 1:
        raise SizingError(f"rate: must be in [0, 1], got {rate!r}")
    if rate >= p:
        return None
    if rate == 0:
        return _first_upper_meeting(0, p, alpha, 1, "producer risk")
    binom = _stats().binom
    want = 1 - beta
    lo = 1
    size = _FIRST_CHUNK
    while lo <= MAX_RUNS:
        hi = min(lo + size, MAX_RUNS + 1)
        n = np.arange(lo, hi, dtype=np.int64)
        # k*(N): the largest k with P(Bin(N, p) <= k) <= alpha; ppf gives the smallest k with
        # cdf >= alpha, so k* is ppf or ppf - 1. A float margin, then the exact decision.
        guess = np.asarray(binom.ppf(float(alpha), n, float(p)), dtype=np.float64)
        prob = np.asarray(binom.cdf(guess, n, float(rate)), dtype=np.float64)
        candidates = np.flatnonzero(prob >= float(want) * (1.0 - 2.0 * _BOUNDARY))
        for index in candidates:
            count = int(n[index])
            k_star = _k_star(count, p, alpha, int(guess[index]))
            if k_star >= 0 and _probability_at_least(k_star, count, rate, want):
                return count
        lo = hi
        size *= 2
    raise SizingError(f"producer risk: needs more than {MAX_RUNS} runs")


def _k_star(n: int, p: Fraction, alpha: Fraction, guess: int) -> int:
    """The largest k with ``U(k, n) <= p`` (``-1`` when none), from a guess near it."""
    k = min(max(guess, 0), n)
    while k >= 0 and not upper_meets(k, n, p, alpha):
        k -= 1
    while k + 1 <= n and upper_meets(k + 1, n, p, alpha):
        k += 1
    return k


def _probability_at_least(k: int, n: int, p: Fraction, want: Fraction) -> bool:
    value = float(_stats().binom.cdf(k, n, float(p)))
    if _near(value, want):
        return _exact_cdf(k, n, p) >= want
    return value >= float(want)


# --- claims ---------------------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class ProbabilityClaim:
    """``P(fail) <= p_max`` under ``criterion`` (default ``status == halt``)."""

    p_max: Number
    criterion: str | None = None
    confidence: Number = DEFAULT_CONFIDENCE
    producer_risk: Number = DEFAULT_PRODUCER_RISK

    def __post_init__(self) -> None:
        _open_unit("p_max", self.p_max)
        _text("criterion", self.criterion, optional=True)
        _open_unit("confidence", self.confidence)
        _open_unit("producer_risk", self.producer_risk)

    def text(self) -> str:
        return f"P({self.criterion or mc_analysis.DEFAULT_CRITERION}) <= {self.p_max}"


@dataclasses.dataclass(frozen=True)
class PercentileClaim:
    """``P(metric > limit) <= 1 - quantile`` (``upper``) or ``P(metric < limit) <= 1 - quantile``
    (``lower``)."""

    metric: str
    quantile: Number
    limit: Number
    side: Literal["upper", "lower"] = "upper"
    confidence: Number = DEFAULT_CONFIDENCE
    producer_risk: Number = DEFAULT_PRODUCER_RISK

    def __post_init__(self) -> None:
        _text("metric", self.metric)
        _open_unit("quantile", self.quantile)
        exact("limit", self.limit)
        if self.side not in ("upper", "lower"):
            raise SizingError(f"side: must be upper or lower, got {self.side!r}")
        _open_unit("confidence", self.confidence)
        _open_unit("producer_risk", self.producer_risk)

    def text(self) -> str:
        op = ">" if self.side == "upper" else "<"
        return f"P({self.metric} {op} {self.limit}) <= 1 - {self.quantile}"


STATISTICS = ("mean", "std", "percentile", "probability", "correlation")


@dataclasses.dataclass(frozen=True)
class PrecisionClaim:
    """``statistic`` known within ``tolerance`` (absolute, or ``relative`` to the estimate)."""

    statistic: Literal["mean", "std", "percentile", "probability", "correlation"]
    tolerance: Number
    relative: bool = False
    metric: str | None = None
    criterion: str | None = None
    parameter: str | None = None
    quantile: Number | None = None
    confidence: Number = DEFAULT_CONFIDENCE

    def __post_init__(self) -> None:
        if self.statistic not in STATISTICS:
            raise SizingError(
                f"statistic: must be one of {', '.join(STATISTICS)}, got {self.statistic!r}"
            )
        if exact("tolerance", self.tolerance) <= 0:
            raise SizingError(f"tolerance: must be greater than zero, got {self.tolerance!r}")
        checked = cast(object, self.relative)  # a claims file is untyped: "false" is not False
        if not isinstance(checked, bool):
            raise SizingError(f"relative: must be true or false, got {self.relative!r}")
        for name in ("metric", "criterion", "parameter"):
            _text(name, getattr(self, name), optional=True)
        _open_unit("confidence", self.confidence)
        needs = {
            "mean": ("metric",),
            "std": ("metric",),
            "percentile": ("metric", "quantile"),
            "probability": (),
            "correlation": ("metric", "parameter"),
        }[self.statistic]
        for name in ("metric", "criterion", "parameter", "quantile"):
            given = getattr(self, name) is not None
            allowed = name in needs or (name == "criterion" and self.statistic == "probability")
            if name in needs and not given:
                raise SizingError(f"{name}: required for the {self.statistic} statistic")
            if given and not allowed:
                raise SizingError(f"{name}: not used by the {self.statistic} statistic")
        if self.quantile is not None:
            _open_unit("quantile", self.quantile)

    def text(self) -> str:
        subject = {
            "mean": f"mean({self.metric})",
            "std": f"std({self.metric})",
            "percentile": f"q{self.quantile}({self.metric})",
            "probability": f"P({self.criterion or mc_analysis.DEFAULT_CRITERION})",
            "correlation": f"rho({self.parameter}, {self.metric})",
        }[self.statistic]
        within = f"{self.tolerance} relative" if self.relative else f"{self.tolerance}"
        return f"{subject} within {within}"


@dataclasses.dataclass(frozen=True)
class RankingClaim:
    """``top``: one parameter drives ``metric`` the most; ``null``: no parameter has |rho| >=
    ``r_min``."""

    metric: str
    kind: Literal["top", "null"] = "top"
    r_min: Number | None = None
    confidence: Number = DEFAULT_CONFIDENCE

    def __post_init__(self) -> None:
        _text("metric", self.metric)
        if self.kind not in ("top", "null"):
            raise SizingError(f"kind: must be top or null, got {self.kind!r}")
        if self.kind == "null":
            if self.r_min is None:
                raise SizingError("r_min: required for the null claim")
            if not 0 < exact("r_min", self.r_min) < 1:
                raise SizingError(f"r_min: must be in (0, 1), got {self.r_min!r}")
        elif self.r_min is not None:
            raise SizingError("r_min: only the null claim takes it")
        _open_unit("confidence", self.confidence)

    def text(self) -> str:
        if self.kind == "top":
            return f"a top driver of {self.metric}"
        return f"no parameter with |rho| >= {self.r_min} against {self.metric}"


Claim = ProbabilityClaim | PercentileClaim | PrecisionClaim | RankingClaim
OPTION = {
    ProbabilityClaim: "probability",
    PercentileClaim: "percentile",
    PrecisionClaim: "precision",
    RankingClaim: "ranking",
}


# --- results --------------------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class ParameterInterval:
    """One parameter of a ranking: Spearman's rho, its nominal simultaneous interval, and the |rho|
    bounds (NaN, with ``note``, when no interval is given)."""

    parameter: str
    rho: float
    lower: float
    upper: float
    abs_lower: float
    abs_upper: float
    note: str = ""


@dataclasses.dataclass(frozen=True)
class Check:
    """One claim checked against one batch (module docstring). ``lower``/``upper`` are the
    one-sided bounds for a calibrated claim and the nominal interval otherwise; ``verdict`` is
    ``None`` for a nominal claim; ``needed`` is ``None`` with the reason in ``notes``."""

    option: str
    tier: Literal["calibrated", "nominal"]
    claim: str
    n: int
    errors: int
    estimate: float
    lower: float
    upper: float
    verdict: str | None
    needed: int | None
    minimum: int
    notes: tuple[str, ...] = ()
    parameters: tuple[ParameterInterval, ...] = ()

    def text(self) -> str:
        lines = [f"{self.option} ({self.tier}): {self.claim}"]
        lines.append(f"  runs analyzed: {self.n} (error rows not analyzed: {self.errors})")
        lines.append(f"  estimate: {_fmt(self.estimate)}")
        if self.tier == "calibrated":
            lines.append(f"  one-sided bounds: lower {_fmt(self.lower)}, upper {_fmt(self.upper)}")
            lines.append(f"  verdict: {self.verdict}")
        else:
            lines.append(f"  interval ({NOMINAL}): [{_fmt(self.lower)}, {_fmt(self.upper)}]")
        for row in self.parameters:
            note = f" ({row.note})" if row.note else ""
            lines.append(
                f"  {row.parameter}: rho {_fmt(row.rho)}, [{_fmt(row.lower)}, {_fmt(row.upper)}], "
                f"|rho| in [{_fmt(row.abs_lower)}, {_fmt(row.abs_upper)}]{note}"
            )
        needed = "none" if self.needed is None else str(self.needed)
        label = "" if self.tier == "calibrated" else f" ({NOMINAL})"
        lines.append(f"  needed: {needed}{label} (minimum {self.minimum})")
        lines += [f"  note: {note}" for note in self.notes]
        return "\n".join(lines)


def _fmt(value: float) -> str:
    return "nan" if math.isnan(value) else f"{value:.6g}"


@dataclasses.dataclass(frozen=True)
class Table:
    """Several claims checked against one batch. ``seed`` is the batch's master seed (from the spec
    its summary records), or ``None``."""

    batch: str
    seed: int | None
    rows: tuple[Check, ...]

    def text(self) -> str:
        seed = "unknown" if self.seed is None else str(self.seed)
        lines = [f"batch: {self.batch} (master seed {seed})", f"protocol: {PROTOCOL}"]
        lines += [row.text() for row in self.rows]
        return "\n".join(lines) + "\n"


def batch_seed(batch: mc_analysis.Batch) -> int | None:
    """The batch's master seed from the spec its summary records (``astra.montecarlo``'s record),
    or ``None`` (a synthetic batch, or one without a record)."""
    if batch.path is None:
        return None
    pq: Any = importlib.import_module("pyarrow.parquet")
    try:
        schema: Any = pq.read_schema(batch.path / montecarlo.SUMMARY)
    except (OSError, ValueError):
        return None
    metadata = cast(dict[bytes, bytes], schema.metadata or {})
    text = metadata.get(montecarlo.SPEC_KEY)
    if text is None:
        return None
    try:
        spec: Any = yaml.safe_load(text.decode("utf-8"))
    except (UnicodeDecodeError, yaml.YAMLError):
        return None
    seed: Any = cast(dict[str, Any], spec).get("seed") if isinstance(spec, dict) else None
    return seed if isinstance(seed, int) and not isinstance(seed, bool) else None


# --- checks ---------------------------------------------------------------------------------------


def check(batch: mc_analysis.Batch, claims: Sequence[Claim], *, bootstrap: bool = True) -> Table:
    """Every claim against ``batch``. Raises ``SizingError`` and
    ``astra.mc_analysis.CriterionError``."""
    rows = tuple(check_one(batch, claim, bootstrap=bootstrap) for claim in claims)
    name = str(batch.path) if batch.path is not None else "(synthetic)"
    return Table(name, batch_seed(batch), rows)


def check_one(batch: mc_analysis.Batch, claim: Claim, *, bootstrap: bool = True) -> Check:
    """One claim against ``batch`` (module docstring)."""
    if isinstance(claim, ProbabilityClaim):
        return _probability(batch, claim)
    if isinstance(claim, PercentileClaim):
        return _percentile(batch, claim)
    if isinstance(claim, PrecisionClaim):
        return _precision(batch, claim, bootstrap)
    return _ranking(batch, claim)


def _errors(batch: mc_analysis.Batch) -> int:
    return len(batch) - len(mc_analysis.analyzed_rows(batch))


def _calibrated(
    option: str,
    text: str,
    k: int,
    n: int,
    errors: int,
    p_max: Fraction,
    confidence: Fraction,
    producer_risk: Fraction,
    notes: tuple[str, ...] = (),
) -> Check:
    alpha = 1 - confidence
    minimum = _first_upper_meeting(0, p_max, alpha, 1, option)
    if n == 0:
        return Check(
            option,
            "calibrated",
            text,
            0,
            errors,
            math.nan,
            math.nan,
            math.nan,
            "undetermined",
            None,
            minimum,
            ("no analyzed run", *notes),
        )
    lower, upper = one_sided_bounds(k, n, confidence)
    if upper_meets(k, n, p_max, alpha):
        verdict = "met"
    elif lower_exceeds(k, n, p_max, alpha):
        verdict = "violated"
    else:
        verdict = "undetermined"
    rate = Fraction(k, n)
    extra: list[str] = []
    needed: int | None = None
    if _PROJECT.get():
        needed = producer_runs(rate, p_max, confidence, producer_risk)
        if needed is None:
            extra.append(
                f"no needed: the observed rate {float(rate):.6g} is not below {float(p_max):.6g}"
            )
        else:
            needed = max(needed, minimum)
    if n < minimum:
        extra.append(f"met is unreachable below {minimum} runs")
    return Check(
        option,
        "calibrated",
        text,
        n,
        errors,
        float(rate),
        lower,
        upper,
        verdict,
        needed,
        minimum,
        (*extra, *notes),
    )


def _probability(batch: mc_analysis.Batch, claim: ProbabilityClaim) -> Check:
    result = mc_analysis.outcomes(batch, claim.criterion)
    k = result.count(mc_analysis.FAIL)
    n = k + result.count(mc_analysis.PASS)
    return _calibrated(
        "probability",
        claim.text(),
        k,
        n,
        result.count(mc_analysis.ERROR),
        exact("p_max", claim.p_max),
        exact("confidence", claim.confidence),
        exact("producer_risk", claim.producer_risk),
    )


def _metric(batch: mc_analysis.Batch, column: str) -> FloatArray:
    return mc_analysis.metric(batch, column)


def _percentile(batch: mc_analysis.Batch, claim: PercentileClaim) -> Check:
    values = _metric(batch, claim.metric)
    limit = exact("limit", claim.limit)
    gamma = exact("quantile", claim.quantile)
    confidence = exact("confidence", claim.confidence)
    # Both operands as written: a value is its shortest repr, as the limit is (review G-001), so a
    # value printed 0.1 equals a limit written 0.1 and is not an exceedance.
    observed = [exact("value", value) for value in values.tolist()]
    if claim.side == "upper":
        k = sum(1 for value in observed if value > limit)
    else:
        k = sum(1 for value in observed if value < limit)
    notes: list[str] = []
    bound = order_statistic_bound(values, gamma, confidence, claim.side)
    if bound is not None:
        index, value = bound
        side = "upper" if claim.side == "upper" else "lower"
        notes.append(
            f"information only: X_({index}) = {value:.6g} is a {side} bound on the quantile"
        )
    return _calibrated(
        "percentile",
        claim.text(),
        k,
        int(values.size),
        _errors(batch),
        1 - gamma,
        confidence,
        exact("producer_risk", claim.producer_risk),
        tuple(notes),
    )


def order_statistic_bound(
    values: FloatArray, gamma: Fraction, confidence: Fraction, side: str
) -> tuple[int, float] | None:
    """Information only. ``upper``: ``(r, X_(r))`` with r the smallest index such that
    ``P(Bin(n, gamma) <= r - 1) >= C``, an upper bound on ``q_gamma`` at C. ``lower``:
    ``(s, X_(s))`` with s the largest such that ``P(Bin(n, 1 - gamma) >= s) >= C``, a lower bound on
    ``q_(1 - gamma)``. ``None`` when n is too small."""
    n = int(values.size)
    if n == 0:
        return None
    ordered = np.sort(values)
    binom = _stats().binom
    level = float(confidence)
    if side == "upper":
        cdf = np.asarray(binom.cdf(np.arange(n), n, float(gamma)), dtype=np.float64)
        hits = np.flatnonzero(cdf >= level)
        if not hits.size:
            return None
        r = int(hits[0]) + 1
        return r, float(ordered[r - 1])
    sf = np.asarray(binom.sf(np.arange(n), n, float(1 - gamma)), dtype=np.float64)  # P(X >= s)
    hits = np.flatnonzero(sf >= level)
    if not hits.size:
        return None
    s = int(hits[-1]) + 1
    return s, float(ordered[s - 1])


# --- precision (nominal) --------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class Moments:
    """The estimator conventions (module docstring)."""

    n: int
    mean: float
    s: float
    kurtosis: float
    skewness: float


def is_constant(values: FloatArray) -> bool:
    """Every value equal to the first, compared exactly, before any moment arithmetic: rounding in
    the mean can leave a constant output a tiny nonzero spread (review G-002)."""
    return bool(values.size == 0 or np.all(values == values[0]))


def moments(values: FloatArray) -> Moments:
    """The estimator conventions; NaN kurtosis and skewness where ``m2`` or its powers vanish."""
    n = int(values.size)
    mean = float(values.mean())
    centered = values - mean
    m2 = float((centered**2).mean())
    m3 = float((centered**3).mean())
    m4 = float((centered**4).mean())
    s = float(math.sqrt(float((centered**2).sum()) / (n - 1))) if n > 1 else math.nan
    square = m2 * m2
    cube = m2**1.5
    kurtosis = m4 / square if square > 0 else math.nan  # m2 * m2 can underflow
    skewness = m3 / cube if cube > 0 else math.nan
    return Moments(n, mean, s, kurtosis, skewness)


def bonett_kurtosis(values: FloatArray) -> float:
    """Bonett's (2006) kurtosis estimate ``g4 = n sum (x - m)^4 / (sum (x - mean)^2)^2``, with
    ``m`` the mean trimmed by ``floor(n / (2 sqrt(n - 4)))`` values from each tail."""
    n = int(values.size)
    if is_constant(values):
        return math.nan  # a constant output: no kurtosis, and no interval (review G-002)
    ordered = np.sort(values)
    cut = math.floor(n / (2.0 * math.sqrt(n - 4)))
    trimmed = float(ordered[cut : n - cut].mean())
    squares = float(((values - values.mean()) ** 2).sum())
    denominator = squares * squares
    if not denominator > 0:
        return math.nan  # the spread underflows: no kurtosis, and no interval
    return n * float(((values - trimmed) ** 4).sum()) / denominator


def std_interval(s: float, g4: float, n: int, confidence: float) -> tuple[float, float] | None:
    """Bonett's (2006) interval for the standard deviation: ``sqrt(c s^2 exp(+-z se))`` with
    ``c = n / (n - z)`` and ``se = c sqrt([g4 - (n - 3) / n] / (n - 1))`` (module docstring), or
    ``None`` when ``g4 <= (n - 3) / n``."""
    spread = (g4 - (n - 3) / n) / (n - 1)
    if not spread > 0:
        return None
    z = float(_stats().norm.ppf((1.0 + confidence) / 2.0))
    c = n / (n - z)
    se = c * math.sqrt(spread)
    center = math.log(c * s * s)
    return math.sqrt(math.exp(center - z * se)), math.sqrt(math.exp(center + z * se))


def bootstrap_std(
    values: FloatArray, confidence: float, resamples: int = BOOTSTRAP
) -> tuple[float, float]:
    """The percentile-bootstrap interval of ``s``, resampled with ``astra.montecarlo.Rng(0)``."""
    rng = montecarlo.Rng(0)
    n = int(values.size)
    picks = np.empty((resamples, n), dtype=np.int64)
    for b in range(resamples):
        for i in range(n):
            picks[b, i] = rng.bits53() % n
    samples = values[picks]
    stds = samples.std(axis=1, ddof=1)
    tail = (1.0 - confidence) / 2.0
    lo, hi = np.quantile(stds, [tail, 1.0 - tail])
    return float(lo), float(hi)


def _nominal(
    text: str,
    n: int,
    errors: int,
    estimate: float,
    interval: tuple[float, float] | None,
    tolerance: float,
    project: Callable[[float], int | None],
    minimum: int,
    notes: list[str],
) -> Check:
    if interval is None:
        return Check(
            "precision",
            "nominal",
            text,
            n,
            errors,
            estimate,
            math.nan,
            math.nan,
            None,
            None,
            minimum,
            tuple(notes),
        )
    lower, upper = interval
    hw = max(upper - estimate, estimate - lower)
    needed: int | None = None
    if not _PROJECT.get():
        pass
    elif not tolerance > 0:
        notes.append("no projection: a relative tolerance of a zero estimate")
    elif hw == 0:
        notes.append("no projection: zero-width interval")
    else:
        projected = project(hw / tolerance)
        needed = None if projected is None else max(projected, minimum)
    notes.append(f"half-width {hw:.6g} against tolerance {tolerance:.6g}")
    return Check(
        "precision",
        "nominal",
        text,
        n,
        errors,
        estimate,
        lower,
        upper,
        None,
        needed,
        minimum,
        tuple(notes),
    )


def _scaled(n: int) -> Callable[[float], int]:
    return lambda ratio: math.ceil(n * ratio * ratio)


def _tolerance(claim: PrecisionClaim, estimate: float) -> float:
    tolerance = float(exact("tolerance", claim.tolerance))
    return tolerance * abs(estimate) if claim.relative else tolerance


def _precision(batch: mc_analysis.Batch, claim: PrecisionClaim, bootstrap: bool) -> Check:
    confidence = float(exact("confidence", claim.confidence))
    errors = _errors(batch)
    text = claim.text()
    if claim.statistic == "probability":
        result = mc_analysis.outcomes(batch, claim.criterion)
        k = result.count(mc_analysis.FAIL)
        n = k + result.count(mc_analysis.PASS)
        if n == 0:
            return _nominal(
                text, 0, errors, math.nan, None, 0.0, lambda _r: None, 1, ["no analyzed run"]
            )
        estimate = k / n
        tolerance = _tolerance(claim, estimate)

        def project(_ratio: float) -> int | None:
            if not 0 < tolerance < 0.5:
                return None
            return precision_runs(estimate, tolerance, claim.confidence)

        return _nominal(
            text,
            n,
            errors,
            estimate,
            clopper_pearson(k, n, claim.confidence),
            tolerance,
            project,
            1,
            [],
        )
    assert claim.metric is not None
    values = _metric(batch, claim.metric)
    n = int(values.size)
    if claim.statistic == "mean":
        if n < MEAN_MIN_N:
            return _nominal(
                text,
                n,
                errors,
                math.nan,
                None,
                0.0,
                lambda _r: None,
                MEAN_MIN_N,
                [f"too few runs: the mean needs {MEAN_MIN_N}"],
            )
        m = moments(values)
        t = float(_stats().t.ppf((1.0 + confidence) / 2.0, n - 1))
        hw = t * m.s / math.sqrt(n)
        return _nominal(
            text,
            n,
            errors,
            m.mean,
            (m.mean - hw, m.mean + hw),
            _tolerance(claim, m.mean),
            _scaled(n),
            MEAN_MIN_N,
            [],
        )
    if claim.statistic == "std":
        return _precision_std(text, values, errors, claim, confidence, bootstrap)
    if claim.statistic == "percentile":
        assert claim.quantile is not None
        gamma = float(exact("quantile", claim.quantile))
        minimum = percentile_interval_minimum(gamma, confidence)
        ordered = np.sort(values)
        if n < minimum:
            return _nominal(
                text,
                n,
                errors,
                math.nan,
                None,
                0.0,
                lambda _r: None,
                minimum,
                [f"too few runs for an interval: {minimum} needed"],
            )
        estimate = float(ordered[math.ceil(n * gamma) - 1])
        indices = percentile_interval_indices(n, gamma, confidence)
        assert indices is not None
        lo, hi = indices
        notes = ["the projection assumes a positive density at the quantile"]
        return _nominal(
            text,
            n,
            errors,
            estimate,
            (float(ordered[lo - 1]), float(ordered[hi - 1])),
            _tolerance(claim, estimate),
            _scaled(n),
            minimum,
            notes,
        )
    assert claim.parameter is not None
    return _precision_correlation(batch, claim, confidence, text, errors)


def _precision_std(
    text: str,
    values: FloatArray,
    errors: int,
    claim: PrecisionClaim,
    confidence: float,
    bootstrap: bool,
) -> Check:
    n = int(values.size)
    if n < STD_MIN_N:
        return _nominal(
            text,
            n,
            errors,
            math.nan,
            None,
            0.0,
            lambda _r: None,
            STD_MIN_N,
            [f"too few runs: the std needs {STD_MIN_N}"],
        )
    notes: list[str] = []
    if is_constant(values):
        notes.append("no interval: the metric is constant")
        return _nominal(text, n, errors, 0.0, None, 0.0, lambda _r: None, STD_MIN_N, notes)
    m = moments(values)
    if abs(m.skewness) > 1.0:
        notes.append(f"flag: skewed (skewness {m.skewness:.3g})")
    if m.kurtosis - 3.0 > 2.0:
        notes.append(f"flag: heavy-tailed (excess kurtosis {m.kurtosis - 3.0:.3g})")
    g4 = bonett_kurtosis(values)
    interval = std_interval(m.s, g4, n, confidence)
    if interval is None:
        reason = (
            "the spread underflows in double precision"
            if math.isnan(g4)
            else "Bonett's kurtosis estimate is at most (n - 3) / n"
        )
        notes.append(f"no interval: {reason}")
        return _nominal(text, n, errors, m.s, None, 0.0, lambda _r: None, STD_MIN_N, notes)
    tolerance = _tolerance(claim, m.s)
    if bootstrap:
        lo, hi = bootstrap_std(values, confidence)
        hw = max(interval[1] - m.s, m.s - interval[0])
        hw_boot = max(hi - m.s, m.s - lo)
        if hw > 0 and abs(hw_boot - hw) / hw > BOOTSTRAP_DISAGREEMENT:
            notes.append(f"flag: bootstrap disagrees (half-width {hw_boot:.6g} against {hw:.6g})")
        else:
            notes.append(
                f"bootstrap half-width {hw_boot:.6g} (agreement does not imply calibration)"
            )

    def project(_ratio: float) -> int | None:
        return std_runs(m.s, g4, tolerance, confidence)

    return _nominal(text, n, errors, m.s, interval, tolerance, project, STD_MIN_N, notes)


def std_half_widths(s: float, g4: float, n: IntArray, confidence: float) -> FloatArray:
    """The half-width of Bonett's interval (the larger distance from ``s``) at each run count in
    ``n``, with ``s`` and ``g4`` held; ``inf`` where the interval does not exist."""
    z = float(_stats().norm.ppf((1.0 + confidence) / 2.0))
    size = n.astype(np.float64)
    spread = (g4 - (size - 3.0) / size) / (size - 1.0)
    c = size / (size - z)
    with np.errstate(invalid="ignore"):
        se = c * np.sqrt(spread)
        center = np.log(c * s * s)
        lower = np.sqrt(np.exp(center - z * se))
        upper = np.sqrt(np.exp(center + z * se))
    widths = np.maximum(upper - s, s - lower)
    return np.where(spread > 0, widths, np.inf)


def std_runs(s: float, g4: float, tolerance: float, confidence: float) -> int | None:
    """The smallest N whose Bonett half-width is at most ``tolerance``, ``s`` and ``g4`` held
    (module docstring)."""

    def meets(n: IntArray) -> BoolArray:
        return std_half_widths(s, g4, n, confidence) <= tolerance

    return _search(meets, 5, "std precision")


def percentile_interval_indices(n: int, gamma: float, confidence: float) -> tuple[int, int] | None:
    """``(l, u)``, 1-based, of the two-sided order-statistic interval for ``q_gamma`` at
    ``confidence``: l the largest with ``P(Bin(n, gamma) <= l - 1) <= (1 - C) / 2``, u the smallest
    with ``P(Bin(n, gamma) <= u - 1) >= (1 + C) / 2``. ``None`` when either is out of 1..n."""
    tail = (1.0 - confidence) / 2.0
    cdf = np.asarray(_stats().binom.cdf(np.arange(n), n, gamma), dtype=np.float64)  # at r - 1
    low = np.flatnonzero(cdf <= tail)
    high = np.flatnonzero(cdf >= 1.0 - tail)
    if not low.size or not high.size:
        return None
    return int(low[-1]) + 1, int(high[0]) + 1


def percentile_interval_minimum(gamma: float, confidence: float) -> int:
    """The smallest n with a two-sided order-statistic interval for ``q_gamma`` at
    ``confidence``."""
    tail = (1.0 - confidence) / 2.0
    n = max(1, math.ceil(math.log(tail) / math.log(max(gamma, 1.0 - gamma))) - 1)
    while percentile_interval_indices(n, gamma, confidence) is None:
        n += 1
    return n


# --- Spearman with the jackknife (nominal) --------------------------------------------------------


def _average_ranks(values: FloatArray) -> FloatArray:
    return np.asarray(_stats().rankdata(values), dtype=np.float64)


def spearman_jackknife(x: FloatArray, y: FloatArray) -> tuple[float, FloatArray]:
    """Spearman's rho of ``x`` and ``y`` and its n leave-one-out values, from average ranks: leaving
    out point i lowers every larger value's rank by 1 and every tied value's by 1/2."""
    n = int(x.size)
    rx = _average_ranks(x)
    ry = _average_ranks(y)
    rho = _pearson(rx, ry)
    keep = ~np.eye(n, dtype=bool)

    def loo(values: FloatArray, ranks: FloatArray) -> FloatArray:
        above = (values[None, :] > values[:, None]).astype(np.float64)
        tied = (values[None, :] == values[:, None]).astype(np.float64) * keep
        return ranks[None, :] - above - 0.5 * tied  # row i: ranks without point i

    lx = loo(x, rx)
    ly = loo(y, ry)
    count = n - 1
    mx = (lx * keep).sum(axis=1) / count
    my = (ly * keep).sum(axis=1) / count
    dx = (lx - mx[:, None]) * keep
    dy = (ly - my[:, None]) * keep
    sxy = (dx * dy).sum(axis=1)
    sxx = (dx * dx).sum(axis=1)
    syy = (dy * dy).sum(axis=1)
    with np.errstate(invalid="ignore", divide="ignore"):
        rhos = sxy / np.sqrt(sxx * syy)
    return rho, np.asarray(rhos, dtype=np.float64)


def _pearson(a: FloatArray, b: FloatArray) -> float:
    a = a - a.mean()
    b = b - b.mean()
    denominator = math.sqrt(float(a @ a) * float(b @ b))
    return float(a @ b) / denominator if denominator > 0 else math.nan


def jackknife_variance(z_loo: FloatArray) -> float:
    """``((n - 1) / n) sum (z_i - mean z)^2``."""
    n = int(z_loo.size)
    return float((n - 1) / n * ((z_loo - z_loo.mean()) ** 2).sum())


@dataclasses.dataclass(frozen=True)
class _Rank:
    name: str
    rho: float
    z: float
    v: float  # jackknife variance of z; NaN when there is no interval
    note: str


def _rank(name: str, x: FloatArray, y: FloatArray) -> _Rank:
    n = int(x.size)
    if n < 3:
        return _Rank(name, math.nan, math.nan, math.nan, f"too few runs: {RANK_MIN_N} needed")
    rho, loo = spearman_jackknife(x, y)
    if math.isnan(rho):
        return _Rank(name, rho, math.nan, math.nan, "constant")
    if n < RANK_MIN_N:
        return _Rank(name, rho, math.nan, math.nan, f"no interval: {RANK_MIN_N} runs needed")
    if abs(rho) >= 1.0:
        return _Rank(name, rho, math.nan, math.nan, "no interval: |rho| = 1")
    if not bool(np.all(np.isfinite(loo))) or bool(np.any(np.abs(loo) >= 1.0)):
        return _Rank(name, rho, math.nan, math.nan, "no interval: a leave-one-out |rho| = 1")
    return _Rank(name, rho, math.atanh(rho), jackknife_variance(np.arctanh(loo)), "")


def _interval(rank: _Rank, c: float, scale: float = 1.0) -> ParameterInterval:
    if math.isnan(rank.v):
        nan = math.nan
        return ParameterInterval(rank.name, rank.rho, nan, nan, nan, nan, rank.note)
    half = c * math.sqrt(rank.v * scale)
    lower = math.tanh(rank.z - half)
    upper = math.tanh(rank.z + half)
    abs_lower = 0.0 if lower <= 0.0 <= upper else min(abs(lower), abs(upper))
    return ParameterInterval(
        rank.name, rank.rho, lower, upper, abs_lower, max(abs(lower), abs(upper))
    )


def _precision_correlation(
    batch: mc_analysis.Batch, claim: PrecisionClaim, confidence: float, text: str, errors: int
) -> Check:
    assert claim.metric is not None
    assert claim.parameter is not None
    if claim.parameter not in batch.parameters:
        raise SizingError(f"parameter: {claim.parameter!r} is not a ranked parameter of the batch")
    rows = mc_analysis.analyzed_rows(batch)
    x = mc_analysis.parameter_values(batch, claim.parameter, rows)
    y = _metric(batch, claim.metric)
    rank = _rank(claim.parameter, x, y)
    n = int(x.size)
    if math.isnan(rank.v):
        return _nominal(
            text, n, errors, rank.rho, None, 0.0, lambda _r: None, RANK_MIN_N, [rank.note]
        )
    c = float(_stats().norm.ppf((1.0 + confidence) / 2.0))
    row = _interval(rank, c)
    tolerance = _tolerance(claim, rank.rho)

    def project(_ratio: float) -> int | None:
        def meets(size: IntArray) -> BoolArray:
            half = c * np.sqrt(rank.v * n / size.astype(np.float64))
            lo = np.tanh(rank.z - half)
            hi = np.tanh(rank.z + half)
            return np.maximum(hi - rank.rho, rank.rho - lo) <= tolerance

        return _search(meets, 1, "correlation precision")

    return _nominal(
        text, n, errors, rank.rho, (row.lower, row.upper), tolerance, project, RANK_MIN_N, []
    )


def _family(batch: mc_analysis.Batch, rows: Sequence[int]) -> list[str]:
    family: list[str] = []
    for name in batch.parameters:
        values = mc_analysis.parameter_values(batch, name, rows)
        if values.size and not bool(np.all(values == values[0])):
            family.append(name)
    return family


def _ranking(batch: mc_analysis.Batch, claim: RankingClaim) -> Check:
    confidence = float(exact("confidence", claim.confidence))
    rows = mc_analysis.analyzed_rows(batch)
    y = _metric(batch, claim.metric)
    n = len(rows)
    errors = _errors(batch)
    family = _family(batch, rows)
    notes: list[str] = []
    if not family:
        notes.append("no ranked parameter varies: nothing to rank")
        return Check(
            "ranking",
            "nominal",
            claim.text(),
            n,
            errors,
            math.nan,
            math.nan,
            math.nan,
            None,
            None,
            RANK_MIN_N,
            tuple(notes),
        )
    m = len(family)
    c = float(_stats().norm.ppf(1.0 - (1.0 - confidence) / (2.0 * m)))
    ranks = [_rank(name, mc_analysis.parameter_values(batch, name, rows), y) for name in family]
    intervals = tuple(_interval(rank, c) for rank in ranks)
    needed: int | None = None
    blocked = [rank for rank in ranks if math.isnan(rank.v)]
    if not _PROJECT.get():
        pass  # convergence: intervals only
    elif blocked:
        notes.append(f"no projection: {blocked[0].name}: {blocked[0].note}")
    elif claim.kind == "top":
        needed = _top_runs(ranks, c, n, notes)
    else:
        assert claim.r_min is not None
        needed = _null_runs(ranks, c, n, float(exact("r_min", claim.r_min)), notes)
    if needed is not None:
        needed = max(needed, RANK_MIN_N)
    notes.append(f"simultaneous intervals over m = {m} parameters (Bonferroni), jackknife variance")
    top = max(ranks, key=lambda rank: -1.0 if math.isnan(rank.rho) else abs(rank.rho))
    return Check(
        "ranking",
        "nominal",
        claim.text(),
        n,
        errors,
        top.rho,
        math.nan,
        math.nan,
        None,
        needed,
        RANK_MIN_N,
        tuple(notes),
        intervals,
    )


def _abs_bounds(rank: _Rank, c: float, scale: FloatArray) -> tuple[FloatArray, FloatArray]:
    half = c * np.sqrt(rank.v * scale)
    lower = np.tanh(rank.z - half)
    upper = np.tanh(rank.z + half)
    spans = (lower <= 0.0) & (upper >= 0.0)
    abs_lower = np.where(spans, 0.0, np.minimum(np.abs(lower), np.abs(upper)))
    return abs_lower, np.maximum(np.abs(lower), np.abs(upper))


def _top_runs(ranks: list[_Rank], c: float, n: int, notes: list[str]) -> int | None:
    ordered = sorted(ranks, key=lambda rank: -abs(rank.rho))
    if len(ordered) > 1 and abs(ordered[0].rho) == abs(ordered[1].rho):
        notes.append("no projection: the two largest |rho| tie")
        return None
    first, others = ordered[0], ordered[1:]

    def meets(size: IntArray) -> BoolArray:
        scale = n / size.astype(np.float64)
        lower, _upper = _abs_bounds(first, c, scale)
        best = np.zeros(size.size)
        for rank in others:
            best = np.maximum(best, _abs_bounds(rank, c, scale)[1])
        return lower > best

    return _search(meets, 4, "ranking top")


def _null_runs(ranks: list[_Rank], c: float, n: int, r_min: float, notes: list[str]) -> int | None:
    reached = [rank for rank in ranks if abs(rank.rho) >= r_min]
    if reached:
        notes.append(
            f"no projection: {reached[0].name} has |rho| {abs(reached[0].rho):.6g} >= {r_min:g}"
        )
        return None

    def meets(size: IntArray) -> BoolArray:
        scale = n / size.astype(np.float64)
        ok = np.ones(size.size, dtype=bool)
        for rank in ranks:
            ok &= _abs_bounds(rank, c, scale)[1] < r_min
        return ok

    return _search(meets, 4, "ranking null")


# --- claims files ---------------------------------------------------------------------------------

_CLAIM_KEYS: dict[str, tuple[type, set[str], set[str]]] = {
    "probability": (ProbabilityClaim, {"p_max"}, {"criterion", "confidence", "producer_risk"}),
    "percentile": (
        PercentileClaim,
        {"metric", "quantile", "limit"},
        {"side", "confidence", "producer_risk"},
    ),
    "precision": (
        PrecisionClaim,
        {"statistic", "tolerance"},
        {"relative", "metric", "criterion", "parameter", "quantile", "confidence"},
    ),
    "ranking": (RankingClaim, {"metric"}, {"kind", "r_min", "confidence"}),
}


def load_claims(path: str | Path) -> list[Claim]:
    """A claims file: ``claims:``, a list of one-key maps ``{<option>: {fields}}``, every map
    closed. Numbers are read as written (``yaml.safe_load``'s floats by their shortest repr).
    Raises ``SizingError`` naming the file and the key."""
    path = Path(path)
    try:
        data: Any = yaml.safe_load(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, yaml.YAMLError) as error:
        raise SizingError(f"{path}: cannot read the claims ({error})") from error
    if not isinstance(data, dict) or set(cast(dict[Any, Any], data)) != {"claims"}:
        raise SizingError(f"{path}: must be a map with the single key 'claims'")
    items: Any = cast(dict[str, Any], data)["claims"]
    if not isinstance(items, list) or not items:
        raise SizingError(f"{path}: claims: must be a non-empty list")
    claims: list[Claim] = []
    for i, item in enumerate(cast(list[Any], items)):
        where = f"{path}: claims[{i}]"
        if not isinstance(item, dict) or len(cast(dict[Any, Any], item)) != 1:
            raise SizingError(f"{where}: must be a map with one option key")
        option, fields = next(iter(cast(dict[Any, Any], item).items()))
        if option not in _CLAIM_KEYS:
            raise SizingError(f"{where}: unknown option {option!r}")
        kind, required, optional = _CLAIM_KEYS[option]
        if not isinstance(fields, dict):
            raise SizingError(f"{where}.{option}: must be a map")
        given = set(cast(dict[str, Any], fields))
        for key in sorted(given - required - optional):
            raise SizingError(f"{where}.{option}: unknown key {key!r}")
        for key in sorted(required - given):
            raise SizingError(f"{where}.{option}: missing key {key!r}")
        try:
            claims.append(cast(Claim, kind(**cast(dict[str, Any], fields))))
        except (SizingError, TypeError) as error:
            raise SizingError(f"{where}.{option}: {error}") from error
    return claims


# --- convergence ----------------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class Convergence:
    """A claim's statistic at every prefix (module docstring). ``rows[j]`` is the check of the first
    ``j + 1`` rows; ``reachable[j]`` says whether ``met`` is reachable there (calibrated)."""

    claim: str
    rows: tuple[Check, ...]

    def text(self, every: int = 10) -> str:
        every = _count("every", every, 1)
        size = len(self.rows)
        first = self.rows[0] if self.rows else None
        lines = [f"convergence: {self.claim}"]
        if first is not None and first.tier == "nominal":
            lines.append(f"intervals are {NOMINAL}")
        header = f"{'runs':>6} {'n':>6} {'estimate':>12} {'lower':>12} {'upper':>12}"
        if first is not None and first.tier == "calibrated":
            header += f" {'verdict':>12} {'met reachable':>13}"
        lines.append(header)
        for j, row in enumerate(self.rows):
            length = j + 1
            if length % every and length != size:
                continue
            line = (
                f"{length:>6} {row.n:>6} {_fmt(row.estimate):>12} {_fmt(row.lower):>12} "
                f"{_fmt(row.upper):>12}"
            )
            if row.tier == "calibrated":
                line += f" {row.verdict:>12} {'yes' if row.n >= row.minimum else 'no':>13}"
            for parameter in row.parameters:
                line += (
                    f"  {parameter.parameter} [{_fmt(parameter.lower)}, {_fmt(parameter.upper)}]"
                )
            lines.append(line)
        return "\n".join(lines) + "\n"


def prefix(batch: mc_analysis.Batch, length: int) -> mc_analysis.Batch:
    """The first ``length`` rows of ``batch`` (its run order) as a batch."""
    columns = {name: list(values)[:length] for name, values in batch.columns.items()}
    return mc_analysis.Batch(
        batch.path, columns, batch.numeric, batch.parameters, batch.seed_parameters
    )


def _prefix_check(batch: mc_analysis.Batch, length: int, claim: Claim) -> Check:
    """``claim`` on the first ``length`` rows: bounds, intervals and verdicts, no sizing search."""
    token = _PROJECT.set(False)
    try:
        return check_one(prefix(batch, length), claim, bootstrap=False)
    finally:
        _PROJECT.reset(token)


def convergence(batch: mc_analysis.Batch, claim: Claim) -> Convergence:
    """``claim`` checked at every prefix of ``batch`` (no bootstrap per prefix)."""
    rows = tuple(_prefix_check(batch, length, claim) for length in range(1, len(batch) + 1))
    return Convergence(claim.text(), rows)


def option_of(claim: Claim) -> str:
    return OPTION[type(claim)]
