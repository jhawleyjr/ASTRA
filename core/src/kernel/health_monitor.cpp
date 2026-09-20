#include "astra/kernel/health_monitor.hpp"

#include <Eigen/Core>
#include <algorithm>
#include <cmath>
#include <string>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/frames.hpp"

namespace astra {

namespace {

// §15: "Quaternion norm drift beyond 1e-6 before renormalization (indicates integrator or rate
// problems)." PLAN.md M1 PR 4 pins the same value as the acceptance threshold.
constexpr double kQuaternionNormTolerance = 1e-6;

std::string keyed_path(const Config& config, std::string_view key) {
  return config.path().empty() ? std::string{key} : config.path() + "." + std::string{key};
}

HealthAction parse_action(const Config& health_config, std::string_view key) {
  const std::string value = health_config.text(key, "halt");
  if (value == "off") {
    return HealthAction::kOff;
  }
  if (value == "warn") {
    return HealthAction::kWarn;
  }
  if (value == "halt") {
    return HealthAction::kHalt;
  }
  throw ConfigError(health_config.origin(), keyed_path(health_config, key),
                    "must be one of off, warn, halt");
}

// Applies action to one detection: counts it (unless off) before deciding whether to halt, so the
// registered signal reflects a halting event too, not only a warned one (§15: "each a registered
// signal so it lands in the log" -- a halt that returned before counting would leave the signal
// reading zero for the very event that stopped the run).
[[nodiscard]] HaltReason apply(HealthAction action, double& count, HealthCheck check,
                               std::string_view source, const StepContext& ctx,
                               std::uint32_t vehicle_index) noexcept {
  if (action == HealthAction::kOff) {
    return {};
  }
  count += 1.0;
  if (action == HealthAction::kWarn) {
    return {};
  }
  return HaltReason{check, source, ctx.frame, vehicle_index};
}

}  // namespace

void HealthMonitor::init(const Config& config, SignalRegistry& registry) {
  if (config.has("sim.health")) {
    const Config health_config = config.at("sim.health");
    if (!health_config.is_map()) {
      throw ConfigError(health_config.origin(), health_config.path(), "must be a map");
    }
    nan_action_ = parse_action(health_config, "nan");
    quaternion_action_ = parse_action(health_config, "quaternion");
    domain_action_ = parse_action(health_config, "domain");
    input_action_ = parse_action(health_config, "input");
  }
  registry.add("health.nan", &nan_count_, "1");
  registry.add("health.quaternion", &quaternion_count_, "1");
  registry.add("health.max_q_drift", &max_quaternion_drift_, "1");
  registry.add("health.domain", &domain_count_, "1");
  registry.add("health.input", &input_count_, "1");
}

HaltReason HealthMonitor::check_wrench(const Wrench& wrench, std::string_view source,
                                       const StepContext& ctx,
                                       std::uint32_t vehicle_index) noexcept {
  if (wrench.force_b.allFinite() && wrench.moment_b.allFinite()) {
    return {};
  }
  return apply(nan_action_, nan_count_, HealthCheck::kNonFiniteWrench, source, ctx, vehicle_index);
}

HaltReason HealthMonitor::check_state(const StateVector& y, const StateNames& names,
                                      const StepContext& ctx,
                                      std::uint32_t vehicle_index) noexcept {
  const Eigen::Vector3d p_n = y.segment<state::kPositionSize>(state::kPosition);
  const Eigen::Vector3d v_b = y.segment<state::kVelocitySize>(state::kVelocity);
  const frames::Quaternion q_nb = y.segment<state::kQuaternionSize>(state::kQuaternion);
  const Eigen::Vector3d omega_b = y.segment<state::kAngularRateSize>(state::kAngularRate);

  if (!p_n.allFinite()) {
    return apply(nan_action_, nan_count_, HealthCheck::kNonFiniteState, names.p_n, ctx,
                 vehicle_index);
  }
  if (!v_b.allFinite()) {
    return apply(nan_action_, nan_count_, HealthCheck::kNonFiniteState, names.v_b, ctx,
                 vehicle_index);
  }
  if (!q_nb.allFinite()) {
    return apply(nan_action_, nan_count_, HealthCheck::kNonFiniteState, names.q_nb, ctx,
                 vehicle_index);
  }
  if (!omega_b.allFinite()) {
    return apply(nan_action_, nan_count_, HealthCheck::kNonFiniteState, names.omega_b, ctx,
                 vehicle_index);
  }

  const double drift = std::abs(q_nb.norm() - 1.0);
  max_quaternion_drift_ = std::max(max_quaternion_drift_, drift);
  if (drift <= kQuaternionNormTolerance) {
    return {};
  }
  return apply(quaternion_action_, quaternion_count_, HealthCheck::kQuaternionNorm, names.q_nb, ctx,
               vehicle_index);
}

HaltReason HealthMonitor::report_out_of_domain(std::string_view source, const StepContext& ctx,
                                               std::uint32_t vehicle_index) noexcept {
  return apply(domain_action_, domain_count_, HealthCheck::kOutOfDomain, source, ctx,
               vehicle_index);
}

HaltReason HealthMonitor::check_input(std::string_view source, const StepContext& ctx,
                                      std::uint32_t vehicle_index) noexcept {
  return apply(input_action_, input_count_, HealthCheck::kNonFiniteInput, source, ctx,
               vehicle_index);
}

}  // namespace astra
