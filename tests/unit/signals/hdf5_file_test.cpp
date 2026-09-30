// The HDF5 RAII wrapper (core/signals/hdf5_file.hpp, ADR-0005, ADR-0025): files never
// overwritten, groups, appendable series and tables, attributes of every stored type, typed
// errors naming the file and object, explicit close, the fixed library-version bounds, and
// ADR-0025's Q7 criterion itself: two identical write sequences to different paths give
// byte-identical files. That criterion holds with libhdf5 1.10.10, so no test here uses Q7's
// dataset-and-attribute-equality fallback. Every file lives in a per-test temporary directory.
#include "astra/signals/hdf5_file.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "astra/errors.hpp"

namespace {

namespace fs = std::filesystem;
using astra::Hdf5Error;
using astra::hdf5::Dataset;
using astra::hdf5::ElementType;
using astra::hdf5::File;
using astra::hdf5::Group;

// A fresh directory per test (pid and test name), removed afterwards.
class Hdf5Test : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir_ = fs::temp_directory_path() / ("astra_hdf5_" + std::to_string(::getpid()) + "_" +
                                        info->test_suite_name() + "_" + info->name());
    fs::remove_all(dir_);
    fs::create_directories(dir_);
  }
  void TearDown() override {
    std::error_code ignored;
    fs::remove_all(dir_, ignored);
  }

  [[nodiscard]] fs::path path(const std::string& name) const { return dir_ / name; }

 private:
  fs::path dir_;
};

std::vector<char> read_bytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// --- Hdf5File ---------------------------------------------------------------------------------

using Hdf5File = Hdf5Test;

TEST_F(Hdf5File, CreateRefusesAnExistingFileAndLeavesItUntouched) {
  const fs::path existing = path("existing.h5");
  {
    std::ofstream out(existing, std::ios::binary);
    out << "not overwritten";
  }
  try {
    static_cast<void>(File::create(existing));
    FAIL() << "create() of an existing file did not throw";
  } catch (const Hdf5Error& error) {
    EXPECT_EQ(error.file_path(), existing.string());
    EXPECT_NE(std::string{error.what()}.find(existing.string()), std::string::npos) << error.what();
  }
  const std::vector<char> bytes = read_bytes(existing);
  EXPECT_EQ(std::string(bytes.begin(), bytes.end()), "not overwritten");
}

TEST_F(Hdf5File, CreateInAMissingDirectoryThrowsNamingThePath) {
  const fs::path missing = path("no_such_dir") / "run.h5";
  EXPECT_THROW(static_cast<void>(File::create(missing)), Hdf5Error);
  EXPECT_FALSE(fs::exists(missing));
}

TEST_F(Hdf5File, OpenReadOnlyRefusesAMissingOrNonHdf5File) {
  EXPECT_THROW(static_cast<void>(File::open_read_only(path("missing.h5"))), Hdf5Error);
  const fs::path text = path("text.h5");
  {
    std::ofstream out(text);
    out << "plain text, not HDF5\n";
  }
  EXPECT_THROW(static_cast<void>(File::open_read_only(text)), Hdf5Error);
}

TEST_F(Hdf5File, LibraryVersionLowBoundGivesSuperblockVersionTwo) {
  // Default bounds (EARLIEST) would give version 0; a 1.10 low bound would give version 3.
  File file = File::create(path("run.h5"));
  EXPECT_EQ(file.superblock_version(), 2U);
  file.close();
  const File reopened = File::open_read_only(path("run.h5"));
  EXPECT_EQ(reopened.superblock_version(), 2U);
}

TEST_F(Hdf5File, CloseRefusesWhileAnObjectIsOpenThenSucceedsAndIsIdempotent) {
  File file = File::create(path("run.h5"));
  Group group = file.create_group("rate_1");
  EXPECT_THROW(file.close(), Hdf5Error);
  EXPECT_TRUE(file.is_open());
  group.close();
  file.close();
  EXPECT_FALSE(file.is_open());
  EXPECT_NO_THROW(file.close());
  EXPECT_THROW(static_cast<void>(file.create_group("late")), Hdf5Error);
  EXPECT_THROW(static_cast<void>(group.has_attribute("x")), Hdf5Error);
}

TEST_F(Hdf5File, TheDestructorClosesAFileWhoseObjectsOutliveIt) {
  {
    Dataset frames = [&] {
      File file = File::create(path("run.h5"));
      return file.create_series("frame", ElementType::kUint64, 8);
    }();
    const std::vector<std::uint64_t> values{1, 2, 3};
    frames.append(values);
  }
  const File file = File::open_read_only(path("run.h5"));
  EXPECT_EQ(file.open_dataset("frame").read_uint64(), (std::vector<std::uint64_t>{1, 2, 3}));
}

// The reason for the 1.8 low bound: a process that dies without closing the file (no destructor,
// no atexit) leaves a file that still opens, and every flushed row is readable. With a 1.10 low
// bound (superblock version 3) the reopen fails until h5clear is run.
TEST_F(Hdf5File, AFlushedFileSurvivesAProcessThatNeverClosesIt) {
  const fs::path crashed = path("crashed.h5");
  const auto write_then_die = [&crashed] {
    File file = File::create(crashed);
    Dataset frames = file.create_series("frame", ElementType::kUint64, 4);
    const std::vector<std::uint64_t> values{7, 8, 9, 10, 11};
    frames.append(values);
    file.flush();
    std::_Exit(0);
  };
  EXPECT_EXIT(write_then_die(), ::testing::ExitedWithCode(0), "");
  const File file = File::open_read_only(crashed);
  EXPECT_EQ(file.open_dataset("frame").read_uint64(),
            (std::vector<std::uint64_t>{7, 8, 9, 10, 11}));
}

// M6 PR 2 (ADR-0027 item 1): create() through another name reports the display name, and the
// sec2 descriptor HDF5 holds is the created file itself.
TEST_F(Hdf5File, CreateThroughAnotherNameReportsTheDisplayNameAndExposesItsDescriptor) {
  const int directory = ::open(path("").c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC);
  ASSERT_GE(directory, 0);
  const fs::path via_proc =
      fs::path{"/proc/self/fd"} / std::to_string(directory) / "run.h5.partial";
  {
    File file = File::create(via_proc, "configured/run.h5");
    EXPECT_EQ(file.file_path(), "configured/run.h5");
    struct stat by_descriptor {};
    struct stat by_name {};
    ASSERT_EQ(::fstat(file.vfd_descriptor(), &by_descriptor), 0);
    ASSERT_EQ(::stat(path("run.h5.partial").c_str(), &by_name), 0);
    EXPECT_EQ(by_descriptor.st_dev, by_name.st_dev);
    EXPECT_EQ(by_descriptor.st_ino, by_name.st_ino);
    file.close();
  }
  try {
    static_cast<void>(File::create(via_proc, "configured/run.h5"));
    FAIL() << "create() of an existing file did not throw";
  } catch (const Hdf5Error& error) {
    EXPECT_EQ(error.file_path(), "configured/run.h5");
    // The message leads with the display name; HDF5's own stack detail after it may quote the
    // name it opened.
    EXPECT_TRUE(std::string{error.what()}.starts_with("hdf5 'configured/run.h5': ")) << error.what();
  }
  ::close(directory);
}

// --- Hdf5Group --------------------------------------------------------------------------------

using Hdf5Group = Hdf5Test;

TEST_F(Hdf5Group, CreateOpenAndNestedPaths) {
  {
    File file = File::create(path("run.h5"));
    Group rate = file.create_group("rate_1");
    EXPECT_EQ(rate.object_path(), "/rate_1");
    const Group inner = rate.create_group("inner");
    EXPECT_EQ(inner.object_path(), "/rate_1/inner");
  }
  const File file = File::open_read_only(path("run.h5"));
  const Group rate = file.open_group("rate_1");
  EXPECT_EQ(rate.open_group("inner").object_path(), "/rate_1/inner");
}

TEST_F(Hdf5Group, ADuplicateOrMissingGroupThrowsNamingTheObject) {
  File file = File::create(path("run.h5"));
  static_cast<void>(file.create_group("rate_1"));
  try {
    static_cast<void>(file.create_group("rate_1"));
    FAIL() << "a duplicate group did not throw";
  } catch (const Hdf5Error& error) {
    EXPECT_EQ(error.object_path(), "/rate_1");
  }
  try {
    static_cast<void>(file.open_group("rate_10"));
    FAIL() << "a missing group did not throw";
  } catch (const Hdf5Error& error) {
    EXPECT_EQ(error.object_path(), "/rate_10");
  }
}

// --- Hdf5Dataset ------------------------------------------------------------------------------

using Hdf5Dataset = Hdf5Test;

TEST_F(Hdf5Dataset, TableAppendsWholeRowsAcrossChunkBoundaries) {
  std::vector<double> expected;
  {
    File file = File::create(path("run.h5"));
    Group rate = file.create_group("rate_1");
    Dataset values = rate.create_table("values", ElementType::kFloat64, 3, 4);
    EXPECT_EQ(values.rank(), 2);
    EXPECT_EQ(values.columns(), 3U);
    EXPECT_EQ(values.rows(), 0U);
    double next = 0.25;
    for (const std::size_t rows : {3U, 5U, 1U, 0U, 8U}) {
      std::vector<double> batch;
      for (std::size_t i = 0; i < rows * 3; ++i) {
        batch.push_back(next);
        next += 1.5;
      }
      values.append(batch);
      expected.insert(expected.end(), batch.begin(), batch.end());
    }
    EXPECT_EQ(values.rows(), 17U);
  }
  const File file = File::open_read_only(path("run.h5"));
  const Dataset values = file.open_group("rate_1").open_dataset("values");
  EXPECT_EQ(values.type(), ElementType::kFloat64);
  EXPECT_EQ(values.rank(), 2);
  EXPECT_EQ(values.columns(), 3U);
  EXPECT_EQ(values.rows(), 17U);
  EXPECT_EQ(values.read_float64(), expected);
}

TEST_F(Hdf5Dataset, SeriesStoresUint64FramesExactly) {
  // Above 2^53, where a float64 round trip would lose the low bits.
  const std::vector<std::uint64_t> frames{0, 1, (std::uint64_t{1} << 53U) + 1U, ~std::uint64_t{0}};
  {
    File file = File::create(path("run.h5"));
    Dataset series = file.create_series("frame", ElementType::kUint64, 2);
    EXPECT_EQ(series.rank(), 1);
    EXPECT_EQ(series.columns(), 1U);
    series.append(frames);
  }
  const File file = File::open_read_only(path("run.h5"));
  const Dataset series = file.open_dataset("frame");
  EXPECT_EQ(series.type(), ElementType::kUint64);
  EXPECT_EQ(series.rank(), 1);
  EXPECT_EQ(series.read_uint64(), frames);
}

TEST_F(Hdf5Dataset, AppendRefusesAPartialRowOrTheWrongTypeAndLeavesTheDatasetUnchanged) {
  File file = File::create(path("run.h5"));
  Dataset values = file.create_table("values", ElementType::kFloat64, 3, 4);
  const std::vector<double> row{1.0, 2.0, 3.0};
  values.append(row);

  const std::vector<double> partial{4.0, 5.0};
  try {
    values.append(partial);
    FAIL() << "a partial row did not throw";
  } catch (const Hdf5Error& error) {
    EXPECT_EQ(error.object_path(), "/values");
  }
  const std::vector<std::uint64_t> wrong_type{1, 2, 3};
  EXPECT_THROW(values.append(wrong_type), Hdf5Error);
  EXPECT_THROW(static_cast<void>(values.read_uint64()), Hdf5Error);

  EXPECT_EQ(values.rows(), 1U);
  EXPECT_EQ(values.read_float64(), row);
}

// A refused extend (the file is read-only) leaves the rows as they were. The rollback after a
// failed write (Codex review F1) has no test: nothing short of an HDF5 fault-injection shim makes
// H5Dwrite fail after H5Dset_extent succeeded.
TEST_F(Hdf5Dataset, AppendToAReadOnlyFileThrowsAndLeavesTheRowsAsTheyWere) {
  {
    File file = File::create(path("run.h5"));
    Dataset values = file.create_table("values", ElementType::kFloat64, 2, 4);
    values.append(std::vector<double>{1.0, 2.0});
  }
  const File file = File::open_read_only(path("run.h5"));
  Dataset values = file.open_dataset("values");
  EXPECT_THROW(values.append(std::vector<double>{3.0, 4.0}), Hdf5Error);
  EXPECT_EQ(values.rows(), 1U);
  EXPECT_EQ(values.read_float64(), (std::vector<double>{1.0, 2.0}));
}

// ADR-0027 item 5: under the 1.8 low bound an appendable chunked dataset is indexed by a
// version-1 B-tree, the index whose node splits the stated limit names.
TEST_F(Hdf5Dataset, AppendableDatasetsUseTheVersionOneBTreeChunkIndex) {
  {
    File file = File::create(path("run.h5"));
    Dataset series = file.create_series("frame", ElementType::kUint64, 4);
    Dataset table = file.create_table("values", ElementType::kFloat64, 3, 4);
    EXPECT_EQ(series.chunk_index(), Dataset::ChunkIndex::kBtreeV1);
    EXPECT_EQ(table.chunk_index(), Dataset::ChunkIndex::kBtreeV1);
    const std::vector<std::uint64_t> frames(40, 1);
    series.append(frames);
  }
  const File file = File::open_read_only(path("run.h5"));
  EXPECT_EQ(file.open_dataset("frame").chunk_index(), Dataset::ChunkIndex::kBtreeV1);
  EXPECT_EQ(file.open_dataset("values").chunk_index(), Dataset::ChunkIndex::kBtreeV1);
}

TEST_F(Hdf5Dataset, CreateRefusesZeroColumnsZeroChunkRowsOrADuplicateName) {
  File file = File::create(path("run.h5"));
  EXPECT_THROW(static_cast<void>(file.create_table("values", ElementType::kFloat64, 0, 4)),
               Hdf5Error);
  EXPECT_THROW(static_cast<void>(file.create_series("frame", ElementType::kUint64, 0)), Hdf5Error);
  static_cast<void>(file.create_series("frame", ElementType::kUint64, 4));
  EXPECT_THROW(static_cast<void>(file.create_series("frame", ElementType::kUint64, 4)), Hdf5Error);
  EXPECT_THROW(static_cast<void>(file.open_dataset("missing")), Hdf5Error);
}

// --- Hdf5Attribute ----------------------------------------------------------------------------

using Hdf5Attribute = Hdf5Test;

TEST_F(Hdf5Attribute, EveryTypeRoundTripsOnTheRootAGroupAndADataset) {
  const std::string config = "sim:\n  dt: 0.001\nvehicle:\n  name: \"n\xC3\xB8tional\"\n";
  const std::vector<std::string> names{"v0.state.x", "v0.state.y", ""};
  const std::vector<std::string> units{"m", "m", "1"};
  {
    File file = File::create(path("run.h5"));
    file.set_attribute("config", config);
    file.set_attribute("dt", 0.001);
    file.set_attribute("core_api_version", std::uint64_t{7});
    file.set_attribute("offset", std::int64_t{-42});
    Group rate = file.create_group("rate_1");
    rate.set_attribute("divisor", std::uint64_t{1});
    Dataset values = rate.create_table("values", ElementType::kFloat64, 3, 4);
    values.set_attribute("names", names);
    values.set_attribute("units", units);
    values.set_attribute("empty", std::vector<std::string>{});
  }
  const File file = File::open_read_only(path("run.h5"));
  EXPECT_EQ(file.attribute_string("config"), config);
  EXPECT_EQ(file.attribute_float64("dt"), 0.001);
  EXPECT_EQ(file.attribute_uint64("core_api_version"), 7U);
  EXPECT_EQ(file.attribute_int64("offset"), -42);
  const Group rate = file.open_group("rate_1");
  EXPECT_EQ(rate.attribute_uint64("divisor"), 1U);
  const Dataset values = rate.open_dataset("values");
  EXPECT_EQ(values.attribute_strings("names"), names);
  EXPECT_EQ(values.attribute_strings("units"), units);
  EXPECT_TRUE(values.attribute_strings("empty").empty());
  EXPECT_TRUE(values.has_attribute("units"));
  EXPECT_FALSE(values.has_attribute("unit"));
}

TEST_F(Hdf5Attribute, SettingAnExistingAttributeReplacesItsValueAndType) {
  {
    File file = File::create(path("run.h5"));
    file.set_attribute("complete", std::uint64_t{0});
    file.set_attribute("complete", std::uint64_t{1});
    file.set_attribute("note", 1.5);
    file.set_attribute("note", "now a string");
  }
  const File file = File::open_read_only(path("run.h5"));
  EXPECT_EQ(file.attribute_uint64("complete"), 1U);
  EXPECT_EQ(file.attribute_string("note"), "now a string");
}

// M6 PR 2 (ADR-0027 item 2): rows_committed is rewritten in place; a missing attribute or one of
// another type is refused, never created.
TEST_F(Hdf5Attribute, OverwriteWritesAnExistingUint64InPlaceAndRefusesAnythingElse) {
  {
    File file = File::create(path("run.h5"));
    Group rate = file.create_group("rate_1");
    rate.set_attribute("rows_committed", std::uint64_t{0});
    rate.set_attribute("dt", 0.001);
    rate.overwrite_attribute("rows_committed", std::uint64_t{1024});
    rate.overwrite_attribute("rows_committed", std::uint64_t{2048});
    EXPECT_THROW(rate.overwrite_attribute("missing", std::uint64_t{1}), Hdf5Error);
    EXPECT_THROW(rate.overwrite_attribute("dt", std::uint64_t{1}), Hdf5Error);
    EXPECT_FALSE(rate.has_attribute("missing"));
    EXPECT_EQ(rate.attribute_float64("dt"), 0.001);
  }
  const File file = File::open_read_only(path("run.h5"));
  EXPECT_EQ(file.open_group("rate_1").attribute_uint64("rows_committed"), 2048U);
}

// Two files whose only difference is how often an attribute was overwritten are byte-identical:
// an in-place write leaves no trace of the earlier values (ADR-0027 item 2 relies on the call
// sequence, not on the attribute's history, for byte identity).
TEST_F(Hdf5Attribute, OverwritingInPlaceLeavesNoTraceOfEarlierValues) {
  for (const auto& [name, writes] : {std::pair{"once.h5", 1}, std::pair{"many.h5", 50}}) {
    File file = File::create(path(name));
    Group rate = file.create_group("rate_1");
    rate.set_attribute("rows_committed", std::uint64_t{0});
    for (int i = 0; i < writes; ++i) {
      rate.overwrite_attribute("rows_committed", static_cast<std::uint64_t>(i + 1));
    }
    rate.overwrite_attribute("rows_committed", std::uint64_t{7});
  }
  EXPECT_TRUE(read_bytes(path("once.h5")) == read_bytes(path("many.h5")));
}

TEST_F(Hdf5Attribute, AMissingAttributeTheWrongTypeOrANulCharacterThrows) {
  File file = File::create(path("run.h5"));
  file.set_attribute("dt", 0.001);
  file.set_attribute("names", std::vector<std::string>{"a", "b"});
  try {
    static_cast<void>(file.attribute_float64("missing"));
    FAIL() << "a missing attribute did not throw";
  } catch (const Hdf5Error& error) {
    EXPECT_EQ(error.object_path(), "/");
    EXPECT_NE(std::string{error.what()}.find("missing"), std::string::npos) << error.what();
  }
  EXPECT_THROW(static_cast<void>(file.attribute_uint64("dt")), Hdf5Error);
  EXPECT_THROW(static_cast<void>(file.attribute_int64("dt")), Hdf5Error);
  EXPECT_THROW(static_cast<void>(file.attribute_string("dt")), Hdf5Error);
  EXPECT_THROW(static_cast<void>(file.attribute_string("names")), Hdf5Error);
  EXPECT_THROW(static_cast<void>(file.attribute_strings("dt")), Hdf5Error);
  EXPECT_THROW(file.set_attribute("bad", std::string_view{"a\0b", 3}), Hdf5Error);
  EXPECT_FALSE(file.has_attribute("bad"));
}

// --- Hdf5Determinism (ADR-0025 Q7) ------------------------------------------------------------

// One fixed write sequence shaped like ADR-0025's run log: provenance attributes on the root, a
// rate group with a uint64 frame series and a float64 value table (names and units attributes),
// appends crossing chunk boundaries, a second rate group, and `complete` rewritten 0 -> 1 last.
// scale changes one stored value, for the sensitivity check.
void write_run_log_shape(const fs::path& path, double scale) {
  File file = File::create(path);
  file.set_attribute("complete", std::uint64_t{0});
  file.set_attribute("config", "sim:\n  dt: 0.001\n  seed: 12345\n");
  file.set_attribute("git_describe", "19d535a-dirty");
  file.set_attribute("core_version", "0.1.0");
  file.set_attribute("core_api_version", std::uint64_t{7});
  file.set_attribute("dt", 0.001);
  for (const std::uint64_t divisor : {1U, 10U}) {
    Group rate = file.create_group("rate_" + std::to_string(divisor));
    Dataset frames = rate.create_series("frame", ElementType::kUint64, 16);
    Dataset values = rate.create_table("values", ElementType::kFloat64, 3, 16);
    values.set_attribute("names", std::vector<std::string>{"v0.state.x", "v0.state.u", "wind.n"});
    values.set_attribute("units", std::vector<std::string>{"m", "m/s", "m/s"});
    std::uint64_t frame = 0;
    for (const std::size_t drain : {7U, 16U, 33U, 1U}) {
      std::vector<std::uint64_t> frame_rows;
      std::vector<double> value_rows;
      for (std::size_t i = 0; i < drain; ++i, frame += divisor) {
        frame_rows.push_back(frame);
        const auto t = static_cast<double>(frame) * 0.001;
        value_rows.insert(value_rows.end(), {t * scale, 250.0 - t, -0.5 * t});
      }
      frames.append(frame_rows);
      values.append(value_rows);
    }
  }
  file.set_attribute("complete", std::uint64_t{1});
  file.close();
}

using Hdf5Determinism = Hdf5Test;

// ADR-0025 Q7: byte identity, no fallback. The two writes go to different directories and are
// separated by more than one second (HDF5's timestamps have one-second resolution, so a stored
// creation or modification time would differ) with the heap dirtied in between (uninitialized
// bytes reaching the file would differ).
TEST_F(Hdf5Determinism, IdenticalWriteSequencesToDifferentPathsGiveIdenticalBytes) {
  fs::create_directories(path("first"));
  fs::create_directories(path("second"));
  const fs::path first = path("first") / "run.h5";
  const fs::path second = path("second") / "other_name.h5";

  write_run_log_shape(first, 1.0);
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  for (int i = 0; i < 64; ++i) {
    std::vector<unsigned char> noise(4096U * static_cast<std::size_t>(1 + (i % 7)),
                                     static_cast<unsigned char>(0xA5 + i));
    ASSERT_EQ(noise.front(), static_cast<unsigned char>(0xA5 + i));
  }
  write_run_log_shape(second, 1.0);

  const std::vector<char> a = read_bytes(first);
  const std::vector<char> b = read_bytes(second);
  ASSERT_FALSE(a.empty());
  EXPECT_EQ(a.size(), b.size());
  EXPECT_TRUE(a == b) << "HDF5 wrote different bytes for identical write sequences";
}

// The comparison above is not vacuous: one different stored value gives different bytes.
TEST_F(Hdf5Determinism, OneDifferentValueGivesDifferentBytes) {
  write_run_log_shape(path("a.h5"), 1.0);
  write_run_log_shape(path("b.h5"), 2.0);
  EXPECT_FALSE(read_bytes(path("a.h5")) == read_bytes(path("b.h5")));
}

// --- Hdf5Records (M5 PR 4: the compound tables behind /plugins and /writes) ---------------------

using Hdf5Records = Hdf5Test;
using astra::hdf5::RecordField;

std::vector<RecordField> sample_records() {
  std::vector<RecordField> fields;
  fields.push_back({"frame", std::vector<std::uint64_t>{0, 7, 18446744073709551615ULL}});
  fields.push_back({"name", std::vector<std::string>{"controls.elevator", "", "ünïcode"}});
  fields.push_back({"value", std::vector<double>{-0.5, 1e300, -0.0}});
  return fields;
}

TEST_F(Hdf5Records, EveryMemberKindRoundTripsInOrderAndExactly) {
  {
    File file = File::create(path("records.h5"));
    file.write_records("writes", sample_records());
    Group group = file.create_group("g");
    group.write_records("nested", sample_records());
    group.close();
    file.close();
  }
  const File file = File::open_read_only(path("records.h5"));
  for (const auto& read :
       {file.read_records("writes"), file.open_group("g").read_records("nested")}) {
    ASSERT_EQ(read.size(), 3U);
    EXPECT_EQ(read[0].name, "frame");
    EXPECT_EQ(read[1].name, "name");
    EXPECT_EQ(read[2].name, "value");
    EXPECT_EQ(std::get<std::vector<std::uint64_t>>(read[0].values),
              (std::vector<std::uint64_t>{0, 7, 18446744073709551615ULL}));
    EXPECT_EQ(std::get<std::vector<std::string>>(read[1].values),
              (std::vector<std::string>{"controls.elevator", "", "ünïcode"}));
    const auto& values = std::get<std::vector<double>>(read[2].values);
    ASSERT_EQ(values.size(), 3U);
    EXPECT_EQ(values[0], -0.5);
    EXPECT_EQ(values[1], 1e300);
    EXPECT_TRUE(std::signbit(values[2])) << "-0.0 is stored bit for bit";
  }
}

TEST_F(Hdf5Records, ZeroRowsIsAValidTableWithItsMembers) {
  {
    File file = File::create(path("empty.h5"));
    std::vector<RecordField> fields;
    fields.push_back({"frame", std::vector<std::uint64_t>{}});
    fields.push_back({"name", std::vector<std::string>{}});
    file.write_records("writes", fields);
    file.close();
  }
  const std::vector<RecordField> read =
      File::open_read_only(path("empty.h5")).read_records("writes");
  ASSERT_EQ(read.size(), 2U);
  EXPECT_EQ(read[0].name, "frame");
  EXPECT_EQ(read[0].rows(), 0U);
  EXPECT_TRUE(std::holds_alternative<std::vector<std::string>>(read[1].values));
}

TEST_F(Hdf5Records, BadFieldsThrowNamingTheTable) {
  File file = File::create(path("bad.h5"));
  const auto expect_refused = [&file](const std::vector<RecordField>& fields,
                                      const std::string& fragment) {
    try {
      file.write_records("t", fields);
      FAIL() << "expected Hdf5Error containing " << fragment;
    } catch (const Hdf5Error& error) {
      EXPECT_EQ(error.object_path(), "/t");
      EXPECT_NE(std::string{error.what()}.find(fragment), std::string::npos) << error.what();
    }
  };
  expect_refused({}, "at least one field");
  expect_refused({{"a", std::vector<double>{1.0}}, {"b", std::vector<double>{1.0, 2.0}}}, "rows");
  expect_refused({{"a", std::vector<double>{1.0}}, {"a", std::vector<double>{2.0}}}, "distinct");
  expect_refused({{"", std::vector<double>{1.0}}}, "non-empty");
  expect_refused({{"s", std::vector<std::string>{std::string{"a\0b", 3}}}}, "NUL");
}

TEST_F(Hdf5Records, IdenticalTablesToDifferentPathsGiveIdenticalBytes) {
  for (const char* name : {"a.h5", "b.h5"}) {
    File file = File::create(path(name));
    file.write_records("writes", sample_records());
    file.close();
  }
  EXPECT_TRUE(read_bytes(path("a.h5")) == read_bytes(path("b.h5")));
}

}  // namespace
