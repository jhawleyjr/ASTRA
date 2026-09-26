// The integer frame counter that is the single source of truth for time (docs/architecture.md
// §5, ADR-0010): t = frame * dt is computed on every call, never accumulated, so it cannot drift
// and a log timestamp is always exact. Only the scheduler advances it.
#pragma once

#include <cmath>
#include <cstdint>

#include "astra/errors.hpp"
#include "astra/interfaces/model.hpp"
#include "astra/kernel/integrator.hpp"  // kDefaultDt: the same 1 kHz major frame Integrator defaults to

namespace astra {

class VirtualClock {
 public:
  // Rejects a non-finite or non-positive dt at construction, matching Integrator's constructor
  // (both take a config-sourced step size and must fail loudly on a bad one, not later).
  explicit VirtualClock(double dt = kDefaultDt) : dt_(dt) {
    if (!std::isfinite(dt) || dt <= 0.0) {
      throw Error("VirtualClock: dt must be finite and positive");
    }
  }

  [[nodiscard]] double dt() const noexcept { return dt_; }
  [[nodiscard]] std::uint64_t frame() const noexcept { return frame_; }
  // t = frame * dt, derived on every call (ADR-0010): never accumulated, so it cannot drift.
  [[nodiscard]] double t() const noexcept { return static_cast<double>(frame_) * dt_; }

  [[nodiscard]] StepContext context() const noexcept {
    return StepContext{.t = t(), .dt = dt_, .frame = frame_};
  }

  void advance() noexcept { ++frame_; }
  void reset() noexcept { frame_ = 0; }
  // Test-only (M4 PR 3): lets a test reach the lockstep sequence-range boundary (ADR-0022,
  // frame < UINT32_MAX - 1) without stepping four billion frames. Reached only through
  // Simulation::preset_frame_for_testing; nothing in production calls it.
  void set_frame_for_testing(std::uint64_t frame) noexcept { frame_ = frame; }

 private:
  double dt_;
  std::uint64_t frame_ = 0;
};

}  // namespace astra
