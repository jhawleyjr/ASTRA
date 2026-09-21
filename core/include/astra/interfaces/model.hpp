// The base of every model (docs/architecture.md §7).
#pragma once

#include <cstdint>
#include <string_view>

namespace astra {

class Config;              // core/include/astra/config/config.hpp
class SignalRegistry;      // core/include/astra/signals/signal_registry.hpp
class VehicleState;        // core/include/astra/kernel/vehicle_state.hpp
struct EnvironmentSample;  // core/include/astra/kernel/environment_sample.hpp

// Handed to every step. frame is the source of truth; t = frame * dt is derived, never accumulated
// (ADR-0010).
struct StepContext {
  double t;
  double dt;
  std::uint64_t frame;
};

// init may allocate and throw (typed errors naming the config path). reset and step may not
// allocate, throw, block, or touch the wall clock (§14); step-time errors go through status flags.
class Model {
 public:
  Model() = default;
  Model(const Model&) = delete;
  Model& operator=(const Model&) = delete;
  Model(Model&&) = delete;
  Model& operator=(Model&&) = delete;
  // Defined out of line in libastra_core on purpose: the key function keeps exactly one vtable and
  // one typeinfo for Model in the process. Plugins compile with -fvisibility=hidden and would
  // otherwise carry their own hidden copies, which breaks dynamic_cast and UBSan's vptr check
  // across the .so boundary.
  virtual ~Model();

  virtual void init(const Config& config, SignalRegistry& registry) = 0;
  virtual void reset() = 0;
  virtual void step(const StepContext& ctx) = 0;
  [[nodiscard]] virtual std::string_view name() const = 0;

  // ADR-0021: a Model may additionally own n >= 0 of its own continuous states, integrated by the
  // core RK4 integrator at full per-RK4-substage fidelity alongside the rigid-body block. All five
  // are defaulted (a no-state model), so every existing subclass keeps compiling and behaving
  // identically without any edit -- see docs/plans/pr4a-dynamics-model.md's own "Rejected
  // alternatives" reasoning for why these are not mandatory.

  // The number of doubles this model owns in Vehicle's combined state buffer.
  [[nodiscard]] virtual int state_count() const noexcept { return 0; }

  // Writes this model's own initial values into q (exactly state_count() doubles, this model's own
  // slice of the combined buffer). Called once, at init, by Vehicle itself; never called when
  // state_count() == 0.
  virtual void initial_state(double* q) const noexcept { static_cast<void>(q); }

  // f(t, vs, env) -> dq, written into the caller-provided buffer: this model's own continuous-state
  // derivative for one RK4 sub-stage, given that stage's rigid-body VehicleState and the frame's
  // EnvironmentSample -- the identical (vs, env) shape WrenchSource::wrench_b() already has, so a
  // model-owned state's derivative reads the rigid-body block the same way any wrench computation
  // already does. Must write all state_count() doubles of dq, every call: dq aliases Integrator's
  // own k1..k4 scratch, sized once and never re-zeroed between calls (R2), so an element this
  // override leaves untouched keeps whatever an EARLIER RK4 sub-stage's call wrote there, not zero
  // -- a real, if narrow, difference from the pre-ADR-0021 return-by-value contract, where every
  // call got a fresh StateVector::Zero(). Per-frame path: no allocation, no exceptions, every call.
  // Never called when state_count() == 0.
  virtual void integrate_state(double t, const VehicleState& vs, const EnvironmentSample& env,
                               double* dq) const noexcept {
    static_cast<void>(t);
    static_cast<void>(vs);
    static_cast<void>(env);
    static_cast<void>(dq);
  }

  // Phase one (Vehicle::init()): hands this model a read-only pointer into its own slice of
  // Vehicle's stage buffer (refreshed every RK4 sub-stage before any wrench_b()/integrate_state()
  // call that stage). A model never holds a mutable pointer into either state buffer; own_stage_
  // slice is read-only for the model's whole lifetime. Init-time only: may throw a typed config
  // error. Never called when state_count() == 0.
  virtual void bind_state(const double* own_stage_slice) { static_cast<void>(own_stage_slice); }

  // Phase two (Vehicle::init(), after every model's phase one has run): resolves reads of a peer
  // model's published state signal(s) via registry.find(...), caching the result as a member --
  // called for every model, state-owning or not, since a pure WrenchSource may still need a peer's
  // state. Init-time only: may throw -- a model requiring a peer signal that find() returns nullptr
  // for must throw a typed ConfigError naming it.
  virtual void resolve_peers(const SignalRegistry& registry) { static_cast<void>(registry); }
};

}  // namespace astra
