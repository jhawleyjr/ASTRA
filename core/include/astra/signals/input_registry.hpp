// SignalRegistry's mutable counterpart (ADR-0017, docs/decisions/0017-control-input-write-seam.md):
// a WrenchSource registers a writable input from a new WrenchSource::bind_inputs() hook and gets
// back a const double* into this registry's own canonical storage. Storage is registry-owned, not
// caller-owned: a second registration under an already-registered name (two instances of one
// plugin, e.g. left/right engines of one turbofan) returns the same pointer instead of colliding,
// so both instances share one value by default. Owned one per Vehicle (ADR-0011).
#pragma once

#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace astra {

class SignalRegistry;  // core/include/astra/signals/signal_registry.hpp

// A resolved, repeatable-write handle into one InputRegistry entry -- mirrors SignalRegistry::
// find()'s own "resolve once at init, the per-frame path holds the pointer and never calls find()
// again" contract, but for writing. Resolved once (typically during bind_inputs()) and called
// repeatedly later -- from inside wrench_b() itself (a labeled, test-only exception -- see
// wrench_source.hpp) or from a model's own step() (the one sanctioned write point) -- since neither
// wrench_b() nor Model::step()'s signature carries an InputRegistry reference of its own. Trivially
// copyable, no allocation, no std::string (same shape as HaltReason).
class InputHandle {
 public:
  [[nodiscard]] explicit operator bool() const noexcept { return pending_ != nullptr; }

  // Stages this entry's pending value -- noexcept, since resolution (name lookup, the only failure
  // mode) already happened in find(). const: mutates *pending_ (the pointee), not pending_ itself,
  // so this is callable from inside a const method -- specifically WrenchSource::wrench_b() const
  // noexcept, for the one labeled test-only exception wrench_source.hpp documents. Per-frame path:
  // no allocation, no exceptions.
  void write(double value) const noexcept;

 private:
  double* pending_ = nullptr;  // into the owning InputRegistry's own stable storage.
  friend class InputRegistry;
};

class InputRegistry {
 public:
  // No constructor parameters -- owned one-per-Vehicle, default-constructed alongside it.
  InputRegistry() = default;

  // Registers name as writable and returns a const double* into this registry's own canonical
  // storage for name -- the plugin stores this pointer as its own member and dereferences it inside
  // wrench_b(). Forwards to registry.add(name, pointer, unit), but ONLY on the first add() call for
  // a given name; registry is a transient parameter, not a stored reference (Simulation, not
  // Vehicle, owns the one shared SignalRegistry, itself a movable, returned-by-value object). A
  // second add() for an already-registered name (two instances of one plugin) returns the SAME
  // pointer without a second registry.add() call (which would itself throw a duplicate-name Error);
  // throws Error if the requested unit does not match the first registration's, or if name is
  // empty. No default_value parameter: the entry has no meaningful value until something write()s
  // it -- every entry's canonical, pending, and freeze()-recorded storage default-initializes to a
  // quiet NaN, never an unspecified value. Init-time only: may throw.
  [[nodiscard]] const double* add(SignalRegistry& registry, std::string_view name,
                                  std::string_view unit);

  // Stages value as name's pending value; has no effect on the canonical value until the next
  // latch(). Throws Error naming name if not registered. Not itself on the per-frame path -- called
  // by config-literal application at init, or (PR 5) Python between sim.step() calls.
  void write(std::string_view name, double value);

  // Resolves name to a stable InputHandle for a caller that will write repeatedly without
  // re-resolving by name each time. Returns a handle that converts to false if name is not
  // registered. Init-time (or equivalent early-hook) use only; the per-frame path holds the handle
  // it already resolved and never calls find() again.
  [[nodiscard]] InputHandle find(std::string_view name) noexcept;

  // The dotted name of the first registered entry with no value yet, or whose only written value is
  // non-finite, or empty if every entry has a real, finite value. Called once, after every model's
  // init()/bind_inputs() and every config-literal write() has run, before freeze(). Inspects each
  // entry's PENDING value, never its canonical value: canonical only ever becomes meaningful once
  // this check has already passed and freeze() has run.
  [[nodiscard]] std::string_view first_unwritten_or_non_finite() const noexcept;

  // Every registered entry's own dotted name, in registration order. Init-time enumeration only,
  // for a caller that needs to know WHICH names exist without walking a config document's own
  // read-state (Codex adversarial review, M3 PR 2): Simulation::from_config uses this, not Config::
  // unknown_keys(), to apply vehicle.initial_controls's literals -- unknown_keys() diffs against a
  // Config document's shared, mutable read-tracking, which a second Simulation built from the SAME
  // Config (e.g. a future Monte Carlo driver reusing one loaded document) would find already marked
  // read by the first construction, silently skipping every input's config literal the second time.
  // Allocates (a std::vector); not on the per-frame path.
  [[nodiscard]] std::vector<std::string_view> names() const;

  // Copies every entry's pending value into its canonical storage -- the single per-vehicle-per-
  // frame commit point. Processes every entry uniformly, never short-circuiting on the first fault
  // (ADR-0016's own precedent for check_wrench's per-stage evaluation). Returns the name of the
  // FIRST entry whose pending value was non-finite, or empty if none; that one entry's canonical
  // value is left at its last good value, every OTHER entry's finite pending value is still
  // committed this same call, warn or halt alike. Per-frame path: no allocation, no exceptions.
  [[nodiscard]] std::string_view latch() noexcept;

  // Restores every entry's canonical and pending value to the value recorded when freeze() last
  // ran, i.e. the config-literal value each entry held at that point -- or, for a vehicle that
  // never called freeze() (Simulation::for_testing), back to that field's own NaN default.
  // Per-frame path discipline is not required here (not called per frame), but the signature stays
  // noexcept to match Vehicle::reset()'s own call site. A conforming WrenchSource must never write
  // through a cached InputHandle from its own reset() (wrench_source.hpp) -- this is the only
  // mechanism permitted to change a value from within Vehicle::reset()'s call.
  void reset() noexcept;

  // Closes registration (matching SignalRegistry::freeze()'s own shape) AND seeds every entry's
  // canonical storage from its already-validated pending value, right here, at init time -- the ONE
  // unconditional commit that establishes a real "last good value" before Simulation::from_config
  // ever returns, independent of whichever per-frame latch() happens to run first. Simulation::
  // for_testing never calls this; its vehicles' entries stay at their NaN default until a test's
  // own write() -- and a forgotten write() then fails loudly at the first latch(), naming the
  // input, rather than reading undefined memory.
  void freeze() noexcept;

 private:
  struct Entry {
    std::string name;
    std::string unit;
    double canonical;
    double pending;
    double frozen_value;
  };

  // std::deque, not std::vector: every entry's storage is independently allocated and never moves,
  // so a const double*/InputHandle handed out at any point (including during bind_inputs(), before
  // this Vehicle is moved into Simulation::vehicles_) remains valid for the Vehicle's entire
  // lifetime, surviving both further registry growth and the Vehicle move itself.
  std::deque<Entry> entries_;
  bool frozen_ = false;
};

}  // namespace astra
