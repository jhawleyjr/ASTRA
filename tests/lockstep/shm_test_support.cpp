// See shm_test_support.hpp.
#include "tests/lockstep/shm_test_support.hpp"

#include <fcntl.h>
#include <linux/futex.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#include "astra/config/config.hpp"
#include "icd/astra_icd.h"

extern char** environ;  // NOLINT(readability-redundant-declaration): POSIX, for posix_spawn

namespace shm_test {
namespace {

constexpr std::size_t kRegionSize = sizeof(astra_shm_region_v1);
constexpr std::string_view kShmDir = "/dev/shm";
constexpr std::string_view kPrefix = "astra_";
constexpr int kNotifyFd = 3;
constexpr milliseconds kPoll{1};
constexpr milliseconds kTerminateGrace{1000};
constexpr double kFrameDt = 1e-3;
constexpr int kSignalBase = 128;

std::runtime_error sys(std::string_view what) {
  return std::runtime_error(std::string{what} + ": " + std::strerror(errno));
}

timespec relative(milliseconds limit) {
  timespec out{};
  out.tv_sec = static_cast<time_t>(limit.count() / 1000);
  out.tv_nsec = static_cast<long>((limit.count() % 1000) * 1000000);
  return out;
}

}  // namespace

double elapsed_ms(Clock::time_point since) {
  return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
}

std::string unique_segment() {
  static std::atomic<int> counter{0};
  return std::string{kPrefix} + std::to_string(getpid()) + "_t" + std::to_string(counter++);
}

std::uint32_t load(const std::uint32_t& word) { return __atomic_load_n(&word, __ATOMIC_ACQUIRE); }

void store(std::uint32_t& word, std::uint32_t value) {
  __atomic_store_n(&word, value, __ATOMIC_RELEASE);
}

void wake(const std::uint32_t& word) {
  syscall(SYS_futex, &word, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
}

void wait_while_equal(const std::uint32_t& word, std::uint32_t expected, milliseconds limit) {
  const timespec timeout = relative(limit);
  syscall(SYS_futex, &word, FUTEX_WAIT, expected, &timeout, nullptr, 0);
}

bool eventually(const std::function<bool()>& ready, milliseconds limit) {
  const Clock::time_point end = Clock::now() + limit;
  while (!ready()) {
    if (Clock::now() >= end) {
      return ready();
    }
    std::this_thread::sleep_for(kPoll);
  }
  return true;
}

// ---------------------------------------------------------------------------------------------

std::unique_ptr<Mapping> Mapping::attach(const std::string& segment, milliseconds limit) {
  const std::string path = "/" + segment;
  const Clock::time_point end = Clock::now() + limit;
  for (;;) {
    const int fd = shm_open(path.c_str(), O_RDWR, 0);
    if (fd >= 0) {
      struct stat info {};
      const bool sized = fstat(fd, &info) == 0 && info.st_size >= static_cast<off_t>(kRegionSize);
      if (sized) {
        void* mapped = mmap(nullptr, kRegionSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (mapped == MAP_FAILED) {
          throw sys("mmap");
        }
        return std::unique_ptr<Mapping>(new Mapping(static_cast<astra_shm_region_v1*>(mapped)));
      }
      close(fd);
    }
    if (Clock::now() >= end) {
      return nullptr;
    }
    std::this_thread::sleep_for(kPoll);
  }
}

Mapping::~Mapping() { munmap(region_, kRegionSize); }

std::unique_ptr<Mapping> raw_peer_handshake(const std::string& segment, milliseconds limit) {
  const Clock::time_point end = Clock::now() + limit;
  std::unique_ptr<Mapping> peer = Mapping::attach(segment, limit);
  if (!peer) {
    return nullptr;
  }
  while (load(peer->header().sim_state) != ASTRA_SHM_SIM_READY) {
    if (Clock::now() >= end) {
      return nullptr;
    }
    wait_while_equal(peer->header().sim_state, ASTRA_SHM_SIM_INIT, kPoll);
  }
  peer->header().peer_pid = static_cast<std::int32_t>(getpid());
  store(peer->header().peer_state, ASTRA_SHM_PEER_READY);
  wake(peer->header().peer_state);
  return peer;
}

void raw_peer_reply(Mapping& peer, std::uint32_t seq) {
  astra_sw_to_sim_v1 reply{};
  reply.magic = ASTRA_ICD_MAGIC;
  reply.version = ASTRA_ICD_VERSION;
  reply.frame = peer.region().request.frame;
  std::memcpy(&peer.region().reply, &reply, sizeof reply);
  store(peer.header().ack_seq, seq);
  wake(peer.header().ack_seq);
}

// ---------------------------------------------------------------------------------------------

Creator::Creator(std::string segment) : segment_(std::move(segment)) {
  fd_ = shm_open(("/" + segment_).c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
  if (fd_ < 0) {
    throw sys("Creator shm_open");
  }
}

Creator::~Creator() {
  if (region_ != nullptr) {
    munmap(region_, kRegionSize);
  }
  if (fd_ >= 0) {
    close(fd_);
  }
  shm_unlink(("/" + segment_).c_str());
}

void Creator::size() {
  if (ftruncate(fd_, static_cast<off_t>(kRegionSize)) != 0) {
    throw sys("Creator ftruncate");
  }
  void* mapped = mmap(nullptr, kRegionSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
  if (mapped == MAP_FAILED) {
    throw sys("Creator mmap");
  }
  region_ = static_cast<astra_shm_region_v1*>(mapped);
}

void Creator::init_header() {
  astra_shm_header_v1& h = header();
  h.magic = ASTRA_SHM_MAGIC;
  h.layout_version = ASTRA_SHM_LAYOUT_VERSION;
  h.layout_size = static_cast<std::uint32_t>(kRegionSize);
  h.icd_version = ASTRA_ICD_VERSION;
  h.req_seq = ASTRA_SHM_SEQ_INIT;
  h.ack_seq = ASTRA_SHM_SEQ_INIT;
  h.sim_pid = static_cast<std::int32_t>(getpid());
}

void Creator::ready() {
  store(header().sim_state, ASTRA_SHM_SIM_READY);
  wake(header().sim_state);
}

bool Creator::wait_peer_ready(milliseconds limit) {
  return eventually([&] { return load(header().peer_state) == ASTRA_SHM_PEER_READY; }, limit);
}

void Creator::publish(std::uint64_t frame) {
  astra_sim_to_sw_v1 request{};
  request.magic = ASTRA_ICD_MAGIC;
  request.version = ASTRA_ICD_VERSION;
  request.frame = frame;
  request.t = static_cast<double>(frame) * kFrameDt;
  std::memcpy(&region_->request, &request, sizeof request);
  store(header().req_seq, static_cast<std::uint32_t>(frame));
  wake(header().req_seq);
}

void Creator::close_normal() {
  store(header().sim_state, ASTRA_SHM_SIM_CLOSED);
  store(header().req_seq, ASTRA_SHM_CLOSE_SEQ);
  wake(header().req_seq);
  wake(header().sim_state);
}

// ---------------------------------------------------------------------------------------------

std::unique_ptr<LoadedShm> load_shm(const std::string& segment) {
  auto shm = std::make_unique<LoadedShm>();
  shm->plugin = shm->loader.load(kShmPlugin);
  shm->transport = dynamic_cast<astra::Transport*>(shm->plugin.model.get());
  if (shm->transport == nullptr) {
    throw std::runtime_error("the shm plugin does not implement Transport");
  }
  shm->transport->init(astra::Config::from_string("segment: " + segment + "\n", "load_shm"),
                       shm->registry);
  return shm;
}

astra::TransportSettings settings(std::uint32_t timeout_ms, std::uint32_t handshake_timeout_ms) {
  astra::TransportSettings out;
  out.timeout_ms = timeout_ms;
  out.handshake_timeout_ms = handshake_timeout_ms;
  out.peer_timeout_ms = std::max(timeout_ms, out.peer_timeout_ms);
  return out;
}

astra_sim_to_sw_v1 request_for(std::uint64_t frame) {
  astra_sim_to_sw_v1 request{};
  request.magic = ASTRA_ICD_MAGIC;
  request.version = ASTRA_ICD_VERSION;
  request.frame = frame;
  request.t = static_cast<double>(frame) * kFrameDt;
  return request;
}

std::unique_ptr<OpenedShm> open_with_raw_peer(const astra::TransportSettings& transport_settings) {
  auto opened = std::make_unique<OpenedShm>();
  opened->segment = unique_segment();
  opened->shm = load_shm(opened->segment);
  {
    const std::jthread peer(
        [&opened] { opened->peer = raw_peer_handshake(opened->segment, milliseconds{5000}); });
    opened->initial_ack = opened->shm->transport->open(transport_settings);
  }
  if (!opened->peer) {
    throw std::runtime_error("the raw peer did not complete the handshake");
  }
  return opened;
}

Outcome drive_wait(astra::Transport& transport, std::uint32_t last) {
  constexpr double kRunawayMs = 10000.0;
  const Clock::time_point start = Clock::now();
  Outcome outcome;
  for (;;) {
    const astra::AckObservation observed = transport.wait_ack(last);
    if (observed.status != astra::AckStatus::kObserved || observed.ack_seq != last) {
      outcome.status = observed.status;
      outcome.ack = observed.ack_seq;
      outcome.ms = elapsed_ms(start);
      return outcome;
    }
    ++outcome.unchanged;
    if (elapsed_ms(start) > kRunawayMs) {
      outcome.runaway = true;
      outcome.ms = elapsed_ms(start);
      return outcome;
    }
  }
}

ScriptedPeer::ScriptedPeer(std::string segment, Respond respond)
    : thread_([this, name = std::move(segment), fn = std::move(respond)] { run(name, fn); }) {}

ScriptedPeer::~ScriptedPeer() {
  stop_ = true;
  thread_.join();
}

void ScriptedPeer::run(const std::string& segment, const Respond& respond) {
  const std::unique_ptr<Mapping> peer = raw_peer_handshake(segment, milliseconds{5000});
  if (!peer) {
    return;
  }
  handshook_ = true;
  std::uint32_t last = ASTRA_SHM_SEQ_INIT;
  while (!stop_) {
    const std::uint32_t req = load(peer->header().req_seq);
    if (req == last || req == ASTRA_SHM_CLOSE_SEQ) {
      wait_while_equal(peer->header().req_seq, req, kPoll);
      continue;
    }
    last = req;
    respond(*peer, req, stop_);
  }
}

void reply_correctly(Mapping& peer, std::uint32_t seq, const std::atomic<bool>& /*stop*/) {
  raw_peer_reply(peer, seq);
}

// ---------------------------------------------------------------------------------------------

Child::Child(const std::vector<std::string>& args, bool notify) {
  std::array<int, 2> out{-1, -1};
  std::array<int, 2> note{-1, -1};
  if (pipe2(out.data(), O_CLOEXEC) != 0 || (notify && pipe2(note.data(), O_CLOEXEC) != 0)) {
    throw sys("pipe2");
  }
  std::vector<std::string> full{ASTRA_ECHO_PEER_PATH};
  full.insert(full.end(), args.begin(), args.end());
  if (notify) {
    full.emplace_back("--notify-fd");
    full.emplace_back(std::to_string(kNotifyFd));
  }
  std::vector<char*> argv;
  argv.reserve(full.size() + 1);
  for (std::string& arg : full) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);

  posix_spawn_file_actions_t actions{};
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);  // dup2 clears CLOEXEC
  if (notify) {
    posix_spawn_file_actions_adddup2(&actions, note[1], kNotifyFd);
  }
  const int rc = posix_spawn(&pid_, argv[0], &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  close(out[1]);
  stdout_fd_ = out[0];
  if (notify) {
    close(note[1]);
    notify_fd_ = note[0];
  }
  if (rc != 0) {
    pid_ = -1;
    throw std::runtime_error(std::string{"posix_spawn: "} + std::strerror(rc));
  }
}

Child::~Child() {
  if (pid_ > 0 && !status_) {
    kill(pid_, SIGTERM);
    if (!wait_exit(kTerminateGrace)) {
      kill(pid_, SIGKILL);
      int raw = 0;
      waitpid(pid_, &raw, 0);
    }
  }
  if (notify_fd_ >= 0) {
    close(notify_fd_);
  }
  if (stdout_fd_ >= 0) {
    close(stdout_fd_);
  }
}

bool Child::wait_notify(milliseconds limit) const {
  const Clock::time_point end = Clock::now() + limit;
  for (;;) {
    char byte = 0;
    const int flags = fcntl(notify_fd_, F_GETFL);
    fcntl(notify_fd_, F_SETFL, flags | O_NONBLOCK);
    const ssize_t got = read(notify_fd_, &byte, 1);
    if (got == 1) {
      return true;
    }
    if (got == 0 || Clock::now() >= end) {
      return false;  // EOF: the child exited without notifying
    }
    std::this_thread::sleep_for(kPoll);
  }
}

std::optional<int> Child::wait_exit(milliseconds limit) {
  if (status_) {
    return status_;
  }
  const Clock::time_point end = Clock::now() + limit;
  for (;;) {
    int raw = 0;
    const pid_t got = waitpid(pid_, &raw, WNOHANG);
    if (got == pid_) {
      status_ = WIFEXITED(raw) ? WEXITSTATUS(raw) : kSignalBase + WTERMSIG(raw);
      return status_;
    }
    if (Clock::now() >= end) {
      return std::nullopt;
    }
    std::this_thread::sleep_for(kPoll);
  }
}

void Child::kill_now() const { kill(pid_, SIGKILL); }

std::string Child::output() const {
  std::string text;
  std::array<char, 256> buffer{};
  for (;;) {
    const ssize_t got = read(stdout_fd_, buffer.data(), buffer.size());
    if (got <= 0) {
      return text;
    }
    text.append(buffer.data(), static_cast<std::size_t>(got));
  }
}

std::string sim_yaml(const SimSpec& spec) {
  std::ostringstream out;
  out << "sim:\n  dt: 0.001\n  frames: " << spec.frames
      << "\nvehicle:\n  models:\n    - { plugin: tests/plugins/input_consumer }\n"
         "  initial_controls:\n    controls:\n      elevator: 0.0\n"
         "physical:\n"
         "  earth: plugins/physical/earth/flat\n"
         "  gravity: plugins/physical/gravity/constant\n"
         "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, "
         "izz: 1.0 }\n"
         "initial:\n  state: { p_n: [0.0, 0.0, -100.0], v_b: [60.0, 0.0, 0.0] }\n"
         "transport:\n  plugin: "
      << kShmPlugin << "\n";
  if (spec.with_segment_key) {
    out << "  segment: " << spec.segment << "\n";
  }
  out << "  divisor: " << spec.divisor << "\n  timeout_ms: " << spec.timeout_ms
      << "\n  handshake_timeout_ms: " << spec.handshake_timeout_ms
      << "\n  peer_timeout_ms: " << spec.peer_timeout_ms
      << "\n  commands: { elevator: controls.elevator }\n  limits: " << spec.limits << "\n";
  return out.str();
}

astra::Simulation build_sim(const SimSpec& spec) {
  return astra::Simulation::from_config(astra::Config::from_string(sim_yaml(spec), "shm test"),
                                        ASTRA_TEST_PLUGIN_DIR);
}

std::vector<std::string> peer_args(const std::string& segment, std::uint32_t handshake_ms,
                                   std::uint32_t peer_ms, const std::vector<std::string>& extra) {
  std::vector<std::string> args{"--segment",
                                segment,
                                "--handshake-timeout-ms",
                                std::to_string(handshake_ms),
                                "--peer-timeout-ms",
                                std::to_string(peer_ms)};
  args.insert(args.end(), extra.begin(), extra.end());
  return args;
}

// ---------------------------------------------------------------------------------------------

void sweep_dead_creators() {
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(kShmDir, error)) {
    const std::string name = entry.path().filename().string();
    if (!name.starts_with(kPrefix)) {
      continue;
    }
    const std::size_t end = name.find('_', kPrefix.size());
    if (end == std::string::npos) {
      continue;
    }
    const std::string digits = name.substr(kPrefix.size(), end - kPrefix.size());
    if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos) {
      continue;
    }
    const auto pid = static_cast<pid_t>(std::stol(digits));
    if (kill(pid, 0) == -1 && errno == ESRCH) {
      shm_unlink(("/" + name).c_str());
    }
  }
}

std::vector<std::string> own_segments() {
  const std::string mine = std::string{kPrefix} + std::to_string(getpid()) + "_";
  std::vector<std::string> found;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(kShmDir, error)) {
    const std::string name = entry.path().filename().string();
    if (name.starts_with(mine)) {
      found.push_back(name);
    }
  }
  return found;
}

void LockstepTest::SetUp() {
  reset_hooks();
  sweep_dead_creators();
}

void LockstepTest::TearDown() {
  reset_hooks();
  const std::vector<std::string> left = own_segments();
  EXPECT_TRUE(left.empty()) << "orphaned segment: " << (left.empty() ? "" : left.front());
  for (const std::string& name : left) {
    shm_unlink(("/" + name).c_str());
  }
  errno = 0;
  EXPECT_EQ(waitpid(-1, nullptr, WNOHANG), -1) << "an unreaped child process";
  EXPECT_EQ(errno, ECHILD);
}

// ---------------------------------------------------------------------------------------------

std::size_t offset_of_req_seq() { return offsetof(astra_shm_header_v1, req_seq); }
std::size_t offset_of_sim_state() { return offsetof(astra_shm_header_v1, sim_state); }

Hooks& hooks() {
  static Hooks instance;
  return instance;
}

void reset_hooks() {
  Hooks& h = hooks();
  h.before_wait = nullptr;
  h.after_wait = nullptr;
  h.wait_calls = 0;
  const std::lock_guard lock(h.mutex);
  h.watched.clear();
  h.observed_thread = std::thread::id{};
  h.fstat_sizes.clear();
  h.mmaps = 0;
  h.mmap_while_short = false;
  h.waited_on_sim_state = false;
}

}  // namespace shm_test
