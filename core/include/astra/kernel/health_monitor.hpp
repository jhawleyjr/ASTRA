// Health checks that run during the frame loop (docs/architecture.md §15, PLAN.md M1 PR 4): NaN/Inf
// in the state vector or in any wrench contribution, and quaternion norm drift beyond 1e-6 before
// renormalization. Each check has a configured action -- off, warn, or halt -- read from the
// optional "sim.health" config block (keys "nan" and "quaternion", both defaulting to "halt"), and
// each fires a registered signal ("health.nan", "health.quaternion", "health.max_q_drift") so the
// event lands in the log regardless of what the check did. This class' shape is not pinned by any
// ADR (workflow.md's M1 plan-review trigger list); it is the one place that reads "sim.health" and
// decides warn/halt/off -- Vehicle and Simulation only report observations and propagate the
// result.
#pragma once

#include <cstdint>
#include <string_view>

#include "astra/interfaces/model.hpp"
#include "astra/interfaces/wrench.hpp"
#include "astra/kernel/state.hpp"

namespace astra {

class Config;          // core/include/astra/config/config.hpp
class SignalRegistry;  // core/include/astra/signals/signal_registry.hpp

enum class HealthCheck : std::uint8_t { kNone, kNonFiniteState, kNonFiniteWrench, kQuaternionNorm };
enum class HealthAction : std::uint8_t { kOff, kWarn, kHalt };

// Names for the four ADR-0003 state segments, built once at init by whoever calls check_state
// (Vehicle, from its own name_) so a halt names "v0.state.q_nb" for vehicle "v0" without a "v0"
// literal anywhere in core/ (ADR-0011: nothing here may assume there is exactly one vehicle, or
// that it is named "v0").
struct StateNames {
  std::string_view p_n;
  std::string_view v_b;
  std::string_view q_nb;
  std::string_view omega_b;
};

// Trivially copyable; no allocation, no std::string -- safe to return from the per-frame path
// (§14). source points at storage the caller owns for the run's lifetime (a plugin's config path,
// or one of Vehicle's own state-name strings), never at a temporary built per frame.
struct HaltReason {
  HealthCheck check = HealthCheck::kNone;
  std::string_view source;
  std::uint64_t frame = 0;
  std::uint32_t vehicle_index = 0;

  [[nodiscard]] explicit operator bool() const noexcept { return check != HealthCheck::kNone; }
};

class HealthMonitor {
 public:
  // Reads the optional "sim.health" map ("nan", "quaternion", each "off"/"warn"/"halt", default
  // "halt") and registers this monitor's three signals. Init-time only: may throw ConfigError
  // (unrecognized action string, or "sim.health" present but not a map); must run before
  // registry.freeze() and before Config::unknown_keys() is checked.
  void init(const Config& config, SignalRegistry& registry);

  // Checks one wrench contribution for NaN/Inf, naming source (the contributing plugin's path) on a
  // halt. Per-frame path: no allocation, no exceptions.
  [[nodiscard]] HaltReason check_wrench(const Wrench& wrench, std::string_view source,
                                        const StepContext& ctx,
                                        std::uint32_t vehicle_index) noexcept;

  // Checks a state vector: each ADR-0003 segment for NaN/Inf, naming the first bad one from names;
  // then, once every segment is finite, quaternion norm drift beyond 1e-6 (before renormalization),
  // naming names.q_nb. Per-frame path: no allocation, no exceptions.
  [[nodiscard]] HaltReason check_state(const StateVector& y, const StateNames& names,
                                       const StepContext& ctx,
                                       std::uint32_t vehicle_index) noexcept;

 private:
  HealthAction nan_action_ = HealthAction::kHalt;
  HealthAction quaternion_action_ = HealthAction::kHalt;
  double nan_count_ = 0.0;
  double quaternion_count_ = 0.0;
  double max_quaternion_drift_ = 0.0;
};

}  // namespace astra
