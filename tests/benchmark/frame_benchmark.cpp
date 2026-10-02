// M6 PR 9 (docs/plans/m6-pr9-benchmark.md; M6 plan R19, Q11, C-19, N-05): the frame-time benchmark.
//
// Two measurements, each validated before anything is recorded:
// - The cost of one step(), for each named config (D1): a separate untimed warm-up instance first,
//   then a fresh instance from frame 0 with one steady-clock sample around exactly one step() per
//   frame. After each sample, outside the timed window, the step must not have halted and the clock
//   must have advanced to that frame, so every frame timed is a frame that ran (C-19).
// - A logged run end to end (N-05, R19b's five cases, D2): from after from_config to after
//   close_log(), so the drains and flushes outside step() are counted. Each repeat's file is then
//   checked (published, complete, frames_run) and deleted with discard_log() (ADR-0027 item 8).
// The JSON (D7) is written once, at the end, only if every check passed (D6): exit 1 on a halt or a
// failed check, 64 on a usage error, 74 when the output cannot be written.
//
// The steady clock is read here, in tests/, which is outside the wall-clock scan's scope (ADR-0010,
// ADR-0023; scripts/wallclock_scan.py), like the lockstep timing tests. It never supplies
// simulation time and nothing it measures goes into a run log.
#include <sys/utsname.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>  // mkdtemp
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "astra/build_info.hpp"
#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/kernel/health_monitor.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/signals/hdf5_file.hpp"
#include "tests/benchmark/frame_stats.hpp"

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
static_assert(Clock::is_steady, "the benchmark needs a monotonic clock");

constexpr int kExitFailed = 1;
constexpr int kExitUsage = 64;  // EX_USAGE
constexpr int kExitIo = 74;     // EX_IOERR
constexpr std::uint64_t kWarmupFrames = 1000;
constexpr int kRepeats = 3;
constexpr int kClockPairs = 10000;
constexpr std::string_view kSchema = "astra.frame_benchmark/1";
constexpr std::string_view kModelsAnchor = "\n  models:\n";
constexpr std::string_view kSlowRateEntry =
    "    - { plugin: tests/plugins/slow_rate, divisor: 10 }\n";

// D1: the named configs, relative to the repository root.
constexpr std::string_view kCruise = "aircraft/notional_transport/configs/cruise.yaml";
constexpr std::string_view kCruiseStillAir =
    "aircraft/notional_transport/configs/cruise_still_air.yaml";
constexpr std::string_view kDummy = "tests/configs/dummy_1000.yaml";

// A check that failed: the benchmark records nothing and exits kExitFailed.
class Failure : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class UsageError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// The output could not be written: exit kExitIo.
class OutputError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct Options {
  fs::path out;
  std::optional<std::uint64_t> frames;
  std::vector<std::string> configs;  // as given; empty means D1's set
  // Test-only (--inject-log-fault-for-testing): every logged run's close fails this way, so the
  // smoke can check that a failed repeat leaves no file behind (Codex R-001).
  std::optional<astra::RunLogFault> log_fault;
};

struct StepResult {
  std::string config;
  double dt = 0.0;
  std::uint64_t frames = 0;
  std::uint64_t warmup_frames = 0;
  astra::bench::FrameStats stats;
};

enum class Pattern : std::uint8_t { kRunClose, kStepFlushClose };

struct EndToEndCase {
  std::string name;
  std::string config;  // what the JSON names
  std::string text;    // the config's YAML, before the log block
  Pattern pattern = Pattern::kRunClose;
  bool one_frame = false;  // R19b's fixed open-and-close cost: 1 frame, whatever --frames says
};

struct EndToEndResult {
  std::string name;
  std::string config;
  Pattern pattern = Pattern::kRunClose;
  std::uint64_t frames = 0;
  std::vector<double> seconds;
  double median = 0.0;
};

struct HostInfo {
  std::string cpu_model = "unknown";
  unsigned logical_cpus = 0;
  std::string governor = "unknown";
  std::string kernel = "unknown";
};

std::string_view pattern_name(Pattern pattern) {
  return pattern == Pattern::kRunClose ? "run_close" : "step_flush_close";
}

double seconds(Clock::duration d) { return std::chrono::duration<double>(d).count(); }

std::string read_text(const fs::path& path) {
  std::ifstream in(path);
  if (!in) {
    throw Failure("cannot read " + path.string());
  }
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::string trimmed_first_line(const fs::path& path) {
  std::ifstream in(path);
  std::string line;
  if (!in || !std::getline(in, line)) {
    return "";
  }
  const auto end = line.find_last_not_of(" \t\r\n");
  return end == std::string::npos ? "" : line.substr(0, end + 1);
}

// The repository-relative path for D1's names, and the path as given otherwise.
fs::path resolve(const std::string& config) {
  const fs::path given{config};
  return given.is_absolute() || fs::exists(given) ? given : fs::path{ASTRA_SOURCE_DIR} / given;
}

std::uint64_t parse_frames(const std::string& text) {
  std::uint64_t value = 0;
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc{} || end != text.data() + text.size() || value == 0) {
    throw UsageError("--frames needs a positive integer, not '" + text + "'");
  }
  return value;
}

astra::RunLogFault parse_log_fault(const std::string& text) {
  if (text == "rename") {
    return astra::RunLogFault::kRenameUnsupported;
  }
  if (text == "summary") {
    return astra::RunLogFault::kSummary;
  }
  throw UsageError("--inject-log-fault-for-testing takes rename or summary, not '" + text + "'");
}

Options parse_options(int argc, char** argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);
  Options options;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& flag = args[i];
    if (flag != "--out" && flag != "--frames" && flag != "--config" &&
        flag != "--inject-log-fault-for-testing") {
      throw UsageError("unknown argument '" + flag + "'");
    }
    if (i + 1 >= args.size()) {
      throw UsageError(flag + " needs a value");
    }
    const std::string& value = args[++i];
    if (flag == "--out") {
      options.out = value;
    } else if (flag == "--frames") {
      options.frames = parse_frames(value);
    } else if (flag == "--inject-log-fault-for-testing") {
      options.log_fault = parse_log_fault(value);
    } else {
      options.configs.push_back(value);
    }
  }
  if (options.out.empty()) {
    throw UsageError("--out is required");
  }
  return options;
}

astra::Simulation build(const std::string& text, const std::string& origin) {
  return astra::Simulation::from_config(astra::Config::from_string(text, origin),
                                        ASTRA_TEST_PLUGIN_DIR);
}

[[noreturn]] void halted(const std::string& what, const astra::HaltReason& halt) {
  throw Failure(what + ": halted at frame " + std::to_string(halt.frame) + ", check " +
                std::string{astra::health_check_name(halt.check)} + ", source " +
                std::string{halt.source});
}

void expect_frame(const astra::Simulation& sim, std::uint64_t expected, const std::string& what) {
  if (sim.clock().frame() != expected) {
    throw Failure(what + ": the clock is at frame " + std::to_string(sim.clock().frame()) +
                  ", not " + std::to_string(expected));
  }
}

// D4: an untimed warm-up on its own instance, so the process (pages, plugin symbols, caches) is
// warm and the measured instance still starts at frame 0.
std::uint64_t warm_up(const std::string& text, const std::string& config, std::uint64_t frames) {
  const std::uint64_t warmup = std::min(frames, kWarmupFrames);
  astra::Simulation sim = build(text, config);
  const astra::HaltReason halt = sim.run(warmup);
  if (halt) {
    halted(config + " (warm-up)", halt);
  }
  expect_frame(sim, warmup, config + " (warm-up)");
  return warmup;
}

StepResult time_steps(const std::string& config, const std::optional<std::uint64_t>& override) {
  const std::string text = read_text(resolve(config));
  astra::Simulation sim = build(text, config);
  if (sim.has_log()) {
    throw Failure(config +
                  ": has a log block; step() is timed without one (a ring nothing drains halts)");
  }
  StepResult result;
  result.config = config;
  result.dt = sim.clock().dt();
  result.frames = override.value_or(sim.frames());
  if (result.frames == 0) {
    throw Failure(config + ": sim.frames is 0; nothing to time");
  }
  result.warmup_frames = warm_up(text, config, result.frames);
  expect_frame(sim, 0, config);

  std::vector<Clock::duration> ticks(result.frames);
  for (std::uint64_t i = 0; i < result.frames; ++i) {
    const Clock::time_point start = Clock::now();
    const astra::HaltReason halt = sim.step();
    const Clock::time_point stop = Clock::now();
    ticks[i] = stop - start;
    if (halt) {
      halted(config, halt);
    }
    expect_frame(sim, i + 1, config);
  }
  std::vector<double> samples(ticks.size());
  std::ranges::transform(ticks, samples.begin(), seconds);
  result.stats = astra::bench::summarize(samples, result.dt);
  return result;
}

// D2: cruise with tests/plugins/slow_rate as one more model, inserted after the one models: line.
std::string with_slow_rate(std::string text) {
  const std::size_t at = text.find(kModelsAnchor);
  if (at == std::string::npos || text.find(kModelsAnchor, at + 1) != std::string::npos) {
    throw Failure(std::string{kCruise} +
                  ": the vehicle.models line is not there exactly once; slow_rate cannot be added");
  }
  text.insert(at + kModelsAnchor.size(), kSlowRateEntry);
  return text;
}

// The published file must be complete with frames_run = frames, and its .partial gone.
void check_published(const fs::path& path, std::uint64_t frames, const std::string& what) {
  if (!fs::exists(path) || fs::exists(fs::path{path.string() + ".partial"})) {
    throw Failure(what + ": close_log() did not publish " + path.string());
  }
  const astra::hdf5::File file = astra::hdf5::File::open_read_only(path);
  if (file.attribute_int64("complete") != 1) {
    throw Failure(what + ": " + path.string() + " is not marked complete");
  }
  const std::uint64_t frames_run = file.open_group("summary").attribute_uint64("frames_run");
  if (frames_run != frames) {
    throw Failure(what + ": frames_run is " + std::to_string(frames_run) + ", not " +
                  std::to_string(frames));
  }
}

// Drives one logged run; returns the halt, if any. Inside the timed window.
astra::HaltReason drive(astra::Simulation& sim, Pattern pattern, std::uint64_t frames) {
  if (pattern == Pattern::kRunClose) {
    return sim.run(frames);
  }
  for (std::uint64_t i = 0; i < frames; ++i) {
    if (const astra::HaltReason halt = sim.step(); halt) {
      return halt;
    }
    if (const astra::HaltReason halt = sim.flush_log(); halt) {
      return halt;
    }
  }
  return {};
}

// After a failed repeat, while its Simulation is alive: deletes the run's own file through
// discard_log() (ADR-0027 item 8), which leaves alone any name that no longer holds this run's
// file. What it could not delete is reported, never removed by name. Never throws: the original
// failure is the one reported.
void discard_after_failure(astra::Simulation& sim, const std::string& what) noexcept {
  try {
    const astra::LogDiscardResult discarded = sim.discard_log();
    if (!discarded.deleted() && discarded.outcome != astra::LogDiscardOutcome::kMissing) {
      std::cerr << "astra_frame_benchmark: " << what << ": left " << discarded.path << ": "
                << discarded.detail << "\n";
    }
  } catch (const std::exception& e) {
    std::cerr << "astra_frame_benchmark: " << what << ": discard_log() threw: " << e.what() << "\n";
  }
}

double time_one_repeat(const EndToEndCase& c, std::uint64_t frames, const fs::path& path,
                       const std::optional<astra::RunLogFault>& fault) {
  const std::string what = c.name + " (" + path.filename().string() + ")";
  // A refused construction leaves no file of this run's to delete (ADR-0027 item 8).
  astra::Simulation sim = build(c.text + "\nlog:\n  path: " + path.string() + "\n", c.config);
  if (fault) {
    sim.inject_log_fault_for_testing(*fault);
  }
  try {
    const Clock::time_point start = Clock::now();
    const astra::HaltReason halt = drive(sim, c.pattern, frames);
    if (!halt) {
      sim.close_log();
    }
    const Clock::time_point stop = Clock::now();
    if (halt) {
      halted(what, halt);
    }
    expect_frame(sim, frames, what);
    check_published(path, frames, what);
    const astra::LogDiscardResult discarded = sim.discard_log();
    if (!discarded.deleted()) {
      throw Failure(what + ": discard_log() did not delete " + discarded.path + ": " +
                    discarded.detail);
    }
    return seconds(stop - start);
  } catch (const Failure&) {
    discard_after_failure(sim, what);
    throw;
  } catch (const std::exception& e) {  // a LogError from close_log(), an Hdf5Error from the check
    discard_after_failure(sim, what);
    throw Failure(what + ": " + e.what());
  }
}

EndToEndResult time_end_to_end(const EndToEndCase& c, std::uint64_t frames, const fs::path& dir,
                               const std::optional<astra::RunLogFault>& fault) {
  EndToEndResult result;
  result.name = c.name;
  result.config = c.config;
  result.pattern = c.pattern;
  result.frames = frames;
  for (int repeat = 0; repeat < kRepeats; ++repeat) {
    const fs::path path = dir / (c.name + "_" + std::to_string(repeat) + ".h5");
    result.seconds.push_back(time_one_repeat(c, frames, path, fault));
  }
  result.median = astra::bench::median(result.seconds);
  return result;
}

std::vector<EndToEndCase> end_to_end_cases() {
  const std::string cruise = read_text(resolve(std::string{kCruise}));
  const std::string slow = with_slow_rate(cruise);
  const std::string cruise_name{kCruise};
  const std::string slow_name = cruise_name + " + tests/plugins/slow_rate (divisor 10)";
  return {
      {"cruise_1", cruise_name, cruise, Pattern::kRunClose, true},
      {"cruise_n", cruise_name, cruise, Pattern::kRunClose, false},
      {"cruise_slow_rate_1", slow_name, slow, Pattern::kRunClose, true},
      {"cruise_slow_rate_n", slow_name, slow, Pattern::kRunClose, false},
      {"cruise_n_step_flush", cruise_name, cruise, Pattern::kStepFlushClose, false},
  };
}

// A fresh directory of the benchmark's own for its log files (under TMPDIR), removed on every way
// out, success or failure, but only if it is empty: a file discard_log() would not delete stays.
class LogDir {
 public:
  LogDir() {
    std::string pattern = (fs::temp_directory_path() / "astra_frame_benchmark_XXXXXX").string();
    if (::mkdtemp(pattern.data()) == nullptr) {
      throw Failure("cannot create a directory under " + fs::temp_directory_path().string());
    }
    path_ = pattern;
  }
  ~LogDir() {
    std::error_code ignored;
    fs::remove(path_, ignored);  // fails, harmlessly, on a non-empty directory
  }
  LogDir(const LogDir&) = delete;
  LogDir& operator=(const LogDir&) = delete;
  LogDir(LogDir&&) = delete;
  LogDir& operator=(LogDir&&) = delete;

  [[nodiscard]] const fs::path& path() const noexcept { return path_; }

 private:
  fs::path path_;
};

double clock_overhead() {
  std::vector<double> pairs(kClockPairs);
  for (double& pair : pairs) {
    const Clock::time_point a = Clock::now();
    const Clock::time_point b = Clock::now();
    pair = seconds(b - a);
  }
  return astra::bench::median(pairs);
}

std::vector<double> load_average() {
  std::istringstream fields(trimmed_first_line("/proc/loadavg"));
  std::vector<double> load(3);
  for (double& value : load) {
    if (!(fields >> value)) {
      throw Failure("cannot read /proc/loadavg");
    }
  }
  return load;
}

HostInfo host_info() {
  HostInfo host;
  std::ifstream cpuinfo("/proc/cpuinfo");
  for (std::string line; std::getline(cpuinfo, line);) {
    if (line.starts_with("model name")) {
      const auto colon = line.find(':');
      const auto value =
          colon == std::string::npos ? colon : line.find_first_not_of(' ', colon + 1);
      if (value != std::string::npos) {
        host.cpu_model = line.substr(value);
      }
      break;
    }
  }
  host.logical_cpus = std::thread::hardware_concurrency();
  if (std::string governor =
          trimmed_first_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
      !governor.empty()) {
    host.governor = std::move(governor);
  }
  utsname name{};
  if (::uname(&name) == 0) {
    host.kernel = name.release;
  }
  return host;
}

// --- JSON (D7) ---

using astra::bench::json_number;
using astra::bench::json_string;

std::string json_array(const std::vector<double>& values) {
  std::string out = "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    out += (i == 0 ? "" : ", ") + json_number(values[i]);
  }
  return out + "]";
}

std::string json_count(std::uint64_t value) { return std::to_string(value); }

// `"name": `, the start of one member.
std::string key(std::string_view name) { return json_string(name) + ": "; }

std::string step_json(const StepResult& r) {
  const astra::bench::FrameStats& s = r.stats;
  std::ostringstream o;
  o << "    {" << key("config") << json_string(r.config) << ", " << key("dt") << json_number(r.dt)
    << ", " << key("frames") << json_count(r.frames) << ", " << key("warmup_frames")
    << json_count(r.warmup_frames) << ", " << key("samples") << json_count(s.count) << ",\n     "
    << key("mean") << json_number(s.mean) << ", " << key("median") << json_number(s.median) << ", "
    << key("p99") << json_number(s.p99) << ", " << key("max") << json_number(s.max) << ", "
    << key("first_frame") << json_number(s.first) << ",\n     " << key("frames_per_second")
    << json_number(s.frames_per_second) << ", " << key("margin_p99") << json_number(s.margin_p99)
    << ", " << key("margin_max") << json_number(s.margin_max) << "}";
  return o.str();
}

std::string end_to_end_json(const EndToEndResult& r) {
  std::ostringstream o;
  o << "    {" << key("case") << json_string(r.name) << ", " << key("config")
    << json_string(r.config) << ", " << key("pattern") << json_string(pattern_name(r.pattern))
    << ", " << key("frames") << json_count(r.frames) << ",\n     " << key("repeats")
    << json_count(r.seconds.size()) << ", " << key("seconds") << json_array(r.seconds) << ", "
    << key("median") << json_number(r.median) << ", " << key("frames_per_second")
    << json_number(static_cast<double>(r.frames) / r.median) << "}";
  return o.str();
}

template <typename T, typename F>
std::string json_list(const std::vector<T>& items, F to_json) {
  std::string out = "[\n";
  for (std::size_t i = 0; i < items.size(); ++i) {
    out += to_json(items[i]) + (i + 1 < items.size() ? ",\n" : "\n");
  }
  return out + "  ]";
}

struct Report {
  HostInfo host;
  std::vector<double> load_start;
  std::vector<double> load_end;
  double clock_overhead = 0.0;
  std::optional<std::uint64_t> frames_override;
  std::vector<StepResult> steps;
  std::vector<EndToEndResult> end_to_end;
};

std::string report_json(const Report& r) {
  const double period =
      static_cast<double>(Clock::period::num) / static_cast<double>(Clock::period::den);
  std::ostringstream o;
  o << "{\n  " << key("schema") << json_string(kSchema) << ",\n  " << key("units") << "{"
    << key("time") << json_string("s") << ", " << key("rate") << json_string("1/s") << ", "
    << key("margin") << json_string("fraction of dt") << "},\n  " << key("build") << "{"
    << key("type") << json_string(ASTRA_BENCHMARK_BUILD_TYPE) << ", " << key("built_with")
    << json_string(astra::built_with()) << ", " << key("compiler") << json_string(__VERSION__)
    << ", " << key("git_describe") << json_string(astra::git_describe()) << ", "
    << key("core_version") << json_string(astra::core_version()) << "},\n  " << key("host") << "{"
    << key("cpu_model") << json_string(r.host.cpu_model) << ", " << key("logical_cpus")
    << json_count(r.host.logical_cpus) << ", " << key("governor") << json_string(r.host.governor)
    << ", " << key("kernel") << json_string(r.host.kernel) << ", " << key("loadavg_start")
    << json_array(r.load_start) << ", " << key("loadavg_end") << json_array(r.load_end) << "},\n  "
    << key("clock") << "{" << key("name") << json_string("std::chrono::steady_clock") << ", "
    << key("period") << json_number(period) << ", " << key("overhead")
    << json_number(r.clock_overhead) << "},\n  " << key("frames_source")
    << json_string(r.frames_override ? "override" : "config") << ",\n  " << key("step")
    << json_list(r.steps, step_json) << ",\n  " << key("end_to_end")
    << json_list(r.end_to_end, end_to_end_json) << "\n}\n";
  return o.str();
}

void write_report(const fs::path& out, const std::string& json) {
  std::ofstream file(out, std::ios::trunc);
  file << json;
  file.close();
  if (!file) {
    throw OutputError("cannot write " + out.string());
  }
}

Report measure(const Options& options) {
  Report report;
  report.host = host_info();
  report.load_start = load_average();
  report.frames_override = options.frames;
  report.clock_overhead = clock_overhead();

  std::vector<std::string> configs = options.configs;
  if (configs.empty()) {
    configs = {std::string{kCruise}, std::string{kCruiseStillAir}, std::string{kDummy}};
  }
  for (const std::string& config : configs) {
    report.steps.push_back(time_steps(config, options.frames));
    const auto& s = report.steps.back().stats;
    std::cout << "step " << config << ": " << s.count << " frames, median " << s.median * 1e6
              << " us, p99 " << s.p99 * 1e6 << " us, max " << s.max * 1e6 << " us\n";
  }

  // N defaults to cruise's own sim.frames, as the step phase's frames do.
  const std::uint64_t n =
      options.frames.value_or(build(read_text(resolve(std::string{kCruise})), "cruise").frames());
  const LogDir dir;
  for (const EndToEndCase& c : end_to_end_cases()) {
    report.end_to_end.push_back(
        time_end_to_end(c, c.one_frame ? 1 : n, dir.path(), options.log_fault));
    const EndToEndResult& r = report.end_to_end.back();
    std::cout << "end_to_end " << r.name << ": " << r.frames << " frames, median of "
              << r.seconds.size() << " " << r.median * 1e3 << " ms\n";
  }
  report.load_end = load_average();
  return report;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  try {
    options = parse_options(argc, argv);
  } catch (const UsageError& e) {
    std::cerr << "astra_frame_benchmark: " << e.what()
              << "\nusage: astra_frame_benchmark --out FILE.json [--frames N] [--config PATH]...\n";
    return kExitUsage;
  }
  try {
    const Report report = measure(options);
    write_report(options.out, report_json(report));
  } catch (const OutputError& e) {
    std::cerr << "astra_frame_benchmark: " << e.what() << "\n";
    return kExitIo;
  } catch (const std::exception& e) {
    std::cerr << "astra_frame_benchmark: " << e.what() << "\nnothing was recorded\n";
    return kExitFailed;
  }
  std::cout << "wrote " << options.out.string() << "\n";
  return 0;
}
