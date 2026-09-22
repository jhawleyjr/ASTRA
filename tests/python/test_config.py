"""Config.from_dict (M3 PR 5, docs/plans/pr5-nanobind-bindings.md R2/R2a): a dict, YAML file, and
YAML string build an equivalent Config; the emitted-YAML-text round trip is lossless for every
scalar case the plan's own R2a row names, empirically verified against the real yaml-cpp before
being written (see the plan's "What Config from a Python dict needs" section)."""

import math
from pathlib import Path

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


def test_from_dict_builds_a_simulation_equivalent_to_the_same_yaml() -> None:
    d = {"sim": {"dt": 0.001, "frames": 1}, "vehicle": {"models": []}, "physical": PHYSICAL}
    yaml_text = (
        "sim: { dt: 0.001, frames: 1 }\n"
        "vehicle: { models: [] }\n"
        "physical:\n"
        "  earth: plugins/physical/earth/flat\n"
        "  gravity: plugins/physical/gravity/constant\n"
        "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: "
        "1.0 }\n"
    )

    sim_from_dict = _core.Simulation.from_config(
        _core.Config.from_dict(d), _core.default_plugin_dir()
    )
    sim_from_yaml = _core.Simulation.from_config(
        _core.Config.from_string(yaml_text, "test"), _core.default_plugin_dir()
    )

    assert list(sim_from_dict.vehicles[0].state) == list(sim_from_yaml.vehicles[0].state)


def test_from_dict_rejects_an_unsupported_python_value_type() -> None:
    with pytest.raises(TypeError):
        _core.Config.from_dict({"sim": {"dt": object()}})


def test_from_file_is_bound(tmp_path: Path) -> None:
    yaml_file = tmp_path / "config.yaml"
    yaml_file.write_text("sim: { dt: 0.001, frames: 1 }\nvehicle: { models: [] }\n")
    config = _core.Config.from_file(yaml_file)
    assert config.has("sim")


def test_from_dict_round_trips_scalars_exactly() -> None:
    d = {
        "sim": {"dt": 0.001, "frames": 1},
        "vehicle": {"models": []},
        "test": {
            "g0": 9.820228349842926,  # Table 73's g0, the 2026-09-19 lesson's own value
            "neg_zero": -0.0,
            "tiny": 1e-300,
            "huge": 1e300,
            "big_int": (1 << 60) + 12345,  # beyond 2**53
            "flag_true": True,
            "flag_false": False,
            "s_double": "1.5",
            "s_true": "true",
            "s_no": "no",
            "s_tilde": "~",
            "s_empty": "",
        },
    }
    config = _core.Config.from_dict(d)
    node = config.at("test")

    assert math.isclose(node.number("g0"), 9.820228349842926, rel_tol=0)
    assert math.copysign(1.0, node.number("neg_zero")) == -1.0
    assert node.number("tiny") == 1e-300
    assert node.number("huge") == 1e300
    assert node.integer("big_int") == (1 << 60) + 12345

    assert node.boolean("flag_true") is True
    assert node.boolean("flag_false") is False
    # bool-before-int dispatch order (Joe's plan review): a bool value must not have silently
    # become an int YAML node -- Config.integer() on it must raise, not return 1/0.
    with pytest.raises(_core.Error):
        node.integer("flag_true")

    assert node.text("s_double") == "1.5"
    assert node.text("s_true") == "true"
    assert node.text("s_no") == "no"
    assert node.text("s_tilde") == "~"
    assert node.text("s_empty") == ""
