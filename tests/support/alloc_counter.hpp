// Counts every heap allocation for the life of the process, by replacing the global operator new /
// operator delete set (docs/architecture.md §14/§15: "any heap allocation in the per-frame path
// fails the test suite"). alloc_counter.cpp replaces the WHOLE set -- plain, nothrow, array, sized,
// and aligned forms -- together: replacing only some of them lets a call resolve to a mismatched
// pair (this translation unit's operator delete freeing memory ASan's own operator new returned, or
// vice versa), which -fsanitize=address reports as alloc-dealloc-mismatch. ASan's own replacements
// are weak symbols, so this TU's definitions win at link time while every allocation still goes
// through std::malloc/std::free underneath -- ASan's interceptor, and therefore its own checking,
// stays in the loop. Linked into astra_physics_tests only (tests/physics/CMakeLists.txt); the unit
// suite does not link it.
//
// Known scope limit (Codex adversarial review, M1 PR 4): this counts C++ operator new/delete only,
// not a direct std::malloc/calloc/realloc/aligned_alloc call from a future model or plugin -- such
// a call would allocate without incrementing allocation_count(), so a passing no_alloc_test.cpp
// does not prove the absence of THAT specific pattern. No model or plugin in this build calls the
// raw C allocator family directly today (grepped clean at review time). Deliberately not closed by
// also overriding malloc/calloc/free here: those symbols are exactly what -fsanitize=address's own
// runtime intercepts, so redefining them in a process ASan already instruments risks fighting its
// allocator rather than extending it, for a pattern nothing in this codebase currently uses.
// Revisit if a plugin ever needs a C allocator directly.
#pragma once

#include <cstddef>

namespace astra_test {

// The number of times a global allocating operator new (any form) has been called since the
// process started. Monotonic, no reset: a test only ever needs the delta across one sim.run().
[[nodiscard]] std::size_t allocation_count() noexcept;

}  // namespace astra_test
