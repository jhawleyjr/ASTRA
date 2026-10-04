"""astra.plot: plot packages over run logs (M6 PR 5, docs/plans/m6-pr5-plots.md;
docs/architecture.md §12; ADR-0002, ADR-0010, ADR-0011).

A plot package is one YAML file, written once and applied to any run log (``astra.log.load``).
Framework packages live in ``python/astra/plots/`` (``framework_package``); an aircraft's own
live in ``aircraft/<name>/plots/``, and ``aircraft/_template/plots/`` carries a starter set that
``just new-aircraft`` copies. The package's name is its file stem.

Format. Every map is closed: an unknown key at any level is an error, and so is a key written twice
(PyYAML would keep the last one silently).

- ``title``: optional string.
- ``figures``: a non-empty list. Each figure is ``{name, title, panels}``: ``name`` is required,
  matches ``[a-z0-9_]+``, is unique in the package and names the output file; ``title`` is optional.
- ``panels``: a non-empty list of ``{title, ylabel, series}``, the first two optional. A panel's
  y axis reads ``<ylabel> [<unit>]``; all its series must share one display unit.
- ``series``: a non-empty list of ``{signal, unit, label}``, the last two optional:
  - ``signal`` names a logged signal. ``{v}`` in it is replaced by each vehicle index the log holds
    (its ``v<index>.`` prefixes, ascending), so ``v{v}.derived.alpha`` is one series per vehicle
    and a package never hardcodes ``v0``. No other ``{...}`` is allowed.
  - ``unit`` is the display unit. It defaults to the log's unit, with the data unchanged. Otherwise
    the conversion is a function of ``core/util/units`` bound in ``astra._core`` (``CONVERSIONS``);
    a conversion the table does not define for the log's unit string is an error.
  - ``label`` is the legend text; it defaults to the expanded signal name, and a ``{v}`` series's
    label is prefixed with its vehicle (``v0 alpha``).
- Or, instead of ``figures``, ``function: {module | file, name}``: ``module`` is a dotted importable
  module name, ``file`` a ``.py`` file beside the package (a bare file name, no ``/`` or ``..``),
  loaded by path so an aircraft's own Python plots travel with its directory. ``name`` is a
  callable that takes a ``RunLog`` and returns a list of ``Figure``, for plots YAML cannot
  express; its result is checked like YAML-built figures. ``vehicles`` and ``column`` help write
  one. Such a ``Panel`` may set ``aspect="equal"`` (default ``"auto"``), for an x and a y in the
  same unit, such as a ground track: both renderers then draw one unit the same length on both
  axes, matplotlib by widening the limits (``adjustable="datalim"``) rather than shrinking the
  axes. An equal panel must be its figure's only panel, since the figure's panels share x when
  their x labels match and a shared x would make matplotlib move the y limits instead (M6 PR 12).
  YAML has no ``aspect`` key: its x is always ``t``.

Strings must be YAML strings: YAML 1.1 reads ``on``, ``off``, ``yes`` and ``no`` as booleans, and a
value starting with ``{`` as a flow map, so quote such a value. The format has no numeric key.

Errors. A ``PlotPackageError`` reads ``<package file>: <key>: <problem>``, the key a path such as
``figures[0].panels[1].series[2].unit``, or names the file, line and column of a YAML syntax error.
``render`` resolves the whole package against the log (signals, vehicles, conversions, or the
function and its result) and checks that no output file exists before it creates the output
directory or writes anything, so a package error never leaves a file behind. It never overwrites a
file: an existing output is a ``PlotOutputExistsError``, and each file is created exclusively. If a
write fails, it deletes the files it created, and only those, and re-raises.

Renderers. matplotlib writes one file per figure, ``<package>-<figure>.svg`` (or ``.png``). It draws
on ``matplotlib.figure.Figure`` directly, never ``pyplot``, so it is headless and keeps no global
figure state, and it renders under a pinned style: matplotlib's own defaults (the user's
matplotlibrc is ignored), DejaVu Sans (the font matplotlib bundles) drawn as paths, a hash salt
fixed per figure (``astra/<package>/<figure>``) and no date metadata. Same log, package and pinned
versions on one machine give a byte-identical SVG in any process. PNG is not byte-stable. Every
legend sits outside its axes, to the right (``legend_outside``; M6 PR 6, D1), so none covers
data. plotly writes one interactive HTML file per package, ``<package>.html``, with plotly.js
inline so it opens offline; it is several megabytes, so it is written only on request.
"""

from __future__ import annotations

import collections.abc
import contextlib
import dataclasses
import hashlib
import html
import importlib
import io
import re
import sys
import types
from collections.abc import Callable, Generator, Sequence
from pathlib import Path
from typing import TYPE_CHECKING, Any, Literal, cast

import numpy as np
import numpy.typing as npt
import yaml

from astra import _core, _runkit, log

if TYPE_CHECKING:
    from matplotlib.figure import Figure as MatplotlibFigure
    from matplotlib.typing import RcKeyType

FloatArray = npt.NDArray[np.float64]

FRAMEWORK_DIR = Path(__file__).resolve().parent / "plots"

_PACKAGE_NAME = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_-]*")
_FIGURE_NAME = re.compile(r"[a-z0-9_]+")
_MODULE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*")
_IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_VEHICLE = re.compile(r"v(0|[1-9][0-9]*)\.")
_PLACEHOLDER = "{v}"


class PlotPackageError(Exception):
    """The package is refused (module docstring, Errors); the message names the file and the key."""


class PlotOutputExistsError(FileExistsError):
    """An output file already exists; ``render`` never overwrites one."""


# --- figures: what a package resolves to, and what an escape-hatch function returns ---------------


@dataclasses.dataclass(frozen=True, eq=False)
class Series:
    """One line: ``x`` and ``y`` are 1-D float64 arrays of one length, in the panel's units."""

    label: str
    x: FloatArray
    y: FloatArray


@dataclasses.dataclass(frozen=True, eq=False)
class Panel:
    """One axes. The y axis reads ``<ylabel> [<unit>]`` and the x axis ``<xlabel> [<xunit>]``.
    ``aspect="equal"`` draws one x unit and one y unit the same length (module docstring,
    Format)."""

    title: str
    ylabel: str
    unit: str
    series: Sequence[Series]
    xlabel: str = "t"
    xunit: str = "s"
    aspect: Literal["auto", "equal"] = "auto"


@dataclasses.dataclass(frozen=True, eq=False)
class Figure:
    """One output figure; ``name`` matches ``[a-z0-9_]+`` and names its file."""

    name: str
    title: str
    panels: Sequence[Panel]


# --- the parsed package ---------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class SeriesSpec:
    signal: str
    unit: str | None
    label: str | None


@dataclasses.dataclass(frozen=True)
class PanelSpec:
    title: str
    ylabel: str
    series: tuple[SeriesSpec, ...]


@dataclasses.dataclass(frozen=True)
class FigureSpec:
    name: str
    title: str
    panels: tuple[PanelSpec, ...]


@dataclasses.dataclass(frozen=True)
class FunctionSpec:
    """Exactly one of ``module`` and ``file`` is set; ``file`` is resolved beside the package."""

    module: str | None
    file: Path | None
    name: str


@dataclasses.dataclass(frozen=True)
class Package:
    """A loaded package: exactly one of ``figures`` and ``function`` is set."""

    path: Path
    name: str
    title: str
    figures: tuple[FigureSpec, ...] | None
    function: FunctionSpec | None


# --- display units (ADR-0002: every conversion is core/util/units, bound in astra._core) ----------

Conversion = Callable[[FloatArray], FloatArray]

# Keyed by (the log's unit string, the display unit). Each entry calls the bound function when it
# runs, never a Python-side constant. A display unit equal to the log's unit is the identity.
CONVERSIONS: dict[tuple[str, str], Conversion] = {
    ("m", "ft"): lambda values: _core.meters_to_feet(values),
    ("m/s", "kt"): lambda values: _core.meters_per_second_to_knots(values),
    ("m/s", "ft/s"): lambda values: _core.meters_to_feet(values),
    ("rad", "deg"): lambda values: _core.radians_to_degrees(values),
    ("rad/s", "deg/s"): lambda values: _core.radians_to_degrees(values),
}


def _identity(values: FloatArray) -> FloatArray:
    return values


def _conversion(path: Path, where: str, signal: str, log_unit: str, unit: str) -> Conversion:
    if unit == log_unit:
        return _identity
    found = CONVERSIONS.get((log_unit, unit))
    if found is None:
        defined = sorted(display for source, display in CONVERSIONS if source == log_unit)
        raise PlotPackageError(
            f"{path}: {where}: no conversion from {signal!r}'s log unit {log_unit!r} to {unit!r} "
            f"(defined for {log_unit!r}: {', '.join(defined) if defined else 'none'})"
        )
    return found


# --- loading --------------------------------------------------------------------------------------


class _Loader(yaml.SafeLoader):
    """``yaml.SafeLoader`` that refuses a key written twice in one map."""


def _mapping(loader: _Loader, node: yaml.MappingNode) -> dict[Any, Any]:
    loader.flatten_mapping(node)
    keys: list[Any] = []
    for key_node, _value in node.value:
        # typeshed's PyYAML stubs leave construct_object's return untyped.
        key = cast(Any, loader.construct_object(key_node))  # pyright: ignore[reportUnknownMemberType]
        if key in keys:
            raise yaml.constructor.ConstructorError(
                "while constructing a mapping",
                node.start_mark,
                f"found duplicate key {key!r}",
                key_node.start_mark,
            )
        keys.append(key)
    return loader.construct_mapping(node)


_Loader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _mapping)


def _string(path: Path, value: Any, where: str) -> str:
    if isinstance(value, str) and value:
        return value
    hint = ""
    if isinstance(value, bool):
        hint = " (YAML 1.1 reads on, off, yes, no, true and false as booleans: quote it)"
    elif isinstance(value, int | float):
        hint = " (quote it to make it a string)"
    elif isinstance(value, dict):
        hint = " (a value starting with '{' is a YAML flow map: quote it)"
    raise PlotPackageError(f"{path}: {where}: must be a non-empty string, got {value!r}{hint}")


def _optional_string(path: Path, data: dict[str, Any], key: str, where: str) -> str:
    return _string(path, data[key], f"{where}.{key}") if key in data else ""


def _list(path: Path, value: Any, where: str) -> list[Any]:
    if not isinstance(value, list):
        raise PlotPackageError(f"{path}: {where}: must be a list")
    items = cast(list[Any], value)
    if not items:
        raise PlotPackageError(f"{path}: {where}: must not be empty")
    return items


def _closed(
    path: Path, value: Any, where: str, *, required: set[str], optional: set[str]
) -> dict[str, Any]:
    return _runkit.closed_map(
        path, value, where, required=required, optional=optional, error=PlotPackageError
    )


def framework_package(name: str) -> Path:
    """The framework package called ``name`` (``python/astra/plots/<name>.yaml``)."""
    path = FRAMEWORK_DIR / f"{name}.yaml"
    if not _PACKAGE_NAME.fullmatch(name) or not path.is_file():
        available = sorted(p.stem for p in FRAMEWORK_DIR.glob("*.yaml"))
        raise PlotPackageError(
            f"{path}: no framework plot package {name!r} (available: {', '.join(available)})"
        )
    return path


def package_path(argument: str) -> Path:
    """``argument`` as a package file: a path if it contains ``/`` or ends in ``.yaml`` or ``.yml``,
    otherwise a framework package name."""
    if "/" in argument or argument.endswith((".yaml", ".yml")):
        return Path(argument)
    return framework_package(argument)


def load_package(path: str | Path) -> Package:
    """Reads and checks the package at ``path`` (module docstring, Format). Everything that does
    not need a log is checked here; ``resolve`` checks the rest."""
    path = Path(path)
    if not _PACKAGE_NAME.fullmatch(path.stem):
        raise PlotPackageError(
            f"{path}: the package name {path.stem!r} (the file stem) must match "
            f"{_PACKAGE_NAME.pattern}"
        )
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise PlotPackageError(f"{path}: cannot read the package: {error}") from error
    try:
        data: Any = yaml.load(text, Loader=_Loader)  # a SafeLoader: no Python-object tags
    except yaml.MarkedYAMLError as error:
        mark = error.problem_mark or error.context_mark
        where = f"line {mark.line + 1}, column {mark.column + 1}" if mark else "YAML"
        problem = error.problem or error.context or "invalid YAML"
        raise PlotPackageError(f"{path}: {where}: YAML error: {problem}") from error
    except yaml.YAMLError as error:
        raise PlotPackageError(f"{path}: YAML error: {error}") from error
    top = _closed(
        path, data, "top level", required=set(), optional={"title", "figures", "function"}
    )
    title = _optional_string(path, top, "title", "top level")
    if ("figures" in top) == ("function" in top):
        raise PlotPackageError(
            f"{path}: top level: exactly one of figures and function is required"
        )
    if "function" in top:
        return Package(path, path.stem, title, None, _function_spec(path, top["function"]))
    return Package(path, path.stem, title, _figure_specs(path, top["figures"]), None)


def _figure_specs(path: Path, value: Any) -> tuple[FigureSpec, ...]:
    figures: list[FigureSpec] = []
    seen: set[str] = set()
    for i, item in enumerate(_list(path, value, "figures")):
        where = f"figures[{i}]"
        data = _closed(
            path, item, where, required={"name", "panels"}, optional={"name", "title", "panels"}
        )
        name = _string(path, data["name"], f"{where}.name")
        if not _FIGURE_NAME.fullmatch(name):
            raise PlotPackageError(f"{path}: {where}.name: {name!r} must match [a-z0-9_]+")
        if name in seen:
            raise PlotPackageError(f"{path}: {where}.name: {name!r} is already a figure's name")
        seen.add(name)
        panels = tuple(
            _panel_spec(path, panel, f"{where}.panels[{j}]")
            for j, panel in enumerate(_list(path, data["panels"], f"{where}.panels"))
        )
        figures.append(FigureSpec(name, _optional_string(path, data, "title", where), panels))
    return tuple(figures)


def _panel_spec(path: Path, value: Any, where: str) -> PanelSpec:
    data = _closed(path, value, where, required={"series"}, optional={"title", "ylabel", "series"})
    series = tuple(
        _series_spec(path, item, f"{where}.series[{k}]")
        for k, item in enumerate(_list(path, data["series"], f"{where}.series"))
    )
    return PanelSpec(
        _optional_string(path, data, "title", where),
        _optional_string(path, data, "ylabel", where),
        series,
    )


def _series_spec(path: Path, value: Any, where: str) -> SeriesSpec:
    data = _closed(path, value, where, required={"signal"}, optional={"signal", "unit", "label"})
    signal = _string(path, data["signal"], f"{where}.signal")
    rest = signal.replace(_PLACEHOLDER, "")
    if "{" in rest or "}" in rest:
        raise PlotPackageError(
            f"{path}: {where}.signal: {signal!r}: {_PLACEHOLDER} is the only placeholder"
        )
    unit = _string(path, data["unit"], f"{where}.unit") if "unit" in data else None
    label = _string(path, data["label"], f"{where}.label") if "label" in data else None
    return SeriesSpec(signal, unit, label)


def _function_spec(path: Path, value: Any) -> FunctionSpec:
    data = _closed(path, value, "function", required={"name"}, optional={"module", "file", "name"})
    if ("module" in data) == ("file" in data):
        raise PlotPackageError(f"{path}: function: exactly one of module and file is required")
    name = _string(path, data["name"], "function.name")
    if not _IDENTIFIER.fullmatch(name):
        raise PlotPackageError(f"{path}: function.name: {name!r} is not a Python identifier")
    if "module" in data:
        module = _string(path, data["module"], "function.module")
        if not _MODULE.fullmatch(module):
            raise PlotPackageError(f"{path}: function.module: {module!r} is not a module name")
        return FunctionSpec(module, None, name)
    file = _string(path, data["file"], "function.file")
    if "/" in file or "\\" in file or not file.endswith(".py") or file.startswith("."):
        raise PlotPackageError(
            f"{path}: function.file: {file!r} must be a bare .py file name beside the package "
            "(no '/', no '..')"
        )
    located = path.parent / file
    if not located.is_file():
        raise PlotPackageError(f"{path}: function.file: {located} is not a file")
    return FunctionSpec(None, located, name)


# --- resolving against a log ----------------------------------------------------------------------


def vehicles(run_log: log.RunLog) -> list[int]:
    """The vehicle indices of the log's ``v<index>.`` signals, ascending (ADR-0011)."""
    found: set[int] = set()
    for name in run_log.units:
        match = _VEHICLE.match(name)
        if match:
            found.add(int(match.group(1)))
    return sorted(found)


def column(run_log: log.RunLog, signal: str) -> tuple[FloatArray, FloatArray]:
    """``(t, values)`` of ``signal`` from its rate group, as new float64 arrays (``t = frame * dt``,
    ADR-0010). Raises ``KeyError`` for a name that is not a signal."""
    for frame in run_log.rates.values():
        if signal in frame.columns:
            t = cast(Any, frame["t"]).to_numpy(dtype=np.float64, copy=True)
            values = cast(Any, frame[signal]).to_numpy(dtype=np.float64, copy=True)
            return cast(FloatArray, t), cast(FloatArray, values)
    raise KeyError(signal)


def resolve(package: Package, run_log: log.RunLog) -> list[Figure]:
    """The package's figures with every series's data, for ``run_log``. Raises
    ``PlotPackageError`` naming the file and key (module docstring, Errors)."""
    if package.function is not None:
        result = _call(package, package.function, run_log)
        return _checked(package.path, "function", result)
    assert package.figures is not None
    figures: list[Figure] = []
    for i, spec in enumerate(package.figures):
        panels = [
            _panel(package.path, f"figures[{i}].panels[{j}]", panel, run_log)
            for j, panel in enumerate(spec.panels)
        ]
        figures.append(Figure(spec.name, spec.title, panels))
    return _checked(package.path, "figures", figures)


def _panel(path: Path, where: str, spec: PanelSpec, run_log: log.RunLog) -> Panel:
    series: list[Series] = []
    units: list[tuple[str, str]] = []
    for k, item in enumerate(spec.series):
        key = f"{where}.series[{k}]"
        for signal, vehicle in _expand(path, key, item.signal, run_log):
            if signal not in run_log.units:
                owner = f" (vehicle {vehicle})" if vehicle is not None else ""
                raise PlotPackageError(
                    f"{path}: {key}.signal: {signal!r}{owner} is not a signal in the log "
                    f"{run_log.path}"
                )
            log_unit = run_log.units[signal]
            unit = item.unit if item.unit is not None else log_unit
            convert = _conversion(path, f"{key}.unit", signal, log_unit, unit)
            t, values = column(run_log, signal)
            label = item.label if item.label is not None else signal
            if vehicle is not None and item.label is not None:
                label = f"v{vehicle} {label}"
            series.append(Series(label, t, convert(values)))
            units.append((key, unit))
    for key, unit in units[1:]:
        if unit != units[0][1]:
            raise PlotPackageError(
                f"{path}: {key}.unit: display unit {unit!r} differs from {units[0][1]!r} "
                f"({units[0][0]}); a panel's series share one unit"
            )
    return Panel(spec.title, spec.ylabel, units[0][1], series)


def _expand(
    path: Path, where: str, signal: str, run_log: log.RunLog
) -> list[tuple[str, int | None]]:
    if _PLACEHOLDER not in signal:
        return [(signal, None)]
    indices = vehicles(run_log)
    if not indices:
        raise PlotPackageError(
            f"{path}: {where}.signal: {signal!r} needs a vehicle, and the log {run_log.path} has "
            "none (no v<index>. signal)"
        )
    return [(signal.replace(_PLACEHOLDER, str(i)), i) for i in indices]


def _call(package: Package, spec: FunctionSpec, run_log: log.RunLog) -> object:
    path = package.path
    if spec.module is not None:
        try:
            module = importlib.import_module(spec.module)
        except Exception as error:
            raise PlotPackageError(
                f"{path}: function.module: cannot import {spec.module!r}: "
                f"{type(error).__name__}: {error}"
            ) from error
        where = f"{spec.module}.{spec.name}"
    else:
        assert spec.file is not None
        module = _load_file(path, spec.file)
        where = f"{spec.file.name}:{spec.name}"
    function = getattr(module, spec.name, None)
    if not callable(function):
        raise PlotPackageError(f"{path}: function.name: {where} is not a callable")
    try:
        return cast(Callable[[log.RunLog], object], function)(run_log)
    except Exception as error:
        raise PlotPackageError(
            f"{path}: function: {where} raised {type(error).__name__}: {error}"
        ) from error


def _load_file(path: Path, file: Path) -> object:
    """Executes ``file`` as a fresh module under a private name derived from its resolved path. The
    source is compiled here, not imported, so nothing (no ``__pycache__``) is written beside it: a
    template's directory stays exactly what ``just new-aircraft`` should copy."""
    digest = hashlib.sha256(str(file.resolve()).encode()).hexdigest()[:16]
    name = f"_astra_plot_user_{digest}"
    module = types.ModuleType(name)
    module.__file__ = str(file)
    sys.modules[name] = module  # a dataclass in the module needs its module registered
    try:
        code = compile(file.read_bytes(), str(file), "exec")
        exec(code, module.__dict__)
    except Exception as error:
        del sys.modules[name]
        raise PlotPackageError(
            f"{path}: function.file: {file} raised {type(error).__name__}: {error}"
        ) from error
    return module


def _checked(path: Path, where: str, value: object) -> list[Figure]:
    """``value`` as a non-empty list of well-formed figures, or a ``PlotPackageError``."""
    if not isinstance(value, list | tuple):
        raise PlotPackageError(
            f"{path}: {where}: must give a list of astra.plot.Figure, got {type(value).__name__}"
        )
    figures = cast(Sequence[object], value)
    if not figures:
        raise PlotPackageError(f"{path}: {where}: gave no figure")
    names: set[str] = set()
    for i, figure in enumerate(figures):
        key = f"{where}[{i}]"
        if not isinstance(figure, Figure):
            raise PlotPackageError(f"{path}: {key}: {type(figure).__name__} is not a Figure")
        if not isinstance(cast(object, figure.name), str) or not _FIGURE_NAME.fullmatch(
            figure.name
        ):
            raise PlotPackageError(f"{path}: {key}.name: {figure.name!r} must match [a-z0-9_]+")
        if figure.name in names:
            raise PlotPackageError(
                f"{path}: {key}.name: {figure.name!r} is already a figure's name"
            )
        names.add(figure.name)
        _check_text(path, f"{key}.title", figure.title)
        _check_panels(path, key, figure.panels)
    return [cast(Figure, figure) for figure in figures]


def _check_text(path: Path, where: str, value: object) -> None:
    if not isinstance(value, str):
        raise PlotPackageError(f"{path}: {where}: must be a string, got {value!r}")


def _check_panels(path: Path, where: str, panels: object) -> None:
    if not isinstance(panels, collections.abc.Sequence) or isinstance(panels, str) or not panels:
        raise PlotPackageError(f"{path}: {where}.panels: must be a non-empty sequence of Panel")
    count = len(cast(Sequence[object], panels))
    for j, panel in enumerate(cast(Sequence[object], panels)):
        key = f"{where}.panels[{j}]"
        if not isinstance(panel, Panel):
            raise PlotPackageError(f"{path}: {key}: {type(panel).__name__} is not a Panel")
        for field in ("title", "ylabel", "unit", "xlabel", "xunit"):
            _check_text(path, f"{key}.{field}", getattr(panel, field))
        aspect = cast(object, panel.aspect)
        if not isinstance(aspect, str) or aspect not in ("auto", "equal"):
            raise PlotPackageError(
                f"{path}: {key}.aspect: must be 'auto' or 'equal', got {aspect!r}"
            )
        if aspect == "equal" and count > 1:
            raise PlotPackageError(
                f"{path}: {key}.aspect: an 'equal' panel must be its figure's only panel "
                f"(this figure has {count})"
            )
        series = cast(object, panel.series)
        if (
            not isinstance(series, collections.abc.Sequence)
            or isinstance(series, str)
            or not series
        ):
            raise PlotPackageError(f"{path}: {key}.series: must be a non-empty sequence of Series")
        for k, line in enumerate(cast(Sequence[object], series)):
            _check_series(path, f"{key}.series[{k}]", line)


def _check_series(path: Path, where: str, line: object) -> None:
    if not isinstance(line, Series):
        raise PlotPackageError(f"{path}: {where}: {type(line).__name__} is not a Series")
    _check_text(path, f"{where}.label", line.label)
    for field in ("x", "y"):
        array = cast(object, getattr(line, field))
        if not isinstance(array, np.ndarray):
            raise PlotPackageError(f"{path}: {where}.{field}: must be a numpy array")
        checked = cast(npt.NDArray[Any], array)
        if checked.ndim != 1 or checked.dtype != np.float64:
            raise PlotPackageError(
                f"{path}: {where}.{field}: must be 1-D float64, "
                f"got {checked.ndim}-D {checked.dtype}"
            )
    if line.x.shape != line.y.shape:
        raise PlotPackageError(
            f"{path}: {where}: x has {line.x.shape[0]} points and y has {line.y.shape[0]}"
        )


# --- rendering ------------------------------------------------------------------------------------

# matplotlib's own defaults (rcdefaults) plus these, so a user's matplotlibrc changes nothing.
_STYLE: dict[RcKeyType, Any] = {
    "font.family": ["DejaVu Sans"],  # bundled with matplotlib
    "svg.fonttype": "path",  # glyphs drawn from the bundled font, not left to the viewer
    "figure.dpi": 100,
    "savefig.dpi": 100,
    "axes.grid": True,
    "axes.formatter.useoffset": False,
}


@contextlib.contextmanager
def pinned_style(package: str, figure: str) -> Generator[None]:
    """The pinned style for one figure (module docstring, Renderers)."""
    import matplotlib

    # matplotlib's stubs leave rc_context's fname as PathLike[Unknown].
    with matplotlib.rc_context():  # pyright: ignore[reportUnknownMemberType]
        matplotlib.rcdefaults()
        matplotlib.rcParams.update(_STYLE)
        matplotlib.rcParams["svg.hashsalt"] = f"astra/{package}/{figure}"
        yield


def _axis_label(name: str, unit: str) -> str:
    if not unit:
        return name
    return f"{name} [{unit}]" if name else f"[{unit}]"


def legend_outside(ax: Any, *, handles: Sequence[Any] | None = None, ncols: int = 1) -> None:
    """The axes' legend, outside it to the right (M6 PR 6, D1): anchored at the axes' top right
    corner, so it never covers data; the constrained layout makes room for it. ``handles`` limits
    the entries (default: every labeled artist)."""
    options: dict[str, Any] = {
        "loc": "upper left",
        "bbox_to_anchor": (1.02, 1.0),
        "borderaxespad": 0.0,
        "ncols": ncols,
    }
    if handles is not None:
        options["handles"] = list(handles)
    ax.legend(**options)


def _shared_x(figure: Figure) -> bool:
    return len({(panel.xlabel, panel.xunit) for panel in figure.panels}) == 1


def draw_matplotlib(figure: Figure) -> MatplotlibFigure:
    """``figure`` drawn on a new matplotlib figure. ``render`` calls it under ``pinned_style``."""
    from matplotlib.figure import Figure as MatplotlibFigure

    count = len(figure.panels)
    shared = _shared_x(figure)
    drawn = MatplotlibFigure(figsize=(8.0, 1.0 + 2.2 * count), layout="constrained")
    axes = drawn.subplots(count, 1, sharex=shared, squeeze=False)
    for i, panel in enumerate(figure.panels):
        ax = axes[i][0]
        for line in panel.series:
            ax.plot(line.x, line.y, label=line.label, linewidth=1.0)
        if panel.title:
            ax.set_title(panel.title)
        ax.set_ylabel(_axis_label(panel.ylabel, panel.unit))
        if panel.aspect == "equal":
            ax.set_aspect("equal", adjustable="datalim")
        legend_outside(ax)
        if not shared or i == count - 1:
            ax.set_xlabel(_axis_label(panel.xlabel, panel.xunit))
    # matplotlib's stubs leave suptitle's and savefig's **kwargs untyped.
    drawn.suptitle(figure.title or figure.name)  # pyright: ignore[reportUnknownMemberType]
    return drawn


def draw_plotly(figure: Figure) -> Any:
    """``figure`` as a ``plotly.graph_objects.Figure``, one subplot row per panel. plotly ships no
    type information (no py.typed), so it is handled as ``Any`` at this boundary."""
    go: Any = importlib.import_module("plotly.graph_objects")
    subplots: Any = importlib.import_module("plotly.subplots")
    count = len(figure.panels)
    shared = _shared_x(figure)
    drawn = subplots.make_subplots(
        rows=count,
        cols=1,
        shared_xaxes=shared,
        subplot_titles=[panel.title for panel in figure.panels],
    )
    for i, panel in enumerate(figure.panels):
        for line in panel.series:
            drawn.add_trace(
                go.Scatter(x=line.x, y=line.y, name=line.label, mode="lines"), row=i + 1, col=1
            )
        drawn.update_yaxes(title_text=_axis_label(panel.ylabel, panel.unit), row=i + 1, col=1)
        if panel.aspect == "equal":
            anchor = drawn.get_subplot(i + 1, 1).yaxis.anchor  # this row's x axis
            drawn.update_yaxes(scaleanchor=anchor, scaleratio=1, row=i + 1, col=1)
        if not shared or i == count - 1:
            drawn.update_xaxes(title_text=_axis_label(panel.xlabel, panel.xunit), row=i + 1, col=1)
    drawn.update_layout(title_text=figure.title or figure.name, height=120 + 260 * count)
    return drawn


def _html(package: Package, figures: Sequence[Figure]) -> str:
    """One offline page: plotly.js inline once, then one div per figure."""
    pio: Any = importlib.import_module("plotly.io")
    title = html.escape(package.title or package.name)
    parts = [
        "<!DOCTYPE html>",
        "<html>",
        "<head>",
        '<meta charset="utf-8">',
        f"<title>{title}</title>",
    ]
    parts += ["</head>", "<body>"]
    for i, figure in enumerate(figures):
        part = pio.to_html(
            draw_plotly(figure),
            full_html=False,
            include_plotlyjs=i == 0,
            include_mathjax=False,
            div_id=f"{package.name}-{figure.name}",
            config={"displaylogo": False, "responsive": True},
        )
        parts.append(cast(str, part))
    parts += ["</body>", "</html>", ""]
    return "\n".join(parts)


def render(
    package: Package,
    run_log: log.RunLog,
    out_dir: str | Path,
    *,
    fmt: Literal["svg", "png"] = "svg",
    html: bool = False,
) -> list[Path]:
    """Writes the package's figures for ``run_log`` into ``out_dir`` and returns the paths written:
    one ``<package>-<figure>.<fmt>`` per figure, then ``<package>.html`` when ``html`` is true
    (module docstring, Errors and Renderers)."""
    if fmt not in ("svg", "png"):
        raise ValueError(f"fmt must be 'svg' or 'png', got {fmt!r}")
    figures = resolve(package, run_log)
    out_dir = Path(out_dir)
    files = [out_dir / f"{package.name}-{figure.name}.{fmt}" for figure in figures]
    page = out_dir / f"{package.name}.html" if html else None
    targets = [*files, *([page] if page is not None else [])]
    for target in targets:
        if target.exists() or target.is_symlink():
            raise PlotOutputExistsError(
                f"{target}: already exists; astra.plot never overwrites a file"
            )
    text = _html(package, figures) if page is not None else None

    out_dir.mkdir(parents=True, exist_ok=True)
    created: list[Path] = []
    try:
        for figure, target in zip(figures, files, strict=True):
            with pinned_style(package.name, figure.name):
                drawn = draw_matplotlib(figure)
                with create_exclusive(target, created) as stream:
                    write_figure(drawn, stream, fmt)
        if page is not None and text is not None:
            with create_exclusive(page, created) as stream:
                stream.write(text.encode("utf-8"))
    except BaseException:
        for path in created:
            path.unlink(missing_ok=True)
        raise
    return targets


def svg_bytes(drawn: MatplotlibFigure) -> bytes:
    """``drawn`` as SVG: the one ``savefig`` call behind every ASTRA SVG (``render``,
    ``astra.mc_analysis.render_figures`` and ``astra.report``'s inline figures), so a figure is the
    same bytes wherever it lands. Call it under ``pinned_style``, which the bytes depend on. No date
    metadata (and ``SOURCE_DATE_EPOCH`` is not consulted when ``Date`` is ``None``)."""
    stream = io.BytesIO()
    drawn.savefig(stream, format="svg", metadata={"Date": None})  # pyright: ignore[reportUnknownMemberType]
    return stream.getvalue()


def write_figure(drawn: MatplotlibFigure, stream: Any, fmt: str) -> None:
    """``drawn`` written to the binary ``stream``: SVG through ``svg_bytes``, or PNG."""
    if fmt == "svg":
        stream.write(svg_bytes(drawn))
    else:
        drawn.savefig(stream, format=fmt, metadata={})  # pyright: ignore[reportUnknownMemberType]


@contextlib.contextmanager
def create_exclusive(path: Path, created: list[Path]) -> Generator[Any]:
    """``path`` created exclusively (never replacing a file), recorded in ``created`` once it is
    ours."""
    try:
        stream = path.open("xb")
    except FileExistsError as error:
        raise PlotOutputExistsError(
            f"{path}: already exists; astra.plot never overwrites a file"
        ) from error
    created.append(path)
    with stream:
        yield stream
