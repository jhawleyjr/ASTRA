// Integrator over a runtime-sized combined state buffer (ADR-0003/0004, ADR-0021): fixed-step RK4
// (default) and forward Euler, so a test can show RK4 is doing its job. Integrators are part of the
// kernel's contract with the state vector, not plugins (docs/architecture.md §8). Per-frame path:
// no allocation, no exceptions; the derivative functor itself must be noexcept.
//
// ADR-0021: the functor contract changes from return-by-value (f(t,y) -> StateVector) to
// write-into-caller-provided-buffer (f(t, y, dy) noexcept), matching Nutaro's own der_func shape
// now applied at the integrator level too, not just the model level (docs/architecture.md §8). The
// constructor takes the combined state's own size and allocates k1..k4 and the RK4 trial-state
// scratch exactly once there (an init-time allocation, not a per-step() one -- R2); step() itself
// never allocates.
//
// step() does NOT run any post-step hook (renormalization or otherwise). That is Vehicle's own job,
// over whichever DynamicsModel(s) declare one -- architecture.md §5 and ADR-0004 put it after the
// integrator in the frame order, so PLAN.md M1 PR 4's health check ("|q| drift > 1e-6 before
// renormalization") can observe the drift before it runs.
#pragma once

#include <cassert>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "astra/errors.hpp"

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

// f(t, y, dy) writes the derivative of the state y (state_count() doubles) into dy (also
// state_count() doubles, caller-owned, never the same address as y); must not throw: the per-frame
// path never does (docs/architecture.md §14). Checked at compile time instead of merely documented.
template <typename F>
concept StateDerivative = requires(const F& f, double t, const double* y, double* dy) {
  { f(t, y, dy) } noexcept -> std::same_as<void>;
};

class Integrator {
 public:
  // state_count is the combined state buffer's own size (13 when no model owns state beyond the
  // rigid body). Allocates every scratch buffer this instance will ever need, once, here.
  explicit Integrator(std::size_t state_count, IntegratorKind kind = IntegratorKind::kRk4,
                      double dt = kDefaultDt)
      : kind_(kind),
        dt_(dt),
        state_count_(state_count),
        k1_(state_count),
        k2_(state_count),
        k3_(state_count),
        k4_(state_count),
        trial_(state_count) {
    if (!std::isfinite(dt) || dt <= 0.0) {
      throw Error("Integrator: dt must be finite and positive");
    }
    if (!is_valid_kind(kind)) {
      throw Error("Integrator: unknown IntegratorKind");
    }
  }

  [[nodiscard]] IntegratorKind kind() const noexcept { return kind_; }
  [[nodiscard]] double dt() const noexcept { return dt_; }
  [[nodiscard]] std::size_t state_count() const noexcept { return state_count_; }

  // Advances y (state_count() doubles, in place) by exactly one major frame. t is the frame's start
  // time, frame * dt (ADR-0010: the integer frame counter is the source of truth; t is derived by
  // the caller, never accumulated here). y's own size must equal state_count(); asserted, never
  // resized.
  template <StateDerivative F>
  void step(const F& f, double t, double* y) const noexcept {
    assert(is_valid_kind(kind_) && "kind_ was validated in the constructor and never mutated");
    switch (kind_) {
      case IntegratorKind::kEuler: {
        f(t, y, k1_.data());
        for (std::size_t i = 0; i < state_count_; ++i) {
          y[i] += dt_ * k1_[i];
        }
        return;
      }
      case IntegratorKind::kRk4: {
        // NOLINTBEGIN(readability-magic-numbers): the classical RK4 tableau, not arbitrary
        // constants.
        const double half_dt = dt_ / 2.0;
        f(t, y, k1_.data());
        for (std::size_t i = 0; i < state_count_; ++i) {
          trial_[i] = y[i] + half_dt * k1_[i];
        }
        f(t + half_dt, trial_.data(), k2_.data());
        for (std::size_t i = 0; i < state_count_; ++i) {
          trial_[i] = y[i] + half_dt * k2_[i];
        }
        f(t + half_dt, trial_.data(), k3_.data());
        for (std::size_t i = 0; i < state_count_; ++i) {
          trial_[i] = y[i] + dt_ * k3_[i];
        }
        f(t + dt_, trial_.data(), k4_.data());
        for (std::size_t i = 0; i < state_count_; ++i) {
          y[i] += (dt_ / 6.0) * (k1_[i] + 2.0 * k2_[i] + 2.0 * k3_[i] + k4_[i]);
        }
        // NOLINTEND(readability-magic-numbers)
        return;
      }
    }
  }

 private:
  IntegratorKind kind_;
  double dt_;
  std::size_t state_count_;
  // Own scratch, sized exactly once at construction (R2): step() itself never allocates. mutable
  // because step() is logically const (it mutates only its own private scratch, not observable
  // state), matching the const Integrator& every caller already holds.
  mutable std::vector<double> k1_;
  mutable std::vector<double> k2_;
  mutable std::vector<double> k3_;
  mutable std::vector<double> k4_;
  mutable std::vector<double> trial_;
};

}  // namespace astra
