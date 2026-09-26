// M4's log (ADR-0022, docs/architecture.md §10 "Exchange recorder"; M4 PR 3,
// docs/plans/m4-pr3-core-seam.md): a preallocated in-memory recorder with three streams -- every
// astra_sim_to_sw_v1 request, every astra_sw_to_sim_v1 reply (all 64 bytes), and the bound
// vehicle's rigid-body state at every major frame -- serialized after the run behind a fixed
// header, so a byte comparison of two runs covers counts and layout, not only payloads. M5's HDF5
// logger supersedes it.
//
// Capacity is fixed at construction and never grows: recording is on the per-frame path (no
// allocation), and architecture §15 makes log overflow a halt ("a silently truncated log is not a
// log"). The owner (LockstepExchange::pre_frame) checks can_record() before a frame runs and halts
// when the frame's records would not fit, so the record_* calls below never see a full stream.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

#include "astra/kernel/state.hpp"
#include "icd/astra_icd.h"

namespace astra {

// One state-stream record: the frame the state belongs to, then the 13 rigid-body doubles in
// ADR-0003 order. 8 + 13 * 8 = 112 bytes, no padding.
struct RecordedState {
  std::uint64_t frame;
  std::array<double, kStateSize> y;
};
static_assert(sizeof(RecordedState) ==
                  sizeof(std::uint64_t) + (static_cast<std::size_t>(kStateSize) * sizeof(double)),
              "recorder state layout changed: bump ExchangeRecorder::kVersion");

class ExchangeRecorder {
 public:
  // "ASXR", read the same way as ASTRA_ICD_MAGIC's "ASTR".
  static constexpr std::uint32_t kMagic = 0x41535852U;
  static constexpr std::uint32_t kVersion = 1;

  // The serialized header: magic, version, the three record sizes (request, reply, state), a
  // reserved zero word, then the three record counts. 48 bytes, native endian (same build, same
  // machine only; ADR-0022 Determinism). The three streams follow in that order.
  struct Header {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t request_size;
    std::uint32_t reply_size;
    std::uint32_t state_size;
    std::uint32_t reserved;
    std::uint64_t request_count;
    std::uint64_t reply_count;
    std::uint64_t state_count;
  };
  // No padding anywhere: every byte of a serialized header is a field's byte.
  static_assert(std::has_unique_object_representations_v<Header>,
                "recorder header layout changed: bump kVersion");

  // Init-time: preallocates (and zeroes) every record slot. May allocate/throw.
  ExchangeRecorder(std::size_t exchange_capacity, std::size_t state_capacity);

  // Whether `exchanges` more request/reply pairs and `states` more state records fit.
  [[nodiscard]] bool can_record(std::size_t exchanges, std::size_t states) const noexcept {
    return request_count_ + exchanges <= requests_.size() &&
           reply_count_ + exchanges <= replies_.size() && state_count_ + states <= states_.size();
  }

  // Per-frame path: no allocation, no exceptions. Preconditions: can_record() said the record
  // fits (a call on a full stream is ignored rather than writing out of bounds; the owner's
  // capacity halt makes that unreachable).
  void record_request(const astra_sim_to_sw_v1& request) noexcept;
  void record_reply(const astra_sw_to_sim_v1& reply) noexcept;
  void record_state(std::uint64_t frame, std::span<const double> rigid_body) noexcept;

  [[nodiscard]] std::span<const astra_sim_to_sw_v1> requests() const noexcept {
    return {requests_.data(), request_count_};
  }
  [[nodiscard]] std::span<const astra_sw_to_sim_v1> replies() const noexcept {
    return {replies_.data(), reply_count_};
  }
  [[nodiscard]] std::span<const RecordedState> states() const noexcept {
    return {states_.data(), state_count_};
  }

  // The header followed by the three streams, as bytes. Allocates; not on the per-frame path.
  [[nodiscard]] std::vector<std::byte> serialize() const;

 private:
  std::vector<astra_sim_to_sw_v1> requests_;
  std::vector<astra_sw_to_sim_v1> replies_;
  std::vector<RecordedState> states_;
  std::size_t request_count_ = 0;
  std::size_t reply_count_ = 0;
  std::size_t state_count_ = 0;
};

}  // namespace astra
