// PropulsionModel: a WrenchSource specialization (docs/architecture.md §7, ADR-0020) that exists
// for type identity plus one safe-default override, nothing more. Mirrors AeroModel's own shape
// exactly -- see aero_model.hpp and ADR-0020's own Decision for the full reasoning, not duplicated
// here.
#pragma once

#include "astra/interfaces/wrench_source.hpp"

namespace astra {

class PropulsionModel : public WrenchSource {
 public:
  // Defined out of line in libastra_core; same key-function reasoning as AeroModel/WrenchSource.
  ~PropulsionModel() override;

  [[nodiscard]] EnvironmentRequirements requires_environment() const noexcept override {
    return {.wind = false, .atmosphere = true};
  }
};

}  // namespace astra
