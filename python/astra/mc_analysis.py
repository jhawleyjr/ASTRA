"""astra.mc_analysis: what a Monte Carlo batch shows, and which dispersed parameters drive its
outcome (M6 PR 6, docs/plans/m6-pr6-mc-analysis.md; docs/plans/m6-analysis-tooling.md Q7,
Q8; ADR-0025, ADR-0027, ADR-0010).

A batch is the directory ``astra.montecarlo.run`` wrote: ``summary.parquet`` plus ``run_<i>.h5`` for
every ``ok`` and ``halt`` row (``load_batch``). Tools read it and never modify it. Every result is a
deterministic function of the batch and the arguments.

Parameters (D3). The batch's dispersions are the summary columns between ``status`` and
``error_type`` (``astra.montecarlo``'s schema), in spec order. The int64 ones are ``seed``
dispersions: they are listed but never ranked or plotted, because a seed is not an ordered quantity.
The others are the *parameters*. A parameter constant over the analyzed rows is listed as
``constant`` and leaves every family. The analyzed rows are the non-``error`` rows; ``error`` rows
are counted separately, never as failures, and drawn in their own color.

Outcome (D4). Each analyzed row passes or fails a criterion; the default is ``status == halt``. A
criterion is a closed expression, never evaluated Python::

    expr := cmp (("and" | "or") cmp)*         ("and" binds tighter than "or"; no parentheses)
    cmp  := COLUMN OP NUMBER | "status" ("==" | "!=") ("ok" | "halt")
    OP   := "<" | "<=" | ">" | ">=" | "==" | "!="

``COLUMN`` is a numeric summary column, a dotted name such as ``max.v0.derived.alpha`` or
``vehicle.models.0.force_x``; ``NUMBER`` is a finite decimal or exponent literal with an optional
sign. An unknown column, operator or token, a string column, a non-numeric value, a missing or
trailing comparison, and a null or NaN in a referenced column on an analyzed row are each a
``CriterionError`` naming it. A continuous *metric* (``metric``) is a numeric column under the
same rules that is not a dispersion, ``run`` or ``seed``.

Methods (D5, D6). Each family ranks the tested parameters by p ascending, then |effect| descending,
then spec order, and applies the Benjamini-Hochberg step-up procedure at ``q`` across the family.
Every row reports the effect, the raw p and the BH decision, and the ranking reports the family
size ``m``. Below a method's minimum, its result is a stated line with no ranking, never an error.

- ``ks_ranking``: two-sample Kolmogorov-Smirnov (``scipy.stats.ks_2samp``, two-sided,
  ``method="auto"``) of the failing against the passing draws; the effect is ``D``. At least
  ``MIN_GROUP`` failing and ``MIN_GROUP`` passing rows.
- ``spearman_ranking``: Spearman's rho against the metric, p from ``scipy.stats.spearmanr``. At
  least 3 rows.
- ``prcc_ranking``: partial rank correlation: every parameter and the metric are rank-transformed
  (average ranks for ties), the other parameters (with an intercept) are regressed out of both by
  least squares, and the residuals are correlated (Pearson). p from a t-test with ``n - 1 -
  rank(Z)`` degrees of freedom, ``Z`` the intercept and the other parameters' ranks. A parameter
  whose ranks the others explain (residual variance at most ``COLLINEAR`` of its own) is
  ``collinear`` and leaves the family. At least ``m + 3`` rows.
- Without a metric, Spearman and PRCC are not run (a stated line); they never rank against the 0/1
  outcome.
- BH controls the false-discovery rate under independence or positive regression dependence. The
  KS and Spearman statistics are independent conditional on the outcome or the metric, because
  ``astra.montecarlo`` draws every dispersion independently. PRCC residuals share every
  parameter, so its BH is approximate.
- ``fit_tree``: an in-house classification tree (Q8), descriptive (no p-value), to surface
  interactions (D7). Gini impurity, depth at most ``MAX_DEPTH``; each parameter's candidate
  thresholds are ``lo + k (hi - lo) / GRID`` for k = 1 .. GRID - 1, ``[lo, hi]`` over the analyzed
  rows; a split ``x <= t`` / ``x > t`` must leave ``MIN_LEAF`` rows per side and strictly lower the
  impurity, computed exactly (``Fraction``). Ties go to the earlier parameter, then the smaller
  threshold. A leaf predicts fail only when its fails strictly outnumber its passes. A split whose
  two sides are leaves predicting the same class changes no prediction and is collapsed into one
  leaf, so a rule never carries a condition that decides nothing. Its rules are the fail leaves'
  paths, each reduced to the tightest bound per parameter and direction.

Figures (D8). matplotlib, under ``astra.plot``'s pinned style (``pinned_style("mc", <figure>)``),
every legend outside its axes (``astra.plot.legend_outside``):

- ``mc-hist``: one panel per parameter, bins ``numpy.histogram_bin_edges(every row's draw,
  bins=HIST_BINS)``, stacked pass, fail, error.
- ``mc-scatter``: the lower triangle of every parameter pair, pass, fail and error in ``PASS``,
  ``FAIL`` and ``ERROR`` (error with an ``x`` marker). Needs two parameters.
- ``mc-envelope-<signal>``: across the run logs, by frame number in the signal's rate group (x =
  ``frame * dt``, ADR-0010). A run contributes at a frame if and only if its log holds that row;
  nothing is interpolated or padded. Lines for the minimum, the 5th percentile, the median, the
  95th percentile and the maximum (``numpy.percentile``, linear) over exactly the runs that reached
  each frame, each failing run labeled ``run <i>``, and a second panel with that count. The legend
  shows at most ``MAX_LEGEND_RUNS`` failing runs (the lowest indices) and then ``and k more``.
  Logs are read strictly (``astra.log.load``) one at a time. A missing or refused log of an ``ok``
  or ``halt`` row, a ``dt`` or a divisor that differs from the first log's, and a signal no log
  holds are each an ``EnvelopeError`` naming the run or the signal.

``render_figures`` follows ``astra.plot.render``'s file rules: everything is resolved and every
target checked before anything is written, each file is created exclusively, and a failed write
deletes only the files this call created.
"""

from __future__ import annotations

import dataclasses
import importlib
import math
import re
from collections.abc import Mapping, Sequence
from fractions import Fraction
from pathlib import Path
from typing import TYPE_CHECKING, Any, Literal, cast

import numpy as np
import numpy.typing as npt

from astra import log, montecarlo, plot

if TYPE_CHECKING:
    from matplotlib.figure import Figure as MatplotlibFigure

FloatArray = npt.NDArray[np.float64]
IntArray = npt.NDArray[np.int64]

PASS = "pass"
FAIL = "fail"
ERROR = "error"
OUTCOMES = (PASS, FAIL, ERROR)
COLORS = {PASS: "#1f77b4", FAIL: "#d62728", ERROR: "#7f7f7f"}

DEFAULT_CRITERION = "status == halt"
DEFAULT_Q = 0.05
MIN_GROUP = 5  # failing and passing rows that KS and the tree need, each
MIN_SPEARMAN = 3
PRCC_EXTRA = 3  # PRCC needs at least m + PRCC_EXTRA rows
COLLINEAR = 1e-12  # relative residual variance at or below which a parameter is collinear
MAX_DEPTH = 3
GRID = 20
MIN_LEAF = 5
HIST_BINS = 20
PERCENTILES = (5.0, 50.0, 95.0)
MAX_LEGEND_RUNS = 10

_OPERATORS = ("<=", ">=", "==", "!=", "<", ">")
_OPERATOR_CHARS = frozenset("<>=!~")
_NUMBER = re.compile(r"[-+]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][-+]?[0-9]+)?")
_COLUMN = re.compile(r"[A-Za-z_][A-Za-z0-9_.]*")
_STATUSES = ("ok", "halt")


class BatchError(Exception):
    """The batch cannot be read as an ``astra.montecarlo`` batch; the message names the file."""


class CriterionError(Exception):
    """A criterion or metric is refused; the message names the offending token or column."""


class EnvelopeError(Exception):
    """An envelope cannot be built; the message names the run or the signal."""


def _stats() -> Any:
    # scipy ships no type information (no py.typed), so it is handled as Any at this boundary.
    return importlib.import_module("scipy.stats")


# --- the batch ------------------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True, eq=False)
class Batch:
    """A batch's summary, column by column in row order. ``numeric`` names the numeric columns;
    ``parameters`` are the ranked dispersions and ``seed_parameters`` the seed ones, each in spec
    order. ``path`` is the batch directory (``None`` for a synthetic batch, which has no logs)."""

    path: Path | None
    columns: Mapping[str, Sequence[Any]]
    numeric: frozenset[str]
    parameters: tuple[str, ...]
    seed_parameters: tuple[str, ...]

    def __len__(self) -> int:
        return len(self.columns["run"])

    @property
    def runs(self) -> list[int]:
        return [int(run) for run in self.columns["run"]]

    def value(self, column: str, row: int) -> Any:
        return self.columns[column][row]


def load_batch(directory: str | Path) -> Batch:
    """Reads ``<directory>/summary.parquet`` (module docstring, Parameters). Raises
    ``BatchError``."""
    directory = Path(directory)
    path = directory / montecarlo.SUMMARY
    pq: Any = importlib.import_module("pyarrow.parquet")
    pa: Any = importlib.import_module("pyarrow")
    try:
        table: Any = pq.read_table(path)
    except (OSError, ValueError) as error:  # pyarrow's ArrowInvalid is a ValueError
        raise BatchError(f"{path}: cannot read the summary ({error})") from error
    names = [str(name) for name in table.schema.names]
    for required in ("run", "status", "error_type"):
        if required not in names:
            raise BatchError(f"{path}: no {required!r} column: not an astra.montecarlo summary")
    dispersions = names[names.index("status") + 1 : names.index("error_type")]
    numeric = frozenset(
        name
        for name in names
        if pa.types.is_integer(table.schema.field(name).type)
        or pa.types.is_floating(table.schema.field(name).type)
    )
    seeds = tuple(
        name for name in dispersions if pa.types.is_integer(table.schema.field(name).type)
    )
    parameters = tuple(name for name in dispersions if name not in seeds)
    columns = cast(dict[str, list[Any]], table.to_pydict())
    return Batch(directory, columns, numeric, parameters, seeds)


def from_columns(
    columns: Mapping[str, Sequence[Any]],
    parameters: Sequence[str],
    seed_parameters: Sequence[str] = (),
) -> Batch:
    """A synthetic batch (no logs). ``columns`` needs ``run`` and ``status``; a column is numeric
    when every non-null value is an int or a float (never a bool)."""
    for required in ("run", "status", *parameters, *seed_parameters):
        if required not in columns:
            raise BatchError(f"synthetic batch: no {required!r} column")
    lengths = {len(values) for values in columns.values()}
    if len(lengths) != 1:
        raise BatchError(f"synthetic batch: columns of different lengths {sorted(lengths)}")
    numeric = frozenset(
        name
        for name, values in columns.items()
        if all(
            value is None or (isinstance(value, int | float) and not isinstance(value, bool))
            for value in values
        )
    )
    return Batch(
        None,
        {name: list(values) for name, values in columns.items()},
        numeric,
        tuple(parameters),
        tuple(seed_parameters),
    )


def analyzed_rows(batch: Batch) -> list[int]:
    """The row positions of the non-``error`` rows."""
    return [row for row in range(len(batch)) if batch.value("status", row) != "error"]


def parameter_values(batch: Batch, name: str, rows: Sequence[int]) -> FloatArray:
    values = [batch.value(name, row) for row in rows]
    for row, value in zip(rows, values, strict=True):
        if value is None or not math.isfinite(float(value)):
            raise BatchError(
                f"{name}: run {batch.value('run', row)} has no finite draw ({value!r})"
            )
    return np.asarray(values, dtype=np.float64)


# --- outcome --------------------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class Comparison:
    column: str
    operator: str
    value: float | str  # a number, or "ok"/"halt" for the status atom


@dataclasses.dataclass(frozen=True)
class Criterion:
    """``text`` as written; ``disjuncts`` is an OR of ANDs of comparisons."""

    text: str
    disjuncts: tuple[tuple[Comparison, ...], ...]

    @property
    def columns(self) -> list[str]:
        seen: list[str] = []
        for conjunct in self.disjuncts:
            for comparison in conjunct:
                if comparison.column != "status" and comparison.column not in seen:
                    seen.append(comparison.column)
        return seen


@dataclasses.dataclass(frozen=True)
class Outcomes:
    """One label per summary row (``PASS``, ``FAIL`` or ``ERROR``), under ``criterion``."""

    criterion: str
    labels: tuple[str, ...]
    runs: tuple[int, ...]

    def rows(self, label: str) -> list[int]:
        return [row for row, value in enumerate(self.labels) if value == label]

    def runs_of(self, label: str) -> list[int]:
        return [self.runs[row] for row in self.rows(label)]

    def count(self, label: str) -> int:
        return self.labels.count(label)


def _tokens(text: str) -> list[str]:
    tokens: list[str] = []
    i = 0
    while i < len(text):
        char = text[i]
        if char.isspace():
            i += 1
            continue
        j = i
        if char in _OPERATOR_CHARS:
            while j < len(text) and text[j] in _OPERATOR_CHARS:
                j += 1
        else:
            while j < len(text) and not text[j].isspace() and text[j] not in _OPERATOR_CHARS:
                j += 1
        tokens.append(text[i:j])
        i = j
    return tokens


def parse_criterion(text: str, batch: Batch) -> Criterion:
    """``text`` parsed against ``batch``'s columns (module docstring, Outcome). Raises
    ``CriterionError`` naming the offending token."""
    tokens = _tokens(text)
    if not tokens:
        raise CriterionError("criterion: empty")
    disjuncts: list[tuple[Comparison, ...]] = []
    conjunct: list[Comparison] = []
    i = 0
    while True:
        if i + 3 > len(tokens):
            after = f" after {tokens[i - 1]!r}" if i else ""
            raise CriterionError(f"criterion {text!r}: missing a comparison{after}")
        conjunct.append(_comparison(text, batch, tokens[i], tokens[i + 1], tokens[i + 2]))
        i += 3
        if i == len(tokens):
            break
        joiner = tokens[i]
        if joiner == "or":
            disjuncts.append(tuple(conjunct))
            conjunct = []
        elif joiner != "and":
            raise CriterionError(f"criterion {text!r}: expected 'and' or 'or', got {joiner!r}")
        i += 1
    disjuncts.append(tuple(conjunct))
    return Criterion(text, tuple(disjuncts))


def _comparison(text: str, batch: Batch, column: str, operator: str, value: str) -> Comparison:
    where = f"criterion {text!r}"
    if _COLUMN.fullmatch(column) is None or column in ("and", "or"):
        raise CriterionError(f"{where}: {column!r} is not a column name")
    if operator not in _OPERATORS:
        raise CriterionError(f"{where}: unknown operator {operator!r}")
    if column == "status":
        if operator not in ("==", "!="):
            raise CriterionError(f"{where}: status takes == or !=, not {operator!r}")
        if value not in _STATUSES:
            raise CriterionError(f"{where}: status is compared with ok or halt, not {value!r}")
        return Comparison(column, operator, value)
    if column not in batch.columns:
        raise CriterionError(f"{where}: unknown column {column!r}")
    if column not in batch.numeric:
        raise CriterionError(f"{where}: column {column!r} is not numeric")
    if _NUMBER.fullmatch(value) is None:
        raise CriterionError(f"{where}: {value!r} is not a number")
    number = float(value)
    if not math.isfinite(number):
        raise CriterionError(f"{where}: {value!r} is not finite")
    return Comparison(column, operator, number)


def _holds(comparison: Comparison, actual: Any) -> bool:
    expected = comparison.value
    match comparison.operator:
        case "==":
            return bool(actual == expected)
        case "!=":
            return bool(actual != expected)
        case "<":
            return bool(actual < expected)
        case "<=":
            return bool(actual <= expected)
        case ">":
            return bool(actual > expected)
        case _:
            return bool(actual >= expected)


def outcomes(batch: Batch, criterion: Criterion | str | None = None) -> Outcomes:
    """Each row's label under ``criterion`` (default ``status == halt``); ``error`` rows are
    ``ERROR`` whatever the criterion. Raises ``CriterionError``."""
    if criterion is None:
        criterion = DEFAULT_CRITERION
    if isinstance(criterion, str):
        criterion = parse_criterion(criterion, batch)
    rows = analyzed_rows(batch)
    for column in criterion.columns:
        for row in rows:
            value = batch.value(column, row)
            if value is None or (isinstance(value, float) and math.isnan(value)):
                raise CriterionError(
                    f"criterion {criterion.text!r}: column {column!r} is "
                    f"{'null' if value is None else 'NaN'} for run {batch.value('run', row)}"
                )
    labels: list[str] = []
    analyzed = set(rows)
    for row in range(len(batch)):
        if row not in analyzed:
            labels.append(ERROR)
            continue
        failed = any(
            all(_holds(comparison, batch.value(comparison.column, row)) for comparison in conjunct)
            for conjunct in criterion.disjuncts
        )
        labels.append(FAIL if failed else PASS)
    return Outcomes(criterion.text, tuple(labels), tuple(batch.runs))


def metric(batch: Batch, column: str) -> FloatArray:
    """The metric ``column`` over the analyzed rows. Raises ``CriterionError``."""
    if column not in batch.columns:
        raise CriterionError(f"metric: unknown column {column!r}")
    if column not in batch.numeric:
        raise CriterionError(f"metric: column {column!r} is not numeric")
    if column in batch.parameters or column in batch.seed_parameters:
        raise CriterionError(f"metric: {column!r} is a dispersion, not an outcome")
    if column in ("run", "seed"):
        raise CriterionError(f"metric: {column!r} is not an outcome")
    values: list[float] = []
    for row in analyzed_rows(batch):
        value = batch.value(column, row)
        if value is None or not math.isfinite(float(value)):
            raise CriterionError(
                f"metric: column {column!r} is {value!r} for run {batch.value('run', row)}"
            )
        values.append(float(value))
    return np.asarray(values, dtype=np.float64)


# --- rankings -------------------------------------------------------------------------------------


def benjamini_hochberg(pvalues: Sequence[float], q: float) -> list[bool]:
    """The Benjamini-Hochberg step-up procedure at false-discovery rate ``q``: with the p-values
    sorted, reject the ``k`` smallest for the largest ``k`` with ``p_(k) <= k q / m``."""
    if not 0.0 < q < 1.0:
        raise ValueError(f"q must be in (0, 1), got {q!r}")
    m = len(pvalues)
    order = sorted(range(m), key=lambda i: (pvalues[i], i))
    k = 0
    for rank, i in enumerate(order, start=1):
        if pvalues[i] <= rank * q / m:
            k = rank
    rejected = set(order[:k])
    return [i in rejected for i in range(m)]


@dataclasses.dataclass(frozen=True)
class RankRow:
    parameter: str
    effect: float
    p: float
    rejected: bool


@dataclasses.dataclass(frozen=True)
class Ranking:
    """One family's result. ``rows`` are ranked (module docstring, Methods); ``m`` is the number
    of parameters tested; ``excluded`` lists each parameter left out and why. ``status`` is
    ``"ok"`` or the stated reason no ranking was made (then ``rows`` is empty)."""

    method: str
    against: str
    q: float
    n: int
    m: int
    rows: tuple[RankRow, ...]
    excluded: tuple[tuple[str, str], ...]
    status: str

    def effect_name(self) -> str:
        return {"ks": "D", "spearman": "rho", "prcc": "prcc"}[self.method]


_SEED_REASON = "seed dispersion: not an ordered quantity"


def _ranked(
    batch: Batch,
    method: str,
    against: str,
    q: float,
    n: int,
    results: list[tuple[str, float, float]],
    excluded: list[tuple[str, str]],
) -> Ranking:
    order = {name: i for i, name in enumerate(batch.parameters)}
    rejected = benjamini_hochberg([p for _name, _effect, p in results], q) if results else []
    rows = [
        RankRow(name, effect, p, flag)
        for (name, effect, p), flag in zip(results, rejected, strict=True)
    ]
    rows.sort(key=lambda row: (row.p, -abs(row.effect), order[row.parameter]))
    return Ranking(method, against, q, n, len(rows), tuple(rows), tuple(excluded), "ok")


def _stated(method: str, against: str, q: float, n: int, status: str, batch: Batch) -> Ranking:
    excluded = tuple((name, _SEED_REASON) for name in batch.seed_parameters)
    return Ranking(method, against, q, n, 0, (), excluded, status)


def _family(batch: Batch, rows: Sequence[int]) -> tuple[list[str], list[tuple[str, str]]]:
    """The non-constant parameters over ``rows``, and the excluded ones with their reasons."""
    excluded = [(name, _SEED_REASON) for name in batch.seed_parameters]
    family: list[str] = []
    for name in batch.parameters:
        values = parameter_values(batch, name, rows)
        if values.size == 0 or bool(np.all(values == values[0])):
            excluded.append((name, "constant"))
        else:
            family.append(name)
    return family, excluded


def ks_ranking(batch: Batch, result: Outcomes, q: float = DEFAULT_Q) -> Ranking:
    """KS of failing against passing draws per parameter, BH across them (module docstring)."""
    failed = result.rows(FAIL)
    passed = result.rows(PASS)
    n = len(failed) + len(passed)
    if len(failed) < MIN_GROUP or len(passed) < MIN_GROUP:
        status = (
            f"too few runs: {len(failed)} failing and {len(passed)} passing; KS needs at least "
            f"{MIN_GROUP} of each"
        )
        return _stated("ks", result.criterion, q, n, status, batch)
    family, excluded = _family(batch, failed + passed)
    stats = _stats()
    results: list[tuple[str, float, float]] = []
    for name in family:
        test = stats.ks_2samp(
            parameter_values(batch, name, failed),
            parameter_values(batch, name, passed),
            alternative="two-sided",
            method="auto",
        )
        results.append((name, float(test.statistic), float(test.pvalue)))
    return _ranked(batch, "ks", result.criterion, q, n, results, excluded)


def spearman_ranking(
    batch: Batch, result: Outcomes, metric_name: str, q: float = DEFAULT_Q
) -> Ranking:
    """Spearman's rho of each parameter against the metric, BH across them."""
    rows = analyzed_rows(batch)
    y = metric(batch, metric_name)
    del result  # the analyzed rows are the outcome's non-error rows; error labels never enter
    if len(rows) < MIN_SPEARMAN:
        status = f"too few runs: {len(rows)}; Spearman needs at least {MIN_SPEARMAN}"
        return _stated("spearman", metric_name, q, len(rows), status, batch)
    if bool(np.all(y == y[0])):
        return _stated("spearman", metric_name, q, len(rows), "the metric is constant", batch)
    family, excluded = _family(batch, rows)
    stats = _stats()
    results: list[tuple[str, float, float]] = []
    for name in family:
        test = stats.spearmanr(parameter_values(batch, name, rows), y)
        results.append((name, float(test.statistic), float(test.pvalue)))
    return _ranked(batch, "spearman", metric_name, q, len(rows), results, excluded)


def _residual(values: FloatArray, design: FloatArray) -> FloatArray:
    coefficients = np.linalg.lstsq(design, values, rcond=None)[0]
    return values - design @ coefficients


def _pearson(a: FloatArray, b: FloatArray) -> float:
    a = a - a.mean()
    b = b - b.mean()
    return float(a @ b / math.sqrt(float(a @ a) * float(b @ b)))


def prcc_ranking(batch: Batch, result: Outcomes, metric_name: str, q: float = DEFAULT_Q) -> Ranking:
    """Partial rank correlation of each parameter with the metric, BH across them."""
    del result
    rows = analyzed_rows(batch)
    n = len(rows)
    y = metric(batch, metric_name)
    family, excluded = _family(batch, rows)
    m = len(family)
    if n < m + PRCC_EXTRA:
        status = f"too few runs: {n}; PRCC of {m} parameters needs at least {m + PRCC_EXTRA}"
        return _stated("prcc", metric_name, q, n, status, batch)
    if bool(np.all(y == y[0])):
        return _stated("prcc", metric_name, q, n, "the metric is constant", batch)
    stats = _stats()
    ranks = [
        np.asarray(stats.rankdata(parameter_values(batch, name, rows)), dtype=np.float64)
        for name in family
    ]
    ry = np.asarray(stats.rankdata(y), dtype=np.float64)
    results: list[tuple[str, float, float]] = []
    for j, name in enumerate(family):
        others = [ranks[k] for k in range(m) if k != j]
        design = np.column_stack([np.ones(n), *others])
        rx = ranks[j]
        ex = _residual(rx, design)
        ey = _residual(ry, design)
        if float(ex @ ex) <= COLLINEAR * float(((rx - rx.mean()) ** 2).sum()):
            excluded.append((name, "collinear with the other parameters"))
            continue
        if float(ey @ ey) <= COLLINEAR * float(((ry - ry.mean()) ** 2).sum()):
            excluded.append((name, "the other parameters explain the metric"))
            continue
        r = _pearson(ex, ey)
        df = n - 1 - int(np.linalg.matrix_rank(design))
        if abs(r) >= 1.0:
            p = 0.0
        else:
            t = abs(r) * math.sqrt(df / (1.0 - r * r))
            p = float(2.0 * stats.t.sf(t, df))
        results.append((name, r, p))
    return _ranked(batch, "prcc", metric_name, q, n, results, excluded)


# --- the tree -------------------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class Node:
    """``rows`` analyzed rows, ``fails`` of them failing. A split node tests ``parameter <=
    threshold`` (``left``) against ``>`` (``right``); a leaf has neither."""

    rows: int
    fails: int
    parameter: str | None = None
    threshold: float | None = None
    left: Node | None = None
    right: Node | None = None

    @property
    def predicts_fail(self) -> bool:
        return self.fails > self.rows - self.fails


@dataclasses.dataclass(frozen=True)
class Rule:
    """A fail leaf's path: ``(parameter, operator, threshold)`` conditions, ``operator`` being
    ``<=`` or ``>``."""

    conditions: tuple[tuple[str, str, float], ...]
    fails: int
    rows: int

    def text(self) -> str:
        body = " and ".join(f"{name} {op} {value:.6g}" for name, op, value in self.conditions)
        return f"{body or 'every run'} (fail {self.fails} of {self.rows})"


@dataclasses.dataclass(frozen=True)
class Tree:
    """``root`` is ``None`` when no tree was fitted; ``status`` then says why."""

    root: Node | None
    status: str
    max_depth: int = MAX_DEPTH
    grid: int = GRID
    min_leaf: int = MIN_LEAF

    @property
    def depth(self) -> int:
        def depth(node: Node | None) -> int:
            if node is None or node.left is None or node.right is None:
                return 0
            return 1 + max(depth(node.left), depth(node.right))

        return depth(self.root)

    def predict(self, values: Mapping[str, float]) -> bool:
        """Whether the tree predicts failure for one point given by parameter name."""
        node = self.root
        if node is None:
            return False
        while node.left is not None and node.right is not None:
            assert node.parameter is not None
            assert node.threshold is not None
            node = node.left if values[node.parameter] <= node.threshold else node.right
        return node.predicts_fail

    def rules(self) -> list[Rule]:
        rules: list[Rule] = []

        def walk(node: Node, path: list[tuple[str, str, float]]) -> None:
            if node.left is None or node.right is None:
                if node.predicts_fail:
                    rules.append(Rule(_tightest(path), node.fails, node.rows))
                return
            assert node.parameter is not None
            assert node.threshold is not None
            walk(node.left, [*path, (node.parameter, "<=", node.threshold)])
            walk(node.right, [*path, (node.parameter, ">", node.threshold)])

        if self.root is not None:
            walk(self.root, [])
        return rules


def _tightest(path: Sequence[tuple[str, str, float]]) -> tuple[tuple[str, str, float], ...]:
    """One bound per parameter and direction: the smallest ``<=`` and the largest ``>``, in the
    order each first appears on the path."""
    bounds: dict[tuple[str, str], float] = {}
    for name, op, value in path:
        key = (name, op)
        if key not in bounds:
            bounds[key] = value
        elif op == "<=":
            bounds[key] = min(bounds[key], value)
        else:
            bounds[key] = max(bounds[key], value)
    return tuple((name, op, value) for (name, op), value in bounds.items())


def _is_leaf(node: Node) -> bool:
    return node.left is None or node.right is None


def _impurity(fails: int, rows: int) -> Fraction:
    """``rows`` times the Gini impurity, exactly: ``2 f (n - f) / n``."""
    return Fraction(2 * fails * (rows - fails), rows) if rows else Fraction(0)


def fit_tree(
    batch: Batch,
    result: Outcomes,
    *,
    max_depth: int = MAX_DEPTH,
    grid: int = GRID,
    min_leaf: int = MIN_LEAF,
) -> Tree:
    """The classification tree of failing against passing rows (module docstring, Methods)."""
    failed = result.rows(FAIL)
    passed = result.rows(PASS)
    if not failed:
        return Tree(None, "no failures: no tree", max_depth, grid, min_leaf)
    if len(failed) < MIN_GROUP or len(passed) < MIN_GROUP:
        status = (
            f"too few runs: {len(failed)} failing and {len(passed)} passing; the tree needs at "
            f"least {MIN_GROUP} of each"
        )
        return Tree(None, status, max_depth, grid, min_leaf)
    rows = sorted(failed + passed)
    fail = np.asarray([result.labels[row] == FAIL for row in rows])
    values = {name: parameter_values(batch, name, rows) for name in batch.parameters}
    thresholds: dict[str, list[float]] = {}
    for name, column in values.items():
        lo, hi = float(column.min()), float(column.max())
        thresholds[name] = [lo + k * (hi - lo) / grid for k in range(1, grid)] if hi > lo else []

    def build(index: npt.NDArray[np.intp], depth: int) -> Node:
        fails = int(fail[index].sum())
        node = Node(int(index.size), fails)
        if depth == max_depth or fails in (0, index.size):
            return node
        parent = _impurity(fails, int(index.size))
        best: tuple[Fraction, str, float, npt.NDArray[np.bool_]] | None = None
        for name in batch.parameters:
            column = values[name][index]
            for threshold in thresholds[name]:
                left = column <= threshold
                n_left = int(left.sum())
                n_right = int(index.size) - n_left
                if n_left < min_leaf or n_right < min_leaf:
                    continue
                f_left = int(fail[index][left].sum())
                gain = parent - _impurity(f_left, n_left) - _impurity(fails - f_left, n_right)
                if gain > 0 and (best is None or gain > best[0]):
                    best = (gain, name, threshold, left)
        if best is None:
            return node
        _gain, name, threshold, left = best
        low = build(index[left], depth + 1)
        high = build(index[~left], depth + 1)
        if _is_leaf(low) and _is_leaf(high) and low.predicts_fail == high.predicts_fail:
            return node  # both sides predict the same: the split changes no prediction
        return Node(node.rows, fails, name, threshold, low, high)

    root = build(np.arange(len(rows)), 0)
    return Tree(root, "ok", max_depth, grid, min_leaf)


# --- the analysis ---------------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True, eq=False)
class Analysis:
    batch: Batch
    outcomes: Outcomes
    q: float
    metric: str | None
    ks: Ranking
    spearman: Ranking | None
    prcc: Ranking | None
    tree: Tree

    def text(self) -> str:
        """The analysis as deterministic text (``astra mc analyze`` prints it)."""
        batch = self.batch
        statuses = [str(batch.value("status", row)) for row in range(len(batch))]
        lines = [
            f"batch: {batch.path if batch.path is not None else '(synthetic)'}",
            f"runs: {len(batch)} (ok {statuses.count('ok')}, halt {statuses.count('halt')}, "
            f"error {statuses.count('error')})",
            f"outcome: {self.outcomes.criterion}: fail {self.outcomes.count(FAIL)}, pass "
            f"{self.outcomes.count(PASS)}, error {self.outcomes.count(ERROR)} (not analyzed)",
            f"parameters: {', '.join(batch.parameters) or 'none'}",
        ]
        if batch.seed_parameters:
            lines.append(f"seed parameters (not ranked): {', '.join(batch.seed_parameters)}")
        lines += _ranking_lines(self.ks)
        if self.spearman is None or self.prcc is None:
            lines.append("spearman, prcc: no metric given: Spearman and PRCC not run")
        else:
            lines += _ranking_lines(self.spearman)
            lines += _ranking_lines(self.prcc)
        tree = self.tree
        lines.append(
            f"tree (depth <= {tree.max_depth}, grid {tree.grid}, min leaf {tree.min_leaf}):"
        )
        if tree.root is None:
            lines.append(f"  {tree.status}")
        else:
            rules = tree.rules()
            lines += [f"  fail if {rule.text()}" for rule in rules] or ["  no fail leaf"]
        return "\n".join(lines) + "\n"


def _ranking_lines(ranking: Ranking) -> list[str]:
    lines = [
        f"{ranking.method} against {ranking.against} (q = {ranking.q:g}, m = {ranking.m}, "
        f"n = {ranking.n}):"
    ]
    if ranking.status != "ok":
        lines.append(f"  {ranking.status}")
    for rank, row in enumerate(ranking.rows, start=1):
        decision = "reject" if row.rejected else "keep"
        lines.append(
            f"  {rank}. {row.parameter}: {ranking.effect_name()} = {row.effect:.6g}, "
            f"p = {row.p:.3e}, BH: {decision}"
        )
    lines += [f"  excluded: {name} ({reason})" for name, reason in ranking.excluded]
    return lines


def analyze(
    batch: Batch,
    criterion: Criterion | str | None = None,
    metric_name: str | None = None,
    q: float = DEFAULT_Q,
) -> Analysis:
    """Every method on ``batch`` (module docstring). Raises ``CriterionError`` and ``ValueError``
    (``q`` outside (0, 1))."""
    if not 0.0 < q < 1.0:
        raise ValueError(f"q must be in (0, 1), got {q!r}")
    result = outcomes(batch, criterion)
    if metric_name is not None:
        metric(batch, metric_name)  # refused before any method runs
    ks = ks_ranking(batch, result, q)
    spearman = prcc = None
    if metric_name is not None:
        spearman = spearman_ranking(batch, result, metric_name, q)
        prcc = prcc_ranking(batch, result, metric_name, q)
    return Analysis(batch, result, q, metric_name, ks, spearman, prcc, fit_tree(batch, result))


# --- figures --------------------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True, eq=False)
class Envelope:
    """A signal across a batch's run logs (module docstring, Figures). ``frames`` are the frame
    numbers of the longest run's rate group; ``count`` and each statistic are per frame;
    ``failing`` maps each failing run to its own ``(t, y)``."""

    signal: str
    unit: str
    divisor: int
    dt: float
    frames: IntArray
    t: FloatArray
    count: IntArray
    minimum: FloatArray
    maximum: FloatArray
    percentiles: dict[float, FloatArray]
    failing: dict[int, tuple[FloatArray, FloatArray]]
    runs: tuple[int, ...]
    errors: tuple[int, ...]


def envelope(batch: Batch, signal: str, result: Outcomes) -> Envelope:
    """The envelope of ``signal`` over every analyzed row's log. Raises ``EnvelopeError``."""
    if batch.path is None:
        raise EnvelopeError(f"{signal}: a synthetic batch has no run logs")
    traces: list[tuple[int, IntArray, FloatArray]] = []
    reference: tuple[int, float, int, str] | None = None  # run, dt, divisor, unit
    for row in analyzed_rows(batch):
        run = int(batch.value("run", row))
        path = batch.path / montecarlo.log_name(run)
        if not path.exists():
            raise EnvelopeError(f"run {run}: its log {path} is missing")
        try:
            run_log = log.load(path)
        except (log.LogFormatError, log.IncompleteLogError) as error:
            raise EnvelopeError(f"run {run}: its log is refused ({error})") from error
        found = [(divisor, frame) for divisor, frame in run_log.rates.items() if signal in frame]
        if not found:
            raise EnvelopeError(f"run {run}: no signal {signal!r} in its log")
        divisor, frame = found[0]
        data = cast(Any, frame)
        frames = np.asarray(data.index, dtype=np.int64)
        values = np.asarray(data[signal], dtype=np.float64)
        unit = run_log.units[signal]
        if reference is None:
            reference = (run, run_log.dt, divisor, unit)
        elif (run_log.dt, divisor) != reference[1:3]:
            raise EnvelopeError(
                f"run {run}: dt {run_log.dt!r} s and divisor {divisor} differ from run "
                f"{reference[0]}'s dt {reference[1]!r} s and divisor {reference[2]}"
            )
        traces.append((run, frames, values))
    if reference is None:
        raise EnvelopeError(f"{signal}: no run of the batch left a log")
    _first, dt, divisor, unit = reference
    longest = max(traces, key=lambda trace: trace[1].size)[1]
    for run, frames, _values in traces:
        if not np.array_equal(frames, longest[: frames.size]):
            raise EnvelopeError(f"run {run}: its frames are not a prefix of the longest run's")
    size = longest.size
    by_length = sorted(traces, key=lambda trace: (-trace[1].size, trace[0]))
    count = np.zeros(size, dtype=np.int64)
    minimum = np.empty(size)
    maximum = np.empty(size)
    percentiles = {q: np.empty(size) for q in PERCENTILES}
    end = size
    for k in range(len(by_length)):
        start = by_length[k + 1][1].size if k + 1 < len(by_length) else 0
        if start == end:
            continue
        # Frames [start, end) are reached by exactly the k + 1 longest runs.
        stack = np.stack([values[start:end] for _run, _frames, values in by_length[: k + 1]])
        count[start:end] = k + 1
        minimum[start:end] = stack.min(axis=0)
        maximum[start:end] = stack.max(axis=0)
        for q in PERCENTILES:
            percentiles[q][start:end] = np.percentile(stack, q, axis=0)
        end = start
    failing_runs = set(result.runs_of(FAIL))
    failing = {
        run: (frames.astype(np.float64) * dt, values)
        for run, frames, values in sorted(traces)
        if run in failing_runs
    }
    return Envelope(
        signal,
        unit,
        divisor,
        dt,
        longest,
        longest.astype(np.float64) * dt,
        count,
        minimum,
        maximum,
        percentiles,
        failing,
        tuple(sorted(run for run, _frames, _values in traces)),
        tuple(result.runs_of(ERROR)),
    )


def _figure(width: float, height: float) -> MatplotlibFigure:
    from matplotlib.figure import Figure as MatplotlibFigure

    return MatplotlibFigure(figsize=(width, height), layout="constrained")


def draw_histograms(batch: Batch, result: Outcomes) -> MatplotlibFigure:
    """``mc-hist``: one panel per parameter, stacked pass, fail and error (module docstring)."""
    count = max(1, len(batch.parameters))
    drawn = _figure(8.0, 1.0 + 2.2 * count)
    axes = drawn.subplots(count, 1, squeeze=False)
    everyone = list(range(len(batch)))
    for i, name in enumerate(batch.parameters):
        ax = axes[i][0]
        edges = np.histogram_bin_edges(parameter_values(batch, name, everyone), bins=HIST_BINS)
        bottom = np.zeros(edges.size - 1)
        for label in OUTCOMES:
            rows = result.rows(label)
            if not rows:
                continue
            counts = np.histogram(parameter_values(batch, name, rows), bins=edges)[0]
            ax.bar(
                edges[:-1],
                counts,
                width=np.diff(edges),
                bottom=bottom,
                align="edge",
                color=COLORS[label],
                label=label,
            )
            bottom = bottom + counts
        ax.set_xlabel(name)
        ax.set_ylabel("runs")
        plot.legend_outside(ax)
    drawn.suptitle(f"draws: {result.criterion}")  # pyright: ignore[reportUnknownMemberType]
    return drawn


def draw_scatter(batch: Batch, result: Outcomes) -> MatplotlibFigure:
    """``mc-scatter``: every parameter pair, lower triangle (module docstring). Needs two."""
    names = batch.parameters
    if len(names) < 2:
        raise ValueError("a scatter matrix needs two parameters")
    side = len(names) - 1
    drawn = _figure(1.5 + 3.0 * side, 1.0 + 3.0 * side)
    everyone = list(range(len(batch)))
    columns = {name: parameter_values(batch, name, everyone) for name in names}
    first = None
    for i in range(1, len(names)):
        for j in range(i):
            ax: Any = drawn.add_subplot(side, side, (i - 1) * side + j + 1)
            for label in OUTCOMES:
                rows = result.rows(label)
                if not rows:
                    continue
                ax.scatter(
                    columns[names[j]][rows],
                    columns[names[i]][rows],
                    s=12.0,
                    c=COLORS[label],
                    marker="x" if label == ERROR else "o",
                    label=label,
                )
            ax.set_xlabel(names[j])
            ax.set_ylabel(names[i])
            first = first or ax
    if first is not None:
        plot.legend_outside(first)
    drawn.suptitle(f"draws: {result.criterion}")  # pyright: ignore[reportUnknownMemberType]
    return drawn


def draw_envelope(env: Envelope) -> MatplotlibFigure:
    """``mc-envelope-<signal>`` (module docstring, Figures)."""
    from matplotlib.lines import Line2D

    drawn = _figure(9.0, 6.4)
    axes = drawn.subplots(2, 1, sharex=True, height_ratios=(3.0, 1.0), squeeze=False)
    ax = axes[0][0]
    ax.fill_between(env.t, env.minimum, env.maximum, color="#d9d9d9", linewidth=0.0)
    ax.fill_between(
        env.t, env.percentiles[5.0], env.percentiles[95.0], color="#9ecae1", linewidth=0.0
    )
    handles: list[Any] = []
    for label, values, style in (
        ("max", env.maximum, ":"),
        ("95 %", env.percentiles[95.0], "--"),
        ("median", env.percentiles[50.0], "-"),
        ("5 %", env.percentiles[5.0], "--"),
        ("min", env.minimum, ":"),
    ):
        (line,) = ax.plot(
            env.t, values, linestyle=style, color="#08519c", linewidth=1.0, label=label
        )
        handles.append(line)
    for k, (run, (t, y)) in enumerate(sorted(env.failing.items())):
        (line,) = ax.plot(t, y, linewidth=0.8, label=f"run {run}")
        if k < MAX_LEGEND_RUNS:
            handles.append(line)
    more = len(env.failing) - MAX_LEGEND_RUNS
    if more > 0:
        handles.append(Line2D([], [], linestyle="none", label=f"and {more} more"))
    ax.set_ylabel(f"{env.signal} [{env.unit}]" if env.unit else env.signal)
    plot.legend_outside(ax, handles=handles)
    counter = axes[1][0]
    counter.plot(env.t, env.count, drawstyle="steps-post", color="#252525", label="runs")
    counter.set_ylabel("runs")
    counter.set_xlabel("t [s]")
    errors = f", {len(env.errors)} error runs without a log" if env.errors else ""
    drawn.suptitle(  # pyright: ignore[reportUnknownMemberType]
        f"{env.signal}: {len(env.runs)} runs{errors}"
    )
    return drawn


def figure_names(batch: Batch, signals: Sequence[str]) -> list[str]:
    """The figures ``render_figures`` writes, in order."""
    names = ["mc-hist"] if batch.parameters else []
    if len(batch.parameters) >= 2:
        names.append("mc-scatter")
    return names + [f"mc-envelope-{signal}" for signal in signals]


def render_figures(
    batch: Batch,
    result: Outcomes,
    signals: Sequence[str],
    out_dir: str | Path,
    *,
    fmt: Literal["svg", "png"] = "svg",
) -> list[Path]:
    """Writes the batch's figures into ``out_dir`` (module docstring, Figures) and returns their
    paths. Raises ``EnvelopeError`` and ``astra.plot.PlotOutputExistsError`` before writing."""
    if fmt not in ("svg", "png"):
        raise ValueError(f"fmt must be 'svg' or 'png', got {fmt!r}")
    if len(set(signals)) != len(signals):
        raise EnvelopeError(f"a signal is named twice in {list(signals)}")
    envelopes = [envelope(batch, signal, result) for signal in signals]
    names = figure_names(batch, signals)
    out_dir = Path(out_dir)
    targets = [out_dir / f"{name}.{fmt}" for name in names]
    for target in targets:
        if target.exists() or target.is_symlink():
            raise plot.PlotOutputExistsError(
                f"{target}: already exists; astra.mc_analysis never overwrites a file"
            )
    out_dir.mkdir(parents=True, exist_ok=True)
    created: list[Path] = []
    drawers: list[Any] = []
    if batch.parameters:
        drawers.append(lambda: draw_histograms(batch, result))
    if len(batch.parameters) >= 2:
        drawers.append(lambda: draw_scatter(batch, result))
    drawers += [lambda env=env: draw_envelope(env) for env in envelopes]
    try:
        for name, target, draw in zip(names, targets, drawers, strict=True):
            with plot.pinned_style("mc", name):
                drawn = draw()
                with plot.create_exclusive(target, created) as stream:
                    metadata = {"Date": None} if fmt == "svg" else {}
                    drawn.savefig(stream, format=fmt, metadata=metadata)
    except BaseException:
        for path in created:
            path.unlink(missing_ok=True)
        raise
    return targets
