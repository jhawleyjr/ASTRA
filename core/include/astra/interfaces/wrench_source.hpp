// A vehicle model that contributes to the applied wrench at the CG (docs/architecture.md §5, §7):
// AeroModel and PropulsionModel (M3) derive from this so Vehicle can find every such model at init
// and sum its contribution each frame. PLAN.md M1 PR 4 needs this now: §15's "NaN/Inf in ... any
// wrench contribution, with the offending plugin named" has no per-contribution data to check
// without a seam like this one -- Eom::set_applied_wrench existed with no caller before this PR.
#pragma once

#include "astra/interfaces/model.hpp"
#include "astra/interfaces/wrench.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/signals/input_registry.hpp"

namespace astra {

class SignalRegistry;  // core/include/astra/signals/signal_registry.hpp

class WrenchSource : public Model {
 public:
  // Defined out of line in libastra_core; same key-function reasoning as EarthModel (see
  // core/include/astra/interfaces/earth_model.hpp and core/src/interfaces/model.cpp): without it
  // each plugin's -fvisibility=hidden translation unit would carry its own private vtable/typeinfo,
  // breaking dynamic_cast<WrenchSource*> across the .so boundary.
  ~WrenchSource() override;

  // This model's contribution to the vehicle's applied wrench (about the CG, body axes).
  //
  // PLAN.md M2 PR 3c (docs/plans/pr3c-substage-wrench.md): called once per RK4 sub-stage --
  // FOUR TIMES per major frame, not once -- each time with that stage's own vs
  // (Vehicle::integrate's derivative functor), never the frame-start state alone. A conforming
  // implementation MUST therefore be a pure function of (vs, env) with no hidden per-call state:
  // its return value may depend only on the arguments given, never on how many times or in what
  // order it has previously been called this frame or any other. (Before PR 3c this was called once
  // per frame at the frame-start state; nesc_aero and nan_wrench, this tree's only two
  // implementations, already satisfy the stronger contract above, which is why lifting them to
  // 4x/frame needed no change to either -- PR 3c's own plan critique found this contract had never
  // actually been written down until now, only true by accident of there being nothing that could
  // violate it yet.)
  //
  // ADR-0017 amendment: a write-seam member (bound via bind_inputs() below) is exempt from "never
  // on how many times or in what order it has previously been called this frame or any other" in
  // exactly one respect -- its value MAY differ between frames (InputRegistry::latch() commits at
  // most once per frame, before the first RK4 stage runs), but a conforming implementation must
  // still see the IDENTICAL value across all four of one frame's calls, exactly like every other
  // member. The other direction is not relaxed at all: a conforming wrench_b() may READ any input
  // through its own const double*, but must NEVER WRITE through an InputHandle -- from wrench_b()
  // or from reset() -- since either would make this call's own output depend on a hidden mutation
  // no caller asked for, the exact class of hidden per-call state this contract already forbids.
  // step() is the one sanctioned write point (a future M4 ActuatorModel's use case).
  // tests/plugins/input_consumer is a labeled, test-only exception to the wrench_b() half of this
  // rule (its own header comment says so), the same "test instrumentation only" exception
  // tests/plugins/nan_wrench's own mutable call_count_ already documents for a const wrench_b().
  //
  // env's wind_n is frame-latched regardless of how many times this is called: Simulation::step
  // samples it once per frame (before the wrench stage, docs/architecture.md §5) and every RK4
  // sub-stage's env carries the identical value -- a WrenchSource never samples wind itself and
  // cannot observe it changing within a frame. env's atmosphere, by contrast, IS a fresh sample of
  // the stage's own state as of PR 3c: EnvironmentSample's own header has the full account of which
  // field is which and why. Per-frame path (§14): no allocation, no exceptions, every call.
  [[nodiscard]] virtual Wrench wrench_b(const VehicleState& vs,
                                        const EnvironmentSample& env) const noexcept = 0;

  // Which optional environment inputs this source cannot run without (environment_sample.hpp).
  // Simulation::from_config rejects a config that omits a required slot with a ConfigError naming
  // physical.wind / physical.atmosphere -- never a silent zero wind or a NaN density read by a
  // model that needed a real value. Default: none (a source that reads no environment input, e.g.
  // a constant-coefficient contribution, declares nothing). Defined out of line in
  // wrench_source.cpp, so the destructor stays the key function.
  [[nodiscard]] virtual EnvironmentRequirements requires_environment() const noexcept;

  // ADR-0017: registers this model's writable inputs, e.g. elevator_ = inputs.add(registry,
  // "controls.elevator", "rad"); (elevator_ is a const double* into InputRegistry's own storage,
  // not a plugin-owned member). Called once per WrenchSource, by Vehicle::init()'s existing
  // WrenchSource-discovery pass (the same pass that already checks requires_environment() above),
  // after this model's own Model::init() has already run, and still inside the vehicle's
  // SignalRegistry::PrefixScope, so every name lands correctly namespaced ("v0.controls.elevator").
  // Default: does nothing -- a WrenchSource with no writable input (nesc_aero, nan_wrench) need not
  // override this. Init-time only: may throw. Model::init()'s own signature is unchanged -- this
  // seam attaches only to WrenchSource, never to a shared environment model.
  virtual void bind_inputs(InputRegistry& inputs, SignalRegistry& registry);
};

}  // namespace astra
