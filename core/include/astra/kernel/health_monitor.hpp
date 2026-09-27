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

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "astra/interfaces/model.hpp"
#include "astra/interfaces/wrench.hpp"

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
  kNonFiniteEnvironment,
  // ADR-0022 (M4 PR 3): a lockstep transport failure. Unconditionally a halt, with no sim.health
  // action key, so HealthMonitor never produces it: LockstepExchange builds the HaltReason itself
  // (core/include/astra/kernel/lockstep_exchange.hpp names the failure kinds). Appended last so
  // every existing value keeps its integer.
  kTransport,
  // ADR-0025 (M5 PR 4): the run log could not take this frame's row (a full ring) or failed to
  // write a drain. Unconditionally a halt, sticky with or without a transport, with no sim.health
  // key: Simulation builds the HaltReason itself (source "log"). Appended last, as kTransport was.
  kLog
};
enum class HealthAction : std::uint8_t { kOff, kWarn, kHalt };

// The check's name as the Python enum spells it ("NON_FINITE_STATE", "LOG"): what the run log's
// /summary records (ADR-0025), so a reader maps it back with astra._core.HealthCheck[name].
[[nodiscard]] std::string_view health_check_name(HealthCheck check) noexcept;

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

// ADR-0021: one state-owning model's own slice of the combined state buffer, beyond the rigid-body
// block, for check_state's generic finiteness check below -- named per model (like check_wrench's
// own per-plugin, not per-component, naming), not per scalar.
struct NamedStateSlice {
  std::string_view name;
  std::size_t offset;
  std::size_t count;
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

  // Checks the combined state buffer: each ADR-0003 rigid-body segment (the buffer's own first 13
  // doubles) for NaN/Inf, naming the first bad one from names; then, once every rigid-body segment
  // is finite, quaternion norm drift beyond 1e-6 (before renormalization), naming names.q_nb; then
  // (ADR-0021) a generic finiteness check over each of extra_slices, naming that model on a halt --
  // no per-model-specific health logic beyond finiteness, and no quaternion-norm-style check for a
  // model-owned state (only the rigid body's own attitude has that meaning). Per-frame path: no
  // allocation, no exceptions.
  [[nodiscard]] HaltReason check_state(std::span<const double> y, const StateNames& names,
                                       std::span<const NamedStateSlice> extra_slices,
                                       const StepContext& ctx,
                                       std::uint32_t vehicle_index) noexcept;

  // ADR-0012: "a HealthMonitor check over a model-owned predicate" -- the predicate itself
  // (EarthModel::in_domain/GravityModel::in_domain with the one-frame lookahead, or the atmosphere
  // altitude-vs-valid_range comparison) is evaluated by the caller (Vehicle::check_domain,
  // Simulation::step, Vehicle::prepare_stage_environment), because this monitor holds no
  // Earth/gravity/atmosphere model pointers -- it never has (see the class comment above). This is
  // not a deviation from the ADR's design, only from its literal phrasing "the monitor ... checks
  // earth->altitude(vs) against atmosphere->valid_range()": putting the predicate at the call site
  // is what keeps it model-owned, since the alternative is handing this class three pointers to
  // hold. Named report_, not check_, for that reason: the caller has already decided the predicate
  // failed; this call only turns that into the configured action, the count, and the signal,
  // exactly like apply() does for the other three checks. Per-frame path: no allocation, no
  // exceptions.
  //
  // Call frequency is caller-dependent, not "at most once per vehicle per frame" in general
  // (ADR-0020, docs/plans/pr3d-adr0020-implementation.md): Vehicle::check_domain calls this at most
  // twice per frame (pre-step, frame 0 only; post-step, every frame); Simulation::step's own
  // post-step atmosphere-range check calls it at most once per frame;
  // Vehicle::prepare_stage_environment's own per-stage atmosphere domain pre-check (ADR-0020) can
  // call it up to four times per frame, once per RK4 sub-stage, the same "up to 4x (8x counting the
  // total) per frame, not deduplicated across stages" counting rule check_wrench's own comment
  // below already documents -- under domain: warn, a persistent excursion increments domain_count_
  // once per stage it is observed on, not once per frame.
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
  // check, and -- under the DEFAULT configuration (domain: halt or warn, not off) -- only after an
  // atmosphere sample has already been confirmed in-range by a separate, earlier domain pre-check
  // against AtmosphereModel::valid_range() (report_out_of_domain, reused) -- an atmosphere sample
  // out of its own declared valid_range() is a legitimate, documented condition (AtmoState's own
  // all-NaN-outside-range contract), never a fault this method is meant to report; that case is
  // normally caught earlier, before atmosphere is even sampled, and never reaches here.
  //
  // Interaction with "domain: off"/"nan: off" (docs/plans/pr3d-adr0020-implementation.md, Joe's own
  // follow-up): the domain pre-check above is itself gated on domain_enabled() -- when domain
  // checking is off, an out-of-valid_range() altitude is never intercepted before sampling, so
  // AtmosphereModel::at()'s own documented all-NaN-outside-range return value falls through to THIS
  // check instead, reported (if nan is not also off) as kNonFiniteEnvironment naming the atmosphere
  // plugin, not kOutOfDomain -- a legitimate range excursion is then indistinguishable, in the
  // signal/halt it produces, from a genuinely broken model. If nan is ALSO off, this method's own
  // apply() returns falsy with no count and no signal (the existing kOff short-circuit every other
  // check already has) -- but the caller still skips the wrench_sources_ loop regardless (see
  // below), so with both domain and nan off, an out-of-range or genuinely broken atmosphere sample
  // makes that stage run with a silent zero wrench: no halt, no counter, no signal anywhere records
  // that anything was wrong. This is "off" behaving exactly as every other check's "off" already
  // does elsewhere in this class (a plausible-looking-wrong outcome accepted, not a new exception),
  // stated explicitly here because the silence is otherwise easy to miss.
  //
  // Call frequency: like report_out_of_domain's own per-stage call site, this can be called up to
  // four times per frame, once per RK4 sub-stage, if the fault persists across stages under a
  // non-halting action -- the same "up to 4x (8x counting the total) per frame, not deduplicated"
  // rule check_wrench's own comment documents, sharing the same nan_count_. A persistent fault
  // under "nan: warn" therefore increments health.nan once per stage it is observed on, not once
  // per frame.
  //
  // The caller skips every configured WrenchSource::wrench_b() call for that RK4 sub-stage entirely
  // whenever it calls this method at all (or, per the off/off case above, whenever its own
  // predicate fires even with this method's action set to off), regardless of the configured action
  // -- no plugin ever computes a wrench from an environment sample known bad, restoring the exact
  // guarantee the removed `final` guard gave; under "warn" or "off" that stage's total applied
  // wrench is treated as zero, not the last-known-good value -- an explicit, disclosed choice: a
  // sample the caller has already found broken is not merely suspicious, evaluating any
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
