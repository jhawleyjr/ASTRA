"""astra._yaml_route: a config read as a dict through PyYAML, as the core reads it (M6 PR 11,
docs/plans/m6-pr11-yaml-route.md). Private: ``astra.montecarlo``, ``astra.scenario``,
``astra.trim`` and ``astra.lockstep`` read their configs through ``load``.

Those tools edit a config as a dict and hand it to the core through ``Config.from_dict``, which
emits YAML text for yaml-cpp. PyYAML reads YAML 1.1 and yaml-cpp does not, so a scalar can reach
the core as something the file does not say: ``off`` becomes ``False`` and reaches the core as
``false``, and ``010`` becomes 8 where the core reads 10.0 (lessons 2026-09-28, 2026-09-30).
``load`` reads the text both ways, ``yaml.safe_load`` and ``Config.from_string``, and compares
every leaf of the core's reading with the core's reading of the dict (D2):

- A leaf's reading is its node kind (scalar, map, sequence or null) plus what ``number``,
  ``integer``, ``boolean`` and ``text`` each return, or that each refuses.
- A leaf differs when the kinds differ, the numbers differ (bitwise; NaN equals NaN), the
  booleans differ, the core reads an integer and the dict gives another or none, or the core
  reads no number and the texts differ. So a number written two ways (``1.0e+3`` reaching the
  core as ``1000``) does not differ, and ``True`` reaching it as ``true`` does.
- A differing scalar leaf is repaired: the dict gets the core's own text there, which the core
  reads back exactly (yaml-cpp ignores quoting when it converts a scalar). So does a scalar that
  ``from_dict`` cannot carry (a date, bytes, an integer outside int64). The comparison then runs
  again, and any leaf that still differs is refused.
- Refused, through the caller's ``error``, naming the key: a null (``from_dict`` cannot carry
  ``None``), a key that is not text, a key the core cannot address (it contains ``.``, or it is
  a digit string in a map), a set or a pair list, a duplicate key (the core reads the first,
  PyYAML the last), a merge key (yaml-cpp 0.8 does not merge ``<<``), and a document PyYAML
  reads but the core does not.

Two residuals stay as they were (D2): an integer-typed key written ``1000.0`` reaches the core as
``1000``, which ``integer`` accepts and ``astra run`` refuses; and a key read with ``text`` keeps
the dict's spelling of a numeric-looking value (``0.10`` reaches it as ``0.10000000000000001``).
A config with no differing leaf comes back exactly as ``yaml.safe_load`` read it.
"""

from __future__ import annotations

import contextlib
import math
import struct
from collections import Counter
from collections.abc import Callable
from typing import Any, cast

import yaml

from astra import _core

ErrorType = Callable[[str], Exception]

_INT64 = range(-(2**63), 2**63)
_GETTERS = ("number", "integer", "boolean", "text")

# The four getter outcomes (None when refused) and the node kind.
_Reading = tuple[float | None, int | None, bool | None, str | None, str]


def load(text: str, origin: str, *, error: ErrorType) -> Any:
    """``text`` read by ``yaml.safe_load``, with every leaf the core reads differently repaired to
    the core's own text, or refused through ``error`` (module docstring). ``yaml.YAMLError``
    propagates, so each caller keeps its message for a file PyYAML cannot read; a document that
    is not a map comes back as PyYAML read it, for the caller to refuse. PyYAML's constructors
    raise plain ``ValueError`` or ``KeyError`` for some scalars the core reads as text
    (``0000-01-01``, ``!!bool warn``); that is refused through ``error``, naming the key (M6 PR
    11 review A-001)."""
    try:
        raw: Any = yaml.safe_load(text)
    except yaml.YAMLError:
        raise
    except Exception as failure:  # a constructor's own error: no YAMLError wraps it
        raise error(_construction_refusal(text, origin, failure)) from None
    if not isinstance(raw, dict):
        return raw
    data = cast(dict[str, Any], raw)
    try:
        core = _core.Config.from_string(text, origin)
    except _core.ConfigError as failure:
        raise error(f"{origin}: PyYAML reads it, but the core does not: {failure}") from None
    leaves = core.unknown_keys()
    for path, count in Counter(leaves).items():
        if count > 1:
            raise error(
                f"{origin}: {path}: a duplicate key: the core reads the first, PyYAML the last"
            )
    walked: list[str] = []
    _walk(core, data, "", walked, origin, error)
    seen = set(walked)
    missing = [path for path in leaves if path not in seen]
    if missing:
        raise error(f"{origin}: {missing[0]}: the core reads this key and PyYAML does not")
    routed = _core.Config.from_dict(data, origin=origin)
    differing = [path for path in leaves if _differs(_reading(core, path), _reading(routed, path))]
    if not differing:
        return data
    for path in differing:
        _set(data, path, core.text(path))
    routed = _core.Config.from_dict(data, origin=origin)
    for path in leaves:
        before, after = _reading(core, path), _reading(routed, path)
        if _differs(before, after):
            raise error(
                f"{origin}: {path}: the core reads {_describe(before)}, but the dict reaches it "
                f"as {_describe(after)}"
            )
    return data


def _construction_refusal(text: str, origin: str, failure: Exception) -> str:
    """The refusal of a document PyYAML parsed but could not construct: the first scalar that
    fails alone (by its dotted path, from ``yaml.compose``), and the core's reading of it."""
    root = cast("yaml.Node | None", yaml.compose(text, Loader=yaml.SafeLoader))  # pyright: ignore[reportUnknownMemberType]
    path = _failing_path(root, "", yaml.SafeLoader(""))
    reading = ""
    if path is not None:
        with contextlib.suppress(_core.ConfigError):
            reading = f"; the core reads {_core.Config.from_string(text, origin).text(path)!r}"
    where = path if path is not None else "the document"
    return (
        f"{origin}: {where}: PyYAML cannot construct this value "
        f"({type(failure).__name__}: {failure}){reading}"
    )


def _failing_path(node: yaml.Node | None, path: str, loader: yaml.SafeLoader) -> str | None:
    """The dotted path of the first node whose construction raises, or ``None``."""
    if isinstance(node, yaml.MappingNode):
        for key, value in node.value:
            name = key.value if isinstance(key, yaml.ScalarNode) else "?"
            child = _join(path, str(name))
            if isinstance(key, yaml.ScalarNode) and _raises(key, loader):
                return child
            found = _failing_path(value, child, loader)
            if found is not None:
                return found
        return None
    if isinstance(node, yaml.SequenceNode):
        for index, item in enumerate(node.value):
            found = _failing_path(item, _join(path, str(index)), loader)
            if found is not None:
                return found
        return None
    if isinstance(node, yaml.ScalarNode) and _raises(node, loader):
        return path
    return None


def _raises(node: yaml.Node, loader: yaml.SafeLoader) -> bool:
    try:
        loader.construct_object(node, deep=True)  # pyright: ignore[reportUnknownMemberType]
    except Exception:  # any failure locates the key; the caller already holds the error
        return True
    return False


def _join(base: str, segment: str) -> str:
    return segment if not base else f"{base}.{segment}"


def _walk(
    core: _core.Config,
    value: dict[Any, Any] | list[Any],
    path: str,
    walked: list[str],
    origin: str,
    error: ErrorType,
) -> None:
    """Checks ``value`` (PyYAML's reading at ``path``) against the core's node there, collects
    every leaf path, and replaces each scalar ``from_dict`` cannot carry with the core's text."""
    where = path or "the top level"
    if isinstance(value, dict):
        mapping = value
        if not mapping:
            walked.append(path)
        for key in list(mapping):
            if not isinstance(key, str):
                raise error(
                    f"{origin}: {where}: PyYAML reads a key as {key!r} "
                    f"({_type_name(key)}); the core reads every key as text"
                )
            child = _join(path, key)
            if "." in key or not core.has(child):
                hint = ""
                if core.has(_join(path, "<<")):
                    hint = (
                        " (a merge key: the core reads '<<' as a key, and yaml-cpp does not merge)"
                    )
                elif "." in key:
                    hint = " (a key containing '.' cannot be addressed by a dotted path)"
                raise error(f"{origin}: {child}: PyYAML reads this key and the core does not{hint}")
            replacement = _walk_child(core, mapping[key], child, walked, origin, error)
            if replacement is not None:
                mapping[key] = replacement
    else:
        items = value
        if not items:
            walked.append(path)
        for index, item in enumerate(items):
            child = _join(path, str(index))
            if not core.has(child):
                raise error(f"{origin}: {child}: PyYAML reads this item and the core does not")
            replacement = _walk_child(core, item, child, walked, origin, error)
            if replacement is not None:
                items[index] = replacement


def _walk_child(
    core: _core.Config, value: Any, path: str, walked: list[str], origin: str, error: ErrorType
) -> str | None:
    """Walks one child; returns the core's text when that child must be replaced by it."""
    node = core.at(path)
    name = _type_name(cast(object, value))
    if isinstance(value, dict | list):
        if not (node.is_map() if isinstance(value, dict) else node.is_sequence()):
            raise error(f"{origin}: {path}: the core reads {_kind(node)}, PyYAML a {name}")
        _walk(core, cast("dict[Any, Any] | list[Any]", value), path, walked, origin, error)
        return None
    walked.append(path)
    if value is None:
        raise error(
            f"{origin}: {path}: a null, which the core reads as null and PyYAML as None; a "
            "Python tool cannot pass None on, so give it a value or remove the key"
        )
    if isinstance(value, set | tuple) or not node.is_scalar():
        raise error(f"{origin}: {path}: the core reads {_kind(node)}, PyYAML a {name}")
    carried = isinstance(value, str | bool | float) or (isinstance(value, int) and value in _INT64)
    return None if carried else core.text(path)


def _type_name(value: object) -> str:
    return type(value).__name__


def _kind(node: _core.Config) -> str:
    if node.is_scalar():
        return "scalar"
    if node.is_map():
        return "map"
    return "sequence" if node.is_sequence() else "null"


def _reading(config: _core.Config, path: str) -> _Reading:
    outcomes: list[Any] = []
    for getter in _GETTERS:
        try:
            outcomes.append(getattr(config, getter)(path))
        except _core.ConfigError:
            outcomes.append(None)
    number, integer, boolean, text = outcomes
    return number, integer, boolean, text, _kind(config.at(path))


def _same_number(a: float | None, b: float | None) -> bool:
    if a is None or b is None:
        return a is b
    if math.isnan(a) and math.isnan(b):
        return True
    return struct.pack("<d", a) == struct.pack("<d", b)


def _differs(core: _Reading, routed: _Reading) -> bool:
    """D2: the dict's reading by the core differs from the core's reading of the file."""
    number, integer, boolean, text, kind = core
    r_number, r_integer, r_boolean, r_text, r_kind = routed
    return (
        kind != r_kind
        or not _same_number(number, r_number)
        or boolean != r_boolean
        or (integer is not None and integer != r_integer)
        or (number is None and text != r_text)
    )


def _describe(reading: _Reading) -> str:
    number, integer, boolean, text, kind = reading
    if kind != "scalar":
        return f"a {kind}"
    return f"{text!r} (number {number!r}, integer {integer!r}, boolean {boolean!r})"


def _set(data: dict[str, Any], path: str, text: str) -> None:
    """Sets the leaf at ``path`` (every key on it is text with no '.', so ``_walk`` checked)."""
    node: Any = data
    segments = path.split(".")
    for segment in segments[:-1]:
        node = cast(Any, node[int(segment)] if isinstance(node, list) else node[segment])
    last = segments[-1]
    if isinstance(node, list):
        cast(list[Any], node)[int(last)] = text
    else:
        cast(dict[str, Any], node)[last] = text
