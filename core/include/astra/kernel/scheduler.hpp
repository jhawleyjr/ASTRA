// The ordered (Model*, divisor) list of docs/architecture.md §5: an explicit schedule built once
// at init and walked every major frame. Order is data -- it is what Vehicle assembles its models
// in -- and entries() exposes it so a later milestone can log it.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "astra/errors.hpp"
#include "astra/interfaces/model.hpp"

namespace astra {

class Scheduler {
 public:
  struct Entry {
    Model* model;
    std::uint32_t divisor;
  };

  // divisor must be at least 1 (a model that runs every major frame uses 1); model must outlive
  // the Scheduler (Vehicle keeps its models alive for the run). Init-time only: may allocate, may
  // throw.
  void add(Model& model, std::uint32_t divisor = 1) {
    if (divisor == 0) {
      throw Error("Scheduler: divisor must be at least 1");
    }
    entries_.push_back(Entry{&model, divisor});
  }

  // Steps every entry due this frame, in registration order. noexcept: a model that throws on the
  // per-frame path terminates the process instead of unwinding through the scheduler
  // (docs/architecture.md §14 -- step-time errors go through status flags, never exceptions).
  void step(const StepContext& ctx) const noexcept {
    for (const Entry& entry : entries_) {
      if (ctx.frame % entry.divisor == 0) {
        entry.model->step(ctx);
      }
    }
  }

  [[nodiscard]] std::span<const Entry> entries() const noexcept { return entries_; }

 private:
  std::vector<Entry> entries_;
};

}  // namespace astra
