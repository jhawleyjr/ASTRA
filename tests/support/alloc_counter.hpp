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
#pragma once

#include <cstddef>

namespace astra_test {

// The number of times a global allocating operator new (any form) has been called since the
// process started. Monotonic, no reset: a test only ever needs the delta across one sim.run().
[[nodiscard]] std::size_t allocation_count() noexcept;

}  // namespace astra_test
