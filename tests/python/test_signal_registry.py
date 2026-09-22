"""SignalRegistry read views (M3 PR 5, docs/plans/pr5-nanobind-bindings.md R3, Design decision 4):
a copied-out list of every published signal, and a direct by-name lookup. Neither exposes a live
pointer to Python."""

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
    d = {"sim": {"dt": 0.001, "frames": 1}, "vehicle": {"models": []}, "physical": PHYSICAL}
    return _core.Simulation.from_config(_core.Config.from_dict(d), _core.default_plugin_dir())


def test_signals_lists_every_published_signal() -> None:
    sim = _sim()
    names = {signal[0] for signal in sim.registry.signals}
    assert "v0.state.p_n_0" in names
    assert "v0.state.q_nb_w" in names
    assert "v0.wrench_total.force_b_x" in names


def test_signals_reflects_the_current_value_and_unit() -> None:
    sim = _sim()
    by_name = {signal[0]: (signal[1], signal[2]) for signal in sim.registry.signals}
    # Identity attitude is Simulation.from_config's own default (read_initial_state()'s "absent
    # euler_nb means identity attitude").
    value, unit = by_name["v0.state.q_nb_w"]
    assert value == 1.0
    assert unit == ""
    value, unit = by_name["v0.state.v_b_0"]
    assert unit == "m/s"


def test_find_returns_the_current_value_for_a_known_name() -> None:
    sim = _sim()
    assert sim.registry.find("v0.state.q_nb_w") == 1.0


def test_find_returns_none_for_an_unknown_name() -> None:
    sim = _sim()
    assert sim.registry.find("no.such.signal") is None


def test_signals_is_a_copy_not_a_live_view() -> None:
    sim = _sim()
    first = sim.registry.signals
    sim.step()
    second = sim.registry.signals
    # Copied-out at call time: the first snapshot must not have silently updated in place.
    first_p_n_2 = next(signal[1] for signal in first if signal[0] == "v0.state.p_n_2")
    second_p_n_2 = next(signal[1] for signal in second if signal[0] == "v0.state.p_n_2")
    assert first_p_n_2 == 0.0
    assert second_p_n_2 != 0.0  # gravity moved it in one frame
