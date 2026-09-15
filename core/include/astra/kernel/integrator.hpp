// Integrator over StateVector (ADR-0003/0004): fixed-step RK4 (default) and forward Euler, so a
// test can show RK4 is doing its job. Integrators are part of the kernel's contract with the state
// vector, not plugins (docs/architecture.md §8). Per-frame path: no allocation, no exceptions; the
// derivative functor itself must be noexcept, and step()'s stages live in stack StateVector locals.
// The constructor is the init path (may throw): it rejects a non-finite or non-positive dt and any
// IntegratorKind that isn't one of the named enumerators, so a bad config value fails loudly there
// instead of producing Inf/NaN state or a step() that silently does nothing.
//
// step() does NOT renormalize q_nb. Renormalization is the separate post-step stage architecture.md
// §5 and ADR-0004 put after the integrator in the frame order, so PLAN.md M1 PR 4's health check
// ("|q| drift > 1e-6 before renormalization") can observe the drift before post_step_renormalize
// erases it.
#pragma once

#include <cassert>
#include <cmath>
#include <concepts>
#include <cstdint>

#include "astra/errors.hpp"
#include "astra/kernel/state.hpp"
#include "astra/util/frames.hpp"

namespace astra {

// ADR-0004's default major frame, seconds (1 kHz). Named once so later config defaults point here
// instead of re-deciding it.
inline constexpr double kDefaultDt = 1e-3;

enum class IntegratorKind : std::uint8_t { kEuler, kRk4 };

// Rejects any IntegratorKind value that didn't come from the enumerators above (e.g. a bad
// static_cast from deserialized config), so an invalid kind is caught at init instead of making
// step()'s switch silently do nothing.
[[nodiscard]] constexpr bool is_valid_kind(IntegratorKind kind) noexcept {
  switch (kind) {
    case IntegratorKind::kEuler:
    case IntegratorKind::kRk4:
      return true;
  }
  return false;
}

// f(t, y) -> ydot must return exactly StateVector, not an Eigen expression template that would
// dangle past the call, and must not throw: the per-frame path never does (docs/architecture.md
// §14). This is checked at compile time instead of merely documented.
template <typename F>
concept StateDerivative = requires(const F& f, double t, const StateVector& y) {
  { f(t, y) } noexcept -> std::same_as<StateVector>;
};

class Integrator {
 public:
  explicit Integrator(IntegratorKind kind = IntegratorKind::kRk4, double dt = kDefaultDt)
      : kind_(kind), dt_(dt) {
    if (!std::isfinite(dt) || dt <= 0.0) {
      throw Error("Integrator: dt must be finite and positive");
    }
    if (!is_valid_kind(kind)) {
      throw Error("Integrator: unknown IntegratorKind");
    }
  }

  [[nodiscard]] IntegratorKind kind() const noexcept { return kind_; }
  [[nodiscard]] double dt() const noexcept { return dt_; }

  // Advances y by exactly one major frame. t is the frame's start time, frame * dt (ADR-0010: the
  // integer frame counter is the source of truth; t is derived by the caller, never accumulated
  // here).
  template <StateDerivative F>
  void step(const F& f, double t, StateVector& y) const noexcept {
    assert(is_valid_kind(kind_) && "kind_ was validated in the constructor and never mutated");
    switch (kind_) {
      case IntegratorKind::kEuler:
        y = StateVector(y + dt_ * f(t, y));
        return;
      case IntegratorKind::kRk4: {
        // NOLINTBEGIN(readability-magic-numbers): the classical RK4 tableau, not arbitrary
        // constants.
        const double half_dt = dt_ / 2.0;
        const StateVector k1 = f(t, y);
        const StateVector k2 = f(t + half_dt, StateVector(y + half_dt * k1));
        const StateVector k3 = f(t + half_dt, StateVector(y + half_dt * k2));
        const StateVector k4 = f(t + dt_, StateVector(y + dt_ * k3));
        y = StateVector(y + (dt_ / 6.0) * (k1 + 2.0 * k2 + 2.0 * k3 + k4));
        // NOLINTEND(readability-magic-numbers)
        return;
      }
    }
  }

 private:
  IntegratorKind kind_;
  double dt_;
};

// The post-step stage (docs/architecture.md §5, ADR-0001, ADR-0004): renormalizes q_nb in place.
// Kept out of step() so a health check can inspect |q| drift beforehand (PLAN.md M1 PR 4). Returns
// false (q_nb left unmodified) on a zero or non-finite quaternion; see
// frames::normalize_quaternion.
[[nodiscard]] inline bool post_step_renormalize(StateVector& y) noexcept {
  return frames::normalize_quaternion(y.segment<state::kQuaternionSize>(state::kQuaternion));
}

}  // namespace astra
