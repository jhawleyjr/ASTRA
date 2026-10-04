"""M6 PR 8: the run report, ``astra.report`` and ``astra report`` (docs/plans/m6-pr8-report.md;
M6 plan R13, R15's report case, C-11 to C-13). Every test is named ``test_report_*``, so
``.venv/bin/pytest tests/python -k report`` selects each one.

Also here: the allow-list check (``resource_refs``, D9) that R3b (``test_plot.py``) and R13
share.

Expected values are read independently of ``astra.report``: the log's attributes with h5py, the
summary with pyarrow, check rows recomputed with numpy from the log's columns, and each figure as
the file ``astra plot`` or ``astra mc analyze`` writes. Analysis and sizing rows are compared with
``astra.mc_analysis.analyze`` and ``astra.mc_sizing.check``, whose own tests (R6 to R12) are their
oracle; here they show that the report holds every row, in order, unchanged."""

from __future__ import annotations

import dataclasses
import html.parser
import math
import os
import re
import shutil
import subprocess
import sys
from collections.abc import Sequence
from pathlib import Path
from typing import Any, cast

import h5py  # pyright: ignore[reportMissingTypeStubs]
import numpy as np
import pandas as pd  # pyright: ignore[reportMissingTypeStubs]
import pyarrow.parquet as pq  # pyright: ignore[reportMissingTypeStubs]
import pytest
import yaml

import resource_refs
from astra import cli, log, mc_analysis, mc_sizing, montecarlo, plot, report, scenario
from resource_refs import PLOTLY_PAGE, REPORT, SVG, Profile

ROOT = Path(__file__).resolve().parents[2]
ASTRA = Path(sys.executable).parent / "astra"
SCENARIOS = ROOT / "tests/scenarios"
DESCENT = ROOT / "tests/python/mc_analysis_fixtures/descent_batch.yaml"
CLAIMS = ROOT / "tests/python/mc_sizing_fixtures/claims.yaml"
NOTIONAL = ROOT / "aircraft/notional_transport/plots/cruise.yaml"
ALTITUDE = "v0.derived.altitude"
METRIC = "min.v0.derived.altitude"

# --- the resource check (D9) ----------------------------------------------------------------------

PROFILES = {"SVG": SVG, "REPORT": REPORT, "PLOTLY_PAGE": PLOTLY_PAGE}

# Each construct, written where its profile allows the element around it, so the flag comes from
# the construct itself; ``{e}`` is an element with a style attribute in that profile (``g``; ``div``
# on the plotly page). The second field is a fragment of the line the check must give.
CONSTRUCTS = [
    # PR 5's R3b snippets
    ('<img src="http://example.com/a.png">', "element <img>"),
    ('<script src="https://cdn.example.com/plotly.js"></script>', "src="),
    ('<link rel="stylesheet" href="x.css">', "element <link>"),
    ('<a href="http://example.com">x</a>', "href"),
    ('<svg><image xlink:href="http://example.com/a.png"/></svg>', "element <image>"),
    ('<img srcset="a.png 1x">', "srcset"),
    ('<img srcset="data:image/png;base64,AA 1x, https://x.test/a.png 2x">', "srcset"),
    ('<video poster="p.png"></video>', "poster"),
    ('<object data="x.svg"></object>', "element <object>"),
    ("<iframe></iframe>", "element <iframe>"),
    ('<base href="#">', "element <base>"),
    ('<meta http-equiv="Refresh" content="0;url=http://example.com">', "http-equiv"),
    ('<{e} style="background:url(http://example.com/a.png)">x</{e}>', "url() not a #fragment"),
    ('<style>@import "x.css";</style>', "at-rule"),
    ("<style>p { background: url('x.png') }</style>", "url() not a #fragment"),
    ('<form action="http://example.com"></form>', "action"),
    ('<body background="x.png"></body>', "background"),
    # the enumeration's three known misses (PLAN.md's (M6) item)
    ('<?xml-stylesheet href="x.css" type="text/css"?>', "processing instruction"),
    ("<{e} style=\"background:image-set('x.png' 1x)\">x</{e}>", "CSS function image-set()"),
    ('<svg><set attributeName="href" to="http://x.test/a"/></svg>', "element <set>"),
    ('<svg><animate attributeName="href" values="http://x.test/a"/></svg>', "element <animate>"),
    # and beyond
    ('<{e} onload="fetch(1)">x</{e}>', "onload"),
    ('<a href="javascript:alert(1)">x</a>', "href"),
    ("<blink>x</blink>", "element <blink>"),
    ('<{e} style="color: var(--x)">x</{e}>', "CSS function var()"),
    ('<{e} style="background: u\\72l(x.png)">x</{e}>', "CSS escape"),
    ("<style>@font-face { font-family: x }</style>", "at-rule"),
    ("<!DOCTYPE something-else>", "declaration"),
    # review A-002: an HTML parser ignores the slash, so what follows is a live stylesheet or script
    ("<style/>body{background:url(https://example.com/x.png)}</style>", "self-closing <style/>"),
    ("<script/>fetch('https://example.com/x')</script>", "self-closing <script/>"),
    # Joe's review of #95: inside an SVG a browser decodes a style's character references and
    # parses its tags; a browser closes "<!-->" and "<!--->" at once and ends a comment at "--!>".
    ('<svg><style>&#64;import "https://example.com/x.css";</style></svg>', "character reference"),
    (
        "<svg><style>g{fill:&#117;&#114;&#108;(https://example.com/x)}</style></svg>",
        "character reference",
    ),
    (
        "<svg><style>&commat;import 'https://example.com/x.css';</style></svg>",
        "character reference",
    ),
    (
        '<svg><style><image href="https://example.com/a.png"/></style></svg>',
        "markup inside the style",
    ),
    ('<!--><img src="https://example.com/a.png"><!-- -->', "<img"),
    ('<!---><img src="https://example.com/a.png"><!-- -->', "<img"),
    ('<!-- a --!><img src="https://example.com/a.png"><!-- b -->', "<img"),
    ('<svg><use xlink:href="data:image/svg+xml;base64,AA=="/></svg>', "not a #fragment"),
    ('<svg><g clip-path="url(data:x)"></g></svg>', "clip-path"),
    ('<svg><g transform="translate(1,2) url(x)"></g></svg>', "transform"),
]


@pytest.mark.parametrize("profile_name", list(PROFILES))
@pytest.mark.parametrize(("template", "expected"), CONSTRUCTS)
def test_report_resource_check_flags_each_construct(
    profile_name: str, template: str, expected: str
) -> None:
    profile = PROFILES[profile_name]
    snippet = template.replace("{e}", "div" if profile is PLOTLY_PAGE else "g")
    found = resource_refs.disallowed(snippet, profile)
    assert found, snippet
    assert any(expected in line for line in found) or any(
        line.startswith("element <") for line in found
    ), (snippet, found)


@pytest.mark.parametrize(
    ("template", "expected"),
    [(t, e) for t, e in CONSTRUCTS if not e.startswith("element")],
)
def test_report_resource_check_names_the_construct_where_its_element_is_allowed(
    template: str, expected: str
) -> None:
    # In REPORT every element above but the plainly foreign ones is allowed, so the line names the
    # construct itself, not the element around it.
    snippet = template.replace("{e}", "g")
    found = resource_refs.disallowed(snippet, REPORT)
    assert any(expected in line for line in found), (snippet, found)


_MATPLOTLIB_LIKE = (
    '<?xml version="1.0" encoding="utf-8" standalone="no"?>\n'
    '<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN"\n'
    '  "http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd">\n'
    '<svg xmlns:xlink="http://www.w3.org/1999/xlink" width="576pt" height="432pt" '
    'viewBox="0 0 576 432" xmlns="http://www.w3.org/2000/svg" version="1.1">\n'
    " <metadata>\n"
    '  <rdf:RDF xmlns:dc="http://purl.org/dc/elements/1.1/" '
    'xmlns:cc="http://creativecommons.org/ns#" '
    'xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">\n'
    '   <cc:Work><dc:type rdf:resource="http://purl.org/dc/dcmitype/StillImage"/>'
    "<dc:format>image/svg+xml</dc:format><dc:creator><cc:Agent><dc:title>Matplotlib</dc:title>"
    "</cc:Agent></dc:creator></cc:Work>\n"
    "  </rdf:RDF>\n"
    " </metadata>\n"
    " <defs>\n"
    '  <style type="text/css">*{stroke-linejoin: round; stroke-linecap: butt}</style>\n'
    '  <path id="DejaVuSans-30" d="M 0 0 L 1 1 z" transform="scale(0.015625)"/>\n'
    '  <clipPath id="p0123456789"><rect x="0" y="0" width="1" height="1"/></clipPath>\n'
    " </defs>\n"
    ' <g id="figure_1"><g id="patch_1"><path d="M 0 0 L 1 1" style="fill: #ffffff"/></g>\n'
    '  <g clip-path="url(#p0123456789)"><use xlink:href="#DejaVuSans-30" x="1" y="2" '
    'transform="translate(1 2) rotate(-90) scale(0.1 -0.1)"/></g></g>\n'
    "</svg>\n"
)


def test_report_resource_check_allows_each_profile_s_own_constructs() -> None:
    assert resource_refs.disallowed(_MATPLOTLIB_LIKE, SVG) == []
    assert resource_refs.id_problems(_MATPLOTLIB_LIKE) == []
    body = _MATPLOTLIB_LIKE.split("\n", 3)[3]  # the inline SVG, without its prolog
    report = (
        '<!DOCTYPE html>\n<html lang="en"><head><meta charset="utf-8"><title>t</title>'
        "<style>body { margin: 0 } .x th { text-align: left }</style></head><body><main>"
        '<section id="sec-summary"><h2>Summary</h2><p class="x"><a href="#sec-summary">top</a></p>'
        "<table><caption>c</caption><thead><tr><th>a</th></tr></thead><tbody><tr><td>1</td></tr>"
        "</tbody></table><ul><li>x</li></ul><pre><code>sim: {}</code></pre><h3>f</h3>"
        f"{body}</section></main></body></html>\n"
    )
    assert resource_refs.disallowed(report, REPORT) == []
    assert resource_refs.id_problems(report) == []
    page = (
        '<!DOCTYPE html>\n<html><head><meta charset="utf-8"><title>t</title></head><body>'
        '<div style="height:900px; width:100%;"><div id="a" class="plotly-graph-div" '
        'style="height:100%; width:100%;"></div></div>'
        '<script>var u = "https://plot.ly"; window.PLOTLYENV = {};</script></body></html>\n'
    )
    assert resource_refs.disallowed(page, PLOTLY_PAGE) == []


def test_report_resource_check_profiles_are_distinct() -> None:
    # A script is the plotly page's alone; the report's own HTML is not an SVG file's.
    assert resource_refs.disallowed("<script>x</script>", REPORT)
    assert resource_refs.disallowed("<script>x</script>", SVG)
    assert resource_refs.disallowed("<table></table>", SVG)
    assert resource_refs.disallowed("<svg></svg>", PLOTLY_PAGE)
    profiles: list[Profile] = [SVG, REPORT, PLOTLY_PAGE]
    assert len({profile.name for profile in profiles}) == 3


def test_report_id_check_catches_repeats_unresolved_and_cross_figure_references() -> None:
    two = '<svg><g id="a"/><use xlink:href="#a"/></svg><svg><g id="b"/><use xlink:href="#a"/></svg>'
    problems = resource_refs.id_problems(two)
    assert problems == ["<use xlink:href>: #a, used figure 2, resolves elsewhere"], problems
    repeated = '<svg><g id="a"/></svg><svg><g id="a"/></svg>'
    assert resource_refs.id_problems(repeated) == ["id 'a' repeated"]
    unresolved = '<svg><g clip-path="url(#nowhere)"/></svg><a href="#sec-x">x</a>'
    assert resource_refs.id_problems(unresolved) == [
        "<g clip-path>: #nowhere does not resolve",
        "<a href>: #sec-x does not resolve",
    ]
    outside = '<section id="s"></section><svg><use xlink:href="#s"/></svg>'
    assert resource_refs.id_problems(outside) == [
        "<use xlink:href>: #s, used figure 1, resolves elsewhere"
    ]


# --- reading a report -----------------------------------------------------------------------------

_VOID = frozenset({"meta", "br", "hr", "img", "link", "input"})


@dataclasses.dataclass
class Node:
    tag: str
    attrs: dict[str, str]
    children: list[Node | str] = dataclasses.field(
        default_factory=lambda: cast(list[Node | str], [])
    )

    def text(self) -> str:
        return "".join(c if isinstance(c, str) else c.text() for c in self.children)

    def find(self, tag: str) -> list[Node]:
        found: list[Node] = []
        for child in self.children:
            if isinstance(child, Node):
                if child.tag == tag:
                    found.append(child)
                found += child.find(tag)
        return found


class _Tree(html.parser.HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.root = Node("#document", {})
        self.stack = [self.root]
        self.ids: dict[str, Node] = {}

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        node = Node(tag, {name: value or "" for name, value in attrs})
        self.stack[-1].children.append(node)
        if "id" in node.attrs:
            self.ids[node.attrs["id"]] = node
        if tag not in _VOID:
            self.stack.append(node)

    def handle_startendtag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        node = Node(tag, {name: value or "" for name, value in attrs})
        self.stack[-1].children.append(node)
        if "id" in node.attrs:
            self.ids[node.attrs["id"]] = node

    def handle_endtag(self, tag: str) -> None:
        for i in range(len(self.stack) - 1, 0, -1):
            if self.stack[i].tag == tag:
                del self.stack[i:]
                return

    def handle_data(self, data: str) -> None:
        self.stack[-1].children.append(data)


def parse(text: str) -> dict[str, Node]:
    tree = _Tree()
    tree.feed(text)
    tree.close()
    return tree.ids


def cells(row: Node) -> list[str]:
    return [
        cell.text() for cell in row.children if isinstance(cell, Node) and cell.tag in ("td", "th")
    ]


def field(ids: dict[str, Node], ident: str) -> str:
    """The value cell of a two-column field row."""
    return cells(ids[ident])[1]


def body_rows(ids: dict[str, Node], ident: str) -> list[list[str]]:
    table = ids[ident]
    return [cells(row) for body in table.find("tbody") for row in body.find("tr")]


_INLINE = re.compile(r'<h3>([^<]*)</h3>\n(<svg id="fig(\d+)" .*?</svg>)', re.DOTALL)
_PROLOG = re.compile(r"\A<\?xml [^>]*\?>\s*<!DOCTYPE svg [^>]*>\s*")


def inline_figures(text: str) -> list[tuple[str, int, str]]:
    """``(heading, k, svg)`` of every inline figure, in order."""
    return [
        (html.unescape(m.group(1)), int(m.group(3)), m.group(2)) for m in _INLINE.finditer(text)
    ]


def denamespaced(svg: str, k: int) -> str:
    """The test's own inverse of the report's namespacing (D8), so an inline figure can be compared
    with the file ``astra plot`` writes."""
    prefix = f"fig{k}-"
    text = svg.replace(f'<svg id="fig{k}" ', "<svg ", 1)
    text = text.replace(f"#fig{k} *{{", "*{")
    text = re.sub(rf'\bid="{prefix}', 'id="', text)
    text = re.sub(rf'="#{prefix}', '="#', text)
    return re.sub(rf"url\(#{prefix}", "url(#", text)


def file_svg(path: Path) -> str:
    return _PROLOG.sub("", path.read_text(encoding="utf-8")).rstrip()


# --- fixtures -------------------------------------------------------------------------------------


@pytest.fixture(scope="module")
def logs(tmp_path_factory: pytest.TempPathFactory) -> dict[str, Path]:
    """Logs of the elevator step and of the atmosphere-ceiling halt, run by astra.scenario."""
    out = tmp_path_factory.mktemp("report-logs")
    paths: dict[str, Path] = {}
    for name in ("elevator_step", "atmosphere_ceiling_halt"):
        case = scenario.load(SCENARIOS / f"{name}.yaml", root=ROOT)
        result = scenario.run(case, out / f"{name}.h5")
        assert result.failures == []
        paths[name] = out / f"{name}.h5"
    return paths


@pytest.fixture(scope="module")
def batch_dir(tmp_path_factory: pytest.TempPathFactory) -> Path:
    out = tmp_path_factory.mktemp("report-batch") / "descent"
    montecarlo.run(montecarlo.load(DESCENT, root=ROOT), out, workers=4)
    return out


def _packages(names: Sequence[str] = report.DEFAULT_PACKAGES) -> list[plot.Package]:
    return [plot.load_package(plot.package_path(name)) for name in names]


def _case(name: str) -> scenario.Scenario:
    return scenario.load(SCENARIOS / f"{name}.yaml", root=ROOT)


@pytest.fixture(scope="module")
def log_text(logs: dict[str, Path]) -> str:
    packages = _packages([*report.DEFAULT_PACKAGES, str(NOTIONAL)])
    return report.log_report(log.load(logs["elevator_step"]), packages, source="elevator_step.h5")


@pytest.fixture(scope="module")
def scenario_text(logs: dict[str, Path]) -> str:
    return report.log_report(
        log.load(logs["elevator_step"]),
        _packages(),
        source="elevator_step.h5",
        case=_case("elevator_step"),
        case_source="elevator_step.yaml",
    )


@pytest.fixture(scope="module")
def batch_text(batch_dir: Path) -> str:
    return report.batch_report(
        mc_analysis.load_batch(batch_dir),
        source="descent",
        metric=METRIC,
        claims=mc_sizing.load_claims(CLAIMS),
        signals=[ALTITUDE],
    )


# --- R13: a log (C-13) ----------------------------------------------------------------------------


def test_report_log_holds_each_package_figure_as_astra_plot_writes_it(
    logs: dict[str, Path], log_text: str, tmp_path: Path
) -> None:
    names = [*report.DEFAULT_PACKAGES, str(NOTIONAL)]
    for name in names:
        assert cli.main(["plot", str(logs["elevator_step"]), name, "--out", str(tmp_path)]) == 0
    written = {path.stem: path for path in tmp_path.iterdir()}
    figures = inline_figures(log_text)
    expected = [
        "flight-air_data",
        "flight-aero_angles",
        "attitude-euler",
        "attitude-rates",
        "health-health",
        "cruise-longitudinal",
        "cruise-lateral",
    ]
    assert [heading for heading, _k, _svg in figures] == expected
    assert sorted(written) == sorted(expected)
    assert log_text.count("<svg ") == len(expected)
    for heading, k, svg in figures:
        assert denamespaced(svg, k) == file_svg(written[heading]), heading


def _summary_attrs(path: Path) -> dict[str, Any]:
    with h5py.File(path, "r") as file:
        return dict(cast(Any, file)["summary"].attrs)


@pytest.mark.parametrize("name", ["elevator_step", "atmosphere_ceiling_halt"])
def test_report_log_summary_equals_the_log_s_summary(logs: dict[str, Path], name: str) -> None:
    text = report.log_report(log.load(logs[name]), _packages(["health"]), source=name)
    ids = parse(text)
    attrs = _summary_attrs(logs[name])
    with h5py.File(logs[name], "r") as file:
        dt = float(cast(Any, file).attrs["dt"])
    frames_run = int(attrs["frames_run"])
    assert field(ids, "sec-frames-run") == str(frames_run)
    assert field(ids, "sec-duration") == repr(frames_run * dt)
    health = {name: float(value) for name, value in attrs.items() if name.startswith("health.")}
    assert body_rows(ids, "sec-health") == [[k, repr(health[k])] for k in sorted(health)]
    assert len(health) == 5
    check = str(attrs["halt_check"])
    if check == "NONE":
        assert name == "elevator_step"
        assert field(ids, "sec-halt") == "none"
    else:
        assert name == "atmosphere_ceiling_halt"
        assert field(ids, "sec-halt") == (
            f"{check} source={attrs['halt_source']} frame={int(attrs['halt_frame'])} "
            f"vehicle={int(attrs['halt_vehicle'])}"
        )
        assert check == "OUT_OF_DOMAIN"


def test_report_provenance_equals_the_log_s_attributes(
    logs: dict[str, Path], log_text: str
) -> None:
    ids = parse(log_text)
    with h5py.File(logs["elevator_step"], "r") as file:
        root = cast(Any, file)
        attrs = dict(root.attrs)
        plugins = root["plugins"][()]
    assert field(ids, "prov-input") == "elevator_step.h5"
    assert field(ids, "prov-format_version") == str(int(attrs["log_format_version"]))
    assert field(ids, "prov-git_describe") == str(attrs["git_describe"])
    assert field(ids, "prov-core_version") == str(attrs["core_version"])
    assert field(ids, "prov-core_api_version") == str(int(attrs["core_api_version"]))
    assert float(field(ids, "prov-dt")) == float(attrs["dt"])
    expected_rows = [
        [
            value.decode() if isinstance(value, bytes) else str(int(value))
            for value in (record[name] for name in report.PLUGIN_COLUMNS)
        ]
        for record in plugins
    ]
    assert body_rows(ids, "prov-plugins") == expected_rows
    assert len(expected_rows) >= 6
    config = str(attrs["config"])
    # The equality below catches a PyYAML round trip only if the text is not a fixed point of one.
    assert yaml.safe_dump(yaml.safe_load(config)) != config
    assert ids["prov-config"].text() == config


def test_report_escapes_every_input_string(logs: dict[str, Path]) -> None:
    real = log.load(logs["elevator_step"])
    config = "</pre><script>alert(1)</script>&amp; <b>x</b>\nsim: {dt: 0.001}\n"
    plugins = pd.DataFrame(
        {
            "path": ['plugins/a"b<c>&d'],
            "category": ["physical"],
            "name": ["x"],
            "core_api_version": [np.uint64(9)],
            "built_with": ["<i>"],
            "sha256": ["static"],
        }
    )
    synthetic = dataclasses.replace(real, config=config, plugins=plugins)
    text = report.log_report(synthetic, _packages(["health"]), source='in<put>&"')
    ids = parse(text)
    assert ids["prov-config"].text() == config
    assert body_rows(ids, "prov-plugins") == [
        ['plugins/a"b<c>&d', "physical", "x", "9", "<i>", "static"]
    ]
    assert field(ids, "prov-input") == 'in<put>&"'
    assert resource_refs.disallowed(text, REPORT) == []
    assert "<script" not in text


# --- R13: a scenario ------------------------------------------------------------------------------


def _independent_row(
    path: Path, signal: str, kind: str, a: float, b: float, window: Any
) -> list[str]:
    """A check row recomputed from the log's column with numpy: rows, worst value, worst frame,
    result, and the window's frames."""
    run_log = log.load(path)
    table = next(t for t in run_log.rates.values() if signal in t.columns)
    frames = np.asarray(cast(Any, table).index.to_numpy(), dtype=np.int64)
    values = np.asarray(cast(Any, table)[signal].to_numpy(), dtype=np.float64)
    if window == "final":
        chosen = np.array([frames.size - 1])
    else:
        chosen = np.flatnonzero((frames >= window[0]) & (frames < window[1]))
    x = values[chosen]
    excess = np.maximum(a - x, x - b) if kind == "between" else np.abs(x - a) - b
    excess = np.where(np.isnan(excess), np.inf, excess)
    worst = int(np.argmax(excess))
    result = "pass" if excess[worst] <= 0.0 else "FAIL"
    return [str(chosen.size), repr(float(x[worst])), str(int(frames[chosen[worst]])), result]


def test_report_scenario_rows_equal_an_independent_computation(
    logs: dict[str, Path], scenario_text: str
) -> None:
    ids = parse(scenario_text)
    rows = body_rows(ids, "sec-check-rows")
    assert len(rows) == 1
    expected = _independent_row(
        logs["elevator_step"], "v0.derived.q", "between", 1.6e-3, 8.8e-3, (1100, 1500)
    )
    assert rows[0][:4] == [
        "0",
        "v0.derived.q",
        "between [0.0016, 0.0088]",
        "[1.1, 1.5) s (frames 1100..1499)",
    ]
    assert rows[0][4:] == expected
    assert ids["sec-other-assertions"].text().strip() == "none"


def test_report_scenario_final_window_on_a_halting_log(logs: dict[str, Path]) -> None:
    path = logs["atmosphere_ceiling_halt"]
    text = report.log_report(
        log.load(path),
        _packages(["health"]),
        source="halt.h5",
        case=_case("atmosphere_ceiling_halt"),
        case_source="atmosphere_ceiling_halt.yaml",
    )
    rows = body_rows(parse(text), "sec-check-rows")
    expected = _independent_row(path, "v0.derived.altitude", "between", 85999.8, 86000.0, "final")
    assert rows == [
        ["0", "v0.derived.altitude", "between [85999.8, 86000.0]", "final (frame 511)", *expected]
    ]


def test_report_scenario_failing_check_shows_fail_at_verify_s_frame(
    logs: dict[str, Path], tmp_path: Path
) -> None:
    data = yaml.safe_load((SCENARIOS / "elevator_step.yaml").read_text())
    data["checks"][0]["between"]["hi"] = 5.0e-3
    tight = tmp_path / "elevator_step_tight.yaml"
    tight.write_text(yaml.safe_dump(data))
    case = scenario.load(tight, root=ROOT)
    run_log = log.load(logs["elevator_step"])
    failures = scenario.verify(case, run_log)
    assert len(failures) == 1
    frame = re.search(r"at frame (\d+)", failures[0])
    assert frame is not None
    text = report.log_report(run_log, _packages(["health"]), source="x", case=case, case_source="t")
    row = body_rows(parse(text), "sec-check-rows")[0]
    assert row[6] == frame.group(1)
    assert row[7] == "FAIL"
    assert row[4:] == _independent_row(
        logs["elevator_step"], "v0.derived.q", "between", 1.6e-3, 5.0e-3, (1100, 1500)
    )
    assert parse(text)["sec-other-assertions"].text().strip() == "none"


def test_report_scenario_that_does_not_match_the_log_is_refused(logs: dict[str, Path]) -> None:
    run_log = log.load(logs["elevator_step"])
    with pytest.raises(
        report.ReportError, match=r"write 0 .*controls\.elevator.* is not the scenario's event"
    ):
        report.log_report(run_log, _packages(["health"]), source="x", case=_case("aileron_step"))
    with pytest.raises(report.ReportError, match=r"dt 0\.001 s differs from the log's 0\.002 s"):
        report.log_report(
            dataclasses.replace(run_log, dt=0.002),
            _packages(["health"]),
            source="x",
            case=_case("elevator_step"),
        )
    # A write the scenario does not have.
    with pytest.raises(
        report.ReportError, match=r"write 0 .* is not the scenario's event 'no event'"
    ):
        report.log_report(run_log, _packages(["health"]), source="x", case=_case("cruise_hold"))


def _halt_scenario(tmp_path: Path, at: float, *, expect_halt: bool) -> Path:
    """atmosphere_ceiling_halt with one elevator event at ``at`` s (its log has no write; the run
    halts at frame 512)."""
    data = yaml.safe_load((SCENARIOS / "atmosphere_ceiling_halt.yaml").read_text())
    data["config"] = str(ROOT / data["config"])
    data["events"] = [{"at": at, "write": "controls.elevator", "value": 0.0}]
    if not expect_halt:
        del data["expect_halt"]
    path = tmp_path / f"halt_event_{at}.yaml"
    path.write_text(yaml.safe_dump(data))
    return path


def test_report_scenario_event_on_the_halted_frame_must_be_logged(
    logs: dict[str, Path], tmp_path: Path
) -> None:
    # Review A-001: astra.scenario writes a frame's events before that frame's step, so a run that
    # halts at frame 512 wrote its frame-512 events; a log without one is not this scenario's.
    run_log = log.load(logs["atmosphere_ceiling_halt"])
    assert run_log.frames_run == 512
    on_halt = scenario.load(_halt_scenario(tmp_path, 0.512, expect_halt=True), root=ROOT)
    assert on_halt.events[0].frame == 512
    with pytest.raises(report.ReportError, match=r"is at or before frames_run 512"):
        report.log_report(run_log, _packages(["health"]), source="x", case=on_halt)
    out = tmp_path / "r.html"
    path = str(_halt_scenario(tmp_path, 0.512, expect_halt=True))
    argv = ["report", str(logs["atmosphere_ceiling_halt"]), "--scenario", path, "--out", str(out)]
    assert cli.main(argv) == cli.EX_DATAERR
    assert not out.exists()
    # An event after the halted frame was never written: that log can be this scenario's.
    later = scenario.load(_halt_scenario(tmp_path, 0.6, expect_halt=False), root=ROOT)
    text = report.log_report(run_log, _packages(["health"]), source="x", case=later)
    assert "unexpected halt" in parse(text)["sec-other-assertions"].text()


# --- R13: a batch ---------------------------------------------------------------------------------


def test_report_batch_outcomes_equal_the_summary(batch_dir: Path, batch_text: str) -> None:
    summary = pq.read_table(batch_dir / montecarlo.SUMMARY).to_pydict()
    statuses = cast(list[str], summary["status"])
    ids = parse(batch_text)
    assert field(ids, "sec-runs") == str(len(statuses))
    for status in ("ok", "halt", "error"):
        assert field(ids, f"sec-status-{status}") == str(statuses.count(status))
    assert field(ids, "sec-criterion") == "status == halt"
    assert field(ids, "sec-outcome-fail") == str(statuses.count("halt"))
    assert field(ids, "sec-outcome-pass") == str(statuses.count("ok"))
    assert field(ids, "sec-outcome-error") == str(statuses.count("error"))
    assert statuses.count("halt") > 0
    assert statuses.count("error") > 0


def _g(value: float) -> str:
    return "nan" if math.isnan(value) else format(value, ".6g")


def test_report_batch_sizing_rows_equal_mc_sizing_check(batch_dir: Path, batch_text: str) -> None:
    table = mc_sizing.check(mc_analysis.load_batch(batch_dir), mc_sizing.load_claims(CLAIMS))
    ids = parse(batch_text)
    rows = body_rows(ids, "sec-claims")
    assert len(rows) == len(table.rows) == 4
    for row, check in zip(rows, table.rows, strict=True):
        assert row == [
            check.option,
            check.tier,
            check.claim,
            str(check.n),
            str(check.errors),
            _g(check.estimate),
            _g(check.lower),
            _g(check.upper),
            check.verdict if check.verdict is not None else mc_sizing.NOMINAL,
            "none" if check.needed is None else str(check.needed),
            str(check.minimum),
        ]
    assert field(ids, "sec-master-seed") == str(table.seed)
    ranking = next(i for i, check in enumerate(table.rows) if check.parameters)
    parameters = body_rows(ids, f"sec-claim-{ranking + 1}-parameters")
    # Every cell, not only the names (review N-1).
    assert parameters == [
        [
            p.parameter,
            *(_g(v) for v in (p.rho, p.lower, p.upper, p.abs_lower, p.abs_upper)),
            p.note,
        ]
        for p in table.rows[ranking].parameters
    ]
    assert parameters


@pytest.mark.parametrize("metric", [None, METRIC], ids=["no-metric", "metric"])
def test_report_batch_ranking_rows_equal_analyze(batch_dir: Path, metric: str | None) -> None:
    batch = mc_analysis.load_batch(batch_dir)
    analysis = mc_analysis.analyze(batch, None, metric)
    text = report.batch_report(batch, source="descent", metric=metric)
    ids = parse(text)
    families = [("sec-ks", analysis.ks)]
    if metric is not None:
        assert analysis.spearman is not None
        assert analysis.prcc is not None
        families += [("sec-spearman", analysis.spearman), ("sec-prcc", analysis.prcc)]
    else:
        assert "Spearman and PRCC not run" in ids["sec-correlation"].text()
    for ident, ranking in families:
        expected = [
            [
                str(rank),
                row.parameter,
                _g(row.effect),
                format(row.p, ".3e"),
                "reject" if row.rejected else "keep",
            ]
            for rank, row in enumerate(ranking.rows, start=1)
        ]
        if expected:
            assert body_rows(ids, ident) == expected, ident
        else:
            assert ident not in ids
            assert ranking.status in ids["sec-correlation"].text()
        for name, reason in ranking.excluded:
            assert f"{name} ({reason})" in ids["sec-correlation"].text()
    assert analysis.ks.rows, "the descent batch's KS family must rank something"
    rules = [f"fail if {rule.text()}" for rule in analysis.tree.rules()]
    assert [li.text() for li in ids["sec-tree-rules"].find("li")] == rules
    assert rules


def test_report_batch_figures_are_mc_analyze_s_files(
    batch_dir: Path, batch_text: str, tmp_path: Path
) -> None:
    code = cli.main(
        ["mc", "analyze", str(batch_dir), "--signal", ALTITUDE, "--out", str(tmp_path / "f")]
    )
    assert code == 0
    written = {path.stem: path for path in (tmp_path / "f").iterdir()}
    figures = inline_figures(batch_text)
    assert [heading for heading, _k, _svg in figures] == [
        "mc-hist",
        "mc-scatter",
        f"mc-envelope-{ALTITUDE}",
    ]
    assert sorted(written) == sorted(heading for heading, _k, _svg in figures)
    for heading, k, svg in figures:
        assert denamespaced(svg, k) == file_svg(written[heading]), heading


def test_report_batch_without_claims_states_it_and_the_seed(batch_dir: Path) -> None:
    text = report.batch_report(mc_analysis.load_batch(batch_dir), source="descent")
    ids = parse(text)
    assert ids["sec-no-claims"].text() == report.NO_CLAIMS
    assert "sec-claims" not in ids
    assert field(ids, "sec-master-seed") == "606"
    assert field(ids, "prov-seed") == "606"
    assert mc_sizing.PROTOCOL in html.unescape(ids["sec-sizing"].text())


def test_report_batch_provenance_comes_from_the_record_and_the_logs(
    batch_dir: Path, batch_text: str
) -> None:
    ids = parse(batch_text)
    metadata = pq.read_schema(batch_dir / montecarlo.SUMMARY).metadata
    assert ids["prov-spec"].text() == metadata[montecarlo.SPEC_KEY].decode()
    assert ids["prov-config"].text() == metadata[montecarlo.CONFIG_KEY].decode()
    assert field(ids, "prov-config-name") == metadata[montecarlo.CONFIG_NAME_KEY].decode()
    summary = pq.read_table(batch_dir / montecarlo.SUMMARY).to_pydict()
    kept = sorted(
        run
        for run, status in zip(summary["run"], summary["status"], strict=True)
        if status != "error"
    )
    first = batch_dir / montecarlo.log_name(kept[0])
    assert field(ids, "prov-log") == montecarlo.log_name(kept[0])
    with h5py.File(first, "r") as file:
        attrs = dict(cast(Any, file).attrs)
    assert field(ids, "prov-git_describe") == str(attrs["git_describe"])
    assert field(ids, "prov-core_version") == str(attrs["core_version"])
    assert float(field(ids, "prov-dt")) == float(attrs["dt"])


def test_report_batch_with_a_log_from_another_build_is_refused(
    batch_dir: Path, tmp_path: Path
) -> None:
    copy = tmp_path / "batch"
    shutil.copytree(batch_dir, copy)
    summary = pq.read_table(copy / montecarlo.SUMMARY).to_pydict()
    kept = sorted(
        run
        for run, status in zip(summary["run"], summary["status"], strict=True)
        if status != "error"
    )
    with h5py.File(copy / montecarlo.log_name(kept[2]), "r+") as file:
        cast(Any, file).attrs["git_describe"] = "v0.0.0-other-build"
    with pytest.raises(
        report.ReportError,
        match=(
            rf"run {kept[2]}: its log's git_describe 'v0.0.0-other-build' differs from run "
            rf"{kept[0]}'s"
        ),
    ):
        report.batch_report(mc_analysis.load_batch(copy), source="copy")


def test_report_batch_with_a_kept_run_s_log_missing_is_refused(
    batch_dir: Path, tmp_path: Path
) -> None:
    copy = tmp_path / "batch"
    shutil.copytree(batch_dir, copy)
    summary = pq.read_table(copy / montecarlo.SUMMARY).to_pydict()
    ok = [
        run for run, status in zip(summary["run"], summary["status"], strict=True) if status == "ok"
    ]
    (copy / montecarlo.log_name(ok[1])).unlink()
    with pytest.raises(
        report.ReportError, match=rf"run {ok[1]}: its log .* is missing \(an ok row keeps one\)"
    ):
        report.batch_report(mc_analysis.load_batch(copy), source="copy")


# --- R13: resources and ids (C-11, C-12) ----------------------------------------------------------


def test_report_every_kind_is_self_contained_with_unique_ids(
    log_text: str, scenario_text: str, batch_text: str, logs: dict[str, Path]
) -> None:
    for text in (log_text, scenario_text, batch_text):
        assert resource_refs.disallowed(text, REPORT) == []
        assert resource_refs.id_problems(text) == []
    assert len(inline_figures(log_text)) >= 5
    # Without the namespacing, two figures repeat ids: the check above would catch it.
    with plot.pinned_style("health", "health"):
        package = _packages(["health"])[0]
        figure = plot.resolve(package, log.load(logs["elevator_step"]))[0]
        raw = _PROLOG.sub("", plot.svg_bytes(plot.draw_matplotlib(figure)).decode())
    assert any("repeated" in problem for problem in resource_refs.id_problems(raw + raw))


def test_report_inline_svg_leaves_label_comments_as_drawn() -> None:
    # Review N-2: matplotlib writes each label into a comment; one that reads like an id or a
    # reference is not namespaced.
    label = 'id="sample" xlink:href="#data" url(#temperature)'
    x = np.array([0.0, 1.0])
    figure = plot.Figure("f", "t", [plot.Panel("p", "y", "m", [plot.Series(label, x, x)])])
    with plot.pinned_style("pkg", "f"):
        raw = plot.svg_bytes(plot.draw_matplotlib(figure)).decode()
    inline = report.inline_svg(raw.encode(), 3)
    comments = re.findall(r"<!--.*?-->", raw, re.DOTALL)
    assert any(label in comment for comment in comments)
    assert re.findall(r"<!--.*?-->", inline, re.DOTALL) == comments
    assert resource_refs.id_problems(inline) == []
    assert 'id="fig3-' in inline


def test_report_refuses_a_log_that_is_not_complete_and_published(
    logs: dict[str, Path], tmp_path: Path
) -> None:
    # Review G-001: allow_incomplete=True can return a dropped run's log, or a complete .partial
    # that was never published; neither is reported as a finished run (ADR-0027).
    partial = tmp_path / "elevator_step.h5.partial"
    shutil.copyfile(logs["elevator_step"], partial)
    unpublished = log.load(partial, allow_incomplete=True)
    assert unpublished.state == log.COMPLETE
    assert not unpublished.published
    with pytest.raises(report.ReportError, match=r"not published \(a \.partial name\)"):
        report.log_report(unpublished, _packages(["health"]), source="p")
    dropped = dataclasses.replace(
        log.load(logs["elevator_step"]), state=log.DESTRUCTOR, complete=False
    )
    with pytest.raises(report.ReportError, match=r"finalization state is destructor"):
        report.log_report(dropped, _packages(["health"]), source="d")


def test_report_does_not_import_yaml() -> None:
    source = Path(cast(str, report.__file__)).read_text(encoding="utf-8")
    assert "yaml" not in vars(report)
    assert re.search(r"^\s*(import|from)\s+yaml\b", source, re.MULTILINE) is None


# --- numbers --------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("value", "expected"),
    [
        (np.float64(0.1), "0.1"),
        (np.float32(0.5), "0.5"),
        (np.int64(-3), "-3"),
        (np.uint64(9), "9"),
        (0.001, "0.001"),
        (7, "7"),
        (math.nan, "nan"),
    ],
)
def test_report_numbers_are_python_reprs(value: object, expected: str) -> None:
    assert report.number(value) == expected


@pytest.mark.parametrize("value", [True, np.bool_(True), "1", None])
def test_report_numbers_refuse_what_is_not_a_number(value: object) -> None:
    with pytest.raises(TypeError, match="not a number to report"):
        report.number(value)


def test_report_numbers_hold_no_numpy_repr(
    log_text: str, scenario_text: str, batch_text: str
) -> None:
    assert repr(np.float64(1.0)) == "np.float64(1.0)"  # what an unconverted value would write
    for text in (log_text, scenario_text, batch_text):
        assert "np." not in text
        assert "numpy" not in text


# --- determinism (C-12) ---------------------------------------------------------------------------

_USER_RC = "font.family: serif\nfont.size: 17\nlines.linewidth: 4\naxes.grid: False\n"


def _astra_report(args: Sequence[str], env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(ASTRA), "report", *args],
        env=env,
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=600,
        check=False,
    )


@pytest.mark.parametrize("kind", ["log", "scenario", "batch"])
def test_report_is_byte_identical_across_processes(
    kind: str, logs: dict[str, Path], batch_dir: Path, tmp_path: Path
) -> None:
    if kind == "batch":
        args = [str(batch_dir), "--claims", str(CLAIMS), "--metric", METRIC, "--signal", ALTITUDE]
    else:
        args = [str(logs["elevator_step"]), "--package", "flight", "--package", str(NOTIONAL)]
        if kind == "scenario":
            args += ["--scenario", "tests/scenarios/elevator_step.yaml"]
    config_dir = tmp_path / "mplconfig"
    config_dir.mkdir()
    base = {
        k: v for k, v in os.environ.items() if k not in ("SOURCE_DATE_EPOCH", "MATPLOTLIBRC", "TZ")
    }
    base["MPLCONFIGDIR"] = str(config_dir)
    first = _astra_report(
        [*args, "--out", str(tmp_path / "a.html")], base | {"PYTHONHASHSEED": "1", "TZ": "UTC"}
    )
    assert first.returncode == 0, first.stderr
    assert first.stdout == f"{tmp_path / 'a.html'}\n"
    assert list(config_dir.glob("fontlist-*.json")), "the first process built no font cache"
    (config_dir / "matplotlibrc").write_text(_USER_RC, encoding="utf-8")
    warm = base | {"PYTHONHASHSEED": "2", "SOURCE_DATE_EPOCH": "1700000000", "TZ": "Asia/Kolkata"}
    second = _astra_report([*args, "--out", str(tmp_path / "b.html")], warm)
    assert second.returncode == 0, second.stderr
    a = (tmp_path / "a.html").read_bytes()
    assert a == (tmp_path / "b.html").read_bytes()
    text = a.decode("utf-8")
    assert re.search(r"\d{4}-\d{2}-\d{2}", text) is None
    assert re.search(r"\d{2}:\d{2}:\d{2}", text) is None
    assert "<dc:date>" not in text
    assert str(tmp_path) not in text.replace(str(logs["elevator_step"]), "").replace(
        str(batch_dir), ""
    )
    assert "a.html" not in text
    assert resource_refs.disallowed(text, REPORT) == []


# --- R15: astra report (CLI) ----------------------------------------------------------------------


def test_report_cli_default_packages_are_the_module_s() -> None:
    assert cli._REPORT_PACKAGES == report.DEFAULT_PACKAGES  # pyright: ignore[reportPrivateUsage]


@pytest.mark.parametrize(
    "args",
    [
        ["{log}"],
        ["{log}", "--out", "{out}", "--criterion", "status == halt"],
        ["{log}", "--out", "{out}", "--claims", "x.yaml"],
        ["{batch}", "--out", "{out}", "--package", "flight"],
        ["{batch}", "--out", "{out}", "--scenario", "s.yaml"],
        ["{batch}", "--out", "{out}", "--q", "2"],
        ["--out", "{out}"],
    ],
    ids=[
        "no-out",
        "criterion-on-log",
        "claims-on-log",
        "package-on-batch",
        "scenario-on-batch",
        "q-2",
        "no-input",
    ],
)
def test_report_cli_usage_errors_exit_64(
    args: list[str], logs: dict[str, Path], batch_dir: Path, tmp_path: Path
) -> None:
    out = tmp_path / "r.html"
    argv = [a.format(log=logs["elevator_step"], batch=batch_dir, out=out) for a in args]
    assert cli.main(["report", *argv]) == cli.EX_USAGE
    assert not out.exists()


@pytest.mark.parametrize(
    "case",
    [
        "missing-log",
        "not-a-log",
        "no-summary",
        "refused-package",
        "refused-criterion",
        "refused-claims",
        "mismatched-scenario",
        "inside-the-batch",
        "unknown-signal",
    ],
)
def test_report_cli_refusals_exit_65(
    case: str,
    logs: dict[str, Path],
    batch_dir: Path,
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    out = tmp_path / "r.html"
    elevator = str(logs["elevator_step"])
    bad = tmp_path / "bad.yaml"
    bad.write_text("claims: []\n" if case == "refused-claims" else "figures: []\n")
    not_log = tmp_path / "not.h5"
    not_log.write_text("not hdf5\n")
    (tmp_path / "empty").mkdir()
    argv = {
        "missing-log": [str(tmp_path / "nothing.h5"), "--out", str(out)],
        "not-a-log": [str(not_log), "--out", str(out)],
        "no-summary": [str(tmp_path / "empty"), "--out", str(out)],
        "refused-package": [elevator, "--package", str(bad), "--out", str(out)],
        "refused-criterion": [str(batch_dir), "--criterion", "nope > 1", "--out", str(out)],
        "refused-claims": [str(batch_dir), "--claims", str(bad), "--out", str(out)],
        "mismatched-scenario": [
            elevator,
            "--scenario",
            str(SCENARIOS / "aileron_step.yaml"),
            "--out",
            str(out),
        ],
        "inside-the-batch": [str(batch_dir), "--out", str(batch_dir / "r.html")],
        "unknown-signal": [str(batch_dir), "--signal", "v0.no.such", "--out", str(out)],
    }[case]
    cwd = Path.cwd()
    os.chdir(ROOT)
    try:
        assert cli.main(["report", *argv]) == cli.EX_DATAERR
    finally:
        os.chdir(cwd)
    assert not out.exists()
    assert not (batch_dir / "r.html").exists()
    assert "astra report: refused:" in capsys.readouterr().err


def test_report_cli_existing_output_is_left_untouched(
    logs: dict[str, Path], tmp_path: Path
) -> None:
    out = tmp_path / "r.html"
    out.write_text("mine\n")
    assert cli.main(["report", str(logs["elevator_step"]), "--out", str(out)]) == cli.EX_DATAERR
    assert out.read_text() == "mine\n"
    with pytest.raises(report.ReportOutputExistsError):
        report.write("x", out)
    assert out.read_text() == "mine\n"


@pytest.mark.skipif(os.geteuid() == 0, reason="root ignores the directory's permissions")
def test_report_cli_unwritable_directory_exits_74(logs: dict[str, Path], tmp_path: Path) -> None:
    out_dir = tmp_path / "ro"
    out_dir.mkdir()
    out_dir.chmod(0o500)
    try:
        assert (
            cli.main(["report", str(logs["elevator_step"]), "--out", str(out_dir / "r.html")])
            == cli.EX_IOERR
        )
        assert list(out_dir.iterdir()) == []
    finally:
        out_dir.chmod(0o700)


def test_report_cli_a_halt_and_a_failed_check_exit_0(logs: dict[str, Path], tmp_path: Path) -> None:
    halt = tmp_path / "halt.html"
    assert cli.main(["report", str(logs["atmosphere_ceiling_halt"]), "--out", str(halt)]) == 0
    assert "OUT_OF_DOMAIN" in halt.read_text()
    data = yaml.safe_load((SCENARIOS / "elevator_step.yaml").read_text())
    data["config"] = str(ROOT / data["config"])
    data["checks"][0]["between"]["hi"] = 5.0e-3
    tight = tmp_path / "tight.yaml"
    tight.write_text(yaml.safe_dump(data))
    failed = tmp_path / "failed.html"
    assert (
        cli.main(
            ["report", str(logs["elevator_step"]), "--scenario", str(tight), "--out", str(failed)]
        )
        == 0
    )
    assert ">FAIL</td>" in failed.read_text()
