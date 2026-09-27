// core/util/sha256 (M5 PR 4, ADR-0025 Q9: the /plugins table's library digests) against the
// published FIPS 180-2 / NIST CSRC SHA-256 example vectors, which are independent of this code.
// Suite name "RunLogSha256" so the PR 4 acceptance selector (-R RunLog) runs it.
#include "astra/util/sha256.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

#include "astra/errors.hpp"

namespace {

namespace sha256 = astra::sha256;

TEST(RunLogSha256, FipsExampleVectors) {
  EXPECT_EQ(sha256::hex_digest(""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(sha256::hex_digest("abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  // 448 bits: the padding needs a second block.
  EXPECT_EQ(sha256::hex_digest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  EXPECT_EQ(sha256::hex_digest("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
                               "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"),
            "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
  EXPECT_EQ(sha256::hex_digest(std::string(1000000, 'a')),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(RunLogSha256, IncrementalUpdatesAcrossBlockBoundariesMatchOneShot) {
  const std::string text(1000000, 'a');
  for (const std::size_t piece : {1U, 3U, 55U, 63U, 64U, 65U, 4097U}) {
    sha256::Hasher hasher;
    for (std::size_t offset = 0; offset < text.size(); offset += piece) {
      hasher.update(std::string_view{text}.substr(offset, piece));
    }
    EXPECT_EQ(sha256::to_hex(hasher.finish()),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0")
        << "piece " << piece;
  }
}

TEST(RunLogSha256, FileDigestHashesTheBytesAndAMissingFileThrowsNamingIt) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("astra_sha256_" + std::to_string(::getpid()) + ".bin");
  {
    std::ofstream out(path, std::ios::binary);
    out << "abc";
  }
  EXPECT_EQ(sha256::file_hex_digest(path),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  std::filesystem::remove(path);
  try {
    static_cast<void>(sha256::file_hex_digest(path));
    FAIL() << "expected astra::Error";
  } catch (const astra::Error& error) {
    EXPECT_NE(std::string{error.what()}.find(path.string()), std::string::npos) << error.what();
  }
}

}  // namespace
