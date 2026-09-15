// The base of every model (docs/architecture.md §7).
#pragma once

#include <cstdint>
#include <string_view>

namespace astra {

class Config;          // core/include/astra/config/config.hpp
class SignalRegistry;  // core/include/astra/signals/signal_registry.hpp

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
};

}  // namespace astra
