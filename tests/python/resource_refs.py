"""Self-contained documents: an allow-list check of HTML and SVG (M6 PR 5's R3b, C-11; M6 PR 8's R13
and C-12; docs/plans/m6-pr8-report.md, "The resource check", D9). A test helper.

``disallowed(text, profile)`` parses ``text`` with ``html.parser`` and returns one line for every
element, attribute, attribute value, declaration, processing instruction or CSS function that its
profile does not allow. Anything not listed is flagged, so a loading construct nobody thought of is
caught too, not only the ones someone enumerated. Names are compared lowercased, as ``html.parser``
reports them. Comments are ignored.

Profiles (``SVG``, ``REPORT``, ``PLOTLY_PAGE``):

- ``SVG``, a matplotlib SVG file: the elements and attributes matplotlib's SVG backend writes
  (surveyed on 3.11.2), its XML declaration and the SVG 1.1 DOCTYPE.
- ``REPORT``, an ``astra.report`` page: ``SVG``'s elements and attributes (the figures are inlined)
  plus the report's own HTML, and ``<!DOCTYPE html>``.
- ``PLOTLY_PAGE``, ``astra.plot``'s interactive page: the skeleton, ``div`` and an attribute-free
  ``script``. Script text is not scanned: what inline JavaScript fetches at run time cannot be
  judged statically (the stated limit, M6 PR 5's R3b).

Value rules:

- *fragment* (``href``, ``xlink:href``): ``#`` and at least one character, no whitespace. Nothing
  else: no ``data:`` URI either, since nothing ASTRA writes uses one.
- *CSS* (``style`` attributes, ``<style>`` text, ``clip-path``): the only function is ``url``, whose
  argument must be a fragment; any other ``<name>(`` is flagged by name (``image-set``, ``var``,
  ...), and so is any ``@`` (an at-rule such as ``@import``) or backslash (an escape could spell a
  function name).
- *transform*: a list of ``matrix``, ``translate``, ``scale``, ``rotate``, ``skewX`` and ``skewY``
  calls over numbers.
- a few attributes take one exact value (the namespace URIs, ``charset="utf-8"``, ...).

``html.parser`` is not a browser's parser, so four constructs it reads differently are flagged in
every profile (nothing ASTRA writes uses one):

- a self-closing ``<style/>`` or ``<script/>``: an HTML parser ignores the slash, so the text after
  it is live CSS or script that ``html.parser`` would pass over as data;
- a character reference (``&``) or markup (``<``) in ``<style>`` text: inside an SVG, inline or a
  standalone file, a browser decodes the references and parses the tags as elements, while
  ``html.parser`` hands the text over raw;
- a comment a browser closes early: ``<!-->`` and ``<!--->`` close at once and ``--!>`` ends a
  comment, while ``html.parser`` may read on to the next ``-->`` and hide the markup between;
- a ``<title>`` inside an ``<svg>``: ``html.parser`` reads a title's content as text, but an SVG
  title is an HTML integration point whose content a browser parses as live HTML. The HTML
  document's own ``<title>``, outside every SVG, is text in a browser too and stays allowed.

Every other element whose content ``html.parser`` hides (``textarea``, ``xmp``, ``iframe``,
``noembed``, ``noframes``, and ``script`` outside the plotly page) is outside every profile, so it
is flagged as an element.

``id_problems(text)`` returns every ``id`` used twice in the document and every reference (``#X`` in
``href`` or ``xlink:href``, ``url(#X)`` anywhere) that does not resolve. A reference inside an
``<svg>`` (a figure) must resolve inside that same figure, and one outside every figure must resolve
outside every figure, so a report that inlines several SVGs cannot lean on another figure's ids.
"""

from __future__ import annotations

import dataclasses
import html.parser
import re
from collections.abc import Callable, Mapping

# --- value rules ----------------------------------------------------------------------------------

_FRAGMENT = re.compile(r"#[^\s]+")
_CSS_FUNCTION = re.compile(r"([A-Za-z_-][A-Za-z0-9_-]*)\s*\(([^)]*)\)?")
_TRANSFORM = re.compile(
    r"\s*(?:(?:matrix|translate|scale|rotate|skewx|skewy)\s*\(\s*[-+0-9.eE,\s]*\)\s*,?\s*)*",
    re.IGNORECASE,
)
_WHITESPACE = re.compile(r"\s+")


def _fragment(value: str) -> list[str]:
    return [] if _FRAGMENT.fullmatch(value) else [f"not a #fragment: {value!r}"]


def css_problems(css: str) -> list[str]:
    """Everything in ``css`` outside the CSS rule (module docstring, Value rules)."""
    problems = [f"CSS at-rule ({css!r})" for _ in range(css.count("@"))]
    problems += [f"CSS escape ({css!r})" for _ in range(css.count("\\"))]
    for match in _CSS_FUNCTION.finditer(css):
        name = match.group(1).lower()
        if name != "url":
            problems.append(f"CSS function {name}()")
            continue
        argument = match.group(2).strip().strip("'\"")
        if not _FRAGMENT.fullmatch(argument):
            problems.append(f"CSS url() not a #fragment: {argument!r}")
    return problems


def _transform(value: str) -> list[str]:
    return [] if _TRANSFORM.fullmatch(value) else [f"not a transform list: {value!r}"]


def _exactly(*allowed: str) -> Callable[[str], list[str]]:
    def check(value: str) -> list[str]:
        return [] if value in allowed else [f"value {value!r} is not {' or '.join(allowed)}"]

    return check


def _any(_value: str) -> list[str]:
    return []


# --- profiles -------------------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class Rule:
    """An allowed attribute: the elements it may sit on (``None``: any allowed element) and its
    value rule."""

    on: frozenset[str] | None
    value: Callable[[str], list[str]]


@dataclasses.dataclass(frozen=True)
class Profile:
    name: str
    elements: frozenset[str]
    attributes: Mapping[str, Rule]
    declarations: frozenset[str]  # whitespace collapsed, as html.parser reports them
    instructions: frozenset[str]
    scripts_unscanned: bool  # PLOTLY_PAGE: <script> text is not scanned


_SVG_ELEMENTS = frozenset(
    {
        "svg",
        "metadata",
        "rdf:rdf",
        "cc:work",
        "dc:type",
        "dc:format",
        "dc:creator",
        "cc:agent",
        "dc:title",
        "defs",
        "style",
        "g",
        "path",
        "use",
        "clippath",
        "rect",
    }
)
_SVG_ATTRIBUTES: dict[str, Rule] = {
    "id": Rule(None, _any),
    "d": Rule(None, _any),
    "x": Rule(None, _any),
    "y": Rule(None, _any),
    "width": Rule(None, _any),
    "height": Rule(None, _any),
    "viewbox": Rule(None, _any),
    "version": Rule(None, _any),
    "type": Rule(frozenset({"style"}), _exactly("text/css")),
    "style": Rule(None, css_problems),
    "transform": Rule(None, _transform),
    "clip-path": Rule(None, css_problems),
    "xlink:href": Rule(None, _fragment),
    "rdf:resource": Rule(None, _exactly("http://purl.org/dc/dcmitype/StillImage")),
    "xmlns": Rule(None, _exactly("http://www.w3.org/2000/svg")),
    "xmlns:xlink": Rule(None, _exactly("http://www.w3.org/1999/xlink")),
    "xmlns:rdf": Rule(None, _exactly("http://www.w3.org/1999/02/22-rdf-syntax-ns#")),
    "xmlns:cc": Rule(None, _exactly("http://creativecommons.org/ns#")),
    "xmlns:dc": Rule(None, _exactly("http://purl.org/dc/elements/1.1/")),
}
_SVG_DOCTYPE = 'DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN" "http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd"'
_HTML_DOCTYPE = "DOCTYPE html"

SVG = Profile(
    "SVG",
    _SVG_ELEMENTS,
    _SVG_ATTRIBUTES,
    frozenset({_SVG_DOCTYPE}),
    frozenset({'xml version="1.0" encoding="utf-8" standalone="no"?'}),
    scripts_unscanned=False,
)

REPORT = Profile(
    "REPORT",
    _SVG_ELEMENTS
    | {
        "html",
        "head",
        "meta",
        "title",
        "body",
        "main",
        "section",
        "h1",
        "h2",
        "h3",
        "p",
        "ul",
        "li",
        "a",
        "table",
        "caption",
        "thead",
        "tbody",
        "tr",
        "th",
        "td",
        "pre",
        "code",
    },
    _SVG_ATTRIBUTES
    | {
        "lang": Rule(frozenset({"html"}), _any),
        "charset": Rule(frozenset({"meta"}), _exactly("utf-8")),
        "class": Rule(None, _any),
        "href": Rule(frozenset({"a"}), _fragment),
    },
    frozenset({_HTML_DOCTYPE}),
    frozenset(),
    scripts_unscanned=False,
)

PLOTLY_PAGE = Profile(
    "PLOTLY_PAGE",
    frozenset({"html", "head", "meta", "title", "body", "div", "script"}),
    {
        "charset": Rule(frozenset({"meta"}), _exactly("utf-8")),
        "id": Rule(frozenset({"div"}), _any),
        "class": Rule(frozenset({"div"}), _any),
        "style": Rule(frozenset({"div"}), css_problems),
    },
    frozenset({_HTML_DOCTYPE}),
    frozenset(),
    scripts_unscanned=True,
)


# --- the check ------------------------------------------------------------------------------------


class _Scanner(html.parser.HTMLParser):
    def __init__(self, profile: Profile) -> None:
        super().__init__(convert_charrefs=True)
        self.profile = profile
        self.found: list[str] = []
        self._text_of: str | None = None  # "style" or "script" while inside one
        self._svg_depth = 0

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        self._element(tag, attrs)
        if tag in ("style", "script"):
            self._text_of = tag
        if tag == "svg":
            self._svg_depth += 1
        if tag == "title" and self._svg_depth > 0:
            # html.parser reads a title's content as text (RCDATA), but an SVG <title> is an HTML
            # integration point: a browser parses its content as live HTML (review V-001).
            # matplotlib writes none; the HTML document's own title, outside every SVG, stays
            # allowed.
            self.found.append("<title> inside an <svg>: a browser parses its content as HTML")

    def handle_startendtag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        self._element(tag, attrs)
        if tag in ("style", "script"):
            # An HTML parser ignores the slash, so the text that follows is a live stylesheet or
            # script that html.parser would read as plain data (review A-002). Nothing ASTRA writes
            # uses one, so it is flagged in every profile.
            self.found.append(f"self-closing <{tag}/>: its following text would be live {tag}")

    def handle_endtag(self, tag: str) -> None:
        if tag == self._text_of:
            self._text_of = None
        if tag == "svg" and self._svg_depth > 0:
            self._svg_depth -= 1

    def handle_data(self, data: str) -> None:
        if self._text_of == "style":
            self.found += [f"<style>: {problem}" for problem in css_problems(data)]
            # html.parser hands style text over raw, but inside an SVG (inline in HTML, or a
            # standalone XML file) a browser decodes its character references and parses its tags
            # as elements (Joe's review of #95), so what it runs cannot be read from the raw text.
            if "&" in data:
                self.found.append(f"<style>: a character reference ({data!r})")
            if "<" in data:
                self.found.append(f"<style>: markup inside the style text ({data!r})")

    def handle_comment(self, data: str) -> None:
        # A browser closes "<!-->" and "<!--->" at once and ends a comment at "--!>"; html.parser
        # may read on to the next "-->", so the markup between would be live yet unseen (Joe's
        # review of #95).
        if data.startswith((">", "->")) or "--!>" in data:
            self.found.append(f"comment a browser closes early ({data!r})")

    def handle_decl(self, decl: str) -> None:
        text = _WHITESPACE.sub(" ", decl).strip()
        if text not in self.profile.declarations:
            self.found.append(f"declaration <!{text}>")

    def unknown_decl(self, data: str) -> None:
        self.found.append(f"declaration <![{data}]>")

    def handle_pi(self, data: str) -> None:
        if data not in self.profile.instructions:
            self.found.append(f"processing instruction <?{data}>")

    def _element(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        profile = self.profile
        if tag not in profile.elements:
            self.found.append(f"element <{tag}>")
        for name, value in attrs:
            rule = profile.attributes.get(name)
            if rule is None or (rule.on is not None and tag not in rule.on):
                self.found.append(f"<{tag}> attribute {name}={value!r}")
                continue
            self.found += [f"<{tag} {name}>: {problem}" for problem in rule.value(value or "")]


def disallowed(text: str, profile: Profile) -> list[str]:
    """Everything in ``text`` its profile does not allow (module docstring); empty when the document
    holds only allowed constructs."""
    scanner = _Scanner(profile)
    scanner.feed(text)
    scanner.close()
    return scanner.found


# --- ids ------------------------------------------------------------------------------------------

_URL_REFERENCE = re.compile(r"url\(\s*['\"]?#([^)'\"\s]+)['\"]?\s*\)", re.IGNORECASE)


class _Ids(html.parser.HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.ids: list[tuple[str, int | None]] = []  # (id, figure) in document order
        self.references: list[tuple[str, int | None, str]] = []  # (target, figure, where)
        self._depth = 0  # <svg> nesting
        self._figures = 0
        self._figure: int | None = None
        self._in_style = False

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag == "svg":
            if self._depth == 0:
                self._figures += 1
                self._figure = self._figures
            self._depth += 1
        self._attributes(tag, attrs)
        self._in_style = tag == "style"

    def handle_startendtag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        self._attributes(tag, attrs)

    def handle_endtag(self, tag: str) -> None:
        if tag == "style":
            self._in_style = False
        if tag == "svg" and self._depth > 0:
            self._depth -= 1
            if self._depth == 0:
                self._figure = None

    def handle_data(self, data: str) -> None:
        if self._in_style:
            for match in _URL_REFERENCE.finditer(data):
                self.references.append((match.group(1), self._figure, "<style>"))

    def _attributes(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        for name, value in attrs:
            value = value or ""
            if name == "id":
                self.ids.append((value, self._figure))
            elif name in ("href", "xlink:href") and value.startswith("#"):
                self.references.append((value[1:], self._figure, f"<{tag} {name}>"))
            for match in _URL_REFERENCE.finditer(value):
                self.references.append((match.group(1), self._figure, f"<{tag} {name}>"))


def id_problems(text: str) -> list[str]:
    """Every repeated id and every unresolved or cross-figure reference (module docstring)."""
    parser = _Ids()
    parser.feed(text)
    parser.close()
    problems: list[str] = []
    seen: dict[str, int | None] = {}
    for name, figure in parser.ids:
        if name in seen:
            problems.append(f"id {name!r} repeated")
        else:
            seen[name] = figure
    for target, figure, where in parser.references:
        if target not in seen:
            problems.append(f"{where}: #{target} does not resolve")
        elif seen[target] != figure:
            inside = f"figure {figure}" if figure is not None else "outside every figure"
            problems.append(f"{where}: #{target}, used {inside}, resolves elsewhere")
    return problems
