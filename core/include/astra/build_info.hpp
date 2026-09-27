// What this libastra_core build is, for the run log's provenance attributes (ADR-0025 Q10). Both
// strings come from a translation unit generated at build time by cmake/astra_build_info.cmake,
// rewritten only when its text changes.
#pragma once

namespace astra {

// `git describe --always --dirty` of the source tree at the last build, or "unknown" for a build
// outside a git checkout. Disclosed limit (ADR-0025): it describes the tree when libastra_core was
// last built, not the files at run time.
[[nodiscard]] const char* git_describe() noexcept;

// The project version (CMake's PROJECT_VERSION, the same string astra._core.VERSION carries).
[[nodiscard]] const char* core_version() noexcept;

}  // namespace astra
