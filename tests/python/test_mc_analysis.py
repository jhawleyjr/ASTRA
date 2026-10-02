"""astra.mc_analysis, ``astra mc analyze`` and ``astra mc rerun`` (M6 PR 6,
docs/plans/m6-pr6-mc-analysis.md; docs/plans/m6-analysis-tooling.md R5 to R8, R7b, R10 and R15's
mc cases; C-06, C-07, C-10 and N-06).

The figure, analyze and rerun tests share one batch built once per session from
``mc_analysis_fixtures/descent_batch.yaml`` (ok, halt and error rows, runs of different lengths, a
divisor-1 and a divisor-10 signal); a test that changes a batch works on its own copy. The
statistics tests use synthetic summaries drawn with ``astra.montecarlo.Rng`` (a stable stream,
never numpy's generators) from stated seeds. Every expected value is computed independently of
the module: from the summary file, from each log through ``astra.log.load``, from numpy and scipy
directly, or by hand.
"""

from __future__ import annotations

import hashlib
import math
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, cast

import numpy as np
import pyarrow.parquet as pq  # pyright: ignore[reportMissingTypeStubs]
import pytest
import scipy.stats  # pyright: ignore[reportMissingTypeStubs]
import yaml
from matplotlib.colors import to_rgba

from astra import _runkit, cli, log, mc_analysis, montecarlo

REPO = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / "mc_analysis_fixtures"
SPEC = FIXTURES / "descent_batch.yaml"
DESCENT = "initial.state.v_b.2"
MASS = "physical.mass.mass"
FORCE = "vehicle.models.0.force_x"
WIND_SEED = "physical.wind.seed"
ALTITUDE = "v0.derived.altitude"
SLOW = "v0.slow_rate.value"


# --- the shared batch ---------------------------------------------------------------------------


@pytest.fixture(scope="session")
def descent_batch(tmp_path_factory: pytest.TempPathFactory) -> Path:
    out = tmp_path_factory.mktemp("descent") / "batch"
    montecarlo.run(montecarlo.load(SPEC, root=REPO), out, workers=4)
    return out


def _copy(batch: Path, tmp_path: Path) -> Path:
    copy = tmp_path / "batch"
    shutil.copytree(batch, copy)
    return copy


def _summary(batch: Path) -> dict[str, list[Any]]:
    return cast(dict[str, list[Any]], pq.read_table(batch / montecarlo.SUMMARY).to_pydict())


def _labels(summary: dict[str, list[Any]]) -> list[str]:
    """The default criterion, computed from the file: halt fails, error is apart."""
    return [
        "error" if status == "error" else "fail" if status == "halt" else "pass"
        for status in summary["status"]
    ]


def _trace(batch: Path, run: int, signal: str) -> tuple[np.ndarray, np.ndarray, float]:
    """``(frames, values, dt)`` of ``signal`` in run ``run``'s log, read independently."""
    run_log = log.load(batch / montecarlo.log_name(run))
    for frame in run_log.rates.values():
        if signal in frame:
            data = cast(Any, frame)
            return (
                np.asarray(data.index, dtype=np.int64),
                np.asarray(data[signal], dtype=np.float64),
                run_log.dt,
            )
    raise AssertionError(f"run {run} has no {signal}")


def _hashes(directory: Path) -> dict[str, str]:
    return {
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(directory.iterdir())
    }


def test_mc_analysis_the_fixture_batch_has_ok_halt_and_error_rows_of_different_lengths(
    descent_batch: Path,
) -> None:
    summary = _summary(descent_batch)
    statuses = summary["status"]
    assert statuses.count("ok") >= 1
    assert statuses.count("error") >= 1
    halt_frames = {
        frame
        for frame, status in zip(summary["halt_frame"], statuses, strict=True)
        if status == "halt"
    }
    assert len(halt_frames) >= 2
    batch = mc_analysis.load_batch(descent_batch)
    assert batch.parameters == (DESCENT, MASS, FORCE)
    assert batch.seed_parameters == (WIND_SEED,)
    # Two rate groups: the slow signal is logged every tenth frame.
    run = statuses.index("ok")
    frames, _values, _dt = _trace(descent_batch, run, SLOW)
    assert list(frames[:3]) == [0, 10, 20]


# --- R5 and C-10: the figures -------------------------------------------------------------------


def _color(collection: Any) -> tuple[float, ...]:
    faces = collection.get_facecolor()
    colors = faces if len(faces) else collection.get_edgecolor()
    return tuple(float(v) for v in colors[0])


def test_mc_analysis_histogram_bars_equal_numpy_per_outcome(descent_batch: Path) -> None:
    batch = mc_analysis.load_batch(descent_batch)
    drawn = mc_analysis.draw_histograms(batch, mc_analysis.outcomes(batch))
    summary = _summary(descent_batch)
    labels = np.asarray(_labels(summary))
    assert len(drawn.axes) == len(batch.parameters)
    for ax, name in zip(drawn.axes, batch.parameters, strict=True):
        values = np.asarray(summary[name], dtype=np.float64)
        edges = np.histogram_bin_edges(values, bins=20)
        present = [label for label in ("pass", "fail", "error") if (labels == label).any()]
        containers = list(ax.containers)
        assert [c.get_label() for c in containers] == present
        bottom = np.zeros(edges.size - 1)
        for container, label in zip(containers, present, strict=True):
            counts = np.histogram(values[labels == label], bins=edges)[0]
            patches = list(cast(Any, container))
            assert [p.get_height() for p in patches] == list(counts), (name, label)
            assert [p.get_y() for p in patches] == list(bottom), (name, label)
            assert [p.get_x() for p in patches] == list(edges[:-1]), (name, label)
            assert patches[0].get_facecolor() == to_rgba(mc_analysis.COLORS[label])
            bottom = bottom + counts
        assert ax.get_xlabel() == name


def test_mc_analysis_scatter_points_are_exactly_each_outcomes_rows(descent_batch: Path) -> None:
    batch = mc_analysis.load_batch(descent_batch)
    drawn = mc_analysis.draw_scatter(batch, mc_analysis.outcomes(batch))
    summary = _summary(descent_batch)
    labels = np.asarray(_labels(summary))
    names = batch.parameters
    pairs = [(names[j], names[i]) for i in range(1, len(names)) for j in range(i)]
    assert [(ax.get_xlabel(), ax.get_ylabel()) for ax in drawn.axes] == pairs
    for ax, (x_name, y_name) in zip(drawn.axes, pairs, strict=True):
        x = np.asarray(summary[x_name], dtype=np.float64)
        y = np.asarray(summary[y_name], dtype=np.float64)
        collections = list(ax.collections)
        present = [label for label in ("pass", "fail", "error") if (labels == label).any()]
        assert [c.get_label() for c in collections] == present
        for collection, label in zip(collections, present, strict=True):
            expected = np.column_stack([x[labels == label], y[labels == label]])
            assert np.array_equal(np.asarray(collection.get_offsets()), expected), label
            assert _color(collection) == to_rgba(mc_analysis.COLORS[label]), label


@pytest.mark.parametrize("signal", [ALTITUDE, SLOW])
def test_mc_analysis_envelope_equals_numpy_over_the_runs_that_reached_each_frame(
    descent_batch: Path, signal: str
) -> None:
    batch = mc_analysis.load_batch(descent_batch)
    result = mc_analysis.outcomes(batch)
    drawn = mc_analysis.draw_envelope(mc_analysis.envelope(batch, signal, result))
    summary = _summary(descent_batch)
    traces = {
        run: _trace(descent_batch, run, signal)
        for run, status in zip(summary["run"], summary["status"], strict=True)
        if status != "error"
    }
    longest = max((frames for frames, _values, _dt in traces.values()), key=len)
    dt = next(iter(traces.values()))[2]
    lines = {str(line.get_label()): line for line in drawn.axes[0].get_lines()}
    (count_line,) = drawn.axes[1].get_lines()
    assert np.array_equal(np.asarray(count_line.get_xdata()), longest.astype(np.float64) * dt)
    stats = {"min": None, "5 %": 5.0, "median": 50.0, "95 %": 95.0, "max": None}
    for k in range(longest.size):
        reached = np.asarray(
            [values[k] for frames, values, _dt in traces.values() if frames.size > k]
        )
        assert np.asarray(count_line.get_ydata())[k] == reached.size, k
        for label, q in stats.items():
            if q is None:
                expected = reached.min() if label == "min" else reached.max()
            else:
                expected = np.percentile(reached, q)
            assert np.asarray(lines[label].get_ydata())[k] == expected, (signal, label, k)
    halted = [
        run
        for run, status in zip(summary["run"], summary["status"], strict=True)
        if status == "halt"
    ]
    overlaid = sorted(label for label in lines if label.startswith("run "))
    assert overlaid == sorted(f"run {run}" for run in halted)
    for run in halted:
        frames, values, _dt = traces[run]
        line = lines[f"run {run}"]
        assert np.array_equal(np.asarray(line.get_xdata()), frames.astype(np.float64) * dt)
        assert np.array_equal(np.asarray(line.get_ydata()), values)
    errors = summary["status"].count("error")
    assert f"{errors} error runs without a log" in drawn.get_suptitle()


def test_mc_analysis_the_envelope_keeps_only_each_signal_column(descent_batch: Path) -> None:
    # Review R-001: a retained trace is its own array, never a view that keeps the log's whole
    # rate-group matrix alive.
    batch = mc_analysis.load_batch(descent_batch)
    env = mc_analysis.envelope(batch, ALTITUDE, mc_analysis.outcomes(batch))
    assert env.failing
    for _t, values in env.failing.values():
        assert values.flags.owndata
        assert values.base is None


def test_mc_analysis_a_custom_criterion_changes_the_failing_set_and_its_overlays(
    descent_batch: Path,
) -> None:
    batch = mc_analysis.load_batch(descent_batch)
    criterion = f"status == halt or {FORCE} > 2.5"
    result = mc_analysis.outcomes(batch, criterion)
    summary = _summary(descent_batch)
    expected = [
        run
        for run, status, force in zip(
            summary["run"], summary["status"], summary[FORCE], strict=True
        )
        if status != "error" and (status == "halt" or force > 2.5)
    ]
    assert result.runs_of("fail") == expected
    assert len(expected) > summary["status"].count("halt")  # the criterion added ok runs
    drawn = mc_analysis.draw_envelope(mc_analysis.envelope(batch, ALTITUDE, result))
    overlaid = sorted(str(line.get_label()) for line in drawn.axes[0].get_lines())
    assert [label for label in overlaid if label.startswith("run ")] == sorted(
        f"run {run}" for run in expected
    )


def test_mc_analysis_the_legend_shows_ten_failing_runs_then_and_k_more() -> None:
    t = np.arange(4, dtype=np.float64) * 0.001
    flat = np.zeros(4)
    failing = {run: (t, flat + run) for run in range(12)}
    env = mc_analysis.Envelope(
        "x",
        "m",
        1,
        0.001,
        np.arange(4, dtype=np.int64),
        t,
        np.full(4, 12, dtype=np.int64),
        flat,
        flat,
        {5.0: flat, 50.0: flat, 95.0: flat},
        failing,
        tuple(range(12)),
        (),
    )
    drawn = mc_analysis.draw_envelope(env)
    ax = drawn.axes[0]
    legend = ax.get_legend()
    assert legend is not None
    texts = [text.get_text() for text in legend.get_texts()]
    assert texts == [
        "max",
        "95 %",
        "median",
        "5 %",
        "min",
        *(f"run {run}" for run in range(10)),
        "and 2 more",
    ]
    labels = [str(line.get_label()) for line in ax.get_lines()]
    assert [label for label in labels if label.startswith("run ")] == [
        f"run {run}" for run in range(12)
    ]
    drawn.draw_without_rendering()
    assert legend.get_window_extent().x0 >= ax.get_window_extent().x1  # D1: outside the axes


def test_mc_analysis_a_missing_log_of_an_ok_run_is_refused_naming_the_run(
    descent_batch: Path, tmp_path: Path
) -> None:
    batch_dir = _copy(descent_batch, tmp_path)
    summary = _summary(batch_dir)
    run = summary["run"][summary["status"].index("ok")]
    (batch_dir / montecarlo.log_name(run)).unlink()
    batch = mc_analysis.load_batch(batch_dir)
    with pytest.raises(mc_analysis.EnvelopeError, match=rf"run {run}: its log .* is missing"):
        mc_analysis.envelope(batch, ALTITUDE, mc_analysis.outcomes(batch))


def test_mc_analysis_an_unknown_signal_is_refused_naming_it(descent_batch: Path) -> None:
    batch = mc_analysis.load_batch(descent_batch)
    with pytest.raises(mc_analysis.EnvelopeError, match=r"no signal 'v0\.derived\.nope'"):
        mc_analysis.envelope(batch, "v0.derived.nope", mc_analysis.outcomes(batch))


def test_mc_analysis_a_log_at_another_divisor_is_refused_naming_the_run(
    descent_batch: Path, tmp_path: Path
) -> None:
    # A hand-built batch: one ok run's log replaced by a log of the same config with slow_rate at
    # divisor 5, from a one-run batch of its own.
    config = (FIXTURES / "descent.yaml").read_text(encoding="utf-8")
    other = tmp_path / "descent_divisor_5.yaml"
    other.write_text(config.replace("divisor: 10", "divisor: 5"), encoding="utf-8")
    spec = tmp_path / "spec.yaml"
    spec.write_text(
        f"config: {other.name}\nduration: 0.5\nruns: 1\nseed: 1\n"
        f"dispersions:\n  {FORCE}: {{uniform: {{lo: 0.0, hi: 5.0}}}}\n",
        encoding="utf-8",
    )
    montecarlo.run(montecarlo.load(spec, root=tmp_path), tmp_path / "other", workers=1)
    batch_dir = _copy(descent_batch, tmp_path)
    summary = _summary(batch_dir)
    ok = [run for run, s in zip(summary["run"], summary["status"], strict=True) if s == "ok"]
    target = batch_dir / montecarlo.log_name(ok[-1])
    target.unlink()
    shutil.copy(tmp_path / "other" / montecarlo.log_name(0), target)
    batch = mc_analysis.load_batch(batch_dir)
    with pytest.raises(mc_analysis.EnvelopeError, match=rf"run {ok[-1]}: .*divisor 5 differ"):
        mc_analysis.envelope(batch, SLOW, mc_analysis.outcomes(batch))


_RENDER = """
import sys
from astra import mc_analysis
batch = mc_analysis.load_batch(sys.argv[1])
mc_analysis.render_figures(batch, mc_analysis.outcomes(batch), [sys.argv[3]], sys.argv[2])
"""


def test_mc_analysis_figures_are_byte_identical_across_processes(
    descent_batch: Path, tmp_path: Path
) -> None:
    for name in ("a", "b"):
        subprocess.run(
            [sys.executable, "-c", _RENDER, str(descent_batch), str(tmp_path / name), ALTITUDE],
            check=True,
        )
    names = sorted(path.name for path in (tmp_path / "a").iterdir())
    assert names == ["mc-envelope-v0.derived.altitude.svg", "mc-hist.svg", "mc-scatter.svg"]
    for name in names:
        assert (tmp_path / "a" / name).read_bytes() == (tmp_path / "b" / name).read_bytes(), name


def test_mc_analysis_render_never_overwrites_and_writes_nothing_on_a_refusal(
    descent_batch: Path, tmp_path: Path
) -> None:
    batch = mc_analysis.load_batch(descent_batch)
    result = mc_analysis.outcomes(batch)
    out = tmp_path / "out"
    out.mkdir()
    (out / "mc-scatter.svg").write_bytes(b"keep")
    with pytest.raises(mc_analysis.plot.PlotOutputExistsError, match=r"mc-scatter\.svg"):
        mc_analysis.render_figures(batch, result, [], out)
    assert sorted(path.name for path in out.iterdir()) == ["mc-scatter.svg"]
    with pytest.raises(mc_analysis.EnvelopeError):
        mc_analysis.render_figures(batch, result, ["v0.derived.nope"], tmp_path / "other")
    assert not (tmp_path / "other").exists()


# --- synthetic batches --------------------------------------------------------------------------


def _draws(seed: int, n: int, names: list[str]) -> tuple[montecarlo.Rng, dict[str, list[float]]]:
    """``n`` rows of independent uniform [0, 1) draws, row by row in ``names`` order."""
    rng = montecarlo.Rng(seed)
    columns: dict[str, list[float]] = {name: [] for name in names}
    for _ in range(n):
        for name in names:
            columns[name].append(rng.uniform01())
    return rng, columns


def _batch(
    columns: dict[str, list[Any]], failed: list[bool], parameters: list[str]
) -> mc_analysis.Batch:
    n = len(failed)
    return mc_analysis.from_columns(
        {"run": list(range(n)), "status": ["halt" if f else "ok" for f in failed], **columns},
        parameters,
    )


def _by_name(ranking: mc_analysis.Ranking) -> dict[str, mc_analysis.RankRow]:
    return {row.parameter: row for row in ranking.rows}


# --- R6 and C-06: a planted cause, and Benjamini-Hochberg ---------------------------------------


def test_mc_analysis_r6_a_planted_cause_ranks_first_and_alone_survives_bh() -> None:
    names = [f"p{i}" for i in range(1, 7)]
    _rng, columns = _draws(montecarlo.run_seed(2026, 6), 300, names)
    failed = [p1 > 0.8 for p1 in columns["p1"]]
    batch = _batch(columns, failed, names)
    ranking = mc_analysis.ks_ranking(batch, mc_analysis.outcomes(batch), 0.05)
    assert ranking.status == "ok"
    assert ranking.m == 6
    first = ranking.rows[0]
    assert first.parameter == "p1"
    assert first.effect == 1.0  # the classes are separated
    assert first.rejected
    assert [row.parameter for row in ranking.rows if row.rejected] == ["p1"]


@pytest.mark.parametrize(
    ("pvalues", "bh", "bonferroni", "uncorrected"),
    [
        # BH rejects all five (0.05 <= 5 x 0.05 / 5); Bonferroni (p <= 0.01) only one.
        ([0.01, 0.02, 0.03, 0.04, 0.05], 5, 1, 5),
        # Step-up: 0.04 <= 0.05 rejects all four though 0.03 > 0.025; a step-down stops at one.
        ([0.01, 0.03, 0.035, 0.04], 4, 1, 4),
        # BH one, uncorrected three.
        ([0.001, 0.04, 0.045, 0.3], 1, 1, 3),
    ],
)
def test_mc_analysis_benjamini_hochberg_against_hand_computed_vectors(
    pvalues: list[float], bh: int, bonferroni: int, uncorrected: int
) -> None:
    q = 0.05
    m = len(pvalues)
    assert sum(mc_analysis.benjamini_hochberg(pvalues, q)) == bh
    assert sum(p <= q / m for p in pvalues) == bonferroni
    assert sum(p <= q for p in pvalues) == uncorrected
    # A step-down procedure would stop at the first p above its threshold.
    step_down = 0
    for rank, p in enumerate(sorted(pvalues), start=1):
        if p > rank * q / m:
            break
        step_down = rank
    if pvalues == [0.01, 0.03, 0.035, 0.04]:
        assert step_down == 1


def test_mc_analysis_benjamini_hochberg_order_ties_and_scipy() -> None:
    assert mc_analysis.benjamini_hochberg([0.04, 0.01, 0.03, 0.035], 0.05) == [True] * 4
    # Ties: 0.02 <= 3 x 0.05 / 4 rejects all three.
    assert mc_analysis.benjamini_hochberg([0.02, 0.5, 0.02, 0.02], 0.05) == [
        True,
        False,
        True,
        True,
    ]
    rng = montecarlo.Rng(99)
    for _ in range(50):
        pvalues = [rng.uniform01() ** 4 for _ in range(12)]
        adjusted = scipy.stats.false_discovery_control(pvalues)
        assert mc_analysis.benjamini_hochberg(pvalues, 0.05) == [bool(a <= 0.05) for a in adjusted]
    with pytest.raises(ValueError, match="q must be in"):
        mc_analysis.benjamini_hochberg([0.1], 1.0)


def test_mc_analysis_ranking_order_is_p_then_effect_then_spec_order() -> None:
    names = ["p1", "p2", "p3", "p4"]
    batch = _batch({name: [0.0, 1.0] for name in names}, [False, True], names)
    ranking = mc_analysis._ranked(  # pyright: ignore[reportPrivateUsage]
        batch,
        "spearman",
        "metric",
        0.05,
        2,
        [("p1", 0.5, 0.0), ("p2", 0.9, 0.0), ("p3", -0.9, 0.0), ("p4", 0.99, 0.01)],
        [],
    )
    assert [row.parameter for row in ranking.rows] == ["p2", "p3", "p1", "p4"]


def test_mc_analysis_too_few_failures_is_a_stated_result() -> None:
    names = ["p1", "p2"]
    _rng, columns = _draws(5, 40, names)
    failed = [i < 4 for i in range(40)]
    batch = _batch(columns, failed, names)
    ranking = mc_analysis.ks_ranking(batch, mc_analysis.outcomes(batch))
    assert ranking.rows == ()
    assert ranking.status.startswith("too few runs: 4 failing and 36 passing")
    tree = mc_analysis.fit_tree(batch, mc_analysis.outcomes(batch))
    assert tree.root is None
    assert tree.status.startswith("too few runs")


# --- R8 and C-06: global-null calibration -------------------------------------------------------


def test_mc_analysis_r8_false_positives_stay_within_the_binomial_allowance() -> None:
    # Under the global null (no parameter matters), BH at q makes P(any rejection) <= q for the KS
    # and Spearman families (independent given the outcome); PRCC's is approximate. Over K seeded
    # batches the count flagging anything must not exceed the binomial's 99 % point.
    batches, n, q = 200, 200, 0.05
    names = [f"p{i}" for i in range(1, 6)]
    flagged = {"ks": 0, "spearman": 0, "prcc": 0}
    for k in range(batches):
        rng = montecarlo.Rng(montecarlo.run_seed(2026, k))
        columns: dict[str, list[Any]] = {name: [] for name in [*names, "metric"]}
        failed: list[bool] = []
        for _ in range(n):
            for name in names:
                columns[name].append(rng.uniform01())
            failed.append(rng.uniform01() < 0.2)
            columns["metric"].append(rng.uniform01())
        batch = _batch(columns, failed, names)
        result = mc_analysis.outcomes(batch)
        rankings = {
            "ks": mc_analysis.ks_ranking(batch, result, q),
            "spearman": mc_analysis.spearman_ranking(batch, result, "metric", q),
            "prcc": mc_analysis.prcc_ranking(batch, result, "metric", q),
        }
        for method, ranking in rankings.items():
            assert ranking.status == "ok"
            flagged[method] += any(row.rejected for row in ranking.rows)
    allowance = int(scipy.stats.binom.ppf(0.99, batches, q))
    assert all(count <= allowance for count in flagged.values()), (flagged, allowance)


# --- R7 and C-07: the tree ----------------------------------------------------------------------


def test_mc_analysis_r7_a_planted_interaction_is_a_conjunction_right_in_all_four_quadrants() -> (
    None
):
    names = [f"p{i}" for i in range(1, 7)]
    _rng, columns = _draws(montecarlo.run_seed(2026, 7), 400, names)
    failed = [a > 0.6 and b < 0.3 for a, b in zip(columns["p1"], columns["p2"], strict=True)]
    batch = _batch(columns, failed, names)
    tree = mc_analysis.fit_tree(batch, mc_analysis.outcomes(batch))
    assert tree.root is not None
    assert tree.depth <= 3
    step = {name: (max(columns[name]) - min(columns[name])) / mc_analysis.GRID for name in names}
    (rule,) = tree.rules()  # one conjunction, naming p1 and p2 only
    bounds = {(name, op): value for name, op, value in rule.conditions}
    assert set(bounds) == {("p1", ">"), ("p2", "<=")}, rule.text()
    assert abs(bounds[("p1", ">")] - 0.6) <= step["p1"], rule.text()
    assert abs(bounds[("p2", "<=")] - 0.3) <= step["p2"], rule.text()
    mesh = np.linspace(0.0, 1.0, 21)
    checked = set()
    for a in mesh:
        for b in mesh:
            if abs(a - 0.6) <= step["p1"] or abs(b - 0.3) <= step["p2"]:
                continue
            point = {name: 0.5 for name in names} | {"p1": float(a), "p2": float(b)}
            assert tree.predict(point) == (a > 0.6 and b < 0.3), (a, b)
            checked.add((a > 0.6, b < 0.3))
    assert checked == {(True, True), (True, False), (False, True), (False, False)}


def test_mc_analysis_the_tree_stops_at_depth_three() -> None:
    names = [f"p{i}" for i in range(1, 5)]
    _rng, columns = _draws(41, 2000, names)
    failed = [all(columns[name][i] > 0.3 for name in names) for i in range(2000)]
    batch = _batch(columns, failed, names)
    tree = mc_analysis.fit_tree(batch, mc_analysis.outcomes(batch))
    assert tree.depth == 3  # the target needs four splits


def _leaves(node: mc_analysis.Node) -> list[mc_analysis.Node]:
    if node.left is None or node.right is None:
        return [node]
    return [*_leaves(node.left), *_leaves(node.right)]


def test_mc_analysis_the_tree_breaks_ties_by_parameter_then_threshold() -> None:
    # A duplicated column: equal gains, so the earlier parameter splits.
    _rng, columns = _draws(17, 200, ["p1"])
    columns["copy"] = list(columns["p1"])
    failed = [value > 0.5 for value in columns["p1"]]
    batch = _batch(columns, failed, ["p1", "copy"])
    tree = mc_analysis.fit_tree(batch, mc_analysis.outcomes(batch))
    assert tree.root is not None
    assert tree.root.parameter == "p1"
    # A gap between 0.2 and 0.8: every grid threshold inside it splits the same way, so the
    # smallest one is chosen.
    low = [0.2 * i / 49 for i in range(50)]
    high = [0.8 + 0.2 * i / 49 for i in range(50)]
    values = low + high
    batch = _batch({"x": values}, [v > 0.5 for v in values], ["x"])
    tree = mc_analysis.fit_tree(batch, mc_analysis.outcomes(batch))
    assert tree.root is not None
    grid = [0.0 + k * (1.0 - 0.0) / mc_analysis.GRID for k in range(1, mc_analysis.GRID)]
    assert tree.root.threshold == min(t for t in grid if 0.2 <= t < 0.8)
    for leaf in _leaves(tree.root):
        assert leaf.rows >= mc_analysis.MIN_LEAF


def test_mc_analysis_a_tied_leaf_predicts_pass_and_no_failure_is_a_stated_result() -> None:
    assert not mc_analysis.Node(rows=4, fails=2).predicts_fail
    assert mc_analysis.Node(rows=5, fails=3).predicts_fail
    _rng, columns = _draws(3, 30, ["p1"])
    batch = _batch(columns, [False] * 30, ["p1"])
    tree = mc_analysis.fit_tree(batch, mc_analysis.outcomes(batch))
    assert (tree.root, tree.status) == (None, "no failures: no tree")


# --- R7b and C-07: Spearman and PRCC ------------------------------------------------------------


def test_mc_analysis_r7b_prcc_is_partial() -> None:
    rng = montecarlo.Rng(montecarlo.run_seed(2026, 77))
    columns: dict[str, list[Any]] = {"p1": [], "p2": [], "metric": []}
    for _ in range(500):
        p1 = rng.uniform01()
        columns["p1"].append(p1)
        columns["p2"].append(p1 + 0.1 * rng.uniform01())
        columns["metric"].append(p1 + 0.1 * rng.uniform01())
    batch = _batch(columns, [False] * 500, ["p1", "p2"])
    result = mc_analysis.outcomes(batch)
    spearman = _by_name(mc_analysis.spearman_ranking(batch, result, "metric"))
    prcc = _by_name(mc_analysis.prcc_ranking(batch, result, "metric"))
    # Population: rho(p2, y) ~ 0.99 and the partial rho(p2, y | p1) = 0, whose sampling error is
    # about 1 / sqrt(n - 3) ~ 0.045, so 0.2 is over four of them.
    assert spearman["p2"].effect > 0.9
    assert abs(prcc["p2"].effect) < 0.2
    assert prcc["p1"].effect > 0.5


def _pearson(a: list[float], b: list[float]) -> float:
    ma = sum(a) / len(a)
    mb = sum(b) / len(b)
    cov = sum((x - ma) * (y - mb) for x, y in zip(a, b, strict=True))
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((y - mb) ** 2 for y in b)
    return cov / math.sqrt(va * vb)


def test_mc_analysis_spearman_against_hand_ranked_ties_and_a_monotone_input() -> None:
    batch = _batch(
        {"x": [1.0, 2.0, 2.0, 3.0, 4.0], "metric": [2.0, 1.0, 4.0, 3.0, 5.0]}, [False] * 5, ["x"]
    )
    (row,) = mc_analysis.spearman_ranking(batch, mc_analysis.outcomes(batch), "metric").rows
    expected = _pearson([1.0, 2.5, 2.5, 4.0, 5.0], [2.0, 1.0, 4.0, 3.0, 5.0])  # average ranks
    assert row.effect == pytest.approx(expected, abs=1e-12)
    batch = _batch(
        {"x": [1.0, 2.0, 3.0, 4.0, 5.0, 6.0], "metric": [6.0, 5.0, 4.0, 3.0, 2.0, 1.0]},
        [False] * 6,
        ["x"],
    )
    (row,) = mc_analysis.spearman_ranking(batch, mc_analysis.outcomes(batch), "metric").rows
    assert row.effect == -1.0


def test_mc_analysis_prcc_equals_the_inverse_correlation_matrix_oracle() -> None:
    names = ["p1", "p2", "p3"]
    rng, columns = _draws(123, 60, names)
    columns["metric"] = [
        a + 0.5 * b + 0.3 * rng.uniform01()
        for a, b in zip(columns["p1"], columns["p2"], strict=True)
    ]
    batch = _batch(columns, [False] * 60, names)
    prcc = _by_name(mc_analysis.prcc_ranking(batch, mc_analysis.outcomes(batch), "metric"))
    ranks = np.column_stack([scipy.stats.rankdata(columns[name]) for name in [*names, "metric"]])
    inverse = np.linalg.inv(np.corrcoef(ranks, rowvar=False))
    for j, name in enumerate(names):
        expected = -inverse[j, 3] / math.sqrt(inverse[j, j] * inverse[3, 3])
        assert prcc[name].effect == pytest.approx(expected, abs=1e-12), name
    # m = 2: the first-order partial correlation in closed form.
    batch = _batch({k: columns[k] for k in ("p1", "p2", "metric")}, [False] * 60, ["p1", "p2"])
    prcc = _by_name(mc_analysis.prcc_ranking(batch, mc_analysis.outcomes(batch), "metric"))
    r = np.corrcoef(ranks[:, [0, 1, 3]], rowvar=False)
    r12, r1y, r2y = r[0, 1], r[0, 2], r[1, 2]
    expected = (r1y - r12 * r2y) / math.sqrt((1 - r12**2) * (1 - r2y**2))
    assert prcc["p1"].effect == pytest.approx(expected, abs=1e-12)


def test_mc_analysis_constant_collinear_and_too_few_are_stated() -> None:
    _rng, columns = _draws(8, 40, ["p1", "metric"])
    columns["flat"] = [0.25] * 40
    columns["double"] = [2.0 * value for value in columns["p1"]]
    batch = _batch(columns, [False] * 40, ["p1", "flat", "double"])
    result = mc_analysis.outcomes(batch)
    spearman = mc_analysis.spearman_ranking(batch, result, "metric")
    assert ("flat", "constant") in spearman.excluded
    assert {row.parameter for row in spearman.rows} == {"p1", "double"}
    prcc = mc_analysis.prcc_ranking(batch, result, "metric")
    assert ("p1", "collinear with the other parameters") in prcc.excluded
    assert ("double", "collinear with the other parameters") in prcc.excluded
    assert prcc.rows == ()
    small = _batch(
        {"p1": [0.1, 0.2, 0.3], "p2": [0.3, 0.1, 0.2], "metric": [1.0, 2.0, 3.0]},
        [False] * 3,
        ["p1", "p2"],
    )
    prcc = mc_analysis.prcc_ranking(small, mc_analysis.outcomes(small), "metric")
    assert prcc.status == "too few runs: 3; PRCC of 2 parameters needs at least 5"
    tiny = _batch({"p1": [0.1, 0.2], "metric": [1.0, 2.0]}, [False] * 2, ["p1"])
    spearman = mc_analysis.spearman_ranking(tiny, mc_analysis.outcomes(tiny), "metric")
    assert spearman.status == "too few runs: 2; Spearman needs at least 3"


def test_mc_analysis_without_a_metric_spearman_and_prcc_are_not_run() -> None:
    names = ["p1", "p2"]
    _rng, columns = _draws(9, 60, names)
    batch = _batch(columns, [value > 0.5 for value in columns["p1"]], names)
    analysis = mc_analysis.analyze(batch)
    assert analysis.spearman is None
    assert analysis.prcc is None
    assert "no metric given: Spearman and PRCC not run" in analysis.text()


# --- R10: the criterion is closed ---------------------------------------------------------------


def _criterion_batch() -> mc_analysis.Batch:
    return mc_analysis.from_columns(
        {
            "run": [0, 1, 2, 3],
            "status": ["ok", "halt", "error", "ok"],
            "a": [1.0, 0.0, 1.0, 0.0],
            "b": [0.0, 1.0, 1.0, 1.0],
            "c": [0.0, 0.0, 1.0, 1.0],
            "vehicle.models.0.force_x": [-1.0, 0.5, 2.0, 3.0],
            "max.v0.derived.alpha": [0.001, 0.2, 0.3, 0.5],
            "error_type": [None, None, "ConfigError", None],
            "halt_frame": [None, 5, None, None],
            "bad": [1.0, float("nan"), 1.0, 1.0],
        },
        ["a", "b", "c"],
    )


@pytest.mark.parametrize(
    ("text", "message"),
    [
        ("nope > 1", "unknown column 'nope'"),
        ("a = 1", "unknown operator '='"),
        ("a => 1", "unknown operator '=>'"),
        ("a ~ 1", "unknown operator '~'"),
        ("a > abc", "'abc' is not a number"),
        ("a > 1e999", "'1e999' is not finite"),
        ("error_type == 1", "column 'error_type' is not numeric"),
        ("a > 1 and", "missing a comparison after 'and'"),
        ("a > 1 b > 2", "expected 'and' or 'or', got 'b'"),
        ("status == error", "status is compared with ok or halt, not 'error'"),
        ("status > ok", "status takes == or !="),
        ("", "empty"),
    ],
)
def test_mc_analysis_r10_a_refused_criterion_names_the_token(text: str, message: str) -> None:
    with pytest.raises(mc_analysis.CriterionError, match=message):
        mc_analysis.outcomes(_criterion_batch(), text)


@pytest.mark.parametrize(
    ("column", "message"),
    [("halt_frame", "'halt_frame' is null for run 0"), ("bad", "'bad' is NaN for run 1")],
)
def test_mc_analysis_r10_a_null_or_nan_in_a_referenced_column_names_it(
    column: str, message: str
) -> None:
    with pytest.raises(mc_analysis.CriterionError, match=message):
        mc_analysis.outcomes(_criterion_batch(), f"{column} > 1")


@pytest.mark.parametrize(
    ("text", "failing"),
    [
        ("vehicle.models.0.force_x < -0.5", [0]),
        ("max.v0.derived.alpha <= 1e-3", [0]),
        ("max.v0.derived.alpha > +2.0E-1", [3]),
        ("status == halt or vehicle.models.0.force_x > 2.5", [1, 3]),
        ("status != ok", [1]),
        # "and" binds tighter: a or (b and c). Run 0 (a, not b, not c) fails; under (a or b) and c
        # it would pass, and run 1 (b, not c) would differ the other way.
        ("a > 0.5 or b > 0.5 and c > 0.5", [0, 3]),
    ],
)
def test_mc_analysis_r10_accepted_criteria_and_error_rows_apart(
    text: str, failing: list[int]
) -> None:
    result = mc_analysis.outcomes(_criterion_batch(), text)
    assert result.runs_of("fail") == failing
    assert result.runs_of("error") == [2]  # whatever the criterion says of it


def test_mc_analysis_r10_an_integer_literal_compares_a_64_bit_seed_exactly() -> None:
    # Review R-003: 2^60 + 1 is not a double; as a float literal it would round onto 2^60.
    big = 2**60
    batch = mc_analysis.from_columns(
        {"run": [0, 1], "status": ["ok", "ok"], "seed": [big, big + 1], "a": [0.0, 1.0]}, ["a"]
    )
    assert mc_analysis.outcomes(batch, f"seed == {big + 1}").runs_of("fail") == [1]
    assert mc_analysis.outcomes(batch, f"seed > {big}").runs_of("fail") == [1]
    assert mc_analysis.outcomes(batch, f"seed <= {big}").runs_of("fail") == [0]
    assert mc_analysis.outcomes(batch, f"seed != {big + 1}").runs_of("fail") == [0]


@pytest.mark.parametrize(
    ("column", "message"),
    [
        ("nope", "unknown column 'nope'"),
        ("error_type", "not numeric"),
        ("a", "is a dispersion"),
        ("run", "not an outcome"),
    ],
)
def test_mc_analysis_r10_a_refused_metric_names_it(column: str, message: str) -> None:
    with pytest.raises(mc_analysis.CriterionError, match=message):
        mc_analysis.metric(_criterion_batch(), column)


# --- R15: astra mc analyze ----------------------------------------------------------------------


def test_mc_analysis_cli_analyze_prints_the_tables_and_writes_the_figures(
    descent_batch: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    before = _hashes(descent_batch)
    out = tmp_path / "out"
    code = cli.main(
        [
            "mc",
            "analyze",
            str(descent_batch),
            "--out",
            str(out),
            "--signal",
            ALTITUDE,
            "--signal",
            SLOW,
            "--metric",
            f"min.{ALTITUDE}",
        ]
    )
    captured = capsys.readouterr()
    assert code == 0, captured.err
    assert captured.err == ""
    text = captured.out
    summary = _summary(descent_batch)
    statuses = summary["status"]
    assert (
        f"runs: {len(statuses)} (ok {statuses.count('ok')}, halt {statuses.count('halt')}, "
        f"error {statuses.count('error')})"
    ) in text
    assert "ks against status == halt (q = 0.05, m = 3" in text
    assert f"spearman against min.{ALTITUDE}" in text
    assert f"prcc against min.{ALTITUDE}" in text
    assert f"excluded: {WIND_SEED} (seed dispersion: not an ordered quantity)" in text
    assert "tree (depth <= 3, grid 20, min leaf 5):" in text
    names = sorted(path.name for path in out.iterdir())
    assert names == [
        "mc-envelope-v0.derived.altitude.svg",
        "mc-envelope-v0.slow_rate.value.svg",
        "mc-hist.svg",
        "mc-scatter.svg",
    ]
    assert _hashes(descent_batch) == before  # the batch is read, never modified


@pytest.mark.parametrize(
    "argv",
    [
        ["mc", "analyze"],
        ["mc", "analyze", "{batch}", "--q", "1.5"],
        ["mc", "analyze", "{batch}", "--bogus"],
        ["mc"],
    ],
)
def test_mc_analysis_cli_analyze_usage_errors_exit_64(
    descent_batch: Path, argv: list[str], capsys: pytest.CaptureFixture[str]
) -> None:
    argv = [arg.replace("{batch}", str(descent_batch)) for arg in argv]
    assert cli.main(argv) == cli.EX_USAGE
    capsys.readouterr()


def test_mc_analysis_cli_analyze_refusals_exit_65(
    descent_batch: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    out = tmp_path / "out"
    code = cli.main(
        ["mc", "analyze", str(descent_batch), "--criterion", "nope > 1", "--out", str(out)]
    )
    assert code == cli.EX_DATAERR
    assert "unknown column 'nope'" in capsys.readouterr().err
    assert not out.exists()
    assert cli.main(["mc", "analyze", str(descent_batch), "--out", str(out)]) == 0
    capsys.readouterr()
    assert cli.main(["mc", "analyze", str(descent_batch), "--out", str(out)]) == cli.EX_DATAERR
    assert "already exists" in capsys.readouterr().err
    assert cli.main(["mc", "analyze", str(tmp_path / "none"), "--out", str(out)]) == cli.EX_DATAERR
    assert "cannot read the summary" in capsys.readouterr().err


# --- R15 and N-06: astra mc rerun ---------------------------------------------------------------


def _first(summary: dict[str, list[Any]], status: str) -> int:
    return cast(int, summary["run"][summary["status"].index(status)])


def test_mc_analysis_cli_rerun_recreates_a_deleted_log_byte_for_byte(
    descent_batch: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    batch = _copy(descent_batch, tmp_path)
    summary = _summary(batch)
    run = _first(summary, "halt")
    saved = tmp_path / "saved.h5"
    shutil.copy(batch / montecarlo.log_name(run), saved)
    for path in batch.glob("run_*.h5"):
        path.unlink()
    before = _hashes(batch)
    out = tmp_path / "rerun"
    code = cli.main(["mc", "rerun", str(batch), str(run), "--out", str(out)])
    captured = capsys.readouterr()
    assert code == 0, captured.err
    assert "row: reproduced" in captured.out
    assert "log bytes: not compared" in captured.out
    new = out / montecarlo.log_name(run)
    run_log = log.load(new)  # strict: complete and published
    assert run_log.complete
    assert run_log.published
    assert new.read_bytes() == saved.read_bytes()
    stored = yaml.safe_load(run_log.config)
    for key in (DESCENT, MASS, FORCE, WIND_SEED):
        assert _runkit.numeric_leaf(stored, key) == summary[key][run], key
    assert run_log.halt is not None
    assert run_log.frames_run == summary["frames_run"][run]
    figures = sorted(path.name for path in out.glob("*.svg"))
    assert any(name.startswith("flight-") for name in figures)
    assert any(name.startswith("health-") for name in figures)
    assert _hashes(batch) == before


def test_mc_analysis_rerun_with_the_original_log_compares_its_bytes(
    descent_batch: Path, tmp_path: Path
) -> None:
    summary = _summary(descent_batch)
    run = _first(summary, "ok")
    before = _hashes(descent_batch)
    result = montecarlo.rerun(descent_batch, run, tmp_path / "rerun")
    assert result.differences == ()
    assert result.log_identical is True
    assert result.reproduced
    assert _hashes(descent_batch) == before


def test_mc_analysis_cli_rerun_refusals_exit_65(
    descent_batch: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    summary = _summary(descent_batch)
    batch = str(descent_batch)
    busy = tmp_path / "busy"
    busy.mkdir()
    (busy / "x").write_bytes(b"x")
    cases = [
        ([batch, str(_first(summary, "ok")), "--out", str(busy)], "not empty"),
        ([batch, str(_first(summary, "error")), "--out", str(tmp_path / "e")], "is an error row"),
        ([batch, "999", "--out", str(tmp_path / "n")], "run 999 is not in the summary"),
        (
            [batch, "0", "--out", str(tmp_path / "p"), "--package", "nope"],
            "package error",
        ),
    ]
    for argv, message in cases:
        assert cli.main(["mc", "rerun", *argv]) == cli.EX_DATAERR, message
        assert message in capsys.readouterr().err


def test_mc_analysis_cli_rerun_refuses_a_batch_without_a_record(
    descent_batch: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    batch = _copy(descent_batch, tmp_path)
    path = batch / montecarlo.SUMMARY
    table = cast(Any, pq.read_table(path)).replace_schema_metadata(None)
    path.unlink()
    pq.write_table(table, path)
    code = cli.main(["mc", "rerun", str(batch), "0", "--out", str(tmp_path / "out")])
    assert code == cli.EX_DATAERR
    err = capsys.readouterr().err
    assert "records no spec" in err
    assert "before M6 PR 6" in err
    assert not (tmp_path / "out").exists()


def test_mc_analysis_cli_rerun_refuses_a_hand_edited_draw_naming_the_column(
    descent_batch: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    batch = _copy(descent_batch, tmp_path)
    path = batch / montecarlo.SUMMARY
    table: Any = pq.read_table(path)
    run = _first(_summary(batch), "ok")
    values = table.column(FORCE).to_pylist()
    values[run] += 1.0
    edited = table.set_column(
        table.schema.get_field_index(FORCE), table.schema.field(FORCE), [values]
    )
    path.unlink()
    pq.write_table(edited, path)
    code = cli.main(["mc", "rerun", str(batch), str(run), "--out", str(tmp_path / "out")])
    assert code == cli.EX_DATAERR
    assert f"run {run}: {FORCE}: the summary records" in capsys.readouterr().err


def test_mc_analysis_cli_rerun_reports_a_row_that_differs_and_exits_1(
    descent_batch: Path,
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    real = montecarlo.run_one

    def changed(
        spec: montecarlo.Spec, index: int, out_dir: str | Path, **kwargs: Any
    ) -> dict[str, Any]:
        row = real(spec, index, out_dir, **kwargs)
        row[f"max.{ALTITUDE}"] += 1.0
        return row

    monkeypatch.setattr(montecarlo, "run_one", changed)
    run = _first(_summary(descent_batch), "ok")
    out = tmp_path / "out"
    code = cli.main(["mc", "rerun", str(descent_batch), str(run), "--out", str(out)])
    captured = capsys.readouterr()
    assert code == cli.NOT_REPRODUCED
    assert f"row: differs in max.{ALTITUDE}" in captured.out
    assert "reproduced: no" in captured.out
    assert (out / montecarlo.log_name(run)).exists()  # the log is kept


def test_mc_analysis_cli_rerun_prints_the_comparison_before_a_render_failure(
    descent_batch: Path,
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    def fail(*_args: Any, **_kwargs: Any) -> list[Path]:
        raise OSError("disk full (injected)")

    monkeypatch.setattr(mc_analysis.plot, "render", fail)
    run = _first(_summary(descent_batch), "ok")
    out = tmp_path / "out"
    code = cli.main(["mc", "rerun", str(descent_batch), str(run), "--out", str(out)])
    captured = capsys.readouterr()
    assert code == cli.EX_IOERR
    assert "reproduced: yes" in captured.out
    assert "the log is kept; cannot write the figures: disk full (injected)" in captured.err
    assert (out / montecarlo.log_name(run)).exists()


@pytest.mark.parametrize(
    "argv",
    [
        ["mc", "rerun", "{batch}", "-1", "--out", "{out}"],
        ["mc", "rerun", "{batch}", "x", "--out", "{out}"],
        ["mc", "rerun", "{batch}", "0"],
    ],
)
def test_mc_analysis_cli_rerun_usage_errors_exit_64(
    descent_batch: Path, tmp_path: Path, argv: list[str], capsys: pytest.CaptureFixture[str]
) -> None:
    argv = [
        arg.replace("{batch}", str(descent_batch)).replace("{out}", str(tmp_path / "o"))
        for arg in argv
    ]
    assert cli.main(argv) == cli.EX_USAGE
    capsys.readouterr()
    assert not (tmp_path / "o").exists()


# --- review round 1: R-002 (never into the batch) and R-004 (a failed write is 74) ---------------


def test_mc_analysis_nothing_is_written_into_the_batch(
    descent_batch: Path,
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    batch = _copy(descent_batch, tmp_path)
    before = _hashes(batch)
    alias = tmp_path / "alias"
    alias.symlink_to(batch, target_is_directory=True)
    for out in (batch, batch / "figures", alias, alias / "figures"):
        assert cli.main(["mc", "analyze", str(batch), "--out", str(out)]) == cli.EX_DATAERR
        assert "never writes into the batch" in capsys.readouterr().err
    monkeypatch.chdir(batch)  # the default --out is "."
    assert cli.main(["mc", "analyze", str(batch)]) == cli.EX_DATAERR
    capsys.readouterr()
    run = _first(_summary(batch), "ok")
    for out in (batch / "rerun", alias / "rerun"):
        code = cli.main(["mc", "rerun", str(batch), str(run), "--out", str(out)])
        assert code == cli.EX_DATAERR
        assert "rerun never writes into a batch" in capsys.readouterr().err
    assert _hashes(batch) == before
    assert not (batch / "figures").exists()
    assert not (batch / "rerun").exists()


@pytest.mark.parametrize(
    "failure",
    [OSError(28, "No space left on device (injected)"), montecarlo.LogHaltError("LOG (injected)")],
)
def test_mc_analysis_cli_rerun_a_failed_write_exits_74_with_nothing_compared(
    descent_batch: Path,
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    monkeypatch: pytest.MonkeyPatch,
    failure: Exception,
) -> None:
    def fail(*_args: Any, **_kwargs: Any) -> log.RunLog:
        raise failure

    monkeypatch.setattr(_runkit, "run_logged", fail)
    run = _first(_summary(descent_batch), "ok")
    out = tmp_path / "out"
    code = cli.main(["mc", "rerun", str(descent_batch), str(run), "--out", str(out)])
    captured = capsys.readouterr()
    assert code == cli.EX_IOERR
    assert captured.out == ""  # no comparison was made
    assert "could not be written" in captured.err
    assert "(injected)" in captured.err
    assert not (out / montecarlo.log_name(run)).exists()
