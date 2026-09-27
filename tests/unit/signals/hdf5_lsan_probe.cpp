// Hdf5LsanProbe (tests/unit/CMakeLists.txt, sanitizer presets only): settles LeakSanitizer against
// the uninstrumented libhdf5 (plan m5-tooling.md, PR 2) with no suppression file. Both modes use
// the wrapper the way the run log will (create, attributes, appends, close, reopen, read strings
// back) and then return from main, so HDF5's atexit teardown and LSan's exit-time scan both run.
//   clean  must exit 0 with no LeakSanitizer report: HDF5's global allocations are freed first.
//   leak   additionally leaks one block and must be reported: the positive control showing that
//          leak detection is live in this process, so "clean" passing means something.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "astra/signals/hdf5_file.hpp"

int main(int argc, char** argv) {
  const std::vector<std::string_view> args(argv, argv + argc);
  if (args.size() != 3 || (args[1] != "clean" && args[1] != "leak")) {
    std::fputs("usage: astra_hdf5_lsan_probe clean|leak <file.h5>\n", stderr);
    return 2;
  }
  const std::filesystem::path path{args[2]};
  std::filesystem::remove(path);
  {
    using astra::hdf5::ElementType;
    astra::hdf5::File file = astra::hdf5::File::create(path);
    file.set_attribute("config", "sim:\n  dt: 0.001\n");
    astra::hdf5::Group rate = file.create_group("rate_1");
    astra::hdf5::Dataset values = rate.create_table("values", ElementType::kFloat64, 2, 4);
    values.set_attribute("names", std::vector<std::string>{"a", "b"});
    values.append(std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0, 6.0});
    values.close();
    rate.close();
    file.close();
  }
  {
    const astra::hdf5::File file = astra::hdf5::File::open_read_only(path);
    const std::vector<std::string> names =
        file.open_group("rate_1").open_dataset("values").attribute_strings("names");
    if (names.size() != 2 || file.attribute_string("config").empty()) {
      std::fputs("astra_hdf5_lsan_probe: read-back mismatch\n", stderr);
      return 1;
    }
  }
  std::filesystem::remove(path);
  // Deliberate: the positive control. The only pointer is dropped, so LSan must report it (the
  // analyzer reports it too, at the return).
  // NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks)
  if (args[1] == "leak") {
    static_cast<void>(new std::uint64_t[8]);
  }
  return 0;
  // NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)
}
