#include "astra/util/sha256.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "astra/errors.hpp"

namespace astra::sha256 {

// NOLINTBEGIN(readability-magic-numbers): every literal in this file is a fixed FIPS 180-4 SHA-256
// constant (round constants, initial state, rotation and shift counts, block and length sizes) or a
// byte-packing shift.

namespace {

// FIPS 180-4 §4.2.2: the first 32 bits of the fractional parts of the cube roots of the first 64
// primes.
constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U};

// FIPS 180-4 §5.3.3: the first 32 bits of the fractional parts of the square roots of the first
// eight primes.
constexpr std::array<std::uint32_t, 8> kInitialState = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U,
                                                        0xa54ff53aU, 0x510e527fU, 0x9b05688cU,
                                                        0x1f83d9abU, 0x5be0cd19U};

constexpr std::size_t kBlockBytes = 64;

constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) noexcept {
  return (x >> n) | (x << (32U - n));
}

}  // namespace

Hasher::Hasher() noexcept : state_(kInitialState) {}

void Hasher::compress(const std::uint8_t* block) noexcept {
  std::array<std::uint32_t, 64> w{};
  for (std::size_t t = 0; t < 16; ++t) {
    w[t] = (static_cast<std::uint32_t>(block[4 * t]) << 24U) |
           (static_cast<std::uint32_t>(block[(4 * t) + 1]) << 16U) |
           (static_cast<std::uint32_t>(block[(4 * t) + 2]) << 8U) |
           static_cast<std::uint32_t>(block[(4 * t) + 3]);
  }
  for (std::size_t t = 16; t < 64; ++t) {
    const std::uint32_t s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >> 3U);
    const std::uint32_t s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >> 10U);
    w[t] = w[t - 16] + s0 + w[t - 7] + s1;
  }
  std::uint32_t a = state_[0];
  std::uint32_t b = state_[1];
  std::uint32_t c = state_[2];
  std::uint32_t d = state_[3];
  std::uint32_t e = state_[4];
  std::uint32_t f = state_[5];
  std::uint32_t g = state_[6];
  std::uint32_t h = state_[7];
  for (std::size_t t = 0; t < 64; ++t) {
    const std::uint32_t big_s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const std::uint32_t choose = (e & f) ^ (~e & g);
    const std::uint32_t t1 = h + big_s1 + choose + kRoundConstants[t] + w[t];
    const std::uint32_t big_s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t t2 = big_s0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

void Hasher::update(std::span<const std::byte> data) noexcept {
  length_bytes_ += data.size();
  std::size_t offset = 0;
  while (offset < data.size()) {
    const std::size_t take = std::min(kBlockBytes - buffered_, data.size() - offset);
    std::memcpy(buffer_.data() + buffered_, data.data() + offset, take);
    buffered_ += take;
    offset += take;
    if (buffered_ == kBlockBytes) {
      compress(buffer_.data());
      buffered_ = 0;
    }
  }
}

void Hasher::update(std::string_view text) noexcept { update(std::as_bytes(std::span{text})); }

Digest Hasher::finish() noexcept {
  // FIPS 180-4 §5.1.1: a 1 bit, zeros to 56 mod 64 bytes, then the message length in bits as a
  // big-endian 64-bit integer.
  const std::uint64_t length_bits = length_bytes_ * 8U;
  buffer_[buffered_++] = 0x80U;
  if (buffered_ > kBlockBytes - 8) {
    std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_), buffer_.end(), 0U);
    compress(buffer_.data());
    buffered_ = 0;
  }
  std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_), buffer_.end() - 8, 0U);
  for (std::size_t i = 0; i < 8; ++i) {
    buffer_[kBlockBytes - 1 - i] = static_cast<std::uint8_t>(length_bits >> (8U * i));
  }
  compress(buffer_.data());
  Digest digest{};
  for (std::size_t i = 0; i < state_.size(); ++i) {
    digest[4 * i] = static_cast<std::uint8_t>(state_[i] >> 24U);
    digest[(4 * i) + 1] = static_cast<std::uint8_t>(state_[i] >> 16U);
    digest[(4 * i) + 2] = static_cast<std::uint8_t>(state_[i] >> 8U);
    digest[(4 * i) + 3] = static_cast<std::uint8_t>(state_[i]);
  }
  return digest;
}

std::string to_hex(const Digest& digest) {
  constexpr std::string_view kHex = "0123456789abcdef";
  std::string text;
  text.reserve(2 * digest.size());
  for (const std::uint8_t byte : digest) {
    text.push_back(kHex[byte >> 4U]);
    text.push_back(kHex[byte & 0x0fU]);
  }
  return text;
}

std::string hex_digest(std::string_view text) {
  Hasher hasher;
  hasher.update(text);
  return to_hex(hasher.finish());
}

std::string file_hex_digest(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw Error("sha256: cannot open '" + path.string() + "'");
  }
  Hasher hasher;
  constexpr std::size_t kChunk = 1U << 16U;
  std::vector<char> chunk(kChunk);
  while (in) {
    in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    const std::streamsize got = in.gcount();
    if (got > 0) {
      hasher.update(std::as_bytes(std::span{chunk.data(), static_cast<std::size_t>(got)}));
    }
  }
  if (in.bad()) {
    throw Error("sha256: cannot read '" + path.string() + "'");
  }
  return to_hex(hasher.finish());
}

// NOLINTEND(readability-magic-numbers)

}  // namespace astra::sha256
