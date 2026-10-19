"""M7b's oracle (b): the port pattern, proven on M4's autopilot
(docs/plans/m7b-pr3-autopilot-peer.md; ADR-0029 Decision 8, ADR-0034 Decision 8).

The notional transport's sim-side autopilot peer (``aircraft/notional_transport/sim/transport/
autopilot``), a C++23 second implementation of M4's law run in-process, must be bitwise equal to
``embedded/autopilot`` flown over the shm transport through ``astra.lockstep``, on ADR-0029 Decision
8's objects, at both Dryden levels of M4's acceptance scenario (seed 42, 100,000 frames).

- R6 (§6): the objects O1 to O16, each bitwise, with only Decision 8's exclusions.
- R7 (§5): the floating-point flags of the peer's, the port's, the variant's and the two C++ tests'
  translation units in every preset and in the embedded-standalone configure, read from
  ``compile_commands.json`` and from the driver's effective arguments.
- R8 (§9): the four porting controls of ``tests/plugins/autopilot_peer_variant``, each first
  breaking equality at its registered record, each inside M4's bands. Each one's precondition is
  §9's exact replay of both command expressions on the shm run's recorded requests, and its results
  are the literals in REGISTERED below, recorded in §9 from the reference alone before any control
  ran.
- R9 and Q8 (§7, §10): the configs' drift, the six law parameters' parse equality, ``astra run``'s
  refusal, and the independence of the two implementations (§3).

Every run is a ``lockstep_child.py`` process (test_lockstep.py's placement rule), on one build
(``build/python``) and one worktree state. Every constant here that the plan pre-registers cites its
section (ADR-0034 Decision 6). Reports go to ``$ASTRA_AUTOPILOT_PEER_REPORT_DIR`` when set, else to
pytest's tmp directory.
"""

from __future__ import annotations

import ctypes
import json
import math
import os
import re
import shlex
import struct
import subprocess
import sys
import time
from collections import Counter
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import h5py  # pyright: ignore[reportMissingTypeStubs]
import numpy as np
import pytest
import yaml

from astra import _core, _yaml_route
from astra import lockstep as ls
from test_lockstep import (
    ALTITUDE_BAND_M,
    FULL_RUN_TIMEOUT_S,
    HEADING_BAND_DEG,
    PR_SET_CHILD_SUBREAPER,
    SETPOINTS,
    Host,
    Reaper,
    _executable,
    euler_321,
)

ROOT = Path(__file__).resolve().parents[2]
CONFIGS = ROOT / "aircraft/notional_transport/configs"
LEVELS = ("light", "moderate")
PEER_PLUGIN = "aircraft/notional_transport/sim/transport/autopilot"
VARIANT_PLUGIN = "tests/plugins/autopilot_peer_variant"
# §9's four controls.
DEFECTS = ("float_elevator_trim", "output_before_integral", "float_pitch_rate", "mode_word_zero")
FRAMES = 100000  # §6
EXCHANGES = 10000
STATES = 100001
REPLY_FIELDS = (
    "magic",
    "version",
    "frame",
    "elevator",
    "aileron",
    "rudder",
    "throttle",
    "mode_flags",
    "fault_flags",
)
READ_FIELDS = ("altitude", "vertical_speed", "theta", "q", "psi", "phi", "p", "tas", "r")
WIND_INVALID = 0x10  # icd/astra_icd.h ASTRA_ICD_STATUS_WIND_INVALID


def m4_config(level: str) -> Path:
    return CONFIGS / f"cruise_{level}_turbulence.yaml"


def peer_config(level: str) -> Path:
    return CONFIGS / f"cruise_{level}_turbulence_autopilot_peer.yaml"


def base_load(path: Path) -> Any:
    """The text of every scalar, as written (PyYAML's BaseLoader reads every scalar as a string)."""
    return yaml.load(path.read_text(encoding="utf-8"), Loader=yaml.BaseLoader)


# --- the children ---------------------------------------------------------------------------------


@pytest.fixture(scope="module")
def reaper() -> Iterator[Reaper]:
    """test_lockstep.py's subreaper, for this module's children (the same rule, E3)."""
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0:
        pytest.fail(f"prctl(PR_SET_CHILD_SUBREAPER) failed: errno {ctypes.get_errno()}")
    ls.remove_stale_segments()
    the_reaper = Reaper()
    try:
        yield the_reaper
    finally:
        for pid in sorted(the_reaper.pending):
            the_reaper.collect(pid, 5.0)
        libc.prctl(PR_SET_CHILD_SUBREAPER, 0, 0, 0, 0)
        ls.remove_stale_segments()


@pytest.fixture(scope="module")
def report_dir(tmp_path_factory: pytest.TempPathFactory) -> Path:
    value = os.environ.get("ASTRA_AUTOPILOT_PEER_REPORT_DIR")
    path = Path(value) if value else tmp_path_factory.mktemp("autopilot_peer")
    path.mkdir(parents=True, exist_ok=True)
    return path


def worktree_state() -> dict[str, str]:
    """O16: the commit and the working tree's status (plan §16, P-02)."""

    def git(*args: str) -> str:
        return subprocess.run(
            ["git", *args], cwd=ROOT, capture_output=True, text=True, check=True
        ).stdout

    return {"head": git("rev-parse", "HEAD").strip(), "status": git("status", "--porcelain")}


@dataclass
class ChildRun:
    name: str
    out: Path
    result: dict[str, Any]
    data: bytes
    recording: ls.Recording

    @property
    def log_path(self) -> Path:
        return self.out / "run.h5"


class PeerRuns:
    """Each run once per module, on first use, so ``-k`` selects only what a test needs."""

    def __init__(self, reaper: Reaper, report_dir: Path) -> None:
        self.reaper = reaper
        self.report_dir = report_dir
        self.autopilot = _executable("ASTRA_AUTOPILOT", "embedded/autopilot/astra_autopilot")
        self.echo_peer = _executable("ASTRA_ECHO_PEER", "tests/lockstep/astra_echo_peer")
        self.cache: dict[str, ChildRun] = {}
        self.worktree_before: dict[str, str] | None = None

    def _run(self, name: str, scenario: str, config: Path, *extra: str) -> ChildRun:
        if name not in self.cache:
            if self.worktree_before is None:
                self.worktree_before = worktree_state()
            out = self.report_dir / name
            out.mkdir(parents=True, exist_ok=True)
            args = [
                scenario,
                "--config",
                str(config),
                "--autopilot",
                str(self.autopilot),
                "--echo-peer",
                str(self.echo_peer),
                "--out",
                str(out),
                *extra,
            ]
            result = Host(self.reaper, args, FULL_RUN_TIMEOUT_S).finish()
            data = (out / "recorder.bin").read_bytes()
            self.cache[name] = ChildRun(name, out, result, data, ls.parse_recording(data))
        return self.cache[name]

    def shm(self, level: str) -> ChildRun:
        """§6: M4's config, the real autopilot over shm, the trim from the config, a log added."""
        return self._run(f"shm_{level}", "run", m4_config(level), "--trim", "config", "--log")

    def inprocess(self, level: str) -> ChildRun:
        """§6: the peer config, the shipped peer in-process, the same log block."""
        return self._run(f"inprocess_{level}", "inprocess", peer_config(level), "--log")

    def repeat(self) -> ChildRun:
        """O14: the light in-process run again, recorder only."""
        return self._run("inprocess_light_repeat", "inprocess", peer_config("light"))

    def control(self, defect: str, level: str) -> ChildRun:
        """§9: the variant with one defect, recorder only."""
        return self._run(
            f"control_{defect}_{level}", "inprocess", peer_config(level), "--defect", defect
        )


@pytest.fixture(scope="module")
def runs(reaper: Reaper, report_dir: Path) -> PeerRuns:
    return PeerRuns(reaper, report_dir)


def write_json(path: Path, payload: Any) -> None:
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


# --- §10: the configs, and R9 ---------------------------------------------------------------------


@pytest.mark.parametrize("level", LEVELS)
def test_autopilot_peer_configs_copy_m4_with_only_the_transport_block_replaced(level: str) -> None:
    m4 = base_load(m4_config(level))
    mine = base_load(peer_config(level))
    m4_transport = m4.pop("transport")
    transport = mine.pop("transport")
    assert json.dumps(mine) == json.dumps(m4), (
        "outside transport, the copy differs from M4's config"
    )
    assert list(transport) == ["plugin", "divisor", "commands", "limits", "peer"]
    assert transport["plugin"] == PEER_PLUGIN
    for key in ("divisor", "commands", "limits"):
        assert json.dumps(transport[key]) == json.dumps(m4_transport[key]), key
    assert list(transport["peer"]) == ["setpoints", "trim"]
    assert list(transport["peer"]["setpoints"]) == ["altitude", "airspeed", "heading"]
    assert list(transport["peer"]["trim"]) == ["elevator", "throttle", "theta"]


@pytest.mark.parametrize("level", LEVELS)
def test_autopilot_peer_configs_exit_65_under_astra_run(level: str, tmp_path: Path) -> None:
    astra = Path(sys.executable).parent / "astra"  # test_cli.py's ASTRA
    path = peer_config(level)
    result = subprocess.run(
        [str(astra), "run", str(path)],
        cwd=tmp_path,
        capture_output=True,
        text=True,
        timeout=300,
        check=False,
    )
    assert result.returncode == 65, result.stderr
    assert result.stdout == ""
    assert result.stderr == (
        f"astra run: config error: config '{path}' at 'transport': a transport block is "
        "refused until M10 (closed loop)\n"
    )
    assert list(tmp_path.iterdir()) == []


# --- §7: Q8's one trim result, and parse equality ------------------------------------------------

PARAMETER_SIGNALS = {
    ("setpoints", "altitude"): "transport.peer.altitude_setpoint",
    ("setpoints", "airspeed"): "transport.peer.airspeed_setpoint",
    ("setpoints", "heading"): "transport.peer.heading_setpoint",
    ("trim", "elevator"): "transport.peer.elevator_trim",
    ("trim", "throttle"): "transport.peer.throttle_trim",
    ("trim", "theta"): "transport.peer.theta_trim",
}


def strtod(text: str) -> float:
    """glibc's strtod, the function embedded/autopilot/cli.cpp's parse_finite calls."""
    libc = ctypes.CDLL(None)
    libc.strtod.restype = ctypes.c_double
    libc.strtod.argtypes = [ctypes.c_char_p, ctypes.c_void_p]
    return float(libc.strtod(text.encode(), None))


def peer_signals(config: _core.Config) -> dict[str, float]:
    sim = _core.Simulation.from_config(config, _core.default_plugin_dir())
    values: dict[str, float] = {}
    for name in PARAMETER_SIGNALS.values():
        value = sim.registry.find(name)
        assert value is not None, name
        values[name] = float(value)
    return values


@pytest.mark.parametrize("level", LEVELS)
def test_autopilot_peer_law_parameters_parse_to_the_same_doubles(level: str) -> None:
    mine = base_load(peer_config(level))
    m4 = base_load(m4_config(level))
    peer = mine["transport"]["peer"]
    # 1. Text: written once (Q8), the same characters as the configs' own literals.
    for config in (mine, m4):
        controls = config["vehicle"]["initial_controls"]["controls"]
        assert peer["trim"]["elevator"] == controls["elevator"]
        assert peer["trim"]["throttle"] == controls["throttle"]
        assert peer["trim"]["theta"] == config["initial"]["state"]["euler_nb"][1]
    setpoints = {
        "altitude": SETPOINTS.altitude_m,
        "airspeed": SETPOINTS.airspeed_mps,
        "heading": SETPOINTS.heading_rad,
    }
    for key, value in setpoints.items():
        assert float(peer["setpoints"][key]).hex() == value.hex(), key
    texts = {(group, key): peer[group][key] for group, key in PARAMETER_SIGNALS}
    # 2. The autopilot's parse: strtod on the repr the driver passes (lockstep.py:359-387).
    for (group, key), text in texts.items():
        assert repr(float(text)) == text, (group, key)
        assert strtod(repr(float(text))).hex() == float(text).hex(), (group, key)
    # 3. The peer's parse, on both Config routes.
    path = peer_config(level)
    routes = {
        "from_file": _core.Config.from_file(str(path)),
        "from_dict": _core.Config.from_dict(
            _yaml_route.load(path.read_text(encoding="utf-8"), str(path), error=ls.LockstepError),
            origin=str(path),
        ),
    }
    for route, config in routes.items():
        signals = peer_signals(config)
        for parameter, name in PARAMETER_SIGNALS.items():
            assert signals[name].hex() == float(texts[parameter]).hex(), (route, name)


# --- §5: the floating-point flags (R7) -----------------------------------------------------------

PEER_TU = "aircraft/notional_transport/sim/transport/autopilot/autopilot.cpp"
PORT_TU = "embedded/autopilot/autopilot.cpp"
VARIANT_TU = "tests/plugins/autopilot_peer_variant/autopilot_peer_variant.cpp"
R5_TU = "tests/autopilot_peer/autopilot_peer_equivalence_test.cpp"
TRANSPORT_TU = "tests/autopilot_peer/autopilot_peer_test.cpp"
UNITS = {
    "peer": PEER_TU,
    "port": PORT_TU,
    "variant": VARIANT_TU,
    "r5": R5_TU,
    "transport": TRANSPORT_TU,
}
PRESETS = ("debug", "release", "static-plugins", "asan")

# §5's token set, with amendment 3's two and P-01's six (plan §16).
FP_PREFIXES = (
    "-O",
    "-march=",
    "-mtune=",
    "-mfpmath=",
    "-ffp-contract=",
    "-ffp-model=",
    "-fdenormal-fp-math",
    "-fexcess-precision=",
    "-ffp-eval-method=",
    "-ffp-exception-behavior=",
    "-fveclib=",
    "-mrecip",
)
FP_FLAGS = {
    "-ffast-math",
    "-fno-fast-math",
    "-mdaz-ftz",
    "-mno-daz-ftz",
    "-fassociative-math",
    "-fno-associative-math",
    "-freciprocal-math",
    "-fno-reciprocal-math",
    "-fno-signed-zeros",
    "-fsigned-zeros",
    "-fno-trapping-math",
    "-ftrapping-math",
    "-ffinite-math-only",
    "-fno-finite-math-only",
    "-fno-math-errno",
    "-fmath-errno",
    "-funsafe-math-optimizations",
    "-fno-unsafe-math-optimizations",
    "-fapprox-func",
    "-fno-approx-func",
    "-frounding-math",
    "-fno-rounding-math",
    "-fhonor-nans",
    "-fno-honor-nans",
    "-fhonor-infinities",
    "-fno-honor-infinities",
}
EXPECTED_TOKENS = {
    "debug": set(),
    "release": {"-O3"},
    "static-plugins": set(),
    "asan": set(),
    "python": {"-O3"},
}  # §5, check 1
# §5 check 2's named residuals, the peer's and the variant's units against the port's.
ALLOWED_RESIDUALS = {
    "-std=c++17",
    "-std=c++23",
    "-fvisibility=hidden",
    "-fvisibility-inlines-hidden",
}
# P-01 (ii)'s effective cc1 set, measured by plan mode's -### probe for all four preset shapes.
CC1_PREFIXES = (
    "-ffp-",
    "-fdenormal-fp-math",
    "-fveclib=",
    "-mrecip",
    "-complex-range=",
    "-mlimit-float-precision",
    "-O",
    "-march=",
    "-mtune=",
    "-mfpmath=",
)
CC1_FLAGS = {
    "-frounding-math",
    "-fno-rounding-math",
    "-fmath-errno",
    "-fno-math-errno",
    "-menable-no-infs",
    "-menable-no-nans",
    "-fapprox-func",
    "-funsafe-math-optimizations",
    "-fno-signed-zeros",
    "-mreassociate",
    "-freciprocal-math",
    "-ffast-math",
    "-ffinite-math-only",
}
CC1_PAIRS = ("-target-cpu", "-tune-cpu", "-target-feature")
CLANG_CC1 = {
    "-fmath-errno",
    "-ffp-contract=on",
    "-fno-rounding-math",
    "-target-cpu x86-64",
    "-tune-cpu generic",
}
GCC_CC1 = {"-march=x86-64", "-mtune=generic", "-O3"}
# Arguments that name an output, a dependency file or the compile step, with their values.
OUTPUT_ARGS = {"-o", "-MF", "-MT", "-MQ"}
DEPENDENCY_FLAGS = {"-MD", "-MMD", "-c"}


def is_fp_token(token: str) -> bool:
    if token in FP_FLAGS or token.startswith(FP_PREFIXES) or token == "-Ofast":
        return True
    return token.startswith("-m") and any(part in token for part in ("fma", "avx", "sse"))


@dataclass
class Unit:
    build: str
    name: str
    source: str
    directory: str
    tokens: list[str]


def load_units(build: str, directory: Path, names: dict[str, str]) -> dict[str, Unit]:
    database = directory / "compile_commands.json"
    assert database.is_file(), f"{database} is missing: build every preset first (`just check`)"
    entries = json.loads(database.read_text(encoding="utf-8"))
    by_file = {os.path.realpath(e["file"]): e for e in entries}
    units: dict[str, Unit] = {}
    for name, relative in names.items():
        source = os.path.realpath(ROOT / relative)
        assert source in by_file, f"{build}: {relative} is not in {database}"
        entry = by_file[source]
        tokens = shlex.split(entry["command"]) if "command" in entry else list(entry["arguments"])
        units[name] = Unit(build, name, source, entry["directory"], tokens)
    return units


def without_outputs(unit: Unit) -> list[str]:
    """The unit's command without its source, compile step, output or dependency arguments
    (P-03): what remains writes nothing."""
    kept: list[str] = []
    skip = False
    for token in unit.tokens:
        if skip:
            skip = False
            continue
        if token in OUTPUT_ARGS:
            skip = True
            continue
        if (
            token in DEPENDENCY_FLAGS
            or os.path.realpath(os.path.join(unit.directory, token)) == unit.source
        ):
            continue
        kept.append(token)
    return kept


def residual(unit: Unit) -> Counter[str]:
    """Every token but the compiler, -I, -isystem, -D, -o, -c and the source (§5 check 2)."""
    out: Counter[str] = Counter()
    skip = False
    for token in without_outputs(unit)[1:]:
        if skip:
            skip = False
            continue
        if token in ("-I", "-isystem", "-D", "-include"):
            skip = True
            continue
        if token.startswith(("-I", "-D", "-isystem")):
            continue
        out[token] += 1
    return out


def effective_cc1(unit: Unit) -> set[str]:
    """P-01 (ii): the driver's own cc1 (cc1plus) command for the unit's full compile command,
    printed by -### (which runs nothing), and its floating-point arguments."""
    proc = subprocess.run(
        [unit.tokens[0], "-###", *unit.tokens[1:]],
        cwd=unit.directory,
        capture_output=True,
        text=True,
        timeout=120,
        check=False,
    )
    assert proc.returncode == 0, proc.stderr
    assert "Configuration file:" not in proc.stderr, proc.stderr
    lines = [line for line in proc.stderr.splitlines() if '"-cc1"' in line or "cc1plus " in line]
    assert len(lines) == 1, proc.stderr
    args = shlex.split(lines[0])
    found: set[str] = set()
    for index, arg in enumerate(args):
        if arg in CC1_PAIRS:
            found.add(f"{arg} {args[index + 1]}")
        elif arg in CC1_FLAGS or arg.startswith(CC1_PREFIXES):
            found.add(arg)
    return found


def target_facts(unit: Unit) -> dict[str, Any]:
    """§5 check 3, with P-01 (iii) and P-03: the unit's own arguments, outputs removed, on an empty
    source (-dM -E) and on the eval-method macro (clang expands __FLT_EVAL_METHOD__ as a builtin,
    so -dM does not list it; plan §16, C-01)."""
    base = without_outputs(unit)
    macros = subprocess.run(
        [*base, "-dM", "-E", "-x", "c++", "/dev/null"],
        cwd=unit.directory,
        capture_output=True,
        text=True,
        timeout=120,
        check=True,
    ).stdout
    defined = {line.split()[1] for line in macros.splitlines() if line.startswith("#define ")}
    method = subprocess.run(
        [*base, "-E", "-P", "-x", "c++", "-"],
        input="__FLT_EVAL_METHOD__\n",
        cwd=unit.directory,
        capture_output=True,
        text=True,
        timeout=120,
        check=True,
    ).stdout.split()
    return {
        "fma": sorted(defined & {"__FMA__", "__FMA4__"}),
        "sse2_math": "__SSE2_MATH__" in defined,
        "eval_method": method[-1] if method else "",
    }


def check_target(unit: Unit) -> dict[str, Any]:
    facts = target_facts(unit)
    assert facts["fma"] == [], f"{unit.build} {unit.name}: {facts}"
    assert facts["sse2_math"], f"{unit.build} {unit.name}: {facts}"
    assert facts["eval_method"] == "0", f"{unit.build} {unit.name}: {facts}"
    return facts


def check_tokens(unit: Unit, expected: set[str]) -> set[str]:
    hits = [t for t in unit.tokens if t.startswith("@") or t.startswith("--config")]
    assert hits == [], f"{unit.build} {unit.name}: response or config file {hits}"
    found = {t for t in unit.tokens if is_fp_token(t)}
    assert found == expected, f"{unit.build} {unit.name}: floating-point flags {found}"
    return found


def python_build() -> Path:
    return Path(_core.default_plugin_dir()).parent


def test_autopilot_peer_flags_every_preset(report_dir: Path) -> None:
    """R7, §5 checks 1 to 3 and P-01, for every preset the gate builds and build/python."""
    builds = {preset: ROOT / "build" / preset for preset in PRESETS} | {"python": python_build()}
    table: dict[str, Any] = {}
    for build, directory in builds.items():
        units = load_units(build, directory, UNITS)
        expected_cc1 = CLANG_CC1 | ({"-O3"} if build in ("release", "python") else set())
        for unit in units.values():
            table[f"{build} {unit.name}"] = {
                "tokens": sorted(check_tokens(unit, EXPECTED_TOKENS[build])),
                "cc1": sorted(effective_cc1(unit)),
                "target": check_target(unit),
                "command": " ".join(t for t in without_outputs(unit)),
            }
            assert set(table[f"{build} {unit.name}"]["cc1"]) == expected_cc1, (
                f"{build} {unit.name}: effective {table[f'{build} {unit.name}']['cc1']}"
            )
        port = residual(units["port"])
        for name in ("peer", "variant"):
            mine = residual(units[name])
            difference = set((mine - port) + (port - mine))
            assert difference <= ALLOWED_RESIDUALS, f"{build} {name} against port: {difference}"
    write_json(report_dir / "fp_flags.json", table)


def standalone_recipe_arguments() -> list[str]:
    """The justfile's embedded-standalone configure line, as written."""
    lines = (ROOT / "justfile").read_text(encoding="utf-8").splitlines()
    start = next(i for i, line in enumerate(lines) if line.startswith("embedded-standalone:"))
    for line in lines[start + 1 :]:
        if line.strip().startswith("cmake -S embedded"):
            return shlex.split(line.strip())
    raise AssertionError("the embedded-standalone recipe has no `cmake -S embedded` line")


def test_autopilot_peer_flags_embedded_standalone(tmp_path: Path, report_dir: Path) -> None:
    """R7, §5 and decision 7: the port's unit in the standalone configure, for both compilers,
    with the recipe's own arguments plus -DCMAKE_EXPORT_COMPILE_COMMANDS=ON. The wall time is
    reported as part of the pytest cost (amendment 6)."""
    recipe = standalone_recipe_arguments()
    table: dict[str, Any] = {}
    for cxx in ("clang++-18", "g++"):
        directory = tmp_path / cxx
        args = [a.replace("${dir}", str(directory)).replace("${cxx}", cxx) for a in recipe]
        started = time.monotonic()
        subprocess.run(
            [*args, "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=300,
            check=True,
        )
        elapsed = time.monotonic() - started
        unit = load_units(f"standalone-{cxx}", directory, {"port": PORT_TU})["port"]
        expected_cc1 = (CLANG_CC1 | {"-O3"}) if cxx.startswith("clang") else GCC_CC1
        cc1 = effective_cc1(unit)
        table[cxx] = {
            "configure_s": round(elapsed, 2),
            "tokens": sorted(check_tokens(unit, {"-O3"})),
            "cc1": sorted(cc1),
            "target": check_target(unit),
            "command": " ".join(without_outputs(unit)),
        }
        assert cc1 == expected_cc1, f"{cxx}: effective {sorted(cc1)}"
    write_json(report_dir / "fp_flags_standalone.json", table)


# --- §3: independence ----------------------------------------------------------------------------


def test_autopilot_peer_shares_no_include_or_symbol_with_embedded() -> None:
    """§3: no dependency of the peer's or the variant's unit lies under embedded/ (-MM on the unit's
    own command with its outputs removed, P-03), and neither .so names an astra_autopilot:: symbol;
    the shipped .so names none of the variant's defect types (amendment 2)."""
    units = load_units("python", python_build(), {"peer": PEER_TU, "variant": VARIANT_TU})
    embedded = os.path.realpath(ROOT / "embedded") + os.sep
    for unit in units.values():
        deps = subprocess.run(
            [*without_outputs(unit), "-MM", unit.source],
            cwd=unit.directory,
            capture_output=True,
            text=True,
            timeout=120,
            check=True,
        ).stdout
        paths = re.sub(r"^[^:]*:", "", deps).replace("\\\n", " ").split()
        assert paths, unit.name
        inside = [
            p
            for p in paths
            if os.path.realpath(os.path.join(unit.directory, p)).startswith(embedded)
        ]
        assert inside == [], f"{unit.name} includes {inside}"
    plugin_dir = Path(_core.default_plugin_dir())
    for plugin, forbidden in (
        (
            PEER_PLUGIN,
            (
                "astra_autopilot::",
                "FloatElevatorTrimLaw",
                "OutputFirstPid",
                "FloatPitchRateLaw",
                "ModeWordZeroLaw",
            ),
        ),
        (VARIANT_PLUGIN, ("astra_autopilot::",)),
    ):
        library = plugin_dir / _core.PluginLoader.library_name(plugin)
        symbols = subprocess.run(
            ["nm", "-C", str(library)], capture_output=True, text=True, timeout=60, check=True
        ).stdout
        assert symbols, library
        for name in forbidden:
            assert name not in symbols, f"{library} names {name}"


# --- §6: the equivalence (R6) ---------------------------------------------------------------------


def _plain(value: Any) -> Any:
    if isinstance(value, bytes):
        return value.decode()
    if isinstance(value, np.ndarray):
        return [_plain(v) for v in value.tolist()]
    if isinstance(value, np.generic):
        return value.item()
    return value


def attrs(obj: Any) -> dict[str, Any]:
    return {key: _plain(value) for key, value in obj.attrs.items()}


def kept(names: list[str]) -> list[int]:
    return [i for i, name in enumerate(names) if not name.startswith("transport.")]


def bits(values: np.ndarray) -> np.ndarray:
    return np.ascontiguousarray(values, dtype=np.float64).view(np.uint64)


GIT_DESCRIBE_HELP = (
    "git_describe is core-build provenance, the tree at the last libastra_core build "
    "(core/include/astra/build_info.hpp:8-10): a mismatch means libastra_core was rebuilt between "
    "the two runs, and a -dirty suffix means that build saw uncommitted edits (lessons 2026-10-04, "
    "2026-10-08). Read a whole-log difference first as this attribute, not as the port."
)


def compare_logs(shm: Path, mine: Path) -> list[str]:
    """O1, O2, O5 to O9, O11 to O13: every log object, bitwise, with Decision 8's exclusions."""
    with h5py.File(shm, "r") as shm_file, h5py.File(mine, "r") as mine_file:
        return compare_open_logs(shm_file, mine_file)


def compare_open_logs(a: Any, b: Any) -> list[str]:
    problems: list[str] = []
    ra, rb = attrs(a), attrs(b)
    for side, root in (("shm", ra), ("in-process", rb)):  # O1
        if root.get("complete") != 1 or "finalized_by" in root:
            problems.append(f"O1 {side}: complete {root.get('complete')}, {root}")
    if sorted(ra) != sorted(rb):
        problems.append(f"O11 root attribute names {sorted(ra)} against {sorted(rb)}")
    for key in sorted(set(ra) & set(rb) - {"config"}):  # O11
        if ra[key] != rb[key]:
            help_text = f" ({GIT_DESCRIBE_HELP})" if key == "git_describe" else ""
            problems.append(f"O11 {key}: {ra[key]!r} against {rb[key]!r}{help_text}")
    configs = []  # O8
    for text in (ra["config"], rb["config"]):
        tree = yaml.load(text, Loader=yaml.BaseLoader)
        tree.pop("transport")
        configs.append(json.dumps(tree))
    if configs[0] != configs[1]:
        problems.append("O8: the stored configs differ outside the transport block")
    groups = sorted(k for k in a if k.startswith("rate_"))  # O5, O6
    if groups != sorted(k for k in b if k.startswith("rate_")):
        problems.append(f"O5 rate groups {groups} against {sorted(k for k in b)}")
    for group in groups:
        va, vb = a[group]["values"], b[group]["values"]
        na, nb = _plain(va.attrs["names"]), _plain(vb.attrs["names"])
        ua, ub = _plain(va.attrs["units"]), _plain(vb.attrs["units"])
        ka, kb = kept(na), kept(nb)
        if [(na[i], ua[i]) for i in ka] != [(nb[i], ub[i]) for i in kb]:
            problems.append(f"O5 {group}: names or units differ")
            continue
        if not np.array_equal(a[group]["frame"][()], b[group]["frame"][()]):
            problems.append(f"O6 {group}: frame column differs")
        xa, xb = bits(va[()][:, ka]), bits(vb[()][:, kb])
        if xa.shape != xb.shape or not np.array_equal(xa, xb):
            rows, cols = np.nonzero(xa != xb) if xa.shape == xb.shape else ([0], [0])
            problems.append(
                f"O6 {group}: values differ, first at row {rows[0]}, column {na[ka[cols[0]]]}"
            )
        if attrs(a[group]) != attrs(b[group]):
            problems.append(f"O6 {group}: {attrs(a[group])} against {attrs(b[group])}")
    sa, sb = a["summary"], b["summary"]  # O2, O12
    if attrs(sa) != attrs(sb):
        problems.append(f"O12 /summary attributes {attrs(sa)} against {attrs(sb)}")
    if attrs(sa).get("frames_run") != FRAMES:
        problems.append(f"O2 frames_run {attrs(sa).get('frames_run')}")
    for stat in ("min", "max"):
        na, nb = _plain(sa[stat].attrs["names"]), _plain(sb[stat].attrs["names"])
        ka, kb = kept(na), kept(nb)
        if [na[i] for i in ka] != [nb[i] for i in kb] or not np.array_equal(
            bits(sa[stat][()][ka]), bits(sb[stat][()][kb])
        ):
            problems.append(f"O12 /summary/{stat} differs")
    fa, fb = a["final_state"], b["final_state"]  # O7
    if attrs(fa) != attrs(fb) or sorted(fa) != sorted(fb):
        problems.append(f"O7 /final_state {attrs(fa)} against {attrs(fb)}")
    for name in sorted(set(fa) & set(fb)):
        if attrs(fa[name]) != attrs(fb[name]) or not np.array_equal(
            bits(fa[name][()]), bits(fb[name][()])
        ):
            problems.append(f"O7 /final_state/{name} differs")
    if len(a["writes"]) != 0 or len(b["writes"]) != 0:  # O13
        problems.append("O13 /writes is not empty")
    rows = []  # O9
    for f, transport in ((a, ls.SHM_PLUGIN), (b, PEER_PLUGIN)):
        table = [tuple(_plain(v) for v in row) for row in f["plugins"][()]]
        transports = [row for row in table if row[0] == transport]
        if len(transports) != 1:
            problems.append(f"O9: {len(transports)} rows for {transport}")
        rows.append([row for row in table if row[0] != transport])
    if rows[0] != rows[1]:
        problems.append(f"O9 /plugins outside the transport row: {rows[0]} against {rows[1]}")
    return problems


def first_record_difference(a: np.ndarray, b: np.ndarray) -> int | None:
    """The first index whose record bytes differ, or None (equal counts assumed)."""
    if len(a) != len(b):
        return min(len(a), len(b))
    rows_a = np.frombuffer(a.tobytes(), dtype=np.uint8).reshape(len(a), a.dtype.itemsize)
    rows_b = np.frombuffer(b.tobytes(), dtype=np.uint8).reshape(len(b), b.dtype.itemsize)
    differ = np.nonzero(np.any(rows_a != rows_b, axis=1))[0]
    return int(differ[0]) if len(differ) else None


@pytest.mark.parametrize("level", LEVELS)
def test_autopilot_peer_equals_the_autopilot_bitwise(
    runs: PeerRuns, level: str, report_dir: Path
) -> None:
    shm, mine = runs.shm(level), runs.inprocess(level)
    rs, rm = shm.result, mine.result
    signals = rm["signals"]
    report: dict[str, Any] = {"shm": rs, "inprocess": rm}
    write_json(report_dir / f"equivalence_{level}.json", report)
    # O2, O3, O10, O13: the counts, each side's own.
    assert rs["halt"] is None, rs["halt"]
    assert rm["halt"] is None, rm["halt"]
    assert rs["frame"] == FRAMES
    assert rm["frame"] == FRAMES
    assert rs["exchanges"] == float(EXCHANGES)
    assert signals["transport.exchanges"] == float(EXCHANGES)
    assert rs["missed"] == 0.0
    assert signals["transport.missed"] == 0.0
    assert rs["fault"] == 0.0
    assert signals["transport.fault"] == 0.0
    counts = [
        (len(r.recording.requests), len(r.recording.replies), len(r.recording.states))
        for r in (shm, mine)
    ]
    assert counts == [(EXCHANGES, EXCHANGES, STATES)] * 2
    summary = rs["peer_summary"]
    assert rs["peer_returncode"] == 0, rs["peer_stderr"]
    assert (summary["name"], summary["handled"], summary["held"]) == ("autopilot", EXCHANGES, 0)
    assert signals["transport.peer.handled"] == float(summary["handled"])
    assert signals["transport.peer.held"] == float(summary["held"])
    # O15: the binaries the two runs used are the audited build's (plan §16, P-02).
    built = os.path.realpath(python_build() / "embedded/autopilot/astra_autopilot")
    assert rs["autopilot"] == built, f"the shm run launched {rs['autopilot']}, not {built}"
    assert rs["plugin_dir"] == rm["plugin_dir"] == _core.default_plugin_dir()
    assert rs["fp_environment"] == rm["fp_environment"]
    # O4: the recorder, byte for byte.
    if shm.data != mine.data:
        where = {
            stream: first_record_difference(
                getattr(shm.recording, stream), getattr(mine.recording, stream)
            )
            for stream in ("requests", "replies", "states")
        }
        pytest.fail(f"O4: the recorders differ; first differing record per stream {where}")
    # O1, O2, O5 to O9, O11 to O13: the logs.
    problems = compare_logs(shm.log_path, mine.log_path)
    assert problems == [], "\n".join(problems)


def test_autopilot_peer_is_deterministic(runs: PeerRuns) -> None:
    """O14: the light in-process run twice, recorder bytes equal."""
    assert runs.repeat().data == runs.inprocess("light").data


# --- §9: the four porting controls (R8) ----------------------------------------------------------

GAIN_LINE = re.compile(r"^constexpr double (k\w+) = ([-+0-9.eE]+);$", re.MULTILINE)
RANGE_LINE = re.compile(
    r"^inline constexpr Range (k\w+)\{([-+0-9.eE]+), ([-+0-9.eE]+)\};$", re.MULTILINE
)


def port_constants() -> tuple[dict[str, float], dict[str, tuple[float, float]]]:
    """The port's gains and limits from embedded/autopilot's own text (the replay writes the
    port's expressions with the port's constants; AutopilotPeerEquivalence pins the peer's)."""
    gains = GAIN_LINE.findall((ROOT / "embedded/autopilot/gains.hpp").read_text(encoding="utf-8"))
    ranges = RANGE_LINE.findall(
        (ROOT / "embedded/autopilot/limits.hpp").read_text(encoding="utf-8")
    )
    assert len(gains) == 12
    assert len(ranges) == 5
    return ({k: float(v) for k, v in gains}, {k: (float(lo), float(hi)) for k, lo, hi in ranges})


GAINS, LIMITS = port_constants()
TWO_PI = 2.0 * math.pi  # autopilot.cpp:14-15: kPi's literal rounds to math.pi
MODE_FLAGS = 1  # limits.hpp kModeFlags
MAX_DT = 1.0  # limits.hpp kMaxIntegrationDt


def f32(value: float) -> float:
    """static_cast<double>(static_cast<float>(value))."""
    return float(struct.unpack("<f", struct.pack("<f", value))[0])


def clamp(value: float, lo: float, hi: float) -> float:
    return lo if value < lo else (hi if value > hi else value)


class ReplayPid:
    """pid.hpp:28-41 in plain Python doubles, in its operation order; ``output_first`` is the
    output_before_integral control's slip."""

    def __init__(self, kp: float, ki: float, kd: float, output_first: bool) -> None:
        self.kp, self.ki, self.kd = kp, ki, kd
        self.output_first = output_first
        self.integral = 0.0

    def update(
        self,
        error: float,
        error_rate: float,
        dt: float,
        offset: float,
        sign: float,
        limit: tuple[float, float],
    ) -> float:
        lo, hi = limit

        def distance(value: float) -> float:
            return lo - value if value < lo else (value - hi if value > hi else 0.0)

        proportional = self.kp * error + self.kd * error_rate
        before = offset + sign * (proportional + self.integral)
        early = clamp(before, lo, hi)
        if dt > 0.0:
            candidate = self.integral + self.ki * error * dt
            after = offset + sign * (proportional + candidate)
            if math.isfinite(candidate) and (
                (lo <= after <= hi) or distance(after) < distance(before)
            ):
                self.integral = candidate
        if self.output_first:
            return early
        return clamp(offset + sign * (proportional + self.integral), lo, hi)


class ReplayLaw:
    """autopilot.cpp:40-85 in plain Python doubles, with one control's slip or none."""

    def __init__(self, trims: tuple[float, float, float], defect: str | None) -> None:
        elevator, self.throttle_trim, self.theta_trim = trims
        self.elevator_trim = f32(elevator) if defect == "float_elevator_trim" else elevator
        self.pitch_rate = (
            f32(GAINS["kPitchRate"]) if defect == "float_pitch_rate" else GAINS["kPitchRate"]
        )
        first = defect == "output_before_integral"
        self.altitude = ReplayPid(GAINS["kAltP"], GAINS["kAltI"], GAINS["kAltD"], first)
        self.speed = ReplayPid(GAINS["kSpeedP"], GAINS["kSpeedI"], 0.0, first)
        self.heading = ReplayPid(GAINS["kHeadingP"], GAINS["kHeadingI"], 0.0, first)
        self.mode = 0 if defect == "mode_word_zero" else MODE_FLAGS
        self.last = {
            "elevator": self.elevator_trim,
            "aileron": 0.0,
            "rudder": 0.0,
            "throttle": self.throttle_trim,
        }
        self.previous_now = 0.0
        self.have_previous = False

    def update(self, request: np.void) -> dict[str, float]:
        x = {name: float(request[name]) for name in (*READ_FIELDS, "t")}
        now = x["t"]
        dt = now - self.previous_now if self.have_previous else 0.0
        self.previous_now = now
        self.have_previous = True
        if not math.isfinite(dt) or dt <= 0.0 or dt > MAX_DT:
            dt = 0.0
        if usable(request):
            offset = (
                self.elevator_trim
                + GAINS["kPitchTheta"] * (x["theta"] - self.theta_trim)
                + self.pitch_rate * x["q"]
            )
            self.last["elevator"] = self.altitude.update(
                SETPOINTS.altitude_m - x["altitude"],
                -x["vertical_speed"],
                dt,
                offset,
                -1.0,
                LIMITS["kElevatorLimit"],
            )
            self.last["throttle"] = self.speed.update(
                SETPOINTS.airspeed_mps - x["tas"],
                0.0,
                dt,
                self.throttle_trim,
                1.0,
                LIMITS["kThrottleLimit"],
            )
            bank = self.heading.update(
                math.remainder(SETPOINTS.heading_rad - x["psi"], TWO_PI),
                0.0,
                dt,
                0.0,
                1.0,
                LIMITS["kBankCommandLimit"],
            )
            self.last["aileron"] = clamp(
                GAINS["kBankP"] * (bank - x["phi"]) - GAINS["kRollRate"] * x["p"],
                *LIMITS["kAileronLimit"],
            )
            self.last["rudder"] = clamp(GAINS["kYawRate"] * x["r"], *LIMITS["kRudderLimit"])
        return {**self.last, "mode_flags": float(self.mode)}


def usable(request: np.void) -> bool:
    if (int(request["status_flags"]) & WIND_INVALID) != 0:
        return False
    return all(math.isfinite(float(request[name])) for name in READ_FIELDS)


def reply_values(reply: np.void) -> dict[str, float]:
    throttle = reply["throttle"]
    assert float(throttle[0]).hex() == float(throttle[1]).hex()
    return {
        "elevator": float(reply["elevator"]),
        "aileron": float(reply["aileron"]),
        "rudder": float(reply["rudder"]),
        "throttle": float(throttle[0]),
        "mode_flags": float(reply["mode_flags"]),
    }


def differing(a: dict[str, float], b: dict[str, float]) -> list[str]:
    return sorted(k for k in a if a[k].hex() != b[k].hex())


def config_trims(level: str) -> tuple[float, float, float]:
    """Q8: the config's committed literals, as the shm run's ``--trim config`` reads them."""
    config = base_load(m4_config(level))
    controls = config["vehicle"]["initial_controls"]["controls"]
    return (
        float(controls["elevator"]),
        float(controls["throttle"]),
        float(config["initial"]["state"]["euler_nb"][1]),
    )


def replay_preconditions(shm: ChildRun, level: str) -> dict[str, Any]:
    """§9's precondition, in its registered order: usable; the shipped replay reproduces the
    reference's replies 0 and 1; then each control's defective expressions."""
    requests, replies = shm.recording.requests, shm.recording.replies
    assert usable(requests[0]), "request 0 is not usable"
    assert usable(requests[1]), "request 1 is not usable"
    shipped = ReplayLaw(config_trims(level), None)
    expected = [shipped.update(requests[0]), shipped.update(requests[1])]
    for k in (0, 1):
        assert differing(expected[k], reply_values(replies[k])) == [], (
            f"the replay does not reproduce the reference's reply {k}"
        )
    facts: dict[str, Any] = {"request0_q": float(requests[0]["q"]).hex()}
    for defect in DEFECTS:
        law = ReplayLaw(config_trims(level), defect)
        got = [law.update(requests[0]), law.update(requests[1])]
        facts[defect] = {
            "exchange0": differing(expected[0], got[0]),
            "exchange1": differing(expected[1], got[1]),
            "elevator": [[expected[k]["elevator"].hex(), got[k]["elevator"].hex()] for k in (0, 1)],
        }
    facts["shm_mode_flags_all_one"] = bool(np.all(replies["mode_flags"] == MODE_FLAGS))
    return facts


# §9's table: each control's registered first differing record and its precondition results, from
# the shm reference alone, recorded before any control run (amendment 1): §14 step 7a, 2026-10-18,
# at 52e2e65 (preconditions_<level>.json, copied verbatim).
REGISTERED: dict[str, dict[str, Any]] | None = {
    "light": {
        "request0_q": "0x0.0p+0",
        "float_elevator_trim": {
            "exchange0": ["elevator"],
            "exchange1": ["elevator"],
            "elevator": [
                ["0x1.b508da83e1eddp-6", "0x1.b508da0000000p-6"],
                ["0x1.b50fa169dda99p-6", "0x1.b50fa0e5fbbbcp-6"],
            ],
        },
        "output_before_integral": {
            "exchange0": [],
            "exchange1": ["aileron", "elevator", "throttle"],
            "elevator": [
                ["0x1.b508da83e1eddp-6", "0x1.b508da83e1eddp-6"],
                ["0x1.b50fa169dda99p-6", "0x1.b50fa16a0fd9fp-6"],
            ],
        },
        "float_pitch_rate": {
            "exchange0": [],
            "exchange1": ["elevator"],
            "elevator": [
                ["0x1.b508da83e1eddp-6", "0x1.b508da83e1eddp-6"],
                ["0x1.b50fa169dda99p-6", "0x1.b50fa169ddeaap-6"],
            ],
        },
        "mode_word_zero": {
            "exchange0": ["mode_flags"],
            "exchange1": ["mode_flags"],
            "elevator": [
                ["0x1.b508da83e1eddp-6", "0x1.b508da83e1eddp-6"],
                ["0x1.b50fa169dda99p-6", "0x1.b50fa169dda99p-6"],
            ],
        },
        "shm_mode_flags_all_one": True,
    },
    "moderate": {
        "request0_q": "0x0.0p+0",
        "float_elevator_trim": {
            "exchange0": ["elevator"],
            "exchange1": ["elevator"],
            "elevator": [
                ["0x1.b508da83e1eddp-6", "0x1.b508da0000000p-6"],
                ["0x1.b541db1eabccep-6", "0x1.b541da9ac9df1p-6"],
            ],
        },
        "output_before_integral": {
            "exchange0": [],
            "exchange1": ["aileron", "elevator", "throttle"],
            "elevator": [
                ["0x1.b508da83e1eddp-6", "0x1.b508da83e1eddp-6"],
                ["0x1.b541db1eabccep-6", "0x1.b541db20b97f2p-6"],
            ],
        },
        "float_pitch_rate": {
            "exchange0": [],
            "exchange1": ["elevator"],
            "elevator": [
                ["0x1.b508da83e1eddp-6", "0x1.b508da83e1eddp-6"],
                ["0x1.b541db1eabccep-6", "0x1.b541db1eae318p-6"],
            ],
        },
        "mode_word_zero": {
            "exchange0": ["mode_flags"],
            "exchange1": ["mode_flags"],
            "elevator": [
                ["0x1.b508da83e1eddp-6", "0x1.b508da83e1eddp-6"],
                ["0x1.b541db1eabccep-6", "0x1.b541db1eabccep-6"],
            ],
        },
        "shm_mode_flags_all_one": True,
    },
}


@pytest.mark.parametrize("level", LEVELS)
def test_autopilot_peer_control_preconditions_hold(
    runs: PeerRuns, level: str, report_dir: Path
) -> None:
    """§9's structural preconditions (§14 step 7a): the reference alone; nothing of the peer or
    the variant runs here."""
    facts = replay_preconditions(runs.shm(level), level)
    write_json(report_dir / f"preconditions_{level}.json", facts)
    assert facts["float_elevator_trim"]["exchange0"] == ["elevator"]
    assert facts["output_before_integral"]["exchange0"] == []
    assert "throttle" in facts["output_before_integral"]["exchange1"]
    assert float.fromhex(facts["request0_q"]) == 0.0
    assert facts["float_pitch_rate"]["exchange0"] == []
    assert facts["float_pitch_rate"]["exchange1"] == ["elevator"]
    assert facts["mode_word_zero"]["exchange0"] == ["mode_flags"]
    assert facts["shm_mode_flags_all_one"]


@pytest.mark.parametrize("level", LEVELS)
def test_autopilot_peer_control_preconditions_equal_the_registered_literals(
    runs: PeerRuns, level: str
) -> None:
    assert REGISTERED is not None, "§9's literals are not recorded yet (§14 step 7a)"
    assert replay_preconditions(runs.shm(level), level) == REGISTERED[level]


def registered_record(defect: str, level: str) -> tuple[int, list[str]]:
    """§9: k* and S for a control at a level, from the registered literals."""
    assert REGISTERED is not None
    facts = REGISTERED[level][defect]
    if defect in ("float_elevator_trim", "mode_word_zero"):
        return 0, facts["exchange0"]
    return 1, facts["exchange1"]


def reply_field_differences(a: np.void, b: np.void) -> list[str]:
    out = []
    for name in REPLY_FIELDS:
        if np.asarray(a[name]).tobytes() != np.asarray(b[name]).tobytes():
            out.append(name)
    return out


def band_maxima(recording: ls.Recording) -> dict[str, float]:
    """M4's R2 metric over every recorded state (test_lockstep.py's flight_metrics)."""
    y = recording.states["y"]
    psi, _, _ = euler_321(y[:, 6:10])
    heading = np.degrees(np.arctan2(np.sin(psi), np.cos(psi)))
    return {
        "altitude_m": float(np.max(np.abs(y[:, 2] - SETPOINTS.altitude_m))),
        "heading_deg": float(np.max(np.abs(heading))),
    }


@pytest.mark.parametrize("level", LEVELS)
@pytest.mark.parametrize("defect", DEFECTS)
def test_autopilot_peer_control_breaks_first_where_registered(
    runs: PeerRuns, defect: str, level: str, report_dir: Path
) -> None:
    k_star, fields = registered_record(defect, level)
    reference = runs.shm(level).recording
    control = runs.control(defect, level)
    assert control.result["halt"] is None, control.result["halt"]
    rec = control.recording
    first_request = first_record_difference(reference.requests, rec.requests)
    first_reply = first_record_difference(reference.replies, rec.replies)
    first_state = first_record_difference(reference.states, rec.states)
    report: dict[str, Any] = {
        "k_star": k_star,
        "fields": fields,
        "first_request": first_request,
        "first_reply": first_reply,
        "first_state": first_state,
        "bands": band_maxima(rec),
        "nominal_bands": band_maxima(runs.inprocess(level).recording),
    }
    if first_reply is not None:
        report["values"] = {
            name: [
                _plain(reference.replies[first_reply][name]),
                _plain(rec.replies[first_reply][name]),
            ]
            for name in reply_field_differences(
                reference.replies[first_reply], rec.replies[first_reply]
            )
        }
    write_json(report_dir / f"control_{defect}_{level}.json", report)
    assert first_reply == k_star, f"the first differing reply is {first_reply}, not {k_star}"
    if defect == "mode_word_zero":  # §9's whole-run registration (amendment 2)
        assert first_request is None, first_request
        assert first_state is None, first_state
        assert all(
            reply_field_differences(a, b) == ["mode_flags"]
            for a, b in zip(reference.replies, rec.replies, strict=True)
        )
    else:
        assert first_request is None or first_request > k_star, first_request
        assert first_state is None or first_state > 10 * k_star, first_state
        differ = reply_field_differences(reference.replies[k_star], rec.replies[k_star])
        assert differ == sorted(fields), f"reply {k_star} differs in {differ}, registered {fields}"
    # The bands, registered as a pass (amendment 2); the nominal's are context (amendment 4).
    assert report["bands"]["altitude_m"] <= ALTITUDE_BAND_M, report["bands"]
    assert report["bands"]["heading_deg"] <= HEADING_BAND_DEG, report["bands"]


def test_autopilot_peer_worktree_unchanged_across_the_runs(runs: PeerRuns) -> None:
    """O16 (plan §16, P-02): the commit and the working tree's status, read before this module's
    first run and after its last, are equal. Defined last, so it runs after every run."""
    if runs.worktree_before is None:
        pytest.skip("no run in this selection")
    assert worktree_state() == runs.worktree_before
