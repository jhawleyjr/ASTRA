// SHA-256 (FIPS 180-4), in-house so the run log's /plugins table (ADR-0025) can pin each loaded
// plugin library without a new dependency. Checked against the FIPS 180-2 / NIST CSRC example
// vectors (tests/unit/util/sha256_test.cpp). Init-time only: nothing here is on the per-frame path.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace astra::sha256 {

// NOLINTBEGIN(readability-magic-numbers): 32-byte digest, 8-word state and 64-byte block are
// FIPS 180-4's fixed SHA-256 sizes.
using Digest = std::array<std::uint8_t, 32>;

// Incremental hashing: update() any number of times, then finish() once.
class Hasher {
 public:
  Hasher() noexcept;
  void update(std::span<const std::byte> data) noexcept;
  void update(std::string_view text) noexcept;
  [[nodiscard]] Digest finish() noexcept;

 private:
  void compress(const std::uint8_t* block) noexcept;

  std::array<std::uint32_t, 8> state_;
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffered_ = 0;
  std::uint64_t length_bytes_ = 0;
};
// NOLINTEND(readability-magic-numbers)

// Lower-case hexadecimal, 64 characters.
[[nodiscard]] std::string to_hex(const Digest& digest);
[[nodiscard]] std::string hex_digest(std::string_view text);
// The digest of a file's bytes; throws Error naming the path if it cannot be read.
[[nodiscard]] std::string file_hex_digest(const std::filesystem::path& path);

}  // namespace astra::sha256
