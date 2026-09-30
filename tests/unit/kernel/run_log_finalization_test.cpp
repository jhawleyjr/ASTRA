// M6 PR 2 (ADR-0027, docs/plans/m6-pr2-finalization.md): run-log finalization through
// Simulation::from_config and RunLog directly. Item 1 (the directory descriptor, the refusals,
// the chdir case J1, the two directory failure sites J2), item 3 (publication, its refusals, the
// identity check's three cases, a refused .partial surviving the destructor J11), item 4 (the
// destructor, in run_log_test.cpp), item 8 (discard_log() and the init-failure delete), R18 (byte
// identity across every drain pattern, for cruise and the divisor-10 fixture, a halting run
// included), confirmation 1 (the chunk index, from the library; the byte scan is test_log.py's),
// confirmation 2 (the frame column of a halted run stepped again) and R16's deterministic kill
// tests (J4: every killed file is read in a child process). Every file lives in a per-test
// temporary directory.
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/kernel/run_log.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/signals/hdf5_file.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

namespace fs = std::filesystem;
using astra::HaltReason;
using astra::HealthCheck;
using astra::LogDiscardOutcome;
using astra::LogDiscardResult;
using astra::RunLog;
using astra::Simulation;
using astra::hdf5::Dataset;
using astra::hdf5::File;
using KillPoint = astra::RunLogKillPoint;

constexpr std::string_view kSlowRateEntry =
    "    - { plugin: tests/plugins/slow_rate, divisor: 10 }\n";

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

void write_text(const fs::path& path, std::string_view text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
}

fs::path partial(const fs::path& path) { return fs::path{path.string() + ".partial"}; }

std::string cruise_text() {
  return read_text(fs::path{ASTRA_AIRCRAFT_DIR} / "notional_transport/configs/cruise.yaml");
}

// A model entry added to a config's one vehicle.models list.
std::string with_model(std::string text, std::string_view entry) {
  const std::string_view anchor = "  models:\n";
  const std::size_t at = text.find(anchor);
  EXPECT_NE(at, std::string::npos);
  text.insert(at + anchor.size(), entry);
  return text;
}

std::string cruise_slow_rate() { return with_model(cruise_text(), kSlowRateEntry); }

std::string fast_slow_rate() {
  return read_text(fs::path{ASTRA_TEST_CONFIG_DIR} / "log_slow_rate.yaml");
}

std::string with_log(std::string_view base, const fs::path& path, std::string_view extra = "") {
  std::string text{base};
  text += "\nlog:\n  path: " + path.string() + "\n";
  text += extra;
  return text;
}

Simulation build(const std::string& text) {
  return Simulation::from_config(astra::Config::from_string(text, "finalization test"),
                                 ASTRA_TEST_PLUGIN_DIR);
}

std::string log_error_text(Simulation& sim) {
  try {
    sim.close_log();
  } catch (const astra::LogError& error) {
    return error.what();
  }
  return "";
}

class RunLogFinalizationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir_ = fs::temp_directory_path() / ("astra_finalization_" + std::to_string(::getpid()) + "_" +
                                        info->test_suite_name() + "_" + info->name());
    fs::remove_all(dir_);
    fs::create_directories(dir_);
  }
  void TearDown() override {
    std::error_code ignored;
    fs::current_path(start_, ignored);
    fs::permissions(dir_, fs::perms::owner_all, ignored);
    for (const auto& entry : fs::recursive_directory_iterator(dir_, ignored)) {
      fs::permissions(entry.path(), fs::perms::owner_all, fs::perm_options::add, ignored);
    }
    fs::remove_all(dir_, ignored);
  }

  [[nodiscard]] fs::path path(const std::string& name) const { return dir_ / name; }

 private:
  fs::path dir_;
  fs::path start_ = fs::current_path();
};

// --- Item 1: where a log is written ---------------------------------------------------------

using RunLogPlacement = RunLogFinalizationTest;

// J1: a relative log.path is resolved once, at construction; a chdir during the run redirects
// neither the publication nor a delete. Decoys with the same names in the new directory prove it.
TEST_F(RunLogPlacement, AChdirMidRunRedirectsNeitherThePublicationNorADelete) {
  fs::create_directories(path("origin"));
  fs::create_directories(path("elsewhere"));
  write_text(path("elsewhere") / "published.h5", "decoy");
  write_text(path("elsewhere") / "published.h5.partial", "decoy partial");
  write_text(path("elsewhere") / "open.h5.partial", "decoy partial");
  const std::string base = fast_slow_rate();
  {
    fs::current_path(path("origin"));
    Simulation sim = build(with_log(base, "published.h5"));
    ASSERT_FALSE(sim.run(1500));
    fs::current_path(path("elsewhere"));
    sim.close_log();
    EXPECT_TRUE(fs::exists(path("origin") / "published.h5"));
    EXPECT_FALSE(fs::exists(path("origin") / "published.h5.partial"));
    // After the publication, discard_log() deletes log.path in the original directory.
    const LogDiscardResult result = sim.discard_log();
    EXPECT_EQ(result.outcome, LogDiscardOutcome::kDeleted) << result.detail;
    EXPECT_FALSE(fs::exists(path("origin") / "published.h5"));
  }
  {
    fs::current_path(path("origin"));
    Simulation sim = build(with_log(base, "open.h5"));
    ASSERT_FALSE(sim.run(1500));
    fs::current_path(path("elsewhere"));
    // Before the publication, it deletes the .partial in the original directory.
    const LogDiscardResult result = sim.discard_log();
    EXPECT_EQ(result.outcome, LogDiscardOutcome::kDeleted) << result.detail;
    EXPECT_FALSE(fs::exists(path("origin") / "open.h5.partial"));
  }
  EXPECT_EQ(read_text(path("elsewhere") / "published.h5"), "decoy");
  EXPECT_EQ(read_text(path("elsewhere") / "published.h5.partial"), "decoy partial");
  EXPECT_EQ(read_text(path("elsewhere") / "open.h5.partial"), "decoy partial");
}

// J2: with a directory descriptor, a missing directory fails where the parent is opened, and an
// unwritable one at the create. Both are ConfigErrors naming log.path, and neither creates a file.
TEST_F(RunLogPlacement, AMissingAndAnUnwritableDirectoryEachFailAtTheirOwnSite) {
  try {
    static_cast<void>(build(with_log(fast_slow_rate(), path("nowhere") / "run.h5")));
    ADD_FAILURE() << "expected a ConfigError";
  } catch (const astra::ConfigError& error) {
    EXPECT_EQ(error.config_path(), "log.path");
    EXPECT_NE(std::string{error.what()}.find("cannot open the directory"), std::string::npos)
        << error.what();
  }
  if (::geteuid() == 0) {
    GTEST_SKIP() << "root ignores directory permissions; the unwritable case cannot fail";
  }
  fs::create_directories(path("locked"));
  fs::permissions(path("locked"), fs::perms::owner_read | fs::perms::owner_exec);
  try {
    static_cast<void>(build(with_log(fast_slow_rate(), path("locked") / "run.h5")));
    ADD_FAILURE() << "expected a ConfigError";
  } catch (const astra::ConfigError& error) {
    EXPECT_EQ(error.config_path(), "log.path");
    EXPECT_NE(std::string{error.what()}.find("cannot create the file"), std::string::npos)
        << error.what();
  }
  EXPECT_TRUE(fs::is_empty(path("locked")));
}

// R16b: each refusal is a ConfigError naming log.path, raised before anything is created, and
// leaves what was there byte- and link-identical. A stale .partial belongs to the user.
TEST_F(RunLogPlacement, ExistingNamesAndAPartialSuffixAreRefusedAndLeftAlone) {
  write_text(path("existing.h5"), "never overwritten");
  write_text(path("stale.h5.partial"), "an earlier run's partial");
  fs::create_symlink(path("gone"), path("dangling.h5"));
  fs::create_symlink(path("gone"), path("dangling_partial.h5.partial"));
  struct Case {
    std::string name;
    std::string target;
    std::string says;
  };
  const std::vector<Case> cases = {
      {"existing log.path", "existing.h5", "already exists; a run log never overwrites"},
      {"dangling symlink at log.path", "dangling.h5", "already exists; a run log never overwrites"},
      {"stale .partial", "stale.h5", "stale partial log belongs to the user"},
      {"dangling symlink at the .partial", "dangling_partial.h5",
       "stale partial log belongs to the user"},
      {"a name ending .partial", "run.h5.partial", "ends in .partial"},
      {"a name that is not a file", "sub/..", "does not name a file"},
  };
  for (const Case& c : cases) {
    SCOPED_TRACE(c.name);
    try {
      static_cast<void>(build(with_log(fast_slow_rate(), path(c.target))));
      ADD_FAILURE() << "expected a ConfigError";
    } catch (const astra::ConfigError& error) {
      EXPECT_EQ(error.config_path(), "log.path");
      EXPECT_NE(std::string{error.what()}.find(c.says), std::string::npos) << error.what();
    }
  }
  EXPECT_EQ(read_text(path("existing.h5")), "never overwritten");
  EXPECT_EQ(read_text(path("stale.h5.partial")), "an earlier run's partial");
  EXPECT_FALSE(fs::exists(path("existing.h5.partial")));
  EXPECT_FALSE(fs::exists(path("stale.h5")));
  EXPECT_TRUE(fs::is_symlink(path("dangling.h5")));
  EXPECT_TRUE(fs::is_symlink(path("dangling_partial.h5.partial")));
  EXPECT_FALSE(fs::exists(path("run.h5.partial.partial")));
}

// --- Item 3: publication ------------------------------------------------------------------------

using RunLogPublication = RunLogFinalizationTest;

TEST_F(RunLogPublication, ANormalRunLeavesOnlyLogPathCompleteAndNoPartial) {
  {
    Simulation sim = build(with_log(fast_slow_rate(), path("run.h5")));
    EXPECT_TRUE(fs::exists(partial(path("run.h5"))));
    EXPECT_FALSE(fs::exists(path("run.h5")));
    ASSERT_FALSE(sim.run(2500));
    sim.close_log();
    EXPECT_FALSE(fs::exists(partial(path("run.h5"))));
  }
  const File file = File::open_read_only(path("run.h5"));
  EXPECT_EQ(file.attribute_int64("complete"), 1);
  EXPECT_FALSE(file.has_attribute("finalized_by"));
  EXPECT_EQ(file.open_group("rate_1").attribute_uint64("rows_committed"), 2500U);
  EXPECT_EQ(file.open_group("rate_10").attribute_uint64("rows_committed"), 250U);
  EXPECT_EQ(file.open_group("rate_10").open_dataset("frame").rows(), 250U);
}

// R16b: a file created at log.path during the run is a collision: nothing is renamed, that file
// is untouched, the .partial is kept (holding complete = 1), a second close raises the same, and
// J11: dropping the Simulation afterwards leaves the refused .partial byte-identical.
TEST_F(RunLogPublication, ACollisionAtLogPathRefusesAndTheDestructorLeavesThePartialAlone) {
  std::vector<char> refused;
  {
    Simulation sim = build(with_log(fast_slow_rate(), path("run.h5")));
    ASSERT_FALSE(sim.run(1200));
    write_text(path("run.h5"), "someone else's file");
    const std::string first = log_error_text(sim);
    EXPECT_NE(first.find("the publication failed"), std::string::npos) << first;
    EXPECT_NE(first.find("EEXIST"), std::string::npos) << first;
    EXPECT_EQ(log_error_text(sim), first) << "idempotent, no retry";
    refused = read_bytes(partial(path("run.h5")));
    ASSERT_FALSE(refused.empty());
    EXPECT_EQ(File::open_read_only(partial(path("run.h5"))).attribute_int64("complete"), 1);
  }
  EXPECT_EQ(read_text(path("run.h5")), "someone else's file");
  EXPECT_TRUE(read_bytes(partial(path("run.h5"))) == refused);
}

// R16b: a filesystem or kernel without RENAME_NOREPLACE (simulated) refuses to publish; there is
// never a non-atomic fallback.
TEST_F(RunLogPublication, AnUnsupportedNoReplaceRenamePublishesNothing) {
  Simulation sim = build(with_log(fast_slow_rate(), path("run.h5")));
  ASSERT_FALSE(sim.run(100));
  sim.inject_log_fault_for_testing(RunLog::Fault::kRenameUnsupported);
  const std::string first = log_error_text(sim);
  EXPECT_NE(first.find("not supported"), std::string::npos) << first;
  EXPECT_EQ(log_error_text(sim), first);
  EXPECT_FALSE(fs::exists(path("run.h5")));
  EXPECT_TRUE(fs::exists(partial(path("run.h5"))));
}

// Item 1's identity check, case 1: the .partial is moved aside and a second run creates the same
// name. The first run's close refuses, touches neither file, and cannot publish the second run's
// open file; the second run then publishes normally.
TEST_F(RunLogPublication, APartialMovedAsideAndRecreatedByASecondRunIsNotPublished) {
  const fs::path target = path("run.h5");
  Simulation first = build(with_log(fast_slow_rate(), target));
  ASSERT_FALSE(first.run(100));
  fs::rename(partial(target), path("moved.h5"));
  Simulation second = build(with_log(fast_slow_rate(), target));
  ASSERT_FALSE(second.run(50));
  const std::vector<char> second_bytes = read_bytes(partial(target));
  const std::string refusal = log_error_text(first);
  EXPECT_NE(refusal.find("no longer this run's file"), std::string::npos) << refusal;
  EXPECT_NE(refusal.find("is another file"), std::string::npos) << refusal;
  EXPECT_FALSE(fs::exists(target));
  EXPECT_TRUE(read_bytes(partial(target)) == second_bytes);
  EXPECT_TRUE(fs::exists(path("moved.h5")));
  const LogDiscardResult discarded = first.discard_log();
  EXPECT_EQ(discarded.outcome, LogDiscardOutcome::kMismatch) << discarded.detail;
  EXPECT_TRUE(read_bytes(partial(target)) == second_bytes);
  second.close_log();
  EXPECT_EQ(File::open_read_only(target).attribute_int64("complete"), 1);
  EXPECT_TRUE(fs::exists(path("moved.h5")));
}

// Case 2: moved away with nothing in its place.
TEST_F(RunLogPublication, APartialMovedAwayWithNothingInItsPlaceIsNotPublished) {
  const fs::path target = path("run.h5");
  Simulation sim = build(with_log(fast_slow_rate(), target));
  ASSERT_FALSE(sim.run(100));
  fs::rename(partial(target), path("away.h5"));
  const std::string refusal = log_error_text(sim);
  EXPECT_NE(refusal.find("no longer this run's file (nothing is at"), std::string::npos) << refusal;
  const std::vector<char> away = read_bytes(path("away.h5"));
  EXPECT_FALSE(fs::exists(target));
  EXPECT_FALSE(fs::exists(partial(target)));
  EXPECT_EQ(sim.discard_log().outcome, LogDiscardOutcome::kMissing);
  EXPECT_TRUE(read_bytes(path("away.h5")) == away);
}

// Case 3: replaced by a symlink to the moved original. fstatat does not follow it, so the name is
// not this run's file even though the link resolves to it.
TEST_F(RunLogPublication, APartialReplacedByASymlinkToTheOriginalIsNotPublished) {
  const fs::path target = path("run.h5");
  Simulation sim = build(with_log(fast_slow_rate(), target));
  ASSERT_FALSE(sim.run(100));
  fs::rename(partial(target), path("original.h5"));
  fs::create_symlink(path("original.h5"), partial(target));
  const std::string refusal = log_error_text(sim);
  EXPECT_NE(refusal.find("is a symlink"), std::string::npos) << refusal;
  const std::vector<char> original = read_bytes(path("original.h5"));
  EXPECT_FALSE(fs::exists(target));
  EXPECT_EQ(sim.discard_log().outcome, LogDiscardOutcome::kMismatch);
  EXPECT_TRUE(fs::is_symlink(partial(target)));
  EXPECT_EQ(fs::read_symlink(partial(target)), path("original.h5"));
  EXPECT_TRUE(read_bytes(path("original.h5")) == original);
}

// --- Item 8: discard_log() and the init-failure delete --------------------------------------

using RunLogDiscard = RunLogFinalizationTest;

TEST_F(RunLogDiscard, AnOpenLogIsClosedWithoutAMarkerAndItsPartialDeleted) {
  const fs::path target = path("run.h5");
  Simulation sim = build(with_log(fast_slow_rate(), target));
  ASSERT_FALSE(sim.run(3000));
  const LogDiscardResult first = sim.discard_log();
  EXPECT_EQ(first.outcome, LogDiscardOutcome::kDeleted) << first.detail;
  EXPECT_TRUE(first.deleted());
  EXPECT_EQ(first.path, partial(target).string());
  EXPECT_FALSE(fs::exists(partial(target)));
  EXPECT_FALSE(fs::exists(target));
  const LogDiscardResult second = sim.discard_log();
  EXPECT_EQ(second.outcome, first.outcome);
  EXPECT_EQ(second.path, first.path);
  const std::string closed = log_error_text(sim);
  EXPECT_NE(closed.find("discarded"), std::string::npos) << closed;
  EXPECT_EQ(sim.step().check, HealthCheck::kLog);
}

// R18b's collision case in the core: after the refusal, discard_log() removes the owned .partial
// and leaves the foreign file at log.path untouched.
TEST_F(RunLogDiscard, AfterACollisionItRemovesTheOwnedPartialAndLeavesTheForeignFile) {
  const fs::path target = path("run.h5");
  Simulation sim = build(with_log(fast_slow_rate(), target));
  ASSERT_FALSE(sim.run(100));
  write_text(target, "foreign");
  EXPECT_FALSE(log_error_text(sim).empty());
  EXPECT_EQ(sim.discard_log().outcome, LogDiscardOutcome::kDeleted);
  EXPECT_FALSE(fs::exists(partial(target)));
  EXPECT_EQ(read_text(target), "foreign");
}

TEST_F(RunLogDiscard, AfterThePublicationItDeletesLogPath) {
  const fs::path target = path("run.h5");
  Simulation sim = build(with_log(fast_slow_rate(), target));
  ASSERT_FALSE(sim.run(100));
  sim.close_log();
  const LogDiscardResult result = sim.discard_log();
  EXPECT_EQ(result.outcome, LogDiscardOutcome::kDeleted) << result.detail;
  EXPECT_EQ(result.path, target.string());
  EXPECT_FALSE(fs::exists(target));
}

TEST_F(RunLogDiscard, APublishedLogReplacedByAnotherFileIsNotDeleted) {
  const fs::path target = path("run.h5");
  Simulation sim = build(with_log(fast_slow_rate(), target));
  ASSERT_FALSE(sim.run(100));
  sim.close_log();
  fs::rename(target, path("kept.h5"));
  write_text(target, "a replacement");
  const LogDiscardResult result = sim.discard_log();
  EXPECT_EQ(result.outcome, LogDiscardOutcome::kMismatch) << result.detail;
  EXPECT_EQ(read_text(target), "a replacement");
  EXPECT_TRUE(fs::exists(path("kept.h5")));
}

TEST_F(RunLogDiscard, WithoutALogItReportsNoLog) {
  Simulation sim = build(fast_slow_rate());
  const LogDiscardResult result = sim.discard_log();
  EXPECT_EQ(result.outcome, LogDiscardOutcome::kNoLog);
  EXPECT_TRUE(result.path.empty());
}

// Item 8's init-failure delete, through RunLog directly (the init faults cannot be injected after
// construction): a failed reopen (checked against HDF5's own descriptor) and a failed provenance
// write each leave neither name.
TEST_F(RunLogDiscard, AFailedReopenOrProvenanceWriteLeavesNeitherName) {
  std::vector<double> sources(2, 0.0);
  astra::SignalRegistry registry;
  registry.add("every", sources.data(), "m");
  registry.add("tenth", &sources[1], "s", 10);
  registry.freeze();
  for (const RunLog::Fault fault : {RunLog::Fault::kReopen, RunLog::Fault::kProvenance}) {
    SCOPED_TRACE(static_cast<int>(fault));
    const fs::path target = path("init.h5");
    astra::RunLogSettings settings{target, 8, "init", {}};
    settings.test_hooks.init_fault = fault;
    try {
      RunLog log(settings, registry, {}, astra::RunLogProvenance{"{}\n", 0.001, {}});
      ADD_FAILURE() << "expected a ConfigError";
    } catch (const astra::ConfigError& error) {
      EXPECT_EQ(error.config_path(), "log.path");
      EXPECT_EQ(std::string{error.what()}.find("was left"), std::string::npos) << error.what();
    }
    EXPECT_FALSE(fs::exists(target));
    EXPECT_FALSE(fs::exists(partial(target)));
  }
}

// --- R18: byte identity across every drain pattern ----------------------------------------------

using RunLogDrainIdentity = RunLogFinalizationTest;

enum class Drain : std::uint8_t { kRun, kStepFlushEveryFrame, kSmallRing, kIrregular };

// Runs `frames` frames of `base` under `drain` and closes the log. Returns the halt that ended it.
HaltReason drive(const std::string& base, const fs::path& target, std::uint64_t frames,
                 Drain drain) {
  std::string extra;
  if (drain == Drain::kSmallRing) {
    extra = "  buffer_frames: 3\n";
  } else if (drain == Drain::kIrregular) {
    extra = "  buffer_frames: 7\n";
  }
  Simulation sim = build(with_log(base, target, extra));
  HaltReason halt;
  if (drain == Drain::kRun || drain == Drain::kSmallRing) {
    halt = sim.run(frames);
  } else {
    for (std::uint64_t k = 0; k < frames && !halt; ++k) {
      halt = sim.step();
      if (drain == Drain::kStepFlushEveryFrame || k % 5 == 3) {
        const HaltReason drained = sim.flush_log();
        EXPECT_FALSE(drained) << "frame " << k;
      }
    }
  }
  sim.close_log();
  return halt;
}

void expect_identical_across_drains(const std::string& base, const fs::path& dir,
                                    std::uint64_t frames, bool halts) {
  const std::vector<std::pair<std::string, Drain>> patterns = {
      {"run_a.h5", Drain::kRun},
      {"run_b.h5", Drain::kRun},
      {"step_flush.h5", Drain::kStepFlushEveryFrame},
      {"small_ring.h5", Drain::kSmallRing},
      {"irregular.h5", Drain::kIrregular},
  };
  std::vector<char> reference;
  for (const auto& [name, pattern] : patterns) {
    SCOPED_TRACE(name);
    const HaltReason halt = drive(base, dir / name, frames, pattern);
    EXPECT_EQ(static_cast<bool>(halt), halts);
    const std::vector<char> bytes = read_bytes(dir / name);
    ASSERT_FALSE(bytes.empty());
    if (reference.empty()) {
      reference = bytes;
    } else {
      EXPECT_TRUE(bytes == reference) << name << " differs from run_a.h5";
    }
  }
}

TEST_F(RunLogDrainIdentity, CruiseGivesIdenticalBytesUnderEveryDrainPattern) {
  expect_identical_across_drains(cruise_text(), path(""), 25000, false);
  // Not vacuous: a different Dryden seed changes the bytes.
  std::string text = cruise_text();
  text.replace(text.find("seed: 42"), std::string_view{"seed: 42"}.size(), "seed: 43");
  static_cast<void>(drive(text, path("seed43.h5"), 25000, Drain::kRun));
  EXPECT_FALSE(read_bytes(path("seed43.h5")) == read_bytes(path("run_a.h5")));
}

TEST_F(RunLogDrainIdentity, TheDivisorTenFixtureGivesIdenticalBytesUnderEveryDrainPattern) {
  expect_identical_across_drains(cruise_slow_rate(), path(""), 25000, false);
  const File file = File::open_read_only(path("run_a.h5"));
  EXPECT_EQ(file.open_group("rate_10").attribute_uint64("rows_committed"), 2500U)
      << "the /rate_10 group crosses two 1024-row blocks and a tail";
}

TEST_F(RunLogDrainIdentity, AHaltingRunGivesIdenticalBytesUnderEveryDrainPattern) {
  const std::string base = with_model(
      cruise_slow_rate(), "    - { plugin: tests/plugins/nan_wrench, nan_frame: 12345 }\n");
  expect_identical_across_drains(base, path(""), 25000, true);
  const File file = File::open_read_only(path("run_a.h5"));
  EXPECT_EQ(file.open_group("summary").attribute_uint64("frames_run"), 12345U);
}

// --- Confirmations 1 and 2 ----------------------------------------------------------------------

using RunLogConfirmation = RunLogFinalizationTest;

// Confirmation 1 (ADR-0027 item 5), from the writer's own library: every chunked dataset in a
// real log (both rate groups' frame and values, /summary's min and max, /final_state's vehicle)
// uses the version-1 B-tree chunk index (H5Dget_chunk_index_type). The evidence independent of this
// library, a byte scan against the chunked datasets h5py lists, is tests/python/test_log.py's
// test_log_chunk_index_byte_scan (Joe's J12, as corrected at the scan's first run).
TEST_F(RunLogConfirmation, EveryChunkedDatasetUsesTheVersionOneBTreeChunkIndex) {
  {
    Simulation sim = build(with_log(cruise_slow_rate(), path("run.h5")));
    ASSERT_FALSE(sim.run(12000));
    sim.close_log();
  }
  const File file = File::open_read_only(path("run.h5"));
  const std::vector<std::pair<std::string, std::string>> datasets = {
      {"rate_1", "frame"}, {"rate_1", "values"}, {"rate_10", "frame"},  {"rate_10", "values"},
      {"summary", "min"},  {"summary", "max"},   {"final_state", "v0"},
  };
  for (const auto& [group, dataset] : datasets) {
    EXPECT_EQ(file.open_group(group).open_dataset(dataset).chunk_index(),
              Dataset::ChunkIndex::kBtreeV1)
        << group << "/" << dataset;
  }
}

void expect_frames(const File& file, const std::string& group, std::uint64_t divisor,
                   std::uint64_t frames_run) {
  const std::vector<std::uint64_t> frames =
      file.open_group(group).open_dataset("frame").read_uint64();
  ASSERT_EQ(frames.size(), (frames_run + divisor - 1) / divisor) << group;
  for (std::size_t i = 0; i < frames.size(); ++i) {
    ASSERT_EQ(frames[i], i * divisor) << group << " row " << i;
  }
  EXPECT_EQ(file.open_group(group).attribute_uint64("rows_committed"), frames.size());
}

// Confirmation 2 (ADR-0027 item 6): a halted run stepped again keeps its frame column exactly 0,
// d, 2d, ... below frames_run, because a halted frame is never committed and its row is retaken.
TEST_F(RunLogConfirmation, AHaltedRunSteppedAgainKeepsAContiguousFrameColumn) {
  {
    const std::string base = with_model(
        read_text(fs::path{ASTRA_TEST_CONFIG_DIR} / "health_nan_wrench.yaml"), kSlowRateEntry);
    Simulation sim = build(with_log(base, path("repeated.h5")));
    const HaltReason halt = sim.run();
    ASSERT_EQ(halt.check, HealthCheck::kNonFiniteWrench);
    for (int again = 0; again < 5; ++again) {
      const HaltReason repeat = sim.step();
      EXPECT_EQ(repeat.check, HealthCheck::kNonFiniteWrench);
      EXPECT_EQ(repeat.frame, halt.frame);
    }
    sim.close_log();
    const File file = File::open_read_only(path("repeated.h5"));
    const std::uint64_t frames_run = file.open_group("summary").attribute_uint64("frames_run");
    EXPECT_EQ(frames_run, halt.frame);
    expect_frames(file, "rate_1", 1, frames_run);
    expect_frames(file, "rate_10", 10, frames_run);
  }
  {
    Simulation sim = build(with_log(cruise_slow_rate(), path("recovered.h5")));
    ASSERT_FALSE(sim.run(100));
    EXPECT_THROW(sim.write_input("controls.elevator", std::nan("")), astra::Error);
    EXPECT_EQ(sim.step().check, HealthCheck::kNonFiniteInput);
    sim.write_input("controls.elevator", 0.0);
    ASSERT_FALSE(sim.run(50));
    sim.close_log();
    const File file = File::open_read_only(path("recovered.h5"));
    EXPECT_EQ(file.open_group("summary").attribute_uint64("frames_run"), 150U);
    expect_frames(file, "rate_1", 1, 150);
    expect_frames(file, "rate_10", 10, 150);
  }
}

// --- R16: deterministic kills -------------------------------------------------------------------

using RunLogKill = RunLogFinalizationTest;

// Runs `body` in a forked child; returns its wait status and what it wrote to the pipe.
struct ChildResult {
  int status = 0;
  std::string output;
};

ChildResult in_child(const std::function<std::string()>& body) {
  std::array<int, 2> pipe_fds{-1, -1};
  EXPECT_EQ(::pipe(pipe_fds.data()), 0);
  std::cout.flush();
  std::cerr.flush();
  const pid_t pid = ::fork();
  if (pid == 0) {
    ::close(pipe_fds[0]);
    std::string text;
    int code = 0;
    try {
      text = body();
    } catch (const std::exception& error) {
      text = std::string{"exception: "} + error.what();
      code = 3;
    } catch (...) {
      text = "unknown exception";
      code = 3;
    }
    static_cast<void>(::write(pipe_fds[1], text.data(), text.size()));
    ::close(pipe_fds[1]);
    std::_Exit(code);  // no atexit, no destructors: the child's only effects are its files
  }
  ::close(pipe_fds[1]);
  ChildResult result;
  std::array<char, 4096> buffer{};
  ssize_t got = 0;
  while ((got = ::read(pipe_fds[0], buffer.data(), buffer.size())) > 0) {
    result.output.append(buffer.data(), static_cast<std::size_t>(got));
  }
  ::close(pipe_fds[0]);
  ::waitpid(pid, &result.status, 0);
  return result;
}

bool killed(const ChildResult& child) {
  return WIFSIGNALED(child.status) && WTERMSIG(child.status) == SIGKILL;
}

// One rate group of the clean reference run.
struct GroupRows {
  std::uint32_t divisor = 1;
  std::size_t columns = 0;
  std::vector<std::uint64_t> frames;
  std::vector<double> values;
};

std::vector<GroupRows> read_reference(const fs::path& file_path) {
  const File file = File::open_read_only(file_path);
  std::vector<GroupRows> groups;
  for (const std::uint32_t divisor : {1U, 10U}) {
    const astra::hdf5::Group group = file.open_group("rate_" + std::to_string(divisor));
    const Dataset values = group.open_dataset("values");
    groups.push_back({divisor, values.columns(), group.open_dataset("frame").read_uint64(),
                      values.read_float64()});
  }
  return groups;
}

// The oracle, run in a child (J4) so a reader crash is a named failure, not the test binary's.
// Prints "refused <why>" (the file or a group does not open, rows_committed is missing or beyond
// an extent, or the certified frame column is not 0, d, 2d, ...: what astra.log.load refuses),
// "wrong <why>" (certified rows that pass those checks differ from the reference: never allowed),
// or "ok <certified rate_1> <certified rate_10> <complete> <finalized_by>".
std::string judge(const fs::path& file_path, const std::vector<GroupRows>& reference) {
  std::vector<std::uint64_t> certified;
  bool complete = false;
  bool destructor = false;
  try {
    const File file = File::open_read_only(file_path);
    complete = file.has_attribute("complete");
    destructor = file.has_attribute("finalized_by");
    for (const GroupRows& ref : reference) {
      const std::string name = "rate_" + std::to_string(ref.divisor);
      const astra::hdf5::Group group = file.open_group(name);
      const std::uint64_t committed = group.attribute_uint64("rows_committed");
      const Dataset frame = group.open_dataset("frame");
      const Dataset values = group.open_dataset("values");
      if (committed > frame.rows() || committed > values.rows()) {
        return "refused " + name + " rows_committed beyond an extent";
      }
      const std::vector<std::uint64_t> frames = frame.read_uint64();
      const std::vector<double> rows = values.read_float64();
      for (std::uint64_t i = 0; i < committed; ++i) {
        if (frames[i] != i * ref.divisor) {
          return "refused " + name + " frame column";
        }
      }
      if (committed > ref.frames.size() || values.columns() != ref.columns) {
        return "wrong " + name + " more certified rows or other columns than the clean run";
      }
      for (std::size_t v = 0; v < committed * ref.columns; ++v) {
        if (std::bit_cast<std::uint64_t>(rows[v]) != std::bit_cast<std::uint64_t>(ref.values[v])) {
          return "wrong " + name + " value " + std::to_string(v) + " differs from the clean run";
        }
      }
      certified.push_back(committed);
    }
  } catch (const astra::Hdf5Error& error) {
    return std::string{"refused "} + error.what();
  }
  return "ok " + std::to_string(certified[0]) + " " + std::to_string(certified[1]) + " " +
         (complete ? "1" : "0") + " " + (destructor ? "1" : "0");
}

// Keeps a file the oracle crashed on, for inspection.
std::string keep_for_inspection(const fs::path& file, const std::string& label) {
  const fs::path kept = fs::temp_directory_path() /
                        ("astra_kill_kept_" + std::to_string(::getpid()) + "_" + label + ".h5");
  std::error_code ignored;
  fs::copy_file(file, kept, fs::copy_options::overwrite_existing, ignored);
  return kept.string();
}

struct KillCase {
  std::string label;
  KillPoint point;
  std::uint64_t occurrence;
  std::uint32_t divisor;
  bool close;      // run to the end, then close_log() (else: run, and the kill lands mid-run)
  bool drop;       // run to the end, then drop the Simulation (the destructor's close)
  bool must_open;  // outside every stated-limit window: the file must open with its prefix
  std::uint64_t min_rate1;  // certified /rate_1 rows the file must hold when it opens
  bool published;           // the kill lands after the publication point
};

TEST_F(RunLogKill, EveryKillPointLeavesACorrectPrefixATypedRefusalOrThePublishedLog) {
  constexpr std::uint64_t kFrames = 12000;
  const std::string base = fast_slow_rate();
  {
    Simulation sim = build(with_log(base, path("reference.h5")));
    ASSERT_FALSE(sim.run(kFrames));
    sim.close_log();
  }
  const std::vector<GroupRows> reference = read_reference(path("reference.h5"));
  ASSERT_EQ(reference[0].frames.size(), kFrames);
  ASSERT_EQ(reference[1].frames.size(), kFrames / 10);

  const std::vector<KillCase> cases = {
      {"block1_after_values", KillPoint::kBlockAfterValues, 2, 1, false, false, false, 0, false},
      {"block1_after_flush", KillPoint::kBlockAfterFlush, 2, 1, false, false, false, 0, false},
      {"block1_committed_1st", KillPoint::kBlockAfterCommitted, 1, 1, false, false, true, 0, false},
      {"block1_committed_3rd", KillPoint::kBlockAfterCommitted, 3, 1, false, false, true, 2048,
       false},
      {"block10_after_values", KillPoint::kBlockAfterValues, 1, 10, false, false, false, 0, false},
      {"block10_committed_1st", KillPoint::kBlockAfterCommitted, 1, 10, false, false, true, 9216,
       false},
      {"close_after_tails", KillPoint::kCloseAfterTails, 1, 0, true, false, false, 0, false},
      {"close_after_final_objects", KillPoint::kCloseAfterFinalObjects, 1, 0, true, false, false, 0,
       false},
      {"close_after_marker", KillPoint::kCloseAfterMarker, 1, 0, true, false, false, 0, false},
      {"close_before_rename", KillPoint::kCloseBeforeRename, 1, 0, true, false, true, kFrames,
       false},
      {"close_after_rename", KillPoint::kCloseAfterRename, 1, 0, true, false, true, kFrames, true},
      {"destructor_after_marker", KillPoint::kCloseAfterMarker, 1, 0, false, true, false, 0, false},
  };
  for (const KillCase& c : cases) {
    SCOPED_TRACE(c.label);
    const fs::path target = path(c.label + ".h5");
    const ChildResult child = in_child([&] {
      Simulation sim = build(with_log(base, target));
      sim.inject_log_kill_for_testing(c.point, c.occurrence, c.divisor);
      static_cast<void>(sim.run(kFrames));
      if (c.close) {
        sim.close_log();
      }
      // With c.drop the kill lands in the destructor, after this string is built.
      return std::string{"the kill point was never reached"};
    });
    ASSERT_TRUE(killed(child)) << "status " << child.status << ": " << child.output;
    const fs::path left = c.published ? target : partial(target);
    EXPECT_EQ(fs::exists(target), c.published) << "log.path exists only after the publication";
    EXPECT_EQ(fs::exists(partial(target)), !c.published);
    const ChildResult read = in_child([&] { return judge(left, reference); });
    if (!WIFEXITED(read.status) || WEXITSTATUS(read.status) != 0) {
      ADD_FAILURE() << "the reader crashed or failed on " << left << " (status " << read.status
                    << ", kept at " << keep_for_inspection(left, c.label) << "): " << read.output;
      continue;
    }
    std::cout << "R16 kill " << c.label << ": " << read.output << "\n";
    EXPECT_FALSE(read.output.starts_with("wrong")) << read.output;
    if (c.must_open) {
      ASSERT_TRUE(read.output.starts_with("ok ")) << read.output;
      std::istringstream fields(read.output.substr(3));
      std::uint64_t rate1 = 0;
      std::uint64_t rate10 = 0;
      int complete = 0;
      fields >> rate1 >> rate10 >> complete;
      EXPECT_GE(rate1, c.min_rate1);
      if (c.published || c.point == KillPoint::kCloseBeforeRename) {
        EXPECT_EQ(rate1, kFrames);
        EXPECT_EQ(rate10, kFrames / 10);
        EXPECT_EQ(complete, 1);
      }
    }
  }
}

// The init-time kill points, through RunLog directly (they cannot be injected after
// construction). After the provenance flush the file must open with zero certified rows; right
// after the create (before that flush) it may not open, and if it does it holds no certified row.
TEST_F(RunLogKill, AKillDuringInitLeavesAFileWithNoCertifiedRowOrOneThatDoesNotOpen) {
  std::vector<double> sources(2, 0.0);
  astra::SignalRegistry registry;
  registry.add("every", sources.data(), "m");
  registry.add("tenth", &sources[1], "s", 10);
  registry.freeze();
  for (const KillPoint point : {KillPoint::kAfterCreate, KillPoint::kAfterProvenanceFlush}) {
    const std::string label =
        point == KillPoint::kAfterCreate ? "after_create" : "after_provenance";
    SCOPED_TRACE(label);
    const fs::path target = path(label + ".h5");
    const ChildResult child = in_child([&] {
      astra::RunLogSettings settings{target, 8, "init", {}};
      settings.test_hooks.kill = point;
      const RunLog log(settings, registry, {}, astra::RunLogProvenance{"{}\n", 0.001, {}});
      return std::string{"the kill point was never reached"};
    });
    ASSERT_TRUE(killed(child)) << child.output;
    EXPECT_FALSE(fs::exists(target));
    ASSERT_TRUE(fs::exists(partial(target)));
    const ChildResult read = in_child([&] {
      try {
        const File file = File::open_read_only(partial(target));
        std::string text =
            "ok version " + std::to_string(file.attribute_uint64("log_format_version"));
        for (const std::string group : {"rate_1", "rate_10"}) {
          text += " " + std::to_string(file.open_group(group).attribute_uint64("rows_committed")) +
                  "/" + std::to_string(file.open_group(group).open_dataset("frame").rows());
        }
        return text;
      } catch (const astra::Hdf5Error& error) {
        return std::string{"refused "} + error.what();
      }
    });
    ASSERT_TRUE(WIFEXITED(read.status) && WEXITSTATUS(read.status) == 0)
        << "the reader crashed (kept at " << keep_for_inspection(partial(target), label)
        << "): " << read.output;
    std::cout << "R16 kill " << label << " (" << fs::file_size(partial(target))
              << " bytes): " << read.output << "\n";
    if (point == KillPoint::kAfterProvenanceFlush) {
      EXPECT_EQ(read.output, "ok version 2 0/0 0/0");
    } else {
      EXPECT_TRUE(read.output.starts_with("refused ") || read.output == "ok version 2 0/0 0/0")
          << read.output;
    }
  }
}

// Between blocks, outside any window (a kill at a frame boundary after a drain): the file opens and
// holds at least every block before the last one written.
TEST_F(RunLogKill, AKillBetweenBlocksLeavesTheCertifiedPrefix) {
  const std::string base = fast_slow_rate();
  {
    Simulation sim = build(with_log(base, path("reference.h5")));
    ASSERT_FALSE(sim.run(12000));
    sim.close_log();
  }
  const std::vector<GroupRows> reference = read_reference(path("reference.h5"));
  const fs::path target = path("between.h5");
  const ChildResult child = in_child([&] {
    Simulation sim = build(with_log(base, target));
    static_cast<void>(sim.run(5000));
    static_cast<void>(sim.flush_log());
    static_cast<void>(std::raise(SIGKILL));
    return std::string{"not killed"};
  });
  ASSERT_TRUE(killed(child));
  const ChildResult read = in_child([&] { return judge(partial(target), reference); });
  ASSERT_TRUE(WIFEXITED(read.status) && WEXITSTATUS(read.status) == 0) << read.output;
  std::cout << "R16 kill between_blocks: " << read.output << "\n";
  ASSERT_TRUE(read.output.starts_with("ok ")) << read.output;
  std::istringstream fields(read.output.substr(3));
  std::uint64_t rate1 = 0;
  fields >> rate1;
  EXPECT_GE(rate1, 3072U) << "four blocks were written; at most the last one's count may lag";
}

}  // namespace
