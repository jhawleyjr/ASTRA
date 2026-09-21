// AeroModel: a WrenchSource specialization (docs/architecture.md §7, ADR-0020) that exists for type
// identity plus one safe-default override, nothing more -- no compute() step, no core-defined
// Inputs struct, no final anywhere (superseding ADR-0018's compute(const AeroInputs&)/final
// wrench_b()/final requires_environment() design). A concrete plugin overrides
// WrenchSource::wrench_b(const VehicleState&, const EnvironmentSample&) const noexcept directly,
// the same pattern tests/plugins/nesc_aero/nan_wrench already use, composing whatever it needs from
// core/include/astra/util/air_data.hpp's utilities.
#pragma once

#include "astra/interfaces/wrench_source.hpp"

namespace astra {

class AeroModel : public WrenchSource {
 public:
  // Defined out of line in libastra_core, the same key-function reasoning as WrenchSource itself
  // (wrench_source.hpp): without it, each plugin's own -fvisibility=hidden translation unit would
  // carry a private vtable/typeinfo for AeroModel, breaking dynamic_cast<AeroModel*> across the
  // .so boundary.
  ~AeroModel() override;

  // Non-final (ADR-0020): a concrete plugin MAY override this if it genuinely needs something
  // else. An author who never touches it gets this safe default without having to know to ask for
  // it -- this class's own reason to exist beyond a bare WrenchSource, alongside letting
  // Vehicle::init()'s discovery pass recognize "this is an aero contribution" for logging,
  // diagnostics, and any future aero-specific §15 envelope-monitor hook.
  [[nodiscard]] EnvironmentRequirements requires_environment() const noexcept override {
    return {.wind = false, .atmosphere = true};
  }
};

}  // namespace astra
