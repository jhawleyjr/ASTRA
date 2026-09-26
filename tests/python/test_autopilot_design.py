"""The autopilot's gain design (M4 PR 5, docs/plans/m4-pr5-autopilot.md D7; M4 plan Q13).

The gains analyzed here are the gains that ship: `autopilot_design.read_gains` parses
embedded/autopilot/gains.hpp. The plants are cruise.yaml's trim, 230 m/s at 10 km, linearized by
`astra.linearize` (unchanged) with the altitude state PR 8's test builds, in `autopilot_design`.
This is the design model, NOT the acceptance oracle: it has no saturation and no turbulence, and
the nonlinear closed-loop runs in PR 6 (R2) are what decide whether the autopilot holds its bands.

Thresholds and their sources (docs/plans/m4-pr5-autopilot.md D7; Joe accepted the provisional ones
on 2026-09-26):
  - Loop-at-a-time phase margin >= 45 degrees and gain margin >= 6 dB, both directions. Source: the
    MIL-STD-1797A Appendix A bandwidth definition ("phase margin at least 45 deg and gain margin at
    least 6 dB"; docs/refs/textbooks/MIL-HDBK-1797, Appendix A "D. Bandwidth, Time Delay").
    PROVISIONAL: that text defines a pilot-in-the-loop handling-qualities bandwidth, not a flight-
    control stability-margin requirement. The conventional source, MIL-F-9490D, is not in docs/refs
    and is frequency-dependent, less strict below 0.06 Hz, so a flat 45 degrees / 6 dB is
    conservative there.
  - Damping ratio >= 0.30 on every closed-loop oscillatory pole, and largest real part <= -0.03
    1/s: ASTRA's own design targets, PROVISIONAL, above the specification lower bounds below.
  - Inner-loop separation (correction round 1; ASTRA's own criteria, PROVISIONAL): the bank-attitude
    loop alone (heading loop open) has every pole at Re <= -0.5 1/s (time constant <= 2 s), and its
    bandwidth is at least 3x the heading loop's; the same 3x separation is asserted for the pitch
    loop's short-period pair against the altitude and speed loops. kBankP leaves at least 40
    percent of the aileron limit as headroom at a full-scale bank error.
  - Yaw damper sign (round 1): the closed-loop effective Cn_beta is not below the open-loop
    cn_beta (aero_math.hpp's N = q S b (cn_beta beta + cn_rudder delta_r), cruise.yaml's values).
  - MIL-F-8785C (docs/refs/MIL-F-8785C.pdf), Category B, Level 1, read from the page images, not
    the OCR text: phugoid zeta >= 0.04 (3.2.1.2, p. 12); short-period zeta 0.30 to 2.00 (3.2.2.1.2,
    Table IV, p. 13); Dutch roll zeta >= 0.08, zeta*wn >= 0.15 rad/s, wn >= 0.4 rad/s (3.3.1.1,
    Table VI, p. 22; Class III may be excepted from the wn minimum).
  - Helper cross-check: PR 8's recorded 5x5 phugoid (PLAN.md: 108.5 s, zeta 0.027;
    docs/plans/pr8-cruise-regression.md: 108.53 s and 0.0273), within 0.5 % in period and 0.002 in
    damping. PR 8 itself agrees to 0.04 % / 0.0002, so this catches a broken helper without being
    brittle to a rebuild.
"""

from __future__ import annotations

import math
from pathlib import Path

import numpy as np
import pytest

import autopilot_design as design

CRUISE_CONFIG = "aircraft/notional_transport/configs/cruise.yaml"
TRUE_AIRSPEED_MPS = 230.0
ALTITUDE_M = 10_000.0

PHASE_MARGIN_MIN_DEG = 45.0
GAIN_MARGIN_MIN_DB = 6.0
DAMPING_FLOOR = 0.30
REAL_PART_CEILING = -0.03  # 1/s

# MIL-F-8785C, Category B, Level 1.
PHUGOID_ZETA_MIN = 0.04
SHORT_PERIOD_ZETA_RANGE = (0.30, 2.00)
DUTCH_ROLL_ZETA_MIN = 0.08
DUTCH_ROLL_ZETA_WN_MIN = 0.15
DUTCH_ROLL_WN_MIN = 0.4

# PR 8's recorded reference (PLAN.md).
BANK_LOOP_TIME_CONSTANT_MAX_S = 2.0
SEPARATION_RATIO_MIN = 3.0
AILERON_HEADROOM_FRACTION = 0.6  # kBankP * bank limit <= 60 % of the aileron limit

PR8_PERIOD_S = 108.5
PR8_ZETA = 0.027
PERIOD_RELATIVE_TOLERANCE = 0.005
ZETA_ABSOLUTE_TOLERANCE = 0.002


@pytest.fixture(scope="module")
def gains() -> dict[str, float]:
    return design.read_gains()


@pytest.fixture(scope="module")
def plants() -> design.Plants:
    return design.build_plants(CRUISE_CONFIG, TRUE_AIRSPEED_MPS, ALTITUDE_M)


@pytest.fixture(scope="module")
def longitudinal(plants: design.Plants, gains: dict[str, float]) -> design.Loop:
    return design.longitudinal_loop(plants, gains)


@pytest.fixture(scope="module")
def lateral(plants: design.Plants, gains: dict[str, float]) -> design.Loop:
    return design.lateral_loop(plants, gains)


def _poles(loop: design.Loop) -> np.ndarray:
    return np.linalg.eigvals(loop.closed())


# --- The design is the shipped design ---


def test_gains_file_is_read_strictly(tmp_path: Path) -> None:
    good = design.read_gains()
    allowed = set(design.GAIN_NAMES) | set(design.OPTIONAL_GAIN_NAMES)
    assert set(design.GAIN_NAMES) <= set(good) <= allowed
    text = design.GAINS_HPP.read_text(encoding="utf-8")

    def written(content: str) -> Path:
        path = tmp_path / "gains.hpp"
        path.write_text(content, encoding="utf-8")
        return path

    dropped = "\n".join(
        line for line in text.splitlines() if not line.startswith("constexpr double kAltP ")
    )
    with pytest.raises(ValueError, match="missing"):
        design.read_gains(written(dropped))
    with pytest.raises(ValueError, match="extra"):
        design.read_gains(written(text + "constexpr double kExtra = 1.0;\n"))
    with pytest.raises(ValueError, match="twice"):
        design.read_gains(written(text + "constexpr double kAltP = 1.0;\n"))
    with pytest.raises(ValueError, match="plain"):
        design.read_gains(written(text + "constexpr double kBad = 2.0 * 3.0;\n"))


# --- Helper cross-check against PR 8's recorded reference ---


def test_helper_reproduces_the_recorded_pr8_phugoid(plants: design.Plants) -> None:
    phugoid = design.open_loop_phugoid(plants.a_lon)
    print(
        f"open-loop 5x5 phugoid: period {phugoid.damped_period_s:.4f} s, "
        f"zeta {phugoid.damping_ratio:.5f}"
    )
    assert abs(phugoid.damped_period_s / PR8_PERIOD_S - 1.0) < PERIOD_RELATIVE_TOLERANCE
    assert abs(phugoid.damping_ratio - PR8_ZETA) < ZETA_ABSOLUTE_TOLERANCE


# --- Closed-loop stability, damping, and the specification lower bounds ---


@pytest.mark.parametrize("which", ["longitudinal", "lateral"])
def test_closed_loop_meets_the_pole_floors(which: str, request: pytest.FixtureRequest) -> None:
    poles = _poles(request.getfixturevalue(which))
    print(f"{which} poles: {np.sort_complex(poles)}")
    assert float(np.max(np.real(poles))) <= REAL_PART_CEILING
    assert design.min_damping_ratio(poles) >= DAMPING_FLOOR


def test_longitudinal_modes_meet_mil_f_8785c_level_1(longitudinal: design.Loop) -> None:
    poles = _poles(longitudinal)
    oscillatory = sorted((complex(p) for p in poles if p.imag > 1e-6), key=abs)
    assert oscillatory, "no oscillatory longitudinal pole"
    short_period = oscillatory[-1]  # the fastest oscillatory pair
    zeta_sp = -short_period.real / abs(short_period)
    assert SHORT_PERIOD_ZETA_RANGE[0] <= zeta_sp <= SHORT_PERIOD_ZETA_RANGE[1]
    for pole in oscillatory[:-1]:  # any slower oscillation is a phugoid-type mode
        assert -pole.real / abs(pole) >= PHUGOID_ZETA_MIN


def test_dutch_roll_meets_mil_f_8785c_level_1(lateral: design.Loop) -> None:
    poles = _poles(lateral)
    oscillatory = sorted((complex(p) for p in poles if p.imag > 1e-6), key=abs)
    assert oscillatory, "no oscillatory lateral pole"
    dutch_roll = oscillatory[-1]  # the fastest oscillatory pair
    wn = abs(dutch_roll)
    zeta = -dutch_roll.real / wn
    print(f"Dutch roll: wn {wn:.4f} rad/s, zeta {zeta:.4f}, zeta*wn {zeta * wn:.4f}")
    assert zeta >= DUTCH_ROLL_ZETA_MIN
    assert zeta * wn >= DUTCH_ROLL_ZETA_WN_MIN
    assert wn >= DUTCH_ROLL_WN_MIN


# --- Loop-at-a-time margins at each actuator input ---


@pytest.mark.parametrize("which", ["longitudinal", "lateral"])
def test_loop_at_a_time_margins_at_each_actuator_input(
    which: str, request: pytest.FixtureRequest
) -> None:
    loop: design.Loop = request.getfixturevalue(which)
    for channel, margin in design.loop_margins(loop).items():
        print(
            f"{channel}: phase margin {margin.phase_margin_deg:.1f} deg, gain margin "
            f"+{margin.gain_increase_db:.1f} / -{margin.gain_decrease_db:.1f} dB"
        )
        assert margin.phase_margin_deg >= PHASE_MARGIN_MIN_DEG, channel
        assert margin.gain_increase_db >= GAIN_MARGIN_MIN_DB, channel
        assert margin.gain_decrease_db >= GAIN_MARGIN_MIN_DB, channel


def test_margin_analysis_can_fail(longitudinal: design.Loop) -> None:
    # The margin machinery is live: with the elevator loop's gains cut to a tenth the closed loop
    # is still stable (the pole test would pass) but its phase margin collapses below the floor, so
    # the assertion above can fire. (A sign-flipped loop is unstable, which the pole test catches;
    # margins say nothing about a loop that is unstable when closed, so they are asserted
    # alongside stability.)
    weak = design.Loop(
        longitudinal.az,
        longitudinal.bz,
        np.vstack([0.1 * longitudinal.cc[0], longitudinal.cc[1]]),
        longitudinal.n_plant,
        longitudinal.channels,
    )
    assert float(np.max(np.real(np.linalg.eigvals(weak.closed())))) < 0.0
    margin = design.loop_margins(weak)["elevator"]
    assert margin.phase_margin_deg < PHASE_MARGIN_MIN_DEG


# --- The sample-and-hold closed loop, as the sim applies commands ---


@pytest.mark.parametrize("which", ["longitudinal", "lateral"])
def test_exchange_rate_sample_and_hold_closed_loop_is_stable(
    which: str, request: pytest.FixtureRequest
) -> None:
    # Acceptance: zero-order hold at the exchange period, command applied in the frame it is
    # computed
    # (architecture section 5, ADR-0022: zero compute delay), no extra delay.
    loop: design.Loop = request.getfixturevalue(which)
    radius = float(np.max(np.abs(np.linalg.eigvals(design.sampled_closed_loop(loop)))))
    print(f"{which} sampled closed loop, max |z| = {radius:.6f}")
    assert radius < 1.0


@pytest.mark.parametrize("which", ["longitudinal", "lateral"])
def test_one_extra_exchange_of_delay_is_reported(
    which: str, request: pytest.FixtureRequest
) -> None:
    # Conservative variant, reported, not the acceptance: what one more exchange (10 ms) of delay
    # would do.
    loop: design.Loop = request.getfixturevalue(which)
    radius = float(np.max(np.abs(np.linalg.eigvals(design.sampled_closed_loop(loop, delay=True)))))
    print(f"{which} sampled closed loop with one extra exchange of delay, max |z| = {radius:.6f}")
    assert math.isfinite(radius)


def test_sampled_model_matches_the_continuous_one_at_a_short_period(
    longitudinal: design.Loop,
) -> None:
    # A consistency check on the discretization: as the period shrinks, log(z) / T approaches the
    # continuous closed-loop poles, so the sampled model is the same plant and law.
    continuous = np.sort_complex(_poles(longitudinal))
    tiny = 1e-4
    sampled = np.linalg.eigvals(design.sampled_closed_loop(longitudinal, period_s=tiny))
    approx = np.sort_complex(np.log(sampled.astype(complex)) / tiny)
    assert np.allclose(continuous, approx, rtol=2e-2, atol=2e-3)


# --- The yaw damper's sign (correction round 1, finding F1) ---


def test_closed_loop_effective_cn_beta_is_not_below_open_loop(gains: dict[str, float]) -> None:
    # aero_math.hpp: N = q S b (cn_beta beta + cn_rudder delta_r + ...); cruise.yaml cn_beta =
    # +0.26, cn_rudder = -0.20 (positive rudder is nose-left, a negative moment). A rudder law with
    # a beta term delta_r = k beta has effective cn_beta = cn_beta + cn_rudder k; the shipped law
    # has no beta term.
    cn_beta, cn_rudder = design.aero_yaw_coefficients(CRUISE_CONFIG)
    assert cn_beta > 0.0 > cn_rudder
    effective = design.effective_cn_beta(cn_beta, cn_rudder, gains.get("kSideslip", 0.0))
    print(f"cn_beta {cn_beta}, cn_rudder {cn_rudder}, effective cn_beta {effective}")
    assert effective >= cn_beta


def test_effective_cn_beta_check_can_fail() -> None:
    # The defect round 1 found: kSideslip = +0.269 (rudder nose-left for beta > 0) takes 0.26 to
    # 0.206.
    cn_beta, cn_rudder = design.aero_yaw_coefficients(CRUISE_CONFIG)
    assert design.effective_cn_beta(cn_beta, cn_rudder, 0.269) < cn_beta
    assert design.effective_cn_beta(cn_beta, cn_rudder, -0.269) > cn_beta  # the stiffening sign


# --- Inner-loop separation (correction round 1, finding F2) ---


def test_bank_loop_is_fast_and_separated_from_the_heading_loop(
    plants: design.Plants, gains: dict[str, float], lateral: design.Loop
) -> None:
    bank = design.bank_loop_poles(plants, gains)
    heading = design.slowest_poles(_poles(lateral), 2)
    bank_bandwidth = float(np.min(-np.real(bank)))  # the slowest bank-loop pole
    heading_bandwidth = max(abs(p) for p in heading)
    print(f"bank-loop poles (heading loop open): {np.sort_complex(bank)}")
    print(f"heading-loop poles: {heading}")
    print(
        f"bank time constant {1.0 / bank_bandwidth:.2f} s, bank/heading bandwidth ratio "
        f"{bank_bandwidth / heading_bandwidth:.1f}"
    )
    assert float(np.max(np.real(bank))) < 0.0
    assert 1.0 / bank_bandwidth <= BANK_LOOP_TIME_CONSTANT_MAX_S
    assert bank_bandwidth >= SEPARATION_RATIO_MIN * heading_bandwidth


def test_pitch_loop_is_separated_from_the_altitude_and_speed_loops(
    plants: design.Plants, gains: dict[str, float], longitudinal: design.Loop
) -> None:
    inner = design.pitch_loop_poles(plants, gains)
    short_period = design.fastest_oscillatory_pair(inner)
    full = _poles(longitudinal)
    fast = design.fastest_oscillatory_pair(full)
    outer = [complex(p) for p in full if abs(p - fast) > 1e-9 and abs(p - fast.conjugate()) > 1e-9]
    ratio = abs(short_period) / max(abs(p) for p in outer)
    print(f"pitch-loop poles (altitude and speed loops open): {np.sort_complex(inner)}")
    print(f"outer-loop poles: {sorted(outer, key=abs)}")
    print(
        f"pitch short period wn {abs(short_period):.3f} rad/s (Re {short_period.real:.3f}), "
        f"pitch/outer bandwidth ratio {ratio:.1f}"
    )
    assert -short_period.real >= 1.0 / BANK_LOOP_TIME_CONSTANT_MAX_S
    assert ratio >= SEPARATION_RATIO_MIN


def test_bank_loop_leaves_aileron_headroom_at_a_full_scale_bank_error(
    gains: dict[str, float],
) -> None:
    limits = design.read_limits()
    bank_hi = limits["kBankCommandLimit"][1]
    aileron_hi = limits["kAileronLimit"][1]
    demand = gains["kBankP"] * bank_hi
    print(
        f"aileron demand at a full-scale bank error {demand:.3f} rad of {aileron_hi} "
        f"({demand / aileron_hi:.0%}); the bank command saturates at a heading error of "
        f"{math.degrees(bank_hi / gains['kHeadingP']):.1f} degrees"
    )
    assert demand <= AILERON_HEADROOM_FRACTION * aileron_hi
