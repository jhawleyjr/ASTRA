"""astra.report: one self-contained HTML report of a run log, a scenario's log or a Monte Carlo
batch (M6 PR 8, docs/plans/m6-pr8-report.md; M6 plan "Run report", R13, Q6; ADR-0025, ADR-0027).

A report reads its inputs and runs nothing. It is one HTML file with every figure inline as SVG, no
script, and nothing loaded from outside the file: every reference is a ``#fragment`` within it. The
same inputs give the same bytes, in any process: no date, time, output path, host or user is
written.

Log report (``log_report``), in this order:

1. Summary: frames run and their duration (``frames_run * dt``), the halt (check, source, frame,
   vehicle) or ``none``, and every ``health.*`` counter of the log's ``/summary``.
2. Scenario checks, when a scenario is given: one row per check (``astra.scenario.evaluate``, the
   same evaluation ``verify`` reports): its signal, kind and bounds, window, rows checked, worst
   value and frame, and ``pass`` or ``FAIL``; then ``verify``'s other assertions (frames, halt, warn
   counters). The scenario must match the log (``check_scenario``): the same ``dt``, and the log's
   ``/writes`` must be the scenario's events in order, every event at or before ``frames_run``
   included. That is a consistency check, not proof that the scenario produced the log. The log
   must be complete and published (ADR-0027): a log read with ``allow_incomplete=True`` is refused.
3. Figures: every figure of each plot package (``astra.plot``), as ``astra plot`` draws it.
4. Provenance: the input as given, the log's format version, ``git_describe``, ``core_version``,
   ``core_api_version``, ``dt``, its plugin table, and its config text. The config text is the log's
   ``config`` attribute, escaped; it is never parsed or re-serialized (this module does not import
   ``yaml``).

Batch report (``batch_report``), in this order:

1. Outcomes: runs and their ``ok``/``halt``/``error`` counts, and pass/fail/error under the
   criterion (``astra.mc_analysis``, default ``status == halt``).
2. Sizing: with claims (``astra.mc_sizing.load_claims``), ``astra.mc_sizing.check``'s table, the
   master seed and the protocol; without, a stated line, the master seed and the protocol. No claim
   is invented.
3. Correlation: ``astra.mc_analysis.analyze`` as ``astra mc analyze`` prints it: KS, and Spearman
   and PRCC against a metric when one is given, each with m, n and q, its BH decisions and its
   exclusions, then the tree's rules.
4. Figures: ``mc-hist``, ``mc-scatter`` (two or more parameters) and ``mc-envelope-<signal>`` per
   signal, as ``astra mc analyze`` draws them.
5. Provenance: the batch's record (its spec, base config name and text, master seed), then the build
   provenance of its run logs (``batch_provenance``): read from every kept (``ok`` or ``halt``)
   row's log with ``astra.log.read_provenance``, shown once from the lowest-indexed run. A kept
   row whose log is missing, or a log whose build provenance differs from that run's, is a
   ``ReportError`` naming the run (D7, D10).

Figures. Each SVG is ``astra.plot.svg_bytes`` under ``astra.plot.pinned_style``, so it is the file
``astra plot`` or ``astra mc analyze`` would write. Inlined (``inline_svg``), figure k (from 1, in
report order) loses its XML declaration and DOCTYPE, its root ``<svg>`` gets ``id="fig<k>"``, every
id inside it becomes ``fig<k>-<id>`` and every reference to one (``href``, ``xlink:href``,
``url(#...)``) follows. matplotlib's ``<style>`` rule, which would apply to the whole page, is
scoped to its figure. Ids are unique across the report, and every reference resolves inside its own
figure. The report's own ids are ``sec-*`` and ``prov-*``.

Numbers. Every number goes through ``number`` (numpy scalars become Python ``int`` or ``float``
first, so a ``repr`` is never ``np.float64(...)``), and is written by ``repr`` or by the format
``astra mc analyze`` and ``astra mc size`` already use. Every string from an input is escaped.

Errors. ``ReportError``: the inputs are refused (the message names the run, field or event).
``ReportOutputExistsError``: the output exists; ``write`` never overwrites a file. Everything is
computed before ``write`` creates the file; if the write fails, the file it created is deleted.
"""

from __future__ import annotations

import html
import importlib
import math
import re
from collections.abc import Sequence
from pathlib import Path
from typing import Any, cast

import numpy as np

from astra import log, mc_analysis, mc_sizing, montecarlo, plot, scenario

DEFAULT_PACKAGES = ("flight", "attitude", "health")
NO_CLAIMS = "no claims given (--claims FILE, the format astra mc size check reads)"
PLUGIN_COLUMNS = ("path", "category", "name", "core_api_version", "built_with", "sha256")
BUILD_FIELDS = ("format_version", "git_describe", "core_version", "core_api_version", "dt")

# matplotlib's one stylesheet in each SVG (3.11.2). Inlined, it would apply to the whole page, so it
# is scoped to its figure; any other <style> means matplotlib changed and is an internal error.
MATPLOTLIB_STYLE = '<style type="text/css">*{stroke-linejoin: round; stroke-linecap: butt}</style>'

_CSS = """\
body { font-family: sans-serif; margin: 1.5em; color: #1a1a1a; background: #ffffff; }
table { border-collapse: collapse; margin: 0.4em 0 1em 0; }
th, td { border: 1px solid #c8c8c8; padding: 0.15em 0.5em; text-align: left; vertical-align: top; }
td.number { text-align: right; font-family: monospace; }
pre { background: #f4f4f4; padding: 0.6em; white-space: pre-wrap; }
.fail { color: #b00020; font-weight: bold; }
section.figure svg { max-width: 100%; height: auto; }
"""

_PROLOG = re.compile(r"\A<\?xml [^>]*\?>\s*<!DOCTYPE svg [^>]*>\s*")
_ID = re.compile(r'\bid="([^"]*)"')
_HREF = re.compile(r'(\b(?:xlink:)?href)="#([^"]*)"')
_URL = re.compile(r"url\(#([^)]*)\)")
_COMMENT = re.compile(r"(<!--.*?-->)", re.DOTALL)  # split keeps each comment at an odd index


class ReportError(Exception):
    """The report's inputs are refused; the message names the run, field or event."""


class ReportOutputExistsError(FileExistsError):
    """The output file already exists; ``write`` never overwrites one."""


# --- text -----------------------------------------------------------------------------------------


def number(value: object) -> str:
    """``value`` written by ``repr`` after numpy scalars become Python numbers (module docstring,
    Numbers). Anything that is not then an int or a float, a bool included, is a defect."""
    if isinstance(value, np.generic):
        value = cast(Any, value).item()
    if isinstance(value, bool) or not isinstance(value, int | float):
        raise TypeError(f"not a number to report: {value!r}")
    return repr(value)


def _fmt(value: object, spec: str) -> str:
    """``value`` in ``spec`` (``.6g``, ``.3e``), after ``number``'s checks; NaN as ``nan``."""
    number(value)
    real = float(cast(Any, value))
    return "nan" if math.isnan(real) else format(real, spec)


def _e(text: object) -> str:
    return html.escape(str(text), quote=True)


def _cell(value: object) -> str:
    if isinstance(value, str):
        return f"<td>{_e(value)}</td>"
    return f'<td class="number">{number(value)}</td>'


def _table(header: Sequence[str], rows: Sequence[Sequence[str]], *, table_id: str = "") -> str:
    """A table; ``rows`` hold finished ``<td>`` cells."""
    ident = f' id="{table_id}"' if table_id else ""
    head = "".join(f"<th>{_e(name)}</th>" for name in header)
    body = "\n".join(f"<tr>{''.join(row)}</tr>" for row in rows)
    return f"<table{ident}>\n<thead><tr>{head}</tr></thead>\n<tbody>\n{body}\n</tbody>\n</table>"


def _fields(rows: Sequence[tuple[str, str, str]]) -> str:
    """A two-column table of ``(id, name, finished <td>)``."""
    body = "\n".join(
        f'<tr id="{ident}"><th>{_e(name)}</th>{cell}</tr>' for ident, name, cell in rows
    )
    return f"<table>\n<tbody>\n{body}\n</tbody>\n</table>"


def _page(title: str, sections: Sequence[tuple[str, str, str]]) -> str:
    """The whole document: ``sections`` are ``(id, heading, body)``."""
    contents = " ".join(f'<a href="#{ident}">{_e(heading)}</a>' for ident, heading, _ in sections)
    parts = [
        "<!DOCTYPE html>",
        '<html lang="en">',
        "<head>",
        '<meta charset="utf-8">',
        f"<title>{_e(title)}</title>",
        f"<style>\n{_CSS}</style>",
        "</head>",
        "<body>",
        "<main>",
        f"<h1>{_e(title)}</h1>",
        f"<p>{contents}</p>",
    ]
    for ident, heading, body in sections:
        parts += [f'<section id="{ident}">', f"<h2>{_e(heading)}</h2>", body, "</section>"]
    parts += ["</main>", "</body>", "</html>", ""]
    return "\n".join(parts)


# --- figures --------------------------------------------------------------------------------------


def inline_svg(svg: bytes, k: int) -> str:
    """Figure ``k``'s SVG, ready to inline (module docstring, Figures)."""
    text = svg.decode("utf-8")
    prolog = _PROLOG.match(text)
    if prolog is None:
        raise RuntimeError(f"figure {k}: the SVG does not start with matplotlib's prolog")
    text = text[prolog.end() :]
    styles = re.findall(r"<style\b.*?</style>", text, flags=re.DOTALL)
    if styles != [MATPLOTLIB_STYLE]:
        raise RuntimeError(f"figure {k}: unexpected <style> elements {styles!r}")
    prefix = f"fig{k}-"
    text = text.replace(
        MATPLOTLIB_STYLE,
        f'<style type="text/css">#fig{k} *{{stroke-linejoin: round; stroke-linecap: butt}}</style>',
    )
    # Only markup is namespaced: matplotlib writes each text label into a comment, and a label that
    # reads like an id or a reference (review N-2) stays as drawn.
    parts = _COMMENT.split(text)
    for i in range(0, len(parts), 2):
        markup = _ID.sub(lambda m: f'id="{prefix}{m.group(1)}"', parts[i])
        markup = _HREF.sub(lambda m: f'{m.group(1)}="#{prefix}{m.group(2)}"', markup)
        parts[i] = _URL.sub(lambda m: f"url(#{prefix}{m.group(1)})", markup)
    text = "".join(parts)
    if not text.startswith("<svg "):
        raise RuntimeError(f"figure {k}: the SVG's root is not <svg>")
    return f'<svg id="fig{k}" {text[len("<svg ") :].rstrip()}'


def _figures_section(figures: Sequence[tuple[str, bytes]]) -> str:
    parts: list[str] = []
    for k, (name, svg) in enumerate(figures, start=1):
        parts += [
            '<section class="figure">',
            f"<h3>{_e(name)}</h3>",
            inline_svg(svg, k),
            "</section>",
        ]
    return "\n".join(parts) if parts else "<p>no figures</p>"


def package_figures(
    packages: Sequence[plot.Package], run_log: log.RunLog
) -> list[tuple[str, bytes]]:
    """Every figure of ``packages`` for ``run_log``, as ``astra plot`` draws it:
    ``(<package>-<figure>, svg)``. Raises ``astra.plot.PlotPackageError``."""
    resolved = [(package, plot.resolve(package, run_log)) for package in packages]
    figures: list[tuple[str, bytes]] = []
    for package, drawn_figures in resolved:
        for figure in drawn_figures:
            with plot.pinned_style(package.name, figure.name):
                drawn = plot.draw_matplotlib(figure)
                figures.append((f"{package.name}-{figure.name}", plot.svg_bytes(drawn)))
    return figures


def batch_figures(
    batch: mc_analysis.Batch, outcomes: mc_analysis.Outcomes, signals: Sequence[str]
) -> list[tuple[str, bytes]]:
    """The batch's figures, as ``astra mc analyze`` draws them. Raises
    ``astra.mc_analysis.EnvelopeError``."""
    if len(set(signals)) != len(signals):
        raise mc_analysis.EnvelopeError(f"a signal is named twice in {list(signals)}")
    envelopes = [mc_analysis.envelope(batch, signal, outcomes) for signal in signals]
    drawers: list[Any] = []
    if batch.parameters:
        drawers.append(lambda: mc_analysis.draw_histograms(batch, outcomes))
    if len(batch.parameters) >= 2:
        drawers.append(lambda: mc_analysis.draw_scatter(batch, outcomes))
    drawers += [lambda env=env: mc_analysis.draw_envelope(env) for env in envelopes]
    figures: list[tuple[str, bytes]] = []
    for name, draw in zip(mc_analysis.figure_names(batch, signals), drawers, strict=True):
        with plot.pinned_style("mc", name):
            figures.append((name, plot.svg_bytes(draw())))
    return figures


# --- log report -----------------------------------------------------------------------------------


def check_scenario(case: scenario.Scenario, run_log: log.RunLog) -> None:
    """Refuses a scenario that does not match the log (module docstring, Log report)."""
    if case.dt != run_log.dt:
        raise ReportError(
            f"scenario {case.path}: dt {case.dt!r} s differs from the log's {run_log.dt!r} s"
        )
    writes = cast(Any, run_log.writes)
    logged = (
        []
        if writes is None
        else [
            (int(frame), str(name), float(value))
            for frame, name, value in zip(
                cast(list[Any], writes["frame"].tolist()),
                cast(list[Any], writes["name"].tolist()),
                cast(list[Any], writes["value"].tolist()),
                strict=True,
            )
        ]
    )
    events = [(event.frame, event.name, event.value) for event in case.events]
    frames_run = cast(int, run_log.frames_run)
    for i, write in enumerate(logged):
        if i >= len(events) or write != events[i]:
            expected = events[i] if i < len(events) else "no event"
            raise ReportError(
                f"scenario {case.path}: the log's write {i} {write!r} is not the scenario's event "
                f"{expected!r} (frame, input, value)"
            )
    # astra.scenario writes a frame's events before that frame's step, so a run that halts at frame
    # frames_run has written its events at that frame too; only later ones go unwritten (review
    # A-001).
    for event in events[len(logged) :]:
        if event[0] <= frames_run:
            raise ReportError(
                f"scenario {case.path}: the event {event!r} (frame, input, value) is at or before "
                f"frames_run {frames_run} but the log has no such write"
            )


def _summary_section(run_log: log.RunLog) -> str:
    frames_run = cast(int, run_log.frames_run)
    halt = run_log.halt
    halt_cell = (
        "<td>none</td>"
        if halt is None
        else f"<td>{_e(halt.check)} source={_e(halt.source)} frame={number(halt.frame)} "
        f"vehicle={number(halt.vehicle_index)}</td>"
    )
    duration = frames_run * run_log.dt
    rows = [
        ("sec-frames-run", "frames run", _cell(frames_run)),
        ("sec-duration", "duration [s]", _cell(duration)),
        ("sec-halt", "halt", halt_cell),
    ]
    health = [
        [f"<td>{_e(name)}</td>", _cell(run_log.health[name])] for name in sorted(run_log.health)
    ]
    return "\n".join(
        [
            _fields(rows),
            "<h3>Health counters</h3>",
            _table(["counter", "value"], health, table_id="sec-health"),
        ]
    )


def _window(result: scenario.CheckResult) -> str:
    if result.span is not None:
        return result.span
    window = result.check.window
    if window.stop is None:
        return "final"
    return f"frames {window.first}..{window.stop - 1}"


def _bounds(check: scenario.Check) -> str:
    if check.kind == "between":
        return f"between [{number(check.a)}, {number(check.b)}]"
    return f"{check.kind} {number(check.a)} +/- {number(check.b)}"


def _checks_section(case: scenario.Scenario, source: str, run_log: log.RunLog) -> str:
    results = [scenario.evaluate(case, check, run_log) for check in case.checks]
    rows: list[list[str]] = []
    for result in results:
        if result.worst is None:
            worst = ["<td>none</td>", "<td>none</td>"]
        else:
            worst = [_cell(result.worst[0]), _cell(result.worst[1])]
        if result.passed:
            verdict = "<td>pass</td>"
        else:
            reason = f": {_e(result.problem)}" if result.problem is not None else ""
            verdict = f'<td class="fail">FAIL{reason}</td>'
        rows.append(
            [
                _cell(result.check.index),
                _cell(result.check.signal),
                f"<td>{_e(_bounds(result.check))}</td>",
                f"<td>{_e(_window(result))}</td>",
                _cell(result.rows),
                *worst,
                verdict,
            ]
        )
    header = ["check", "signal", "kind", "window", "rows", "worst value", "worst frame", "result"]
    check_messages = {result.message() for result in results} - {None}
    other = [failure for failure in scenario.verify(case, run_log) if failure not in check_messages]
    others = (
        "<ul>\n" + "\n".join(f"<li>{_e(failure)}</li>" for failure in other) + "\n</ul>"
        if other
        else "<p>none</p>"
    )
    return "\n".join(
        [
            f"<p>scenario: {_e(source)} ({_e(case.name)})</p>",
            _table(header, rows, table_id="sec-check-rows")
            if rows
            else "<p>the scenario has no checks</p>",
            "<h3>Other assertions (frames, halt, warn counters)</h3>",
            f'<section id="sec-other-assertions">\n{others}\n</section>',
        ]
    )


def plugin_rows(plugins: Any) -> list[list[Any]] | None:
    """``/plugins`` (a DataFrame, or ``None`` when the log has none) as rows in
    ``PLUGIN_COLUMNS`` order."""
    if plugins is None:
        return None
    frame = plugins
    columns: dict[str, list[Any]] = {name: list(frame[name].tolist()) for name in PLUGIN_COLUMNS}
    count = len(columns[PLUGIN_COLUMNS[0]])
    return [[columns[name][i] for name in PLUGIN_COLUMNS] for i in range(count)]


def _plugin_table(plugins: Any) -> str:
    rows = plugin_rows(plugins)
    if rows is None:
        return "<p>no plugin table</p>"
    cells = [[_cell(value) for value in row] for row in rows]
    return _table(list(PLUGIN_COLUMNS), cells, table_id="prov-plugins")


def _build_rows(provenance: log.Provenance | log.RunLog) -> list[tuple[str, str, str]]:
    return [
        ("prov-format_version", "log format version", _cell(provenance.format_version)),
        ("prov-git_describe", "git describe", _cell(provenance.git_describe)),
        ("prov-core_version", "core version", _cell(provenance.core_version)),
        ("prov-core_api_version", "core API version", _cell(provenance.core_api_version)),
        ("prov-dt", "dt [s]", _cell(provenance.dt)),
    ]


def log_report(
    run_log: log.RunLog,
    packages: Sequence[plot.Package],
    *,
    source: str,
    case: scenario.Scenario | None = None,
    case_source: str = "",
) -> str:
    """The report of a complete, published run log (module docstring, Log report); ``source`` is the
    input as given, ``case`` the scenario whose checks it shows. Raises ``ReportError`` and
    ``astra.plot.PlotPackageError``."""
    # A log read with allow_incomplete=True can hold frames_run and still be a dropped run or an
    # unpublished .partial (ADR-0027); neither is reported as a finished run (review G-001).
    if run_log.state != log.COMPLETE or not run_log.published or run_log.frames_run is None:
        published = "published" if run_log.published else "not published (a .partial name)"
        raise ReportError(
            f"{source}: the log's finalization state is {run_log.state} and it is {published}; "
            "astra.report reports only a complete, published log (ADR-0027)"
        )
    if case is not None:
        check_scenario(case, run_log)
    figures = package_figures(packages, run_log)
    sections = [("sec-summary", "Summary", _summary_section(run_log))]
    if case is not None:
        sections.append(
            ("sec-checks", "Scenario checks", _checks_section(case, case_source, run_log))
        )
    sections.append(("sec-figures", "Figures", _figures_section(figures)))
    provenance = "\n".join(
        [
            _fields([("prov-input", "input", _cell(source)), *_build_rows(run_log)]),
            "<h3>Plugins</h3>",
            _plugin_table(run_log.plugins),
            "<h3>Config (the log's own text)</h3>",
            f'<pre id="prov-config">{_e(run_log.config)}</pre>',
        ]
    )
    sections.append(("sec-provenance", "Provenance", provenance))
    return _page(f"astra report: {source}", sections)


# --- batch report ---------------------------------------------------------------------------------


def batch_record(batch: mc_analysis.Batch) -> tuple[str, str, str]:
    """The batch's record: its spec text, base config name and base config text (summary.parquet's
    schema metadata, ``astra.montecarlo``). Raises ``ReportError``."""
    path = cast(Path, batch.path) / montecarlo.SUMMARY
    pq: Any = importlib.import_module("pyarrow.parquet")
    try:
        schema: Any = pq.read_schema(path)
    except (OSError, ValueError) as error:
        raise ReportError(f"{path}: cannot read the summary's schema ({error})") from error
    metadata = cast(dict[bytes, bytes], schema.metadata or {})
    keys = (montecarlo.SPEC_KEY, montecarlo.CONFIG_NAME_KEY, montecarlo.CONFIG_KEY)
    texts: list[str] = []
    for key in keys:
        if key not in metadata:
            raise ReportError(f"{path}: no {key.decode()} record (a batch from before M6 PR 6)")
        try:
            texts.append(metadata[key].decode("utf-8"))
        except UnicodeDecodeError as error:
            raise ReportError(f"{path}: {key.decode()} is not UTF-8 text") from error
    return texts[0], texts[1], texts[2]


def batch_provenance(batch: mc_analysis.Batch) -> tuple[int, log.Provenance] | None:
    """The build provenance of the batch's run logs, from the lowest-indexed kept run, after every
    kept run's log is read and compared with it (module docstring, Batch report). ``None`` when the
    batch kept no log. Raises ``ReportError``, and ``astra.log``'s errors for a refused log."""
    directory = cast(Path, batch.path)
    runs = sorted(int(batch.value("run", row)) for row in mc_analysis.analyzed_rows(batch))
    for run in runs:
        path = directory / montecarlo.log_name(run)
        if not path.exists():
            raise ReportError(
                f"run {run}: its log {path} is missing (an {_status(batch, run)} row keeps one); "
                "astra mc analyze without --signal and astra mc size read only the summary"
            )
    first: tuple[int, log.Provenance] | None = None
    for run in runs:
        provenance = log.read_provenance(directory / montecarlo.log_name(run))
        if first is None:
            first = (run, provenance)
            continue
        reference = first[1]
        for field in BUILD_FIELDS:
            if getattr(provenance, field) != getattr(reference, field):
                raise ReportError(
                    f"run {run}: its log's {field} {getattr(provenance, field)!r} differs from run "
                    f"{first[0]}'s {getattr(reference, field)!r}"
                )
        if plugin_rows(provenance.plugins) != plugin_rows(reference.plugins):
            raise ReportError(f"run {run}: its log's plugin table differs from run {first[0]}'s")
    return first


def _status(batch: mc_analysis.Batch, run: int) -> str:
    row = batch.runs.index(run)
    return str(batch.value("status", row))


def _outcomes_section(batch: mc_analysis.Batch, outcomes: mc_analysis.Outcomes) -> str:
    statuses = [str(batch.value("status", row)) for row in range(len(batch))]
    rows = [
        ("sec-runs", "runs", _cell(len(batch))),
        ("sec-status-ok", "ok", _cell(statuses.count("ok"))),
        ("sec-status-halt", "halt", _cell(statuses.count("halt"))),
        ("sec-status-error", "error", _cell(statuses.count("error"))),
        ("sec-criterion", "criterion", _cell(outcomes.criterion)),
        ("sec-outcome-pass", "pass", _cell(outcomes.count(mc_analysis.PASS))),
        ("sec-outcome-fail", "fail", _cell(outcomes.count(mc_analysis.FAIL))),
        ("sec-outcome-error", "error (not analyzed)", _cell(outcomes.count(mc_analysis.ERROR))),
    ]
    return _fields(rows)


def _sizing_section(table: mc_sizing.Table | None, seed: int | None) -> str:
    seed_cell = "<td>unknown</td>" if seed is None else _cell(seed)
    parts = [
        _fields([("sec-master-seed", "master seed", seed_cell)]),
        f"<p>protocol: {_e(mc_sizing.PROTOCOL)}</p>",
    ]
    if table is None:
        parts.append(f'<p id="sec-no-claims">{_e(NO_CLAIMS)}</p>')
        return "\n".join(parts)
    header = [
        "option",
        "tier",
        "claim",
        "runs analyzed",
        "errors",
        "estimate",
        "lower",
        "upper",
        "verdict",
        "needed",
        "minimum",
    ]
    rows: list[list[str]] = []
    details: list[str] = []
    for i, check in enumerate(table.rows):
        verdict = check.verdict if check.verdict is not None else mc_sizing.NOMINAL
        needed = "<td>none</td>" if check.needed is None else _cell(check.needed)
        rows.append(
            [
                _cell(check.option),
                _cell(check.tier),
                _cell(check.claim),
                _cell(check.n),
                _cell(check.errors),
                f'<td class="number">{_fmt(check.estimate, ".6g")}</td>',
                f'<td class="number">{_fmt(check.lower, ".6g")}</td>',
                f'<td class="number">{_fmt(check.upper, ".6g")}</td>',
                _cell(verdict),
                needed,
                _cell(check.minimum),
            ]
        )
        if check.parameters:
            parameter_rows = [
                [
                    _cell(row.parameter),
                    *(
                        f'<td class="number">{_fmt(value, ".6g")}</td>'
                        for value in (row.rho, row.lower, row.upper, row.abs_lower, row.abs_upper)
                    ),
                    _cell(row.note),
                ]
                for row in check.parameters
            ]
            details.append(f"<h3>Claim {i + 1}: {_e(check.claim)}</h3>")
            details.append(
                _table(
                    ["parameter", "rho", "lower", "upper", "|rho| lower", "|rho| upper", "note"],
                    parameter_rows,
                    table_id=f"sec-claim-{i + 1}-parameters",
                )
            )
        if check.notes:
            details.append(f"<p>claim {i + 1} notes:</p>")
            details.append(
                "<ul>\n" + "\n".join(f"<li>{_e(n)}</li>" for n in check.notes) + "\n</ul>"
            )
    parts.append(_table(header, rows, table_id="sec-claims"))
    parts += details
    return "\n".join(parts)


def _ranking(ranking: mc_analysis.Ranking, ident: str) -> str:
    heading = (
        f"{ranking.method} against {ranking.against} (q = {ranking.q:g}, m = {ranking.m}, "
        f"n = {ranking.n})"
    )
    parts = [f"<h3>{_e(heading)}</h3>"]
    if ranking.status != "ok":
        parts.append(f"<p>{_e(ranking.status)}</p>")
    if ranking.rows:
        rows = [
            [
                _cell(rank),
                _cell(row.parameter),
                f'<td class="number">{_fmt(row.effect, ".6g")}</td>',
                f'<td class="number">{_fmt(row.p, ".3e")}</td>',
                _cell("reject" if row.rejected else "keep"),
            ]
            for rank, row in enumerate(ranking.rows, start=1)
        ]
        header = ["rank", "parameter", ranking.effect_name(), "p", "BH"]
        parts.append(_table(header, rows, table_id=ident))
    if ranking.excluded:
        items = "\n".join(
            f"<li>{_e(name)} ({_e(reason)})</li>" for name, reason in ranking.excluded
        )
        parts.append(f"<p>excluded:</p>\n<ul>\n{items}\n</ul>")
    return "\n".join(parts)


def _correlation_section(analysis: mc_analysis.Analysis) -> str:
    batch = analysis.batch
    parts = [f"<p>parameters: {_e(', '.join(batch.parameters) or 'none')}</p>"]
    if batch.seed_parameters:
        parts.append(f"<p>seed parameters (not ranked): {_e(', '.join(batch.seed_parameters))}</p>")
    parts.append(_ranking(analysis.ks, "sec-ks"))
    if analysis.spearman is None or analysis.prcc is None:
        parts.append("<p>spearman, prcc: no metric given: Spearman and PRCC not run</p>")
    else:
        parts.append(_ranking(analysis.spearman, "sec-spearman"))
        parts.append(_ranking(analysis.prcc, "sec-prcc"))
    tree = analysis.tree
    parts.append(
        f"<h3>tree (depth &lt;= {tree.max_depth}, grid {tree.grid}, min leaf {tree.min_leaf})</h3>"
    )
    if tree.root is None:
        parts.append(f"<p>{_e(tree.status)}</p>")
    else:
        rules = [f"fail if {rule.text()}" for rule in tree.rules()] or ["no fail leaf"]
        parts.append(
            '<ul id="sec-tree-rules">\n'
            + "\n".join(f"<li>{_e(rule)}</li>" for rule in rules)
            + "\n</ul>"
        )
    return "\n".join(parts)


def batch_report(
    batch: mc_analysis.Batch,
    *,
    source: str,
    criterion: str | None = None,
    metric: str | None = None,
    q: float = mc_analysis.DEFAULT_Q,
    claims: Sequence[mc_sizing.Claim] | None = None,
    signals: Sequence[str] = (),
) -> str:
    """The report of a batch (module docstring, Batch report); ``source`` is the input as given.
    Raises ``ReportError``, ``astra.mc_analysis``'s ``BatchError``, ``CriterionError`` and
    ``EnvelopeError``, ``astra.mc_sizing.SizingError``, and ``astra.log``'s errors for a refused
    run log."""
    if batch.path is None:
        raise ReportError("a synthetic batch has no record or logs to report")
    spec_text, config_name, config_text = batch_record(batch)
    provenance = batch_provenance(batch)
    analysis = mc_analysis.analyze(batch, criterion, metric, q)
    table = mc_sizing.check(batch, claims) if claims else None
    seed = table.seed if table is not None else mc_sizing.batch_seed(batch)
    figures = batch_figures(batch, analysis.outcomes, signals)
    seed_cell = "<td>unknown</td>" if seed is None else _cell(seed)
    record = [
        _fields(
            [
                ("prov-input", "input", _cell(source)),
                ("prov-seed", "master seed", seed_cell),
                ("prov-config-name", "base config", _cell(config_name)),
            ]
        ),
        "<h3>Spec (the batch's record)</h3>",
        f'<pre id="prov-spec">{_e(spec_text)}</pre>',
        "<h3>Base config (the batch's record)</h3>",
        f'<pre id="prov-config">{_e(config_text)}</pre>',
        "<h3>Build (the run logs)</h3>",
    ]
    if provenance is None:
        record.append('<p id="prov-no-log">no run log</p>')
    else:
        run, first = provenance
        record += [
            _fields(
                [("prov-log", "read from", _cell(montecarlo.log_name(run))), *_build_rows(first)]
            ),
            "<h3>Plugins</h3>",
            _plugin_table(first.plugins),
        ]
    sections = [
        ("sec-outcomes", "Outcomes", _outcomes_section(batch, analysis.outcomes)),
        ("sec-sizing", "Sizing", _sizing_section(table, seed)),
        ("sec-correlation", "Correlation", _correlation_section(analysis)),
        ("sec-figures", "Figures", _figures_section(figures)),
        ("sec-provenance", "Provenance", "\n".join(record)),
    ]
    return _page(f"astra report: {source}", sections)


# --- output ---------------------------------------------------------------------------------------


def write(text: str, out: str | Path) -> Path:
    """``text`` written to ``out`` as UTF-8, created exclusively (never overwriting a file). If the
    write fails, the file this call created is deleted and the error re-raised."""
    out = Path(out)
    if out.exists() or out.is_symlink():
        raise ReportOutputExistsError(
            f"{out}: already exists; astra.report never overwrites a file"
        )
    try:
        stream = out.open("xb")
    except FileExistsError as error:
        raise ReportOutputExistsError(
            f"{out}: already exists; astra.report never overwrites a file"
        ) from error
    try:
        with stream:
            stream.write(text.encode("utf-8"))
    except BaseException:
        out.unlink(missing_ok=True)
        raise
    return out
