// M5 PR 4 (ADR-0025, docs/plans/m5-tooling.md PR 4 row): the run log through
// Simulation::from_config -- R2 (every signal a column with its unit, one row per frame), the core
// half of R3 (row k is t_k: frame-start state, frame-end signals, bitwise), R4 (byte identity,
// and independence from buffer_frames and drain timing), R6 (loud, state-preserving failures,
// sticky kLog), R6b's core half (finalization failures reach the caller, complete never set,
// idempotent close), and R7 (provenance, /plugins, /writes, /summary, /final_state). R5 (no
// allocation) is tests/physics/no_alloc_test.cpp's; the transport cases are
// simulation_transport_test.cpp's. Every file lives in a per-test temporary directory.
#include "astra/kernel/run_log.hpp"

#include <gtest/gtest.h>
#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "astra/build_info.hpp"
#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/hdf5_file.hpp"
#include "astra/util/sha256.hpp"

namespace {

namespace fs = std::filesystem;
using astra::HaltReason;
using astra::HealthCheck;
using astra::RunLog;
using astra::Simulation;
using astra::hdf5::File;

// A physics config with every kind of signal the log must place in time: rigid-body state and a
// model-owned state (state_owner), Dryden wind (an RNG draw per frame), the atmosphere and the
// derived signals, a write-seam input, health counters and the k1 wrench.
constexpr std::string_view kRich =
    "sim:\n"
    "  dt: 0.001\n"
    "  frames: 1000\n"
    "vehicle:\n"
    "  models:\n"
    "    - { plugin: tests/plugins/nan_wrench, force_x: 1.0 }\n"
    "    - { plugin: tests/plugins/state_owner, rate: 1.0 }\n"
    "    - { plugin: tests/plugins/input_consumer }\n"
    "  initial_controls:\n"
    "    controls:\n"
    "      elevator: 0.0\n"
    "physical:\n"
    "  earth: plugins/physical/earth/flat\n"
    "  gravity: plugins/physical/gravity/constant\n"
    "  atmosphere: plugins/physical/atmosphere/isa1976\n"
    "  wind: { plugin: plugins/physical/wind/dryden, seed: 3, w20_knots: 15.0, sigma_high: 3.0 }\n"
    "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 }\n"
    "initial:\n"
    "  state: { p_n: [0.0, 0.0, -100.0], v_b: [60.0, 0.0, 0.0] }\n";

std::string read_text(const fs::path& path) {
  std::ifstream in(path);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::vector<char> read_bytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string cruise_text() {
  return read_text(fs::path{ASTRA_AIRCRAFT_DIR} / "notional_transport/configs/cruise.yaml");
}

std::string with_log(std::string_view base, const fs::path& path, std::string_view extra = "") {
  std::string text{base};
  text += "\nlog:\n  path: " + path.string() + "\n";
  text += extra;
  return text;
}

Simulation build(const std::string& text) {
  return Simulation::from_config(astra::Config::from_string(text, "run log test"),
                                 ASTRA_TEST_PLUGIN_DIR);
}

bool same_halt(const HaltReason& a, const HaltReason& b) {
  return a.check == b.check && a.source == b.source && a.frame == b.frame &&
         a.vehicle_index == b.vehicle_index;
}

std::vector<double> signal_values(const Simulation& sim) {
  std::vector<double> values;
  for (const astra::Signal& signal : sim.registry().signals()) {
    values.push_back(*signal.value);
  }
  return values;
}

bool same_bits(double a, double b) {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

bool bitwise_equal(std::span<const double> a, std::span<const double> b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), same_bits);
}

// The logged table of one rate group, read back through the wrapper.
struct Table {
  std::vector<std::string> names;
  std::vector<std::string> units;
  std::vector<std::uint64_t> frames;
  std::vector<double> values;

  [[nodiscard]] std::size_t columns() const { return names.size(); }
  [[nodiscard]] std::size_t column(std::string_view name) const {
    const auto it = std::find(names.begin(), names.end(), name);
    EXPECT_NE(it, names.end()) << name;
    return static_cast<std::size_t>(it - names.begin());
  }
  [[nodiscard]] double at(std::size_t row, std::string_view name) const {
    return values[(row * columns()) + column(name)];
  }
  [[nodiscard]] std::span<const double> row(std::size_t r) const {
    return {values.data() + (r * columns()), columns()};
  }
};

Table read_table(const fs::path& path, std::string_view group = "rate_1") {
  const File file = File::open_read_only(path);
  const astra::hdf5::Group rate = file.open_group(group);
  const astra::hdf5::Dataset values = rate.open_dataset("values");
  return Table{values.attribute_strings("names"), values.attribute_strings("units"),
               rate.open_dataset("frame").read_uint64(), values.read_float64()};
}

bool has_complete(const fs::path& path) {
  return File::open_read_only(path).has_attribute("complete");
}

class RunLogTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir_ = fs::temp_directory_path() / ("astra_run_log_" + std::to_string(::getpid()) + "_" +
                                        info->test_suite_name() + "_" + info->name());
    fs::remove_all(dir_);
    fs::create_directories(dir_);
  }
  void TearDown() override {
    std::error_code ignored;
    fs::permissions(dir_, fs::perms::owner_all, ignored);
    fs::remove_all(dir_, ignored);
  }

  [[nodiscard]] fs::path path(const std::string& name) const { return dir_ / name; }

 private:
  fs::path dir_;
};

// --- RunLogConfig -------------------------------------------------------------------------------

using RunLogConfig = RunLogTest;

TEST_F(RunLogConfig, NoLogBlockMeansNoLoggerAndTheLogCallsDoNothing) {
  Simulation sim = build(std::string{kRich});
  EXPECT_FALSE(sim.has_log());
  EXPECT_FALSE(sim.run(10));
  EXPECT_FALSE(sim.flush_log());
  sim.close_log();
  EXPECT_EQ(sim.clock().frame(), 10U);
}

TEST_F(RunLogConfig, EachBadLogBlockIsAConfigErrorNamingItsKeyAndCreatesNoFile) {
  const fs::path target = path("run.h5");
  {
    std::ofstream out(path("existing.h5"));
    out << "never overwritten";
  }
  fs::create_directories(path("locked"));
  fs::permissions(path("locked"), fs::perms::owner_read | fs::perms::owner_exec);
  struct Case {
    std::string name;
    std::string log;
    std::string key;
  };
  const std::vector<Case> cases = {
      {"unknown key", "log:\n  path: " + target.string() + "\n  bogus: 1\n", "log.bogus"},
      {"existing file", "log:\n  path: " + path("existing.h5").string() + "\n", "log.path"},
      {"missing directory", "log:\n  path: " + path("nowhere/run.h5").string() + "\n", "log.path"},
      {"unwritable directory", "log:\n  path: " + path("locked/run.h5").string() + "\n",
       "log.path"},
      {"missing path", "log:\n  buffer_frames: 8\n", "log.path"},
      {"empty path", "log:\n  path: ''\n", "log.path"},
      {"zero buffer", "log:\n  path: " + target.string() + "\n  buffer_frames: 0\n",
       "log.buffer_frames"},
      {"negative buffer", "log:\n  path: " + target.string() + "\n  buffer_frames: -4\n",
       "log.buffer_frames"},
      {"not a map", "log: " + target.string() + "\n", "log"},
  };
  for (const Case& c : cases) {
    SCOPED_TRACE(c.name);
    try {
      static_cast<void>(build(std::string{kRich} + c.log));
      ADD_FAILURE() << "expected a ConfigError";
    } catch (const astra::ConfigError& error) {
      EXPECT_EQ(error.config_path(), c.key) << error.what();
    }
    EXPECT_FALSE(fs::exists(target)) << "a rejected config creates no file";
  }
  EXPECT_EQ(read_text(path("existing.h5")), "never overwritten");
}

// --- RunLogRows ---------------------------------------------------------------------------------

using RunLogRows = RunLogTest;

// R2: every registered signal is a column, in registry order, with its unit; one row per frame.
TEST_F(RunLogRows, EveryRegisteredSignalIsAColumnWithItsUnitAndEveryFrameARow) {
  Simulation sim = build(with_log(kRich, path("run.h5")));
  ASSERT_TRUE(sim.has_log());
  ASSERT_FALSE(sim.run());
  sim.close_log();
  const Table table = read_table(path("run.h5"));
  const std::span<const astra::Signal> signals = sim.registry().signals();
  ASSERT_EQ(table.columns(), signals.size());
  for (std::size_t i = 0; i < signals.size(); ++i) {
    EXPECT_EQ(table.names[i], signals[i].name);
    EXPECT_EQ(table.units[i], signals[i].unit) << signals[i].name;
  }
  ASSERT_EQ(table.frames.size(), 1000U);
  for (std::uint64_t k = 0; k < 1000; ++k) {
    ASSERT_EQ(table.frames[k], k);
  }
  EXPECT_EQ(table.values.size(), 1000U * signals.size());
  const File file = File::open_read_only(path("run.h5"));
  EXPECT_EQ(file.open_group("rate_1").open_dataset("values").rows(), 1000U);
}

// R3's core half: row k describes t_k. State columns (rigid-body and model-owned) equal the
// combined state read before step k; every other column equals its signal read after step k. Both
// bitwise. The published v0.state.* read after step k is t_{k+1}, row k+1's state, which is the
// timing ADR-0025 exists to avoid.
TEST_F(RunLogRows, RowKHoldsTheFrameStartStateAndTheFrameEndSignals) {
  Simulation sim = build(with_log(kRich, path("run.h5"), "  buffer_frames: 64\n"));
  const std::span<const astra::Signal> signals = sim.registry().signals();
  const std::span<const double> published = sim.vehicles()[0].published_state();
  constexpr std::size_t kFrames = 300;
  std::vector<std::vector<double>> start_state;
  std::vector<std::vector<double>> end_signals;
  for (std::size_t k = 0; k < kFrames; ++k) {
    const std::span<const double> state = sim.vehicles()[0].combined_state();
    start_state.emplace_back(state.begin(), state.end());
    ASSERT_FALSE(sim.step());
    end_signals.push_back(signal_values(sim));
    if (k % 50 == 49) {
      ASSERT_FALSE(sim.flush_log());
    }
  }
  sim.close_log();
  const Table table = read_table(path("run.h5"));
  ASSERT_EQ(table.frames.size(), kFrames);
  std::size_t state_columns = 0;
  for (std::size_t c = 0; c < signals.size(); ++c) {
    const double* pointer = signals[c].value;
    const bool is_state =
        pointer >= published.data() && pointer < published.data() + published.size();
    state_columns += is_state ? 1 : 0;
    for (std::size_t k = 0; k < kFrames; ++k) {
      const double logged = table.values[(k * signals.size()) + c];
      const double expected =
          is_state ? start_state[k][static_cast<std::size_t>(pointer - published.data())]
                   : end_signals[k][c];
      ASSERT_TRUE(same_bits(logged, expected))
          << signals[c].name << " at row " << k << ": " << logged << " vs " << expected;
    }
  }
  EXPECT_EQ(state_columns, 13U + 1U) << "the rigid-body block plus state_owner's one state";
  // The published state after step k is the start of frame k+1, not frame k.
  const std::size_t u = table.column("v0.state.v_b_0");
  bool lagged = false;
  for (std::size_t k = 0; k + 1 < kFrames; ++k) {
    EXPECT_EQ(end_signals[k][u], table.at(k + 1, "v0.state.v_b_0"));
    lagged = lagged || end_signals[k][u] != table.at(k, "v0.state.v_b_0");
  }
  EXPECT_TRUE(lagged) << "the fixture must move, or the check above proves nothing";
}

// run() drains between frames at the high-water mark (half the ring), so even a three-row ring
// never fills under run(); step() alone does not drain (RunLogFailure below).
TEST_F(RunLogRows, RunDrainsAtTheHighWaterMarkSoASmallRingNeverFills) {
  Simulation sim = build(with_log(kRich, path("run.h5"), "  buffer_frames: 3\n"));
  ASSERT_FALSE(sim.run(2100));
  sim.close_log();
  const Table table = read_table(path("run.h5"));
  ASSERT_EQ(table.frames.size(), 2100U);
  EXPECT_EQ(table.frames.back(), 2099U);
}

// Multi-rate groups, through RunLog directly: no model registers a divisor other than 1 today,
// so from_config cannot reach this. Signals of divisors 1, 2 and 3 land in /rate_1, /rate_2 and
// /rate_3, each with the frames that are multiples of its divisor.
TEST_F(RunLogRows, EachRateGroupHasItsOwnDatasetsAndDueFrames) {
  std::vector<double> sources(3, 0.0);
  astra::SignalRegistry registry;
  registry.add("every", sources.data(), "m");
  registry.add("second", &sources[1], "s", 2);
  registry.add("third", &sources[2], "1", 3);
  registry.freeze();
  const astra::RunLogSettings settings{path("rates.h5"), 4, "rates"};
  {
    RunLog log(settings, registry, {}, astra::RunLogProvenance{"{}\n", 0.001, {}});
    for (std::uint64_t frame = 0; frame < 12; ++frame) {
      ASSERT_TRUE(log.reserve(frame));
      sources[0] = static_cast<double>(frame);
      sources[1] = 100.0 + static_cast<double>(frame);
      sources[2] = 200.0 + static_cast<double>(frame);
      log.commit(frame);
      ASSERT_TRUE(log.drain());
    }
    log.close(astra::RunLogEnd{12, {}, {}});
  }
  const Table one = read_table(path("rates.h5"), "rate_1");
  const Table two = read_table(path("rates.h5"), "rate_2");
  const Table three = read_table(path("rates.h5"), "rate_3");
  EXPECT_EQ(one.names, std::vector<std::string>{"every"});
  EXPECT_EQ(two.names, std::vector<std::string>{"second"});
  EXPECT_EQ(three.units, std::vector<std::string>{"1"});
  EXPECT_EQ(one.frames.size(), 12U);
  EXPECT_EQ(two.frames, (std::vector<std::uint64_t>{0, 2, 4, 6, 8, 10}));
  EXPECT_EQ(three.frames, (std::vector<std::uint64_t>{0, 3, 6, 9}));
  EXPECT_EQ(three.values, (std::vector<double>{200.0, 203.0, 206.0, 209.0}));
}

// --- RunLogDeterminism --------------------------------------------------------------------------

using RunLogDeterminism = RunLogTest;

// R4: two runs of the Dryden cruise config, written to different paths, give byte-identical
// files. 3000 frames cross the 1024-row block twice and the default buffer's high-water mark once.
// The SHA-256 is printed for the evidence log.
TEST_F(RunLogDeterminism, TwoRunsOfTheDrydenCruiseToDifferentPathsAreByteIdentical) {
  fs::create_directories(path("a"));
  fs::create_directories(path("b"));
  for (const fs::path& target : {path("a") / "cruise.h5", path("b") / "other.h5"}) {
    Simulation sim = build(with_log(cruise_text(), target));
    ASSERT_FALSE(sim.run(3000));
    sim.close_log();
  }
  const std::vector<char> a = read_bytes(path("a") / "cruise.h5");
  ASSERT_FALSE(a.empty());
  EXPECT_TRUE(a == read_bytes(path("b") / "other.h5"));
  const std::string digest = astra::sha256::file_hex_digest(path("a") / "cruise.h5");
  EXPECT_EQ(digest, astra::sha256::file_hex_digest(path("b") / "other.h5"));
  std::cout << "R4 cruise.yaml 3000 frames, run log SHA-256: " << digest << " (" << a.size()
            << " bytes)\n";
}

// The file's bytes do not depend on buffer_frames or on when the caller drains (run_log.hpp), and
// a different seed changes them (the comparison is not vacuous).
TEST_F(RunLogDeterminism, BytesDoNotDependOnBufferFramesOrDrainTimingButOnTheRun) {
  {
    Simulation sim = build(with_log(cruise_text(), path("run.h5")));
    ASSERT_FALSE(sim.run(2500));
    sim.close_log();
  }
  {
    Simulation sim = build(with_log(cruise_text(), path("stepped.h5"), "  buffer_frames: 7\n"));
    for (int k = 0; k < 2500; ++k) {
      ASSERT_FALSE(sim.step());
      if (k % 5 == 3) {
        ASSERT_FALSE(sim.flush_log());
      }
    }
    sim.close_log();
  }
  {
    std::string text = cruise_text();
    text.replace(text.find("seed: 42"), std::string_view{"seed: 42"}.size(), "seed: 43");
    Simulation sim = build(with_log(text, path("seed43.h5"), "  buffer_frames: 1\n"));
    ASSERT_FALSE(sim.run(2500));
    sim.close_log();
  }
  EXPECT_TRUE(read_bytes(path("run.h5")) == read_bytes(path("stepped.h5")));
  EXPECT_FALSE(read_bytes(path("run.h5")) == read_bytes(path("seed43.h5")));
}

// --- RunLogFailure ------------------------------------------------------------------------------

using RunLogFailure = RunLogTest;

// R6: a ring with no free row halts kLog naming "log" before any side effect: the clock, the
// combined state and every signal (the Dryden wind among them, so its RNG did not draw) are
// bitwise what they were before the call, and equal to a run that simply stopped after 10 frames.
TEST_F(RunLogFailure, AFullRingHaltsBeforeAnySideEffectOfTheFrame) {
  Simulation sim = build(with_log(kRich, path("run.h5"), "  buffer_frames: 10\n"));
  for (int k = 0; k < 10; ++k) {
    ASSERT_FALSE(sim.step()) << k;
  }
  const std::vector<double> state_before{sim.vehicles()[0].combined_state().begin(),
                                         sim.vehicles()[0].combined_state().end()};
  const std::vector<double> signals_before = signal_values(sim);
  const HaltReason halt = sim.step();
  EXPECT_EQ(halt.check, HealthCheck::kLog);
  EXPECT_EQ(halt.source, "log");
  EXPECT_EQ(halt.frame, 10U);
  EXPECT_EQ(halt.vehicle_index, 0U);
  EXPECT_EQ(sim.clock().frame(), 10U);
  EXPECT_TRUE(bitwise_equal(sim.vehicles()[0].combined_state(), state_before));
  EXPECT_TRUE(bitwise_equal(signal_values(sim), signals_before));

  Simulation reference = build(std::string{kRich});
  ASSERT_FALSE(reference.run(10));
  EXPECT_TRUE(
      bitwise_equal(sim.vehicles()[0].combined_state(), reference.vehicles()[0].combined_state()));
  EXPECT_TRUE(bitwise_equal(signal_values(sim), signal_values(reference)));
}

// R6, the side effects themselves: counting_wind counts its scheduler step() (where a stochastic
// wind such as Dryden draws its RNG) and its begin_frame(), and dummy counts its model step().
// None of them runs for the frame the full ring refuses, or on any repeated call.
TEST_F(RunLogFailure, AFullRingRunsNoEnvironmentWindOrModelStep) {
  constexpr std::string_view kCounting =
      "sim:\n"
      "  dt: 0.001\n"
      "  frames: 100\n"
      "vehicle:\n"
      "  models:\n"
      "    - tests/plugins/dummy\n"
      "physical:\n"
      "  earth: plugins/physical/earth/flat\n"
      "  gravity: plugins/physical/gravity/constant\n"
      "  wind: tests/plugins/counting_wind\n"
      "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, "
      "izz: 1.0 }\n"
      "initial:\n"
      "  state: { p_n: [0.0, 0.0, -100.0], v_b: [60.0, 0.0, 0.0] }\n";
  Simulation sim = build(with_log(kCounting, path("run.h5"), "  buffer_frames: 5\n"));
  const auto count = [&sim](std::string_view name) {
    const double* value = sim.registry().find(name);
    EXPECT_NE(value, nullptr) << name;
    return value == nullptr ? -1.0 : *value;
  };
  for (int k = 0; k < 5; ++k) {
    ASSERT_FALSE(sim.step());
  }
  ASSERT_EQ(count("counting_wind.step_calls"), 5.0) << "the counters are live";
  ASSERT_EQ(count("counting_wind.begin_frame_calls"), 5.0);
  ASSERT_EQ(count("v0.dummy.steps"), 5.0);
  EXPECT_EQ(sim.step().check, HealthCheck::kLog);
  EXPECT_EQ(sim.run(3).check, HealthCheck::kLog);
  EXPECT_EQ(sim.flush_log().check, HealthCheck::kLog);
  EXPECT_EQ(sim.step().check, HealthCheck::kLog);
  EXPECT_EQ(count("counting_wind.step_calls"), 5.0);
  EXPECT_EQ(count("counting_wind.begin_frame_calls"), 5.0);
  EXPECT_EQ(count("v0.dummy.steps"), 5.0);
  EXPECT_EQ(sim.clock().frame(), 5U);
}

// R6: sticky without a transport. step(), run() and flush_log() all return the same halt and
// nothing advances; flush_log() does not drain after it.
TEST_F(RunLogFailure, AKLogHaltIsStickyWithoutATransport) {
  Simulation sim = build(with_log(kRich, path("run.h5"), "  buffer_frames: 10\n"));
  for (int k = 0; k < 10; ++k) {
    ASSERT_FALSE(sim.step()) << k;  // step() never drains
  }
  const HaltReason first = sim.step();
  ASSERT_EQ(first.check, HealthCheck::kLog);
  ASSERT_EQ(first.frame, 10U);
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(same_halt(sim.step(), first));
    EXPECT_TRUE(same_halt(sim.run(5), first));
    EXPECT_TRUE(same_halt(sim.flush_log(), first));
  }
  EXPECT_EQ(sim.clock().frame(), 10U);
  sim.close_log();
  const Table table = read_table(path("run.h5"));
  EXPECT_EQ(table.frames.size(), 10U) << "the rows before the halt are all kept";
  const File file = File::open_read_only(path("run.h5"));
  const astra::hdf5::Group summary = file.open_group("summary");
  EXPECT_EQ(summary.attribute_string("halt_check"), "LOG");
  EXPECT_EQ(summary.attribute_string("halt_source"), "log");
  EXPECT_EQ(summary.attribute_uint64("halt_frame"), 10U);
  EXPECT_EQ(file.attribute_int64("complete"), 1) << "a full ring loses no row: the file is whole";
}

// R6: an injected drain write failure halts kLog, sticky; close_log() then throws naming that
// write, complete stays unset, and a second close_log() throws the same.
TEST_F(RunLogFailure, AFailedDrainWriteHaltsKLogAndCloseThrowsNamingIt) {
  Simulation sim = build(with_log(kRich, path("run.h5"), "  buffer_frames: 64\n"));
  sim.inject_log_fault_for_testing(RunLog::Fault::kDrainWrite);
  const HaltReason halt = sim.run(2000);
  EXPECT_EQ(halt.check, HealthCheck::kLog);
  EXPECT_EQ(halt.source, "log");
  EXPECT_EQ(halt.frame, 1024U) << "the first block write, after frame 1023's drain";
  EXPECT_EQ(sim.clock().frame(), 1024U);
  EXPECT_TRUE(same_halt(sim.step(), halt));
  EXPECT_TRUE(same_halt(sim.flush_log(), halt));
  std::string first_message;
  try {
    sim.close_log();
    FAIL() << "expected LogError";
  } catch (const astra::LogError& error) {
    EXPECT_EQ(error.write(), "the rows of /rate_1");
    EXPECT_NE(std::string{error.what()}.find("injected"), std::string::npos) << error.what();
    first_message = error.what();
  }
  try {
    sim.close_log();
    FAIL() << "expected the same LogError again";
  } catch (const astra::LogError& error) {
    EXPECT_EQ(std::string{error.what()}, first_message);
  }
  EXPECT_FALSE(has_complete(path("run.h5")));
}

// R6, organically: the file may not grow past its size at init plus 4 KiB (RLIMIT_FSIZE, SIGXFSZ
// ignored), so HDF5's own write fails with EFBIG. The first 1024-row block fits HDF5's chunk
// cache; the second evicts it to disk, so the drain after frame 2047 fails: a kLog halt, then
// close_log() throws naming that drain and complete is never set. The process must also exit
// cleanly afterwards (libhdf5 1.10.10's exit-time teardown crashes after a failed close, which the
// wrapper's exit handler and RunLog's abandon() avoid; see hdf5_file.hpp).
TEST_F(RunLogFailure, ARealWriteErrorHaltsKLogThenCloseThrowsAndNeverSetsComplete) {
  Simulation sim = build(with_log(kRich, path("run.h5"), "  buffer_frames: 64\n"));
  const auto size = static_cast<rlim_t>(fs::file_size(path("run.h5")));
  rlimit saved{};
  ASSERT_EQ(::getrlimit(RLIMIT_FSIZE, &saved), 0);
  const auto previous = std::signal(SIGXFSZ, SIG_IGN);
  rlimit limited = saved;
  limited.rlim_cur = size + 4096;
  ASSERT_EQ(::setrlimit(RLIMIT_FSIZE, &limited), 0);
  const HaltReason halt = sim.run(20000);
  bool threw = false;
  try {
    sim.close_log();
  } catch (const astra::LogError& error) {
    threw = true;
    std::cout << "close_log: " << error.what() << "\n";
  }
  ASSERT_EQ(::setrlimit(RLIMIT_FSIZE, &saved), 0);
  static_cast<void>(std::signal(SIGXFSZ, previous));
  EXPECT_TRUE(threw) << "the file cannot be completed within its size limit";
  EXPECT_EQ(halt.check, HealthCheck::kLog);
  EXPECT_EQ(halt.source, "log");
  std::cout << "run halted kLog at frame " << halt.frame << "\n";
  EXPECT_FALSE(has_complete(path("run.h5")));
}

// --- RunLogClose --------------------------------------------------------------------------------

using RunLogClose = RunLogTest;

// R7: provenance, /plugins, /writes, /summary, /final_state, complete.
TEST_F(RunLogClose, WritesProvenancePluginsWritesSummaryFinalStateAndComplete) {
  const std::string text = with_log(kRich, path("run.h5"), "  buffer_frames: 100\n");
  Simulation sim = build(text);
  ASSERT_FALSE(sim.run(250));
  sim.write_input("controls.elevator", 0.25);
  ASSERT_FALSE(sim.run(250));
  sim.write_input("controls.elevator", -0.125);
  sim.write_input("controls.elevator", 0.5);
  ASSERT_FALSE(sim.run(100));
  EXPECT_THROW(sim.write_input("controls.bogus", 1.0), astra::Error);  // no effect, not recorded
  sim.close_log();

  const File file = File::open_read_only(path("run.h5"));
  EXPECT_EQ(file.attribute_int64("complete"), 1);
  EXPECT_EQ(file.attribute_uint64("log_format_version"), RunLog::kFormatVersion);
  EXPECT_EQ(file.attribute_string("git_describe"), astra::git_describe());
  EXPECT_EQ(file.attribute_string("core_version"), astra::core_version());
  EXPECT_EQ(file.attribute_uint64("core_api_version"), astra::kCoreApiVersion);
  EXPECT_EQ(file.attribute_float64("dt"), 0.001);
  // The config without its log block: it names no log key and loads back to the same document.
  const std::string stored = file.attribute_string("config");
  const astra::Config reloaded = astra::Config::from_string(stored, "stored");
  EXPECT_FALSE(reloaded.has("log"));
  EXPECT_EQ(reloaded.number("physical.wind.seed"), 3.0);
  EXPECT_EQ(stored, astra::Config::from_string(text, "original").emit_without("log"));

  const std::vector<astra::hdf5::RecordField> plugins = file.read_records("plugins");
  ASSERT_EQ(plugins.size(), 6U);
  const auto& paths = std::get<std::vector<std::string>>(plugins[0].values);
  const auto& digests = std::get<std::vector<std::string>>(plugins[5].values);
  EXPECT_EQ(plugins[5].name, "sha256");
  const std::vector<std::string> expected_paths = {"plugins/physical/earth/flat",
                                                   "plugins/physical/gravity/constant",
                                                   "plugins/physical/atmosphere/isa1976",
                                                   "plugins/physical/wind/dryden",
                                                   "tests/plugins/nan_wrench",
                                                   "tests/plugins/state_owner",
                                                   "tests/plugins/input_consumer",
                                                   "plugins/physical/mass/constant"};
  EXPECT_EQ(paths, expected_paths);
  for (std::size_t i = 0; i < paths.size(); ++i) {
    if (astra::PluginLoader::is_static()) {
      EXPECT_EQ(digests[i], "static");
    } else {
      EXPECT_EQ(digests[i],
                astra::sha256::file_hex_digest(fs::path{ASTRA_TEST_PLUGIN_DIR} /
                                               astra::PluginLoader::library_name(paths[i])));
    }
  }
  EXPECT_EQ(std::get<std::vector<std::uint64_t>>(plugins[3].values)[0], astra::kCoreApiVersion);

  const std::vector<astra::hdf5::RecordField> writes = file.read_records("writes");
  ASSERT_EQ(writes.size(), 3U);
  EXPECT_EQ(std::get<std::vector<std::uint64_t>>(writes[0].values),
            (std::vector<std::uint64_t>{250, 500, 500}));
  EXPECT_EQ(std::get<std::vector<std::string>>(writes[1].values),
            (std::vector<std::string>(3, "controls.elevator")));
  EXPECT_EQ(std::get<std::vector<double>>(writes[2].values),
            (std::vector<double>{0.25, -0.125, 0.5}));

  const astra::hdf5::Group summary = file.open_group("summary");
  EXPECT_EQ(summary.attribute_uint64("frames_run"), 600U);
  EXPECT_EQ(summary.attribute_string("halt_check"), "NONE");
  EXPECT_EQ(summary.attribute_string("halt_source"), "");
  for (const astra::Signal& signal : sim.registry().signals()) {
    if (signal.name.starts_with("health.")) {
      EXPECT_EQ(summary.attribute_float64(signal.name), *signal.value) << signal.name;
    }
  }
  const Table table = read_table(path("run.h5"));
  const std::vector<double> minimum = summary.open_dataset("min").read_float64();
  const std::vector<double> maximum = summary.open_dataset("max").read_float64();
  EXPECT_EQ(summary.open_dataset("min").attribute_strings("names"), table.names);
  ASSERT_EQ(minimum.size(), table.columns());
  for (std::size_t c = 0; c < table.columns(); ++c) {
    double lo = std::numeric_limits<double>::quiet_NaN();
    double hi = lo;
    for (std::size_t r = 0; r < table.frames.size(); ++r) {
      const double v = table.row(r)[c];
      if (!std::isnan(v)) {
        lo = std::isnan(lo) ? v : std::min(lo, v);
        hi = std::isnan(hi) ? v : std::max(hi, v);
      }
    }
    EXPECT_TRUE(same_bits(minimum[c], lo)) << table.names[c];
    EXPECT_TRUE(same_bits(maximum[c], hi)) << table.names[c];
  }
  EXPECT_EQ(table.at(599, "v0.controls.elevator"), 0.5);

  const astra::hdf5::Group final_state = file.open_group("final_state");
  EXPECT_EQ(final_state.attribute_uint64("frame"), 600U);
  EXPECT_EQ(final_state.attribute_int64("after_halt"), 0);
  const astra::hdf5::Dataset v0 = final_state.open_dataset("v0");
  EXPECT_TRUE(bitwise_equal(v0.read_float64(), sim.vehicles()[0].combined_state()));
  const std::vector<std::string> names = v0.attribute_strings("names");
  ASSERT_EQ(names.size(), 14U);
  EXPECT_EQ(names.front(), "v0.state.p_n_0");
  EXPECT_EQ(names[13], "v0.state_owner.state.0");
}

// A run that halts records its halt, and /final_state says it follows one.
TEST_F(RunLogClose, AHaltedRunRecordsItsHaltAndFinalStateAfterHalt) {
  const std::string base = read_text(fs::path{ASTRA_TEST_CONFIG_DIR} / "health_nan_wrench.yaml");
  Simulation sim = build(with_log(base, path("run.h5")));
  const HaltReason halt = sim.run();
  ASSERT_EQ(halt.check, HealthCheck::kNonFiniteWrench);
  sim.close_log();
  const File file = File::open_read_only(path("run.h5"));
  const astra::hdf5::Group summary = file.open_group("summary");
  EXPECT_EQ(summary.attribute_string("halt_check"), "NON_FINITE_WRENCH");
  EXPECT_EQ(summary.attribute_string("halt_source"), halt.source);
  EXPECT_EQ(summary.attribute_uint64("halt_frame"), halt.frame);
  EXPECT_EQ(summary.attribute_uint64("frames_run"), halt.frame);
  EXPECT_EQ(file.open_group("final_state").attribute_int64("after_halt"), 1);
  EXPECT_EQ(read_table(path("run.h5")).frames.size(), halt.frame) << "the halted frame has no row";
  EXPECT_EQ(file.attribute_int64("complete"), 1);
}

// Idempotent close; a step after it halts kLog (the log is final); flush_log() is then the halt.
TEST_F(RunLogClose, CloseIsIdempotentAndAStepAfterItHaltsKLog) {
  Simulation sim = build(with_log(kRich, path("run.h5")));
  ASSERT_FALSE(sim.run(10));
  sim.close_log();
  const std::vector<char> bytes = read_bytes(path("run.h5"));
  sim.close_log();
  EXPECT_TRUE(read_bytes(path("run.h5")) == bytes) << "a second close writes nothing";
  const HaltReason halt = sim.step();
  EXPECT_EQ(halt.check, HealthCheck::kLog);
  EXPECT_EQ(halt.frame, 10U);
  EXPECT_EQ(sim.clock().frame(), 10U);
  EXPECT_TRUE(same_halt(sim.flush_log(), halt));
  sim.close_log();
  EXPECT_TRUE(read_bytes(path("run.h5")) == bytes);
}

// R6b's core half: an injected failure at each finalization write throws LogError naming it,
// leaves complete unset, and a second close_log() throws the same.
TEST_F(RunLogClose, AFailedFinalizationWriteThrowsNamingItAndNeverSetsComplete) {
  const std::vector<std::pair<RunLog::Fault, std::string>> cases = {
      {RunLog::Fault::kWrites, "/writes"},
      {RunLog::Fault::kSummary, "/summary"},
      {RunLog::Fault::kFinalState, "/final_state"},
      {RunLog::Fault::kComplete, "complete"},
      {RunLog::Fault::kDrainWrite, "the rows of /rate_1"},
  };
  int index = 0;
  for (const auto& [fault, write] : cases) {
    SCOPED_TRACE(write);
    const fs::path target = path("run" + std::to_string(index++) + ".h5");
    Simulation sim = build(with_log(kRich, target));
    ASSERT_FALSE(sim.run(10));
    sim.inject_log_fault_for_testing(fault);
    std::string message;
    try {
      sim.close_log();
      ADD_FAILURE() << "expected LogError";
    } catch (const astra::LogError& error) {
      EXPECT_EQ(error.write(), write);
      EXPECT_EQ(error.file_path(), target.string());
      message = error.what();
    }
    try {
      sim.close_log();
      ADD_FAILURE() << "expected the same LogError";
    } catch (const astra::LogError& error) {
      EXPECT_EQ(std::string{error.what()}, message);
    }
    EXPECT_FALSE(has_complete(target));
  }
}

// The destructor completes a log nobody closed, and never after a failure.
TEST_F(RunLogClose, TheDestructorCompletesAnOpenLogButNeverAfterAFailure) {
  {
    Simulation sim = build(with_log(kRich, path("open.h5")));
    ASSERT_FALSE(sim.run(10));
  }
  EXPECT_EQ(File::open_read_only(path("open.h5")).attribute_int64("complete"), 1);
  EXPECT_EQ(read_table(path("open.h5")).frames.size(), 10U);
  {
    Simulation sim = build(with_log(kRich, path("failed.h5")));
    ASSERT_FALSE(sim.run(10));
    sim.inject_log_fault_for_testing(RunLog::Fault::kSummary);
  }
  EXPECT_FALSE(has_complete(path("failed.h5")));
}

// A moved-from Simulation owns no log; the moved-to one closes it once.
TEST_F(RunLogClose, AMovedSimulationCarriesItsLog) {
  Simulation first = build(with_log(kRich, path("run.h5")));
  Simulation second = std::move(first);
  ASSERT_FALSE(second.run(5));
  second.close_log();
  EXPECT_EQ(read_table(path("run.h5")).frames.size(), 5U);
}

}  // namespace
