#include "astra/kernel/run_log.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <exception>
#include <functional>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "astra/build_info.hpp"
#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/util/sha256.hpp"

namespace astra {

namespace {

constexpr std::string_view kHealthPrefix = "health.";
constexpr std::string_view kPublication = "the publication";
constexpr std::string_view kProcFd = "/proc/self/fd/";

std::string errno_text(int error) {
  return std::error_code(error, std::generic_category()).message();
}

// ADR-0027 item 1's identity check: is `name` in `directory` the file `reference` is open on?
// fstatat never follows a symlink, so a symlink at the name is never this run's file.
enum class Identity : std::uint8_t { kSame, kMissing, kOther, kError };

Identity identify(int directory, const std::string& name, int reference, const std::string& shown,
                  std::string& detail) {
  struct stat ours {};
  if (::fstat(reference, &ours) != 0) {
    detail = "cannot read this run's own file: " + errno_text(errno);
    return Identity::kError;
  }
  struct stat named {};
  if (::fstatat(directory, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0) {
    const int error = errno;
    if (error == ENOENT) {
      detail = "nothing is at '" + shown + "'";
      return Identity::kMissing;
    }
    detail = "cannot read '" + shown + "': " + errno_text(error);
    return Identity::kError;
  }
  if (S_ISLNK(named.st_mode)) {
    detail = "'" + shown + "' is a symlink";
    return Identity::kOther;
  }
  if (named.st_dev != ours.st_dev || named.st_ino != ours.st_ino) {
    detail = "'" + shown + "' is another file";
    return Identity::kOther;
  }
  return Identity::kSame;
}

std::string rate_group_name(std::uint32_t divisor) { return "rate_" + std::to_string(divisor); }

// The combined-state offset `value` names when it points into `vehicle`'s stage buffer. std::less
// gives a total order over pointers into different arrays, so the range test is well defined.
std::optional<std::size_t> stage_offset(const Vehicle& vehicle, const double* value) noexcept {
  const std::span<const double> stage = vehicle.published_state();
  if (stage.empty()) {
    return std::nullopt;
  }
  const std::less<> before;
  if (before(value, stage.data()) || !before(value, stage.data() + stage.size())) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(value - stage.data());
}

// The HDF5 object `handle` holds. The log writes only while its handles are open (abandon() and
// close() empty them only on the way out), so an empty one is a logic error, reported as a write
// failure rather than undefined behavior.
template <typename T>
T& open_handle(std::optional<T>& handle, const std::filesystem::path& path) {
  if (!handle.has_value()) {
    throw Hdf5Error(path.string(), "", "the run log's HDF5 object is not open");
  }
  return *handle;
}

void update_extremes(double value, double& minimum, double& maximum) noexcept {
  if (std::isnan(value)) {
    return;
  }
  if (std::isnan(minimum) || value < minimum) {
    minimum = value;
  }
  if (std::isnan(maximum) || value > maximum) {
    maximum = value;
  }
}

std::vector<hdf5::RecordField> plugin_records(std::span<const PluginLoadRecord> plugins) {
  std::vector<std::string> paths;
  std::vector<std::string> categories;
  std::vector<std::string> names;
  std::vector<std::uint64_t> api_versions;
  std::vector<std::string> toolchains;
  std::vector<std::string> digests;
  for (const PluginLoadRecord& plugin : plugins) {
    const PluginDescriptor& descriptor = *plugin.descriptor;
    paths.push_back(plugin.plugin_path);
    categories.emplace_back(descriptor.category != nullptr ? descriptor.category : "");
    names.emplace_back(descriptor.name != nullptr ? descriptor.name : "");
    api_versions.push_back(descriptor.core_api_version);
    toolchains.emplace_back(descriptor.built_with != nullptr ? descriptor.built_with : "");
    // ADR-0025 Q9: the loaded library's bytes pin the plugin; a static build has no library.
    digests.push_back(plugin.library.empty() ? std::string{"static"}
                                             : sha256::file_hex_digest(plugin.library));
  }
  std::vector<hdf5::RecordField> fields;
  fields.push_back({"path", std::move(paths)});
  fields.push_back({"category", std::move(categories)});
  fields.push_back({"name", std::move(names)});
  fields.push_back({"core_api_version", std::move(api_versions)});
  fields.push_back({"built_with", std::move(toolchains)});
  fields.push_back({"sha256", std::move(digests)});
  return fields;
}

}  // namespace

std::optional<RunLogSettings> RunLog::read_settings(const Config& config) {
  if (!config.has("log")) {
    return std::nullopt;
  }
  const Config log = config.at("log");
  if (!log.is_map()) {
    throw ConfigError(config.origin(), "log", "must be a map: log: { path, buffer_frames }");
  }
  RunLogSettings settings;
  settings.origin = std::string{config.origin()};
  const std::string path = log.text("path");
  if (path.empty()) {
    throw ConfigError(config.origin(), "log.path", "must not be empty");
  }
  settings.path = path;
  const std::int64_t buffer_frames =
      log.integer("buffer_frames", static_cast<std::int64_t>(kDefaultBufferFrames));
  if (buffer_frames < 1) {
    throw ConfigError(config.origin(), "log.buffer_frames", "must be at least 1");
  }
  settings.buffer_frames = static_cast<std::size_t>(buffer_frames);
  // ADR-0027 item 1: the name must be a file's, and never the unpublished name a log is written
  // under. Whether log.path or its .partial exists is the constructor's check, through the
  // directory descriptor it then keeps.
  const std::string name = settings.path.filename().string();
  if (name.empty() || name == "." || name == "..") {
    throw ConfigError(config.origin(), "log.path", "'" + path + "' does not name a file");
  }
  if (name.ends_with(kPartialSuffix)) {
    throw ConfigError(config.origin(), "log.path",
                      "'" + path +
                          "' ends in .partial, the name a run log has until close_log() publishes "
                          "it (ADR-0027)");
  }
  return settings;
}

RunLog::RunLog(const RunLogSettings& settings, const SignalRegistry& registry,
               std::span<const Vehicle> vehicles, const RunLogProvenance& provenance)
    : path_(settings.path),
      origin_(settings.origin),
      capacity_(settings.buffer_frames),
      high_water_(std::max<std::size_t>(1, settings.buffer_frames / 2)),
      kill_(settings.test_hooks) {
  try {
    build_rings(registry, vehicles);
  } catch (const std::bad_alloc&) {
    throw ConfigError(origin_, "log.buffer_frames", "the log's rings cannot be allocated");
  } catch (const std::length_error&) {
    throw ConfigError(origin_, "log.buffer_frames", "the log's rings cannot be allocated");
  }
  // Hashed before the file exists, so a plugin that cannot be read leaves no file behind.
  const std::vector<hdf5::RecordField> plugins = plugin_records(provenance.plugins);
  open_directory();
  refuse_existing(name_);
  refuse_existing(partial_name_);
  create_file();
  try {
    if (settings.test_hooks.init_fault == Fault::kProvenance) {
      writing_ = "the provenance";
      injected_fired_ = true;
      throw Hdf5Error(display(partial_name_), "", "injected failure (test)");
    }
    write_provenance(provenance);
    hdf5::File& file = open_handle(file_, path_);
    file.write_records("plugins", plugins);
    file.flush();
  } catch (const Hdf5Error& error) {
    note_failure(error.what());
    abandon();
    closed_ = true;
    // ADR-0027 item 8: this call created the .partial, and it is failing: the one delete by the
    // core outside discard_log(), after the identity check.
    const LogDiscardResult removed = checked_unlink(partial_name_, kept_.get());
    throw ConfigError(origin_, "log.path",
                      std::string{"cannot write the log's provenance: "} + error.what() +
                          (removed.deleted() ? "" : "; the .partial was left: " + removed.detail));
  }
  kill_point(KillPoint::kAfterProvenanceFlush);
}

RunLog::~RunLog() = default;

RunLog::Descriptor::~Descriptor() {
  if (fd_ >= 0) {
    static_cast<void>(::close(fd_));
  }
}

RunLog::Descriptor::Descriptor(Descriptor&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

RunLog::Descriptor& RunLog::Descriptor::operator=(Descriptor&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) {
      static_cast<void>(::close(fd_));
    }
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

std::string RunLog::display(const std::string& name) const {
  return (path_.parent_path() / name).string();
}

// ADR-0027 item 1: the directory is held once, here, and every later operation by name goes
// through it, so neither a chdir nor a rename of an ancestor redirects the create, the rename or a
// delete. A relative log.path is resolved against the working directory at this call.
void RunLog::open_directory() {
  std::filesystem::path parent = path_.parent_path();
  if (parent.empty()) {
    parent = ".";
  }
  const int fd = ::open(parent.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) {
    const int error = errno;
    throw ConfigError(origin_, "log.path",
                      "cannot open the directory '" + parent.string() + "' of '" + path_.string() +
                          "': " + errno_text(error));
  }
  directory_ = Descriptor{fd};
  name_ = path_.filename().string();
  partial_name_ = name_ + std::string{kPartialSuffix};
}

// ADR-0025 Q5 and ADR-0027 item 1: never overwritten, and a stale .partial is the user's. A
// dangling symlink counts as existing (no follow).
void RunLog::refuse_existing(const std::string& name) const {
  struct stat info {};
  if (::fstatat(directory_.get(), name.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0) {
    if (name == name_) {
      throw ConfigError(
          origin_, "log.path",
          "'" + path_.string() + "' already exists; a run log never overwrites a file");
    }
    throw ConfigError(origin_, "log.path",
                      "'" + display(name) +
                          "' already exists: a stale partial log belongs to the user, and ASTRA "
                          "never opens, changes or deletes it (ADR-0027); move it aside or choose "
                          "another log.path");
  }
  const int error = errno;
  if (error != ENOENT) {
    throw ConfigError(origin_, "log.path",
                      "cannot check whether '" + display(name) + "' exists: " + errno_text(error));
  }
}

// ADR-0027 item 1: the exclusive create of the .partial through the directory descriptor, then a
// descriptor of the log's own, reopened from HDF5's through /proc/self/fd: a separate open file
// description (never a dup(), which would share HDF5's file lock), O_PATH (no read permission
// needed, no lock), O_CLOEXEC (never inherited by a lockstep peer).
void RunLog::create_file() {
  const std::string via =
      std::string{kProcFd} + std::to_string(directory_.get()) + "/" + partial_name_;
  try {
    file_.emplace(hdf5::File::create(via, display(partial_name_)));
  } catch (const Hdf5Error& error) {
    throw ConfigError(origin_, "log.path", error.what());
  }
  int hdf5_fd = -1;
  try {
    hdf5_fd = open_handle(file_, path_).vfd_descriptor();
  } catch (const Hdf5Error& error) {
    // No descriptor to check the name against, so nothing is deleted (ADR-0027 item 8).
    file_.reset();
    closed_ = true;
    throw ConfigError(
        origin_, "log.path",
        std::string{error.what()} + "; the .partial was left: '" + display(partial_name_) + "'");
  }
  int kept = -1;
  int error = EMFILE;
  if (kill_.init_fault != Fault::kReopen) {
    kept = ::open((std::string{kProcFd} + std::to_string(hdf5_fd)).c_str(), O_PATH | O_CLOEXEC);
    error = errno;
  }
  if (kept < 0) {
    // ADR-0027 item 1: the identity check runs against HDF5's own descriptor, still open here.
    const LogDiscardResult removed = checked_unlink(partial_name_, hdf5_fd);
    file_.reset();  // the file itself is sound: it closes normally
    closed_ = true;
    throw ConfigError(origin_, "log.path",
                      "cannot keep a descriptor to the created file (a reopen through "
                      "/proc/self/fd: " +
                          errno_text(error) + ")" +
                          (removed.deleted() ? "" : "; the .partial was left: " + removed.detail));
  }
  kept_ = Descriptor{kept};
  kill_point(KillPoint::kAfterCreate);
}

void RunLog::build_rings(const SignalRegistry& registry, std::span<const Vehicle> vehicles) {
  const std::span<const Signal> signals = registry.signals();
  std::vector<std::uint32_t> divisors;
  for (const Signal& signal : signals) {
    if (std::find(divisors.begin(), divisors.end(), signal.divisor) == divisors.end()) {
      divisors.push_back(signal.divisor);
    }
  }
  std::sort(divisors.begin(), divisors.end());
  rings_.resize(divisors.size());
  for (std::size_t r = 0; r < divisors.size(); ++r) {
    rings_[r].divisor = divisors[r];
  }
  signal_names_.reserve(signals.size());
  for (std::size_t i = 0; i < signals.size(); ++i) {
    const Signal& signal = signals[i];
    signal_names_.push_back(signal.name);
    signal_units_.push_back(signal.unit);
    if (signal.name.starts_with(kHealthPrefix)) {
      health_names_.push_back(signal.name);
      health_sources_.push_back(signal.value);
    }
    Ring& ring = *std::find_if(rings_.begin(), rings_.end(), [&signal](const Ring& candidate) {
      return candidate.divisor == signal.divisor;
    });
    const std::size_t column = ring.columns++;
    ring.signal_indices.push_back(i);
    bool is_state = false;
    for (const Vehicle& vehicle : vehicles) {
      if (const std::optional<std::size_t> offset = stage_offset(vehicle, signal.value)) {
        ring.state_columns.push_back(column);
        ring.state_sources.push_back(vehicle.combined_state().data() + *offset);
        state_names_.push_back(StateName{vehicle.index(), *offset, signal.name});
        is_state = true;
        break;
      }
    }
    if (!is_state) {
      ring.signal_columns.push_back(column);
      ring.signal_sources.push_back(signal.value);
    }
  }
  for (Ring& ring : rings_) {
    ring.write_label = "the rows of /" + rate_group_name(ring.divisor);
    if (capacity_ > std::numeric_limits<std::size_t>::max() / ring.columns) {
      throw std::length_error("buffer_frames x columns overflows");
    }
    ring.rows.assign(capacity_ * ring.columns, 0.0);
    ring.frames.assign(capacity_, 0);
    ring.block_values.assign(kBlockRows * ring.columns, 0.0);
    ring.block_frames.assign(kBlockRows, 0);
  }
  constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
  minimum_.assign(signals.size(), kNan);
  maximum_.assign(signals.size(), kNan);
}

void RunLog::write_provenance(const RunLogProvenance& provenance) {
  hdf5::File& file = open_handle(file_, path_);
  file.set_attribute("log_format_version", kFormatVersion);
  file.set_attribute("config", std::string_view{provenance.config_text});
  file.set_attribute("git_describe", std::string_view{git_describe()});
  file.set_attribute("core_version", std::string_view{core_version()});
  file.set_attribute("core_api_version", static_cast<std::uint64_t>(core_api_version()));
  file.set_attribute("dt", provenance.dt);
  for (Ring& ring : rings_) {
    ring.group.emplace(file.create_group(rate_group_name(ring.divisor)));
    ring.frame_dataset.emplace(open_handle(ring.group, path_)
                                   .create_series("frame", hdf5::ElementType::kUint64, kBlockRows));
    ring.values_dataset.emplace(
        open_handle(ring.group, path_)
            .create_table("values", hdf5::ElementType::kFloat64, ring.columns, kBlockRows));
    std::vector<std::string> names;
    std::vector<std::string> units;
    names.reserve(ring.columns);
    units.reserve(ring.columns);
    for (const std::size_t index : ring.signal_indices) {
      names.push_back(signal_names_[index]);
      units.push_back(signal_units_[index]);
    }
    open_handle(ring.values_dataset, path_).set_attribute("names", names);
    open_handle(ring.values_dataset, path_).set_attribute("units", units);
    open_handle(ring.group, path_).set_attribute("rows_committed", std::uint64_t{0});
  }
}

bool RunLog::reserve(std::uint64_t frame) noexcept {
  if (closed_) {
    return false;
  }
  for (const Ring& ring : rings_) {
    if (due(ring, frame) && ring.count == capacity_) {
      return false;
    }
  }
  for (Ring& ring : rings_) {
    if (!due(ring, frame)) {
      continue;
    }
    const std::size_t slot = (ring.head + ring.count) % capacity_;
    double* row = ring.rows.data() + (slot * ring.columns);
    for (std::size_t i = 0; i < ring.state_columns.size(); ++i) {
      row[ring.state_columns[i]] = *ring.state_sources[i];
    }
  }
  return true;
}

void RunLog::commit(std::uint64_t frame) noexcept {
  for (Ring& ring : rings_) {
    if (!due(ring, frame)) {
      continue;
    }
    const std::size_t slot = (ring.head + ring.count) % capacity_;
    double* row = ring.rows.data() + (slot * ring.columns);
    for (std::size_t i = 0; i < ring.signal_columns.size(); ++i) {
      row[ring.signal_columns[i]] = *ring.signal_sources[i];
    }
    ring.frames[slot] = frame;
    ++ring.count;
  }
}

bool RunLog::past_high_water() const noexcept {
  return std::any_of(rings_.begin(), rings_.end(),
                     [this](const Ring& ring) { return ring.count >= high_water_; });
}

bool RunLog::drain() noexcept {
  if (failed() || closed_) {
    return !failed();
  }
  try {
    move_rows_to_blocks();
    return true;
  } catch (const std::exception& error) {
    note_failure(error.what());
  } catch (...) {
    note_failure("unknown exception");
  }
  abandon();
  return false;
}

void RunLog::move_rows_to_blocks() {
  while (true) {
    // The oldest row across rate groups; a tie goes to the smaller divisor (rings_ is ascending).
    Ring* next = nullptr;
    for (Ring& ring : rings_) {
      if (ring.count != 0 &&
          (next == nullptr || ring.frames[ring.head] < next->frames[next->head])) {
        next = &ring;
      }
    }
    if (next == nullptr) {
      return;
    }
    Ring& ring = *next;
    const double* row = ring.rows.data() + (ring.head * ring.columns);
    std::copy_n(row, ring.columns, ring.block_values.data() + (ring.block_count * ring.columns));
    ring.block_frames[ring.block_count] = ring.frames[ring.head];
    ++ring.block_count;
    for (std::size_t c = 0; c < ring.columns; ++c) {
      const std::size_t index = ring.signal_indices[c];
      update_extremes(row[c], minimum_[index], maximum_[index]);
    }
    ring.head = (ring.head + 1) % capacity_;
    --ring.count;
    if (ring.block_count == kBlockRows) {
      write_block(ring);
    }
  }
}

// ADR-0027 item 2: values, frame, a flush, then rows_committed. A value set after the flush names
// only rows already on disk, so it stays true even if HDF5 writes it before the next flush.
void RunLog::write_block(Ring& ring) {
  writing_ = ring.write_label;
  check_injected(Fault::kDrainWrite);
  open_handle(ring.values_dataset, path_)
      .append(std::span<const double>{ring.block_values.data(), ring.block_count * ring.columns});
  kill_point(KillPoint::kBlockAfterValues, ring.divisor);
  open_handle(ring.frame_dataset, path_)
      .append(std::span<const std::uint64_t>{ring.block_frames.data(), ring.block_count});
  open_handle(file_, path_).flush();
  kill_point(KillPoint::kBlockAfterFlush, ring.divisor);
  ring.rows_written += ring.block_count;
  open_handle(ring.group, path_).overwrite_attribute("rows_committed", ring.rows_written);
  kill_point(KillPoint::kBlockAfterCommitted, ring.divisor);
  ring.block_count = 0;
}

void RunLog::record_write(std::uint64_t frame, std::string_view name, double value) {
  write_frames_.push_back(frame);
  write_names_.emplace_back(name);
  write_values_.push_back(value);
}

void RunLog::close(const RunLogEnd& end) {
  if (discarded_.has_value()) {
    throw LogError(path_.string(), "close_log()", "the log was discarded by discard_log()");
  }
  if (closed_) {
    if (failed()) {
      throw LogError(path_.string(), failed_write_, failure_);
    }
    return;
  }
  closed_ = true;
  if (failed()) {
    throw LogError(path_.string(), failed_write_, failure_);
  }
  try {
    finish_file(end, Marker::kComplete);
  } catch (const std::exception& error) {
    note_failure(error.what());
    abandon();
    throw LogError(path_.string(), failed_write_, failure_);
  }
  publish();
}

void RunLog::finish_file(const RunLogEnd& end, Marker marker) {
  move_rows_to_blocks();
  for (Ring& ring : rings_) {
    if (ring.block_count != 0) {
      write_block(ring);
    }
  }
  kill_point(KillPoint::kCloseAfterTails);
  writing_ = "/writes";
  check_injected(Fault::kWrites);
  write_writes();
  writing_ = "/summary";
  check_injected(Fault::kSummary);
  write_summary(end);
  writing_ = "/final_state";
  check_injected(Fault::kFinalState);
  write_final_state(end);
  kill_point(KillPoint::kCloseAfterFinalObjects);
  // Codex review round 1 (M5 PR 4), F1: every object but the file is closed, and everything is
  // flushed, before the marker, so the marker is the last write and certifies only data already on
  // disk. A file close that fails after it leaves only the .partial (ADR-0027 item 3).
  writing_ = "the close of the rate groups";
  close_ring_handles();
  writing_ = "the flush before the marker";
  open_handle(file_, path_).flush();
  if (marker == Marker::kComplete) {
    writing_ = "complete";
    check_injected(Fault::kComplete);
    open_handle(file_, path_).set_attribute("complete", std::int64_t{1});
  } else {
    writing_ = "finalized_by";
    open_handle(file_, path_).set_attribute("finalized_by", std::string_view{"destructor"});
  }
  kill_point(KillPoint::kCloseAfterMarker);
  writing_ = "the file's close";
  open_handle(file_, path_).close();
}

// ADR-0027 item 3, steps 6 and 7. The successful rename is the publication point.
void RunLog::publish() {
  writing_ = kPublication;
  std::string detail;
  if (identify(directory_.get(), partial_name_, kept_.get(), display(partial_name_), detail) !=
      Identity::kSame) {
    refuse_publication("the .partial is no longer this run's file (" + detail +
                       "); nothing was renamed or touched");
  }
  kill_point(KillPoint::kCloseBeforeRename);
  int result = -1;
  int error = EINVAL;
  if (injected_ == Fault::kRenameUnsupported) {
    injected_fired_ = true;
  } else {
    result = ::renameat2(directory_.get(), partial_name_.c_str(), directory_.get(), name_.c_str(),
                         RENAME_NOREPLACE);
    error = errno;
  }
  if (result != 0) {
    if (error == EEXIST) {
      refuse_publication("a file appeared at '" + path_.string() +
                         "' during the run (EEXIST); it is left untouched, and the .partial is "
                         "kept");
    }
    if (error == EINVAL || error == ENOSYS) {
      refuse_publication(
          "renameat2(RENAME_NOREPLACE) is not supported by this filesystem or "
          "kernel (" +
          errno_text(error) + "); nothing was published, and the .partial is kept");
    }
    refuse_publication("renameat2 failed (" + errno_text(error) +
                       "); nothing was published, and the .partial is kept");
  }
  published_ = true;
  kill_point(KillPoint::kCloseAfterRename);
}

void RunLog::refuse_publication(std::string_view reason) {
  writing_ = kPublication;
  note_failure(reason);
  throw LogError(path_.string(), failed_write_, failure_);
}

void RunLog::finalize_by_destructor(const RunLogEnd& end) noexcept {
  // Best effort by contract (Simulation's destructor): there is no caller to tell. After an
  // earlier failure nothing more is written and no marker is set.
  if (closed_) {
    return;
  }
  closed_ = true;
  if (failed()) {
    return;
  }
  try {
    finish_file(end, Marker::kDestructor);
  } catch (const std::exception& error) {
    note_failure(error.what());
    abandon();
  } catch (...) {
    note_failure("unknown exception");
    abandon();
  }
}

void RunLog::close_after_log_halt() noexcept {
  // Codex review of M6 PR 2, F1: a full ring halts kLog in Simulation without a write failure, so
  // finalize_by_destructor()'s failure guard would not stop it. HDF5's own close of the handles
  // still writes its cached metadata (the certified structure); ASTRA writes nothing more.
  if (closed_) {
    return;
  }
  closed_ = true;
  abandon();
}

LogDiscardResult RunLog::discard() {
  if (discarded_.has_value()) {
    return *discarded_;
  }
  if (!closed_) {
    closed_ = true;
    abandon();  // no tail, no marker: the file is being deleted
  }
  LogDiscardResult result = checked_unlink(published_ ? name_ : partial_name_, kept_.get());
  discarded_ = result;
  return result;
}

void RunLog::discard_after_failed_init() noexcept {
  abandon();
  closed_ = true;
  try {
    static_cast<void>(checked_unlink(partial_name_, kept_.get()));
  } catch (...) {
    // Only an allocation failure while building the result can land here: the delete itself
    // either ran or was refused. Recorded; the failing init reports its own error.
    note_failure("the init-failure delete could not report its result");
  }
}

LogDiscardResult RunLog::checked_unlink(const std::string& name, int reference) const {
  LogDiscardResult result;
  result.path = display(name);
  std::string detail;
  switch (identify(directory_.get(), name, reference, result.path, detail)) {
    case Identity::kMissing:
      result.outcome = LogDiscardOutcome::kMissing;
      result.detail = detail + "; nothing was deleted";
      return result;
    case Identity::kOther:
      result.outcome = LogDiscardOutcome::kMismatch;
      result.detail = detail + ", not this run's file; nothing was deleted";
      return result;
    case Identity::kError:
      result.outcome = LogDiscardOutcome::kFailed;
      result.detail = detail + "; nothing was deleted";
      return result;
    case Identity::kSame:
      break;
  }
  if (::unlinkat(directory_.get(), name.c_str(), 0) != 0) {
    result.outcome = LogDiscardOutcome::kFailed;
    result.detail = "unlinkat of '" + result.path + "' failed: " + errno_text(errno);
    return result;
  }
  result.outcome = LogDiscardOutcome::kDeleted;
  return result;
}

void RunLog::write_writes() {
  std::vector<hdf5::RecordField> fields;
  fields.push_back({"frame", write_frames_});
  fields.push_back({"name", write_names_});
  fields.push_back({"value", write_values_});
  open_handle(file_, path_).write_records("writes", fields);
}

void RunLog::write_summary(const RunLogEnd& end) {
  hdf5::Group summary = open_handle(file_, path_).create_group("summary");
  summary.set_attribute("frames_run", end.frame);
  summary.set_attribute("halt_check", health_check_name(end.halt.check));
  summary.set_attribute("halt_source", end.halt.source);
  summary.set_attribute("halt_frame", end.halt.frame);
  summary.set_attribute("halt_vehicle", static_cast<std::uint64_t>(end.halt.vehicle_index));
  for (std::size_t i = 0; i < health_names_.size(); ++i) {
    summary.set_attribute(health_names_[i], *health_sources_[i]);
  }
  const std::size_t chunk = std::max<std::size_t>(1, signal_names_.size());
  hdf5::Dataset minimum = summary.create_series("min", hdf5::ElementType::kFloat64, chunk);
  minimum.append(std::span<const double>{minimum_});
  minimum.set_attribute("names", signal_names_);
  minimum.close();
  hdf5::Dataset maximum = summary.create_series("max", hdf5::ElementType::kFloat64, chunk);
  maximum.append(std::span<const double>{maximum_});
  maximum.set_attribute("names", signal_names_);
  maximum.close();
  summary.close();
}

void RunLog::write_final_state(const RunLogEnd& end) {
  hdf5::Group final_state = open_handle(file_, path_).create_group("final_state");
  final_state.set_attribute("frame", end.frame);
  final_state.set_attribute("after_halt", std::int64_t{end.halt ? 1 : 0});
  for (const Vehicle& vehicle : end.vehicles) {
    const std::span<const double> state = vehicle.combined_state();
    hdf5::Dataset dataset = final_state.create_series(vehicle.name(), hdf5::ElementType::kFloat64,
                                                      std::max<std::size_t>(1, state.size()));
    dataset.append(state);
    std::vector<std::string> names(state.size());
    for (const StateName& entry : state_names_) {
      if (entry.vehicle == vehicle.index() && entry.offset < names.size()) {
        names[entry.offset] = entry.name;
      }
    }
    dataset.set_attribute("names", names);
    dataset.close();
  }
  final_state.close();
}

void RunLog::close_ring_handles() {
  for (Ring& ring : rings_) {
    open_handle(ring.frame_dataset, path_).close();
    open_handle(ring.values_dataset, path_).close();
    open_handle(ring.group, path_).close();
  }
}

void RunLog::abandon() noexcept {
  // After a real failure the handles are dropped unclosed (hdf5::Object::abandon(): libhdf5
  // 1.10.10 corrupts its state on a failed close). After an injected one (tests) the file itself
  // is sound and closes normally.
  const bool leave_open = failed() && !injected_fired_;
  const auto drop = [leave_open](auto& handle) {
    if (leave_open && handle.has_value()) {
      handle->abandon();
    }
    handle.reset();
  };
  for (Ring& ring : rings_) {
    drop(ring.frame_dataset);
    drop(ring.values_dataset);
    drop(ring.group);
  }
  drop(file_);
}

void RunLog::note_failure(std::string_view message) noexcept {
  if (failed()) {
    return;  // the first failure is the one every later call reports
  }
  try {
    failed_write_ = writing_;
    failure_ = message;
  } catch (...) {
    failure_ = "failed";  // short enough for the small-string buffer: no allocation
  }
  if (failure_.empty()) {
    failure_ = "failed";
  }
}

void RunLog::kill_point(KillPoint point, std::uint32_t divisor) noexcept {
  if (kill_.kill != point || (kill_.kill_divisor != 0 && divisor != kill_.kill_divisor)) {
    return;
  }
  if (++kill_seen_ == kill_.kill_occurrence) {
    static_cast<void>(std::raise(SIGKILL));
  }
}

void RunLog::check_injected(Fault fault) {
  if (injected_ == fault && fault != Fault::kNone) {
    injected_fired_ = true;
    throw Hdf5Error(path_.string(), writing_, "injected failure (test)");
  }
}

}  // namespace astra
