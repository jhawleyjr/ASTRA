// Scheduler (core/kernel/scheduler.hpp, docs/architecture.md §5): divisors fire the right number
// of times, in registration order; a zero divisor is rejected at init, not silently ignored at
// step time.
#include "astra/kernel/scheduler.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "astra/errors.hpp"
#include "astra/interfaces/model.hpp"

namespace {

using astra::Error;
using astra::Model;
using astra::Scheduler;
using astra::StepContext;

class RecordingModel final : public Model {
 public:
  explicit RecordingModel(std::string name, std::vector<std::string>& log)
      : name_(std::move(name)), log_(&log) {}

  void init(const astra::Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override { steps_ = 0; }
  void step(const StepContext& /*ctx*/) override {
    ++steps_;
    log_->push_back(name_);
  }
  [[nodiscard]] std::string_view name() const override { return name_; }
  [[nodiscard]] int steps() const noexcept { return steps_; }

 private:
  std::string name_;
  std::vector<std::string>* log_;
  int steps_ = 0;
};

TEST(Scheduler, DivisorsControlHowOftenEachEntryFires) {
  // Each model gets its own log to keep the fire-count assertions independent of order.
  std::vector<std::string> log_a;
  std::vector<std::string> log_b;
  std::vector<std::string> log_c;
  RecordingModel model_a("a", log_a);
  RecordingModel model_b("b", log_b);
  RecordingModel model_c("c", log_c);

  Scheduler scheduler;
  scheduler.add(model_a, 1);
  scheduler.add(model_b, 2);
  scheduler.add(model_c, 10);

  for (std::uint64_t frame = 0; frame < 20; ++frame) {
    scheduler.step(StepContext{.t = 0.0, .dt = 1e-3, .frame = frame});
  }

  EXPECT_EQ(model_a.steps(), 20);
  EXPECT_EQ(model_b.steps(), 10);
  EXPECT_EQ(model_c.steps(), 2);
}

TEST(Scheduler, StepsEntriesInRegistrationOrder) {
  std::vector<std::string> log;
  RecordingModel model_a("a", log);
  RecordingModel model_b("b", log);
  RecordingModel model_c("c", log);

  Scheduler scheduler;
  scheduler.add(model_c);
  scheduler.add(model_a);
  scheduler.add(model_b);
  scheduler.step(StepContext{.t = 0.0, .dt = 1e-3, .frame = 0});

  ASSERT_EQ(log.size(), 3U);
  EXPECT_EQ(log[0], "c");
  EXPECT_EQ(log[1], "a");
  EXPECT_EQ(log[2], "b");
}

TEST(Scheduler, RejectsAZeroDivisorAtAddTime) {
  std::vector<std::string> log;
  RecordingModel model("a", log);
  Scheduler scheduler;
  EXPECT_THROW(scheduler.add(model, 0), Error);
}

TEST(Scheduler, EntriesExposesTheRegisteredOrderAndDivisors) {
  std::vector<std::string> log;
  RecordingModel model_a("a", log);
  RecordingModel model_b("b", log);
  Scheduler scheduler;
  scheduler.add(model_a, 1);
  scheduler.add(model_b, 5);

  ASSERT_EQ(scheduler.entries().size(), 2U);
  EXPECT_EQ(scheduler.entries()[0].model, &model_a);
  EXPECT_EQ(scheduler.entries()[0].divisor, 1U);
  EXPECT_EQ(scheduler.entries()[1].model, &model_b);
  EXPECT_EQ(scheduler.entries()[1].divisor, 5U);
}

}  // namespace
