// astra._core: the nanobind module over libastra_core (docs/architecture.md §12). M0 binds what the
// core has: the ADR-0003 state layout, the ADR-0009 loader with its typed errors, and the core's
// version strings. Everything is SI with no unit suffixes (ADR-0002). M3 PR 5 (docs/plans/
// pr5-nanobind-bindings.md) adds Simulation, Config (including a from-dict path), SignalRegistry
// read views, the PR 2 write seam, and Simulation.derivative() -- the primitive astra.trim/
// astra.linearize (PR 6/7) are built on. Model-base trampolines are not scheduled in M3 (D2).
#include <nanobind/eigen/dense.h>
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>
#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/model.hpp"
#include "astra/kernel/health_monitor.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"

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

// M3 PR 5 (docs/plans/pr5-nanobind-bindings.md, "What Config from a Python dict needs" -- Joe's
// settled option (a)): recursively converts a Python dict/list/scalar into a YAML::Node, checking
// bool BEFORE int (bool is an int subclass in Python; the reverse order would silently turn True
// into the integer 1). Raises TypeError, nanobind-side, for anything else -- never reaches
// Config::from_string with a half-built document.
YAML::Node value_to_yaml(nb::handle value) {
  if (nb::isinstance<nb::bool_>(value)) {
    return YAML::Node(nb::cast<bool>(value));
  }
  if (nb::isinstance<nb::int_>(value)) {
    return YAML::Node(nb::cast<std::int64_t>(value));
  }
  if (nb::isinstance<nb::float_>(value)) {
    return YAML::Node(nb::cast<double>(value));
  }
  if (nb::isinstance<nb::str>(value)) {
    return YAML::Node(nb::cast<std::string>(value));
  }
  if (nb::isinstance<nb::dict>(value)) {
    YAML::Node node(YAML::NodeType::Map);
    for (auto item : nb::cast<nb::dict>(value)) {
      node[nb::cast<std::string>(item.first)] = value_to_yaml(item.second);
    }
    return node;
  }
  if (nb::isinstance<nb::list>(value) || nb::isinstance<nb::tuple>(value)) {
    YAML::Node node(YAML::NodeType::Sequence);
    for (auto item : nb::cast<nb::sequence>(value)) {
      node.push_back(value_to_yaml(item));
    }
    return node;
  }
  throw nb::type_error(
      "Config.from_dict: unsupported value type (expected dict, list, str, bool, int, or float)");
}

astra::Config config_from_dict(const nb::dict& d, std::string_view origin) {
  YAML::Emitter emitter;
  emitter << value_to_yaml(d);
  return astra::Config::from_string(std::string_view{emitter.c_str()}, origin);
}

// M3 PR 5 Design decision 7 (docs/plans/pr5-nanobind-bindings.md): astra::HaltReason::source is a
// std::string_view into storage a Vehicle/Simulation owns, not into the HaltReason value itself --
// binding it directly would let Python read freed memory once the owning Simulation is gone,
// since nanobind's string_view caster only copies at the point Python actually reads the
// property, not necessarily at return time. This type copies source into an owned std::string
// eagerly, at construction -- always immediately after the real call that produced it, while the
// owning Simulation is still alive -- so the bound Python object is fully self-contained.
struct PyHaltReason {
  astra::HealthCheck check = astra::HealthCheck::kNone;
  std::string source;
  std::uint64_t frame = 0;
  std::uint32_t vehicle_index = 0;

  explicit PyHaltReason(const astra::HaltReason& h)
      : check(h.check), source(h.source), frame(h.frame), vehicle_index(h.vehicle_index) {}

  [[nodiscard]] explicit operator bool() const noexcept {
    return check != astra::HealthCheck::kNone;
  }
};

// M3 PR 5 Design decision 6 (docs/plans/pr5-nanobind-bindings.md): every accessor below that
// borrows into Simulation-owned storage (clock/registry/health/vehicles, and a vehicle's own state
// view) uses nb::rv_policy::reference_internal, at every hop, so the owning Simulation Python
// object stays alive for as long as any borrowed wrapper -- or anything extracted from one and
// kept alone -- is reachable.

// M3 PR 5: sim.derivative(state)'s numpy contract -- state must be exactly
// vehicles()[0].combined_state().size() doubles; a mismatched length raises ValueError before any
// C++ call (a Python-idiomatic shape error, not the generic astra.Error C++ itself would raise for
// most other faults this call can hit).
nb::ndarray<nb::numpy, double, nb::ndim<1>> simulation_derivative(
    astra::Simulation& sim, const nb::ndarray<const double, nb::ndim<1>, nb::c_contig>& state) {
  if (sim.vehicles().empty()) {
    throw nb::value_error("Simulation.derivative: no vehicle configured");
  }
  const std::size_t expected = sim.vehicles().front().combined_state().size();
  if (static_cast<std::size_t>(state.shape(0)) != expected) {
    throw nb::value_error(("Simulation.derivative: expected a state of length " +
                           std::to_string(expected) + ", got " + std::to_string(state.shape(0)))
                              .c_str());
  }
  const std::span<const double> state_span(state.data(), state.shape(0));
  std::vector<double> dq = sim.derivative(state_span);

  auto* data = new double[dq.size()];  // NOLINT(cppcoreguidelines-owning-memory)
  std::copy(dq.begin(), dq.end(), data);
  nb::capsule owner(data, [](void* p) noexcept {
    delete[] static_cast<double*>(p);  // NOLINT(cppcoreguidelines-owning-memory)
  });
  return {data, {dq.size()}, owner};
}

// M3 PR 7 (docs/plans/pr7-linearize.md, D14/PR 6's own follow-up): the first vehicle's own
// registered input names, in InputRegistry::names()'s registration order -- mirrors
// simulation_derivative's own "free function over vehicles().front()" shape, not a new
// astra::Simulation member (no core/ header or source change, no kCoreApiVersion bump). Every
// std::string_view is converted to an owned Python str immediately, inside this call, while the
// owning Simulation/Vehicle/InputRegistry is still alive on the stack above it -- unlike
// PyHaltReason::source (a string_view held in a long-lived bound struct field, read lazily whenever
// Python touches the property), this is an ordinary function return, converted once, at call time.
std::vector<std::string> simulation_input_names(const astra::Simulation& sim) {
  if (sim.vehicles().empty()) {
    throw nb::value_error("Simulation.input_names: no vehicle configured");
  }
  const std::vector<std::string_view> names = sim.vehicles().front().inputs().names();
  return {names.begin(), names.end()};
}

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
  // M3 PR 5: these two are rigid-body-block helpers ONLY (ADR-0021's kStateSize == 13, unchanged
  // by the state generalization) -- never describe a vehicle's own combined state, which is
  // runtime-sized (13 + every state-owning model's own slice) and has no fixed "13" of its own.
  // Simulation.derivative()/a vehicle's own `state` property size themselves from the live
  // Vehicle, never from these constants.
  m.def("zero_state", &zero_state,
        "A new all-zero state vector: shape (13,), float64 -- the rigid-body block only, not a "
        "vehicle's own combined state (see Simulation.derivative for that).");
  m.def("identity_state", &identity_state,
        "A new state vector at rest at the origin with identity attitude: zero except q_nb.w = "
        "1.0 -- the rigid-body block only, not a vehicle's own combined state (see "
        "Simulation.derivative for that).");

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
  // M3 PR 5: the first exception reachable from a Python-bound call that can throw one
  // (Config's getters, Simulation.from_config, ...).
  [[maybe_unused]] const nb::exception<astra::ConfigError> config_error(m, "ConfigError",
                                                                        error.ptr());

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

  // M3 PR 5: Simulation.clock's own bound type -- t = frame * dt, derived, never accumulated
  // (ADR-0010).
  nb::class_<astra::VirtualClock>(
      m, "VirtualClock", "The integer frame counter that is the single source of truth for time.")
      .def_prop_ro("dt", &astra::VirtualClock::dt)
      .def_prop_ro("frame", &astra::VirtualClock::frame)
      .def_prop_ro("t", &astra::VirtualClock::t);

  // M3 PR 5 (docs/architecture.md §13): one Config tree, loaded from YAML or built from a Python
  // dict (Joe's settled option (a): dict -> YAML::Node -> emitted text -> the same from_string()
  // bound below, no core-side change -- "What Config from a Python dict needs," docs/plans/
  // pr5-nanobind-bindings.md).
  nb::class_<astra::Config>(
      m, "Config",
      "A loaded, validated configuration tree (docs/architecture.md §13); every getter records "
      "the key as read.")
      .def_static("from_file", &astra::Config::from_file, nb::arg("path"),
                  nb::sig("def from_file(path: str | os.PathLike[str]) -> Config"),
                  "Parses path as YAML; raises ConfigError naming path on a missing file or "
                  "malformed document.")
      .def_static("from_string", &astra::Config::from_string, nb::arg("yaml_text"),
                  nb::arg("origin"),
                  "Parses yaml_text as YAML; origin names the source in every error this Config's "
                  "getters raise.")
      .def_static("from_dict", &config_from_dict, nb::arg("d"), nb::arg("origin") = "<python dict>",
                  nb::sig("def from_dict(d: collections.abc.Mapping[str, object], origin: str = "
                          "'<python dict>') -> Config"),
                  "Builds a Config from a Python dict (values: dict, list, str, bool, int, "
                  "float), via YAML text under the hood -- round-trips exactly (verified against "
                  "yaml-cpp directly, docs/plans/pr5-nanobind-bindings.md's R2a). Raises "
                  "TypeError for an unsupported value type.")
      .def("has", &astra::Config::has, nb::arg("key"))
      .def("at", nb::overload_cast<std::string_view>(&astra::Config::at, nb::const_),
           nb::arg("key"))
      .def("size", &astra::Config::size)
      .def("is_map", &astra::Config::is_map)
      .def("is_sequence", &astra::Config::is_sequence)
      .def("is_scalar", &astra::Config::is_scalar)
      .def_prop_ro("path", &astra::Config::path)
      .def_prop_ro("origin", &astra::Config::origin)
      .def("number", nb::overload_cast<std::string_view>(&astra::Config::number, nb::const_),
           nb::arg("key"))
      .def("integer", nb::overload_cast<std::string_view>(&astra::Config::integer, nb::const_),
           nb::arg("key"))
      .def("boolean", nb::overload_cast<std::string_view>(&astra::Config::boolean, nb::const_),
           nb::arg("key"))
      .def("text", nb::overload_cast<std::string_view>(&astra::Config::text, nb::const_),
           nb::arg("key"))
      .def("unknown_keys", &astra::Config::unknown_keys,
           "Every scalar leaf no getter has read yet, as dotted paths from the root "
           "(docs/architecture.md §15: unknown keys are errors).");

  // M3 PR 5 Design decision 7: HealthCheck names which check fired; the actual halt/fault reason
  // a Python caller reads back from step()/run()/a raised derivative() fault.
  nb::enum_<astra::HealthCheck>(m, "HealthCheck",
                                "Which health check fired, if any (health_monitor.hpp).")
      .value("NONE", astra::HealthCheck::kNone)
      .value("NON_FINITE_STATE", astra::HealthCheck::kNonFiniteState)
      .value("NON_FINITE_WRENCH", astra::HealthCheck::kNonFiniteWrench)
      .value("QUATERNION_NORM", astra::HealthCheck::kQuaternionNorm)
      .value("OUT_OF_DOMAIN", astra::HealthCheck::kOutOfDomain)
      .value("NON_FINITE_INPUT", astra::HealthCheck::kNonFiniteInput)
      .value("NON_FINITE_ENVIRONMENT", astra::HealthCheck::kNonFiniteEnvironment);

  nb::class_<PyHaltReason>(
      m, "HaltReason",
      "Why a step()/run() call halted, if it did -- falsy (bool(reason) is False) when it "
      "didn't. source is always an owned copy (Design decision 7, docs/plans/"
      "pr5-nanobind-bindings.md), safe to hold past the Simulation that produced it.")
      .def("__bool__", &PyHaltReason::operator bool)
      .def_ro("check", &PyHaltReason::check)
      .def_ro("source", &PyHaltReason::source)
      .def_ro("frame", &PyHaltReason::frame)
      .def_ro("vehicle_index", &PyHaltReason::vehicle_index);

  // M3 PR 5: read-only signal views (Design decision 4) -- registry.signals is copied out (no
  // live pointer crosses the boundary); registry.find is a direct-by-name lookup.
  nb::class_<astra::SignalRegistry>(
      m, "SignalRegistry",
      "The published signal dictionary (docs/architecture.md §9); read-only from Python.")
      .def_prop_ro(
          "signals",
          [](const astra::SignalRegistry& r) {
            std::vector<std::tuple<std::string, double, std::string, std::uint32_t>> out;
            out.reserve(r.signals().size());
            for (const astra::Signal& s : r.signals()) {
              out.emplace_back(s.name, *s.value, s.unit, s.divisor);
            }
            return out;
          },
          "Every published (name, value, unit, divisor) tuple, copied out, in registration "
          "order -- the same order as a vehicle's own combined state buffer for its rigid-body "
          "and state-owner signals (ADR-0021 R4).")
      .def(
          "find",
          [](const astra::SignalRegistry& r, std::string_view name) -> std::optional<double> {
            const double* value = r.find(name);
            if (value == nullptr) {
              return std::nullopt;
            }
            return *value;
          },
          nb::arg("name"), "The named signal's current value, or None if no such signal exists.");

  // M3 PR 5: nothing here yet needs mutation/configuration from Python (Non-goals, docs/plans/
  // pr5-nanobind-bindings.md) -- domain_enabled() is the one const, policy-only accessor that
  // exists to bind.
  nb::class_<astra::HealthMonitor>(m, "HealthMonitor",
                                   "Read-only from Python; counters/signals are read through "
                                   "Simulation.registry (e.g. \"health.nan\"), not this type.")
      .def("domain_enabled", &astra::HealthMonitor::domain_enabled);

  // M3 PR 5 (Design decision 5): the authoritative state-layout metadata -- no registry-name
  // parsing. state is a LIVE view (Design decision 6/Joe's plan review minor fold iii): it aliases
  // Vehicle's own combined-state storage directly and changes under step()/run(), unlike
  // Simulation.derivative()'s always-freshly-allocated, independent return array.
  nb::class_<astra::Vehicle>(m, "Vehicle", "One vehicle's own state and signal namespace.")
      .def_prop_ro("name", [](const astra::Vehicle& v) { return v.name(); })
      .def_prop_ro("index", &astra::Vehicle::index)
      .def_prop_ro(
          "state",
          [](const astra::Vehicle& v) {
            const std::span<const double> s = v.combined_state();
            return nb::ndarray<nb::numpy, const double, nb::ndim<1>>(s.data(), {s.size()});
          },
          nb::rv_policy::reference_internal,
          "The full combined state buffer (rigid-body block + every state-owning model's own "
          "slice, ADR-0021 R4): a live, read-only view -- changes under step()/run(), unlike "
          "Simulation.derivative()'s own always-fresh return array. Length matches "
          "state_owner_slices' own offsets plus astra.POSITION/VELOCITY/QUATERNION/"
          "ANGULAR_RATE's fixed rigid-body layout.")
      .def_prop_ro(
          "state_owner_slices",
          [](const astra::Vehicle& v) {
            std::vector<std::tuple<std::string, std::size_t, std::size_t>> out;
            out.reserve(v.state_owner_slices().size());
            for (const astra::NamedStateSlice& slice : v.state_owner_slices()) {
              out.emplace_back(std::string(slice.name), slice.offset, slice.count);
            }
            return out;
          },
          "Every state-owning model's own (name, offset, count) slice of `state`, beyond the "
          "rigid-body block (always offset 0, count astra.STATE_SIZE), in combined-buffer order "
          "-- the authoritative named-slice layout ADR-0021 R4 describes.");

  // M3 PR 5: the first PR to bind Simulation itself. for_testing is deliberately NOT bound (D2/
  // PLAN.md's own PR 5 line, docs/plans/pr5-nanobind-bindings.md) -- a decision, not a gap.
  nb::class_<astra::Simulation>(
      m, "Simulation", "A runnable simulation built from a Config (docs/architecture.md §4).")
      .def_static("from_config", &astra::Simulation::from_config, nb::arg("config"),
                  nb::arg("plugin_dir"),
                  nb::sig("def from_config(config: Config, plugin_dir: str | os.PathLike[str]) "
                          "-> Simulation"),
                  "Builds and initializes every vehicle's models from config. Raises a "
                  "PluginError subclass or ConfigError naming the first unconsumed key.")
      .def(
          "step", [](astra::Simulation& s) { return PyHaltReason(s.step()); },
          "Advances exactly one major frame (docs/architecture.md §5).")
      .def(
          "run",
          [](astra::Simulation& s, std::uint64_t frames) { return PyHaltReason(s.run(frames)); },
          nb::arg("frames"), "Steps `frames` major frames, or until one halts.")
      .def(
          "run", [](astra::Simulation& s) { return PyHaltReason(s.run()); },
          "Steps the frame count read from \"sim.frames\" at from_config time -- an overload of "
          "run(frames) above, not a separate method (nanobind resolves by call signature, like "
          "pybind11).")
      .def_prop_ro(
          "clock", [](astra::Simulation& s) -> const astra::VirtualClock& { return s.clock(); },
          nb::rv_policy::reference_internal)
      .def_prop_ro(
          "registry",
          [](astra::Simulation& s) -> const astra::SignalRegistry& { return s.registry(); },
          nb::rv_policy::reference_internal)
      .def_prop_ro(
          "health", [](astra::Simulation& s) -> const astra::HealthMonitor& { return s.health(); },
          nb::rv_policy::reference_internal)
      .def_prop_ro(
          "vehicles",
          [](astra::Simulation& s) {
            std::vector<const astra::Vehicle*> out;
            out.reserve(s.vehicles().size());
            for (const astra::Vehicle& v : s.vehicles()) {
              out.push_back(&v);
            }
            return out;
          },
          nb::rv_policy::reference_internal,
          "Every vehicle (v1: exactly one, ADR-0011 notwithstanding). The non-const C++ overload "
          "is deliberately not bound -- it would let Python corrupt a running simulation's state "
          "outside any checked path.")
      .def_prop_ro("frames", &astra::Simulation::frames)
      .def("write_input", &astra::Simulation::write_input, nb::arg("name"), nb::arg("value"),
           "Writes and immediately commits value to the named write-seam input (ADR-0017), "
           "visible to the very next derivative()/step() call. Raises Error naming name if it is "
           "not registered or if value is itself non-finite.")
      .def("derivative", &simulation_derivative, nb::arg("state"),
           "The rigid-body-plus-model-state derivative at an arbitrary state, without advancing "
           "the clock -- bitwise identical to the real integrator's own k1 RK4 sub-stage for the "
           "same state (docs/plans/pr5-nanobind-bindings.md R9), not a second implementation. "
           "Raises ValueError for a mismatched state length; Error if no physics is configured, "
           "if wind is configured but has never been latched by a real step()/run() call, or if "
           "the probe evaluation itself faults.")
      .def("input_names", &simulation_input_names,
           "The first vehicle's own registered write-seam input names (ADR-0017), in "
           "InputRegistry::names()'s registration order -- a copy (docs/plans/"
           "pr7-linearize.md, D14/PR 6's own follow-up). A caller writing through write_input "
           "should discover names from here rather than hardcoding a literal.");
}
