"""The package around the module: versions agree, the core's strings come through, no bound name
carries a unit suffix (ADR-0002), and the module knows the plugin directory of the build that
produced it."""

import importlib.metadata
from pathlib import Path

import astra
from astra import _core

# Unit tokens only: frame letters (p_n, v_b, stability axes _s) are notation, not units (ADR-0001).
UNIT_SUFFIXES = (
    "_m", "_ft", "_deg", "_rad", "_kg", "_lb", "_lbf", "_kt", "_kts", "_mps", "_fps", "_pa",
)  # fmt: skip


def test_version_matches_the_installed_distribution() -> None:
    assert astra.__version__ == _core.VERSION == importlib.metadata.version("astra")


def test_core_api_version_and_toolchain_string_come_from_libastra_core() -> None:
    # Bumped by hand alongside core_api.hpp.in's kCoreApiVersion (M2 PR 2: 1 -> 2 -> 3,
    # WrenchSource's then WindModel's vtables changed; M2 PR 2c: 3 -> 4, EarthModel/GravityModel's
    # in_domain; M3 PR 2, ADR-0017: 4 -> 5, WrenchSource's new bind_inputs virtual; M3 PR 3,
    # ADR-0018: 5 -> 6, AeroModel/PropulsionModel's new compute() virtuals settling for the first
    # time; M3 PR 3d, ADR-0020: 6 -> 7, AeroModel/PropulsionModel lose compute() entirely and
    # requires_environment() goes from final to a plain overridden virtual, both classes' vtables
    # settling to their new, final shape; M3 PR 4a, ADR-0021: 7 -> 8, Model gained five new
    # virtuals (state_count, initial_state, integrate_state, bind_state, resolve_peers), changing
    # Model's own vtable layout; M4 PR 3, ADR-0022: 8 -> 9, the new Transport interface settling
    # its vtable shape for the first time, Joe's Q-A decision in docs/plans/m4-pr3-core-seam.md;
    # M7 PR 3b, ADR-0032: 9 -> 10, EnvironmentSample gained omega_ie_b and crosses the loader
    # boundary by reference).
    assert _core.core_api_version() == 10
    assert "Clang" in _core.built_with()
    assert "sanitizers=none" in _core.built_with()
    assert "static-plugins=off" in _core.built_with()


def test_no_bound_name_carries_a_unit_suffix_as_adr_0002_says() -> None:
    names = [name for name in vars(_core) if not name.startswith("_")]
    for obj in list(vars(_core).values()):
        if isinstance(obj, type):
            names += [name for name in vars(obj) if not name.startswith("_")]
    assert len(names) > 20, names
    assert [n for n in names if n.lower().endswith(UNIT_SUFFIXES)] == []


def test_default_plugin_dir_is_the_directory_this_build_wrote_its_plugins_to() -> None:
    plugin_dir = Path(_core.default_plugin_dir())
    assert plugin_dir.is_dir()
    assert (plugin_dir / _core.PluginLoader.library_name("tests/plugins/dummy")).is_file()
