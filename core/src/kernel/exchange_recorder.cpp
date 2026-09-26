#include "astra/kernel/exchange_recorder.hpp"

#include <algorithm>
#include <cstring>

namespace astra {

namespace {

// Appends the raw bytes of `count` objects starting at `data`.
template <typename T>
void append_bytes(std::vector<std::byte>& out, const T* data, std::size_t count) {
  const auto* const first = reinterpret_cast<const std::byte*>(data);  // NOLINT(*-reinterpret-cast)
  out.insert(out.end(), first, first + (count * sizeof(T)));
}

}  // namespace

ExchangeRecorder::ExchangeRecorder(std::size_t exchange_capacity, std::size_t state_capacity)
    : requests_(exchange_capacity), replies_(exchange_capacity), states_(state_capacity) {}

void ExchangeRecorder::record_request(const astra_sim_to_sw_v1& request) noexcept {
  if (request_count_ < requests_.size()) {
    std::memcpy(&requests_[request_count_], &request, sizeof request);
    ++request_count_;
  }
}

void ExchangeRecorder::record_reply(const astra_sw_to_sim_v1& reply) noexcept {
  if (reply_count_ < replies_.size()) {
    std::memcpy(&replies_[reply_count_], &reply, sizeof reply);
    ++reply_count_;
  }
}

void ExchangeRecorder::record_state(std::uint64_t frame,
                                    std::span<const double> rigid_body) noexcept {
  if (state_count_ < states_.size()) {
    RecordedState& record = states_[state_count_];
    record.frame = frame;
    const std::size_t count = std::min(rigid_body.size(), record.y.size());
    std::copy_n(rigid_body.begin(), count, record.y.begin());
    ++state_count_;
  }
}

std::vector<std::byte> ExchangeRecorder::serialize() const {
  const Header header{.magic = kMagic,
                      .version = kVersion,
                      .request_size = sizeof(astra_sim_to_sw_v1),
                      .reply_size = sizeof(astra_sw_to_sim_v1),
                      .state_size = sizeof(RecordedState),
                      .reserved = 0,
                      .request_count = request_count_,
                      .reply_count = reply_count_,
                      .state_count = state_count_};
  std::vector<std::byte> out;
  out.reserve(sizeof header + (request_count_ * sizeof(astra_sim_to_sw_v1)) +
              (reply_count_ * sizeof(astra_sw_to_sim_v1)) + (state_count_ * sizeof(RecordedState)));
  append_bytes(out, &header, 1);
  append_bytes(out, requests_.data(), request_count_);
  append_bytes(out, replies_.data(), reply_count_);
  append_bytes(out, states_.data(), state_count_);
  return out;
}

}  // namespace astra
