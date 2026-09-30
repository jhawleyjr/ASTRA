// The run log (ADR-0025, docs/plans/m5-tooling.md PR 4; finalization per ADR-0027,
// docs/plans/m6-pr2-finalization.md): the logger stage of Simulation::step() and the HDF5 file it
// drains to. Every registered signal is a column of its rate group's table (/rate_<divisor>/values,
// with /rate_<divisor>/frame), one row per due frame.
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
// dataset's chunk); close() writes the partial blocks. After every block write, on both paths, the
// file is flushed and the group's rows_committed attribute is set to its rows written so far
// (ADR-0027 item 2). The sequence of HDF5 calls is therefore the same whatever buffer_frames is and
// whenever the caller drains, so two runs of one config with the same writes give byte-identical
// files (ADR-0025 Q7) however each was drained.
//
// Where the file lives (ADR-0027 items 1, 3, 8). The constructor holds log.path's directory as a
// descriptor and creates "<name>.partial" in it exclusively; every later operation on the file by
// name (the publication rename, a delete) goes through that descriptor and first checks that the
// name is still this run's file, by comparing it with a descriptor the log keeps to the file it
// created. close() publishes the finished file to log.path with renameat2(RENAME_NOREPLACE);
// nothing else ever puts a file there. The destructor path (finalize_by_destructor()) marks the
// file finalized_by = "destructor" and never renames. discard() is the only delete after init.
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

// Test-only fault injection: the named write throws as if HDF5 had failed.
enum class RunLogFault : std::uint8_t {
  kNone,
  kDrainWrite,         // a drain's block write
  kWrites,             // /writes, in close()
  kSummary,            // /summary, in close()
  kFinalState,         // /final_state, in close()
  kComplete,           // the root attribute complete = 1, in close()
  kRenameUnsupported,  // the publication: renameat2 behaves as if it failed with EINVAL
  kReopen,             // init only (RunLogTestHooks): the kept descriptor's reopen fails
  kProvenance,         // init only (RunLogTestHooks): the provenance write fails
};

// Test-only: points at which the process kills itself with SIGKILL (ADR-0027 item 5's kill
// tests), in the order a run reaches them. kCloseAfterMarker fires on close()'s complete = 1 and on
// the destructor's finalized_by alike.
enum class RunLogKillPoint : std::uint8_t {
  kNone,
  kAfterCreate,             // after the exclusive create, before the provenance flush
  kAfterProvenanceFlush,    // the provenance and empty datasets are on disk
  kBlockAfterValues,        // between a block's values and frame appends
  kBlockAfterFlush,         // after a block's flush, before its rows_committed
  kBlockAfterCommitted,     // after a block's rows_committed
  kCloseAfterTails,         // close: every tail block written
  kCloseAfterFinalObjects,  // close: /writes, /summary and /final_state written
  kCloseAfterMarker,        // close or destructor: the marker set, the file not yet closed
  kCloseBeforeRename,       // close: the file closed and the identity check passed
  kCloseAfterRename,        // close: the publication point passed
};

// Test-only hooks a test that builds a RunLog directly (or through RunLogSettings) sets before
// construction: the init faults and init kill points cannot be injected afterwards.
struct RunLogTestHooks {
  RunLogFault init_fault = RunLogFault::kNone;
  RunLogKillPoint kill = RunLogKillPoint::kNone;
  std::uint64_t kill_occurrence = 1;  // the n-th time the point is reached (1-based)
  std::uint32_t kill_divisor = 0;     // block points only: this rate group's (0: any)
};

// The top-level "log" block (ADR-0025 Q5): log: { path, buffer_frames }.
struct RunLogSettings {
  std::filesystem::path path;
  std::size_t buffer_frames = 0;
  // The config document's origin, for the ConfigErrors RunLog's constructor throws.
  std::string origin;
  RunLogTestHooks test_hooks;
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

// What Simulation::discard_log() did (ADR-0027 item 8).
enum class LogDiscardOutcome : std::uint8_t {
  kNoLog,     // no "log" block: nothing to discard
  kDeleted,   // the run's own file was unlinked
  kMissing,   // nothing is at the name the file should have: nothing deleted
  kMismatch,  // the name holds a symlink or another file: nothing deleted
  kFailed,    // the check passed but unlinkat failed (detail says why): nothing deleted
};

struct LogDiscardResult {
  LogDiscardOutcome outcome = LogDiscardOutcome::kNoLog;
  std::string path;    // the name acted on: "<log.path>.partial" or log.path; empty without a log
  std::string detail;  // why nothing was deleted; empty otherwise

  [[nodiscard]] bool deleted() const noexcept { return outcome == LogDiscardOutcome::kDeleted; }
};

class RunLog {
 public:
  static constexpr std::size_t kDefaultBufferFrames = 4096;
  // Rows per HDF5 write and per chunk. Fixed, never derived from buffer_frames (see the header
  // comment).
  static constexpr std::size_t kBlockRows = 1024;
  // Root attribute "log_format_version": the layout this file follows (2: ADR-0027).
  static constexpr std::uint64_t kFormatVersion = 2;
  static constexpr std::string_view kPartialSuffix = ".partial";

  using Fault = RunLogFault;
  using KillPoint = RunLogKillPoint;

  // Reads config's top-level "log" block, or returns nullopt when there is none (no logger at
  // all). Marks path and buffer_frames as read, so any other key under "log" is left for
  // Simulation's unknown-key check, which names it (log.<key>). Throws ConfigError naming "log"
  // (not a map), "log.path" (missing, empty, not naming a file, or ending in ".partial"), or
  // "log.buffer_frames" (below 1). Whether the path exists is the constructor's check.
  [[nodiscard]] static std::optional<RunLogSettings> read_settings(const Config& config);

  // Builds one ring per rate group in `registry` (frozen), sized settings.buffer_frames rows,
  // opens log.path's directory, creates "<log.path>.partial" exclusively in it and writes the
  // provenance attributes, /plugins, every rate group's empty datasets and rows_committed = 0,
  // then flushes. vehicles' combined state buffers must stay at their addresses for this object's
  // lifetime (they do: Simulation owns both). Throws ConfigError naming log.path when the
  // directory cannot be opened, when log.path or its .partial exists (a dangling symlink counts),
  // or when the file cannot be created, reopened or written (a file this call created is removed
  // after the identity check), and naming log.buffer_frames when the rings cannot be allocated.
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

  // ADR-0027 item 3: drains, writes the tail blocks, /writes, /summary and /final_state, flushes,
  // sets the root attribute complete = 1, closes the file, checks that "<log.path>.partial" is
  // still this run's file and publishes it to log.path with renameat2(RENAME_NOREPLACE). Throws
  // LogError naming the write that failed (or, after an earlier failed drain, that drain), or
  // naming "the publication" and its reason (the .partial is no longer this run's file; a file
  // appeared at log.path; the rename is not supported; any other errno), renaming nothing and
  // leaving the .partial in place. Idempotent: once it has run, a later call does nothing after a
  // success and throws the same LogError after a failure; after discard() it throws "discarded".
  void close(const RunLogEnd& end);
  // ADR-0027 item 4, for Simulation's destructor: close()'s writes with the root attribute
  // finalized_by = "destructor" in place of complete, and never a rename. After an earlier failure
  // it writes nothing. Never throws.
  void finalize_by_destructor(const RunLogEnd& end) noexcept;
  // ADR-0027 item 4, for Simulation's destructor after a kLog halt (a full ring, which records no
  // write failure here): closes every handle and writes nothing more, no tail, no closing object
  // and no marker, so the file is left incomplete. Never throws.
  void close_after_log_halt() noexcept;
  // ADR-0027 item 8, Simulation::discard_log(): stops writing (an open file is closed with no tail
  // and no marker), then unlinks the file where it now lives, "<log.path>.partial" before the
  // publication and log.path after it, only if that name is still this run's file. Idempotent:
  // the first result is returned again.
  LogDiscardResult discard();
  // ADR-0027 item 8's init-failure delete: for a Simulation whose construction failed after the
  // file was created (a transport handshake that failed). Closes every handle and unlinks the
  // .partial if it is still this run's file. Never throws.
  void discard_after_failed_init() noexcept;

  [[nodiscard]] bool closed() const noexcept { return closed_; }
  [[nodiscard]] bool failed() const noexcept { return !failure_.empty(); }
  [[nodiscard]] const std::string& failure() const noexcept { return failure_; }
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  void inject_fault_for_testing(Fault fault) noexcept { injected_ = fault; }
  void inject_kill_for_testing(KillPoint point, std::uint64_t occurrence = 1,
                               std::uint32_t divisor = 0) noexcept {
    kill_ = RunLogTestHooks{RunLogFault::kNone, point, occurrence, divisor};
    kill_seen_ = 0;
  }

 private:
  // An owned POSIX descriptor, closed on destruction.
  class Descriptor {
   public:
    Descriptor() noexcept = default;
    explicit Descriptor(int fd) noexcept : fd_(fd) {}
    ~Descriptor();
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    Descriptor(Descriptor&& other) noexcept;
    Descriptor& operator=(Descriptor&& other) noexcept;
    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

   private:
    int fd_ = -1;
  };

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
    std::uint64_t rows_written = 0;  // rows_committed after the last block
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

  // How close() and the destructor mark the finished file.
  enum class Marker : std::uint8_t { kComplete, kDestructor };

  void build_rings(const SignalRegistry& registry, std::span<const Vehicle> vehicles);
  void open_directory();
  void refuse_existing(const std::string& name) const;
  void create_file();
  void write_provenance(const RunLogProvenance& provenance);
  void move_rows_to_blocks();
  void write_block(Ring& ring);
  void write_writes();
  void write_summary(const RunLogEnd& end);
  void write_final_state(const RunLogEnd& end);
  void close_ring_handles();
  // close()'s steps 1 to 5 (ADR-0027 item 3), with the given marker.
  void finish_file(const RunLogEnd& end, Marker marker);
  // close()'s steps 6 and 7: the identity check and the rename.
  void publish();
  [[noreturn]] void refuse_publication(std::string_view reason);
  // The identity check and unlinkat of `name` against `reference`, a descriptor of this run's
  // file (the kept one, or HDF5's own during init).
  [[nodiscard]] LogDiscardResult checked_unlink(const std::string& name, int reference) const;
  [[nodiscard]] std::string display(const std::string& name) const;
  // Drops every handle (their destructors close best effort) without writing anything more.
  void abandon() noexcept;
  // Records the first failure: what was being written (writing_) and why.
  void note_failure(std::string_view message) noexcept;
  void check_injected(Fault fault);
  void kill_point(KillPoint point, std::uint32_t divisor = 0) noexcept;
  [[nodiscard]] static bool due(const Ring& ring, std::uint64_t frame) noexcept {
    return frame % ring.divisor == 0;
  }

  std::filesystem::path path_;
  std::string origin_;
  std::string name_;          // log.path's file name, in directory_
  std::string partial_name_;  // name_ + ".partial"
  Descriptor directory_;      // log.path's directory (O_PATH)
  Descriptor kept_;           // this run's file (O_PATH, reopened through /proc/self/fd)
  bool published_ = false;    // the rename succeeded: the file lives at name_
  std::optional<LogDiscardResult> discarded_;
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
  bool injected_fired_ = false;  // the failure, if any, was an injected one
  RunLogTestHooks kill_;
  std::uint64_t kill_seen_ = 0;
};

}  // namespace astra
