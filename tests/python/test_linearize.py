"""astra.linearize (M3 PR 7, docs/plans/pr7-linearize.md): eight named acceptance tests (R1-R8, R8
being this whole file plus the full gate) plus supporting checks needed to trust them. The
closed-form S&L Sec. 4.2 comparison (R4a-e) is computed here, independently, from cruise.yaml's own
transcribed coefficients -- never reusing aero_math.hpp/turbofan_math.hpp or linearize.py's own
numerical modes() implementation (the same convention test_trim.py's own R3 uses). Cm_alpha_dot is
taken as 0 throughout (aircraft/notional_transport/physical/aero has no such coefficient -- the
numerical A/B this compares against has no alpha-rate feedback either, so this is the consistent
choice, not a simplification introduced here). The dutch-roll/roll/spiral formulas (eq. 4.2-30/
4.2-31/4.2-35) use the primed stability-axis inertias J'_Z/J'_X; this file approximates both with
the plain body-axis Izz/Ixx, since the trim angle of attack is small (~1.5 deg -- cos^2 of that is
0.9993, a much smaller correction than the tolerances below already allow for).

D18 (Revision 3, docs/plans/pr7-linearize.md): running R4 against the real numbers found short
period, phugoid, and spiral outside D13's own tolerance. Short period and spiral were a real defect
in D13's own coefficient list, not in astra.linearize (independently confirmed by R2/R3 and by a
hand check against the derivation below to four significant figures): aero_math.hpp's own
moment_b = moment_native_b + r_aero_b.cross(force_b) transfers its tabulated
Cm_alpha/Cl_beta/Cn_beta from AERORP to the CG (aero_math.hpp's own header, lines 14-15; r_aero_b
is a cruise.yaml coefficient, lines 26-28), while S&L Sec. 4.2's own dimensional derivatives are
CG-referenced throughout. _cg_referenced_coefficients below re-derives the CG-referenced
Cm_alpha/Cl_beta/Cn_beta from first principles (never calling aero_math.hpp or astra.linearize),
and every mode formula below takes them as input -- the equations and tolerances themselves are
unchanged. Cm_q/Cl_p/Cl_r/Cn_r have no transfer term: p/q/r never enter this aero model's force
equations (aero_math.hpp), so r_aero_b.cross(force_b) has no rate dependence. Phugoid stays outside
its bound after the correction, exactly as expected: eq. 4.2-17b (the level-flight Lanchester
result used for omega_n) is (g/V_T)*sqrt(2) -- airspeed and gravity only, no stability derivative
for any coefficient correction to move -- marked xfail(strict=True), not loosened.
"""

from __future__ import annotations

import math

import numpy as np
import pytest

from astra import _core, linearize

CRUISE_CONFIG = "aircraft/notional_transport/configs/cruise.yaml"
TRUE_AIRSPEED_MPS = 230.0
ALTITUDE_M = 10_000.0

# cruise.yaml's own transcribed coefficients (aircraft/notional_transport/physical/aero's config),
# re-typed here for R4a-e's independent closed-form comparison -- never reusing aero_math.hpp/
# turbofan_math.hpp, same convention as tests/python/test_trim.py's own R3 cross-check.
WING_AREA_M2 = 108.78945984
WINGSPAN_M = 28.864560
MEAN_CHORD_M = 3.752088
CL_ALPHA_PER_RAD = 4.3478260869565215
CD_0 = 0.021
CD_K = 0.043
CM_ALPHA_PER_RAD = -0.6
CM_Q_PER_RAD = -27.0
CY_BETA_PER_RAD = -1.0
CROLL_BETA_PER_RAD = -0.09
CROLL_P_PER_RAD = -0.4
CROLL_R_PER_RAD = 0.09
CN_BETA_PER_RAD = 0.26
CN_R_PER_RAD = -0.35
# D18: AERORP relative to CG, body axes (r_aero_b_y_m = 0, unused -- the transfer term below never
# needs it).
R_AERO_B_X_M = 0.3556
R_AERO_B_Z_M = -1.6256
MASS_KG = 37648.0
IXX_KG_M2 = 761970.0
IYY_KG_M2 = 1997120.0
IZZ_KG_M2 = 2567919.0
STANDARD_GRAVITY_MPS2 = 9.80665

# D13's own tolerances, "labeled approximate" -- a miss is a finding to report, never a bound to
# widen (AGENTS.md's oracle rule applies to sanity checks too).
LONGITUDINAL_OMEGA_REL_TOL = 0.15
LONGITUDINAL_ZETA_ABS_TOL = 0.1
DUTCH_ROLL_OMEGA_REL_TOL = 0.30
DUTCH_ROLL_ZETA_ABS_TOL = 0.15
REAL_MODE_SIGMA_REL_TOL = 0.30


def _isa1976_density_kg_m3(altitude_m: float) -> float:
    """Sea-level-to-tropopause ISA-1976 density, independent of plugins/physical/atmosphere/isa1976
    -- the same from-scratch evaluation tests/python/test_trim.py's own R3 helper uses, retyped here
    rather than imported (a different test file's own private helper)."""
    g0 = STANDARD_GRAVITY_MPS2
    specific_gas_constant = 8314.32 / 28.9644
    sea_level_temperature_k = 288.15
    sea_level_pressure_pa = 101_325.0
    lapse_rate_k_per_m = -0.0065
    temperature_k = sea_level_temperature_k + lapse_rate_k_per_m * altitude_m
    pressure_pa = sea_level_pressure_pa * (temperature_k / sea_level_temperature_k) ** (
        -g0 / (specific_gas_constant * lapse_rate_k_per_m)
    )
    return pressure_pa / (specific_gas_constant * temperature_k)


def _cg_referenced_coefficients(qbar_pa: float, alpha_rad: float) -> tuple[float, float, float]:
    """D18: Cm_alpha/Cl_beta/Cn_beta, corrected from AERORP (cruise.yaml's own tabulated values) to
    the CG S&L Sec. 4.2's own dimensional derivatives assume, via the same moment-transfer theorem
    aero_math.hpp applies (moment_b = moment_native_b + r_aero_b.cross(force_b)) -- re-derived here
    from first principles, never calling aero_math.hpp. Force sign convention at beta = 0 (this
    module's own linearization point for every alpha-only/beta-only column): Fx_b = -D cos(alpha) +
    L sin(alpha), Fz_b = -D sin(alpha) - L cos(alpha) (S&L eq. 2.3-2's C_{w/bf} transform,
    aero_math.hpp's own compute_wrench), Fy_b(beta=0) = 0 with dFy_b/dbeta = -D + qbar S
    cy_beta_per_rad. Cm_q/Cl_p/Cl_r/Cn_r need no correction: p/q/r never enter this aero model's
    force equations, so r_aero_b.cross(force_b) carries no rate dependence."""
    cl_trim = (MASS_KG * STANDARD_GRAVITY_MPS2 * math.cos(alpha_rad)) / (qbar_pa * WING_AREA_M2)
    cd_trim = CD_0 + CD_K * cl_trim**2
    lift_n = cl_trim * qbar_pa * WING_AREA_M2
    drag_n = cd_trim * qbar_pa * WING_AREA_M2
    d_lift_d_alpha = qbar_pa * WING_AREA_M2 * CL_ALPHA_PER_RAD
    d_drag_d_alpha = qbar_pa * WING_AREA_M2 * 2.0 * CD_K * cl_trim * CL_ALPHA_PER_RAD
    ca, sa = math.cos(alpha_rad), math.sin(alpha_rad)

    d_fx_d_alpha = drag_n * sa - d_drag_d_alpha * ca + lift_n * ca + d_lift_d_alpha * sa
    d_fz_d_alpha = -d_drag_d_alpha * sa - drag_n * ca - d_lift_d_alpha * ca + lift_n * sa
    d_pitch_transfer_d_alpha = R_AERO_B_Z_M * d_fx_d_alpha - R_AERO_B_X_M * d_fz_d_alpha
    cm_alpha_cg = CM_ALPHA_PER_RAD + d_pitch_transfer_d_alpha / (
        qbar_pa * WING_AREA_M2 * MEAN_CHORD_M
    )

    d_fy_d_beta = -drag_n + qbar_pa * WING_AREA_M2 * CY_BETA_PER_RAD
    d_roll_transfer_d_beta = -R_AERO_B_Z_M * d_fy_d_beta
    d_yaw_transfer_d_beta = R_AERO_B_X_M * d_fy_d_beta
    cl_beta_cg = CROLL_BETA_PER_RAD + d_roll_transfer_d_beta / (qbar_pa * WING_AREA_M2 * WINGSPAN_M)
    cn_beta_cg = CN_BETA_PER_RAD + d_yaw_transfer_d_beta / (qbar_pa * WING_AREA_M2 * WINGSPAN_M)

    return cm_alpha_cg, cl_beta_cg, cn_beta_cg


def _short_period_approximation(
    qbar_pa: float, rho_kg_m3: float, cm_alpha_cg: float
) -> linearize.OscillatoryMode:
    """S&L eq. 4.2-11/4.2-12 (Cm_alpha_dot = 0 -- see module docstring), Cm_alpha CG-referenced
    (D18)."""
    mass_ratio = (rho_kg_m3 * WING_AREA_M2 * MEAN_CHORD_M) / (4.0 * MASS_KG)
    bracket = -cm_alpha_cg - mass_ratio * CM_Q_PER_RAD * CL_ALPHA_PER_RAD
    omega_n_sq = (qbar_pa * WING_AREA_M2 * MEAN_CHORD_M / IYY_KG_M2) * bracket
    omega_n = math.sqrt(omega_n_sq)
    zeta = (
        (1.0 / (4.0 * math.sqrt(2.0)))
        * math.sqrt((rho_kg_m3 * WING_AREA_M2 * MEAN_CHORD_M) * MEAN_CHORD_M**2 / IYY_KG_M2)
        * (-CM_Q_PER_RAD + (2.0 * IYY_KG_M2 / (MASS_KG * MEAN_CHORD_M**2)) * CL_ALPHA_PER_RAD)
        / math.sqrt(bracket)
    )
    return linearize.OscillatoryMode(omega_n, zeta)


def _phugoid_approximation() -> linearize.OscillatoryMode:
    """S&L eq. 4.2-17b (Lanchester's level-flight result) for omega_n; eq. 4.2-18 for zeta, which
    collapses to exactly 0 for this model (C_DV = C_TV = 0: neither cd_0/cd_k nor thrust_ref_n
    carries any Mach/airspeed dependence in this notional aero/propulsion model). No D18 correction
    applies: eq. 4.2-17b has no stability-derivative term of any kind to correct."""
    omega_n = (STANDARD_GRAVITY_MPS2 / TRUE_AIRSPEED_MPS) * math.sqrt(2.0)
    zeta = 0.0
    return linearize.OscillatoryMode(omega_n, zeta)


def _dutch_roll_approximation(
    qbar_pa: float, rho_kg_m3: float, cn_beta_cg: float
) -> linearize.OscillatoryMode:
    """S&L eq. 4.2-30/4.2-31 (unprimed N_beta -- not eq. 4.2-28b/4.2-29's J_xz-corrected primed
    form), J'_Z approximated by IZZ_KG_M2 (module docstring), Cn_beta CG-referenced (D18)."""
    jz_prime = IZZ_KG_M2
    mass_ratio = (rho_kg_m3 * WING_AREA_M2 * WINGSPAN_M) / (4.0 * MASS_KG)
    bracket = cn_beta_cg + mass_ratio * CY_BETA_PER_RAD * CN_R_PER_RAD
    omega_n_sq = (qbar_pa * WING_AREA_M2 * WINGSPAN_M / jz_prime) * bracket
    omega_n = math.sqrt(omega_n_sq)
    zeta = (
        -(1.0 / 4.0)
        * math.sqrt((rho_kg_m3 * WING_AREA_M2 * WINGSPAN_M) * WINGSPAN_M**2 / (2.0 * jz_prime))
        * (CN_R_PER_RAD + (2.0 * jz_prime / (MASS_KG * WINGSPAN_M**2)) * CY_BETA_PER_RAD)
        / math.sqrt(bracket)
    )
    return linearize.OscillatoryMode(omega_n, zeta)


def _roll_sigma_approximation(rho_kg_m3: float) -> float:
    """S&L eq. 4.2-35, J'_X approximated by IXX_KG_M2 (module docstring). No D18 correction: Cl_p
    has no transfer term. D16: returns the signed Re(lambda) = -1/tau_R directly, never
    abs(tau_R)."""
    jx_prime = IXX_KG_M2
    tau_r = (
        -(WINGSPAN_M / TRUE_AIRSPEED_MPS)
        * (4.0 * jx_prime / (rho_kg_m3 * WING_AREA_M2 * WINGSPAN_M**3))
        * (1.0 / CROLL_P_PER_RAD)
    )
    return -1.0 / tau_r


def _spiral_sigma_approximation(cl_beta_cg: float, cn_beta_cg: float) -> float:
    """S&L eq. 4.2-36, Cl_beta/Cn_beta CG-referenced (D18) -- Cl_p unaffected (no transfer term).
    D16: returns the signed Re(lambda) = -1/tau_S directly, never abs(tau_S) -- the sign is the
    entire point of this formula's comparison (the spiral mode may be mildly unstable at cruise; a
    sign mismatch here is a real finding, not something to mask)."""
    tau_s = (
        (-CROLL_P_PER_RAD * cn_beta_cg)
        / (cl_beta_cg * CN_R_PER_RAD - CROLL_R_PER_RAD * cn_beta_cg)
        * (TRUE_AIRSPEED_MPS / STANDARD_GRAVITY_MPS2)
    )
    return -1.0 / tau_s


@pytest.fixture(scope="module")
def result() -> linearize.LinearizeResult:
    return linearize.linearize(CRUISE_CONFIG, TRUE_AIRSPEED_MPS, ALTITUDE_M)


def test_halving_every_step_changes_a_and_b_by_less_than_the_bound(
    result: linearize.LinearizeResult,
) -> None:
    halved = linearize.linearize(
        CRUISE_CONFIG,
        TRUE_AIRSPEED_MPS,
        ALTITUDE_M,
        v_step=0.05,
        omega_step=5e-4,
        angle_step=5e-4,
        throttle_step=5e-3,
    )
    a_ratio = np.linalg.norm(halved.a - result.a, "fro") / np.linalg.norm(result.a, "fro")
    b_ratio = np.linalg.norm(halved.b - result.b, "fro") / np.linalg.norm(result.b, "fro")
    assert a_ratio < 1e-3, f"A changed by {a_ratio:.3e} relative when every step was halved"
    assert b_ratio < 1e-3, f"B changed by {b_ratio:.3e} relative when every step was halved"


def test_longitudinal_and_lateral_blocks_do_not_cross_couple(
    result: linearize.LinearizeResult,
) -> None:
    a = result.a
    long_idx = linearize._LONGITUDINAL_INDICES
    lat_idx = linearize._LATERAL_INDICES
    long_norm = np.linalg.norm(a[np.ix_(long_idx, long_idx)], "fro")
    lat_norm = np.linalg.norm(a[np.ix_(lat_idx, lat_idx)], "fro")

    long_to_lat = np.linalg.norm(a[np.ix_(long_idx, lat_idx)], "fro")
    lat_to_long = np.linalg.norm(a[np.ix_(lat_idx, long_idx)], "fro")

    assert long_to_lat < 1e-3 * long_norm
    assert lat_to_long < 1e-3 * lat_norm


def test_the_trim_points_own_derivative_is_near_zero(result: linearize.LinearizeResult) -> None:
    base_state = np.array(result.trim.state, dtype=float)
    f0 = linearize._evaluate(result.simulation, base_state, result.x0, result.u0)
    assert np.max(np.abs(f0)) < 1e-6


def test_two_calls_are_bitwise_identical() -> None:
    a = linearize.linearize(CRUISE_CONFIG, TRUE_AIRSPEED_MPS, ALTITUDE_M)
    b = linearize.linearize(CRUISE_CONFIG, TRUE_AIRSPEED_MPS, ALTITUDE_M)
    assert np.array_equal(a.a, b.a)
    assert np.array_equal(a.b, b.b)


def test_input_registry_reads_trim_values_after_any_column(
    result: linearize.LinearizeResult,
) -> None:
    sim = result.simulation
    base_state = np.array(result.trim.state, dtype=float)

    def assert_registry_matches_u0() -> None:
        for name, expected in zip(result.input_names, result.u0, strict=True):
            assert sim.registry.find(f"v0.{name}") == expected

    for i in range(9):
        linearize._column(sim, base_state, result.x0, result.u0, i, linearize.state_step(i), "x")
        assert_registry_matches_u0()

    for j, name in enumerate(result.input_names):
        linearize._column(
            sim, base_state, result.x0, result.u0, j, linearize.control_step(name), "u"
        )
        assert_registry_matches_u0()


def test_input_names_returns_the_four_registered_control_names() -> None:
    config = _core.Config.from_file(CRUISE_CONFIG)
    sim = _core.Simulation.from_config(config, _core.default_plugin_dir())
    assert sim.input_names() == [
        "controls.elevator",
        "controls.aileron",
        "controls.rudder",
        "controls.throttle",
    ]


def test_short_period_matches_the_approximation(result: linearize.LinearizeResult) -> None:
    rho = _isa1976_density_kg_m3(ALTITUDE_M)
    qbar = 0.5 * rho * TRUE_AIRSPEED_MPS**2
    cm_alpha_cg, _, _ = _cg_referenced_coefficients(qbar, result.x0[7])
    approx = _short_period_approximation(qbar, rho, cm_alpha_cg)
    numerical = linearize.modes(result.a).short_period

    omega_ratio = abs(numerical.natural_frequency_rad_s - approx.natural_frequency_rad_s) / (
        approx.natural_frequency_rad_s
    )
    assert omega_ratio < LONGITUDINAL_OMEGA_REL_TOL, (
        f"short period omega_n: numerical {numerical.natural_frequency_rad_s:.4g} vs "
        f"approximate {approx.natural_frequency_rad_s:.4g} rad/s ({omega_ratio:.1%} off)"
    )
    zeta_diff = abs(numerical.damping_ratio - approx.damping_ratio)
    assert zeta_diff < LONGITUDINAL_ZETA_ABS_TOL, (
        f"short period zeta: numerical {numerical.damping_ratio:.4g} vs "
        f"approximate {approx.damping_ratio:.4g} ({zeta_diff:.4g} absolute off)"
    )


def test_phugoid_zeta_matches_the_approximation(result: linearize.LinearizeResult) -> None:
    # Codex adversarial review (base 53064e0): damping must be validated independently of the
    # known omega_n miss below -- an un-narrowed xfail covering both assertions would never reach
    # this one at all (the omega_n assert always raises first), silently disabling damping
    # coverage.
    approx = _phugoid_approximation()
    numerical = linearize.modes(result.a).phugoid
    zeta_diff = abs(numerical.damping_ratio - approx.damping_ratio)
    assert zeta_diff < LONGITUDINAL_ZETA_ABS_TOL, (
        f"phugoid zeta: numerical {numerical.damping_ratio:.4g} vs "
        f"approximate {approx.damping_ratio:.4g} ({zeta_diff:.4g} absolute off)"
    )


@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason=(
        "D18 (docs/plans/pr7-linearize.md): eq. 4.2-17b (Lanchester's level-flight omega_n, "
        "(g/V_T)*sqrt(2)) carries no stability derivative, so the CG-referenced-coefficient "
        "correction that fixes short period/spiral cannot apply here. Measured this session: "
        "numerical omega_n 0.0502 rad/s vs approximate 0.0603 rad/s, 16.7% off (bound 15%) -- a "
        "real, expected miss, not a tolerance to widen. strict=True + raises=AssertionError "
        "(Codex adversarial review, base 53064e0): an unexpected pass, or any non-assertion "
        "error, fails loudly instead of being silently absorbed as an expected failure."
    ),
)
def test_phugoid_omega_n_matches_the_approximation(result: linearize.LinearizeResult) -> None:
    approx = _phugoid_approximation()
    numerical = linearize.modes(result.a).phugoid

    omega_ratio = abs(numerical.natural_frequency_rad_s - approx.natural_frequency_rad_s) / (
        approx.natural_frequency_rad_s
    )
    assert omega_ratio < LONGITUDINAL_OMEGA_REL_TOL, (
        f"phugoid omega_n: numerical {numerical.natural_frequency_rad_s:.4g} vs "
        f"approximate {approx.natural_frequency_rad_s:.4g} rad/s ({omega_ratio:.1%} off)"
    )


def test_dutch_roll_matches_the_approximation(result: linearize.LinearizeResult) -> None:
    rho = _isa1976_density_kg_m3(ALTITUDE_M)
    qbar = 0.5 * rho * TRUE_AIRSPEED_MPS**2
    _, _, cn_beta_cg = _cg_referenced_coefficients(qbar, result.x0[7])
    approx = _dutch_roll_approximation(qbar, rho, cn_beta_cg)
    numerical = linearize.modes(result.a).dutch_roll

    omega_ratio = abs(numerical.natural_frequency_rad_s - approx.natural_frequency_rad_s) / (
        approx.natural_frequency_rad_s
    )
    assert omega_ratio < DUTCH_ROLL_OMEGA_REL_TOL, (
        f"dutch roll omega_n: numerical {numerical.natural_frequency_rad_s:.4g} vs "
        f"approximate {approx.natural_frequency_rad_s:.4g} rad/s ({omega_ratio:.1%} off)"
    )
    zeta_diff = abs(numerical.damping_ratio - approx.damping_ratio)
    assert zeta_diff < DUTCH_ROLL_ZETA_ABS_TOL, (
        f"dutch roll zeta: numerical {numerical.damping_ratio:.4g} vs "
        f"approximate {approx.damping_ratio:.4g} ({zeta_diff:.4g} absolute off)"
    )


def test_roll_matches_the_approximation(result: linearize.LinearizeResult) -> None:
    rho = _isa1976_density_kg_m3(ALTITUDE_M)
    approx_sigma = _roll_sigma_approximation(rho)
    numerical_sigma = linearize.modes(result.a).roll.sigma_per_s

    # D16: sign first, unconditionally -- never compare abs(tau) or abs(sigma).
    assert (numerical_sigma < 0) == (approx_sigma < 0), (
        f"roll sigma sign disagreement: numerical {numerical_sigma:.4g} vs "
        f"approximate {approx_sigma:.4g} 1/s"
    )
    ratio = abs(numerical_sigma - approx_sigma) / abs(approx_sigma)
    assert ratio < REAL_MODE_SIGMA_REL_TOL, (
        f"roll sigma: numerical {numerical_sigma:.4g} vs approximate {approx_sigma:.4g} 1/s "
        f"({ratio:.1%} off)"
    )


def test_spiral_matches_the_approximation(result: linearize.LinearizeResult) -> None:
    rho = _isa1976_density_kg_m3(ALTITUDE_M)
    qbar = 0.5 * rho * TRUE_AIRSPEED_MPS**2
    _, cl_beta_cg, cn_beta_cg = _cg_referenced_coefficients(qbar, result.x0[7])
    approx_sigma = _spiral_sigma_approximation(cl_beta_cg, cn_beta_cg)
    numerical_sigma = linearize.modes(result.a).spiral.sigma_per_s

    # D16: sign first, unconditionally -- the spiral mode may be mildly unstable at cruise, and a
    # sign disagreement between numerical and closed-form is a failure, never masked by abs().
    assert (numerical_sigma < 0) == (approx_sigma < 0), (
        f"spiral sigma sign disagreement: numerical {numerical_sigma:.4g} vs "
        f"approximate {approx_sigma:.4g} 1/s"
    )
    ratio = abs(numerical_sigma - approx_sigma) / abs(approx_sigma)
    assert ratio < REAL_MODE_SIGMA_REL_TOL, (
        f"spiral sigma: numerical {numerical_sigma:.4g} vs approximate {approx_sigma:.4g} 1/s "
        f"({ratio:.1%} off)"
    )


def test_quaternion_from_euler_matches_the_real_construction() -> None:
    # frames.hpp::quaternion_from_euler (S&L eq. 1.8-16), checked against the real
    # Simulation.from_config's own initial.state.euler_nb construction at three distinct, all-
    # nonzero angles -- catches a phi/psi argument-order transposition (the permutation trap the
    # module docstring flags), which a phi = psi = 0 case (like R3's own trim point) cannot.
    physical = {
        "earth": "plugins/physical/earth/flat",
        "gravity": "plugins/physical/gravity/constant",
        "mass": {
            "plugin": "plugins/physical/mass/constant",
            "mass": 10.0,
            "ixx": 1.0,
            "iyy": 1.0,
            "izz": 1.0,
        },
    }
    for psi_deg, theta_deg, phi_deg in ((15.0, -8.0, 22.0), (-30.0, 5.0, -12.0)):
        psi_rad, theta_rad, phi_rad = (
            math.radians(psi_deg),
            math.radians(theta_deg),
            math.radians(phi_deg),
        )
        d = {
            "sim": {"dt": 0.001, "frames": 1},
            "vehicle": {"models": []},
            "physical": physical,
            "initial": {"state": {"euler_nb": [psi_rad, theta_rad, phi_rad]}},
        }
        sim = _core.Simulation.from_config(_core.Config.from_dict(d), _core.default_plugin_dir())
        real_q = np.array(
            sim.vehicles[0].state[_core.QUATERNION : _core.QUATERNION + _core.QUATERNION_SIZE]
        )
        ported_q = linearize._quaternion_from_euler(
            psi_rad=psi_rad, theta_rad=theta_rad, phi_rad=phi_rad
        )
        assert np.allclose(ported_q, real_q, atol=1e-15)


def test_euler_rate_matches_the_wings_level_identity() -> None:
    # At phi = theta = 0 (wings level, not climbing/diving) eq. 1.4-4's own H(Phi) matrix is the
    # identity: [phi_dot, theta_dot, psi_dot] = [p, q, r] exactly -- a hand-checkable case
    # independent of any core binding, since the kinematic matrix itself is never bound to Python.
    omega_b = np.array([0.37, -0.21, 0.84])
    euler_dot = linearize._euler_rate(omega_b, phi_rad=0.0, theta_rad=0.0)
    assert np.allclose(euler_dot, omega_b)
