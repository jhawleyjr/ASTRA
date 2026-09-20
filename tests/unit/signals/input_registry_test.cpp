// InputRegistry (core/signals/input_registry.hpp, ADR-0017): SignalRegistry's mutable counterpart.
// add/write/find/latch/reset/freeze, the quiet-NaN default, get-or-create semantics for two
// instances of one plugin sharing a name (h), and latch()'s uniform, never-short-circuited commit
// (c).
#include "astra/signals/input_registry.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "astra/errors.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Error;
using astra::InputHandle;
using astra::InputRegistry;
using astra::SignalRegistry;

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

TEST(InputRegistry, EveryEntryDefaultsToQuietNanNeverAnUnspecifiedValue) {
  SignalRegistry registry;
  InputRegistry inputs;
  const double* canonical = inputs.add(registry, "controls.elevator", "rad");
  ASSERT_NE(canonical, nullptr);
  EXPECT_TRUE(std::isnan(*canonical));
}

TEST(InputRegistry, AddAlsoRegistersReadOnlyUnderTheIdenticalNameInSignalRegistry) {
  SignalRegistry registry;
  InputRegistry inputs;
  const double* canonical = inputs.add(registry, "controls.elevator", "rad");

  const double* read_side = registry.find("controls.elevator");
  ASSERT_NE(read_side, nullptr);
  EXPECT_EQ(read_side, canonical);
}

TEST(InputRegistry, WriteThenLatchCommitsThePendingValueIntoCanonicalStorage) {
  SignalRegistry registry;
  InputRegistry inputs;
  const double* canonical = inputs.add(registry, "controls.elevator", "rad");

  inputs.write("controls.elevator", 0.25);
  EXPECT_TRUE(std::isnan(*canonical)) << "write() alone must not touch canonical storage";

  EXPECT_TRUE(inputs.latch().empty());
  EXPECT_DOUBLE_EQ(*canonical, 0.25);
}

TEST(InputRegistry, WriteToAnUnregisteredNameThrowsNamingIt) {
  InputRegistry inputs;
  EXPECT_THROW(inputs.write("controls.elevator", 1.0), Error);
}

TEST(InputRegistry, AddRejectsAnEmptyName) {
  SignalRegistry registry;
  InputRegistry inputs;
  EXPECT_THROW(static_cast<void>(inputs.add(registry, "", "rad")), Error);
}

TEST(InputRegistry, AddAfterFreezeThrows) {
  SignalRegistry registry;
  InputRegistry inputs;
  static_cast<void>(inputs.add(registry, "controls.elevator", "rad"));
  inputs.write("controls.elevator", 0.0);
  inputs.freeze();
  EXPECT_THROW(static_cast<void>(inputs.add(registry, "controls.throttle", "1")), Error);
}

// (h): two instances of one plugin registering the same name share one value by default.
TEST(InputRegistry, ASecondAddForAnAlreadyRegisteredNameWithAMatchingUnitReturnsTheSamePointer) {
  SignalRegistry registry;
  InputRegistry inputs;
  const double* first = inputs.add(registry, "controls.throttle", "1");
  const double* second = inputs.add(registry, "controls.throttle", "1");
  EXPECT_EQ(first, second);

  inputs.write("controls.throttle", 0.75);
  ASSERT_TRUE(inputs.latch().empty());
  EXPECT_DOUBLE_EQ(*first, 0.75);
  EXPECT_DOUBLE_EQ(*second, 0.75);
}

TEST(InputRegistry, ASecondAddForAnAlreadyRegisteredNameWithAMismatchedUnitThrows) {
  SignalRegistry registry;
  InputRegistry inputs;
  static_cast<void>(inputs.add(registry, "controls.throttle", "1"));
  EXPECT_THROW(static_cast<void>(inputs.add(registry, "controls.throttle", "N")), Error);
}

TEST(InputRegistry, FindResolvesARepeatableWriteHandle) {
  SignalRegistry registry;
  InputRegistry inputs;
  const double* canonical = inputs.add(registry, "controls.elevator", "rad");

  const InputHandle handle = inputs.find("controls.elevator");
  ASSERT_TRUE(handle);
  handle.write(0.1);
  ASSERT_TRUE(inputs.latch().empty());
  EXPECT_DOUBLE_EQ(*canonical, 0.1);

  handle.write(0.2);
  ASSERT_TRUE(inputs.latch().empty());
  EXPECT_DOUBLE_EQ(*canonical, 0.2);
}

TEST(InputRegistry, FindOnAnUnregisteredNameReturnsAFalseyHandle) {
  InputRegistry inputs;
  const InputHandle handle = inputs.find("controls.elevator");
  EXPECT_FALSE(handle);
}

TEST(InputRegistry, FirstUnwrittenOrNonFiniteIsEmptyOnceEveryEntryHasAFiniteValue) {
  SignalRegistry registry;
  InputRegistry inputs;
  static_cast<void>(inputs.add(registry, "controls.elevator", "rad"));
  EXPECT_EQ(inputs.first_unwritten_or_non_finite(), "controls.elevator")
      << "never written yet -- pending is still the NaN default";

  inputs.write("controls.elevator", 0.0);
  EXPECT_TRUE(inputs.first_unwritten_or_non_finite().empty());
}

TEST(InputRegistry, FirstUnwrittenOrNonFiniteNamesANonFiniteWrittenValueToo) {
  SignalRegistry registry;
  InputRegistry inputs;
  static_cast<void>(inputs.add(registry, "controls.elevator", "rad"));
  inputs.write("controls.elevator", kNan);
  EXPECT_EQ(inputs.first_unwritten_or_non_finite(), "controls.elevator");
}

// (c): latch() never short-circuits -- every OTHER finite entry still commits this same call, warn
// or halt alike, and the bad entry's canonical value is left at its last good value (here, its NaN
// default, since nothing ever latched it before).
TEST(InputRegistry, LatchCommitsEveryFiniteEntryAndReportsOnlyTheFirstNonFiniteOne) {
  SignalRegistry registry;
  InputRegistry inputs;
  const double* good = inputs.add(registry, "controls.elevator", "rad");
  const double* bad = inputs.add(registry, "controls.rudder", "rad");
  inputs.write("controls.elevator", 0.3);
  inputs.write("controls.rudder", kNan);

  EXPECT_EQ(inputs.latch(), "controls.rudder");
  EXPECT_DOUBLE_EQ(*good, 0.3) << "the good entry still committed despite the other one's fault";
  EXPECT_TRUE(std::isnan(*bad)) << "the bad entry's canonical value keeps its last good value";
}

TEST(InputRegistry, LatchLeavesAPreviouslyGoodCanonicalValueUntouchedOnANewNonFiniteWrite) {
  SignalRegistry registry;
  InputRegistry inputs;
  const double* canonical = inputs.add(registry, "controls.elevator", "rad");
  inputs.write("controls.elevator", 0.4);
  ASSERT_TRUE(inputs.latch().empty());
  ASSERT_DOUBLE_EQ(*canonical, 0.4);

  inputs.write("controls.elevator", kNan);
  EXPECT_EQ(inputs.latch(), "controls.elevator");
  EXPECT_DOUBLE_EQ(*canonical, 0.4) << "the last successfully latched value, not NaN";
}

TEST(InputRegistry, FreezeSeedsCanonicalStorageFromTheValidatedPendingValue) {
  SignalRegistry registry;
  InputRegistry inputs;
  const double* canonical = inputs.add(registry, "controls.elevator", "rad");
  inputs.write("controls.elevator", 0.5);
  EXPECT_TRUE(std::isnan(*canonical)) << "not yet committed -- freeze() has not run";

  inputs.freeze();
  EXPECT_DOUBLE_EQ(*canonical, 0.5)
      << "freeze() is the one unconditional, init-time commit -- independent of latch()";
}

TEST(InputRegistry, ResetRestoresEveryEntryToItsFreezeTimeValueNotWhateverWasLastCommitted) {
  SignalRegistry registry;
  InputRegistry inputs;
  const double* canonical = inputs.add(registry, "controls.elevator", "rad");
  inputs.write("controls.elevator", 0.0);
  inputs.freeze();
  ASSERT_DOUBLE_EQ(*canonical, 0.0);

  inputs.write("controls.elevator", 5.0);
  ASSERT_TRUE(inputs.latch().empty());
  ASSERT_DOUBLE_EQ(*canonical, 5.0);

  inputs.reset();
  EXPECT_DOUBLE_EQ(*canonical, 0.0);
}

TEST(InputRegistry, ResetWithoutAPriorFreezeRestoresTheNanDefault) {
  SignalRegistry registry;
  InputRegistry inputs;
  const double* canonical = inputs.add(registry, "controls.elevator", "rad");
  inputs.write("controls.elevator", 5.0);
  ASSERT_TRUE(inputs.latch().empty());
  ASSERT_DOUBLE_EQ(*canonical, 5.0);

  inputs.reset();
  EXPECT_TRUE(std::isnan(*canonical))
      << "an entry that was never frozen (Simulation::for_testing) has no recorded good value";
}

}  // namespace
