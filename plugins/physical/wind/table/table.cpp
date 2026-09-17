// Altitude-keyed wind (docs/architecture.md §7, PLAN.md M2 PR 3, ADR-0013): wind_n is a
// piecewise-linear function of Earth-derived altitude_m, from a config-supplied table of (altitude,
// north, east, down) points -- NESC check-case 8's own scenario ("Vwind = 0.003h - 20 ft/s from
// west; h is height MSL in ft", Vol II Table 32), settled by ADR-0013 as the right shape for a
// wind that varies with height rather than with time (neither wind/uniform nor wind/dryden produces
// a commanded mean that varies with anything).
//
// Memoryless, unlike wind/dryden's shaping filters: wind_n at a given altitude does not depend on
// this model's own history, only on the current frame's altitude_m -- so begin_frame's evaluation
// for one vehicle carries nothing forward that a different vehicle's begin_frame call could
// contaminate, even though the same Eigen::Vector3d member holds the latest result between the two
// calls Simulation::step makes for one vehicle (begin_frame immediately followed by wind_n, before
// the next vehicle's own pair runs -- simulation.cpp). shareable_across_vehicles() is therefore
// true, the same reasoning wind/uniform's own (stateless) override gives, despite this plugin
// holding non-const state wind/uniform does not.
//
// Outside the table's own altitude range, at() clamps to the nearest endpoint's wind rather than
// extrapolating the linear segment past it -- a table is data, not a formula, and a config author
// who wants coverage past either end adds another point rather than relying on extrapolation this
// plugin does not promise. altitude_m is exactly whatever EarthModel::altitude() returns (this
// plugin's own assumed_position_representation() is kAny: it never reads vs.p_n(), only the
// altitude_m parameter begin_frame is handed -- see wind_model.hpp's own header for why that is the
// only representation-safe way for a WindModel to use altitude at all).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/position_representation.hpp"
#include "astra/interfaces/wind_model.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::ConfigError;

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

struct Point {
  double altitude_m;
  Eigen::Vector3d wind_n;
};

class TableWind final : public astra::WindModel {
 public:
  void init(const Config& config, astra::SignalRegistry& registry) override {
    const Config points_cfg = config.at("points");  // missing -> ConfigError naming "...points"
    if (!points_cfg.is_sequence()) {
      throw ConfigError(config.origin(), keyed_path(config, "points"), "points must be a sequence");
    }
    const std::size_t count = points_cfg.size();
    if (count < kMinPoints) {
      throw ConfigError(config.origin(), keyed_path(config, "points"),
                        "points must have at least two entries -- a table with one point cannot "
                        "vary with altitude");
    }
    points_.clear();
    points_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const Config point_cfg = points_cfg.at(i);
      const double altitude_m = checked_finite(point_cfg, "altitude", point_cfg.number("altitude"));
      Eigen::Vector3d point_wind_n;
      point_wind_n(0) = checked_finite(point_cfg, "north", point_cfg.number("north", 0.0));
      point_wind_n(1) = checked_finite(point_cfg, "east", point_cfg.number("east", 0.0));
      point_wind_n(2) = checked_finite(point_cfg, "down", point_cfg.number("down", 0.0));
      if (!points_.empty() && altitude_m <= points_.back().altitude_m) {
        throw ConfigError(point_cfg.origin(), keyed_path(point_cfg, "altitude"),
                          "points must be strictly increasing by altitude -- entry " +
                              std::to_string(i) + "'s altitude does not exceed entry " +
                              std::to_string(i - 1) + "'s");
      }
      points_.push_back(Point{altitude_m, point_wind_n});
    }
    registry.add("wind.north", &wind_n_(0), "m/s");
    registry.add("wind.east", &wind_n_(1), "m/s");
    registry.add("wind.down", &wind_n_(2), "m/s");
    registry.add("wind.altitude", &altitude_, "m");
  }
  void reset() override {
    wind_n_.setZero();
    altitude_ = 0.0;
  }
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "wind"; }

  // The only place this plugin evaluates the table: a pure function of altitude_m, latched here for
  // wind_n() below to read (wind_model.hpp's own three-way split -- see this file's header for why
  // this plugin's own lack of frame-to-frame memory makes that split's shareability guarantee
  // hold).
  void begin_frame(const astra::StepContext& /*ctx*/, const astra::VehicleState& /*vs*/,
                   double altitude_m) noexcept override {
    altitude_ = altitude_m;
    wind_n_ = interpolate(altitude_m);
  }

  [[nodiscard]] Eigen::Vector3d wind_n(const astra::StepContext& /*ctx*/,
                                       const astra::VehicleState& /*vs*/) const noexcept override {
    return wind_n_;
  }

  // Never reads vs.p_n() -- only the Earth-derived altitude_m parameter begin_frame is handed --
  // so no Earth model's position representation can be mismatched here (wind_model.hpp).
  [[nodiscard]] astra::PositionRepresentation assumed_position_representation()
      const noexcept override {
    return astra::PositionRepresentation::kAny;
  }

  // Memoryless (this file's header): sharing it across more than one vehicle is exact, each
  // vehicle's begin_frame+wind_n pair evaluating the table fresh from that vehicle's own altitude.
  [[nodiscard]] bool shareable_across_vehicles() const noexcept override { return true; }

 private:
  static constexpr std::size_t kMinPoints = 2;

  // Piecewise-linear; clamps outside [points_.front(), points_.back()] rather than extrapolating
  // (this file's header). points_ is sorted strictly increasing by altitude (enforced at init), so
  // std::upper_bound finds the first point whose altitude exceeds altitude_m in O(log n).
  [[nodiscard]] Eigen::Vector3d interpolate(double altitude_m) const noexcept {
    if (altitude_m <= points_.front().altitude_m) {
      return points_.front().wind_n;
    }
    if (altitude_m >= points_.back().altitude_m) {
      return points_.back().wind_n;
    }
    const auto upper =
        std::upper_bound(points_.begin(), points_.end(), altitude_m,
                         [](double value, const Point& point) { return value < point.altitude_m; });
    const Point& hi = *upper;
    const Point& lo = *std::prev(upper);
    const double fraction = (altitude_m - lo.altitude_m) / (hi.altitude_m - lo.altitude_m);
    return lo.wind_n + fraction * (hi.wind_n - lo.wind_n);
  }

  std::vector<Point> points_;
  Eigen::Vector3d wind_n_ = Eigen::Vector3d::Zero();
  double altitude_ = 0.0;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<TableWind>(); }

constexpr astra::PluginDescriptor kDescriptor{"physical", "wind_table", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
