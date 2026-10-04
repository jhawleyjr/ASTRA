"""M6 PR 11 (docs/plans/m6-pr11-yaml-route.md): every tool that edits a config as a dict reads it
as the core reads the file (``astra._yaml_route``). Acceptance: ``pytest tests/python -k
pyyaml_route``.

Each config variant is a fixture's text with one value substituted, never written by
``yaml.safe_dump``. "Same as quoted" means a form gives exactly what the same file gives with the
core's text quoted (``'off'``, ``'010'``): PyYAML and the core agree on the quoted spelling, and the
repair produces exactly that string. A number the core refuses is refused at build with the core's
own message, as ``astra run`` refuses it; what text cannot repair is refused at read, naming the
key.
"""

from __future__ import annotations

import math
import re
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import pyarrow.parquet as pq
import pytest
import yaml

from astra import _core, _yaml_route, cli, mc_analysis, montecarlo, report, scenario, trim
from astra import lockstep as ls

REPO = Path(__file__).resolve().parents[2]
DESCENT = REPO / "tests" / "python" / "mc_analysis_fixtures" / "descent.yaml"
CRUISE = REPO / "aircraft" / "notional_transport" / "configs" / "cruise.yaml"
LIGHT = REPO / "aircraft" / "notional_transport" / "configs" / "cruise_light_turbulence.yaml"
SETPOINTS = ls.Setpoints(altitude_m=10000.0, airspeed_mps=230.0, heading_rad=0.0)

GETTERS = ("number", "integer", "boolean", "text")


# --- the probe table ------------------------------------------------------------------------------


@dataclass(frozen=True)
class Row:
    doc: str
    outcome: str  # "unchanged", "repaired" or "refused"
    refusal: str = ""  # a regex the refusal must match


PROBE_TABLE: dict[str, Row] = {
    "on": Row("k: on\n", "repaired"),
    "off": Row("k: off\n", "repaired"),
    "yes": Row("k: yes\n", "repaired"),
    "no": Row("k: no\n", "repaired"),
    "Off": Row("k: Off\n", "repaired"),
    "NO": Row("k: NO\n", "repaired"),
    "True": Row("k: True\n", "repaired"),
    "y": Row("k: y\n", "unchanged"),
    "n": Row("k: n\n", "unchanged"),
    "010": Row("k: 010\n", "repaired"),
    "0o10": Row("k: 0o10\n", "unchanged"),
    "0x10": Row("k: 0x10\n", "repaired"),
    "0b101": Row("k: 0b101\n", "repaired"),
    "1:30": Row("k: 1:30\n", "repaired"),
    "190:20:30": Row("k: 190:20:30\n", "repaired"),
    "1_000": Row("k: 1_000\n", "repaired"),
    "1.0e3": Row("k: 1.0e3\n", "unchanged"),
    "1e3": Row("k: 1e3\n", "unchanged"),
    "1e+3": Row("k: 1e+3\n", "unchanged"),
    "1.0e+3": Row("k: 1.0e+3\n", "unchanged"),
    "1.5E+3": Row("k: 1.5E+3\n", "unchanged"),
    "1000.0": Row("k: 1000.0\n", "unchanged"),
    "1.": Row("k: 1.\n", "unchanged"),
    ".5": Row("k: .5\n", "unchanged"),
    "+12": Row("k: +12\n", "unchanged"),
    "0.1": Row("k: 0.1\n", "unchanged"),
    "0.10": Row("k: 0.10\n", "unchanged"),
    "-0.0": Row("k: -0.0\n", "unchanged"),
    ".inf": Row("k: .inf\n", "unchanged"),
    "-.inf": Row("k: -.inf\n", "unchanged"),
    ".nan": Row("k: .nan\n", "unchanged"),
    ".NaN": Row("k: .NaN\n", "unchanged"),
    "~": Row("k: ~\n", "refused", r"p\.yaml: k: a null"),
    "null": Row("k: null\n", "refused", r"p\.yaml: k: a null"),
    "empty": Row("k:\n", "refused", r"p\.yaml: k: a null"),
    "date": Row("k: 2026-10-03\n", "repaired"),
    "datetime": Row("k: 2026-10-03 12:00:00\n", "repaired"),
    "int above int64": Row("k: 9223372036854775808\n", "repaired"),
    "binary": Row("k: !!binary aGk=\n", "repaired"),
    "set": Row("k: !!set {a, b}\n", "refused", r"p\.yaml: k: the core reads map, PyYAML a set"),
    "quoted off": Row("k: 'off'\n", "unchanged"),
    "quoted 010": Row("k: '010'\n", "unchanged"),
    "quoted 1_000": Row('k: "1_000"\n', "unchanged"),
    "!!str 010": Row("k: !!str 010\n", "unchanged"),
    "!!float 1": Row("k: !!float 1\n", "unchanged"),
    "1e+400": Row("k: 1e+400\n", "unchanged"),
    "1e-400": Row("k: 1e-400\n", "unchanged"),
    "duplicate key": Row("a: 1\na: 2\n", "refused", r"p\.yaml: a: a duplicate key"),
    "merge key": Row(
        "base: &b {x: 1, y: 2}\nm:\n  <<: *b\n  y: 3\n",
        "refused",
        r"p\.yaml: m\.x: PyYAML reads this key and the core does not \(a merge key",
    ),
    "alias without merge": Row("a: &x {v: off, w: 010}\nb: *x\n", "repaired"),
    "block literal": Row("s: |\n  line one\n  off\n", "unchanged"),
    "block literal strip": Row("s: |-\n  off\n", "unchanged"),
    "block folded": Row("s: >\n  010\n  more\n", "unchanged"),
    "key on": Row("on: 1\n", "refused", r"p\.yaml: the top level: PyYAML reads a key as True"),
    "key 010": Row("m: {010: 1}\n", "refused", r"p\.yaml: m: PyYAML reads a key as 8 \(int\)"),
    "key null": Row("~: 1\n", "refused", r"p\.yaml: the top level: PyYAML reads a key as None"),
    "dotted key": Row("a.b: 1\n", "refused", r"p\.yaml: a\.b: .*containing '\.'"),
    "sequence": Row("v: [off, 1.0e3, 010, [yes], {k: on}]\n", "repaired"),
    "empty containers": Row("m: {}\nn: []\n", "unchanged"),
}


def _outcomes(config: _core.Config, path: str) -> tuple[str, ...]:
    """Every getter's outcome at ``path``, numbers as their bits so -0.0 and NaN compare exactly."""
    out: list[str] = []
    for getter in GETTERS:
        try:
            value = getattr(config, getter)(path)
        except _core.ConfigError:
            out.append("refused")
            continue
        if isinstance(value, float):
            out.append("nan" if math.isnan(value) else struct.pack("<d", value).hex())
        else:
            out.append(repr(value))
    return tuple(out)


@pytest.mark.parametrize("name", list(PROBE_TABLE))
def test_pyyaml_route_probe_table(name: str) -> None:
    row = PROBE_TABLE[name]
    if row.outcome == "refused":
        with pytest.raises(ValueError, match=row.refusal):
            _yaml_route.load(row.doc, "p.yaml", error=ValueError)
        return
    data = _yaml_route.load(row.doc, "p.yaml", error=ValueError)
    read = yaml.safe_load(row.doc)
    if row.outcome == "unchanged":
        # Exactly as PyYAML read it (NaN compared by its repr).
        assert repr(data) == repr(read)
        return
    assert repr(data) != repr(read)
    core = _core.Config.from_string(row.doc, "p.yaml")
    routed = _core.Config.from_dict(data, origin="p.yaml")
    leaves = core.unknown_keys()
    assert routed.unknown_keys() == leaves
    for path in leaves:
        assert _outcomes(routed, path) == _outcomes(core, path), path


def test_pyyaml_route_tracked_configs_need_no_repair() -> None:
    """D5's premise: every config in the repository comes back exactly as PyYAML read it."""
    patterns = (
        "aircraft/*/configs/*.yaml",
        "tests/configs/*.yaml",
        "tests/physics/nesc/*.yaml",
        "tests/golden/dynamics/*.yaml",
        "tests/python/*_fixtures/*.yaml",
    )
    checked = 0
    for pattern in patterns:
        for path in sorted(REPO.glob(pattern)):
            text = path.read_text(encoding="utf-8")
            read = yaml.safe_load(text)
            if not isinstance(read, dict) or not {"sim", "vehicle", "physical"} & set(read):
                continue  # a spec, scenario or plot package: Python's own format
            assert repr(_yaml_route.load(text, str(path), error=ValueError)) == repr(read), path
            checked += 1
    assert checked >= 30


# --- the variants ---------------------------------------------------------------------------------


def _variant(base: Path, old: str, new: str, directory: Path, name: str = "config.yaml") -> Path:
    text = base.read_text(encoding="utf-8")
    assert text.count(old) == 1, old
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / name
    path.write_text(text.replace(old, new), encoding="utf-8")
    return path


DESCENT_SIM = "sim:\n  dt: 0.001\n"
DESCENT_MASS = "mass: 10.0, ixx"
DESCENT_FORCE = "force_x: 1.0 }"


def _health(form: str) -> tuple[str, str]:
    return DESCENT_SIM, f"sim:\n  health:\n    nan: {form}\n  dt: 0.001\n"


# (the text replaced, its replacement) per form, on descent.yaml.
DESCENT_SAME_AS_QUOTED = {
    "off": (_health("off"), _health("'off'")),
    "010": (
        (DESCENT_FORCE, "force_x: 010 }"),
        (DESCENT_FORCE, "force_x: '010' }"),
    ),
}
DESCENT_REFUSED_AT_BUILD = {
    form: (DESCENT_MASS, f"mass: {form}, ixx")
    for form in ("1_000", "1:30", "0x10", "0b101", "2026-10-03")
}
DESCENT_REFUSED_AT_READ = {
    "null": ((DESCENT_MASS, "mass: ~, ixx"), r"physical\.mass\.mass: a null"),
    "duplicate key": (
        (DESCENT_MASS, "mass: 10.0, mass: 11.0, ixx"),
        r"physical\.mass\.mass: a duplicate key",
    ),
    "merge key": (
        (
            "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, "
            "izz: 1.0 }\n",
            "  mass:\n    <<: { plugin: plugins/physical/mass/constant, mass: 10.0 }\n"
            "    ixx: 1.0\n    iyy: 1.0\n    izz: 1.0\n",
        ),
        r"physical\.mass\.\w+: PyYAML reads this key and the core does not \(a merge key",
    ),
    "key on": (
        (DESCENT_FORCE, "force_x: 1.0, on: 1 }"),
        r"vehicle\.models\.0: PyYAML reads a key as True",
    ),
}
# The core's own refusal of each form at physical.mass.mass, as Config.from_string gives it.
BUILD_REFUSAL = r"at 'physical\.mass\.mass': expected a number value"


# --- astra.montecarlo -----------------------------------------------------------------------------


def _spec(directory: Path, config: Path, dispersions: str = "  physical.wind.seed: seed\n") -> Path:
    path = directory / "spec.yaml"
    path.write_text(
        f"config: {config}\nduration: 0.05\nruns: 1\nseed: 11\ndispersions:\n{dispersions}",
        encoding="utf-8",
    )
    return path


def _mc_run(
    directory: Path, replacement: tuple[str, str], dispersions: str | None = None
) -> tuple[dict[str, Any], bytes]:
    config = _variant(DESCENT, *replacement, directory)
    spec = (
        montecarlo.load(_spec(directory, config))
        if dispersions is None
        else montecarlo.load(_spec(directory, config, dispersions))
    )
    out = directory / "out"
    out.mkdir()
    row = montecarlo.run_one(spec, 0, out)
    return row, (out / montecarlo.log_name(0)).read_bytes()


@pytest.mark.parametrize("form", list(DESCENT_SAME_AS_QUOTED))
def test_pyyaml_route_montecarlo_same_as_quoted(form: str, tmp_path: Path) -> None:
    plain, quoted = DESCENT_SAME_AS_QUOTED[form]
    row, log_bytes = _mc_run(tmp_path / "plain", plain)
    quoted_row, quoted_log = _mc_run(tmp_path / "quoted", quoted)
    assert row["status"] == "ok"
    assert repr(row) == repr(quoted_row)
    assert log_bytes == quoted_log


@pytest.mark.parametrize("form", list(DESCENT_REFUSED_AT_BUILD))
def test_pyyaml_route_montecarlo_refused_at_build(form: str, tmp_path: Path) -> None:
    config = _variant(DESCENT, *DESCENT_REFUSED_AT_BUILD[form], tmp_path)
    with pytest.raises(_core.ConfigError, match=BUILD_REFUSAL):  # what astra run says
        _core.Simulation.from_config(_core.Config.from_file(config), _core.default_plugin_dir())
    with pytest.raises(montecarlo.SpecError, match=f"the config does not build: .*{BUILD_REFUSAL}"):
        montecarlo.load(_spec(tmp_path, config))


@pytest.mark.parametrize("form", list(DESCENT_REFUSED_AT_READ))
def test_pyyaml_route_montecarlo_refused_at_read(form: str, tmp_path: Path) -> None:
    replacement, message = DESCENT_REFUSED_AT_READ[form]
    config = _variant(DESCENT, *replacement, tmp_path)
    with pytest.raises(montecarlo.SpecError, match=f"spec.yaml: config: .*config.yaml: {message}"):
        montecarlo.load(_spec(tmp_path, config))


def test_pyyaml_route_montecarlo_dispersion_base_kept_as_text(tmp_path: Path) -> None:
    config = _variant(DESCENT, DESCENT_FORCE, "force_x: 010 }", tmp_path)
    dispersions = "  vehicle.models.0.force_x: {uniform: {lo: 0.0, hi: 5.0}}\n"
    with pytest.raises(
        montecarlo.SpecError,
        match=r"dispersions\.vehicle\.models\.0\.force_x: the config's value '010' is not a "
        r"number \(text to PyYAML or to the core",
    ):
        montecarlo.load(_spec(tmp_path, config, dispersions))


def test_pyyaml_route_montecarlo_number_written_two_ways(tmp_path: Path) -> None:
    """1.0e+3 and 1000.0 are one number: not repaired, so the leaf stays dispersable."""
    dispersions = "  vehicle.models.0.force_x: {normal: {sigma: 0.1, mode: relative, clip: 3.0}}\n"
    results = [
        _mc_run(tmp_path / name, (DESCENT_FORCE, f"force_x: {form} }}"), dispersions)
        for name, form in (("exp", "1.0e+3"), ("plain", "1000.0"))
    ]
    assert results[0][0]["status"] == "ok"
    assert repr(results[0][0]) == repr(results[1][0])
    assert results[0][1] == results[1][1]


def test_pyyaml_route_rerun_of_a_batch_read_by_pyyaml(tmp_path: Path) -> None:
    """D4: a batch written before M6 PR 11 with force_x: 010 ran with PyYAML's 8. Its rerun reads
    the recorded config as the core does (10.0), and reports the rows that differ; the report,
    which never parses the config, still renders."""
    config = _variant(DESCENT, DESCENT_FORCE, "force_x: 8 }", tmp_path)
    batch = tmp_path / "batch"
    montecarlo.run(montecarlo.load(_spec(tmp_path, config)), batch, workers=1)
    summary = batch / montecarlo.SUMMARY
    table = pq.read_table(summary)
    metadata = dict(table.schema.metadata or {})
    recorded = metadata[montecarlo.CONFIG_KEY]
    assert recorded.count(b"force_x: 8 }") == 1
    metadata[montecarlo.CONFIG_KEY] = recorded.replace(b"force_x: 8 }", b"force_x: 010 }")
    pq.write_table(table.replace_schema_metadata(metadata), summary)

    result = montecarlo.rerun(batch, 0, tmp_path / "rerun")
    assert result.differences
    assert any(column.startswith("final.") for column in result.differences)
    assert result.log_identical is False
    text = report.batch_report(mc_analysis.load_batch(batch), source="batch")
    assert "force_x: 010" in text


# --- astra.scenario -------------------------------------------------------------------------------


def _scenario(directory: Path, config: Path, extra: str = "") -> Path:
    path = directory / "case.yaml"
    path.write_text(f"config: {config}\nduration: 0.05\n{extra}", encoding="utf-8")
    return path


def _scenario_run(directory: Path, replacement: tuple[str, str]) -> tuple[list[str], bytes]:
    config = _variant(DESCENT, *replacement, directory)
    case = scenario.load(_scenario(directory, config))
    result = scenario.run(case, directory / "run.h5")
    return result.failures, (directory / "run.h5").read_bytes()


@pytest.mark.parametrize("form", list(DESCENT_SAME_AS_QUOTED))
def test_pyyaml_route_scenario_same_as_quoted(form: str, tmp_path: Path) -> None:
    plain, quoted = DESCENT_SAME_AS_QUOTED[form]
    failures, log_bytes = _scenario_run(tmp_path / "plain", plain)
    assert failures == []
    assert (failures, log_bytes) == _scenario_run(tmp_path / "quoted", quoted)


@pytest.mark.parametrize("form", list(DESCENT_REFUSED_AT_BUILD))
def test_pyyaml_route_scenario_refused_at_build(form: str, tmp_path: Path) -> None:
    config = _variant(DESCENT, *DESCENT_REFUSED_AT_BUILD[form], tmp_path)
    with pytest.raises(
        scenario.ScenarioError, match=f"the config does not build: .*{BUILD_REFUSAL}"
    ):
        scenario.load(_scenario(tmp_path, config))


@pytest.mark.parametrize("form", list(DESCENT_REFUSED_AT_READ))
def test_pyyaml_route_scenario_refused_at_read(form: str, tmp_path: Path) -> None:
    replacement, message = DESCENT_REFUSED_AT_READ[form]
    config = _variant(DESCENT, *replacement, tmp_path)
    match = f"case.yaml: config: .*config.yaml: {message}"
    with pytest.raises(scenario.ScenarioError, match=match):
        scenario.load(_scenario(tmp_path, config))


def test_pyyaml_route_scenario_override_target_kept_as_text(tmp_path: Path) -> None:
    config = _variant(DESCENT, DESCENT_FORCE, "force_x: 010 }", tmp_path)
    case = _scenario(tmp_path, config, "overrides: {vehicle.models.0.force_x: 2.0}\n")
    with pytest.raises(
        scenario.ScenarioError,
        match=r"overrides\.vehicle\.models\.0\.force_x: the config's value '010' is not a number "
        r"\(text to PyYAML or to the core",
    ):
        scenario.load(case)


def test_pyyaml_route_scenario_number_written_two_ways(tmp_path: Path) -> None:
    results = [
        _scenario_run(tmp_path / name, (DESCENT_FORCE, f"force_x: {form} }}"))
        for name, form in (("exp", "1.0e+3"), ("plain", "1000.0"))
    ]
    assert results[0][0] == []
    assert results[0] == results[1]


def test_pyyaml_route_scenario_trimmed_same_as_quoted(tmp_path: Path) -> None:
    """A repaired config through trim: _runkit.apply_trim safe-dumps the dict to a copy, which
    astra.trim reads through _yaml_route again; the run equals the quoted spelling's."""
    results: list[tuple[list[str], bytes]] = []
    for name, form in (("plain", "off"), ("quoted", "'off'")):
        directory = tmp_path / name
        config = _variant(
            CRUISE,
            "sim:\n  dt: 0.001\n  frames: 1000\n",
            f"sim:\n  health: {{nan: {form}}}\n  dt: 0.001\n  frames: 1000\n",
            directory,
        )
        trimmed = "trim: {true_airspeed: 230.0, altitude: 10000.0}\n"
        case = scenario.load(_scenario(directory, config, trimmed))
        result = scenario.run(case, directory / "run.h5")
        results.append((result.failures, (directory / "run.h5").read_bytes()))
    assert results[0][0] == []
    assert results[0] == results[1]


# --- astra.trim -----------------------------------------------------------------------------------

CRUISE_SIM = "sim:\n  dt: 0.001\n  frames: 1000\n"
CRUISE_IYY = "iyy: 1997120.0 "
TRIM_SAME_AS_QUOTED = {
    "off": (
        (CRUISE_SIM, "sim:\n  health: {nan: off}\n  dt: 0.001\n  frames: 1000\n"),
        (CRUISE_SIM, "sim:\n  health: {nan: 'off'}\n  dt: 0.001\n  frames: 1000\n"),
    ),
    "010": ((CRUISE_IYY, "iyy: 010 "), (CRUISE_IYY, "iyy: '010' ")),
}


def _trim(directory: Path, replacement: tuple[str, str]) -> trim.TrimResult:
    return trim.trim_straight_and_level(_variant(CRUISE, *replacement, directory), 230.0, 10000.0)


@pytest.mark.parametrize("form", list(TRIM_SAME_AS_QUOTED))
def test_pyyaml_route_trim_same_as_quoted(form: str, tmp_path: Path) -> None:
    plain, quoted = TRIM_SAME_AS_QUOTED[form]
    result = _trim(tmp_path / "plain", plain)
    expected = _trim(tmp_path / "quoted", quoted)
    assert result.state.tobytes() == expected.state.tobytes()
    assert (result.elevator_rad, result.throttle, result.alpha_rad) == (
        expected.elevator_rad,
        expected.throttle,
        expected.alpha_rad,
    )
    assert result.residual.tobytes() == expected.residual.tobytes()


def test_pyyaml_route_trim_refusals(tmp_path: Path) -> None:
    with pytest.raises(_core.ConfigError, match=r"physical\.mass\.mass: a null"):
        _trim(tmp_path / "null", ("mass: 37648.0 ", "mass: ~ "))
    with pytest.raises(
        _core.ConfigError, match=r"at 'physical\.mass\.mass': expected a number value"
    ):
        _trim(tmp_path / "underscore", ("mass: 37648.0 ", "mass: 37_648.0 "))


def test_pyyaml_route_trim_cli_refusal_exits_65(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    config = _variant(CRUISE, "mass: 37648.0 ", "mass: ~ ", tmp_path)
    assert cli.main(["trim", str(config), "--airspeed", "230", "--altitude", "10000"]) == 65
    assert re.search(r"physical\.mass\.mass: a null", capsys.readouterr().err)


# --- astra.lockstep -------------------------------------------------------------------------------

LIGHT_SIM = "sim:\n  dt: 0.001\n"
LOCKSTEP_SAME_AS_QUOTED = {
    "off": (
        (LIGHT_SIM, "sim:\n  health: {nan: off}\n  dt: 0.001\n"),
        (LIGHT_SIM, "sim:\n  health: {nan: 'off'}\n  dt: 0.001\n"),
    ),
    "010": ((CRUISE_IYY, "iyy: 010 "), (CRUISE_IYY, "iyy: '010' ")),
}


@pytest.mark.parametrize("form", list(LOCKSTEP_SAME_AS_QUOTED))
def test_pyyaml_route_lockstep_same_as_quoted(form: str, tmp_path: Path) -> None:
    plain, quoted = LOCKSTEP_SAME_AS_QUOTED[form]
    segment = ls.new_segment_name()
    paths = [_variant(LIGHT, *r, tmp_path / n) for n, r in (("plain", plain), ("quoted", quoted))]
    configs: list[dict[str, Any]] = [ls.load_config(p, segment=segment) for p in paths]
    assert configs[0] == configs[1]
    trims = [ls.trim_for(p, SETPOINTS) for p in paths]
    assert trims[0] == trims[1]


def test_pyyaml_route_lockstep_refusals(tmp_path: Path) -> None:
    null = _variant(LIGHT, "mass: 37648.0 ", "mass: ~ ", tmp_path / "null")
    with pytest.raises(ls.LockstepError, match=r"physical\.mass\.mass: a null"):
        ls.load_config(null, segment=ls.new_segment_name())
    with pytest.raises(ls.LockstepError, match=r"physical\.mass\.mass: a null"):
        ls.trim_for(null, SETPOINTS)
    # The core's integer() refuses 1_000, and the repaired text is refused the same way here.
    underscore = _variant(LIGHT, "timeout_ms: 1000 ", "timeout_ms: 1_000 ", tmp_path / "underscore")
    match = r"transport\.timeout_ms must be given as an integer"
    with pytest.raises(ls.LockstepError, match=match):
        ls.load_config(underscore, segment=ls.new_segment_name())
