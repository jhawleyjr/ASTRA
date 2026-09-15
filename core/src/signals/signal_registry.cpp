#include "astra/signals/signal_registry.hpp"

#include <algorithm>
#include <utility>

#include "astra/errors.hpp"

namespace astra {

void SignalRegistry::add(std::string_view name, const double* value, std::string_view unit,
                         std::uint32_t divisor) {
  if (frozen_) {
    throw Error("SignalRegistry: add() called after freeze()");
  }
  if (name.empty()) {
    throw Error("SignalRegistry: signal name must not be empty");
  }
  std::string full_name = prefix_ + std::string{name};
  if (value == nullptr) {
    throw Error("SignalRegistry: signal '" + full_name + "' has a null value pointer");
  }
  if (divisor == 0) {
    throw Error("SignalRegistry: signal '" + full_name + "' has a zero divisor");
  }
  const bool duplicate =
      std::any_of(signals_.begin(), signals_.end(),
                  [&full_name](const Signal& signal) { return signal.name == full_name; });
  if (duplicate) {
    throw Error("SignalRegistry: duplicate signal name '" + full_name + "'");
  }
  signals_.push_back(Signal{std::move(full_name), value, std::string{unit}, divisor});
}

const double* SignalRegistry::find(std::string_view name) const noexcept {
  for (const Signal& signal : signals_) {
    if (signal.name == name) {
      return signal.value;
    }
  }
  return nullptr;
}

void SignalRegistry::freeze() noexcept { frozen_ = true; }

SignalRegistry::PrefixScope::PrefixScope(SignalRegistry& registry, std::string_view prefix)
    : registry_(registry), previous_prefix_(registry.prefix_) {
  registry_.prefix_ += std::string{prefix};
  registry_.prefix_ += '.';
}

SignalRegistry::PrefixScope::~PrefixScope() { registry_.prefix_ = previous_prefix_; }

}  // namespace astra
