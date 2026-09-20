#include "astra/signals/input_registry.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "astra/errors.hpp"
#include "astra/signals/signal_registry.hpp"

namespace astra {

void InputHandle::write(double value) const noexcept { *pending_ = value; }

const double* InputRegistry::add(SignalRegistry& registry, std::string_view name,
                                 std::string_view unit) {
  if (frozen_) {
    throw Error("InputRegistry: add() called after freeze()");
  }
  if (name.empty()) {
    throw Error("InputRegistry: input name must not be empty");
  }
  for (Entry& entry : entries_) {
    if (entry.name == name) {
      if (entry.unit != unit) {
        throw Error("InputRegistry: input '" + std::string{name} +
                    "' already registered with "
                    "unit '" +
                    entry.unit + "', requested unit '" + std::string{unit} + "'");
      }
      return &entry.canonical;
    }
  }
  constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
  entries_.push_back(Entry{std::string{name}, std::string{unit}, kNan, kNan, kNan});
  Entry& entry = entries_.back();
  registry.add(name, &entry.canonical, unit);
  return &entry.canonical;
}

void InputRegistry::write(std::string_view name, double value) {
  for (Entry& entry : entries_) {
    if (entry.name == name) {
      entry.pending = value;
      return;
    }
  }
  throw Error("InputRegistry: write() to unregistered input '" + std::string{name} + "'");
}

InputHandle InputRegistry::find(std::string_view name) noexcept {
  for (Entry& entry : entries_) {
    if (entry.name == name) {
      InputHandle handle;
      handle.pending_ = &entry.pending;
      return handle;
    }
  }
  return InputHandle{};
}

std::vector<std::string_view> InputRegistry::names() const {
  std::vector<std::string_view> result;
  result.reserve(entries_.size());
  for (const Entry& entry : entries_) {
    result.push_back(entry.name);
  }
  return result;
}

std::string_view InputRegistry::first_unwritten_or_non_finite() const noexcept {
  for (const Entry& entry : entries_) {
    if (!std::isfinite(entry.pending)) {
      return entry.name;
    }
  }
  return {};
}

std::string_view InputRegistry::latch() noexcept {
  std::string_view first_fault;
  for (Entry& entry : entries_) {
    if (std::isfinite(entry.pending)) {
      entry.canonical = entry.pending;
    } else if (first_fault.empty()) {
      first_fault = entry.name;
    }
  }
  return first_fault;
}

void InputRegistry::reset() noexcept {
  for (Entry& entry : entries_) {
    entry.canonical = entry.frozen_value;
    entry.pending = entry.frozen_value;
  }
}

void InputRegistry::freeze() noexcept {
  frozen_ = true;
  for (Entry& entry : entries_) {
    entry.frozen_value = entry.pending;
    entry.canonical = entry.pending;
  }
}

}  // namespace astra
