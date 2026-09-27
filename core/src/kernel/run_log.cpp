#include "astra/kernel/run_log.hpp"

#include <algorithm>
#include <cmath>
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
  // ADR-0025 Q5: never overwritten. A dangling symlink counts as existing too. A status that
  // cannot be read is left to the create in the constructor, which then fails naming log.path.
  std::error_code error;
  const std::filesystem::file_type type =
      std::filesystem::symlink_status(settings.path, error).type();
  if (type != std::filesystem::file_type::not_found && type != std::filesystem::file_type::none) {
    throw ConfigError(config.origin(), "log.path",
                      "'" + path + "' already exists; a run log never overwrites a file");
  }
  return settings;
}

RunLog::RunLog(const RunLogSettings& settings, const SignalRegistry& registry,
               std::span<const Vehicle> vehicles, const RunLogProvenance& provenance)
    : path_(settings.path),
      origin_(settings.origin),
      capacity_(settings.buffer_frames),
      high_water_(std::max<std::size_t>(1, settings.buffer_frames / 2)) {
  try {
    build_rings(registry, vehicles);
  } catch (const std::bad_alloc&) {
    throw ConfigError(origin_, "log.buffer_frames", "the log's rings cannot be allocated");
  } catch (const std::length_error&) {
    throw ConfigError(origin_, "log.buffer_frames", "the log's rings cannot be allocated");
  }
  // Hashed before the file exists, so a plugin that cannot be read leaves no file behind.
  const std::vector<hdf5::RecordField> plugins = plugin_records(provenance.plugins);
  try {
    file_.emplace(hdf5::File::create(path_));
  } catch (const Hdf5Error& error) {
    throw ConfigError(origin_, "log.path", error.what());
  }
  try {
    write_provenance(provenance);
    file_->write_records("plugins", plugins);
  } catch (const Hdf5Error& error) {
    note_failure(error.what());
    discard();
    throw ConfigError(origin_, "log.path",
                      std::string{"cannot write the log's provenance: "} + error.what());
  }
}

RunLog::~RunLog() = default;

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
  hdf5::File& file = *file_;
  file.set_attribute("log_format_version", kFormatVersion);
  file.set_attribute("config", std::string_view{provenance.config_text});
  file.set_attribute("git_describe", std::string_view{git_describe()});
  file.set_attribute("core_version", std::string_view{core_version()});
  file.set_attribute("core_api_version", static_cast<std::uint64_t>(core_api_version()));
  file.set_attribute("dt", provenance.dt);
  for (Ring& ring : rings_) {
    ring.group.emplace(file.create_group(rate_group_name(ring.divisor)));
    ring.frame_dataset.emplace(
        ring.group->create_series("frame", hdf5::ElementType::kUint64, kBlockRows));
    ring.values_dataset.emplace(
        ring.group->create_table("values", hdf5::ElementType::kFloat64, ring.columns, kBlockRows));
    std::vector<std::string> names;
    std::vector<std::string> units;
    names.reserve(ring.columns);
    units.reserve(ring.columns);
    for (const std::size_t index : ring.signal_indices) {
      names.push_back(signal_names_[index]);
      units.push_back(signal_units_[index]);
    }
    ring.values_dataset->set_attribute("names", names);
    ring.values_dataset->set_attribute("units", units);
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

void RunLog::write_block(Ring& ring) {
  writing_ = ring.write_label;
  check_injected(Fault::kDrainWrite);
  ring.values_dataset->append(
      std::span<const double>{ring.block_values.data(), ring.block_count * ring.columns});
  ring.frame_dataset->append(
      std::span<const std::uint64_t>{ring.block_frames.data(), ring.block_count});
  ring.block_count = 0;
}

void RunLog::record_write(std::uint64_t frame, std::string_view name, double value) {
  write_frames_.push_back(frame);
  write_names_.emplace_back(name);
  write_values_.push_back(value);
}

void RunLog::close(const RunLogEnd& end) {
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
    move_rows_to_blocks();
    for (Ring& ring : rings_) {
      if (ring.block_count != 0) {
        write_block(ring);
      }
    }
    writing_ = "/writes";
    check_injected(Fault::kWrites);
    write_writes();
    writing_ = "/summary";
    check_injected(Fault::kSummary);
    write_summary(end);
    writing_ = "/final_state";
    check_injected(Fault::kFinalState);
    write_final_state(end);
    // Everything else is on disk before complete is set, so a file that carries complete = 1
    // holds the whole run.
    writing_ = "the flush before complete";
    file_->flush();
    writing_ = "complete";
    check_injected(Fault::kComplete);
    file_->set_attribute("complete", std::int64_t{1});
    writing_ = "the file's close";
    close_handles();
  } catch (const std::exception& error) {
    note_failure(error.what());
    abandon();
    throw LogError(path_.string(), failed_write_, failure_);
  }
}

void RunLog::close_best_effort(const RunLogEnd& end) noexcept {
  try {
    close(end);
  } catch (
      ...) {  // NOLINT(bugprone-empty-catch): the destructor's close is best effort by contract
  }
}

void RunLog::discard() noexcept {
  abandon();
  closed_ = true;
  std::error_code ignored;
  std::filesystem::remove(path_, ignored);
}

void RunLog::write_writes() {
  std::vector<hdf5::RecordField> fields;
  fields.push_back({"frame", write_frames_});
  fields.push_back({"name", write_names_});
  fields.push_back({"value", write_values_});
  file_->write_records("writes", fields);
}

void RunLog::write_summary(const RunLogEnd& end) {
  hdf5::Group summary = file_->create_group("summary");
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
  hdf5::Group final_state = file_->create_group("final_state");
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

void RunLog::close_handles() {
  for (Ring& ring : rings_) {
    ring.frame_dataset->close();
    ring.values_dataset->close();
    ring.group->close();
  }
  file_->close();
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

void RunLog::check_injected(Fault fault) {
  if (injected_ == fault) {
    injected_fired_ = true;
    throw Hdf5Error(path_.string(), writing_, "injected failure (test)");
  }
}

}  // namespace astra
