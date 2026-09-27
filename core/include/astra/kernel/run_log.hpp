// The run log (ADR-0025, docs/plans/m5-tooling.md PR 4): the logger stage of Simulation::step()
// and the HDF5 file it drains to. Every registered signal is a column of its rate group's table
// (/rate_<divisor>/values, with /rate_<divisor>/frame), one row per due frame.
//
// Row k describes t_k (ADR-0025 Q3). Simulation::step() calls reserve() first, before any
// environment, exchange, model, integrator or RNG side effect; reserve() takes the frame's row in
// every due ring and fills its state columns from the frame-start combined state buffers (the
// allocation-free frame-start snapshot: the published "<v>.state.*" and model-owned state signals
// point into the stage buffer, which holds end-of-frame values by the time the frame ends).
// commit(), at the end of a completed frame, fills every other column through its registry
// pointer, which then holds the frame-k value. A frame that halts is never committed, so its row is
// taken again by the next reservation. Both are allocation-free, exception-free, and never call
// HDF5.
//
// Rows leave the rings only in drain() (Simulation::run() past the high-water mark,
// Simulation::flush_log()) and close(). A drain moves rows, in frame order across rate groups, into
// one staging block per group and writes a block to HDF5 only when it holds kBlockRows rows (the
// dataset's chunk); close() writes the partial blocks. The sequence of HDF5 calls is therefore the
// same whatever buffer_frames is and whenever the caller drains, so two runs of one config with the
// same writes give byte-identical files (ADR-0025 Q7) however each was drained.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "astra/kernel/health_monitor.hpp"
#include "astra/kernel/vehicle.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/hdf5_file.hpp"
#include "astra/signals/signal_registry.hpp"

namespace astra {

class Config;  // core/include/astra/config/config.hpp

// The top-level "log" block (ADR-0025 Q5): log: { path, buffer_frames }.
struct RunLogSettings {
  std::filesystem::path path;
  std::size_t buffer_frames = 0;
  // The config document's origin, for the ConfigErrors RunLog's constructor throws.
  std::string origin;
};

// Provenance written at open (ADR-0025 Q9, Q10).
struct RunLogProvenance {
  std::string config_text;  // the run's config without its "log" block
  double dt = 0.0;
  std::span<const PluginLoadRecord> plugins;
};

// What close() records about the run's end.
struct RunLogEnd {
  std::uint64_t frame = 0;  // the clock's frame after the last call
  HaltReason halt;          // what the last step() or run() returned (falsy: no halt)
  std::span<const Vehicle> vehicles;
};

class RunLog {
 public:
  static constexpr std::size_t kDefaultBufferFrames = 4096;
  // Rows per HDF5 write and per chunk. Fixed, never derived from buffer_frames (see the header
  // comment).
  static constexpr std::size_t kBlockRows = 1024;
  // Root attribute "log_format_version": the layout this file follows.
  static constexpr std::uint64_t kFormatVersion = 1;

  // Test-only fault injection: the named write throws as if HDF5 had failed.
  enum class Fault : std::uint8_t {
    kNone,
    kDrainWrite,  // a drain's block write
    kWrites,      // /writes, in close()
    kSummary,     // /summary, in close()
    kFinalState,  // /final_state, in close()
    kComplete,    // the root attribute complete = 1, in close()
  };

  // Reads config's top-level "log" block, or returns nullopt when there is none (no logger at
  // all). Marks path and buffer_frames as read, so any other key under "log" is left for
  // Simulation's unknown-key check, which names it (log.<key>). Throws ConfigError naming "log"
  // (not a map), "log.path" (missing, empty, or an existing file: a log is never overwritten), or
  // "log.buffer_frames" (below 1).
  [[nodiscard]] static std::optional<RunLogSettings> read_settings(const Config& config);

  // Builds one ring per rate group in `registry` (frozen), sized settings.buffer_frames rows,
  // creates the file and writes the provenance attributes, /plugins and every rate group's empty
  // datasets. vehicles' combined state buffers must stay at their addresses for this object's
  // lifetime (they do: Simulation owns both). Throws ConfigError naming log.path when the file
  // exists or cannot be created or written (a partly written file is removed), and naming
  // log.buffer_frames when the rings cannot be allocated.
  RunLog(const RunLogSettings& settings, const SignalRegistry& registry,
         std::span<const Vehicle> vehicles, const RunLogProvenance& provenance);
  ~RunLog();
  RunLog(const RunLog&) = delete;
  RunLog& operator=(const RunLog&) = delete;
  RunLog(RunLog&&) = delete;
  RunLog& operator=(RunLog&&) = delete;

  // Per-frame path (no allocation, no exceptions, no HDF5). reserve() is all or nothing: false
  // when a ring due at `frame` has no free row, leaving every ring as it was. commit(frame)
  // completes the rows reserve(frame) took; a reservation never committed is simply retaken.
  [[nodiscard]] bool reserve(std::uint64_t frame) noexcept;
  void commit(std::uint64_t frame) noexcept;

  // Between frames. past_high_water(): some ring holds at least half its rows (at least one).
  [[nodiscard]] bool past_high_water() const noexcept;
  // Empties every ring (see the header comment). False when a write failed: the log is then
  // failed for good, failure() says why, and every later drain() returns false at once.
  [[nodiscard]] bool drain() noexcept;
  // One external Simulation::write_input call, written to /writes at close(). Allocates.
  void record_write(std::uint64_t frame, std::string_view name, double value);

  // Drains, writes the partial blocks, /writes, /summary and /final_state, flushes, sets the root
  // attribute complete = 1 and closes the file. Throws LogError naming the write that failed (or,
  // after an earlier failed drain, that drain); complete is then never set and the file is closed
  // best effort. Idempotent: once it has run, a later call does nothing after a success and throws
  // the same LogError after a failure.
  void close(const RunLogEnd& end);
  // close() without the exception, for Simulation's destructor.
  void close_best_effort(const RunLogEnd& end) noexcept;
  // Closes every handle and removes the file: for a Simulation whose construction failed after
  // the file was created (a transport handshake that failed). Best effort, never throws.
  void discard() noexcept;

  [[nodiscard]] bool closed() const noexcept { return closed_; }
  [[nodiscard]] bool failed() const noexcept { return !failure_.empty(); }
  [[nodiscard]] const std::string& failure() const noexcept { return failure_; }
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  void inject_fault_for_testing(Fault fault) noexcept { injected_ = fault; }

 private:
  // One rate group: its ring, its staging block and its two datasets.
  struct Ring {
    std::uint32_t divisor = 1;
    std::size_t columns = 0;
    std::vector<std::size_t> signal_indices;  // registry index of each column
    // Filled at reserve() from the frame-start combined state; every other column at commit().
    std::vector<std::size_t> state_columns;
    std::vector<const double*> state_sources;
    std::vector<std::size_t> signal_columns;
    std::vector<const double*> signal_sources;
    std::vector<double> rows;           // capacity x columns
    std::vector<std::uint64_t> frames;  // capacity
    std::size_t head = 0;
    std::size_t count = 0;
    std::vector<double> block_values;  // kBlockRows x columns
    std::vector<std::uint64_t> block_frames;
    std::size_t block_count = 0;
    std::optional<hdf5::Group> group;
    std::optional<hdf5::Dataset> frame_dataset;
    std::optional<hdf5::Dataset> values_dataset;
    std::string write_label;  // "the rows of /rate_<divisor>", what a failed block write names
  };

  // A combined-state entry of one vehicle, for /final_state's names.
  struct StateName {
    std::uint32_t vehicle = 0;
    std::size_t offset = 0;
    std::string name;
  };

  void build_rings(const SignalRegistry& registry, std::span<const Vehicle> vehicles);
  void write_provenance(const RunLogProvenance& provenance);
  void move_rows_to_blocks();
  void write_block(Ring& ring);
  void write_writes();
  void write_summary(const RunLogEnd& end);
  void write_final_state(const RunLogEnd& end);
  void close_handles();
  // Drops every handle (their destructors close best effort) without writing anything more.
  void abandon() noexcept;
  // Records the first failure: what was being written (writing_) and why.
  void note_failure(std::string_view message) noexcept;
  void check_injected(Fault fault) const;
  [[nodiscard]] static bool due(const Ring& ring, std::uint64_t frame) noexcept {
    return frame % ring.divisor == 0;
  }

  std::filesystem::path path_;
  std::string origin_;
  std::size_t capacity_ = 0;
  std::size_t high_water_ = 1;
  std::vector<Ring> rings_;  // ascending divisor
  // Every registered signal, in registry order: /summary's names, min and max.
  std::vector<std::string> signal_names_;
  std::vector<std::string> signal_units_;
  std::vector<double> minimum_;
  std::vector<double> maximum_;
  // The health.* signals /summary records at close.
  std::vector<std::string> health_names_;
  std::vector<const double*> health_sources_;
  std::vector<StateName> state_names_;
  // /writes, in call order.
  std::vector<std::uint64_t> write_frames_;
  std::vector<std::string> write_names_;
  std::vector<double> write_values_;

  std::optional<hdf5::File> file_;
  bool closed_ = false;
  std::string failure_;       // empty until a write fails
  std::string_view writing_;  // the write in progress, for a failure's message
  std::string failed_write_;  // what failed ("the rows of /rate_1", "/summary", ...)
  Fault injected_ = Fault::kNone;
};

}  // namespace astra
