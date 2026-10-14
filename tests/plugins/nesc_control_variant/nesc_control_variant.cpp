// A test-only variant of the F-16's sim-side F16_control peer (M7 PR 11a,
// docs/plans/m7-pr11a-control-peer.md, Q7 and Joe's amendment 1). It holds the shipped peer's class
// from its header, aircraft/f16/sim/transport/nesc_control/nesc_control_peer.hpp, and never links
// or loads the shipped plugin. The shipped peer carries no variant or test key; this plugin reads
// one, `transport.peer.test_variant`, and changes exactly one thing:
// - the negative controls' plants, applied to the shipped law's reply (Q7):
//   - elevator_sign: the reply's elevator negated, a sign slip at the law's boundary;
//   - aileron_sign: the reply's aileron negated, the boundary's +LWD-to-+RWD flip omitted (after
//     the interconnect, which still reads the +LWD aileron);
//   - one_exchange_delay: each reply carries the previous exchange's commands (R22's plant);
// - the reported variants, never selected (the M7 plan's R6):
//   - dml_references: the DML's hard-coded LQR references in place of the trim's (the M7 plan's
//     Q11 as amended 2026-10-13 after the measurement: the decided references are the trim's, SIM
//     5's choice, and the DML's, the first decided choice, are reported);
//   - rhumb_line, great_circle: 13.4's base course as the rhumb line, or the WGS-84 geodesic at h0,
//     through the base point at the course command (Q27);
//   - line_at_offset_frame: the decided initial-plane line, its base point latched from the lateral
//     offset's frame instead of frame 0 (Q27).
// tests/plugins is skipped by scripts/config_keys.py, so its keys are not documented there.
//
// Test code: the great circle's polyline is allocated when the base point latches, inside a
// per-frame call; the shipped peer allocates nothing there.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "../../../aircraft/f16/sim/transport/nesc_control/nesc_control_peer.hpp"
#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/util/geodesy.hpp"
#include "icd/astra_icd.h"

namespace {

namespace peer = astra::aircraft_f16_nesc_control;
namespace law = astra::aircraft_f16_control_law;
namespace geodesy = astra::geodesy;

enum class Variant : std::uint8_t {
  kElevatorSign,
  kAileronSign,
  kOneExchangeDelay,
  kDmlReferences,
  kRhumbLine,
  kGreatCircle,
  kLineAtOffsetFrame
};

Variant variant_of(const astra::Config& peer_block) {
  const std::string name = peer_block.text("test_variant");
  if (name == "elevator_sign") {
    return Variant::kElevatorSign;
  }
  if (name == "aileron_sign") {
    return Variant::kAileronSign;
  }
  if (name == "one_exchange_delay") {
    return Variant::kOneExchangeDelay;
  }
  if (name == "dml_references") {
    return Variant::kDmlReferences;
  }
  if (name == "rhumb_line") {
    return Variant::kRhumbLine;
  }
  if (name == "great_circle") {
    return Variant::kGreatCircle;
  }
  if (name == "line_at_offset_frame") {
    return Variant::kLineAtOffsetFrame;
  }
  throw astra::ConfigError(peer_block.origin(), peer_block.path() + ".test_variant",
                           "unknown test variant: " + name);
}

// WGS-84's isometric latitude (the Mercator ordinate), for the rhumb line.
double isometric_latitude(double lat) {
  const double e = std::sqrt(geodesy::kEccentricitySquared);
  const double s = e * std::sin(lat);
  return std::log(std::tan((std::numbers::pi / 4.0) + (lat / 2.0)) *
                  std::pow((1.0 - s) / (1.0 + s), e / 2.0));
}

class VariantPeer final : public peer::NescControlPeer {
 public:
  void init(const astra::Config& config, astra::SignalRegistry& registry) override {
    const astra::Config block = config.at("peer");
    variant_ = variant_of(block);
    peer::Options options;
    options.dml_references = variant_ == Variant::kDmlReferences;
    options.latch_at_offset_frame = variant_ == Variant::kLineAtOffsetFrame;
    set_options(options);
    NescControlPeer::init(config, registry);
  }

  void read_reply(astra_sw_to_sim_v1& reply) noexcept override {
    NescControlPeer::read_reply(reply);
    switch (variant_) {
      case Variant::kElevatorSign:
        reply.elevator = -reply.elevator;
        break;
      case Variant::kAileronSign:
        reply.aileron = -reply.aileron;
        break;
      case Variant::kOneExchangeDelay: {
        const astra_sw_to_sim_v1 current = reply;
        if (have_previous_) {
          reply.elevator = previous_.elevator;
          reply.aileron = previous_.aileron;
          reply.rudder = previous_.rudder;
          reply.throttle[0] = previous_.throttle[0];
          reply.throttle[1] = previous_.throttle[1];
        }
        previous_ = current;
        have_previous_ = true;
        break;
      }
      default:
        break;
    }
  }

 protected:
  [[nodiscard]] double cross_track_at(const law::BasePoint& base, double chi, double lat,
                                      double lon) const noexcept override {
    if (variant_ == Variant::kRhumbLine) {
      const double lon_r =
          base.lon0 + (std::tan(chi) * (isometric_latitude(lat) - isometric_latitude(base.lat0)));
      const double east =
          (lon - lon_r) * (geodesy::prime_vertical_radius(lat) + base.h0) * std::cos(lat);
      return east * std::cos(chi);
    }
    if (variant_ == Variant::kGreatCircle) {
      return geodesic_cross_track(base, lat, lon);
    }
    return peer::NescControlPeer::cross_track_at(base, chi, lat, lon);
  }

  void on_base_latched(const law::BasePoint& base, double chi) noexcept override {
    if (variant_ != Variant::kGreatCircle) {
      return;
    }
    // The geodesic at h0 through the base point at azimuth chi, by RK4 in arc length at 1 m steps
    // over 30 km: dlat/ds = cos a / (M + h0), dlon/ds = sin a / ((N + h0) cos lat), da/ds = sin a
    // tan lat / (N + h0).
    const auto slope = [&base](const Point& y) {
      const double m = geodesy::meridian_radius(y.lat) + base.h0;
      const double n = geodesy::prime_vertical_radius(y.lat) + base.h0;
      return Point{std::cos(y.azimuth) / m, std::sin(y.azimuth) / (n * std::cos(y.lat)),
                   std::sin(y.azimuth) * std::tan(y.lat) / n};
    };
    const auto plus = [](const Point& a, const Point& b, double h) {
      return Point{a.lat + (h * b.lat), a.lon + (h * b.lon), a.azimuth + (h * b.azimuth)};
    };
    constexpr std::size_t kPoints = 30001;
    constexpr double kStep = 1.0;
    polyline_.clear();
    polyline_.reserve(kPoints);
    Point y{base.lat0, base.lon0, chi};
    for (std::size_t i = 0; i < kPoints; ++i) {
      polyline_.push_back(y);
      const Point k1 = slope(y);
      const Point k2 = slope(plus(y, k1, kStep / 2.0));
      const Point k3 = slope(plus(y, k2, kStep / 2.0));
      const Point k4 = slope(plus(y, k3, kStep));
      y = Point{y.lat + (kStep / 6.0 * (k1.lat + (2.0 * k2.lat) + (2.0 * k3.lat) + k4.lat)),
                y.lon + (kStep / 6.0 * (k1.lon + (2.0 * k2.lon) + (2.0 * k3.lon) + k4.lon)),
                y.azimuth + (kStep / 6.0 *
                             (k1.azimuth + (2.0 * k2.azimuth) + (2.0 * k3.azimuth) + k4.azimuth))};
    }
    hint_ = 0;
  }

 private:
  struct Point {
    double lat;
    double lon;
    double azimuth;
  };

  // The local offset from the polyline's nearest point, positive to the right of the azimuth.
  [[nodiscard]] double geodesic_cross_track(const law::BasePoint& base, double lat,
                                            double lon) const noexcept {
    if (polyline_.empty()) {
      return 0.0;
    }
    const auto along_and_cross = [&](std::size_t i) {
      const Point& p = polyline_[i];
      const double dn = (lat - p.lat) * (geodesy::meridian_radius(p.lat) + base.h0);
      const double de =
          (lon - p.lon) * (geodesy::prime_vertical_radius(p.lat) + base.h0) * std::cos(p.lat);
      return std::pair<double, double>{(dn * std::cos(p.azimuth)) + (de * std::sin(p.azimuth)),
                                       (-dn * std::sin(p.azimuth)) + (de * std::cos(p.azimuth))};
    };
    // Walk from the last nearest point to the one whose along-track offset is smallest.
    std::size_t i = std::min(hint_, polyline_.size() - 1);
    for (;;) {
      const double along = along_and_cross(i).first;
      if (along > 0.5 && i + 1 < polyline_.size()) {
        ++i;
      } else if (along < -0.5 && i > 0) {
        --i;
      } else {
        break;
      }
    }
    hint_ = i;
    return along_and_cross(i).second;
  }

  Variant variant_ = Variant::kElevatorSign;
  astra_sw_to_sim_v1 previous_{};
  bool have_previous_ = false;
  std::vector<Point> polyline_;
  mutable std::size_t hint_ = 0;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<VariantPeer>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "nesc_control_variant",
                                              astra::kCoreApiVersion, astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
