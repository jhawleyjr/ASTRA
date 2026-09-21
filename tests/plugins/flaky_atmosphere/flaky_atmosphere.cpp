// An AtmosphereModel that returns a finite AtmoState on its own first two at() calls after each
// frame's step() -- Simulation::step's own frame-start pre-sample (simulation.cpp, feeds
// WindModel::begin_frame's altitude and the frame-latched EnvironmentSample baseline, unconditional
// whenever atmosphere_ != nullptr) is always call 1; Vehicle::prepare_stage_environment's own k1
// RK4 sub-stage sample (which always overwrites that baseline fresh -- PR 3c) is call 2 -- then an
// otherwise-finite AtmoState with an infinite speed_of_sound_m_s (the R-001 case: an infinite, not
// NaN, raw field with a finite-looking derived Mach) from k2 onward (call 3+). For ADR-0020's own
// acceptance tests
// (docs/plans/pr3d-adr0020-implementation.md, R3/R4): proves a fault first appearing at k2 is
// still caught, with the correct source, and that report_non_finite_environment checks the RAW
// speed_of_sound_m_s field, not only fields a plugin's own derived quantities would expose. at()
// is const noexcept, per the interface, so its per-frame call counter is a `mutable` member: test
// instrumentation only, the same documented exception tests/plugins/counting_wind's own call
// counters already use.
#include <limits>
#include <memory>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/interfaces/atmosphere_model.hpp"
#include "astra/plugin/descriptor.hpp"

namespace {

class FlakyAtmosphere final : public astra::AtmosphereModel {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override { calls_this_frame_ = 0; }
  // Scheduler-driven, once per frame (Simulation::step's environment_scheduler_.step(ctx)),
  // strictly before any RK4 sub-stage's own at() call -- the same "reset the per-frame count"
  // pattern tests/plugins/counting_wind's own step() already uses for its frame_min/max fields.
  void step(const astra::StepContext& /*ctx*/) override { calls_this_frame_ = 0; }
  [[nodiscard]] std::string_view name() const override { return "flaky_atmosphere"; }

  [[nodiscard]] astra::AtmoState at(double /*altitude_m*/) const noexcept override {
    ++calls_this_frame_;  // mutable: test instrumentation only, see file header
    astra::AtmoState state;
    state.temperature_k = 288.15;
    state.pressure_pa = 101325.0;
    state.density_kg_m3 = 1.225;
    state.speed_of_sound_m_s =
        calls_this_frame_ <= 2 ? 340.0 : std::numeric_limits<double>::infinity();
    return state;
  }

  // Wide enough to cover any altitude a test scenario could use, the same reasoning
  // tests/plugins/nan_atmosphere's own enormous bound uses -- this plugin's own fault is never a
  // valid_range() excursion, so the domain pre-check must never intercept it first.
  [[nodiscard]] astra::AltitudeRange valid_range() const noexcept override {
    return {-1.0e9, 1.0e9};  // NOLINT(readability-magic-numbers): a deliberately huge bound
  }

 private:
  mutable int calls_this_frame_ = 0;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<FlakyAtmosphere>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "flaky_atmosphere", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
