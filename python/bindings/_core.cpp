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
#include <utility>
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
#include "astra/util/units.hpp"

namespace nb = nanobind;

namespace {

// M6 PR 4, review finding R-001: the argument of the unit conversions' float overloads. nanobind's
// own double caster falls back to PyFloat_AsDouble in its conversion pass, and numpy before 2.4
// (pyproject allows numpy>=2) converts a size-1 array of any dtype or shape there with only a
// DeprecationWarning. So an array the array overload refused (int, bool, float32, big-endian,
// 2-D) would come back as a Python float. This caster refuses anything whose ndim is not 0 before
// that fallback, whatever numpy's version. A float, an int, a bool, a numpy scalar and a 0-d array
// still convert as every other _core float parameter does.
struct UnitsScalar {
  double value;
};

}  // namespace

namespace nanobind::detail {

template <>
struct type_caster<UnitsScalar> {
  NB_TYPE_CASTER(UnitsScalar, const_name("float"))

  bool from_python(handle src, uint8_t flags, cleanup_list* cleanup) noexcept {
    if (PyFloat_CheckExact(src.ptr()) == 0) {
      const object ndim = getattr(src, "ndim", none());
      long dims = -1;
      if (!ndim.is_none() && (!try_cast(ndim, dims) || dims != 0)) {
        return false;
      }
    }
    make_caster<double> inner;
    if (!inner.from_python(src, flags, cleanup)) {
      return false;
    }
    value.value = inner.value;
    return true;
  }

  static handle from_cpp(UnitsScalar src, rv_policy /*policy*/,
                         cleanup_list* /*cleanup*/) noexcept {
    return PyFloat_FromDouble(src.value);
  }
};

}  // namespace nanobind::detail

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
// Config::from_string with a half-built document. M7 PR 2: a dict, list or tuple that contains
// itself is a ConfigError naming the key where the cycle closes. ancestors holds (by identity,
// Python's id) only the containers on the current path, so a value shared by two keys is legal
// and carried twice, and only a container reached again below itself is refused.
struct DictWalk {
  std::string_view origin;
  std::vector<std::pair<PyObject*, std::string>> ancestors;
};

YAML::Node value_to_yaml(nb::handle value, const std::string& path, DictWalk& walk);

YAML::Node container_to_yaml(nb::handle value, const std::string& path, DictWalk& walk) {
  for (const auto& [ancestor, ancestor_path] : walk.ancestors) {
    if (ancestor == value.ptr()) {
      const std::string where = ancestor_path.empty() ? "the top level" : "'" + ancestor_path + "'";
      throw astra::ConfigError(
          walk.origin, path,
          "a recursive value: a dict or list that contains itself, reached again from " + where);
    }
  }
  walk.ancestors.emplace_back(value.ptr(), path);
  YAML::Node node;
  if (nb::isinstance<nb::dict>(value)) {
    node = YAML::Node(YAML::NodeType::Map);
    for (auto item : nb::cast<nb::dict>(value)) {
      const auto key = nb::cast<std::string>(item.first);
      node[key] = value_to_yaml(item.second, path.empty() ? key : path + "." + key, walk);
    }
  } else {
    node = YAML::Node(YAML::NodeType::Sequence);
    std::size_t index = 0;
    for (auto item : nb::cast<nb::sequence>(value)) {
      const std::string segment = std::to_string(index++);
      node.push_back(value_to_yaml(item, path.empty() ? segment : path + "." + segment, walk));
    }
  }
  walk.ancestors.pop_back();
  return node;
}

YAML::Node value_to_yaml(nb::handle value, const std::string& path, DictWalk& walk) {
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
  if (nb::isinstance<nb::dict>(value) || nb::isinstance<nb::list>(value) ||
      nb::isinstance<nb::tuple>(value)) {
    return container_to_yaml(value, path, walk);
  }
  throw nb::type_error(
      "Config.from_dict: unsupported value type (expected dict, list, str, bool, int, or float)");
}

astra::Config config_from_dict(const nb::dict& d, std::string_view origin) {
  YAML::Emitter emitter;
  DictWalk walk{.origin = origin, .ancestors = {}};
  emitter << value_to_yaml(d, "", walk);
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

// M6 PR 4 (ADR-0002, docs/plans/m6-pr4-units.md): one core/util/units conversion over a 1-D float64
// array, so a whole log column converts with no Python-side multiply and no per-sample Python loop.
// Element i of the result is Convert(element i), the very function the float overload binds, so
// it is bitwise what the float form returns. Any stride that is a whole number of elements is read
// in place (a strided or reversed view, a column of a 2-D array); the result is a new C-contiguous
// array, never a view of the input. The argument is bound noconvert(): anything but a
// native-byte-order float64 1-D array is refused, never cast, so no rounding hides in an implicit
// dtype conversion. A stride that is not a whole number of elements (a float64 field of a packed
// structured dtype, 9 bytes) is refused too: nanobind's importer cannot express it (R-002, D7).
template <double (*Convert)(double) noexcept>
nb::ndarray<nb::numpy, double, nb::ndim<1>> convert_array(
    const nb::ndarray<const double, nb::ndim<1>, nb::device::cpu>& values) {
  const std::size_t size = values.shape(0);
  auto* data = new double[size];  // NOLINT(cppcoreguidelines-owning-memory)
  const auto view = values.view();
  for (std::size_t i = 0; i < size; ++i) {
    data[i] = Convert(view(i));  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  }
  nb::capsule owner(data, [](void* p) noexcept {
    delete[] static_cast<double*>(p);  // NOLINT(cppcoreguidelines-owning-memory)
  });
  return {data, {size}, owner};
}

// The float overload first, so a Python float binds in nanobind's first (no-conversion) pass; the
// array overload second. A numpy scalar, int, bool or 0-d array reaches the float overload in the
// conversion pass; an ndim >= 1 array the array overload refused cannot, because UnitsScalar
// refuses it (R-001), so the call raises TypeError.
template <double (*Convert)(double) noexcept>
void def_conversion(nb::module_& m, const char* name, const char* arg, const char* doc) {
  m.def(name, [](UnitsScalar x) { return Convert(x.value); }, nb::arg(arg), doc);
  m.def(name, &convert_array<Convert>, nb::arg(arg).noconvert(), doc);
}

// M6 PR 2: LogDiscardOutcome's Python name, for LogDiscardResult's repr.
std::string_view discard_outcome_name(astra::LogDiscardOutcome outcome) {
  switch (outcome) {
    case astra::LogDiscardOutcome::kNoLog:
      return "NO_LOG";
    case astra::LogDiscardOutcome::kDeleted:
      return "DELETED";
    case astra::LogDiscardOutcome::kMissing:
      return "MISSING";
    case astra::LogDiscardOutcome::kMismatch:
      return "MISMATCH";
    case astra::LogDiscardOutcome::kFailed:
      return "FAILED";
  }
  return "?";
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

  // M6 PR 4: the boundary conversions of core/util/units (ADR-0002), float -> float, or a 1-D
  // float64 array -> a new float64 array element by element. The constants are deliberately not
  // bound: a bound constant invites the Python-side multiply ADR-0002 forbids.
  def_conversion<&astra::units::feet_to_meters>(m, "feet_to_meters", "feet",
                                                "Feet to meters (core/util/units, ADR-0002).");
  def_conversion<&astra::units::meters_to_feet>(m, "meters_to_feet", "meters",
                                                "Meters to feet (core/util/units, ADR-0002).");
  def_conversion<&astra::units::knots_to_meters_per_second>(
      m, "knots_to_meters_per_second", "knots",
      "Knots to meters per second (core/util/units, ADR-0002).");
  def_conversion<&astra::units::meters_per_second_to_knots>(
      m, "meters_per_second_to_knots", "meters_per_second",
      "Meters per second to knots (core/util/units, ADR-0002).");
  def_conversion<&astra::units::degrees_to_radians>(
      m, "degrees_to_radians", "degrees", "Degrees to radians (core/util/units, ADR-0002).");
  def_conversion<&astra::units::radians_to_degrees>(
      m, "radians_to_degrees", "radians", "Radians to degrees (core/util/units, ADR-0002).");

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
  // M5 PR 4 (ADR-0025): Simulation.close_log()'s failure, naming the write that failed.
  [[maybe_unused]] const nb::exception<astra::LogError> log_error(m, "LogError", error.ptr());

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
      .value("NON_FINITE_ENVIRONMENT", astra::HealthCheck::kNonFiniteEnvironment)
      .value("TRANSPORT", astra::HealthCheck::kTransport)  // M4 PR 3, ADR-0022
      .value("LOG", astra::HealthCheck::kLog);             // M5 PR 4, ADR-0025

  // M6 PR 2 (ADR-0027 item 8): what Simulation.discard_log() did.
  nb::enum_<astra::LogDiscardOutcome>(m, "LogDiscardOutcome",
                                      "What Simulation.discard_log() did (ADR-0027 item 8).")
      .value("NO_LOG", astra::LogDiscardOutcome::kNoLog)
      .value("DELETED", astra::LogDiscardOutcome::kDeleted)
      .value("MISSING", astra::LogDiscardOutcome::kMissing)
      .value("MISMATCH", astra::LogDiscardOutcome::kMismatch)
      .value("FAILED", astra::LogDiscardOutcome::kFailed);

  nb::class_<astra::LogDiscardResult>(
      m, "LogDiscardResult",
      "Simulation.discard_log()'s result: outcome, the path it acted on (the .partial before the "
      "publication, log.path after it; empty without a log) and, when nothing was deleted, why.")
      .def_ro("outcome", &astra::LogDiscardResult::outcome)
      .def_ro("path", &astra::LogDiscardResult::path)
      .def_ro("detail", &astra::LogDiscardResult::detail)
      .def_prop_ro("deleted", &astra::LogDiscardResult::deleted,
                   "Whether this run's own file was unlinked.")
      .def("__repr__", [](const astra::LogDiscardResult& result) {
        // For a note a tool attaches to the exception it re-raises (astra._runkit).
        return "LogDiscardResult(outcome=" + std::string{discard_outcome_name(result.outcome)} +
               ", path='" + result.path + "', detail='" + result.detail + "')";
      });

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
      // M5 PR 4 (ADR-0025): the run log's two calls. The per-frame reservation is inside step().
      .def(
          "flush_log", [](astra::Simulation& s) { return PyHaltReason(s.flush_log()); },
          "Drains the run log's rings (a step()-driven caller's drain point). Returns the sticky "
          "LOG halt once there is one (a full ring, or a failed drain write, which this call "
          "itself can cause); falsy otherwise, and always without a log block.")
      .def("close_log", &astra::Simulation::close_log,
           "Finalizes the run log (ADR-0027): drains it, writes /summary and /final_state, sets "
           "complete = 1, closes the file and publishes it from '<log.path>.partial' to log.path "
           "with a no-replace rename, after checking the .partial is still this run's file. Raises "
           "LogError naming the write that failed (complete is then never set) or why the "
           "publication was refused (nothing renamed, the .partial kept). A second call does "
           "nothing after a success and raises the same LogError after a failure, or 'discarded' "
           "after discard_log(); without a log block it does nothing. A step() after it halts LOG. "
           "A Simulation dropped without it leaves only the .partial, marked finalized_by = "
           "'destructor'.")
      .def("discard_log", &astra::Simulation::discard_log,
           "ADR-0027 item 8, the only way a tool deletes a log file: stops writing (an open log is "
           "closed with no tail and no marker), then unlinks the file where it now lives (the "
           ".partial before the publication, log.path after it) only if that name is still this "
           "run's file; otherwise it deletes nothing and the result says why. Idempotent: returns "
           "the first result again. Afterwards close_log() raises 'discarded' and step() halts "
           "LOG. Without a log block: outcome NO_LOG.")
      .def_prop_ro("has_log", &astra::Simulation::has_log,
                   "Whether a log block configured a run log.")
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
           "should discover names from here rather than hardcoding a literal.")
      // M4 PR 6 (docs/plans/m4-pr6-lockstep-acceptance.md D1, Joe's Q2): a COPY of the serialized
      // exchange recorder in a new bytes object -- no view and no reference_internal, so holding
      // it cannot keep the Simulation (and its open transport) alive.
      .def(
          "exchange_recorder_bytes",
          [](const astra::Simulation& s) -> std::optional<nb::bytes> {
            const astra::ExchangeRecorder* recorder = s.exchange_recorder();
            if (recorder == nullptr) {
              return std::nullopt;
            }
            const std::vector<std::byte> data = recorder->serialize();
            return nb::bytes(data.data(), data.size());
          },
          "The exchange recorder (ADR-0022) serialized -- the fixed header, then the sim-to-sw, "
          "sw-to-sim and state streams -- as a new bytes object (a copy), or None without a "
          "transport block.");
}
