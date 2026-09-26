// M4 PR 4 (docs/plans/m4-pr4-shm-transport.md; M4 Q14's mitigation): 1e6 lockstep exchanges between
// the real shm plugin (driven exactly as core's receive loop drives it) and the echo peer process,
// Release only (label stress, RUN_SERIAL). Every variable request field carries a distinct
// frame-dependent pattern and every reply is compared byte-for-byte with one built independently
// here (critique F-6), so a stale or torn payload in either direction fails. It reports the
// measured per-exchange cost (mean, p50, p99, max); the cost is reported, not gated.
//
// Detection, not proof: ASan cannot see futex memory-order bugs and TSan cannot follow
// cross-process synchronization (accepted gap, M4 Q14).
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "embedded/hal/astra_hal.h"
#include "tests/lockstep/shm_test_support.hpp"

namespace {

using astra::AckStatus;
using astra::CloseReason;
using shm_test::milliseconds;

constexpr std::uint64_t kExchanges = 1000000;
constexpr std::size_t kDoubles = 22;  // t .. az: the contiguous doubles of astra_sim_to_sw_v1
constexpr std::uint32_t kFnvOffset = 2166136261U;
constexpr std::uint32_t kFnvPrime = 16777619U;
constexpr std::uint32_t kStatusMix = 2654435761U;

static_assert(offsetof(astra_sim_to_sw_v1, t) + kDoubles * sizeof(double) ==
                  offsetof(astra_sim_to_sw_v1, status_flags),
              "t .. az are 22 contiguous doubles");

class EchoPeerStress : public shm_test::LockstepTest {};

void fill_request(std::uint64_t frame, astra_sim_to_sw_v1& request) {
  request = astra_sim_to_sw_v1{};
  request.magic = ASTRA_ICD_MAGIC;
  request.version = ASTRA_ICD_VERSION;
  request.frame = frame;
  std::array<double, kDoubles> values{};
  for (std::size_t i = 0; i < kDoubles; ++i) {
    values[i] =
        static_cast<double>(frame) * static_cast<double>(i + 1) + static_cast<double>(i) / 64.0;
  }
  std::memcpy(&request.t, values.data(), sizeof values);
  request.status_flags = static_cast<std::uint32_t>(frame) * kStatusMix;
}

std::uint32_t fnv1a(const void* data, std::size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  std::uint32_t hash = kFnvOffset;
  for (std::size_t i = 0; i < size; ++i) {
    hash = (hash ^ bytes[i]) * kFnvPrime;
  }
  return hash;
}

// The echo peer's documented echo mode, rebuilt here independently of its source.
astra_sw_to_sim_v1 expected_reply(const astra_sim_to_sw_v1& request) {
  astra_sw_to_sim_v1 reply{};
  reply.magic = ASTRA_ICD_MAGIC;
  reply.version = ASTRA_ICD_VERSION;
  reply.frame = request.frame;
  reply.elevator = request.phi;
  reply.aileron = request.theta;
  reply.rudder = request.psi;
  reply.throttle[0] = request.p;
  reply.throttle[1] = request.q;
  reply.mode_flags = fnv1a(&request, sizeof request);
  reply.fault_flags = static_cast<std::uint32_t>(request.frame) ^ request.status_flags;
  return reply;
}

TEST_F(EchoPeerStress, MillionExchangesEchoPayloadAndReportCost) {
  const std::string segment = shm_test::unique_segment();
  shm_test::Child peer(shm_test::peer_args(segment, 5000, 5000, {"--mode", "echo"}), false);
  auto shm = shm_test::load_shm(segment);
  astra::Transport& transport = *shm->transport;
  std::uint32_t last = transport.open(shm_test::settings(1000, 5000));

  std::vector<double> micros(kExchanges);
  std::uint64_t timeouts = 0;
  std::uint64_t wrong_ack = 0;
  std::uint64_t wrong_reply = 0;
  astra_sim_to_sw_v1 request{};
  astra_sw_to_sim_v1 reply{};
  const auto total_start = shm_test::Clock::now();
  for (std::uint64_t frame = 0; frame < kExchanges; ++frame) {
    fill_request(frame, request);
    const auto seq = static_cast<std::uint32_t>(frame);
    const auto start = shm_test::Clock::now();
    transport.publish(request, seq);
    astra::AckObservation observed{};
    do {  // core's receive loop: unchanged means still waiting
      observed = transport.wait_ack(last);
    } while (observed.status == AckStatus::kObserved && observed.ack_seq == last);
    if (observed.status != AckStatus::kObserved) {
      ++timeouts;
      break;
    }
    if (observed.ack_seq != seq) {
      ++wrong_ack;
      break;
    }
    transport.read_reply(reply);
    micros[frame] =
        std::chrono::duration<double, std::micro>(shm_test::Clock::now() - start).count();
    last = seq;
    const astra_sw_to_sim_v1 expected = expected_reply(request);
    wrong_reply += shm_test::same_bytes(reply, expected) ? 0 : 1;
  }
  const double total_s =
      std::chrono::duration<double>(shm_test::Clock::now() - total_start).count();
  transport.close(CloseReason::kNormal);

  EXPECT_EQ(timeouts, 0U);
  EXPECT_EQ(wrong_ack, 0U);
  EXPECT_EQ(wrong_reply, 0U) << "every reply byte-equal to the independently built one";
  EXPECT_EQ(peer.wait_exit(milliseconds{5000}), 0);
  EXPECT_NE(peer.output().find("echo_peer: handled 1000000 status 1"), std::string::npos);

  std::vector<double> sorted = micros;
  std::sort(sorted.begin(), sorted.end());
  double sum = 0.0;
  for (const double value : micros) {
    sum += value;
  }
  const double mean = sum / static_cast<double>(kExchanges);
  const double p50 = sorted[kExchanges / 2];
  const double p99 = sorted[(kExchanges * 99) / 100];
  const double max = sorted.back();
  std::printf(
      "EchoPeerStress: %llu exchanges in %.3f s; per exchange (publish to reply copied): "
      "mean %.2f us, p50 %.2f us, p99 %.2f us, max %.2f us\n",
      static_cast<unsigned long long>(kExchanges), total_s, mean, p50, p99, max);
  RecordProperty("exchanges", std::to_string(kExchanges));
  RecordProperty("mean_us", std::to_string(mean));
  RecordProperty("p50_us", std::to_string(p50));
  RecordProperty("p99_us", std::to_string(p99));
  RecordProperty("max_us", std::to_string(max));
}

}  // namespace
