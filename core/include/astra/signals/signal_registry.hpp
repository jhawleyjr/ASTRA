// The signal dictionary every model publishes into at init (docs/architecture.md §9, ADR-0002).
// The hot path never touches a name: find() resolves to a pointer once, at init; step() and the
// logger read through that pointer from then on. Every model's namespace is applied by a
// PrefixScope the Vehicle opens around its init (ADR-0011: "v0.aero.CL").
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace astra {

// One published signal: a name (already prefixed), a pointer into the model that owns the value,
// and the unit string ADR-0002 requires as metadata (never as a name suffix).
struct Signal {
  std::string name;
  const double* value;
  std::string unit;
  std::uint32_t divisor;
};

class SignalRegistry {
 public:
  // Registers name (namespaced by any currently open PrefixScope) pointing at value, whose
  // lifetime must outlive the registry (the model that owns it does, by construction: Vehicle
  // keeps its models alive for the run). divisor mirrors the scheduler's rate divisor for this
  // signal's owner. Init-time only: throws Error on an empty name, a null value, a zero divisor,
  // a duplicate name, or a call after freeze().
  void add(std::string_view name, const double* value, std::string_view unit,
           std::uint32_t divisor = 1);

  // Resolves a registered name to its pointer, or nullptr if none was registered under that exact
  // name. For init-time use (a model wiring up a peer's output) and for tests; the per-frame path
  // holds the pointer it already resolved and never calls this.
  [[nodiscard]] const double* find(std::string_view name) const noexcept;

  // Closes registration: add() after this throws. signals() is only meaningful once frozen.
  void freeze() noexcept;
  [[nodiscard]] bool frozen() const noexcept { return frozen_; }

  [[nodiscard]] std::span<const Signal> signals() const noexcept { return signals_; }

  // RAII: while alive, every add() call is prefixed with "<prefix>." (nested scopes concatenate).
  // Vehicle opens one around each model's init so a model registering "dummy.steps" is published
  // as "v0.dummy.steps".
  class PrefixScope {
   public:
    PrefixScope(SignalRegistry& registry, std::string_view prefix);
    ~PrefixScope();
    PrefixScope(const PrefixScope&) = delete;
    PrefixScope& operator=(const PrefixScope&) = delete;
    PrefixScope(PrefixScope&&) = delete;
    PrefixScope& operator=(PrefixScope&&) = delete;

   private:
    SignalRegistry& registry_;
    std::string previous_prefix_;
  };

 private:
  std::string prefix_;
  std::vector<Signal> signals_;
  bool frozen_ = false;
};

}  // namespace astra
