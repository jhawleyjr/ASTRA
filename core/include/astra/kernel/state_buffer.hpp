// The combined and stage state buffers (ADR-0021, docs/plans/pr4a-dynamics-model.md): a runtime-
// sized, contiguous run of doubles, sized exactly once at construction. `Vehicle` owns two:
// `combined_` (the actual state advanced frame to frame -- 13 rigid-body doubles plus every
// state-owning model's own slice) and `stage_` (refreshed every RK4 sub-stage from that stage's own
// trial state, the address `bind_state()`/`SignalRegistry::add()` pointers are resolved into at
// init and read from for the object's whole lifetime).
//
// Non-copyable, move-constructible only (Vehicle itself is moved into Simulation::vehicles_ after
// init() runs, once bind_state()/SignalRegistry pointers already point into stage_ -- a
// std::vector<double>'s move constructor transfers the same heap allocation into fresh, not-yet-
// pointed-into storage, so data() is address-stable across that move). No move-assignment operator:
// nothing in this design ever needs to move-assign one StateBuffer over an already-bound one, which
// (unlike construction into fresh storage) could invalidate every pointer already resolved into the
// assigned-to object. No resize()/push_back()/insert() anywhere in the public interface, so "never
// resized after construction" (R2) is enforced by what the type can do, not by discipline.
#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <vector>

namespace astra {

class StateBuffer {
 public:
  explicit StateBuffer(std::size_t size) : data_(size, 0.0) {}

  StateBuffer(const StateBuffer&) = delete;
  StateBuffer& operator=(const StateBuffer&) = delete;
  StateBuffer(StateBuffer&&) = default;
  StateBuffer& operator=(StateBuffer&&) = delete;
  ~StateBuffer() = default;

  [[nodiscard]] double* data() noexcept { return data_.data(); }
  [[nodiscard]] const double* data() const noexcept { return data_.data(); }
  [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }

  // Overwrites this buffer's own storage, element by element, with other's -- never a resize or a
  // reallocation (asserts equal size; both sides are always sized identically by construction).
  // Deliberately not operator=: the standard does not guarantee std::vector::operator= reuses
  // existing capacity even when sizes already match, and a reallocation here would silently
  // invalidate every bind_state()/SignalRegistry pointer already resolved into this buffer.
  void copy_from(const StateBuffer& other) noexcept {
    assert(other.size() == size() && "StateBuffer::copy_from requires equal size, never a resize");
    std::copy_n(other.data(), size(), data());
  }

 private:
  std::vector<double> data_;
};

}  // namespace astra
