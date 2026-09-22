"""sim.write_input (M3 PR 5, docs/plans/pr5-nanobind-bindings.md R4, Design decision 3): commits
immediately (write() then latch()), unlike the raw two-phase InputRegistry contract, since a
Simulation driven only through derivative() never calls the one thing (step()) that otherwise
calls latch(). ADR-0017's own explicit requirement: raises rather than silently keeping the
previous value when the just-written value is itself non-finite."""

import math

import pytest

from astra import _core

INPUT_CONSUMER_MODELS = [{"plugin": "tests/plugins/input_consumer"}]


def _sim() -> "_core.Simulation":
    d = {
        "sim": {"dt": 0.001, "frames": 1},
        "vehicle": {
            "models": INPUT_CONSUMER_MODELS,
            "initial_controls": {"controls": {"elevator": 0.0}},
        },
    }
    return _core.Simulation.from_config(_core.Config.from_dict(d), _core.default_plugin_dir())


def test_write_input_is_visible_to_the_very_next_read_with_no_step() -> None:
    sim = _sim()
    assert sim.registry.find("v0.controls.elevator") == 0.0

    sim.write_input("controls.elevator", 0.05)

    # InputRegistry.add() also publishes the input read-only into the same SignalRegistry
    # (input_registry.hpp's own contract) -- this is the direct, immediate proof that write_input
    # committed (write() + latch()), not merely staged a pending value nothing has committed yet.
    assert sim.registry.find("v0.controls.elevator") == 0.05


def test_write_input_raises_on_an_unregistered_name() -> None:
    sim = _sim()
    with pytest.raises(_core.Error):
        sim.write_input("no.such.input", 1.0)


def test_write_input_raises_on_a_non_finite_value() -> None:
    sim = _sim()
    with pytest.raises(_core.Error):
        sim.write_input("controls.elevator", math.nan)
    # ADR-0017: a non-finite write leaves the previous, last-good value latched -- not zeroed, not
    # the NaN itself.
    assert sim.registry.find("v0.controls.elevator") == 0.0
