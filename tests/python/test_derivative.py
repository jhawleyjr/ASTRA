"""Simulation.derivative (M3 PR 5, docs/plans/pr5-nanobind-bindings.md): the trim/linearize
primitive. Bit-identity to the real integrator's own k1 is proven in C++
(tests/unit/kernel/simulation_derivative_test.cpp, R9) -- this file proves the Python binding
surface itself: shape/exceptions (R5), that a probe never leaks into the real HealthMonitor's
counters or the wrench_total signal (R5a), that a probe restores state-owner signals (R5b), that a
probe fault raises (R5c), that mass/inertia are cached before the first-ever probe (R5d), fresh
atmosphere (R6), the wind contract (R7/R7a/R7b), the no-physics raise (R7c), and the authoritative
named-slice layout (R8)."""

import numpy as np
import pytest

from astra import _core

PHYSICAL = {
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


def _sim(models: list[dict] | None = None, physical: dict | None = None) -> "_core.Simulation":
    d = {
        "sim": {"dt": 0.001, "frames": 1},
        "vehicle": {"models": models or []},
    }
    if physical is not None:
        d["physical"] = physical
    return _core.Simulation.from_config(_core.Config.from_dict(d), _core.default_plugin_dir())


def test_derivative_returns_an_array_shaped_to_the_combined_state() -> None:
    sim = _sim(physical=PHYSICAL)
    state = np.array(sim.vehicles[0].state)
    dq = sim.derivative(state)
    assert dq.shape == state.shape
    assert dq.dtype == np.float64


def test_derivative_rejects_a_mismatched_state_length() -> None:
    sim = _sim(physical=PHYSICAL)
    with pytest.raises(ValueError, match="expected a state of length"):
        sim.derivative(np.zeros(5))


def test_derivative_raises_when_no_physics_is_configured() -> None:
    sim = _sim()  # no "physical" section at all
    with pytest.raises(_core.Error):
        sim.derivative(np.zeros(13))


# R5a: a probe must never leak into the real HealthMonitor's counters/signals or wrench_total.
def test_repeated_derivative_calls_do_not_change_wrench_total_or_health_signals() -> None:
    sim = _sim(physical=PHYSICAL)
    state = np.array(sim.vehicles[0].state)
    before = {
        signal[0]: signal[1] for signal in sim.registry.signals if "wrench_total" in signal[0]
    }
    before_nan = sim.registry.find("health.nan")

    for _ in range(10):
        # Perturb the state on each call -- a probe that leaked into published_wrench_ or the real
        # HealthMonitor's counters would show it after several distinct evaluations, not just one.
        perturbed = state.copy()
        perturbed[0] += 1.0
        sim.derivative(perturbed)

    after = {signal[0]: signal[1] for signal in sim.registry.signals if "wrench_total" in signal[0]}
    after_nan = sim.registry.find("health.nan")
    assert before == after
    assert before_nan == after_nan


# R5b: a probe must restore stage-owner signals to the accepted state, not leave the last-probed
# trial state visible.
def test_a_derivative_probe_leaves_state_owner_signals_showing_the_accepted_state() -> None:
    models = [{"plugin": "tests/plugins/state_owner", "initial_value": 1.0, "self_rate": 1.0}]
    sim = _sim(models=models, physical=PHYSICAL)
    state = np.array(sim.vehicles[0].state)

    perturbed = state.copy()
    perturbed[13] = 999.0  # the state_owner's own slice (offset 13, per state_owner_slices)
    sim.derivative(perturbed)

    # The published signal must still show the real, accepted initial value (1.0), never the
    # probe's own trial value (999.0) -- Vehicle::derivative()'s own stage_ restore (Design
    # decision 1e).
    assert sim.registry.find("v0.state_owner.state.0") == 1.0


# R5c: a fault during a probe raises, not a plausible-looking wrong derivative. Design decision 1c
# covers exactly the faults prepare_stage_environment's own domain/environment pre-checks can
# catch (the same checks a real k1 sub-stage runs) -- not a general NaN-input guard: check_state's
# own NaN check only ever runs once per FULL RK4 step in the real per-frame path (post-integration),
# never per-substage, so it is correctly outside a single k1-equivalent probe's own scope too.
def test_derivative_raises_when_the_probe_altitude_is_outside_the_atmospheres_valid_range() -> None:
    physical = dict(PHYSICAL, atmosphere="plugins/physical/atmosphere/isa1976")
    sim = _sim(physical=physical)
    state = np.array(sim.vehicles[0].state)
    state[2] = 200_000.0  # NED down: 200 km below origin -> far outside isa1976's [-5000, 86000] m
    with pytest.raises(_core.Error):
        sim.derivative(state)


# R5d: begin_frame()'s mass/inertia caching must run before the very first probe.
def test_first_ever_call_on_a_never_stepped_simulation_uses_the_configured_mass() -> None:
    sim = _sim(physical=PHYSICAL)
    state = np.array(sim.vehicles[0].state)
    dq = sim.derivative(state)
    # v_b's derivative (indices 3-5) is dominated by gravity/mass; a zero-mass divide would
    # produce inf/nan, not a small finite number.
    assert np.all(np.isfinite(dq[3:6]))
    assert dq[5] > 0.0  # gravity, body z, FRD


# R6: fresh atmosphere at the given state, not a frame-latched one. atmosphere_consumer's own
# wrench_b() always returns a zero wrench (it exists only to record what EnvironmentSample it was
# handed, tests/plugins/atmosphere_consumer.cpp's own header comment) -- so the proof is its own
# recorded-density signal, not the returned derivative array, which this plugin never perturbs.
def test_derivative_atmosphere_tracks_the_altitude_in_the_passed_state() -> None:
    physical = dict(PHYSICAL, atmosphere="plugins/physical/atmosphere/isa1976")
    models = [{"plugin": "tests/plugins/atmosphere_consumer"}]
    sim = _sim(models=models, physical=physical)
    state = np.array(sim.vehicles[0].state)

    sea_level = state.copy()
    sea_level[2] = 0.0  # NED down = 0 -> sea level
    sim.derivative(sea_level)
    density_sea_level = sim.registry.find("v0.atmosphere_consumer.density")

    five_km_up = state.copy()
    five_km_up[2] = -5000.0
    sim.derivative(five_km_up)
    density_five_km_up = sim.registry.find("v0.atmosphere_consumer.density")

    assert density_sea_level is not None
    assert density_five_km_up is not None
    assert density_five_km_up < density_sea_level


# R7: "latched wind" -- once a real frame has latched a sample, a probe reads it correctly.
def test_derivative_reads_the_wind_configuration_once_stepped() -> None:
    physical = dict(PHYSICAL, wind={"plugin": "plugins/physical/wind/uniform", "north": 5.0})
    sim = _sim(physical=physical)
    halt = sim.step()
    assert not bool(halt)
    state = np.array(sim.vehicles[0].state)
    # Should not raise now that a real frame has latched a sample.
    sim.derivative(state)


# R7a: derivative() raises, never silently substitutes still air, when wind is configured and
# never latched.
def test_derivative_raises_on_a_never_stepped_simulation_with_wind_configured() -> None:
    physical = dict(PHYSICAL, wind={"plugin": "plugins/physical/wind/uniform", "north": 5.0})
    sim = _sim(physical=physical)
    state = np.array(sim.vehicles[0].state)
    with pytest.raises(_core.Error):
        sim.derivative(state)


def test_derivative_does_not_raise_again_once_stepped_even_without_touching_wind_again() -> None:
    physical = dict(PHYSICAL, wind={"plugin": "plugins/physical/wind/uniform", "north": 5.0})
    sim = _sim(physical=physical)
    assert not bool(sim.step())
    state = np.array(sim.vehicles[0].state)
    for _ in range(5):
        sim.derivative(state)  # must not raise on any of these


# R7b: a probe must never mutate a configured wind plugin's own published signals (the critique's
# own wind/table counterexample).
def test_repeated_derivative_calls_do_not_mutate_the_configured_wind_plugins_own_signals() -> None:
    physical = dict(
        PHYSICAL,
        wind={
            "plugin": "plugins/physical/wind/table",
            "points": [
                {"altitude": 0.0, "north": 1.0},
                {"altitude": 1000.0, "north": 2.0},
            ],
        },
    )
    sim = _sim(physical=physical)
    assert not bool(sim.step())
    state = np.array(sim.vehicles[0].state)
    before_altitude = sim.registry.find("v0.wind.altitude")

    for delta in (0.0, -100.0, -500.0, -900.0):
        perturbed = state.copy()
        perturbed[2] = delta
        sim.derivative(perturbed)

    assert sim.registry.find("v0.wind.altitude") == before_altitude


# R8: authoritative named-slice metadata, not registry-name parsing.
def test_state_owner_slices_reports_the_correct_offset_and_count_for_a_state_owning_model() -> None:
    models = [{"plugin": "tests/plugins/state_owner", "initial_value": 1.0, "self_rate": 1.0}]
    sim = _sim(models=models, physical=PHYSICAL)
    vehicle = sim.vehicles[0]
    assert vehicle.state.shape[0] == _core.STATE_SIZE + 1
    assert vehicle.state_owner_slices == [("state_owner", _core.STATE_SIZE, 1)]


def test_state_owner_slices_is_empty_with_no_state_owning_model() -> None:
    sim = _sim(physical=PHYSICAL)
    assert sim.vehicles[0].state_owner_slices == []
    assert sim.vehicles[0].state.shape[0] == _core.STATE_SIZE
