"""astra.plot and ``astra plot`` (M6 PR 5, docs/plans/m6-pr5-plots.md): R1 (a package renders a
scenario log, every series exact), R2 (packages fail loudly, before any file), R3 (byte-identical
SVG across two processes; C-12), R3b (plotly data exact, nothing loaded from outside; C-11), C-15's
display-unit dispatch, PNG output and the CLI's codes. R4 is test_plot_new_aircraft.py, which
reuses ``check_package`` from here.

Every comparison of data is bitwise: float64, then equal as uint64. Expected values are computed
here, independently of astra.plot: the log's column, then each conversion's documented definition,
one IEEE division by the same constant core/util/units divides by (``x / 0.3048``,
``x / (1852.0 / 3600.0)``, ``x / (math.pi / 180.0)``), which is bitwise what the bound C++ function
returns.
"""

from __future__ import annotations

import base64
import json
import math
import os
import re
import subprocess
import sys
from collections.abc import Callable, Sequence
from pathlib import Path
from typing import Any, cast

import numpy as np
import pandas as pd
import pytest
import yaml

import resource_refs
from astra import _core, cli, log, plot
from astra import scenario as harness

ROOT = Path(__file__).resolve().parents[2]
ASTRA = Path(sys.executable).parent / "astra"
ELEVATOR_STEP = ROOT / "tests/scenarios/elevator_step.yaml"
TEMPLATE_PLOTS = ROOT / "aircraft/_template/plots"
NOTIONAL = ROOT / "aircraft/notional_transport/plots/cruise.yaml"
SHIPPED = [
    plot.framework_package("flight"),
    plot.framework_package("attitude"),
    plot.framework_package("health"),
    TEMPLATE_PLOTS / "controls.yaml",
    TEMPLATE_PLOTS / "wrench.yaml",
    TEMPLATE_PLOTS / "ground_track.yaml",
    NOTIONAL,
]

FloatArray = np.ndarray[Any, np.dtype[np.float64]]

# The documented definitions, independent of astra.plot's table and of _core.
INDEPENDENT: dict[tuple[str, str], Callable[[FloatArray], FloatArray]] = {
    ("m", "ft"): lambda x: x / 0.3048,
    ("m/s", "kt"): lambda x: x / (1852.0 / 3600.0),
    ("m/s", "ft/s"): lambda x: x / 0.3048,
    ("rad", "deg"): lambda x: x / (math.pi / 180.0),
    ("rad/s", "deg/s"): lambda x: x / (math.pi / 180.0),
}


# --- helpers ------------------------------------------------------------------------------------


def assert_bits(actual: Any, expected: FloatArray, what: str) -> None:
    array = np.asarray(actual)
    assert array.dtype == np.float64, f"{what}: dtype {array.dtype}"
    assert array.shape == expected.shape, f"{what}: shape {array.shape} != {expected.shape}"
    assert np.array_equal(array.view(np.uint64), expected.view(np.uint64)), f"{what}: data differ"


def log_vehicles(run_log: log.RunLog) -> list[int]:
    """The test's own reading of the vehicles in a log: every v<index>. prefix, ascending."""
    found = {int(m.group(1)) for n in run_log.units if (m := re.match(r"v(\d+)\.", n))}
    return sorted(found)


def log_column(run_log: log.RunLog, signal: str) -> tuple[FloatArray, FloatArray]:
    for frame in run_log.rates.values():
        if signal in frame.columns:
            data = cast(Any, frame)
            return (
                np.asarray(data["t"].to_numpy(), dtype=np.float64),
                np.asarray(data[signal].to_numpy(), dtype=np.float64),
            )
    raise AssertionError(f"{signal} is not in the log")


def expected_series(
    run_log: log.RunLog, spec: plot.SeriesSpec
) -> list[tuple[str, FloatArray, FloatArray, str]]:
    """(label, x, y, display unit) for each line a YAML series must draw."""
    if "{v}" in spec.signal:
        names = [(spec.signal.replace("{v}", str(i)), i) for i in log_vehicles(run_log)]
    else:
        names = [(spec.signal, None)]
    lines: list[tuple[str, FloatArray, FloatArray, str]] = []
    for signal, vehicle in names:
        t, values = log_column(run_log, signal)
        log_unit = run_log.units[signal]
        unit = spec.unit if spec.unit is not None else log_unit
        if unit != log_unit:
            values = INDEPENDENT[(log_unit, unit)](values)
        label = spec.label if spec.label is not None else signal
        if vehicle is not None and spec.label is not None:
            label = f"v{vehicle} {label}"
        lines.append((label, t, values, unit))
    return lines


def ground_track(run_log: log.RunLog) -> list[tuple[str, FloatArray, FloatArray]]:
    """The template's ground track, recomputed: displacement from the start, each row's NED velocity
    held over the step to the next row."""
    lines: list[tuple[str, FloatArray, FloatArray]] = []
    for i in log_vehicles(run_log):
        t, north_velocity = log_column(run_log, f"v{i}.derived.v_n_0")
        _, east_velocity = log_column(run_log, f"v{i}.derived.v_n_1")
        north = np.concatenate(([0.0], np.cumsum(north_velocity[:-1] * np.diff(t))))
        east = np.concatenate(([0.0], np.cumsum(east_velocity[:-1] * np.diff(t))))
        lines.append((f"v{i}", east, north))
    return lines


def display_scale(drawn: Any, ax: Any) -> tuple[float, float]:
    """Display units per data unit along x and along y, from matplotlib's own data transform after
    layout (M6 PR 12): the diagonal of ``transData``'s affine part. No pixel is compared."""
    drawn.draw_without_rendering()  # applies the aspect and the layout, so the limits are final
    matrix = ax.transData.get_affine().get_matrix()
    assert matrix[0][1] == 0.0, matrix
    assert matrix[1][0] == 0.0, matrix
    return float(matrix[0][0]), float(matrix[1][1])


# matplotlib's own tolerance for an equal aspect: Axes.apply_aspect leaves the limits as they are
# when the scale ratio it would correct, x_scale / y_scale - 1, is under 0.005 (matplotlib 3.11.2,
# axes/_base.py, ``abs(y_expander) < 0.005``). An unequal autoscale misses by far more: the
# straight-track control below is off by over 1000 times.
_ASPECT_TOLERANCE = 0.005


def assert_equal_scale(drawn: Any, ax: Any, what: str) -> None:
    """An equal-aspect panel: one data unit is the same length on both axes (to matplotlib's own
    tolerance), and matplotlib got there by moving the limits (``datalim``), not by shrinking the
    axes."""
    assert ax.get_aspect() == 1.0, f"{what}: aspect {ax.get_aspect()!r}"
    assert ax.get_adjustable() == "datalim", f"{what}: adjustable {ax.get_adjustable()!r}"
    x_scale, y_scale = display_scale(drawn, ax)
    assert x_scale > 0.0, (x_scale, y_scale)
    assert y_scale > 0.0, (x_scale, y_scale)
    ratio = x_scale / y_scale
    assert abs(ratio - 1.0) <= _ASPECT_TOLERANCE, f"{what}: x {x_scale} / y {y_scale} = {ratio}"


def assert_legend_outside(drawn: Any, ax: Any) -> None:
    """M6 PR 6, D1: the legend lies wholly to the right of its axes, so it covers no data."""
    drawn.draw_without_rendering()  # lays the figure out, so the extents are final
    legend = ax.get_legend()
    assert legend is not None
    assert legend.get_window_extent().x0 >= ax.get_window_extent().x1


def check_package(path: Path, run_log: log.RunLog, out_dir: Path) -> list[Path]:
    """R1's checks for one package (also R4's): every figure written under its name, and every drawn
    line's data equal to the expected array, in order, with the display unit on its axis."""
    package = plot.load_package(path)
    figures = plot.resolve(package, run_log)
    written = plot.render(package, run_log, out_dir)
    assert written == [out_dir / f"{package.name}-{f.name}.svg" for f in figures]
    assert all(p.is_file() and p.stat().st_size > 0 for p in written)

    if package.function is not None:
        assert package.path.name == "ground_track.yaml", "only the ground track has an oracle here"
        (figure,) = figures
        drawn = plot.draw_matplotlib(figure)
        (ax,) = drawn.axes
        expected = ground_track(run_log)
        lines = ax.get_lines()
        assert len(lines) == len(expected) == len(log_vehicles(run_log))
        for line, (label, x, y) in zip(lines, expected, strict=True):
            assert line.get_label() == label
            assert_bits(line.get_xdata(), x, f"{path} {label} x")
            assert_bits(line.get_ydata(), y, f"{path} {label} y")
        assert ax.get_xlabel() == "east [m]"
        assert ax.get_ylabel() == "north [m]"
        # M6 PR 12: the track's one panel is equal-aspect, and drawn so.
        assert [panel.aspect for panel in figure.panels] == ["equal"]
        for drawn_ax, panel in zip(drawn.axes, figure.panels, strict=True):
            if panel.aspect == "equal":
                assert_equal_scale(drawn, drawn_ax, f"{path} {figure.name}")
        return written

    assert package.figures is not None
    assert [f.name for f in figures] == [f.name for f in package.figures]
    for figure, spec in zip(figures, package.figures, strict=True):
        drawn = plot.draw_matplotlib(figure)
        assert len(drawn.axes) == len(spec.panels)
        for ax, panel, panel_spec in zip(drawn.axes, figure.panels, spec.panels, strict=True):
            expected = [line for s in panel_spec.series for line in expected_series(run_log, s)]
            lines = ax.get_lines()
            assert len(lines) == len(expected) == len(panel.series)
            legend = ax.get_legend()
            assert legend is not None
            assert [t.get_text() for t in legend.get_texts()] == [e[0] for e in expected]
            assert_legend_outside(drawn, ax)
            for line, resolved, (label, x, y, unit) in zip(
                lines, panel.series, expected, strict=True
            ):
                what = f"{path} {figure.name} {label}"
                assert line.get_label() == label == resolved.label
                assert_bits(resolved.x, x, f"{what} resolved x")
                assert_bits(resolved.y, y, f"{what} resolved y")
                assert_bits(line.get_xdata(), x, f"{what} x")
                assert_bits(line.get_ydata(), y, f"{what} y")
                assert panel.unit == unit
            unit = expected[0][3]
            if unit:
                assert ax.get_ylabel().endswith(f"[{unit}]"), ax.get_ylabel()
        assert drawn.axes[-1].get_xlabel() == "t [s]"
    return written


def synthetic_log(columns: dict[str, tuple[str, Sequence[float]]], dt: float = 0.001) -> log.RunLog:
    """A RunLog built in memory, one rate group. The sim has one vehicle (ADR-0011's list of one),
    so a log with several, or with a vehicle missing a signal, can only be built this way."""
    frames = len(next(iter(columns.values()))[1])
    data: dict[str, Any] = {"t": np.arange(frames) * dt}
    data |= {name: np.asarray(values, dtype=np.float64) for name, (_, values) in columns.items()}
    frame = pd.DataFrame(data, index=pd.Index(np.arange(frames), name="frame"))
    return log.RunLog(
        path=Path("synthetic.h5"),
        complete=True,
        state=log.COMPLETE,
        published=True,
        rows_committed={1: frames},
        format_version=log.FORMAT_VERSION,
        config="",
        dt=dt,
        git_describe="",
        core_version="",
        core_api_version=_core.core_api_version(),
        rates={1: frame},
        units={name: unit for name, (unit, _) in columns.items()},
        plugins=None,
        writes=None,
        frames_run=frames,
        halt=None,
        health={},
        minimum=None,
        maximum=None,
        final_state={},
        final_frame=None,
        final_after_halt=None,
    )


def write_package(directory: Path, text: str, name: str = "pkg") -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"{name}.yaml"
    path.write_text(text, encoding="utf-8")
    return path


def one_series(signal: str, unit: str | None = None) -> str:
    line = f"\n            unit: {unit}" if unit is not None else ""
    return f"""figures:
  - name: fig
    panels:
      - series:
          - signal: {signal}{line}
"""


@pytest.fixture(scope="module")
def scenario_log(tmp_path_factory: pytest.TempPathFactory) -> log.RunLog:
    """One real log: the elevator-step scenario (notional transport, still air), run, verified."""
    directory = tmp_path_factory.mktemp("scenario")
    result = harness.run(harness.load(ELEVATOR_STEP, root=ROOT), directory / "elevator_step.h5")
    assert result.failures == []
    return result.log


# --- R1 -----------------------------------------------------------------------------------------


@pytest.mark.parametrize("path", SHIPPED, ids=lambda p: f"{p.parent.name}/{p.stem}")
def test_r1_every_shipped_package_renders_the_scenario_log_exactly(
    path: Path, scenario_log: log.RunLog, tmp_path: Path
) -> None:
    written = check_package(path, scenario_log, tmp_path / "out")
    assert sorted(p.name for p in (tmp_path / "out").iterdir()) == sorted(p.name for p in written)


def test_r1_a_log_has_exactly_one_vehicle_and_every_shipped_package_uses_it(
    scenario_log: log.RunLog,
) -> None:
    assert log_vehicles(scenario_log) == [0]


def test_vehicle_expansion_covers_every_vehicle_in_numeric_order(tmp_path: Path) -> None:
    run_log = synthetic_log(
        {
            "v10.derived.alpha": ("rad", [0.3, 0.4]),
            "v0.derived.alpha": ("rad", [0.1, 0.2]),
            "v2.derived.alpha": ("rad", [0.5, 0.6]),
        }
    )
    text = one_series("v{v}.derived.alpha", "deg").replace(
        "unit: deg", "unit: deg\n            label: alpha"
    )
    package = plot.load_package(write_package(tmp_path, text))
    ((panel,),) = [f.panels for f in plot.resolve(package, run_log)]
    assert [s.label for s in panel.series] == ["v0 alpha", "v2 alpha", "v10 alpha"]
    for line, vehicle in zip(panel.series, (0, 2, 10), strict=True):
        _, values = log_column(run_log, f"v{vehicle}.derived.alpha")
        assert_bits(line.y, values / (math.pi / 180.0), f"v{vehicle}")


# --- R2 and the other refusals --------------------------------------------------------------------

_LOG_COLUMNS: dict[str, tuple[str, Sequence[float]]] = {
    "v0.derived.alpha": ("rad", [0.1, 0.2, 0.3]),
    "v0.derived.mach": ("1", [0.7, 0.7, 0.7]),
    "v0.derived.altitude": ("m", [100.0, 101.0, 102.0]),
    "v0.state.p_n_0": ("", [0.0, 1.0, 2.0]),
    "v1.derived.mach": ("1", [0.6, 0.6, 0.6]),
    "health.nan": ("1", [0.0, 0.0, 0.0]),
}
_FIG = "figures:\n  - name: fig\n    panels:\n      - series:\n"


_FN_HEADER = "import numpy as np\nfrom astra import plot\n"
_GOOD_SERIES = "plot.Series('a', np.zeros(3), np.zeros(3))"

# (id, package text, key the message names after the file, text the message holds, fn.py source)
_REFUSALS: list[tuple[str, str, str, str, str | None]] = [
    ("unknown-top-key", "figure: []\n", "top level", "unknown key 'figure'", None),
    (
        "unknown-figure-key",
        "figures:\n  - name: fig\n    panel: []\n    panels:\n      - series:\n"
        "          - signal: health.nan\n",
        "figures[0]",
        "unknown key 'panel'",
        None,
    ),
    (
        "unknown-panel-key",
        _FIG + "          - signal: health.nan\n        xlabel: x\n",
        "figures[0].panels[0]",
        "unknown key 'xlabel'",
        None,
    ),
    (
        "unknown-series-key",
        _FIG + "          - signal: health.nan\n            units: deg\n",
        "figures[0].panels[0].series[0]",
        "unknown key 'units'",
        None,
    ),
    (
        "unknown-signal",
        one_series("v0.derived.alphaa"),
        "figures[0].panels[0].series[0].signal",
        "'v0.derived.alphaa' is not a signal",
        None,
    ),
    (
        "undefined-conversion",
        one_series("v0.derived.mach", "deg"),
        "figures[0].panels[0].series[0].unit",
        "no conversion from 'v0.derived.mach''s log unit '1' to 'deg' (defined for '1': none)",
        None,
    ),
    (
        "unknown-display-unit",
        one_series("v0.derived.alpha", "furlong"),
        "figures[0].panels[0].series[0].unit",
        "to 'furlong' (defined for 'rad': deg)",
        None,
    ),
    ("empty-figures", "figures: []\n", "figures", "must not be empty", None),
    (
        "empty-panels",
        "figures:\n  - name: fig\n    panels: []\n",
        "figures[0].panels",
        "must not be empty",
        None,
    ),
    (
        "empty-series",
        "figures:\n  - name: fig\n    panels:\n      - series: []\n",
        "figures[0].panels[0].series",
        "must not be empty",
        None,
    ),
    (
        "vehicle-without-the-signal",
        one_series("v{v}.derived.alpha"),
        "figures[0].panels[0].series[0].signal",
        "'v1.derived.alpha' (vehicle 1) is not a signal",
        None,
    ),
    (
        "duplicate-key",
        _FIG + "          - signal: health.nan\n            signal: health.nan\n",
        "line 6, column 13",
        "found duplicate key 'signal'",
        None,
    ),
    (
        "boolean-label",
        _FIG + "          - signal: health.nan\n            label: no\n",
        "figures[0].panels[0].series[0].label",
        "got False (YAML 1.1 reads on, off, yes, no, true and false as booleans: quote it)",
        None,
    ),
    (
        "flow-map-label",
        _FIG + "          - signal: health.nan\n            label: {v}\n",
        "figures[0].panels[0].series[0].label",
        "is a YAML flow map: quote it",
        None,
    ),
    (
        "mixed-units",
        _FIG + "          - signal: v0.derived.alpha\n"
        "          - signal: v0.derived.alpha\n            unit: deg\n",
        "figures[0].panels[0].series[1].unit",
        "display unit 'deg' differs from 'rad'",
        None,
    ),
    (
        "duplicate-figure-name",
        "figures:\n"
        + "".join(
            "  - name: fig\n    panels:\n      - series:\n          - signal: health.nan\n"
            for _ in range(2)
        ),
        "figures[1].name",
        "'fig' is already a figure's name",
        None,
    ),
    (
        "bad-figure-name",
        "figures:\n  - name: Air Data\n    panels:\n      - series:\n"
        "          - signal: health.nan\n",
        "figures[0].name",
        "must match [a-z0-9_]+",
        None,
    ),
    (
        "other-placeholder",
        one_series("v{x}.derived.alpha"),
        "figures[0].panels[0].series[0].signal",
        "{v} is the only placeholder",
        None,
    ),
    (
        "figures-and-function",
        one_series("health.nan") + "function: { file: fn.py, name: figures }\n",
        "top level",
        "exactly one of figures and function",
        None,
    ),
    ("neither", "title: nothing\n", "top level", "exactly one of figures and function", None),
    (
        "module-and-file",
        "function: { module: os, file: fn.py, name: figures }\n",
        "function",
        "exactly one of module and file",
        None,
    ),
    (
        "module-not-importable",
        "function: { module: astra_no_such_module_m6pr5, name: figures }\n",
        "function.module",
        "cannot import 'astra_no_such_module_m6pr5': ModuleNotFoundError",
        None,
    ),
    (
        "missing-file",
        "function: { file: absent.py, name: figures }\n",
        "function.file",
        "absent.py is not a file",
        None,
    ),
    (
        "file-with-a-path",
        "function: { file: ../fn.py, name: figures }\n",
        "function.file",
        "must be a bare .py file name",
        None,
    ),
    (
        "name-not-callable",
        "function: { file: fn.py, name: figures }\n",
        "function.name",
        "fn.py:figures is not a callable",
        _FN_HEADER + "figures = 3\n",
    ),
    (
        "not-a-list",
        "function: { file: fn.py, name: figures }\n",
        "function",
        "must give a list of astra.plot.Figure, got dict",
        _FN_HEADER + "def figures(run_log):\n    return {}\n",
    ),
    (
        "empty-list",
        "function: { file: fn.py, name: figures }\n",
        "function",
        "gave no figure",
        _FN_HEADER + "def figures(run_log):\n    return []\n",
    ),
    (
        "length-mismatch",
        "function: { file: fn.py, name: figures }\n",
        "function[0].panels[0].series[0]",
        "x has 3 points and y has 2",
        _FN_HEADER + "def figures(run_log):\n"
        "    s = plot.Series('a', np.zeros(3), np.zeros(2))\n"
        "    return [plot.Figure('f', '', [plot.Panel('', '', '', [s])])]\n",
    ),
    (
        "not-float64",
        "function: { file: fn.py, name: figures }\n",
        "function[0].panels[0].series[0].x",
        "must be 1-D float64, got 1-D int64",
        _FN_HEADER + "def figures(run_log):\n"
        "    s = plot.Series('a', np.arange(3), np.zeros(3))\n"
        "    return [plot.Figure('f', '', [plot.Panel('', '', '', [s])])]\n",
    ),
    (
        "function-raises",
        "function: { file: fn.py, name: figures }\n",
        "function",
        "fn.py:figures raised ZeroDivisionError",
        _FN_HEADER + "def figures(run_log):\n    return 1 / 0\n",
    ),
    (
        "empty-panel-from-function",
        "function: { file: fn.py, name: figures }\n",
        "function[0].panels",
        "must be a non-empty sequence of Panel",
        _FN_HEADER + "def figures(run_log):\n    return [plot.Figure('f', '', [])]\n",
    ),
]


@pytest.mark.parametrize(
    ("text", "key", "problem", "source"),
    [pytest.param(*case[1:], id=case[0]) for case in _REFUSALS],
)
def test_r2_a_refused_package_names_the_file_and_key_and_writes_nothing(
    tmp_path: Path, text: str, key: str, problem: str, source: str | None
) -> None:
    path = write_package(tmp_path / "pkg", text)
    if source is not None:
        (tmp_path / "pkg" / "fn.py").write_text(source, encoding="utf-8")
    out = tmp_path / "out"
    with pytest.raises(plot.PlotPackageError) as caught:
        plot.render(plot.load_package(path), synthetic_log(_LOG_COLUMNS), out)
    message = str(caught.value)
    assert message.startswith(f"{path}: {key}: "), message
    assert problem in message, message
    assert not out.exists()


def test_r2_a_vehicle_series_against_a_log_with_no_vehicle(tmp_path: Path) -> None:
    path = write_package(tmp_path, one_series("v{v}.derived.alpha"))
    run_log = synthetic_log({"health.nan": ("1", [0.0, 0.0])})
    out = tmp_path / "out"
    with pytest.raises(plot.PlotPackageError) as caught:
        plot.render(plot.load_package(path), run_log, out)
    assert str(caught.value).startswith(
        f"{path}: figures[0].panels[0].series[0].signal: 'v{{v}}.derived.alpha' needs a vehicle"
    )
    assert not out.exists()


def test_r2_a_yaml_syntax_error_names_the_line_and_column(tmp_path: Path) -> None:
    # PyYAML raises a bare ParserError on this: a flow map cannot be followed by more text.
    text = _FIG + "          - signal: v{v}.derived.altitude\n            label: {v} altitude\n"
    path = write_package(tmp_path, text)
    with pytest.raises(plot.PlotPackageError) as caught:
        plot.load_package(path)
    assert re.fullmatch(
        rf"{re.escape(str(path))}: line 6, column \d+: YAML error: .+", str(caught.value)
    ), str(caught.value)


def test_a_package_file_name_that_cannot_name_an_output_is_refused(tmp_path: Path) -> None:
    path = write_package(tmp_path, one_series("health.nan"), name="my plots")
    with pytest.raises(plot.PlotPackageError, match="the package name 'my plots'"):
        plot.load_package(path)


def test_an_existing_output_is_refused_untouched_and_nothing_else_is_written(
    tmp_path: Path,
) -> None:
    text = "figures:\n" + "".join(
        f"  - name: {name}\n    panels:\n      - series:\n          - signal: health.nan\n"
        for name in ("first", "second")
    )
    path = write_package(tmp_path, text)
    out = tmp_path / "out"
    out.mkdir()
    (out / "pkg-second.svg").write_bytes(b"keep")
    with pytest.raises(plot.PlotOutputExistsError, match=r"pkg-second\.svg: already exists"):
        plot.render(plot.load_package(path), synthetic_log(_LOG_COLUMNS), out, html=True)
    assert sorted(p.name for p in out.iterdir()) == ["pkg-second.svg"]
    assert (out / "pkg-second.svg").read_bytes() == b"keep"


def test_a_failed_write_deletes_only_the_files_it_created(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    text = "figures:\n" + "".join(
        f"  - name: {name}\n    panels:\n      - series:\n          - signal: health.nan\n"
        for name in ("first", "second")
    )
    path = write_package(tmp_path, text)
    out = tmp_path / "out"
    out.mkdir()
    (out / "unrelated.svg").write_bytes(b"theirs")
    real = plot.draw_matplotlib

    def draw(figure: plot.Figure) -> Any:
        if figure.name == "second":
            raise OSError("disk full (injected)")
        return real(figure)

    monkeypatch.setattr(plot, "draw_matplotlib", draw)
    with pytest.raises(OSError, match="injected"):
        plot.render(plot.load_package(path), synthetic_log(_LOG_COLUMNS), out)
    assert sorted(p.name for p in out.iterdir()) == ["unrelated.svg"]


def test_a_file_that_appears_during_the_render_is_left_untouched(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    # Past the up-front check, each file is still created exclusively: one that appears in between
    # is refused, never replaced, and the files this render created are deleted.
    text = "figures:\n" + "".join(
        f"  - name: {name}\n    panels:\n      - series:\n          - signal: health.nan\n"
        for name in ("first", "second")
    )
    path = write_package(tmp_path, text)
    out = tmp_path / "out"
    real = plot.draw_matplotlib

    def draw(figure: plot.Figure) -> Any:
        if figure.name == "second":
            (out / "pkg-second.svg").write_bytes(b"theirs")
        return real(figure)

    monkeypatch.setattr(plot, "draw_matplotlib", draw)
    with pytest.raises(plot.PlotOutputExistsError, match=r"pkg-second\.svg: already exists"):
        plot.render(plot.load_package(path), synthetic_log(_LOG_COLUMNS), out)
    assert sorted(p.name for p in out.iterdir()) == ["pkg-second.svg"]
    assert (out / "pkg-second.svg").read_bytes() == b"theirs"


def test_hashed_svg_ids_differ_between_figures_of_the_same_shape(tmp_path: Path) -> None:
    # Two figures with identical axes would hash to identical clip-path ids under one salt; the
    # salt is per figure, so a report holding both SVGs does not see the ids collide.
    text = "figures:\n" + "".join(
        f"  - name: {name}\n    panels:\n      - series:\n          - signal: health.nan\n"
        for name in ("first", "second")
    )
    path = write_package(tmp_path, text)
    first, second = plot.render(plot.load_package(path), synthetic_log(_LOG_COLUMNS), tmp_path)
    ids = [
        set(re.findall(r'id="(p[0-9a-f]{10})"', p.read_text(encoding="utf-8")))
        for p in (first, second)
    ]
    assert ids[0]
    assert ids[1]
    assert not ids[0] & ids[1]


def test_a_module_function_renders(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    module = "astra_plot_test_module_m6pr5"
    (tmp_path / f"{module}.py").write_text(
        _FN_HEADER + "def figures(run_log):\n"
        "    t, y = plot.column(run_log, 'v0.derived.alpha')\n"
        "    return [plot.Figure('alpha', 'Alpha', [plot.Panel('', 'alpha', 'rad', "
        "[plot.Series('alpha', t, y)])])]\n",
        encoding="utf-8",
    )
    monkeypatch.syspath_prepend(str(tmp_path))
    monkeypatch.delitem(sys.modules, module, raising=False)
    path = write_package(tmp_path / "pkg", f"function: {{ module: {module}, name: figures }}\n")
    run_log = synthetic_log(_LOG_COLUMNS)
    written = plot.render(plot.load_package(path), run_log, tmp_path / "out")
    assert [p.name for p in written] == ["pkg-alpha.svg"]
    (figure,) = plot.resolve(plot.load_package(path), run_log)
    t, values = log_column(run_log, "v0.derived.alpha")
    assert_bits(figure.panels[0].series[0].x, t, "x")
    assert_bits(figure.panels[0].series[0].y, values, "y")


def test_a_file_function_leaves_nothing_beside_it(tmp_path: Path) -> None:
    directory = tmp_path / "pkg"
    path = write_package(directory, "function: { file: fn.py, name: figures }\n")
    (directory / "fn.py").write_text(
        _FN_HEADER + "def figures(run_log):\n"
        f"    return [plot.Figure('f', '', [plot.Panel('', '', '', [{_GOOD_SERIES}])])]\n",
        encoding="utf-8",
    )
    plot.render(plot.load_package(path), synthetic_log(_LOG_COLUMNS), tmp_path / "out")
    assert sorted(p.name for p in directory.iterdir()) == ["fn.py", "pkg.yaml"]


# --- M6 PR 12: each panel at a readable scale (docs/plans/m6-pr12-plots-fixes.md) ---------------

_GROUND_TRACK = TEMPLATE_PLOTS / "ground_track.yaml"
_TRACK_DT = 1.0  # s; 301 rows over 300 s
_TRACK_T = np.arange(301) * _TRACK_DT


def _track_log(north_velocity: FloatArray, east_velocity: FloatArray) -> log.RunLog:
    return synthetic_log(
        {
            "v0.derived.v_n_0": ("m/s", north_velocity.tolist()),
            "v0.derived.v_n_1": ("m/s", east_velocity.tolist()),
        },
        dt=_TRACK_DT,
    )


# Straight: 69 km north with 1 m of east drift. Turn: one full circle at 230 m/s. Still: no motion.
_OMEGA = 2.0 * math.pi / 300.0
_TRACKS: dict[str, tuple[FloatArray, FloatArray]] = {
    "straight": (np.full(301, 230.0), np.full(301, 1.0 / 300.0)),
    "turn": (230.0 * np.cos(_OMEGA * _TRACK_T), 230.0 * np.sin(_OMEGA * _TRACK_T)),
    "still": (np.zeros(301), np.zeros(301)),
}


def _drawn_track(run_log: log.RunLog) -> tuple[Any, Any]:
    (figure,) = plot.resolve(plot.load_package(_GROUND_TRACK), run_log)
    drawn = plot.draw_matplotlib(figure)
    (ax,) = drawn.axes
    return drawn, ax


@pytest.mark.parametrize("track", list(_TRACKS))
def test_panel_scale_ground_track_draws_meters_equal_on_both_axes(track: str) -> None:
    drawn, ax = _drawn_track(_track_log(*_TRACKS[track]))
    assert_equal_scale(drawn, ax, track)
    x_low, x_high = ax.get_xlim()
    y_low, y_high = ax.get_ylim()
    for low, high in ((x_low, x_high), (y_low, y_high)):
        assert math.isfinite(low), (track, low, high)
        assert math.isfinite(high), (track, low, high)
        assert high > low, (track, low, high)
    if track == "straight":
        # The east limits widen to the north scale: they span the north span times the axes'
        # width over height, so 1 m of drift is drawn at its size, not zoomed to fill the panel.
        box = ax.get_window_extent()
        x_span, y_span = x_high - x_low, y_high - y_low
        assert y_span >= 69_000.0, y_span
        assert x_span >= y_span * (box.width / box.height) * (1.0 - 1e-9), (x_span, y_span)


def test_panel_scale_auto_aspect_is_the_default_and_unequal() -> None:
    # The negative control: the same straight track on a panel without an aspect autoscales each
    # axis, so a meter east is drawn far longer than a meter north (the assertion above bites).
    run_log = _track_log(*_TRACKS["straight"])
    (figure,) = plot.resolve(plot.load_package(_GROUND_TRACK), run_log)
    (equal,) = figure.panels
    auto = plot.Panel(equal.title, equal.ylabel, equal.unit, equal.series, xlabel="east", xunit="m")
    assert auto.aspect == "auto"
    drawn = plot.draw_matplotlib(plot.Figure(figure.name, figure.title, [auto]))
    (ax,) = drawn.axes
    assert ax.get_aspect() == "auto"
    x_scale, y_scale = display_scale(drawn, ax)
    assert x_scale > 1000.0 * y_scale, (x_scale, y_scale)


def test_panel_scale_plotly_anchors_the_y_axis_on_an_equal_panel() -> None:
    run_log = _track_log(*_TRACKS["turn"])
    (figure,) = plot.resolve(plot.load_package(_GROUND_TRACK), run_log)
    layout = cast(dict[str, Any], plot.draw_plotly(figure).to_dict())["layout"]
    assert layout["yaxis"]["scaleanchor"] == "x"
    assert layout["yaxis"]["scaleratio"] == 1
    (panel,) = figure.panels
    auto = plot.Panel(panel.title, panel.ylabel, panel.unit, panel.series, "east", "m")
    layout = cast(dict[str, Any], plot.draw_plotly(plot.Figure("f", "", [auto])).to_dict())[
        "layout"
    ]
    assert "scaleanchor" not in layout["yaxis"]
    assert "scaleratio" not in layout["yaxis"]


def _refused_function(tmp_path: Path, panels: str) -> tuple[Path, str]:
    """Renders a file function returning one figure of ``panels``; the refusal's message."""
    directory = tmp_path / "pkg"
    path = write_package(directory, "function: { file: fn.py, name: figures }\n")
    (directory / "fn.py").write_text(
        _FN_HEADER + f"def figures(run_log):\n    return [plot.Figure('f', '', [{panels}])]\n",
        encoding="utf-8",
    )
    out = tmp_path / "out"
    with pytest.raises(plot.PlotPackageError) as caught:
        plot.render(plot.load_package(path), synthetic_log(_LOG_COLUMNS), out)
    assert not out.exists()
    return path, str(caught.value)


def test_panel_scale_a_bad_aspect_is_refused(tmp_path: Path) -> None:
    path, message = _refused_function(
        tmp_path, f"plot.Panel('', '', '', [{_GOOD_SERIES}], aspect='square')"
    )
    assert message == (
        f"{path}: function[0].panels[0].aspect: must be 'auto' or 'equal', got 'square'"
    )


def test_panel_scale_an_equal_panel_must_be_its_figure_only_panel(tmp_path: Path) -> None:
    # Amendment 1: panels share x when their x labels match, and a shared x would make
    # matplotlib's datalim move the y limits, so an equal panel stands alone in its figure.
    path, message = _refused_function(
        tmp_path,
        f"plot.Panel('', '', '', [{_GOOD_SERIES}]), "
        f"plot.Panel('', '', '', [{_GOOD_SERIES}], aspect='equal')",
    )
    assert message == (
        f"{path}: function[0].panels[1].aspect: an 'equal' panel must be its figure's only "
        "panel (this figure has 2)"
    )


def test_panel_scale_tas_and_cas_have_their_own_panels(scenario_log: log.RunLog) -> None:
    package = plot.load_package(plot.framework_package("flight"))
    assert package.figures is not None
    (air_data,) = [figure for figure in package.figures if figure.name == "air_data"]
    assert [
        (panel.ylabel, [(s.signal, s.unit) for s in panel.series]) for panel in air_data.panels
    ] == [
        ("altitude", [("v{v}.derived.altitude", "ft")]),
        ("TAS", [("v{v}.derived.tas", "kt")]),
        ("CAS", [("v{v}.derived.cas", "kt")]),
        ("Mach", [("v{v}.derived.mach", None)]),
        ("vertical speed", [("v{v}.derived.vertical_speed", "ft/s")]),
    ]
    (figure,) = [f for f in plot.resolve(package, scenario_log) if f.name == "air_data"]
    vehicles = log_vehicles(scenario_log)
    assert [[s.label for s in panel.series] for panel in figure.panels] == [
        [f"v{v} {name}" for v in vehicles]
        for name in ("altitude", "TAS", "CAS", "Mach", "vertical speed")
    ]
    assert all(panel.aspect == "auto" for panel in figure.panels)


# --- C-15: display-unit dispatch ------------------------------------------------------------------

_BOUND = (
    "feet_to_meters",
    "meters_to_feet",
    "knots_to_meters_per_second",
    "meters_per_second_to_knots",
    "degrees_to_radians",
    "radians_to_degrees",
)
_ROWS = [
    ("m", "ft", "meters_to_feet"),
    ("m/s", "kt", "meters_per_second_to_knots"),
    ("m/s", "ft/s", "meters_to_feet"),
    ("rad", "deg", "radians_to_degrees"),
    ("rad/s", "deg/s", "radians_to_degrees"),
]


def _resolve_one(tmp_path: Path, log_unit: str, unit: str, values: Sequence[float]) -> FloatArray:
    run_log = synthetic_log({"x.signal": (log_unit, values)})
    path = write_package(tmp_path, one_series("x.signal", f'"{unit}"'))
    (figure,) = plot.resolve(plot.load_package(path), run_log)
    return figure.panels[0].series[0].y


def test_the_table_is_exactly_the_documented_rows() -> None:
    assert sorted(plot.CONVERSIONS) == sorted((log_unit, unit) for log_unit, unit, _ in _ROWS)


@pytest.mark.parametrize(("log_unit", "unit", "function"), _ROWS)
def test_each_display_unit_calls_its_bound_function_and_no_other(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, log_unit: str, unit: str, function: str
) -> None:
    for i, name in enumerate(_BOUND):
        monkeypatch.setattr(_core, name, lambda v, i=i: np.full_like(v, 1000.0 + i))
    y = _resolve_one(tmp_path, log_unit, unit, [1.0, 2.0])
    assert_bits(y, np.full(2, 1000.0 + _BOUND.index(function)), function)


@pytest.mark.parametrize(("log_unit", "unit", "function"), _ROWS)
def test_each_conversion_equals_its_documented_definition(
    tmp_path: Path, log_unit: str, unit: str, function: str
) -> None:
    values = np.random.default_rng(5).normal(scale=1e4, size=257)
    values[:4] = [0.0, -0.0, 1e-310, -1e300]
    y = _resolve_one(tmp_path, log_unit, unit, values.tolist())
    assert_bits(y, INDEPENDENT[(log_unit, unit)](values), function)


def test_known_values(tmp_path: Path) -> None:
    assert_bits(_resolve_one(tmp_path / "a", "m", "ft", [0.3048]), np.array([1.0]), "ft")
    assert_bits(_resolve_one(tmp_path / "b", "m/s", "kt", [1852.0 / 3600.0]), np.array([1.0]), "kt")
    assert_bits(_resolve_one(tmp_path / "c", "m/s", "ft/s", [0.3048]), np.array([1.0]), "ft/s")


@pytest.mark.parametrize("unit", ["rad", "1", "N", "N.m", "m"])
def test_the_log_unit_itself_is_the_identity_and_calls_nothing(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, unit: str
) -> None:
    for name in _BOUND:
        monkeypatch.setattr(_core, name, lambda v: pytest.fail("a conversion was called"))
    values = np.array([0.5, -0.0, 3.25])
    assert_bits(_resolve_one(tmp_path, unit, unit, values.tolist()), values, unit)


@pytest.mark.parametrize(
    ("log_unit", "unit"),
    [
        ("m", "deg"),
        ("rad", "ft"),
        ("1", "deg"),
        ("", "ft"),
        ("N.m", "ft"),
        ("m/s", "deg"),
        ("rad", "kt"),
        ("rad/s", "deg"),
        ("m/s^2", "ft/s"),
        ("ft", "m"),
    ],
)
def test_an_undefined_conversion_is_an_error(tmp_path: Path, log_unit: str, unit: str) -> None:
    with pytest.raises(plot.PlotPackageError) as caught:
        _resolve_one(tmp_path, log_unit, unit, [1.0])
    assert ": figures[0].panels[0].series[0].unit: no conversion from " in str(caught.value)
    assert f"log unit {log_unit!r} to {unit!r}" in str(caught.value)


# --- R3: byte-identical SVG across processes (C-12) -----------------------------------------------

_USER_RC = "font.family: serif\nfont.size: 17\nlines.linewidth: 4\naxes.grid: False\n"


def _astra_plot(log_path: Path, package: str, out: Path, env: dict[str, str]) -> None:
    result = subprocess.run(
        [str(ASTRA), "plot", str(log_path), package, "--out", str(out)],
        env=env,
        capture_output=True,
        text=True,
        timeout=300,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    assert result.stderr == ""
    listed = [Path(line) for line in result.stdout.splitlines()]
    assert listed
    assert all(p.parent == out and p.is_file() for p in listed), listed


def test_r3_svg_is_byte_identical_across_processes_seeds_caches_and_user_settings(
    scenario_log: log.RunLog, tmp_path: Path
) -> None:
    config_dir = tmp_path / "mplconfig"
    config_dir.mkdir()
    base = {k: v for k, v in os.environ.items() if k not in ("SOURCE_DATE_EPOCH", "MATPLOTLIBRC")}
    base["MPLCONFIGDIR"] = str(config_dir)
    packages = ["flight", str(NOTIONAL)]

    for package in packages:
        _astra_plot(scenario_log.path, package, tmp_path / "a", base | {"PYTHONHASHSEED": "1"})
    assert list(config_dir.glob("fontlist-*.json")), "the first process built no font cache"

    (config_dir / "matplotlibrc").write_text(_USER_RC, encoding="utf-8")
    warm = base | {"PYTHONHASHSEED": "2", "SOURCE_DATE_EPOCH": "1700000000"}
    for package in packages:
        _astra_plot(scenario_log.path, package, tmp_path / "b", warm)

    names = sorted(p.name for p in (tmp_path / "a").iterdir())
    assert names == sorted(p.name for p in (tmp_path / "b").iterdir())
    assert names == [
        "cruise-lateral.svg",
        "cruise-longitudinal.svg",
        "flight-aero_angles.svg",
        "flight-air_data.svg",
    ]
    hashed: dict[str, set[str]] = {}
    for name in names:
        first = (tmp_path / "a" / name).read_bytes()
        assert first == (tmp_path / "b" / name).read_bytes(), name
        text = first.decode("utf-8")
        assert "<dc:date>" not in text
        assert "DejaVuSans-" in text
        assert "DejaVuSerif" not in text
        hashed[name] = set(re.findall(r'id="(p[0-9a-f]{10})"', text))
        assert hashed[name], name
    for i, a in enumerate(names):
        for b in names[i + 1 :]:
            assert not hashed[a] & hashed[b], f"{a} and {b} share hashed ids"


# --- R3b: plotly data and offline HTML (C-11) -----------------------------------------------------


def _trace_array(value: Any) -> FloatArray:
    """A trace array from plotly's dict: a base64 typed array (``{dtype, bdata}``) or a list."""
    if isinstance(value, dict):
        typed = cast(dict[str, str], value)
        assert typed["dtype"] == "f8", typed["dtype"]
        return np.frombuffer(base64.b64decode(typed["bdata"]), dtype="<f8").astype(np.float64)
    return np.asarray(value, dtype=np.float64)


def _trace_x(trace: dict[str, Any]) -> FloatArray:
    """A trace's x as plotly.js computes it (M6 PR 14, D1): its ``x`` array, or ``x0 + i * dx`` in
    float64 for ``i`` over its y (``makeCalcdata``, plotly.js 7.1.0)."""
    if "x0" in trace or "dx" in trace:
        assert "x" not in trace, sorted(trace)
        x0, dx = trace["x0"], trace["dx"]
        assert isinstance(x0, float), type(x0)
        assert isinstance(dx, float), type(dx)
        assert dx > 0.0, dx
        return x0 + np.arange(_trace_array(trace["y"]).size, dtype=np.float64) * dx
    return _trace_array(trace["x"])


@pytest.mark.parametrize(
    "path", [plot.framework_package("flight"), NOTIONAL], ids=["flight", "notional"]
)
def test_r3b_plotly_data_equals_the_log_after_conversion(
    path: Path, scenario_log: log.RunLog
) -> None:
    package = plot.load_package(path)
    assert package.figures is not None
    for figure, spec in zip(plot.resolve(package, scenario_log), package.figures, strict=True):
        traces = cast(dict[str, Any], plot.draw_plotly(figure).to_dict())["data"]
        expected = [
            line
            for panel in spec.panels
            for series in panel.series
            for line in expected_series(scenario_log, series)
        ]
        assert len(traces) == len(expected)
        for trace, (label, x, y, _unit) in zip(traces, expected, strict=True):
            assert trace["name"] == label
            assert_bits(_trace_x(trace), x, f"{figure.name} {label} x")
            assert_bits(_trace_array(trace["y"]), y, f"{figure.name} {label} y")


def test_r3b_the_page_is_offline_and_every_svg_is_self_contained(
    scenario_log: log.RunLog, tmp_path: Path
) -> None:
    package = plot.load_package(NOTIONAL)
    written = plot.render(package, scenario_log, tmp_path, html=True)
    page = tmp_path / "cruise.html"
    assert written[-1] == page
    text = page.read_text(encoding="utf-8")
    assert len(text) > 1_000_000, "plotly.js is not inline"
    assert text.count("<script") >= 2
    assert re.search(r"<script[^>]*\ssrc\s*=", text) is None
    assert resource_refs.disallowed(text, resource_refs.PLOTLY_PAGE) == []
    assert 'id="cruise-longitudinal"' in text
    assert 'id="cruise-lateral"' in text
    for svg in written[:-1]:
        svg_text = svg.read_text(encoding="utf-8")
        assert resource_refs.disallowed(svg_text, resource_refs.SVG) == [], svg
        assert resource_refs.id_problems(svg_text) == [], svg


# --- PNG ------------------------------------------------------------------------------------------


def test_png_on_request(scenario_log: log.RunLog, tmp_path: Path) -> None:
    package = plot.load_package(plot.framework_package("attitude"))
    written = plot.render(package, scenario_log, tmp_path, fmt="png")
    assert [p.name for p in written] == ["attitude-euler.png", "attitude-rates.png"]
    for path in written:
        assert path.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"


# --- the CLI --------------------------------------------------------------------------------------


def run_main(capsys: pytest.CaptureFixture[str], *argv: str | Path) -> tuple[int, str, str]:
    code = cli.main([str(a) for a in argv])
    captured = capsys.readouterr()
    return code, captured.out, captured.err


def test_cli_plot_writes_and_lists_the_files(
    scenario_log: log.RunLog, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    out = tmp_path / "new" / "dir"
    code, stdout, stderr = run_main(
        capsys, "plot", scenario_log.path, "health", "--out", out, "--html"
    )
    assert (code, stderr) == (0, "")
    assert stdout == f"{out / 'health-health.svg'}\n{out / 'health.html'}\n"


@pytest.mark.parametrize(
    "argv",
    [
        ["plot"],
        ["plot", "x.h5"],
        ["plot", "x.h5", "flight", "--format", "pdf"],
        ["plot", "--bogus"],
    ],
    ids=["no-arguments", "no-package", "bad-format", "unknown-option"],
)
def test_cli_plot_bad_argument_exits_64(
    capsys: pytest.CaptureFixture[str], argv: list[str]
) -> None:
    code, stdout, stderr = run_main(capsys, *argv)
    assert code == cli.EX_USAGE
    assert stdout == ""
    assert "usage: astra plot" in stderr


def test_cli_plot_refused_package_exits_65_and_writes_nothing(
    scenario_log: log.RunLog, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    path = write_package(tmp_path, one_series("v0.derived.mach", "deg"))
    out = tmp_path / "out"
    code, stdout, stderr = run_main(capsys, "plot", scenario_log.path, path, "--out", out)
    assert (code, stdout) == (cli.EX_DATAERR, "")
    assert stderr.startswith(
        f"astra plot: package error: {path}: figures[0].panels[0].series[0].unit:"
    )
    assert not out.exists()


def test_cli_plot_unknown_framework_package_exits_65(
    scenario_log: log.RunLog, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    code, _, stderr = run_main(capsys, "plot", scenario_log.path, "nope", "--out", tmp_path / "o")
    assert code == cli.EX_DATAERR
    assert "no framework plot package 'nope' (available: attitude, flight, health)" in stderr


def test_cli_plot_missing_log_exits_65(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    code, stdout, stderr = run_main(capsys, "plot", tmp_path / "absent.h5", "flight")
    assert (code, stdout) == (cli.EX_DATAERR, "")
    assert stderr.startswith("astra plot: log error: ")


def test_cli_plot_existing_output_exits_65_untouched(
    scenario_log: log.RunLog, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    (tmp_path / "health-health.svg").write_bytes(b"keep")
    code, stdout, stderr = run_main(capsys, "plot", scenario_log.path, "health", "--out", tmp_path)
    assert (code, stdout) == (cli.EX_DATAERR, "")
    assert stderr.startswith("astra plot: output error: ")
    assert (tmp_path / "health-health.svg").read_bytes() == b"keep"
    assert [p.name for p in tmp_path.iterdir()] == ["health-health.svg"]


@pytest.mark.skipif(os.geteuid() == 0, reason="root ignores the directory's permissions")
def test_cli_plot_failed_write_exits_74(
    scenario_log: log.RunLog, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    out = tmp_path / "readonly"
    out.mkdir()
    out.chmod(0o500)
    try:
        code, stdout, stderr = run_main(capsys, "plot", scenario_log.path, "health", "--out", out)
    finally:
        out.chmod(0o700)
    assert (code, stdout) == (cli.EX_IOERR, "")
    assert stderr.startswith("astra plot: cannot write the figures: ")
    assert list(out.iterdir()) == []


# --- M6 PR 14: the plotly page's size (docs/plans/m6-pr14-plotly-size.md D1, D2, D4) -------------
#
# Every test here is named test_plotly_size_*, so ``pytest tests/python -k plotly_size`` selects
# them. The reduction's reference, _reference_columns, is a plain loop written from astra.plot's
# docstring (Renderers), and never calls astra.plot.

SLOW_RATE = ROOT / "tests/configs/log_slow_rate.yaml"  # /rate_1 and /rate_<divisor>


def _plotly_calls(text: str) -> list[tuple[list[dict[str, Any]], dict[str, Any], dict[str, Any]]]:
    """Each ``Plotly.newPlot(id, data, layout, config)`` call's arguments, parsed from the page's
    own text with ``json``, which rounds each number as JavaScript's parser does."""
    decoder = json.JSONDecoder()
    calls: list[tuple[list[dict[str, Any]], dict[str, Any], dict[str, Any]]] = []
    for match in re.finditer(r"Plotly\.newPlot\(", text):
        arguments: list[Any] = []
        at = match.end()
        for _ in range(4):
            at = len(text) - len(text[at:].lstrip(" \n\r\t,"))
            value, at = decoder.raw_decode(text, at)
            arguments.append(value)
        _, data, layout, config = arguments
        calls.append((data, layout, config))
    return calls


def _uniform_rebuild(x: FloatArray) -> bool:
    """The test's own reading of D1's condition: plotly.js's ``x0 + i * dx`` gives x bitwise."""
    if x.size < 2 or not (np.isfinite(x[0]) and np.isfinite(x[1] - x[0]) and x[1] - x[0] > 0):
        return False
    rebuilt = x[0] + np.arange(x.size, dtype=np.float64) * (x[1] - x[0])
    return bool(np.array_equal(rebuilt.view(np.uint64), x.view(np.uint64)))


def _reference_columns(x: FloatArray, y: FloatArray, columns: int) -> list[int]:
    """The points a trace keeps, frame by frame from the docstring: within each column
    ``[j (n - 1) // N, (j + 1) (n - 1) // N]``, each run of finite x and y keeps its ends and its
    first minimum and maximum of y and of x; plus the trace's ends, and each first non-finite point
    after a finite one."""
    n = len(y)
    if n - 1 <= columns:
        return list(range(n))
    finite = [math.isfinite(x[i]) and math.isfinite(y[i]) for i in range(n)]
    keep = {0, n - 1}
    for j in range(columns):
        lo, hi = j * (n - 1) // columns, (j + 1) * (n - 1) // columns
        i = lo
        while i <= hi:
            if not finite[i]:
                i += 1
                continue
            start = i
            while i + 1 <= hi and finite[i + 1]:
                i += 1
            piece = range(start, i + 1)
            keep |= {start, i}
            keep.add(min(piece, key=lambda k: (y[k], k)))
            keep.add(max(piece, key=lambda k: (y[k], -k)))
            keep.add(min(piece, key=lambda k: (x[k], k)))
            keep.add(max(piece, key=lambda k: (x[k], -k)))
            i += 1
    keep |= {i for i in range(1, n) if finite[i - 1] and not finite[i]}
    return sorted(keep)


def _one_trace(x: FloatArray, y: FloatArray, columns: int | None = None) -> dict[str, Any]:
    figure = plot.Figure("f", "", [plot.Panel("", "y", "", [plot.Series("s", x, y)])])
    (trace,) = cast(dict[str, Any], plot.draw_plotly(figure, columns=columns).to_dict())["data"]
    return cast(dict[str, Any], trace)


def _drawn(trace: dict[str, Any]) -> tuple[FloatArray, FloatArray]:
    return _trace_x(trace), _trace_array(trace["y"])


def _rough(n: int, seed: int) -> tuple[FloatArray, FloatArray]:
    """A time trace (``t = frame * 0.001``) with a rough y: a random walk."""
    rng = np.random.default_rng(seed)
    return np.arange(n, dtype=np.float64) * 0.001, np.cumsum(rng.standard_normal(n))


def _path(n: int, seed: int) -> tuple[FloatArray, FloatArray]:
    """An x-y path whose x is not monotone, like a ground track: a 2-D random walk."""
    rng = np.random.default_rng(seed)
    return np.cumsum(rng.standard_normal(n)), np.cumsum(rng.standard_normal(n))


def _span_problems(
    x: FloatArray, y: FloatArray, drawn_x: FloatArray, drawn_y: FloatArray, columns: int
) -> list[str]:
    """D4's T4, independent of the reference: the drawn points are samples of a time trace, in
    order, with its first and last; in each column, the drawn y range is the samples' range."""
    problems: list[str] = []
    index = np.searchsorted(x, drawn_x)
    if not (np.all(index < x.size) and np.array_equal(x[np.minimum(index, x.size - 1)], drawn_x)):
        return ["a drawn x is not a sample time"]
    if not np.all(np.diff(index) > 0):
        problems.append("the drawn points are not in time order")
    if not np.array_equal(y[index].view(np.uint64), drawn_y.view(np.uint64)):
        problems.append("a drawn y is not the sample at its time")
    if index[0] != 0 or index[-1] != x.size - 1:
        problems.append("the first or last sample is missing")
    n = x.size
    for j in range(columns):
        lo, hi = j * (n - 1) // columns, (j + 1) * (n - 1) // columns
        inside = (index >= lo) & (index <= hi)
        samples = y[lo : hi + 1]
        if np.all(np.isnan(samples)):
            continue
        drawn = drawn_y[inside]
        if not np.any(np.isfinite(drawn)):
            problems.append(f"column {j}: nothing finite drawn")
            continue
        if (np.nanmin(drawn), np.nanmax(drawn)) != (np.nanmin(samples), np.nanmax(samples)):
            problems.append(f"column {j}: the drawn y range is not the samples' range")
    return problems


def _slow_rate_log(tmp_path: Path, divisor: int) -> log.RunLog:
    """A real log with a decimated rate group: ``tests/configs/log_slow_rate.yaml`` with the
    slow_rate plugin at ``divisor``, run 3,000 frames and closed."""
    data = yaml.safe_load(SLOW_RATE.read_text(encoding="utf-8"))
    (model,) = [m for m in data["vehicle"]["models"] if m["plugin"] == "tests/plugins/slow_rate"]
    model["divisor"] = divisor
    path = tmp_path / f"slow_rate_{divisor}.h5"
    config = _core.Config.from_dict({**data, "log": {"path": str(path)}})
    sim = _core.Simulation.from_config(config, _core.default_plugin_dir())
    assert not sim.run(3000)
    sim.close_log()
    del sim
    run_log = log.load(path)
    assert sorted(run_log.rates) == [1, divisor]
    return run_log


@pytest.mark.parametrize(
    "path", [plot.framework_package("flight"), NOTIONAL], ids=["flight", "notional"]
)
def test_plotly_size_uniform_x_is_x0_dx(
    path: Path, scenario_log: log.RunLog, tmp_path: Path
) -> None:
    """T1: every YAML trace's x (the log's t) is written as x0/dx, which rebuilds t bitwise, in
    the figure and in the written page's own JSON text (amendment 2)."""
    package = plot.load_package(path)
    assert package.figures is not None
    figures = plot.resolve(package, scenario_log)
    plot.render(package, scenario_log, tmp_path, html=True)
    calls = _plotly_calls((tmp_path / f"{package.name}.html").read_text(encoding="utf-8"))
    assert len(calls) == len(figures)
    for figure, spec, (page_traces, _, _) in zip(figures, package.figures, calls, strict=True):
        traces = cast(dict[str, Any], plot.draw_plotly(figure).to_dict())["data"]
        expected = [
            line
            for panel in spec.panels
            for series in panel.series
            for line in expected_series(scenario_log, series)
        ]
        assert len(traces) == len(page_traces) == len(expected)
        for trace, page_trace, (label, t, y, _unit) in zip(
            traces, page_traces, expected, strict=True
        ):
            what = f"{figure.name} {label}"
            assert "x" not in trace, what
            assert "x" not in page_trace, what
            for key in ("x0", "dx"):
                written = np.array([page_trace[key]], dtype=np.float64)
                assert_bits(written, np.array([trace[key]], dtype=np.float64), f"{what} {key}")
            assert_bits(_trace_x(page_trace), t, f"{what} page x")
            assert_bits(_trace_x(trace), t, f"{what} x")
            assert_bits(_trace_array(page_trace["y"]), y, f"{what} page y")


def test_plotly_size_nonuniform_x_stays_explicit(scenario_log: log.RunLog) -> None:
    """T2: an x that x0 + i * dx does not give bitwise keeps its array, and either way the trace's
    x is the given x, bit for bit."""
    near = np.arange(1000, dtype=np.float64) * 0.1
    off = near.copy()
    off[500] = np.nextafter(off[500], np.inf)
    two = np.array([0.7, 2.9])  # 0.7 + (2.9 - 0.7) is 2.9000000000000004
    cases: dict[str, FloatArray] = {
        "gapped": np.array([0.0, 1.0, 3.0]),
        "one ulp off": off,
        "two, rounded": two,
        "two, exact": np.array([0.0, 0.5]),
        "one": np.array([2.0]),
        "none": np.array([], dtype=np.float64),
        "nan": np.array([0.0, 1.0, np.nan, 3.0]),
        "decreasing": np.array([3.0, 2.0, 1.0]),
        "constant": np.array([1.0, 1.0, 1.0]),
    }
    assert _uniform_rebuild(near)  # the precondition: one ulp is the only difference
    assert not _uniform_rebuild(off)
    assert not _uniform_rebuild(two)
    assert _uniform_rebuild(cases["two, exact"])
    for name, x in cases.items():
        trace = _one_trace(x, np.zeros(x.size))
        assert ("x" not in trace) == _uniform_rebuild(x), name
        assert ("x" in trace) == (name != "two, exact"), name
        if x.size:
            assert_bits(_trace_x(trace), x, name)

    package = plot.load_package(TEMPLATE_PLOTS / "ground_track.yaml")
    (figure,) = plot.resolve(package, scenario_log)
    traces = cast(dict[str, Any], plot.draw_plotly(figure).to_dict())["data"]
    for trace, (label, east, north) in zip(traces, ground_track(scenario_log), strict=True):
        assert trace["name"] == label
        assert "x" in trace
        assert "x0" not in trace
        assert_bits(_trace_array(trace["x"]), east, f"ground track {label} x")
        assert_bits(_trace_array(trace["y"]), north, f"ground track {label} y")


@pytest.mark.parametrize(("divisor", "form"), [(10, "x0/dx"), (50, "x")], ids=["div10", "div50"])
def test_plotly_size_a_decimated_rate_group_through_a_yaml_series(
    divisor: int, form: str, tmp_path: Path
) -> None:
    """T2, a real decimated rate group (amendment 3): plotly.js's ``i * (d * dt)`` against the
    log's ``(i * d) * dt``, measured. At 3,000 frames and dt = 1 ms, divisor 10 rebuilds every t
    bitwise and divisor 50 does not, so the page keeps that x as an array. Either way the drawn x
    is the log's t."""
    run_log = _slow_rate_log(tmp_path, divisor)
    path = write_package(
        tmp_path,
        "figures:\n  - name: slow\n    panels:\n      - series:\n"
        "          - signal: v{v}.slow_rate.value\n",
    )
    package = plot.load_package(path)
    (figure,) = plot.resolve(package, run_log)
    (trace,) = cast(dict[str, Any], plot.draw_plotly(figure).to_dict())["data"]
    t, values = log_column(run_log, "v0.slow_rate.value")
    frames = run_log.rates[divisor].index.to_numpy()
    assert np.array_equal(frames, np.arange(t.size) * divisor)
    assert _uniform_rebuild(t) == (form == "x0/dx")
    assert ("x0" in trace) == (form == "x0/dx")
    assert ("x" in trace) == (form == "x")
    assert_bits(_trace_x(trace), t, f"divisor {divisor} x")
    assert_bits(_trace_array(trace["y"]), values, f"divisor {divisor} y")


@pytest.mark.parametrize(
    "case",
    ["time", "time_spikes", "time_gaps", "path", "path_gaps", "short_columns"],
)
def test_plotly_size_reduction_equals_the_reference(case: str) -> None:
    """T3: the drawn trace is ``_reference_columns`` of the given one, bit for bit."""
    columns = 37
    if case.startswith("time"):
        x, y = _rough(10_001, 1)
    elif case.startswith("path"):
        x, y = _path(10_001, 2)
    else:
        x, y = _rough(50, 3)  # 49 > 37 columns, so most columns hold two frames
    if case == "time_spikes":
        y[[17, 4_321, 9_999]] = [1e6, -1e6, 1e6]
    if case.endswith("gaps"):
        y[100:400] = np.nan
        y[5_000] = np.nan
        x[7_000:7_003] = np.nan
    index = _reference_columns(x, y, columns)
    trace = _one_trace(x, y, columns)
    drawn_x, drawn_y = _drawn(trace)
    assert_bits(drawn_x, x[index], f"{case} x")
    assert_bits(drawn_y, y[index], f"{case} y")


def test_plotly_size_reduction_spans_each_column() -> None:
    """T4: the reduced trace's points are samples in time order with the first and last, and each
    column's drawn y range is its samples' range. It has teeth: a stride decimation fails at a
    one-frame spike."""
    columns = 37
    x, y = _rough(30_001, 4)
    y[12_345] = 1e6  # a one-frame spike
    drawn_x, drawn_y = _drawn(_one_trace(x, y, columns))
    assert _span_problems(x, y, drawn_x, drawn_y, columns) == []
    stride = np.arange(0, x.size, 100)
    stride = np.unique(np.concatenate((stride, [x.size - 1])))
    assert 12_345 not in stride
    problems = _span_problems(x, y, x[stride], y[stride], columns)
    assert any("drawn y range" in p for p in problems), problems


def test_plotly_size_reduction_keeps_nan_gaps() -> None:
    """T5: an all-NaN column stays a gap, and a column's single finite frame is drawn with a
    non-finite point on each side."""
    columns = 37
    n = 3_701  # 3700 / 37: each column is exactly 100 frames, [100 j, 100 (j + 1)]
    x, y = _rough(n, 5)
    y[1_000:1_101] = np.nan  # column 10, its edges included
    y[2_000:2_101] = np.nan  # column 20 ...
    y[2_050] = 7.0  # ... except one finite frame
    drawn_x, drawn_y = _drawn(_one_trace(x, y, columns))
    index = np.searchsorted(x, drawn_x)
    assert_bits(drawn_x, x[_reference_columns(x, y, columns)], "x")
    assert_bits(drawn_y, y[_reference_columns(x, y, columns)], "y")
    gap = (index >= 1_000) & (index <= 1_100)
    assert list(index[gap]) == [1_000]  # the first NaN after the stretch, and nothing finite
    assert np.isnan(drawn_y[gap]).all()
    assert 999 in index
    assert 1_101 in index
    k = int(np.flatnonzero(index == 2_050)[0])
    assert drawn_y[k] == 7.0
    assert np.isnan(drawn_y[k - 1])
    assert np.isnan(drawn_y[k + 1])


def test_plotly_size_reduction_is_bounded() -> None:
    """T6: the count is n up to N + 1 points; then at most 3N + 1 for a time trace with no gap and
    5N + 1 for a path, whatever n. A gapped trace may exceed 3N + 1 and stays within the
    docstring's 3N + 5r - 1 for r finite stretches (amendment 1)."""
    columns = 37
    for n in (1, 2, columns + 1, columns + 2, 10 * columns, 300_001):
        x, y = _rough(n, n)
        count = _drawn(_one_trace(x, y, columns))[0].size
        if n - 1 <= columns:
            assert count == n, (n, count)
        else:
            assert count <= 3 * columns + 1, (n, count)
        px, py = _path(n, n)
        count = _drawn(_one_trace(px, py, columns))[0].size
        assert count == n if n - 1 <= columns else count <= 5 * columns + 1, (n, count)
    x, y = _rough(300_001, 6)
    y[np.arange(1_000, 300_000, 2_000)] = np.nan  # 150 one-frame gaps, 151 finite stretches
    stretches = 151
    count = _drawn(_one_trace(x, y, columns))[0].size
    assert count > 3 * columns + 1, count
    assert count <= 3 * columns + 5 * stretches - 1, count


def test_plotly_size_reduced_title_and_default_unreduced(
    scenario_log: log.RunLog, tmp_path: Path
) -> None:
    """T7: a figure with a reduced trace says so in its title; one whose traces all fit does not,
    and neither does a figure drawn without columns, which keeps every point. ``render`` refuses
    html_columns without html, or a value that is not an integer of at least 1, before writing."""
    x, y = _rough(1_000, 7)
    figure = plot.Figure("f", "Title", [plot.Panel("", "y", "", [plot.Series("s", x, y)])])
    layout = cast(dict[str, Any], plot.draw_plotly(figure, columns=5).to_dict())["layout"]
    assert layout["title"]["text"] == "Title (reduced: 5 columns per trace)"
    full = cast(dict[str, Any], plot.draw_plotly(figure).to_dict())
    assert full["layout"]["title"]["text"] == "Title"
    assert_bits(_trace_array(full["data"][0]["y"]), y, "unreduced y")
    fits = cast(dict[str, Any], plot.draw_plotly(figure, columns=999).to_dict())
    assert fits["layout"]["title"]["text"] == "Title"
    assert_bits(_trace_array(fits["data"][0]["y"]), y, "999 columns y")

    package = plot.load_package(plot.framework_package("health"))
    for options in (
        {"html_columns": 5},
        {"html": True, "html_columns": 0},
        {"html": True, "html_columns": True},
        {"html": True, "html_columns": 1.5},
    ):
        with pytest.raises(ValueError, match="html_columns"):
            plot.render(package, scenario_log, tmp_path / "out", **cast(Any, options))
    assert not (tmp_path / "out").exists()


@pytest.mark.parametrize(
    "argv",
    [
        ["--html-columns", "5"],
        ["--html", "--html-columns", "0"],
        ["--html", "--html-columns", "-1"],
        ["--html", "--html-columns", "x"],
    ],
    ids=["no-html", "zero", "negative", "not-a-number"],
)
def test_plotly_size_cli_html_columns_usage_exits_64(
    argv: list[str], scenario_log: log.RunLog, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    """T8: a usage error, 64, and no file."""
    out = tmp_path / "out"
    code, stdout, stderr = run_main(
        capsys, "plot", scenario_log.path, "health", "--out", out, *argv
    )
    assert (code, stdout) == (cli.EX_USAGE, "")
    assert "usage: astra plot" in stderr
    assert not out.exists()


def test_plotly_size_cli_html_columns_bounds_the_page(
    scenario_log: log.RunLog, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    """T8: ``--html --html-columns N`` writes a page whose every trace is within 3N + 1 points
    (the log's traces have no gap), with the title marked, and the same SVG as without it."""
    columns = 5
    out = tmp_path / "out"
    code, stdout, stderr = run_main(
        capsys,
        "plot",
        scenario_log.path,
        "flight",
        "--out",
        out,
        "--html",
        "--html-columns",
        str(columns),
    )
    assert (code, stderr) == (0, "")
    assert stdout.splitlines()[-1] == str(out / "flight.html")
    calls = _plotly_calls((out / "flight.html").read_text(encoding="utf-8"))
    assert len(calls) == 2
    rows = next(iter(scenario_log.rates.values())).shape[0]
    assert rows - 1 > columns
    for data, layout, _ in calls:
        assert layout["title"]["text"].endswith(f"(reduced: {columns} columns per trace)")
        for trace in data:
            assert 0 < _trace_x(trace).size <= 3 * columns + 1, trace["name"]
    plain = tmp_path / "plain"
    plot.render(plot.load_package(plot.framework_package("flight")), scenario_log, plain)
    for svg in plain.iterdir():
        assert (out / svg.name).read_bytes() == svg.read_bytes(), svg.name
