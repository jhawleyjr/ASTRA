// A linear plant with model-owned state, the plant of M7b's oracle (a)
// (docs/plans/m7b-pr2-sampled-data.md, §2; docs/plans/m7b-closed-loop-oracles.md, "Oracle (a)"):
// x' = A x + B u, n continuous states owned through ADR-0021's hooks, m inputs bound by name
// through ADR-0017's write seam. It is a WrenchSource only because WrenchSource::bind_inputs is the
// one route to an input; its wrench is always zero, and it reads nothing of the rigid body or the
// environment.
//
// The plan's §5 charges the operation order of integrate_state() below in its roundoff bound, so
// that loop is part of the oracle: changing it changes the derivation (ADR-0034 Decision 4).
//
// Negative controls (the plan's §9; ADR-0034 Decision 5) are planted only through test_defect. Each
// emulates, at the plant's input, the observable effect of the core defect it names. step() runs
// every frame before the latch (simulation.cpp), so it still sees the previous frame's latched
// value:
//   late_command:       every frame integrates with the value latched at the previous frame;
//   long_hold:          only frame test_defect_frame does, so one hold lasts N + 1 frames;
//   stage_one_previous: RK4 stage 1 (t equal to the frame's own t) reads the previous value and
//                       stages 2 to 4 the new one, a command switched inside the step.
//
// Config keys (tests/plugins is skipped by scripts/config_keys.py, so they are documented here):
//   a                 | list of n rows of n numbers   | required | A, row-major
//   b                 | list of n rows of m numbers   | required | B, row-major
//   x0                | list of n numbers             | required | the initial state
//   inputs            | list of m input names         | required | registered with unit "1"
//   test_defect       | text                          | -        | late_command, long_hold or
//          stage_one_previous; absent, the plant is nominal
//   test_defect_frame | integer >= 0                  | -        | long_hold's frame; required then
//
// Published signals: <vehicle>.linear_plant.a.<i>.<j> and .b.<i>.<k>, the parsed doubles, for the
// plan's parse-identity check. The vehicle itself publishes <vehicle>.linear_plant.state.<i>.
//
// Per-frame methods allocate nothing: every vector is sized at init.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::ConfigError;

enum class Defect : std::uint8_t { kNone, kLateCommand, kLongHold, kStageOnePrevious };

std::string keyed(const Config& config, std::string_view key) {
  return config.path().empty() ? std::string{key} : config.path() + "." + std::string{key};
}

// A list under key, checked to be a sequence before it is iterated (lesson 2026-10-13: size() is 0
// for a scalar, so a quoted list would otherwise read as empty).
Config sequence_at(const Config& config, std::string_view key) {
  const Config entry = config.at(key);
  if (!entry.is_sequence() || entry.size() == 0) {
    throw ConfigError(config.origin(), keyed(config, key), "must be a non-empty list");
  }
  return entry;
}

std::vector<double> read_row(const Config& row, std::size_t expected) {
  if (!row.is_sequence() || row.size() != expected) {
    throw ConfigError(row.origin(), row.path(),
                      "must be a list of " + std::to_string(expected) + " numbers");
  }
  std::vector<double> values;
  values.reserve(expected);
  for (std::size_t i = 0; i < expected; ++i) {
    values.push_back(row.at(i).number());
  }
  return values;
}

// rows x cols, row-major, from a list of rows.
std::vector<double> read_matrix(const Config& config, std::string_view key, std::size_t rows,
                                std::size_t cols) {
  const Config matrix = sequence_at(config, key);
  if (matrix.size() != rows) {
    throw ConfigError(config.origin(), keyed(config, key),
                      "must have " + std::to_string(rows) + " rows");
  }
  std::vector<double> values;
  values.reserve(rows * cols);
  for (std::size_t i = 0; i < rows; ++i) {
    const std::vector<double> row = read_row(matrix.at(i), cols);
    values.insert(values.end(), row.begin(), row.end());
  }
  return values;
}

Defect defect_of(const Config& config) {
  if (!config.has("test_defect")) {
    return Defect::kNone;
  }
  const std::string name = config.text("test_defect");
  if (name == "late_command") {
    return Defect::kLateCommand;
  }
  if (name == "long_hold") {
    return Defect::kLongHold;
  }
  if (name == "stage_one_previous") {
    return Defect::kStageOnePrevious;
  }
  throw ConfigError(config.origin(), keyed(config, "test_defect"), "unknown test defect: " + name);
}

class LinearPlant final : public astra::WrenchSource {
 public:
  void init(const Config& config, astra::SignalRegistry& registry) override {
    origin_ = std::string(config.origin());
    config_path_ = config.path();
    const Config a = sequence_at(config, "a");
    n_ = a.size();
    const Config inputs = sequence_at(config, "inputs");
    m_ = inputs.size();
    a_ = read_matrix(config, "a", n_, n_);
    b_ = read_matrix(config, "b", n_, m_);
    x0_ = read_row(sequence_at(config, "x0"), n_);
    for (std::size_t k = 0; k < m_; ++k) {
      input_names_.push_back(inputs.at(k).text());
    }
    defect_ = defect_of(config);
    if (defect_ == Defect::kLongHold) {
      const std::int64_t frame = config.integer("test_defect_frame");
      if (frame < 0) {
        throw ConfigError(origin_, keyed(config, "test_defect_frame"), "must not be negative");
      }
      defect_frame_ = static_cast<std::uint64_t>(frame);
    }
    u_.assign(m_, nullptr);
    pre_latch_.assign(m_, 0.0);
    // Published after every vector above is sized, so no pointer below can move.
    try {
      for (std::size_t i = 0; i < n_; ++i) {
        for (std::size_t j = 0; j < n_; ++j) {
          registry.add("linear_plant.a." + std::to_string(i) + "." + std::to_string(j),
                       &a_[(i * n_) + j], "");
        }
        for (std::size_t k = 0; k < m_; ++k) {
          registry.add("linear_plant.b." + std::to_string(i) + "." + std::to_string(k),
                       &b_[(i * m_) + k], "");
        }
      }
    } catch (const astra::Error& e) {
      throw ConfigError(origin_, config_path_, e.what());
    }
  }

  // ADR-0017: one input per configured name; u_[k] is the registry-owned latched value.
  void bind_inputs(astra::InputRegistry& inputs, astra::SignalRegistry& registry) override {
    for (std::size_t k = 0; k < m_; ++k) {
      u_[k] = inputs.add(registry, input_names_[k], "1");
    }
  }

  void reset() override {}

  // Before this frame's latch: the latched values still hold the previous frame's commands.
  void step(const astra::StepContext& ctx) override {
    for (std::size_t k = 0; k < m_; ++k) {
      pre_latch_[k] = *u_[k];
    }
    frame_ = ctx.frame;
    frame_t_ = ctx.t;
  }

  [[nodiscard]] std::string_view name() const override { return "linear_plant"; }

  [[nodiscard]] astra::Wrench wrench_b(
      const astra::VehicleState& /*vs*/,
      const astra::EnvironmentSample& /*env*/) const noexcept override {
    return {};
  }

  [[nodiscard]] int state_count() const noexcept override { return static_cast<int>(n_); }

  void initial_state(double* q) const noexcept override {
    for (std::size_t i = 0; i < n_; ++i) {
      q[i] = x0_[i];
    }
  }

  void bind_state(const double* own_stage_slice) override { x_ = own_stage_slice; }

  // The plan's §2 and §5: this loop's operation order is what F's bound charges.
  void integrate_state(double t, const astra::VehicleState& /*vs*/,
                       const astra::EnvironmentSample& /*env*/,
                       double* dq) const noexcept override {
    for (std::size_t i = 0; i < n_; ++i) {
      double acc = 0.0;
      for (std::size_t j = 0; j < n_; ++j) {
        acc += a_[(i * n_) + j] * x_[j];
      }
      for (std::size_t k = 0; k < m_; ++k) {
        acc += b_[(i * m_) + k] * input(k, t);
      }
      dq[i] = acc;
    }
  }

 private:
  // The input integrate_state() reads for input k at stage time t: the latched value, unless a
  // planted defect substitutes the previous frame's.
  [[nodiscard]] double input(std::size_t k, double t) const noexcept {
    switch (defect_) {
      case Defect::kNone:
        return *u_[k];
      case Defect::kLateCommand:
        return pre_latch_[k];
      case Defect::kLongHold:
        return frame_ == defect_frame_ ? pre_latch_[k] : *u_[k];
      case Defect::kStageOnePrevious:
        return t == frame_t_ ? pre_latch_[k] : *u_[k];
    }
    return *u_[k];
  }

  std::string origin_;
  std::string config_path_;
  std::size_t n_ = 0;
  std::size_t m_ = 0;
  std::vector<double> a_;
  std::vector<double> b_;
  std::vector<double> x0_;
  std::vector<std::string> input_names_;
  std::vector<const double*> u_;
  std::vector<double> pre_latch_;
  const double* x_ = nullptr;
  Defect defect_ = Defect::kNone;
  std::uint64_t defect_frame_ = 0;
  std::uint64_t frame_ = 0;
  double frame_t_ = 0.0;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<LinearPlant>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "linear_plant", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
