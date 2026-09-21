// Health checks that run during the frame loop (docs/architecture.md §15, PLAN.md M1 PR 4): NaN/Inf
// in the state vector or in any wrench contribution, quaternion norm drift beyond 1e-6 before
// renormalization, (ADR-0012) per-frame domain validity against a model-owned predicate, (ADR-0017)
// a non-finite write-seam input, and (ADR-0020) a non-finite environment sample
// (wind_n/atmosphere). Each check has a configured action -- off, warn, or halt
// -- read from the optional "sim.health" config block (keys "nan", "quaternion", "domain", "input",
// each defaulting to "halt"), and each fires a registered signal ("health.nan",
// "health.quaternion", "health.max_q_drift", "health.domain", "health.input") so the event lands in
// the log regardless of what the check did. report_non_finite_environment (ADR-0020) shares the
// "nan" key/signal/counter with check_state/check_wrench -- the same precedent those two already
// share, rather than a dedicated "environment" key -- so it fires no new registered signal of its
// own. This class' shape is not pinned by any ADR
// (workflow.md's M1 plan-review trigger list) EXCEPT for the domain check's own split, which
// ADR-0012 pins, and check_input's own shape, which ADR-0017 pins: both are the one place that
// reads their own "sim.health" key and decides warn/halt/off -- Vehicle and Simulation own the
// domain, write-seam, and (ADR-0020) environment predicates themselves and only report observations
// here, never model or registry pointers.
#pragma once

#include <cstdint>
#include <string_view>

#include "astra/interfaces/model.hpp"
#include "astra/interfaces/wrench.hpp"
#include "astra/kernel/state.hpp"

namespace astra {

class Config;          // core/include/astra/config/config.hpp
class SignalRegistry;  // core/include/astra/signals/signal_registry.hpp

enum class HealthCheck : std::uint8_t {
  kNone,
  kNonFiniteState,
  kNonFiniteWrench,
  kQuaternionNorm,
  kOutOfDomain,
  kNonFiniteInput,
  kNonFiniteEnvironment
};
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
  // Reads the optional "sim.health" map ("nan", "quaternion", "domain", each "off"/"warn"/"halt",
  // default "halt") and registers this monitor's four signals. Init-time only: may throw
  // ConfigError (unrecognized action string, or "sim.health" present but not a map); must run
  // before registry.freeze() and before Config::unknown_keys() is checked.
  void init(const Config& config, SignalRegistry& registry);

  // Checks one wrench contribution for NaN/Inf, naming source (the contributing plugin's path) on a
  // halt. Per-frame path: no allocation, no exceptions.
  //
  // PLAN.md M2 PR 3c (docs/plans/pr3c-substage-wrench.md): Vehicle::integrate() calls this once per
  // RK4 sub-stage per contribution (plus once per stage for the accumulated total) -- up to 8 calls
  // per frame now, not 2 -- evaluated uniformly every stage regardless of an earlier stage's own
  // result (a fault-dependent short-circuit was considered and rejected: it behaves inconsistently
  // between warn/halt actions, since this method returns a falsy HaltReason under warn regardless
  // of whether a fault was detected). This is a real, not cosmetic, change to what the "nan" signal
  // counts: it now counts stage-evaluations that saw a fault, not frames that did -- a genuinely
  // broken WrenchSource increments it up to 4x (8x counting the total check) what it did before
  // this PR, for what is really one underlying fault. See
  // tests/unit/kernel/simulation_halt_test.cpp's WarnActionRunsAllFramesAndCountsEveryNanDetection
  // for a worked, pinned example of the new count.
  [[nodiscard]] HaltReason check_wrench(const Wrench& wrench, std::string_view source,
                                        const StepContext& ctx,
                                        std::uint32_t vehicle_index) noexcept;

  // Checks a state vector: each ADR-0003 segment for NaN/Inf, naming the first bad one from names;
  // then, once every segment is finite, quaternion norm drift beyond 1e-6 (before renormalization),
  // naming names.q_nb. Per-frame path: no allocation, no exceptions.
  [[nodiscard]] HaltReason check_state(const StateVector& y, const StateNames& names,
                                       const StepContext& ctx,
                                       std::uint32_t vehicle_index) noexcept;

  // ADR-0012: "a HealthMonitor check over a model-owned predicate" -- the predicate itself
  // (EarthModel::in_domain/GravityModel::in_domain with the one-frame lookahead, or the atmosphere
  // altitude-vs-valid_range comparison) is evaluated by the caller (Vehicle::check_domain,
  // Simulation::step), because this monitor holds no Earth/gravity/atmosphere model pointers -- it
  // never has (see the class comment above). This is not a deviation from the ADR's design, only
  // from its literal phrasing "the monitor ... checks earth->altitude(vs) against
  // atmosphere->valid_range()": putting the predicate at the call site is what keeps it
  // model-owned, since the alternative is handing this class three pointers to hold. Named
  // report_, not check_, for that reason: the caller has already decided the predicate failed, at
  // most once per vehicle per frame; this call only turns that into the configured action, the
  // count, and the signal, exactly like apply() does for the other three checks. Per-frame path:
  // no allocation, no exceptions.
  [[nodiscard]] HaltReason report_out_of_domain(std::string_view source, const StepContext& ctx,
                                                std::uint32_t vehicle_index) noexcept;

  // ADR-0017: mirrors report_out_of_domain's shape exactly -- the predicate (a non-finite pending
  // write-seam value) is evaluated by the caller (InputRegistry::latch(), the one place every
  // writer's value is guaranteed to pass through before wrench_b() ever sees it), and this call
  // only turns that already-decided fault into the configured action, count, and signal. source
  // names the input itself (its own dotted name, e.g. "controls.elevator"), never the consuming
  // WrenchSource -- disambiguated across vehicles by vehicle_index alone, the same as
  // report_out_of_domain's own plugin-path source. Called at most once per vehicle per frame
  // (Simulation::step, using latch()'s own first-fault-wins return). Per-frame path: no allocation,
  // no exceptions.
  [[nodiscard]] HaltReason check_input(std::string_view source, const StepContext& ctx,
                                       std::uint32_t vehicle_index) noexcept;

  // ADR-0020: restores, core-side, the no-silent-NaN guarantee ADR-0018's removed `final
  // wrench_b()` guard used to give. Mirrors report_out_of_domain's own shape exactly, for the
  // identical reason: the predicate (whether env.wind_n or an env.atmosphere field is non-finite)
  // is evaluated by the caller (Vehicle::integrate()'s per-stage derivative functor) -- both
  // because this monitor holds no environment-sample-shaped state of its own to check without being
  // handed one (the same "model-owned predicate" reasoning ADR-0012's domain check already
  // established), and because a caller deciding whether to skip every configured
  // WrenchSource::wrench_b() call for this stage needs to know "was a fault detected," which this
  // method's own returned HaltReason cannot answer by itself: it is falsy under both "no fault" and
  // a configured "warn" action (a fault detected, but the run continues) -- discovered by this
  // session's own test run, which is why the predicate moved to the caller rather than living
  // inside this method as first drafted. This call only turns an already-decided fault into the
  // configured action, count, and signal, exactly like report_out_of_domain does. Shares the "nan"
  // sim.health action/signal/counter with check_state/check_wrench (Q1,
  // docs/plans/pr3d-adr0020-implementation.md), not a dedicated key.
  //
  // Caller contract: called only for a source already confirmed non-finite by the caller's own
  // check, and only after an atmosphere sample has already been confirmed in-range by a separate,
  // earlier domain pre-check against AtmosphereModel::valid_range() (report_out_of_domain, reused)
  // -- an atmosphere sample out of its own declared valid_range() is a legitimate, documented
  // condition (AtmoState's own all-NaN-outside-range contract), never a fault this method should
  // ever be called to report; that case is caught earlier, before atmosphere is even sampled, and
  // never reaches here. The caller skips every configured WrenchSource::wrench_b() call for that
  // RK4 sub-stage entirely whenever it calls this method at all, regardless of the configured
  // action -- no plugin ever computes a wrench from an environment sample known bad, restoring the
  // exact guarantee the removed `final` guard gave; under "warn" or "off" that stage's total
  // applied wrench is treated as zero, not the last-known-good value -- an explicit, disclosed
  // choice: a sample the caller has already found broken is not merely suspicious, evaluating any
  // WrenchSource against it would itself be the fault ADR-0018's guard existed to prevent.
  // Per-frame path: no allocation, no exceptions.
  [[nodiscard]] HaltReason report_non_finite_environment(std::string_view source,
                                                         const StepContext& ctx,
                                                         std::uint32_t vehicle_index) noexcept;

  // Whether the domain check should run at all -- what makes "domain: off restores current
  // behavior" literally true: a caller that checks this first pays no in_domain call, no extra
  // p_n_dot, no altitude() when the action is off, rather than evaluating the predicate and
  // discarding a falsey report.
  [[nodiscard]] bool domain_enabled() const noexcept {
    return domain_action_ != HealthAction::kOff;
  }

 private:
  HealthAction nan_action_ = HealthAction::kHalt;
  HealthAction quaternion_action_ = HealthAction::kHalt;
  HealthAction domain_action_ = HealthAction::kHalt;
  HealthAction input_action_ = HealthAction::kHalt;
  double nan_count_ = 0.0;
  double quaternion_count_ = 0.0;
  double max_quaternion_drift_ = 0.0;
  double domain_count_ = 0.0;
  double input_count_ = 0.0;
};

}  // namespace astra
