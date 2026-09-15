// SignalRegistry (core/signals/signal_registry.hpp, docs/architecture.md §9, ADR-0002): add/find,
// units and divisor, duplicate and post-freeze rejection, and the nested PrefixScope that turns
// "dummy.steps" into "v0.dummy.steps" (ADR-0011).
#include "astra/signals/signal_registry.hpp"

#include <gtest/gtest.h>

#include "astra/errors.hpp"

namespace {

using astra::Error;
using astra::SignalRegistry;

TEST(SignalRegistry, AddThenFindResolvesByExactName) {
  SignalRegistry registry;
  const double value = 3.5;
  registry.add("aero.CL", &value, "1");

  const double* found = registry.find("aero.CL");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found, &value);
  EXPECT_EQ(registry.find("aero.CD"), nullptr);
}

TEST(SignalRegistry, SignalsExposesNameUnitAndDivisor) {
  SignalRegistry registry;
  const double value = 1.0;
  registry.add("dummy.t", &value, "s", 10);
  registry.freeze();

  ASSERT_EQ(registry.signals().size(), 1U);
  const astra::Signal& signal = registry.signals().front();
  EXPECT_EQ(signal.name, "dummy.t");
  EXPECT_EQ(signal.value, &value);
  EXPECT_EQ(signal.unit, "s");
  EXPECT_EQ(signal.divisor, 10U);
}

TEST(SignalRegistry, RejectsAnEmptyNameANullPointerAndAZeroDivisor) {
  SignalRegistry registry;
  const double value = 0.0;
  EXPECT_THROW(registry.add("", &value, "1"), Error);
  EXPECT_THROW(registry.add("x", nullptr, "1"), Error);
  EXPECT_THROW(registry.add("x", &value, "1", 0), Error);
}

TEST(SignalRegistry, RejectsADuplicateName) {
  SignalRegistry registry;
  const double a = 1.0;
  const double b = 2.0;
  registry.add("dummy.steps", &a, "1");
  EXPECT_THROW(registry.add("dummy.steps", &b, "1"), Error);
}

TEST(SignalRegistry, RejectsAddAfterFreeze) {
  SignalRegistry registry;
  const double value = 1.0;
  registry.freeze();
  EXPECT_TRUE(registry.frozen());
  EXPECT_THROW(registry.add("dummy.steps", &value, "1"), Error);
}

TEST(SignalRegistry, PrefixScopeNamespacesEveryAddWhileOpen) {
  SignalRegistry registry;
  const double a = 1.0;
  const double b = 2.0;
  {
    const SignalRegistry::PrefixScope scope(registry, "v0");
    registry.add("dummy.steps", &a, "1");
  }
  registry.add("unscoped", &b, "1");

  EXPECT_NE(registry.find("v0.dummy.steps"), nullptr);
  EXPECT_EQ(registry.find("dummy.steps"), nullptr);
  EXPECT_NE(registry.find("unscoped"), nullptr);
}

TEST(SignalRegistry, NestedPrefixScopesConcatenateAndRestoreOnExit) {
  SignalRegistry registry;
  const double a = 1.0;
  const double b = 2.0;
  {
    const SignalRegistry::PrefixScope outer(registry, "v0");
    {
      const SignalRegistry::PrefixScope inner(registry, "aero");
      registry.add("CL", &a, "1");
    }
    registry.add("mass.total", &b, "kg");
  }

  EXPECT_NE(registry.find("v0.aero.CL"), nullptr);
  EXPECT_NE(registry.find("v0.mass.total"), nullptr);
}

}  // namespace
