"""What in an HTML or SVG document would make a viewer load a resource from outside the file (M6
PR 5, C-11; docs/plans/m6-pr5-plots.md R3b). A test helper, so PR 8's report test can reuse it.

``external_loads(text)`` parses the document with ``html.parser`` and returns one line per construct
that loads something other than a ``data:`` URI or a ``#fragment``:

- an attribute that names a resource to fetch: ``src``, ``href``, ``xlink:href``, ``srcset``,
  ``poster``, ``data``, ``action``, ``formaction``, ``background``;
- an element that exists to load or point elsewhere: ``<link>``, ``<base>``, ``<iframe>``,
  ``<frame>``, ``<embed>``, ``<object>``, ``<applet>``, ``<portal>``, and
  ``<meta http-equiv=refresh>``;
- CSS ``url(...)`` or ``@import`` in a ``<style>`` element or a ``style`` attribute.

Identifiers that are not loads are allowed: ``xmlns`` namespace URIs, the SVG DOCTYPE's DTD URL and
RDF metadata such as ``rdf:resource``. Text inside ``<script>`` is not scanned (plotly.js holds URL
strings it never fetches for these figures); what an inline script might fetch at run time cannot be
judged statically, which is the stated limit of this check.
"""

from __future__ import annotations

import html.parser
import re

_URL_ATTRIBUTES = frozenset(
    {"src", "href", "xlink:href", "srcset", "poster", "data", "action", "formaction", "background"}
)
_LOADING_ELEMENTS = frozenset(
    {"link", "base", "iframe", "frame", "embed", "object", "applet", "portal"}
)
_CSS_URL = re.compile(r"url\(\s*(['\"]?)(.*?)\1\s*\)", re.IGNORECASE | re.DOTALL)
_CSS_IMPORT = re.compile(r"@import", re.IGNORECASE)


def _local(value: str) -> bool:
    stripped = value.strip()
    return stripped.startswith("#") or stripped.lower().startswith("data:")


def _css_loads(css: str, where: str) -> list[str]:
    found = [f"{where}: @import" for _ in _CSS_IMPORT.finditer(css)]
    found += [
        f"{where}: url({m.group(2)})" for m in _CSS_URL.finditer(css) if not _local(m.group(2))
    ]
    return found


class _Scanner(html.parser.HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.found: list[str] = []
        self._in_style = False

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        self._element(tag, attrs)
        self._in_style = tag == "style"

    def handle_startendtag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        self._element(tag, attrs)

    def handle_endtag(self, tag: str) -> None:
        if tag == "style":
            self._in_style = False

    def handle_data(self, data: str) -> None:
        if self._in_style:
            self.found += _css_loads(data, "<style>")

    def _element(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag in _LOADING_ELEMENTS:
            self.found.append(f"<{tag}>")
        values = {name: value or "" for name, value in attrs}
        if tag == "meta" and values.get("http-equiv", "").lower() == "refresh":
            self.found.append("<meta http-equiv=refresh>")
        for name, value in values.items():
            if name in _URL_ATTRIBUTES and not _local(value):
                self.found.append(f"<{tag} {name}={value!r}>")
            if name == "style":
                self.found += _css_loads(value, f"<{tag} style>")


def external_loads(text: str) -> list[str]:
    """Every construct in ``text`` that loads a resource from outside the document (module
    docstring); empty when the document is self-contained."""
    scanner = _Scanner()
    scanner.feed(text)
    scanner.close()
    return scanner.found
