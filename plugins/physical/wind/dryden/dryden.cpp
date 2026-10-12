// MIL-F-8785C Dryden turbulence (docs/architecture.md §7, PLAN.md M2 PR 2b). A stateful WindModel
// whose gust sequence is seeded, scheduler-stepped randomness (docs/architecture.md §2 principle 2:
// "Random processes ... are seeded and stepped by the scheduler, never by a system RNG") --
// step(ctx) draws this frame's five normal() samples (constant count, independent of how many
// vehicles or consumers exist); begin_frame(ctx, vs, altitude_m) advances the three shaping filters
// (plugins/physical/wind/dryden/dryden_filter.hpp) using the vehicle's REAL per-frame
// Earth-relative airspeed and Earth-derived altitude, consuming exactly the samples step() just
// drew (Simulation::step runs environment_scheduler_.step(ctx) before the per-vehicle begin_frame,
// so draw-then-consume order is guaranteed by the caller, not by anything in this file);
// wind_n(ctx, vs) is a pure const read of whatever begin_frame already latched.
//
// --- Two things this header states rather than leaves implied (wind_model.hpp's own convention)
// ---
//
// (1) wind_n IS WIND-TO, IN NED (wind_model.hpp, ADR-0001), not the meteorological "wind-from"
// convention. (u_g, v_g, w_g) below are the air-mass velocity components along BODY FRD axes, so
// wind_n_ = rotate_b_to_n(q_nb, gust_b) rotates them into nav frame directly -- no sign flip. A
// consumer forms air-relative velocity as v_b - rotate_n_to_b(q_nb, wind_n(ctx, vs))
// (wind_model.hpp). A PSD test (tests/physics/dryden_psd_test.cpp) cannot catch this sign being
// backwards -- a PSD is invariant under negation -- so it is stated here, not left to be inferred.
//
// (2) THE AIRSPEED THIS FILTER USES IS EARTH-RELATIVE |v_b|, NOT AIR-RELATIVE. This is the standard
// MIL-F-8785C implementation simplification: the gust this filter computes is precisely what turns
// Earth-relative velocity into air-relative velocity (see (1)'s formula), so using air-relative
// airspeed as the filter's own input would close an algebraic loop through the model's own output.
// The resulting error is O(sigma/V) -- small whenever the turbulence intensity is a small fraction
// of the vehicle's speed, which is the regime MIL-F-8785C's own tables are drawn for. Unstated,
// this reads as a bug; it is a standard, bounded approximation.
//
// --- physical.wind config ---
//
// seed (REQUIRED, no default): the std::mt19937_64 seed. §15's determinism guard: "seeds present
// for every stochastic plugin, and the seed is logged" -- a config naming this plugin with no seed
// key is a ConfigError naming physical.wind.seed, never a silently-chosen default. Rejected:
// negative, or greater than 2^53 (the SignalRegistry stores every value as a double, so a larger
// seed could not be logged exactly).
//
// w20_knots (required): MIL-F-8785C's W_20, the wind speed at 20 ft (section 3.7.3.1, p.51), in
// KNOTS -- the spec's own unit for this parameter (figure 9's x-axis, p.52, is "Mean Wind Speed,
// Knots"), converted once at config read, at the boundary (ADR-0002), never inline in the per-frame
// math. sigma_w at low altitude is section 3.7.3.4's (p.53) sigma_w[ft/s] = 0.1*W_20[kt] -- a
// mixed- unit empirical relation, not a unit conversion; feet_to_meters is applied to its ft/s
// RESULT, not to w20_knots itself.
//
// sigma_high (required): sigma_u = sigma_v = sigma_w at and above 2000 ft, m/s (SI already -- this
// key takes no unit suffix). MIL-F-8785C reads this off Figure 7's (p.49) probability-of-exceedance
// chart; THAT CHART IS NOT DIGITIZED HERE -- it would be a human-owned golden table
// (tests/golden/**, Joe's), so a config states the intensity it wants directly. Figure 7's own axis
// is "RMS TURBULENCE AMPLITUDE, sigma (FT/SEC - TAS)" -- feet per second, true airspeed; this key
// takes m/s, a boundary conversion the config author performs when reading a value off that chart.
//
// Disclosed, not enforced: w20_knots and sigma_high are independently configured, but MIL-F-8785C
// derives both from one underlying exceedance-probability chart, and the 1000-2000 ft interpolation
// (dryden_filter.hpp) blends them as if they were a consistent pair. Nothing here rejects a config
// naming an inconsistent pair -- there is no golden chart in the tree to check consistency against,
// the same reason sigma_high itself is a free parameter rather than a table lookup.
//
// Turbulence only, zero mean, on purpose: this plugin takes no north/east/down steady-wind keys.
// W_20 is an intensity parameter, not a mean-wind command, and folding a steady component in here
// would pre-empt the wind-composition design (a summing/layered WindModel) PLAN.md's M2 PR 3 line
// is the right place to decide. A config needing both today uses wind/uniform for the steady part.
//
// Config keys:
//   seed       | integer | 1   | required | The std::mt19937_64 seed, from 0 to 2^53; logged as
//          wind.seed.
//   w20_knots  | number  | kt  | required | MIL-F-8785C's W_20, the wind speed at 20 ft, in knots
//          (the specification's unit, converted once at init); at least 0. It sets the low-altitude
//          intensity.
//   sigma_high | number  | m/s | required | The turbulence intensity sigma_u = sigma_v = sigma_w at
//          and above 2000 ft, in m/s (MIL-F-8785C's Figure 7 reads it in ft/s); at least 0.
#include <Eigen/Core>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/wind_model.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/frames.hpp"
#include "astra/util/random.hpp"
#include "astra/util/units.hpp"
#include "dryden_filter.hpp"

namespace {

using astra::Config;
using astra::ConfigError;
using astra::VehicleState;

std::string keyed_path(const Config& config, std::string_view key) {
  return config.path().empty() ? std::string{key} : config.path() + "." + std::string{key};
}

double checked_finite(const Config& config, std::string_view key, double value) {
  if (!std::isfinite(value)) {
    throw ConfigError(config.origin(), keyed_path(config, key),
                      std::string{key} + " must be finite");
  }
  return value;
}

// The SignalRegistry stores every value as a double (signal_registry.hpp); a seed above this could
// not be logged exactly, defeating §15's "the seed is logged."
constexpr std::int64_t kMaxLoggableSeed = 1LL << 53;

class DrydenWind final : public astra::WindModel {
 public:
  void init(const Config& config, astra::SignalRegistry& registry) override {
    const std::int64_t seed_raw = config.integer("seed");  // no default: missing -> ConfigError
                                                           // naming "physical.wind.seed"
    if (seed_raw < 0) {
      throw ConfigError(config.origin(), keyed_path(config, "seed"), "seed must not be negative");
    }
    if (seed_raw > kMaxLoggableSeed) {
      throw ConfigError(config.origin(), keyed_path(config, "seed"),
                        "seed must not exceed 2^53 (the SignalRegistry logs it as a double)");
    }
    seed_ = static_cast<std::uint64_t>(seed_raw);
    seed_as_double_ = static_cast<double>(seed_raw);
    rng_ = astra::random::Rng(seed_);

    const double w20_knots = checked_finite(config, "w20_knots", config.number("w20_knots"));
    if (w20_knots < 0.0) {
      throw ConfigError(config.origin(), keyed_path(config, "w20_knots"),
                        "w20_knots must not be negative");
    }
    w20_m_s_ = astra::units::knots_to_meters_per_second(w20_knots);
    // section 3.7.3.4: sigma_w[ft/s] = 0.1 * W_20[kt] -- feet_to_meters converts the ft/s RESULT,
    // not w20_knots itself (this relation is empirical, not a unit conversion).
    sigma_w_low_ = astra::units::feet_to_meters(astra::dryden::kSigmaWToW20Ratio * w20_knots);

    sigma_high_ = checked_finite(config, "sigma_high", config.number("sigma_high"));
    if (sigma_high_ < 0.0) {
      throw ConfigError(config.origin(), keyed_path(config, "sigma_high"),
                        "sigma_high must not be negative");
    }

    registry.add("wind.seed", &seed_as_double_, "1");
    registry.add("wind.north", &wind_n_(0), "m/s");
    registry.add("wind.east", &wind_n_(1), "m/s");
    registry.add("wind.down", &wind_n_(2), "m/s");
    registry.add("wind.w20", &w20_m_s_, "m/s");
    registry.add("wind.sigma_u", &sigma_u_, "m/s");
    registry.add("wind.sigma_v", &sigma_v_, "m/s");
    registry.add("wind.sigma_w", &sigma_w_, "m/s");
    registry.add("wind.length_u", &length_u_, "m");
    registry.add("wind.length_v", &length_v_, "m");
    registry.add("wind.length_w", &length_w_, "m");
    registry.add("wind.airspeed", &airspeed_, "m/s");
    registry.add("wind.altitude", &altitude_, "m");
  }

  void reset() override {
    rng_.reseed(seed_);
    stationary_start_ = true;
    u_ = 0.0;
    v_x_.setZero();
    w_x_.setZero();
    wind_n_.setZero();
    sigma_u_ = sigma_v_ = sigma_w_ = 0.0;
    length_u_ = length_v_ = length_w_ = 0.0;
    airspeed_ = 0.0;
    altitude_ = 0.0;
  }

  // Draws this frame's five normal() samples (u: 1; v: 2; w: 2 -- matching a normal frame's
  // consumption exactly on the stationary-start frame too, dryden_filter.hpp's own header). No
  // other work: evolution here depends only on the scheduler, never on how many vehicles read
  // wind_n.
  void step(const astra::StepContext& /*ctx*/) override {
    for (double& sample : samples_) {
      sample = rng_.normal();
    }
  }

  void begin_frame(const astra::StepContext& ctx, const VehicleState& vs,
                   double altitude_m) noexcept override {
    const double airspeed = vs.v_b().norm();  // Earth-relative, on purpose -- see the file header
    const astra::dryden::MilParameters mp =
        astra::dryden::mil_f_8785c_parameters(altitude_m, sigma_w_low_, sigma_high_);
    const double rate_u = astra::dryden::gust_rate(airspeed, mp.length_u_m);
    const double rate_v = astra::dryden::gust_rate(airspeed, mp.length_v_m);
    const double rate_w = astra::dryden::gust_rate(airspeed, mp.length_w_m);

    if (stationary_start_) {
      u_ = astra::dryden::scalar_stationary_init(mp.sigma_u_m_s, samples_[0]);
      v_x_ = astra::dryden::two_state_stationary_init(rate_v, samples_[1], samples_[2]);
      w_x_ = astra::dryden::two_state_stationary_init(rate_w, samples_[3], samples_[4]);
      stationary_start_ = false;
    } else {
      const astra::dryden::ScalarCoefficients cu =
          astra::dryden::scalar_coefficients(rate_u, mp.sigma_u_m_s, ctx.dt);
      u_ = cu.phi * u_ + cu.q_std * samples_[0];

      const astra::dryden::TwoStateCoefficients cv =
          astra::dryden::two_state_coefficients(rate_v, ctx.dt);
      const Eigen::Matrix2d lv = astra::dryden::cholesky_lower_2x2(cv.qd);
      v_x_ = (cv.ad * v_x_) + (lv * Eigen::Vector2d{samples_[1], samples_[2]});

      const astra::dryden::TwoStateCoefficients cw =
          astra::dryden::two_state_coefficients(rate_w, ctx.dt);
      const Eigen::Matrix2d lw = astra::dryden::cholesky_lower_2x2(cw.qd);
      w_x_ = (cw.ad * w_x_) + (lw * Eigen::Vector2d{samples_[3], samples_[4]});
    }

    const double u_g = u_;
    const double v_g = astra::dryden::two_state_output(rate_v, mp.sigma_v_m_s, v_x_);
    const double w_g = astra::dryden::two_state_output(rate_w, mp.sigma_w_m_s, w_x_);
    const Eigen::Vector3d gust_b{u_g, v_g, w_g};
    wind_n_ = astra::frames::rotate_b_to_n(vs.q_nb(), gust_b);

    sigma_u_ = mp.sigma_u_m_s;
    sigma_v_ = mp.sigma_v_m_s;
    sigma_w_ = mp.sigma_w_m_s;
    length_u_ = mp.length_u_m;
    length_v_ = mp.length_v_m;
    length_w_ = mp.length_w_m;
    airspeed_ = airspeed;
    altitude_ = altitude_m;
  }

  [[nodiscard]] Eigen::Vector3d wind_n(const astra::StepContext& /*ctx*/,
                                       const VehicleState& /*vs*/) const noexcept override {
    return wind_n_;
  }

  // Default is false (wind_model.hpp): this filter's state is keyed to one vehicle's own V/h, so it
  // cannot honor two vehicles' begin_frame calls with one set of internal state. Not overridden --
  // the default ships as-is.

  [[nodiscard]] std::string_view name() const override { return "wind"; }

 private:
  astra::random::Rng rng_{0};
  std::uint64_t seed_ = 0;
  double seed_as_double_ = 0.0;
  double sigma_w_low_ = 0.0;  // m/s, derived from w20_knots at init
  double sigma_high_ = 0.0;   // m/s, from config directly
  double w20_m_s_ = 0.0;      // published: the SI value of the configured w20_knots

  // this frame's normal() draws, set by step(): u gets 1, v gets 2, w gets 2 (matching a normal
  // frame's consumption exactly on the stationary-start frame too -- dryden_filter.hpp's own
  // header).
  static constexpr std::size_t kSampleCount = 5;
  std::array<double, kSampleCount> samples_{};
  bool stationary_start_ = true;

  double u_ = 0.0;                                 // scalar (longitudinal) filter state
  Eigen::Vector2d v_x_ = Eigen::Vector2d::Zero();  // two-state lateral filter state
  Eigen::Vector2d w_x_ = Eigen::Vector2d::Zero();  // two-state vertical filter state

  Eigen::Vector3d wind_n_ = Eigen::Vector3d::Zero();  // latched by begin_frame, read by wind_n()

  // Published (registry.add), mirroring begin_frame's latest evaluation:
  double sigma_u_ = 0.0;
  double sigma_v_ = 0.0;
  double sigma_w_ = 0.0;
  double length_u_ = 0.0;
  double length_v_ = 0.0;
  double length_w_ = 0.0;
  double airspeed_ = 0.0;
  double altitude_ = 0.0;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<DrydenWind>(); }

constexpr astra::PluginDescriptor kDescriptor{"physical", "wind_dryden", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
