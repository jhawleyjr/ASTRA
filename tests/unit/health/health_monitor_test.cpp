// docs/architecture.md §15: "tests/unit/health/ feeds a NaN, a broken quaternion ... into a stub
// simulation and asserts the right check fires with the right name." These tests drive
// HealthMonitor directly (no Simulation involved -- that end-to-end path is
// tests/unit/kernel/simulation_halt_test.cpp), so every check/action/counter/naming rule is pinned
// at the unit that owns it.
#include "astra/kernel/health_monitor.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/kernel/state.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::ConfigError;
using astra::HaltReason;
using astra::HealthCheck;
using astra::HealthMonitor;
using astra::InputRegistry;
using astra::SignalRegistry;
using astra::StateNames;
using astra::StateVector;
using astra::StepContext;
using astra::Wrench;

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr StateNames kNames{"v.state.p_n", "v.state.v_b", "v.state.q_nb", "v.state.omega_b"};

// health_fields, e.g. "nan: warn", goes verbatim into "sim: { health: { <health_fields> } }"; the
// empty default leaves "sim.health" entirely absent, so the monitor keeps its halt-by-default
// actions.
HealthMonitor make_monitor(SignalRegistry& registry, std::string_view health_fields = "") {
  const std::string yaml = health_fields.empty()
                               ? std::string{"sim: {}\n"}
                               : "sim: { health: { " + std::string{health_fields} + " } }\n";
  const Config config = Config::from_string(yaml, "health-monitor-fixture");
  HealthMonitor monitor;
  monitor.init(config, registry);
  return monitor;
}

// Identity attitude: |q_nb| = 1 exactly, so a test that only means to break one segment doesn't
// also trip the quaternion-norm check by starting from a zeroed q_nb.
StateVector valid_state() {
  StateVector y = StateVector::Zero();
  y(astra::state::kQuaternion) = 1.0;
  return y;
}

TEST(HealthMonitor, DefaultHaltReasonIsFalsey) { EXPECT_FALSE(HaltReason{}); }

TEST(HealthMonitor, NonFiniteWrenchForceHaltsNamingTheGivenSourceAndFrame) {
  SignalRegistry registry;
  HealthMonitor monitor = make_monitor(registry);
  Wrench wrench;
  wrench.force_b(0) = kNan;

  const HaltReason halt = monitor.check_wrench(wrench, "tests/plugins/nan_wrench",
                                               StepContext{.t = 0.5, .dt = 0.001, .frame = 500}, 0);
  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kNonFiniteWrench);
  EXPECT_EQ(halt.source, "tests/plugins/nan_wrench");
  EXPECT_EQ(halt.frame, 500U);
  EXPECT_EQ(halt.vehicle_index, 0U);
}

TEST(HealthMonitor, NonFiniteWrenchMomentHalts) {
  SignalRegistry registry;
  HealthMonitor monitor = make_monitor(registry);
  Wrench wrench;
  wrench.moment_b(1) = kInf;

  const HaltReason halt =
      monitor.check_wrench(wrench, "src", StepContext{.t = 0.0, .dt = 0.001, .frame = 0}, 0);
  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kNonFiniteWrench);
}

TEST(HealthMonitor, FiniteWrenchDoesNotHalt) {
  SignalRegistry registry;
  HealthMonitor monitor = make_monitor(registry);
  Wrench wrench;
  wrench.force_b(0) = 1.0;

  EXPECT_FALSE(
      monitor.check_wrench(wrench, "src", StepContext{.t = 0.0, .dt = 0.001, .frame = 0}, 0));
}

TEST(HealthMonitor, NonFiniteStateSegmentHaltsNamingTheFirstBadOne) {
  SignalRegistry registry;
  HealthMonitor monitor = make_monitor(registry);
  const StepContext ctx{.t = 0.0, .dt = 0.001, .frame = 1};

  {
    StateVector y = valid_state();
    y(astra::state::kPosition) = kNan;
    const HaltReason halt = monitor.check_state(y, kNames, ctx, 0);
    ASSERT_TRUE(halt);
    EXPECT_EQ(halt.check, HealthCheck::kNonFiniteState);
    EXPECT_EQ(halt.source, kNames.p_n);
  }
  {
    StateVector y = valid_state();
    y(astra::state::kVelocity) = kInf;
    const HaltReason halt = monitor.check_state(y, kNames, ctx, 0);
    ASSERT_TRUE(halt);
    EXPECT_EQ(halt.source, kNames.v_b);
  }
  {
    StateVector y = valid_state();
    y(astra::state::kQuaternion) = kNan;
    const HaltReason halt = monitor.check_state(y, kNames, ctx, 0);
    ASSERT_TRUE(halt);
    EXPECT_EQ(halt.check, HealthCheck::kNonFiniteState);
    EXPECT_EQ(halt.source, kNames.q_nb);
  }
  {
    StateVector y = valid_state();
    y(astra::state::kAngularRate) = kInf;
    const HaltReason halt = monitor.check_state(y, kNames, ctx, 0);
    ASSERT_TRUE(halt);
    EXPECT_EQ(halt.source, kNames.omega_b);
  }
}

TEST(HealthMonitor, QuaternionNormDriftAboveOneEMinusSixHaltsNamingQNb) {
  SignalRegistry registry;
  HealthMonitor monitor = make_monitor(registry);
  StateVector y = valid_state();
  y(astra::state::kQuaternion) = 1.0 + 2e-6;

  const HaltReason halt =
      monitor.check_state(y, kNames, StepContext{.t = 0.0, .dt = 0.001, .frame = 7}, 0);
  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kQuaternionNorm);
  EXPECT_EQ(halt.source, kNames.q_nb);
  EXPECT_EQ(halt.frame, 7U);
}

TEST(HealthMonitor, QuaternionNormDriftBelowOneEMinusSixDoesNotHalt) {
  SignalRegistry registry;
  HealthMonitor monitor = make_monitor(registry);
  StateVector y = valid_state();
  y(astra::state::kQuaternion) = 1.0 + 5e-7;

  EXPECT_FALSE(monitor.check_state(y, kNames, StepContext{.t = 0.0, .dt = 0.001, .frame = 7}, 0));
}

// Codex plan-review finding: a halting detection must still increment its registered counter, not
// only a warned one -- otherwise the very event that stopped the run reads as zero in the log.
TEST(HealthMonitor, AHaltingDetectionStillIncrementsItsRegisteredCounter) {
  SignalRegistry registry;
  HealthMonitor monitor = make_monitor(registry);
  const double* nan_count = registry.find("health.nan");
  ASSERT_NE(nan_count, nullptr);
  EXPECT_EQ(*nan_count, 0.0);

  Wrench wrench;
  wrench.force_b(0) = kNan;
  const HaltReason halt =
      monitor.check_wrench(wrench, "src", StepContext{.t = 0.0, .dt = 0.001, .frame = 0}, 0);
  ASSERT_TRUE(halt);
  EXPECT_EQ(*nan_count, 1.0);
}

TEST(HealthMonitor, WarnCountsWithoutHalting) {
  SignalRegistry registry;
  HealthMonitor monitor = make_monitor(registry, "nan: warn");
  const double* nan_count = registry.find("health.nan");
  ASSERT_NE(nan_count, nullptr);

  Wrench wrench;
  wrench.force_b(0) = kNan;
  const HaltReason halt =
      monitor.check_wrench(wrench, "src", StepContext{.t = 0.0, .dt = 0.001, .frame = 0}, 0);
  EXPECT_FALSE(halt);
  EXPECT_EQ(*nan_count, 1.0);
}

TEST(HealthMonitor, OffNeitherCountsNorHalts) {
  SignalRegistry registry;
  HealthMonitor monitor = make_monitor(registry, "nan: off");
  const double* nan_count = registry.find("health.nan");
  ASSERT_NE(nan_count, nullptr);

  Wrench wrench;
  wrench.force_b(0) = kNan;
  const HaltReason halt =
      monitor.check_wrench(wrench, "src", StepContext{.t = 0.0, .dt = 0.001, .frame = 0}, 0);
  EXPECT_FALSE(halt);
  EXPECT_EQ(*nan_count, 0.0);
}

TEST(HealthMonitor, RejectsAnUnrecognizedActionStringNamingTheKey) {
  SignalRegistry registry;
  const Config config =
      Config::from_string("sim: { health: { nan: loud } }\n", "bad-action-fixture");
  HealthMonitor monitor;
  try {
    monitor.init(config, registry);
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "sim.health.nan");
  }
}

TEST(HealthMonitor, RejectsANonMapSimHealth) {
  SignalRegistry registry;
  const Config config = Config::from_string("sim: { health: [1, 2] }\n", "non-map-health-fixture");
  HealthMonitor monitor;
  EXPECT_THROW(monitor.init(config, registry), ConfigError);
}

// ADR-0012: report_out_of_domain is the same apply()-through-a-counter shape as the other three
// checks, with the predicate itself evaluated by the caller (Vehicle::check_domain,
// Simulation::step) -- this monitor holds no Earth/gravity/atmosphere model pointers, so the
// caller has already decided the report fires before this call, at most once per vehicle per
// frame.
TEST(HealthMonitor, ReportOutOfDomainHaltsNamingTheGivenSourceAndFrame) {
  SignalRegistry registry;
  HealthMonitor monitor = make_monitor(registry);

  const HaltReason halt = monitor.report_out_of_domain(
      "plugins/physical/earth/round", StepContext{.t = 0.5, .dt = 0.001, .frame = 500}, 2);
  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kOutOfDomain);
  EXPECT_EQ(halt.source, "plugins/physical/earth/round");
  EXPECT_EQ(halt.frame, 500U);
  EXPECT_EQ(halt.vehicle_index, 2U);
}

TEST(HealthMonitor, DomainDefaultsToHaltAndCountsUnderHaltAndWarnLikeTheOtherThreeChecks) {
  {
    SignalRegistry registry;
    HealthMonitor monitor = make_monitor(registry);  // no sim.health block at all
    const double* domain_count = registry.find("health.domain");
    ASSERT_NE(domain_count, nullptr);
    const HaltReason halt =
        monitor.report_out_of_domain("src", StepContext{.t = 0.0, .dt = 0.001, .frame = 0}, 0);
    EXPECT_TRUE(halt) << "default action is halt, same as nan/quaternion";
    EXPECT_EQ(*domain_count, 1.0);
  }
  {
    SignalRegistry registry;
    HealthMonitor monitor = make_monitor(registry, "domain: warn");
    const double* domain_count = registry.find("health.domain");
    ASSERT_NE(domain_count, nullptr);
    const HaltReason halt =
        monitor.report_out_of_domain("src", StepContext{.t = 0.0, .dt = 0.001, .frame = 0}, 0);
    EXPECT_FALSE(halt);
    EXPECT_EQ(*domain_count, 1.0);
  }
}

TEST(HealthMonitor, DomainOffNeitherCountsNorHaltsAndDisablesTheCheck) {
  SignalRegistry registry;
  HealthMonitor monitor = make_monitor(registry, "domain: off");
  const double* domain_count = registry.find("health.domain");
  ASSERT_NE(domain_count, nullptr);
  EXPECT_FALSE(monitor.domain_enabled())
      << "what makes 'domain: off restores current behavior' true -- a caller checks this before "
         "ever evaluating an in_domain predicate";

  const HaltReason halt =
      monitor.report_out_of_domain("src", StepContext{.t = 0.0, .dt = 0.001, .frame = 0}, 0);
  EXPECT_FALSE(halt);
  EXPECT_EQ(*domain_count, 0.0);
}

TEST(HealthMonitor, DomainEnabledIsTrueUnderTheDefaultAndUnderWarn) {
  {
    SignalRegistry registry;
    HealthMonitor monitor = make_monitor(registry);
    EXPECT_TRUE(monitor.domain_enabled());
  }
  {
    SignalRegistry registry;
    HealthMonitor monitor = make_monitor(registry, "domain: warn");
    EXPECT_TRUE(monitor.domain_enabled());
  }
}

TEST(HealthMonitor, RejectsAnUnrecognizedDomainActionStringNamingTheKey) {
  SignalRegistry registry;
  const Config config =
      Config::from_string("sim: { health: { domain: loud } }\n", "bad-domain-action-fixture");
  HealthMonitor monitor;
  try {
    monitor.init(config, registry);
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "sim.health.domain");
  }
}

// ADR-0017: check_input mirrors report_out_of_domain's shape exactly -- source names the input
// itself, never the consuming WrenchSource.
TEST(HealthMonitor, NonFiniteInputHaltsNamingTheGivenSourceAndFrame) {
  SignalRegistry registry;
  HealthMonitor monitor = make_monitor(registry);

  const HaltReason halt =
      monitor.check_input("controls.elevator", StepContext{.t = 0.5, .dt = 0.001, .frame = 500}, 1);
  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kNonFiniteInput);
  EXPECT_EQ(halt.source, "controls.elevator");
  EXPECT_EQ(halt.frame, 500U);
  EXPECT_EQ(halt.vehicle_index, 1U);
}

TEST(HealthMonitor, InputDefaultsToHaltAndCountsUnderHaltAndWarnLikeTheOtherChecks) {
  {
    SignalRegistry registry;
    HealthMonitor monitor = make_monitor(registry);  // no sim.health block at all
    const double* input_count = registry.find("health.input");
    ASSERT_NE(input_count, nullptr);
    const HaltReason halt =
        monitor.check_input("src", StepContext{.t = 0.0, .dt = 0.001, .frame = 0}, 0);
    EXPECT_TRUE(halt) << "default action is halt, same as nan/quaternion/domain";
    EXPECT_EQ(*input_count, 1.0);
  }
  {
    SignalRegistry registry;
    HealthMonitor monitor = make_monitor(registry, "input: warn");
    const double* input_count = registry.find("health.input");
    ASSERT_NE(input_count, nullptr);
    const HaltReason halt =
        monitor.check_input("src", StepContext{.t = 0.0, .dt = 0.001, .frame = 0}, 0);
    EXPECT_FALSE(halt);
    EXPECT_EQ(*input_count, 1.0);
  }
}

TEST(HealthMonitor, InputOffNeitherCountsNorHalts) {
  SignalRegistry registry;
  HealthMonitor monitor = make_monitor(registry, "input: off");
  const double* input_count = registry.find("health.input");
  ASSERT_NE(input_count, nullptr);

  const HaltReason halt =
      monitor.check_input("src", StepContext{.t = 0.0, .dt = 0.001, .frame = 0}, 0);
  EXPECT_FALSE(halt);
  EXPECT_EQ(*input_count, 0.0);
}

TEST(HealthMonitor, RejectsAnUnrecognizedInputActionStringNamingTheKey) {
  SignalRegistry registry;
  const Config config =
      Config::from_string("sim: { health: { input: loud } }\n", "bad-input-action-fixture");
  HealthMonitor monitor;
  try {
    monitor.init(config, registry);
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "sim.health.input");
  }
}

// R8: a non-finite write() halts (default) naming the input, not the consuming WrenchSource; on
// warn, every OTHER finite entry still commits that frame, and the bad entry's canonical value
// keeps its last successfully latched value -- InputRegistry::latch()'s own partial-commit
// invariant (c), exercised together with the HealthMonitor check it feeds.
TEST(HealthMonitor,
     ANonFiniteWrittenInputHaltsByDefaultAndTheOtherFiniteEntryStillCommitsUnderWarn) {
  SignalRegistry registry;
  InputRegistry inputs;
  const double* elevator = inputs.add(registry, "controls.elevator", "rad");
  static_cast<void>(inputs.add(registry, "controls.rudder", "rad"));
  inputs.write("controls.elevator", 0.2);
  inputs.write("controls.rudder", kNan);

  {
    HealthMonitor monitor = make_monitor(registry);  // default: halt
    const std::string_view bad = inputs.latch();
    ASSERT_EQ(bad, "controls.rudder");
    const HaltReason halt =
        monitor.check_input(bad, StepContext{.t = 0.0, .dt = 0.001, .frame = 0}, 0);
    ASSERT_TRUE(halt);
    EXPECT_EQ(halt.check, HealthCheck::kNonFiniteInput);
    EXPECT_EQ(halt.source, "controls.rudder");
    EXPECT_DOUBLE_EQ(*elevator, 0.2) << "the good entry still committed despite the halt";
  }

  SignalRegistry warn_registry;
  InputRegistry warn_inputs;
  const double* warn_elevator = warn_inputs.add(warn_registry, "controls.elevator", "rad");
  static_cast<void>(warn_inputs.add(warn_registry, "controls.rudder", "rad"));
  warn_inputs.write("controls.elevator", 0.3);
  warn_inputs.write("controls.rudder", kNan);
  {
    HealthMonitor monitor = make_monitor(warn_registry, "input: warn");
    const std::string_view bad = warn_inputs.latch();
    ASSERT_EQ(bad, "controls.rudder");
    const HaltReason halt =
        monitor.check_input(bad, StepContext{.t = 0.0, .dt = 0.001, .frame = 0}, 0);
    EXPECT_FALSE(halt) << "warn: the run continues";
    EXPECT_DOUBLE_EQ(*warn_elevator, 0.3) << "the good entry still committed under warn too";
  }
}

// ADR-0020: restores, core-side, the no-silent-NaN guarantee ADR-0018's removed final wrench_b()
// guard gave. Mirrors report_out_of_domain's own shape and its own tests below: the predicate
// (whether wind_n/an atmosphere field is non-finite, and which one to name) is evaluated by the
// caller (Vehicle::integrate()), covered end to end by tests/unit/kernel/simulation_halt_test.cpp's
// NanAtmosphereInRangeHaltsNamingTheAtmospherePlugin and
// tests/unit/kernel/aero_propulsion_seam_test.cpp's NonFiniteAtmosphereHaltsNamingTheAtmosphere-
// PluginEndToEnd -- this method only ever turns an already-decided fault into the configured
// action, count, and signal, so these tests pin that shape directly, the same way
// ReportOutOfDomainHaltsNamingTheGivenSourceAndFrame below does for report_out_of_domain.
TEST(HealthMonitor, ReportNonFiniteEnvironmentHaltsNamingTheGivenSourceAndFrame) {
  SignalRegistry registry;
  HealthMonitor monitor = make_monitor(registry);

  const HaltReason halt = monitor.report_non_finite_environment(
      "tests/plugins/nan_atmosphere", StepContext{.t = 0.5, .dt = 0.001, .frame = 500}, 1);
  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.check, HealthCheck::kNonFiniteEnvironment);
  EXPECT_EQ(halt.source, "tests/plugins/nan_atmosphere");
  EXPECT_EQ(halt.frame, 500U);
  EXPECT_EQ(halt.vehicle_index, 1U);
}

// Shares the "nan" action/counter with check_wrench/check_state/check_state (Q1, docs/plans/
// pr3d-adr0020-implementation.md) -- no dedicated "environment" sim.health key.
TEST(HealthMonitor, EnvironmentSharesTheNanActionAndCounterWithTheOtherNonFiniteChecks) {
  SignalRegistry registry;
  HealthMonitor monitor = make_monitor(registry, "nan: warn");
  const double* nan_count = registry.find("health.nan");
  ASSERT_NE(nan_count, nullptr);

  const HaltReason halt = monitor.report_non_finite_environment(
      "src", StepContext{.t = 0.0, .dt = 0.001, .frame = 0}, 0);
  EXPECT_FALSE(halt) << "warn: the run continues";
  EXPECT_EQ(*nan_count, 1.0) << "still counted under the shared \"nan\" key";
}

TEST(HealthMonitor, EnvironmentOffNeitherCountsNorHalts) {
  SignalRegistry registry;
  HealthMonitor monitor = make_monitor(registry, "nan: off");
  const double* nan_count = registry.find("health.nan");
  ASSERT_NE(nan_count, nullptr);

  const HaltReason halt = monitor.report_non_finite_environment(
      "src", StepContext{.t = 0.0, .dt = 0.001, .frame = 0}, 0);
  EXPECT_FALSE(halt);
  EXPECT_EQ(*nan_count, 0.0);
}

// ADR-0011: nothing may assume there is exactly one vehicle named "v0" -- the names a halt reports
// come entirely from what the caller passes in, never a literal this class owns.
TEST(HealthMonitor, SourceNamesComeFromTheCallerNotAV0Literal) {
  SignalRegistry registry;
  HealthMonitor monitor = make_monitor(registry);
  constexpr StateNames kOtherNames{"orbiter.state.p_n", "orbiter.state.v_b", "orbiter.state.q_nb",
                                   "orbiter.state.omega_b"};
  StateVector y = valid_state();
  y(astra::state::kQuaternion) = 1.0 + 2e-6;

  const HaltReason halt =
      monitor.check_state(y, kOtherNames, StepContext{.t = 0.0, .dt = 0.001, .frame = 0}, 3);
  ASSERT_TRUE(halt);
  EXPECT_EQ(halt.source, "orbiter.state.q_nb");
  EXPECT_EQ(halt.vehicle_index, 3U);
}

}  // namespace
