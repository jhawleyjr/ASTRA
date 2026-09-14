"""The ADR-0009 loader from Python (mirrors tests/unit/plugin/loader_test.cpp): loads the dummy
plugin by its path from the build's plugin directory, rejects a bad core_api_version and a bad
toolchain string with typed errors naming the plugin, and refuses malformed paths before touching
the filesystem."""

import re
from pathlib import Path

import pytest

from astra import _core

DUMMY = "tests/plugins/dummy"


@pytest.fixture
def loader() -> _core.PluginLoader:
    return _core.PluginLoader(_core.default_plugin_dir())


def test_loads_the_dummy_plugin_by_path(loader: _core.PluginLoader) -> None:
    loaded = loader.load(DUMMY)

    descriptor = loaded.descriptor
    assert (descriptor.category, descriptor.name) == ("test", "dummy")
    assert descriptor.core_api_version == _core.core_api_version()
    assert descriptor.built_with == _core.built_with()

    model = loaded.model
    assert model.name() == "dummy"
    model.reset()
    model.step(_core.StepContext(t=0.0, dt=1e-3, frame=0))


def test_each_load_creates_its_own_model(loader: _core.PluginLoader) -> None:
    first = loader.load(DUMMY)
    second = loader.load(DUMMY)
    assert first.descriptor.name == second.descriptor.name
    assert first.model is not second.model


def test_a_model_outlives_the_loaded_plugin_it_came_from(loader: _core.PluginLoader) -> None:
    model = loader.load(DUMMY).model
    assert model.name() == "dummy"


@pytest.mark.parametrize(
    ("path", "error"),
    [
        ("tests/plugins/bad_version", _core.PluginApiVersionError),
        ("tests/plugins/bad_toolchain", _core.PluginToolchainError),
        ("plugins/physical/atmosphere/does_not_exist", _core.PluginNotFoundError),
    ],
)
def test_rejects_with_a_typed_error_naming_the_plugin(
    loader: _core.PluginLoader, path: str, error: type[_core.PluginError]
) -> None:
    with pytest.raises(error, match=re.escape(path)) as info:
        loader.load(path)
    assert isinstance(info.value, _core.PluginError)
    assert isinstance(info.value, _core.Error)
    assert isinstance(info.value, RuntimeError)


def test_a_bad_core_api_version_error_names_both_versions(loader: _core.PluginLoader) -> None:
    expected = _core.core_api_version()
    with pytest.raises(
        _core.PluginApiVersionError,
        match=f"core_api_version {expected + 1} does not match this core's {expected}",
    ):
        loader.load("tests/plugins/bad_version")


@pytest.mark.parametrize(
    "path",
    [
        "",
        "/tests/plugins/dummy",
        "tests/plugins/dummy/",
        "tests//plugins/dummy",
        "../tests/plugins/dummy",
        "tests/plugins/dummy.so",
        "tests/plugins/dum my",
    ],
)
def test_rejects_malformed_paths_before_touching_the_filesystem(
    loader: _core.PluginLoader, path: str
) -> None:
    assert not _core.PluginLoader.is_valid_plugin_path(path)
    with pytest.raises(_core.PluginNotFoundError, match="invalid plugin path"):
        loader.load(path)


def test_accepts_well_formed_paths() -> None:
    assert _core.PluginLoader.is_valid_plugin_path("plugins/physical/atmosphere/isa1976")
    assert _core.PluginLoader.is_valid_plugin_path("aircraft/_template/physical/aero")


def test_maps_a_plugin_path_to_its_library_name_as_adr_0009_says() -> None:
    assert (
        _core.PluginLoader.library_name("plugins/physical/atmosphere/isa1976")
        == "libastra_plugins_physical_atmosphere_isa1976.so"
    )
    assert (
        _core.PluginLoader.library_name("aircraft/notional_transport/physical/aero")
        == "libastra_aircraft_notional_transport_physical_aero.so"
    )


def test_this_build_is_shared_not_static(loader: _core.PluginLoader) -> None:
    assert not _core.PluginLoader.is_static()
    assert loader.plugin_dir == Path(_core.default_plugin_dir())


def test_step_context_rejects_a_negative_frame() -> None:
    with pytest.raises(TypeError):
        _core.StepContext(t=0.0, dt=1e-3, frame=-1)
