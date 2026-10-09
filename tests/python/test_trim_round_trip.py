"""Every trim consumer's round trip, residual policy and new options (M7 PR 5,
docs/plans/m7-pr5-trim.md: "Round trips (C-03) and the residual policy (N-03), per consumer"; the M7
plan's R4).

A config rebuilt from what each consumer emits (``astra trim``'s stdout, ``_runkit.apply_trim``'s
dict, ``astra.linearize``'s trim, ``astra.lockstep.trim_for``'s fragment) reproduces the solved
state, controls, altitude and residuals bit for bit: ``astra.trim`` returns the state the core
itself builds from the emitted ``euler_nb`` (critique P-08). Conditions: cruise, heading 45 deg at
36 deg N, case 15's start, and a flat Earth, under both rate conventions (Joe's decision (b),
2026-10-08: ``zero_inertial`` the default, ``n_frame`` an option on every consumer).
"""

from __future__ import annotations

import copy
import math
from pathlib import Path
from typing import Any, cast

import numpy as np
import pytest
import yaml

import astra.trim
from astra import _core, _runkit, cli, linearize, lockstep, montecarlo, scenario

ROOT = Path(__file__).resolve().parents[2]
STILL_AIR = ROOT / "aircraft/notional_transport/configs/cruise_still_air.yaml"
LIGHT = ROOT / "aircraft/notional_transport/configs/cruise_light_turbulence.yaml"
TEMPLATE = ROOT / "aircraft/_template/configs/cruise.yaml"

# (name, latitude rad, longitude rad, heading rad, true airspeed m/s, altitude m, flat Earth)
CONDITIONS = [
    ("cruise", 0.0, 0.0, 0.0, 230.0, 10_000.0, False),
    (
        "36N-heading-45",
        math.radians(36.019),
        math.radians(-75.0),
        math.radians(45.0),
        230.0,
        9_000.0,
        False,
    ),
    (
        "case-15",
        math.radians(89.95),
        math.radians(-45.0),
        math.radians(90.0),
        172.4,
        3_048.0,
        False,
    ),
    ("flat", 0.0, 0.0, math.radians(-120.0), 230.0, 10_000.0, True),
]
CONVENTIONS = astra.trim.RATE_CONVENTIONS


def base_data(lat: float, lon: float, alt: float, flat: bool) -> dict[str, Any]:
    data = cast(dict[str, Any], yaml.safe_load(STILL_AIR.read_text(encoding="utf-8")))
    if flat:
        data["physical"]["earth"] = "plugins/physical/earth/flat"
        data["physical"]["gravity"] = "plugins/physical/gravity/constant"
        data["initial"]["state"]["p_n"] = [0.0, 0.0, -alt]
    else:
        data["initial"]["state"]["p_n"] = [lat, lon, alt]
    return data


def write(tmp_path: Path, data: dict[str, Any], name: str = "config.yaml") -> Path:
    path = tmp_path / name
    path.write_text(yaml.safe_dump(data, sort_keys=False), encoding="utf-8")
    return path


def rebuilt(data: dict[str, Any]) -> _core.Simulation:
    return _core.Simulation.from_config(
        _core.Config.from_dict(data, origin="round trip"), _core.default_plugin_dir()
    )


def assert_reproduces(
    data: dict[str, Any], result: astra.trim.TrimResult, alt: float, flat: bool
) -> None:
    """``data``, the base config with a consumer's output applied, rebuilds ``result`` bit for bit:
    the initial state, every control, the altitude and the residual."""
    sim = rebuilt(data)
    state = np.array(sim.vehicles[0].state[: _core.STATE_SIZE])
    assert np.array_equal(state, result.state)
    config = _core.Config.from_dict(data, origin="round trip")
    for name in sim.input_names():
        assert config.number(f"vehicle.initial_controls.{name}") == result.controls[name], name
    altitude = -state[_core.POSITION + 2] if flat else state[_core.POSITION + 2]
    assert altitude == alt
    derivative = np.array(sim.derivative(state))
    residual = np.concatenate(
        [
            derivative[_core.VELOCITY : _core.VELOCITY + _core.VELOCITY_SIZE],
            derivative[_core.ANGULAR_RATE : _core.ANGULAR_RATE + _core.ANGULAR_RATE_SIZE],
        ]
    )
    assert np.array_equal(residual, result.residual)


def merged(data: dict[str, Any], state: dict[str, Any], controls: dict[str, Any]) -> dict[str, Any]:
    """``data`` with an emitted fragment applied, as a user applies ``astra trim``'s output: the
    initial state's entries replaced (``q_nb`` dropped), the controls merged by name."""
    out = copy.deepcopy(data)
    values = out["initial"]["state"]
    values.pop("q_nb", None)
    values.update(state)
    _runkit.merge_controls(out["vehicle"]["initial_controls"], controls)
    return out


IDS = [f"{c[0]}-{k}" for c in CONDITIONS for k in CONVENTIONS]
CASES = [(c, k) for c in CONDITIONS for k in CONVENTIONS]


@pytest.mark.parametrize(("condition", "convention"), CASES, ids=IDS)
def test_astra_trim_output_rebuilds_the_trim(
    tmp_path: Path, capsys: pytest.CaptureFixture[str], condition: Any, convention: str
) -> None:
    _, lat, lon, psi, speed, alt, flat = condition
    data = base_data(lat, lon, alt, flat)
    path = write(tmp_path, data)
    args = ["--airspeed", repr(speed), "--altitude", repr(alt), "--heading", repr(psi)]
    code = cli.main(["trim", str(path), *args, "--rate-convention", convention])
    captured = capsys.readouterr()
    assert (code, captured.err) == (0, "")
    document = yaml.safe_load(captured.out)
    result = astra.trim.trim_straight_and_level(
        path,
        speed,
        alt,
        heading_rad=psi,
        rate_convention=cast(astra.trim.RateConvention, convention),
    )
    assert document["residual"] == [float(v) for v in result.residual]
    rebuilt_data = merged(
        data, document["initial"]["state"], document["vehicle"]["initial_controls"]
    )
    assert_reproduces(rebuilt_data, result, alt, flat)


@pytest.mark.parametrize(("condition", "convention"), CASES, ids=IDS)
def test_apply_trim_output_rebuilds_the_trim(
    tmp_path: Path, condition: Any, convention: str
) -> None:
    # astra.scenario's and astra.montecarlo's route.
    _, lat, lon, psi, speed, alt, flat = condition
    data = base_data(lat, lon, alt, flat)
    setpoint = _runkit.TrimSetpoint(speed, alt, psi, cast(astra.trim.RateConvention, convention))
    applied = copy.deepcopy(data)
    _runkit.apply_trim(applied, setpoint, origin=tmp_path / "spec.yaml", config_name="config.yaml")
    result = astra.trim.trim_straight_and_level(
        write(tmp_path, data),
        speed,
        alt,
        heading_rad=psi,
        rate_convention=cast(astra.trim.RateConvention, convention),
    )
    assert_reproduces(applied, result, alt, flat)


@pytest.mark.parametrize("convention", CONVENTIONS)
def test_linearize_trim_rebuilds_and_its_x0_u0_follow_it(tmp_path: Path, convention: str) -> None:
    lat, lon, psi = math.radians(36.019), math.radians(-75.0), math.radians(45.0)
    data = base_data(lat, lon, 9_000.0, False)
    result = linearize.linearize(
        write(tmp_path, data),
        230.0,
        9_000.0,
        heading_rad=psi,
        rate_convention=cast(astra.trim.RateConvention, convention),
    )
    state, controls = _runkit.trim_fragment(result.trim)
    assert_reproduces(merged(data, state, controls), result.trim, 9_000.0, False)
    rates = result.trim.state[_core.ANGULAR_RATE : _core.ANGULAR_RATE + _core.ANGULAR_RATE_SIZE]
    assert np.array_equal(result.x0[3:6], rates)
    assert list(result.x0[6:9]) == [0.0, result.trim.alpha_rad, psi]
    assert list(result.u0) == [result.trim.controls[name] for name in result.input_names]


@pytest.mark.parametrize("convention", CONVENTIONS)
def test_trim_for_fragment_rebuilds_the_trim_and_its_biases_follow_it(
    tmp_path: Path, convention: str
) -> None:
    psi = math.radians(30.0)
    setpoints = lockstep.Setpoints(altitude_m=10_000.0, airspeed_mps=230.0, heading_rad=psi)
    trim = lockstep.trim_for(
        LIGHT, setpoints, rate_convention=cast(astra.trim.RateConvention, convention)
    )
    data = cast(dict[str, Any], yaml.safe_load(LIGHT.read_text(encoding="utf-8")))
    data.pop("transport")
    data["physical"].pop("wind")
    result = astra.trim.trim_straight_and_level(
        write(tmp_path, data),
        230.0,
        10_000.0,
        heading_rad=psi,
        rate_convention=cast(astra.trim.RateConvention, convention),
    )
    assert_reproduces(
        merged(data, trim.initial_state, trim.initial_controls), result, 10_000.0, False
    )
    assert (trim.elevator_rad, trim.throttle, trim.theta_rad) == (
        result.elevator_rad,
        result.throttle,
        result.alpha_rad,
    )
    assert trim.initial_state["euler_nb"] == [psi, result.alpha_rad, 0.0]


def test_the_default_convention_is_zero_inertial_everywhere() -> None:
    # Joe's decision (b): rate_convention is an option on every consumer, zero_inertial the default.
    assert astra.trim.DEFAULT_RATE_CONVENTION == "zero_inertial"
    assert cli._RATE_CONVENTIONS == astra.trim.RATE_CONVENTIONS  # pyright: ignore[reportPrivateUsage]
    assert _runkit.TrimSetpoint(230.0, 10_000.0).rate_convention == "zero_inertial"
    result = astra.trim.trim_straight_and_level(STILL_AIR, 230.0, 10_000.0)
    assert result.rate_convention == "zero_inertial"
    assert np.all(result.state[_core.ANGULAR_RATE : _core.ANGULAR_RATE + 3] == 0.0)


def test_the_setpoint_reads_heading_and_rate_convention() -> None:
    spec = {
        "true_airspeed": 230.0,
        "altitude": 10_000.0,
        "heading": 0.5,
        "rate_convention": "n_frame",
    }
    assert _runkit.trim_setpoint(Path("s.yaml"), spec, error=ValueError) == _runkit.TrimSetpoint(
        230.0, 10_000.0, 0.5, "n_frame"
    )
    for error in (scenario.ScenarioError, montecarlo.SpecError):
        with pytest.raises(error, match=r"trim\.rate_convention: must be one of"):
            _runkit.trim_setpoint(Path("s.yaml"), {**spec, "rate_convention": "body"}, error=error)
        with pytest.raises(error, match=r"trim\.heading: must be a number"):
            _runkit.trim_setpoint(Path("s.yaml"), {**spec, "heading": "north"}, error=error)


def test_astra_trim_defaults_the_heading_to_the_configs_own(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    data = base_data(math.radians(36.019), 0.0, 9_000.0, False)
    data["initial"]["state"]["euler_nb"] = [math.radians(-135.0), 0.02, 0.0]
    path = write(tmp_path, data)
    code = cli.main(["trim", str(path), "--airspeed", "230", "--altitude", "9000"])
    document = yaml.safe_load(capsys.readouterr().out)
    assert code == 0
    assert document["initial"]["state"]["euler_nb"][0] == pytest.approx(
        math.radians(-135.0), abs=1e-15
    )


def test_astra_trim_defaults_a_flat_earths_altitude_to_minus_p_n_2(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    path = write(tmp_path, base_data(0.0, 0.0, 7_000.0, True))
    code = cli.main(["trim", str(path), "--airspeed", "230"])
    document = yaml.safe_load(capsys.readouterr().out)
    assert code == 0
    assert document["altitude"] == 7_000.0
    assert document["initial"]["state"]["p_n"] == [0.0, 0.0, -7_000.0]


def test_the_solver_refuses_an_input_the_models_do_not_register(tmp_path: Path) -> None:
    path = write(tmp_path, base_data(0.0, 0.0, 10_000.0, False))
    with pytest.raises(ValueError, match=r"input 'controls\.flap' is not registered"):
        astra.trim.trim_straight_and_level(path, 230.0, 10_000.0, throttle_input="controls.flap")


def test_the_template_no_longer_exits_65_for_its_earth_model(
    capsys: pytest.CaptureFixture[str],
) -> None:
    # PLAN.md's §18 step 4 item: the template's earth/flat config was refused for its Earth model.
    # It still exits 65, for its first TODO zero, the aero's wing area, which the plugin refuses.
    code = cli.main(["trim", str(TEMPLATE), "--airspeed", "230", "--altitude", "1000"])
    stderr = capsys.readouterr().err
    assert code == 65
    assert "'vehicle.models.0.wing_area_m2'" in stderr
    assert "physical.earth" not in stderr


def test_a_filled_in_flat_earth_template_trims(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    # The template's own plugins, with the notional transport's numbers where it has TODO zeros.
    template = cast(dict[str, Any], yaml.safe_load(TEMPLATE.read_text(encoding="utf-8")))
    source = cast(dict[str, Any], yaml.safe_load(STILL_AIR.read_text(encoding="utf-8")))
    for mine, theirs in zip(
        template["vehicle"]["models"], source["vehicle"]["models"], strict=True
    ):
        mine.update({k: v for k, v in theirs.items() if k != "plugin"})
    template["vehicle"]["initial_controls"] = source["vehicle"]["initial_controls"]
    mass = {
        k: v for k, v in source["physical"]["mass"].items() if k in ("mass", "ixx", "iyy", "izz")
    }
    template["physical"]["mass"].update(mass)
    template["initial"]["state"] = {
        "p_n": [0.0, 0.0, -10_000.0],
        "v_b": [230.0, 0.0, 0.0],
        "euler_nb": [0.0, 0.0, 0.0],
        "omega_b": [0.0, 0.0, 0.0],
    }
    code = cli.main(["trim", str(write(tmp_path, template))])
    captured = capsys.readouterr()
    assert (code, captured.err) == (0, "")
    document = yaml.safe_load(captured.out)
    assert document["altitude"] == 10_000.0
    assert max(abs(document["residual"][i]) for i in (0, 2, 4)) <= 1e-6
