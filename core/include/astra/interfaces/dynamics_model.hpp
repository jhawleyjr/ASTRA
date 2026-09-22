// A model-owned continuous-state derivative source (ADR-0021, docs/architecture.md §6): Nutaro's
// `ode_system` (Building Software for Simulation, §5.5), ASTRA-flavored -- `state_count()`,
// `initial_state(double* q)`, `derivative(t, const double* q, double* dq)`, `post_step(double* q)`.
// `RigidBody6DofDynamics` is the one implementation, for the rigid-body block; held directly by
// `Vehicle` (like `Eom` was before this ADR) as `std::optional<RigidBody6DofDynamics>` -- a
// by-value member, unlike every `Model` (always accessed through a pointer/reference, since a
// plugin's concrete type is erased behind the base). `Vehicle` itself is moved into
// `Simulation::vehicles_`, so this base must stay copyable and movable -- deleting them the way
// `Model` does (never sliced because never held by value) would be wrong here, not merely unused,
// since `DynamicsModel` never crosses a plugin `.so` boundary in the first place (no plugin
// implements it). The destructor stays out-of-line anyway, for the same "one key function, one
// vtable/typeinfo" hygiene `Model`'s own header documents.
#pragma once

namespace astra {

class DynamicsModel {
 public:
  DynamicsModel() = default;
  DynamicsModel(const DynamicsModel&) = default;
  DynamicsModel& operator=(const DynamicsModel&) = default;
  DynamicsModel(DynamicsModel&&) = default;
  DynamicsModel& operator=(DynamicsModel&&) = default;
  virtual ~DynamicsModel();

  // The number of doubles this dynamics model owns in the combined state buffer.
  [[nodiscard]] virtual int state_count() const noexcept = 0;

  // Writes this model's own initial values into q, a pointer to exactly state_count() doubles
  // (its own slice of Vehicle's combined state buffer). Called once, at init, by Vehicle itself.
  virtual void initial_state(double* q) const noexcept = 0;

  // f(t, q) -> dq, written into the caller-provided buffer: the derivative of this model's own
  // state_count() doubles, given the complete trial state q (this model's own slice; q points at
  // exactly state_count() doubles belonging to this model). Must write all state_count() doubles of
  // dq, every call: dq aliases Integrator's own k1..k4 scratch, sized once and never re-zeroed
  // between calls (R2) -- an element left untouched keeps whatever an earlier RK4 sub-stage's call
  // wrote there, not zero. Per-frame path: no allocation, no exceptions, every call -- called once
  // per RK4 sub-stage.
  virtual void derivative(double t, const double* q, double* dq) const noexcept = 0;

  // The post-step hook (docs/architecture.md §5, §8): runs once per major frame, after the
  // integrator step, over this model's own state_count()-double slice of the newly-accepted
  // combined state. Default: no-op. RigidBody6DofDynamics overrides this to renormalize q_nb
  // (today's post_step_renormalize).
  virtual void post_step(double* q) const noexcept { static_cast<void>(q); }
};

}  // namespace astra
