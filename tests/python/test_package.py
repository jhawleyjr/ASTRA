"""The package around the module: versions agree, the core's strings come through, no bound name
carries a unit suffix (ADR-0002), and the module knows the plugin directory of the build that
produced it."""

import importlib.metadata
from pathlib import Path

import astra
from astra import _core

UNIT_SUFFIXES = ("_m", "_ft", "_deg", "_rad", "_kg", "_s", "_n", "_pa", "_k")


def test_version_matches_the_installed_distribution() -> None:
    assert astra.__version__ == _core.VERSION == importlib.metadata.version("astra")


def test_core_api_version_and_toolchain_string_come_from_libastra_core() -> None:
    assert _core.core_api_version() == 1
    assert "Clang" in _core.built_with()
    assert "sanitizers=none" in _core.built_with()
    assert "static-plugins=off" in _core.built_with()


def test_no_bound_name_carries_a_unit_suffix_as_adr_0002_says() -> None:
    names = [name for name in dir(_core) if not name.startswith("_")]
    for cls in (_core.StepContext, _core.Model, _core.PluginDescriptor, _core.PluginLoader):
        names += [name for name in dir(cls) if not name.startswith("_")]
    assert names, "nothing bound?"
    assert [n for n in names if n.lower().endswith(UNIT_SUFFIXES)] == []


def test_default_plugin_dir_is_the_directory_this_build_wrote_its_plugins_to() -> None:
    plugin_dir = Path(_core.default_plugin_dir())
    assert plugin_dir.is_dir()
    assert (plugin_dir / _core.PluginLoader.library_name("tests/plugins/dummy")).is_file()
