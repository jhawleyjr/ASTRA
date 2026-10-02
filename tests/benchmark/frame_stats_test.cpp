// M6 PR 9 (docs/plans/m6-pr9-benchmark.md, D5; C-19's aggregation and margin oracles): the
// benchmark's statistics against samples whose answers are worked out by hand, and its JSON
// scalars.
#include "tests/benchmark/frame_stats.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

using astra::bench::FrameStats;
using astra::bench::json_number;
using astra::bench::json_string;
using astra::bench::median;
using astra::bench::nearest_rank;
using astra::bench::summarize;

std::vector<double> one_to(int n) {
  std::vector<double> values(static_cast<std::size_t>(n));
  std::iota(values.begin(), values.end(), 1.0);
  return values;
}

TEST(FrameStats, NearestRankIsTheCeilingOfTheFraction) {
  EXPECT_EQ(nearest_rank(100, 99, 100), 99U);   // 99.00 exactly
  EXPECT_EQ(nearest_rank(101, 99, 100), 100U);  // 99.99 rounds up
  EXPECT_EQ(nearest_rank(1000, 99, 100), 990U);
  EXPECT_EQ(nearest_rank(1, 99, 100), 1U);
  EXPECT_EQ(nearest_rank(200, 50, 100), 100U);
  EXPECT_EQ(nearest_rank(7, 100, 100), 7U);
  EXPECT_THROW((void)nearest_rank(0, 99, 100), std::invalid_argument);
  EXPECT_THROW((void)nearest_rank(5, 0, 100), std::invalid_argument);
  EXPECT_THROW((void)nearest_rank(5, 101, 100), std::invalid_argument);
}

TEST(FrameStats, OneToOneHundred) {
  const std::vector<double> samples = one_to(100);
  const FrameStats s = summarize(samples, 1000.0);
  EXPECT_EQ(s.count, 100U);
  EXPECT_DOUBLE_EQ(s.mean, 50.5);
  EXPECT_DOUBLE_EQ(s.median, 50.5);  // (50 + 51) / 2
  EXPECT_DOUBLE_EQ(s.p99, 99.0);     // the 99th smallest
  EXPECT_DOUBLE_EQ(s.max, 100.0);
  EXPECT_DOUBLE_EQ(s.first, 1.0);
  EXPECT_DOUBLE_EQ(s.frames_per_second, 100.0 / 5050.0);
  EXPECT_DOUBLE_EQ(s.margin_p99, 1.0 - (99.0 / 1000.0));
  EXPECT_DOUBLE_EQ(s.margin_max, 1.0 - (100.0 / 1000.0));
}

TEST(FrameStats, OrderDoesNotChangeTheStatisticsButFirstIsTheInputOrder) {
  std::vector<double> samples = one_to(100);
  std::ranges::reverse(samples);
  const FrameStats s = summarize(samples, 1000.0);
  EXPECT_DOUBLE_EQ(s.median, 50.5);
  EXPECT_DOUBLE_EQ(s.p99, 99.0);
  EXPECT_DOUBLE_EQ(s.max, 100.0);
  EXPECT_DOUBLE_EQ(s.first, 100.0);
}

TEST(FrameStats, OddCountTakesTheMiddleValue) {
  const std::vector<double> samples{3.0, 1.0, 2.0};
  const FrameStats s = summarize(samples, 10.0);
  EXPECT_DOUBLE_EQ(s.mean, 2.0);
  EXPECT_DOUBLE_EQ(s.median, 2.0);
  EXPECT_DOUBLE_EQ(s.p99, 3.0);  // ceil(2.97) = 3
  EXPECT_DOUBLE_EQ(s.max, 3.0);
  EXPECT_DOUBLE_EQ(s.first, 3.0);
  EXPECT_DOUBLE_EQ(median(std::vector<double>{4.0, 1.0, 3.0, 2.0}), 2.5);
}

TEST(FrameStats, OneThousandSamplesTakeThe990th) {
  const FrameStats s = summarize(one_to(1000), 1.0e6);
  EXPECT_DOUBLE_EQ(s.p99, 990.0);
  EXPECT_DOUBLE_EQ(s.median, 500.5);
}

TEST(FrameStats, SingleSample) {
  const std::vector<double> samples{0.5};
  const FrameStats s = summarize(samples, 1.0);
  EXPECT_EQ(s.count, 1U);
  EXPECT_DOUBLE_EQ(s.mean, 0.5);
  EXPECT_DOUBLE_EQ(s.median, 0.5);
  EXPECT_DOUBLE_EQ(s.p99, 0.5);
  EXPECT_DOUBLE_EQ(s.max, 0.5);
  EXPECT_DOUBLE_EQ(s.frames_per_second, 2.0);
  EXPECT_DOUBLE_EQ(s.margin_p99, 0.5);
}

TEST(FrameStats, MarginsAreFractionsOfDtAndGoNegativeOverBudget) {
  // A quarter of a 1 ms frame at p99 leaves three quarters; one sample of 2 ms is a whole frame
  // over.
  std::vector<double> samples(100, 2.5e-4);
  samples.back() = 2.0e-3;
  const FrameStats s = summarize(samples, 1.0e-3);
  EXPECT_DOUBLE_EQ(s.p99, 2.5e-4);
  EXPECT_DOUBLE_EQ(s.margin_p99, 0.75);
  EXPECT_DOUBLE_EQ(s.max, 2.0e-3);
  EXPECT_DOUBLE_EQ(s.margin_max, -1.0);
}

TEST(FrameStats, RefusesWhatHasNoStatistics) {
  EXPECT_THROW((void)summarize(std::vector<double>{}, 1.0), std::invalid_argument);
  EXPECT_THROW((void)summarize(std::vector<double>{0.0, 0.0}, 1.0), std::invalid_argument);
  EXPECT_THROW((void)summarize(std::vector<double>{1.0, -1.0}, 1.0), std::invalid_argument);
  EXPECT_THROW((void)summarize(std::vector<double>{std::numeric_limits<double>::quiet_NaN()}, 1.0),
               std::invalid_argument);
  EXPECT_THROW((void)summarize(std::vector<double>{1.0}, 0.0), std::invalid_argument);
  EXPECT_THROW((void)median(std::vector<double>{}), std::invalid_argument);
}

TEST(FrameStats, JsonStringEscapesQuotesBackslashesAndControlCharacters) {
  EXPECT_EQ(json_string("plain"), "\"plain\"");
  EXPECT_EQ(json_string("a\"b\\c\n\x01"), "\"a\\\"b\\\\c\\u000a\\u0001\"");
  EXPECT_EQ(json_string("\x1f"), "\"\\u001f\"");
  EXPECT_EQ(json_string("µs"), "\"µs\"");
}

TEST(FrameStats, JsonNumberRoundTripsAndRefusesNonFinite) {
  for (const double value : {0.1, 1.0e-7, 1.4e-6, 123456.789, 0.0, -2.5}) {
    const std::string text = json_number(value);
    double back = 0.0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), back);
    ASSERT_EQ(ec, std::errc{}) << text;
    EXPECT_EQ(end, text.data() + text.size()) << text;
    EXPECT_EQ(back, value) << text;
  }
  EXPECT_EQ(json_number(0.1), "0.1");
  EXPECT_THROW((void)json_number(std::numeric_limits<double>::infinity()), std::invalid_argument);
  EXPECT_THROW((void)json_number(std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
}

}  // namespace
