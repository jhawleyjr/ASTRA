// The state vector layout (ADR-0003) and that Eigen compiles clean as a SYSTEM include under
// -Werror.
#include "astra/kernel/state.hpp"

#include <gtest/gtest.h>

static_assert(astra::StateVector::RowsAtCompileTime == astra::kStateSize);
static_assert(astra::StateVector::ColsAtCompileTime == 1);
static_assert(astra::kStateSize == 13);

namespace {

TEST(StateVector, LayoutPartitionsThirteenStatesAsAdr0003Says) {
  EXPECT_EQ(astra::state::kPosition, 0);
  EXPECT_EQ(astra::state::kVelocity, 3);
  EXPECT_EQ(astra::state::kQuaternion, 6);
  EXPECT_EQ(astra::state::kAngularRate, 10);
  EXPECT_EQ(astra::state::kAngularRate + astra::state::kAngularRateSize, astra::kStateSize);
}

TEST(StateVector, IsAFixedSizeVectorOfDoubles) {
  astra::StateVector x = astra::StateVector::Zero();
  EXPECT_EQ(x.size(), astra::kStateSize);
  // Identity attitude, scalar-first (ADR-0001): the only nonzero entry is w.
  x[astra::state::kQuaternion] = 1.0;
  EXPECT_DOUBLE_EQ(x.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion).norm(), 1.0);
  EXPECT_DOUBLE_EQ(x.norm(), 1.0);
}

}  // namespace
