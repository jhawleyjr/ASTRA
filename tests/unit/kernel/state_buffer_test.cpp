// core/kernel/state_buffer.hpp (ADR-0021, R2): sizing, copy_from's element-copy correctness, and
// the pointer-stability-across-move-construction guarantee Vehicle's own move into
// Simulation::vehicles_ depends on -- tested here in isolation (see also
// tests/unit/kernel/vehicle_state_coupling_test.cpp's
// PointerIntoStageBufferSurvivesTheVehicleMoveIntoSimulation for the same property proven end to
// end through the real construction path).
#include "astra/kernel/state_buffer.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <utility>

namespace {

using astra::StateBuffer;

TEST(StateBuffer, ConstructorSizesExactlyOnce) {
  const StateBuffer buffer(5);
  EXPECT_EQ(buffer.size(), 5U);
}

TEST(StateBuffer, DefaultConstructedElementsAreZero) {
  const StateBuffer buffer(3);
  for (std::size_t i = 0; i < buffer.size(); ++i) {
    EXPECT_EQ(buffer.data()[i], 0.0);
  }
}

TEST(StateBuffer, CopyFromCopiesEveryElementWithoutChangingSize) {
  StateBuffer source(4);
  StateBuffer dest(4);
  for (std::size_t i = 0; i < source.size(); ++i) {
    source.data()[i] = static_cast<double>(i) + 1.5;
  }
  dest.copy_from(source);
  ASSERT_EQ(dest.size(), source.size());
  for (std::size_t i = 0; i < dest.size(); ++i) {
    EXPECT_EQ(dest.data()[i], source.data()[i]);
  }
}

TEST(StateBuffer, CopyFromDoesNotChangeTheDestinationsOwnAddress) {
  StateBuffer source(6);
  StateBuffer dest(6);
  const double* address_before = dest.data();
  dest.copy_from(source);
  EXPECT_EQ(dest.data(), address_before)
      << "copy_from must never reallocate -- a bind_state()/SignalRegistry pointer already "
         "resolved into this buffer would otherwise be silently invalidated";
}

// ADR-0021: Vehicle itself is moved into Simulation::vehicles_ after init() has already resolved
// bind_state()/SignalRegistry pointers into its stage_ buffer -- this is the literal mechanism that
// move depends on, in isolation from Vehicle's own machinery.
TEST(StateBuffer, DataPointerIsStableAcrossMoveConstruction) {
  StateBuffer original(7);
  for (std::size_t i = 0; i < original.size(); ++i) {
    original.data()[i] = static_cast<double>(i);
  }
  const double* address_before = original.data();

  const StateBuffer moved(std::move(original));

  EXPECT_EQ(moved.data(), address_before);
  ASSERT_EQ(moved.size(), 7U);
  for (std::size_t i = 0; i < moved.size(); ++i) {
    EXPECT_EQ(moved.data()[i], static_cast<double>(i));
  }
}

}  // namespace
