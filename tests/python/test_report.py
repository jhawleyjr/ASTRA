"""M6 PR 8: the run report, ``astra.report`` and ``astra report`` (docs/plans/m6-pr8-report.md;
M6 plan R13, R15's report case, C-11 to C-13). Every test is named ``test_report_*``, so
``.venv/bin/pytest tests/python -k report`` selects each one.

Also here: the allow-list check (``resource_refs``, D9) that R3b (``test_plot.py``) and R13
share."""

from __future__ import annotations

import pytest

import resource_refs
from resource_refs import PLOTLY_PAGE, REPORT, SVG, Profile

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
