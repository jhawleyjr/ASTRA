// M6 PR 9 (docs/plans/m6-pr9-benchmark.md, D5): the frame-time benchmark's statistics and its JSON
// scalars, kept apart from the executable so frame_stats_test.cpp can check them against known
// samples (C-19's aggregation and margin oracles). Every time is in seconds (SI); nothing here
// reads a clock. Not on any per-frame path: these run once, after every frame has been timed and
// checked.
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace astra::bench {

// The 99th percentile, as a fraction: the nearest rank ceil(99 n / 100).
inline constexpr std::uint64_t kP99Numerator = 99;
inline constexpr std::uint64_t kPercentDenominator = 100;

struct FrameStats {
  std::size_t count = 0;
  double mean = 0.0;    // arithmetic mean
  double median = 0.0;  // the middle value, or the mean of the two middle values for an even count
  double p99 = 0.0;     // nearest rank: the ceil(0.99 n)-th smallest value
  double max = 0.0;
  double first = 0.0;              // the first sample (frame 0's cost), in input order
  double frames_per_second = 0.0;  // count / sum of the samples
  double margin_p99 = 0.0;         // 1 - p99 / dt: the fraction of dt left; negative is over budget
  double margin_max = 0.0;         // 1 - max / dt
};

// The 1-based nearest rank ceil(numerator * n / denominator), for 0 < numerator <= denominator and
// n > 0, computed in integers so no floating-point rounding moves it.
[[nodiscard]] inline std::size_t nearest_rank(std::size_t n, std::uint64_t numerator,
                                              std::uint64_t denominator) {
  if (n == 0 || numerator == 0 || denominator == 0 || numerator > denominator) {
    throw std::invalid_argument("nearest_rank: needs n > 0 and 0 < numerator <= denominator");
  }
  const std::uint64_t rank = ((numerator * n) + denominator - 1) / denominator;
  return static_cast<std::size_t>(rank);
}

// The median of `values` (the middle value, or the mean of the two middle values for an even
// count). Throws std::invalid_argument when empty.
[[nodiscard]] inline double median(std::span<const double> values) {
  if (values.empty()) {
    throw std::invalid_argument("median: no values");
  }
  std::vector<double> sorted(values.begin(), values.end());
  std::ranges::sort(sorted);
  const std::size_t mid = sorted.size() / 2;
  if (sorted.size() % 2 == 1) {
    return sorted[mid];
  }
  return (sorted[mid - 1] + sorted[mid]) / 2.0;
}

// D5's statistics of per-frame costs in seconds against the major frame dt. Throws
// std::invalid_argument for no samples, a non-finite or negative sample, samples summing to zero
// (no rate exists), or a non-finite or non-positive dt.
[[nodiscard]] inline FrameStats summarize(std::span<const double> samples, double dt) {
  if (samples.empty()) {
    throw std::invalid_argument("summarize: no samples");
  }
  if (!std::isfinite(dt) || dt <= 0.0) {
    throw std::invalid_argument("summarize: dt must be finite and positive");
  }
  double sum = 0.0;
  for (const double s : samples) {
    if (!std::isfinite(s) || s < 0.0) {
      throw std::invalid_argument("summarize: a sample is negative or not finite");
    }
    sum += s;
  }
  if (sum <= 0.0) {
    throw std::invalid_argument("summarize: the samples sum to zero");
  }
  std::vector<double> sorted(samples.begin(), samples.end());
  std::ranges::sort(sorted);
  const std::size_t n = sorted.size();
  FrameStats stats;
  stats.count = n;
  stats.mean = sum / static_cast<double>(n);
  stats.median = median(sorted);
  stats.p99 = sorted[nearest_rank(n, kP99Numerator, kPercentDenominator) - 1];
  stats.max = sorted.back();
  stats.first = samples.front();
  stats.frames_per_second = static_cast<double>(n) / sum;
  stats.margin_p99 = 1.0 - (stats.p99 / dt);
  stats.margin_max = 1.0 - (stats.max / dt);
  return stats;
}

// A JSON string literal, quotes included: '"' and '\' escaped, and every control character below
// U+0020 written as \u00XX. Other bytes pass through, so UTF-8 input stays UTF-8.
[[nodiscard]] inline std::string json_string(std::string_view text) {
  constexpr unsigned char kFirstPrintable = 0x20;
  constexpr std::string_view kHex = "0123456789abcdef";
  constexpr unsigned kNibble = 4;
  constexpr unsigned kNibbleMask = 0xF;
  std::string out = "\"";
  for (const char c : text) {
    const auto byte = static_cast<unsigned char>(c);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (byte < kFirstPrintable) {
      out += "\\u00";
      out += kHex[(byte >> kNibble) & kNibbleMask];
      out += kHex[byte & kNibbleMask];
    } else {
      out += c;
    }
  }
  out += '"';
  return out;
}

// A JSON number: the shortest text that reads back as the same double. Throws std::invalid_argument
// for a non-finite value, which JSON cannot hold.
[[nodiscard]] inline std::string json_number(double value) {
  if (!std::isfinite(value)) {
    throw std::invalid_argument("json_number: JSON has no non-finite number");
  }
  constexpr std::size_t kBuffer = 32;
  std::array<char, kBuffer> buffer{};
  const auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  if (ec != std::errc{}) {
    throw std::invalid_argument("json_number: to_chars failed");
  }
  return {buffer.data(), end};
}

}  // namespace astra::bench
