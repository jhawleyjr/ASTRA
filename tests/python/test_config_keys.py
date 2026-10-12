"""The config-key reference (M7 PR 14, docs/plans/m7-pr14-config-key-reference.md; R17's
reference half and R19): every plugin's key block, read by scripts/config_keys.py, against the keys
the core itself reports consumed (Config.unknown_keys()'s complement) by every tracked config.

Scope. The blocks document the keys each plugin under plugins/ and aircraft/ reads from its own
block of a config, the map that names it with `plugin:`. The core's own keys are out of scope: sim,
log, initial, vehicle outside the model blocks, and transport outside its plugin's keys. So are the
keys of the test plugins under tests/plugins/: a block naming one is skipped and printed
(`pytest -rP` shows the lines), never failed.

Tracked configs. Every git-tracked *.yaml or *.yml whose document has top-level sim and vehicle,
read through the core. A tracked YAML the core cannot load fails the test, naming the file; nothing
is skipped as "not a config". tests/python/config_keys_fixtures/ holds ordinary tracked configs
that set the keys no other config sets. This module keeps no list of configs and no list of keys. A
config that from_config refuses must appear in EXPECTED_REFUSALS, a table of refusals: config -> the
key its ConfigError names.

Why the withheld build. A plugin's block also holds keys the core reads itself: `plugin` and
`divisor` in every block, and the transport's sim-owned keys in `transport`. The core reads all of
them before it loads the block's plugin: resolve_model_spec resolves every model block before any
plugin loads, and LockstepExchange's constructor reads the transport's keys before it loads the
transport plugin. So each config is built a second time per plugin it names, with a plugin
directory that holds every library but that plugin's. That build stops at the load with
PluginNotFoundError, after the core's in-block reads and before any read by the plugin. In each of
the plugin's blocks, its keys are the leaves the full build consumed and the withheld build did
not. The core's in-block reads are subtracted by construction, with no list of core keys. A core
read made after a plugin loads would be charged to that plugin and fail here as an undocumented
key: loud, never silent.
"""

import subprocess
import sys
import types
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

import pytest

from astra import _core

ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "scripts" / "config_keys.py"
IN_SCOPE = ("plugins/", "aircraft/")
TEST_PLUGINS = "tests/plugins/"

# Pre-registered (the plan's §4.2): every tracked config that Simulation.from_config refuses, and
# the key its ConfigError names. Any other refusal fails the test.
EXPECTED_REFUSALS = {
    # Its first TODO zero, by design (test_trim_round_trip pins the same refusal).
    "aircraft/_template/configs/cruise.yaml": "vehicle.models.0.wing_area_m2",
    # astra.lockstep supplies the shm segment name at run time.
    "aircraft/notional_transport/configs/cruise_light_turbulence.yaml": "transport.segment",
    "aircraft/notional_transport/configs/cruise_moderate_turbulence.yaml": "transport.segment",
    # A malformed name that the shm plugin reads, then refuses (the fixture's own comment).
    "tests/python/config_keys_fixtures/shm.yaml": "transport.segment",
}
SHM_FIXTURE = "tests/python/config_keys_fixtures/shm.yaml"
SHM_FIXTURE_SEGMENT = Path("/dev/shm/config_keys_fixture")


def _load_generator() -> types.ModuleType:
    """scripts/config_keys.py, compiled and executed rather than imported: an import would write
    scripts/__pycache__/ (lessons 2026-10-01 and 2026-10-07)."""
    module = types.ModuleType("config_keys")
    module.__file__ = str(GENERATOR)
    exec(compile(GENERATOR.read_text(encoding="utf-8"), str(GENERATOR), "exec"), module.__dict__)
    return module


config_keys = _load_generator()


def _build(config: "_core.Config", plugin_dir: Path) -> Exception | None:
    """Simulation.from_config's outcome: None when it builds, else what it raised. Every outcome is
    classified by the caller, so nothing here decides whether a refusal is expected."""
    try:
        _core.Simulation.from_config(config, plugin_dir)
    except Exception as error:
        return error
    return None


def _names_key(error: Exception | None, key: str) -> bool:
    return isinstance(error, _core.ConfigError) and f"at '{key}'" in str(error)


@dataclass
class Survey:
    configs: list[str] = field(default_factory=list)
    problems: list[str] = field(default_factory=list)  # loads, refusals, withheld builds
    consumed: defaultdict[str, set[str]] = field(default_factory=lambda: defaultdict(set))
    reached: set[str] = field(default_factory=set)
    skipped: list[str] = field(default_factory=list)


def _tracked_yaml() -> list[str]:
    listed = subprocess.run(
        ["git", "ls-files", "-z", "--", "*.yaml", "*.yml"],
        cwd=ROOT,
        capture_output=True,
        check=True,
    ).stdout.decode()
    return sorted(name for name in listed.split("\0") if name)


def _owner(leaf: str, blocks: list[str]) -> str | None:
    """The deepest block (a dotted prefix) that holds `leaf`, if any."""
    owners = [block for block in blocks if leaf.startswith(block + ".")]
    return max(owners, key=len) if owners else None


def _survey_config(
    rel: str, plugin_dir: Path, withheld_dir: "WithheldDirs", survey: Survey
) -> None:
    path = str(ROOT / rel)
    full = _core.Config.from_file(path)
    leaves = set(full.unknown_keys())  # nothing read yet: every leaf
    outcome = _build(full, plugin_dir)
    consumed_full = leaves - set(full.unknown_keys())

    declared = EXPECTED_REFUSALS.get(rel)
    if declared is None and outcome is not None:
        survey.problems.append(
            f"{rel}: from_config refused it: {type(outcome).__name__}: {outcome}"
        )
        return
    if declared is not None and not _names_key(outcome, declared):
        survey.problems.append(
            f"{rel}: declared to refuse with a ConfigError at '{declared}', got "
            f"{type(outcome).__name__ if outcome else 'a build'}: {outcome}"
        )
        return

    probe = _core.Config.from_file(path)  # reads here never touch `full`'s read-state
    blocks: dict[str, str] = {}
    named: set[str] = set()
    for leaf in sorted(leaves):
        try:
            value = probe.text(leaf)
        except _core.ConfigError:
            continue  # an empty map or sequence: not a scalar
        if leaf.endswith(".plugin"):
            blocks[leaf.removesuffix(".plugin")] = value
        if (
            _core.PluginLoader.is_valid_plugin_path(value)
            and (plugin_dir / _core.PluginLoader.library_name(value)).is_file()
        ):
            named.add(value)

    for block, plugin in sorted(blocks.items()):
        if plugin.startswith(TEST_PLUGINS):
            survey.skipped.append(f"{rel}: {block} -> {plugin}")

    for plugin in sorted(p for p in named if p.startswith(IN_SCOPE)):
        withheld = _core.Config.from_file(path)
        stopped = _build(withheld, withheld_dir(plugin))
        if isinstance(stopped, _core.PluginNotFoundError) and f"'{plugin}'" in str(stopped):
            survey.reached.add(plugin)
        elif not (declared is not None and _names_key(stopped, declared)):
            survey.problems.append(
                f"{rel}: the build without {plugin}'s library raised "
                f"{type(stopped).__name__ if stopped else 'nothing'}: {stopped}; expected "
                f"PluginNotFoundError naming it, or the declared refusal"
            )
            continue
        only_full = consumed_full - (leaves - set(withheld.unknown_keys()))
        mine = [block for block, owner in blocks.items() if owner == plugin]
        for leaf in only_full:
            block = _owner(leaf, list(blocks))
            if block in mine:
                survey.consumed[plugin].add(config_keys.normalize(leaf[len(block) + 1 :]))


class WithheldDirs:
    """One plugin directory per withheld plugin: a symlink to every library in the installed plugin
    directory but that plugin's own."""

    def __init__(self, plugin_dir: Path, base: Path) -> None:
        self._libraries = sorted(plugin_dir.glob("libastra_*.so"))
        self._base = base
        self._made: dict[str, Path] = {}

    def __call__(self, plugin: str) -> Path:
        if plugin not in self._made:
            directory = self._base / plugin.replace("/", "__")
            directory.mkdir()
            withheld = _core.PluginLoader.library_name(plugin)
            for library in self._libraries:
                if library.name != withheld:
                    (directory / library.name).symlink_to(library)
            self._made[plugin] = directory
        return self._made[plugin]


@pytest.fixture(scope="module")
def survey(tmp_path_factory: pytest.TempPathFactory) -> Survey:
    assert not _core.PluginLoader.is_static(), "the withheld build needs a plugin directory"
    plugin_dir = Path(_core.default_plugin_dir())
    withheld_dir = WithheldDirs(plugin_dir, tmp_path_factory.mktemp("withheld"))
    result = Survey()
    with pytest.MonkeyPatch.context() as patch:
        patch.chdir(tmp_path_factory.mktemp("cwd"))  # a relative path a config names lands here
        for rel in _tracked_yaml():
            try:
                probe = _core.Config.from_file(str(ROOT / rel))
            except Exception as error:
                result.problems.append(f"{rel}: the core cannot load it: {error}")
                continue
            if probe.is_map() and probe.has("sim") and probe.has("vehicle"):
                result.configs.append(rel)
                _survey_config(rel, plugin_dir, withheld_dir, result)
    return result


@pytest.fixture(scope="module")
def documented() -> dict[str, set[str]]:
    blocks, problems = config_keys.collect_blocks(ROOT)
    assert problems == []
    return {block.plugin: {row.key for row in block.rows} for block in blocks}


def test_config_keys_every_tracked_config_loads_and_builds_or_refuses_as_declared(
    survey: Survey,
) -> None:
    missing = sorted(set(EXPECTED_REFUSALS) - set(survey.configs))
    assert missing == [], f"declared refusals that are not tracked configs: {missing}"
    assert survey.problems == []


def test_config_keys_every_documented_plugin_is_reached_by_a_tracked_config(
    survey: Survey, documented: dict[str, set[str]]
) -> None:
    assert sorted(set(documented) - survey.reached) == []


def test_config_keys_documented_keys_equal_the_keys_tracked_configs_consume(
    survey: Survey, documented: dict[str, set[str]]
) -> None:
    for line in survey.skipped:
        print(f"skipped (tests/plugins): {line}")
    differences = []
    for plugin in sorted(set(documented) | set(survey.consumed)):
        if plugin not in documented:
            differences.append(f"{plugin}: consumed keys, but no key block")
            continue
        consumed = survey.consumed.get(plugin, set())
        for key in sorted(consumed - documented[plugin]):
            differences.append(f"{plugin}: '{key}' is consumed but not documented")
        for key in sorted(documented[plugin] - consumed):
            differences.append(
                f"{plugin}: '{key}' is documented but no tracked config's build reads it"
            )
    assert differences == []


def test_config_keys_the_shm_fixture_refuses_at_its_segment_and_creates_none() -> None:
    assert not SHM_FIXTURE_SEGMENT.exists(), f"{SHM_FIXTURE_SEGMENT} exists before the run"
    config = _core.Config.from_file(str(ROOT / SHM_FIXTURE))
    outcome = _build(config, Path(_core.default_plugin_dir()))
    assert _names_key(outcome, "transport.segment"), outcome
    assert "must match astra_" in str(outcome)
    assert not SHM_FIXTURE_SEGMENT.exists(), f"{SHM_FIXTURE_SEGMENT} appeared"


# --- the generator itself -------------------------------------------------------------------------


def _run_generator(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-I", "-B", str(GENERATOR), *args],
        capture_output=True,
        text=True,
        check=False,
    )


def _status() -> str:
    return subprocess.run(
        ["git", "status", "--porcelain", "--ignored", "--", "plugins", "aircraft", "scripts"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=True,
    ).stdout


def test_config_keys_check_mode_passes_on_the_repository_and_writes_nothing() -> None:
    before = _status()
    result = _run_generator("--check", "--root", str(ROOT))
    assert (result.returncode, result.stderr) == (0, ""), result.stderr
    assert result.stdout.startswith("config-keys: OK (")
    assert _status() == before


def _copy_plugin_sources(destination: Path) -> None:
    problems: list[str] = []
    for _plugin, source in config_keys.discover_plugins(ROOT, problems):
        for original in (source.parent / "CMakeLists.txt", source):
            copy = destination / original.relative_to(ROOT)
            copy.parent.mkdir(parents=True, exist_ok=True)
            copy.write_bytes(original.read_bytes())
    assert problems == []


def _files(root: Path) -> set[str]:
    return {path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()}


def test_config_keys_write_mode_creates_only_the_reference_and_is_deterministic(
    tmp_path: Path,
) -> None:
    _copy_plugin_sources(tmp_path)
    sources = _files(tmp_path)
    first = _run_generator("--root", str(tmp_path))
    assert (first.returncode, first.stderr) == (0, "")
    assert _files(tmp_path) - sources == {config_keys.REFERENCE}
    text = (tmp_path / config_keys.REFERENCE).read_bytes()
    second = _run_generator("--root", str(tmp_path))
    assert (second.returncode, second.stderr) == (0, "")
    assert (tmp_path / config_keys.REFERENCE).read_bytes() == text
    assert _files(tmp_path) - sources == {config_keys.REFERENCE}


_ROW = "//   a | number | m | 0 | A meaning."


@pytest.mark.parametrize(
    ("source", "line", "reason"),
    [
        pytest.param("// A plugin.\n#include <x>\n", 1, "no key block", id="no block"),
        pytest.param(
            "// Config keys:\n//   none\n//\n// Config keys:\n//   none\n",
            4,
            "a second key block",
            id="two blocks",
        ),
        pytest.param(
            "// A plugin.\n#include <x>\n// Config keys:\n//   none\n",
            3,
            "must sit in the file's leading comment",
            id="outside the leading comment",
        ),
        pytest.param(
            "// Config keys:\n//   a | number | m | 0\n", 2, "five non-empty cells", id="four cells"
        ),
        pytest.param(
            "// Config keys:\n//   a | number |  | 0 | x\n",
            2,
            "five non-empty cells",
            id="empty cell",
        ),
        pytest.param(
            "// Config keys:\n//   a | float | m | 0 | x\n", 2, "type 'float'", id="unknown type"
        ),
        pytest.param(
            "// Config keys:\n//   Mass | number | kg | 0 | x\n", 2, "key 'Mass'", id="uppercase"
        ),
        pytest.param(
            "// Config keys:\n//   a..b | number | m | 0 | x\n", 2, "key 'a..b'", id="empty segment"
        ),
        pytest.param(
            "// Config keys:\n//   0 | number | m | 0 | x\n", 2, "key '0'", id="index as key"
        ),
        pytest.param(f"// Config keys:\n{_ROW}\n{_ROW}\n", 3, "duplicate key 'a'", id="duplicate"),
        pytest.param(
            "// Config keys:\n//   more meaning\n",
            2,
            "a continuation line",
            id="continuation first",
        ),
        pytest.param(f"// Config keys:\n//   none\n{_ROW}\n", 3, "'none'", id="none then a row"),
        pytest.param(f"// Config keys:\n{_ROW}\n//   none\n", 3, "'none'", id="a row then none"),
        pytest.param("// Config keys:\n//\n", 1, "an empty key block", id="empty block"),
    ],
)
def test_config_keys_parsing_rule_refuses_a_malformed_block(
    tmp_path: Path, source: str, line: int, reason: str
) -> None:
    plugin = tmp_path / "plugins" / "kind" / "thing"
    plugin.mkdir(parents=True)
    (plugin / "CMakeLists.txt").write_text("astra_add_plugin(SOURCES thing.cpp)\n")
    (plugin / "thing.cpp").write_text(source)
    blocks, problems = config_keys.collect_blocks(tmp_path)
    assert blocks == []
    assert any(
        problem.startswith(f"plugins/kind/thing/thing.cpp:{line}: ") and reason in problem
        for problem in problems
    ), problems


def test_config_keys_parsing_rule_reads_rows_continuations_and_none(tmp_path: Path) -> None:
    for name, source in {
        "a": "// Config keys:\n"
        "//   points[].altitude | number | m | required | First line,\n"
        "//     second line.\n"
        "//   flag | boolean | - | false | Off.\n"
        "//\n"
        "// Prose after the block.\n",
        "b": "// Header.\n// Config keys:\n//   none\n",
    }.items():
        plugin = tmp_path / "plugins" / "kind" / name
        plugin.mkdir(parents=True)
        (plugin / "CMakeLists.txt").write_text(
            f"astra_add_plugin(SOURCES {name}.cpp {name}2.cpp)\n"
        )
        (plugin / f"{name}.cpp").write_text(source + "#include <x>\n")
    blocks, problems = config_keys.collect_blocks(tmp_path)
    assert problems == []
    assert [(block.plugin, block.source) for block in blocks] == [
        ("plugins/kind/a", "plugins/kind/a/a.cpp"),
        ("plugins/kind/b", "plugins/kind/b/b.cpp"),
    ]
    assert [row[:5] for row in blocks[0].rows] == [
        ("points[].altitude", "number", "m", "required", "First line, second line."),
        ("flag", "boolean", "-", "false", "Off."),
    ]
    assert blocks[1].rows == ()


@pytest.mark.parametrize(
    ("leaf", "key"),
    [
        ("g", "g"),
        ("points.0.altitude", "points[].altitude"),
        ("peer.schedule.0.throttle.1", "peer.schedule[].throttle[]"),
        ("a.0.1", "a[][]"),
        ("peer.commands.throttle.1", "peer.commands.throttle[]"),
    ],
)
def test_config_keys_sequence_paths_normalize_one_way(leaf: str, key: str) -> None:
    assert config_keys.normalize(leaf) == key
    assert config_keys.KEY_RE.fullmatch(key)


def test_config_keys_a_path_cannot_start_with_an_index() -> None:
    with pytest.raises(ValueError, match="starts with a sequence index"):
        config_keys.normalize("0.a")


def test_config_keys_render_escapes_markdown_outside_code_spans() -> None:
    assert config_keys.escape("Ixz = integral(x*z*dm) | `a|b*c` <pid>_x") == (
        "Ixz = integral(x\\*z\\*dm) \\| `a\\|b*c` \\<pid>\\_x"
    )
