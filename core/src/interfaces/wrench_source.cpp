#include "astra/interfaces/wrench_source.hpp"

namespace astra {

// The key function; see the comment in wrench_source.hpp and in model.cpp.
WrenchSource::~WrenchSource() = default;

// Default: no requirement. Only a WrenchSource that reads env.wind_n or env.atmosphere needs to
// override this (wrench_source.hpp).
EnvironmentRequirements WrenchSource::requires_environment() const noexcept { return {}; }

// ADR-0017: default no-op. Only a WrenchSource with a writable input overrides this.
void WrenchSource::bind_inputs(InputRegistry& /*inputs*/, SignalRegistry& /*registry*/) {}

}  // namespace astra
