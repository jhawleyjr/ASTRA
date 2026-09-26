"""Simulation (M3 PR 5, docs/plans/pr5-nanobind-bindings.md): from_config, step/run, the
ownership/lifetime chain (Design decision 6), HaltReason's owned-copy source (Design decision 7),
for_testing deliberately not bound (R1c), and D2 (no trampolines in M3 -- Model is bound today with
no constructor at all)."""

import gc

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


def _sim() -> "_core.Simulation":
    d = {"sim": {"dt": 0.001, "frames": 3}, "vehicle": {"models": []}, "physical": PHYSICAL}
    return _core.Simulation.from_config(_core.Config.from_dict(d), _core.default_plugin_dir())


def test_from_config_builds_and_steps_a_real_config() -> None:
    sim = _sim()
    halt = sim.step()
    assert not bool(halt)
    assert sim.clock.frame == 1


def test_run_with_an_explicit_frame_count() -> None:
    sim = _sim()
    halt = sim.run(3)
    assert not bool(halt)
    assert sim.clock.frame == 3


def test_run_with_no_argument_uses_sim_frames_from_config() -> None:
    sim = _sim()
    halt = sim.run()
    assert not bool(halt)
    assert sim.clock.frame == 3  # "sim.frames: 3" in the fixture


def test_for_testing_is_not_exposed_to_python() -> None:
    assert not hasattr(_core.Simulation, "for_testing")


def test_a_python_subclass_of_model_cannot_be_instantiated() -> None:
    class MyModel(_core.Model):
        pass

    with pytest.raises(TypeError):
        MyModel()


# Design decision 6: every borrow into Simulation-owned storage keeps the owning Simulation alive,
# including one level removed (a Vehicle extracted from `vehicles` and kept alone, or a numpy
# array extracted from that Vehicle and kept alone).
def test_a_vehicle_kept_alone_after_its_simulation_is_deleted_still_reads() -> None:
    sim = _sim()
    vehicle = sim.vehicles[0]
    del sim
    gc.collect()
    assert vehicle.name == "v0"  # would segfault/UB if the owning Simulation were actually freed


def test_a_state_view_kept_alone_after_its_simulation_is_deleted_still_reads() -> None:
    sim = _sim()
    state_view = sim.vehicles[0].state
    del sim
    gc.collect()
    assert state_view.shape == (_core.STATE_SIZE,)
    assert np.isfinite(state_view).all()


def test_vehicle_state_view_is_not_writable() -> None:
    sim = _sim()
    state_view = sim.vehicles[0].state
    with pytest.raises((TypeError, ValueError)):
        state_view[0] = 1.0


# Design decision 7: HaltReason's source is an owned copy, safe to hold past the Simulation that
# produced it.
def test_halt_reason_source_is_readable_after_the_simulation_is_deleted() -> None:
    d = {
        "sim": {"dt": 0.001, "frames": 1},
        "vehicle": {"models": [{"plugin": "tests/plugins/nan_wrench", "nan_frame": 0}]},
        "physical": PHYSICAL,
    }
    sim = _core.Simulation.from_config(_core.Config.from_dict(d), _core.default_plugin_dir())
    halt = sim.step()
    assert bool(halt)
    del sim
    gc.collect()
    assert isinstance(halt.source, str)
    assert len(halt.source) > 0


def test_halt_reason_is_falsy_when_nothing_halted() -> None:
    sim = _sim()
    halt = sim.step()
    assert not halt
    assert halt.check == _core.HealthCheck.NONE


def test_health_check_binds_transport_after_the_existing_values() -> None:
    # M4 PR 3 (ADR-0022): kTransport is appended last, so every earlier value keeps its integer.
    assert _core.HealthCheck.NON_FINITE_ENVIRONMENT.value == 6
    assert _core.HealthCheck.TRANSPORT.value == 7
