// astra._core: the nanobind module over libastra_core (docs/architecture.md §12). M0 binds what the
// core has: the ADR-0003 state layout, the ADR-0009 loader with its typed errors, and the core's
// version strings. Everything is SI with no unit suffixes (ADR-0002). Simulation, Config, the model
// base classes with trampolines, and SignalRegistry views arrive with the milestones that create
// them.
#include <nanobind/eigen/dense.h>
#include <nanobind/nanobind.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/model.hpp"
#include "astra/kernel/state.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/plugin/loader.hpp"

namespace nb = nanobind;

namespace {

astra::StateVector zero_state() { return astra::StateVector::Zero(); }

astra::StateVector identity_state() {
  astra::StateVector x = astra::StateVector::Zero();
  // q_nb = (1, 0, 0, 0): the scalar-first identity attitude (ADR-0001).
  x[astra::state::kQuaternion] = 1.0;
  return x;
}

const char* default_plugin_dir() { return ASTRA_PLUGIN_DIR; }

// The check the loader applies to every plugin (ADR-0009), applied to this module: the copy pip
// installed into the venv must have been compiled against the libastra_core it just loaded, which
// is the one shared object the loader never sees. A rebuilt build/python without `just python` is
// how they drift apart. nanobind turns the exception into ImportError with this message.
void check_built_against_this_core() {
  if (astra::kCoreApiVersion == astra::core_api_version() &&
      std::string_view{astra::kBuiltWith} == astra::built_with()) {
    return;
  }
  std::string message = "astra._core was compiled against libastra_core '";
  message += astra::kBuiltWith;
  message += "' (core API " + std::to_string(astra::kCoreApiVersion) + ") but loaded '";
  message += astra::built_with();
  message += "' (core API " + std::to_string(astra::core_api_version()) +
             "): stale editable install, run `just python`";
  throw nb::import_error(message.c_str());
}

}  // namespace

NB_MODULE(_core, m) {
  check_built_against_this_core();
  m.doc() =
      "ASTRA core bindings: state layout (ADR-0003), plugin loader (ADR-0009), SI throughout "
      "(ADR-0002).";
  // Not __version__: nanobind's stub generator never emits that name, and pyright would then not
  // know it. astra.__version__ (python/astra/__init__.py) re-exports this.
  m.attr("VERSION") = ASTRA_VERSION;

  m.def("core_api_version", &astra::core_api_version,
        "The plugin ABI version compiled into libastra_core; a plugin must match it to load.");
  m.def("built_with", &astra::built_with,
        "The toolchain and configuration string compiled into libastra_core; plugins must match.");
  m.def("default_plugin_dir", &default_plugin_dir,
        "The plugin directory of the build that produced this module.");

  // The state vector: 13 doubles, p_n(3) v_b(3) q_nb(4) omega_b(3), SI, no unit suffixes.
  m.attr("STATE_SIZE") = astra::kStateSize;
  m.attr("POSITION") = astra::state::kPosition;
  m.attr("VELOCITY") = astra::state::kVelocity;
  m.attr("QUATERNION") = astra::state::kQuaternion;
  m.attr("ANGULAR_RATE") = astra::state::kAngularRate;
  m.attr("POSITION_SIZE") = astra::state::kPositionSize;
  m.attr("VELOCITY_SIZE") = astra::state::kVelocitySize;
  m.attr("QUATERNION_SIZE") = astra::state::kQuaternionSize;
  m.attr("ANGULAR_RATE_SIZE") = astra::state::kAngularRateSize;
  m.def("zero_state", &zero_state, "A new all-zero state vector: shape (13,), float64.");
  m.def("identity_state", &identity_state,
        "A new state vector at rest at the origin with identity attitude: zero except q_nb.w = 1.");

  // Init-time errors (errors.hpp). Each object exists for its constructor's side effect, which
  // registers the Python type and its translator. nanobind runs translators
  // most-recently-registered first, so the bases go first and a PluginApiVersionError reaches
  // Python as itself rather than as PluginError.
  const nb::exception<astra::Error> error(m, "Error", PyExc_RuntimeError);
  const nb::exception<astra::PluginError> plugin_error(m, "PluginError", error.ptr());
  [[maybe_unused]] const nb::exception<astra::PluginNotFoundError> not_found(
      m, "PluginNotFoundError", plugin_error.ptr());
  [[maybe_unused]] const nb::exception<astra::PluginLoadError> load_error(m, "PluginLoadError",
                                                                          plugin_error.ptr());
  [[maybe_unused]] const nb::exception<astra::PluginApiVersionError> api_version(
      m, "PluginApiVersionError", plugin_error.ptr());
  [[maybe_unused]] const nb::exception<astra::PluginToolchainError> toolchain(
      m, "PluginToolchainError", plugin_error.ptr());

  nb::class_<astra::StepContext>(
      m, "StepContext",
      "What every step receives: frame is the source of truth, t = frame * dt (ADR-0010).")
      .def(nb::init<double, double, std::uint64_t>(), nb::arg("t"), nb::arg("dt"), nb::arg("frame"))
      .def_rw("t", &astra::StepContext::t)
      .def_rw("dt", &astra::StepContext::dt)
      .def_rw("frame", &astra::StepContext::frame);

  nb::class_<astra::Model>(
      m, "Model", "A loaded plugin's model. init() is bound once Config and SignalRegistry exist.")
      .def("name", &astra::Model::name)
      .def("reset", &astra::Model::reset)
      .def("step", &astra::Model::step, nb::arg("ctx"));

  nb::class_<astra::PluginDescriptor>(m, "PluginDescriptor",
                                      "What a plugin's astra_plugin() returns (ADR-0009).")
      .def_prop_ro("category", [](const astra::PluginDescriptor& d) { return d.category; })
      .def_prop_ro("name", [](const astra::PluginDescriptor& d) { return d.name; })
      .def_ro("core_api_version", &astra::PluginDescriptor::core_api_version)
      .def_prop_ro("built_with", [](const astra::PluginDescriptor& d) { return d.built_with; });

  nb::class_<astra::LoadedPlugin>(m, "LoadedPlugin",
                                  "A plugin's descriptor and one model created from it.")
      .def_prop_ro(
          "descriptor", [](const astra::LoadedPlugin& p) { return p.descriptor; },
          nb::rv_policy::reference)
      .def_prop_ro(
          "model", [](astra::LoadedPlugin& p) { return p.model.get(); },
          nb::rv_policy::reference_internal);

  nb::class_<astra::PluginLoader>(m, "PluginLoader",
                                  "Loads plugins by path from one plugin directory (ADR-0009).")
      .def(nb::init<std::filesystem::path>(), nb::arg("plugin_dir"),
           nb::sig("def __init__(self, plugin_dir: str | os.PathLike[str]) -> None"))
      .def("load", &astra::PluginLoader::load, nb::arg("plugin_path"),
           "Load one plugin by its path, e.g. 'plugins/physical/atmosphere/isa1976'. Raises a "
           "PluginError subclass naming the plugin on any failure.")
      .def_prop_ro("plugin_dir", &astra::PluginLoader::plugin_dir)
      .def_static("library_name", &astra::PluginLoader::library_name, nb::arg("plugin_path"),
                  "'plugins/physical/atmosphere/isa1976' -> "
                  "'libastra_plugins_physical_atmosphere_isa1976.so'")
      .def_static("is_valid_plugin_path", &astra::PluginLoader::is_valid_plugin_path,
                  nb::arg("plugin_path"), "Segments of [A-Za-z0-9_] joined by single '/'.")
      .def_static("is_static", &astra::PluginLoader::is_static,
                  "True in an ASTRA_STATIC_PLUGINS build, where nothing is dlopened.");
}
