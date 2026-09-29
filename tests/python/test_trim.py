"""astra.trim (M3 PR 6, docs/plans/pr6-trim.md): four named acceptance tests plus two supporting
checks needed to trust them. R1: residual < 1e-6 at cruise. R2: the residual is discriminating (the
untrimmed initial guess fails by a wide margin). R3: the solution is physically plausible (alpha,
elevator, throttle in range, and thrust/drag agree via the independent wind-axis cross-check
T cos(alpha) = D -- not the naive, and wrong, T = D + m g sin(alpha), which double-counts the
weight term already absorbed into D's own z-axis equilibrium, per the plan's own Revision 2
correction). R4: determinism (two runs from the same guess are bitwise identical). Supporting:
the collapsed-quaternion helper matches the real `Simulation.from_config` construction; the
still-air config genuinely has no `physical.wind`.
"""

from __future__ import annotations

import math
from pathlib import Path

import numpy as np
import pytest
import yaml

from astra import _core, trim

CRUISE_CONFIG = "aircraft/notional_transport/configs/cruise.yaml"
TRUE_AIRSPEED_MPS = 230.0
ALTITUDE_M = 10_000.0

# D10 (docs/plans/pr6-trim.md, approved): neither range is a plugin/config value -- elevator from
# docs/refs/aircrafts/737.xml's "Elevator Control" aerosurface_scale (lines 201-206, main checkout);
# throttle from icd/astra_icd.h:46's documented convention, not a config key (turbofan.cpp's own
# comment: throttle is deliberately not range-checked by the plugin).
ELEVATOR_RANGE_RAD = (-0.3, 0.3)
THROTTLE_RANGE = (0.0, 1.0)

# cruise.yaml's own transcribed coefficients (aircraft/notional_transport/physical/aero's config),
# re-typed here for R3's independent cross-check -- never reusing aero_math.hpp/turbofan_math.hpp.
WING_AREA_M2 = 108.78945984
CD_0 = 0.021
CD_K = 0.043
MASS_KG = 37648.0
THRUST_REF_N = 29878.121867743088
DENSITY_REF_KGM3 = 0.4128
STANDARD_GRAVITY_MPS2 = 9.80665


def _isa1976_density_kg_m3(altitude_m: float) -> float:
    """Sea-level-to-tropopause (0-11 km) ISA-1976 density, independent of
    plugins/physical/atmosphere/isa1976 -- a second, from-scratch evaluation of the same closed-form
    troposphere equations (U.S. Standard Atmosphere, 1976, NOAA-S/T 76-1562), the same
    independent-implementation convention tests/golden/atmosphere/generate_isa1976_table.py uses for
    its own oracle, not imported from it (that script is a human-owned golden generator, a different
    concern from this plausibility check)."""
    g0 = STANDARD_GRAVITY_MPS2
    specific_gas_constant = 8314.32 / 28.9644  # R*/M0, doc Table 2
    sea_level_temperature_k = 288.15
    sea_level_pressure_pa = 101_325.0
    lapse_rate_k_per_m = -0.0065
    temperature_k = sea_level_temperature_k + lapse_rate_k_per_m * altitude_m
    pressure_pa = sea_level_pressure_pa * (temperature_k / sea_level_temperature_k) ** (
        -g0 / (specific_gas_constant * lapse_rate_k_per_m)
    )
    return pressure_pa / (specific_gas_constant * temperature_k)


def test_trim_converges_at_cruise() -> None:
    result = trim.trim_straight_and_level(CRUISE_CONFIG, TRUE_AIRSPEED_MPS, ALTITUDE_M)
    assert np.max(np.abs(result.residual)) < 1e-6


def test_untrimmed_guess_fails_by_a_wide_margin() -> None:
    # The untrimmed guess is v_b=[230,0,0] i.e. alpha=0, elevator=0, throttle=0.5 -- cruise.yaml's
    # own initial.state/initial_controls before M3 PR 8 wrote the trim into it (D19,
    # docs/plans/pr8-cruise-regression.md), so they are overridden back here. Hand-estimate: at
    # alpha=0, CL = cl_0 = 0.20 against a required CL ~ 0.31 for level flight, a lift deficit of
    # order 1e5 N, order 1 m/s^2 of v_dot_z -- four-plus orders of magnitude above both the 1e-6
    # (R1) and this test's own 1e-2 threshold.
    data = yaml.safe_load(Path(CRUISE_CONFIG).read_text(encoding="utf-8"))
    data["physical"].pop("wind", None)  # still air, as trim._load_still_air_config does
    data["initial"]["state"]["v_b"] = [TRUE_AIRSPEED_MPS, 0.0, 0.0]
    data["initial"]["state"]["euler_nb"] = [0.0, 0.0, 0.0]
    data["vehicle"]["initial_controls"]["controls"]["elevator"] = 0.0
    data["vehicle"]["initial_controls"]["controls"]["throttle"] = 0.5
    config = _core.Config.from_dict(data, origin=CRUISE_CONFIG)
    sim = _core.Simulation.from_config(config, _core.default_plugin_dir())
    state = np.array(sim.vehicles[0].state)
    residual = trim._residual6(sim, state)
    assert np.max(np.abs(residual)) > 1e-2


def test_solution_is_physically_plausible() -> None:
    result = trim.trim_straight_and_level(CRUISE_CONFIG, TRUE_AIRSPEED_MPS, ALTITUDE_M)

    alpha_deg = math.degrees(result.alpha_rad)
    assert 0.0 <= alpha_deg <= 10.0
    assert ELEVATOR_RANGE_RAD[0] <= result.elevator_rad <= ELEVATOR_RANGE_RAD[1]
    assert THROTTLE_RANGE[0] <= result.throttle <= THROTTLE_RANGE[1]

    # Independent cross-check: the classical wind-axis identity T*cos(alpha) = D (exact at
    # gamma=0, omega_b=0, beta=0 -- combining the body-x and body-z EOM equations; NOT
    # T = D + m*g*sin(alpha), which double-counts the weight term already absorbed into D's own
    # z-axis equilibrium). D is hand-computed from cruise.yaml's own CL/CD coefficients and an
    # independent ISA-1976 density, never from aero_math.hpp/turbofan_math.hpp.
    density_kg_m3 = _isa1976_density_kg_m3(ALTITUDE_M)
    dynamic_pressure_pa = 0.5 * density_kg_m3 * TRUE_AIRSPEED_MPS**2
    weight_n = MASS_KG * STANDARD_GRAVITY_MPS2
    cl = weight_n * math.cos(result.alpha_rad) / (dynamic_pressure_pa * WING_AREA_M2)
    cd = CD_0 + CD_K * cl**2
    drag_n = cd * dynamic_pressure_pa * WING_AREA_M2

    # Two engines, both driven by the same shared "controls.throttle" input (turbofan.cpp).
    thrust_n = 2.0 * result.throttle * THRUST_REF_N * (density_kg_m3 / DENSITY_REF_KGM3)

    assert abs(thrust_n * math.cos(result.alpha_rad) - drag_n) / drag_n < 0.05


def test_two_runs_from_the_same_guess_are_bitwise_identical() -> None:
    result_a = trim.trim_straight_and_level(CRUISE_CONFIG, TRUE_AIRSPEED_MPS, ALTITUDE_M)
    result_b = trim.trim_straight_and_level(CRUISE_CONFIG, TRUE_AIRSPEED_MPS, ALTITUDE_M)

    assert np.array_equal(result_a.state, result_b.state)
    assert result_a.elevator_rad == result_b.elevator_rad
    assert result_a.throttle == result_b.throttle


def test_state_from_alpha_matches_the_real_quaternion_construction() -> None:
    # frames.hpp::quaternion_from_euler (Stevens & Lewis Example 1.8-1 Eq. (1), p. 52) collapses to
    # [cos(alpha/2), 0, sin(alpha/2), 0] when psi=phi=0 -- checked here against the real
    # Simulation.from_config's own initial.state.euler_nb construction, not trusted by hand alone.
    physical = {
        "earth": "plugins/physical/earth/flat",
        "gravity": "plugins/physical/gravity/constant",
        "mass": {
            "plugin": "plugins/physical/mass/constant",
            "mass": 10.0,
            "ixx": 1.0,
            "iyy": 1.0,
            "izz": 1.0,
        },
    }
    for alpha_deg in (0.0, 5.0, -3.0):
        alpha_rad = math.radians(alpha_deg)
        d = {
            "sim": {"dt": 0.001, "frames": 1},
            "vehicle": {"models": []},
            "physical": physical,
            "initial": {"state": {"euler_nb": [0.0, alpha_rad, 0.0]}},
        }
        sim = _core.Simulation.from_config(_core.Config.from_dict(d), _core.default_plugin_dir())
        real_q = np.array(
            sim.vehicles[0].state[_core.QUATERNION : _core.QUATERNION + _core.QUATERNION_SIZE]
        )
        # Exercises the actual helper, not a second hand-typed formula -- p_n/true_airspeed_mps
        # don't affect the quaternion slice, so arbitrary values isolate that slice for comparison.
        helper_state = trim._state_from_alpha(np.zeros(3), 100.0, alpha_rad)
        helper_q = helper_state[_core.QUATERNION : _core.QUATERNION + _core.QUATERNION_SIZE]
        assert np.allclose(helper_q, real_q, atol=1e-15)


def test_still_air_config_has_no_wind_configured() -> None:
    # PR 5's R7a: derivative() raises on a wind-configured, never-stepped Simulation. cruise.yaml
    # itself configures physical.wind (dryden) -- if _load_still_air_config failed to drop it, this
    # call would raise instead of returning a residual.
    config = trim._load_still_air_config(CRUISE_CONFIG)
    sim = _core.Simulation.from_config(config, _core.default_plugin_dir())
    state = np.array(sim.vehicles[0].state)
    trim._residual6(sim, state)  # raises on failure; no assertion needed


def test_trim_of_a_logged_config_creates_no_log_file(tmp_path: Path) -> None:
    # M5 PR 4 (Codex review round 1, F3): a config carrying a log block is trimmed without it, so
    # the analysis Simulation never creates (or finalizes) the run's own log file, and a run built
    # from the same config afterwards still finds its path free.
    data = yaml.safe_load(Path(CRUISE_CONFIG).read_text(encoding="utf-8"))
    log_path = tmp_path / "run.h5"
    data["log"] = {"path": str(log_path), "buffer_frames": 64}
    logged = tmp_path / "cruise_logged.yaml"
    logged.write_text(yaml.safe_dump(data, sort_keys=False), encoding="utf-8")

    result = trim.trim_straight_and_level(logged, TRUE_AIRSPEED_MPS, ALTITUDE_M)
    assert np.max(np.abs(result.residual)) < 1e-6
    assert not log_path.exists()
    sim = _core.Simulation.from_config(
        _core.Config.from_dict(data, origin=str(logged)), _core.default_plugin_dir()
    )
    assert sim.has_log
    assert not sim.run(5)
    sim.close_log()
    assert log_path.exists()


def test_refuses_a_flat_earth_config(tmp_path: Path) -> None:
    # p_n[-1] = altitude_m (trim.py) assumes a geodetic (round/wgs84_rotating) position
    # representation; under earth/flat, p_n[2] is NED down, so the same assignment would place
    # the aircraft at -altitude_m instead (ADR-0003). The map spelling ({plugin: ...}) exercises
    # _earth_plugin_path's dict branch, not just the plain-scalar one every other test uses.
    data = yaml.safe_load(Path(CRUISE_CONFIG).read_text(encoding="utf-8"))
    data["physical"]["earth"] = {"plugin": "plugins/physical/earth/flat"}
    flat_config = tmp_path / "cruise_flat_earth.yaml"
    flat_config.write_text(yaml.safe_dump(data), encoding="utf-8")

    with pytest.raises(ValueError, match=r"physical\.earth"):
        trim.trim_straight_and_level(flat_config, TRUE_AIRSPEED_MPS, ALTITUDE_M)


def test_write_input_raises_on_a_stale_or_wrong_control_name() -> None:
    # write_input's own existing contract (tests/python/test_write_input.py) is the runtime guard
    # against aero.cpp/turbofan.cpp's control names ever drifting silently -- proven here by forcing
    # a wrong name through trim's own call path, not just trusted from reading the plugin source.
    config = trim._load_still_air_config(CRUISE_CONFIG)
    sim = _core.Simulation.from_config(config, _core.default_plugin_dir())
    with pytest.raises(_core.Error):
        sim.write_input("controls.no_such_input", 0.0)
