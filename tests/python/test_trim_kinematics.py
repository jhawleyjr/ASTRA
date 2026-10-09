"""astra.trim's rate convention against an independent closed form (M7 PR 5,
docs/plans/m7-pr5-trim.md, "R4 in this PR"; the M7 plan's Q6, Q7 and R4).

Under ``rate_convention="n_frame"`` the trim sets omega_b to the n frame's own rate, so the Euler
angles hold relative to the local vertical, and its lateral residuals (v_dot_y, p_dot, r_dot) do
not vanish. Their kinematic and gravity parts are derived here from latitude, heading, airspeed,
altitude and the solved pitch alone, never from ASTRA's EOM or its plugins' code, and checked on a
wrench source with no rate dependence, so the derivation is the whole residual. The sources
(``docs/refs/textbooks``, gitignored):
- [SL] Stevens, Lewis and Johnson, *Aircraft Control and Simulation*, 3rd ed. (2015);
- [G] Groves, *Principles of GNSS, Inertial, and Multisensor Integrated Navigation Systems*,
  2nd ed.;
- [TW] Titterton and Weston, *Strapdown Inertial Navigation Technology*, 2nd ed.;
- [TR] NIMA TR8350.2, *Department of Defense World Geodetic System 1984*, 3rd ed.

The gravity path differentiates [SL] eq. 1.6-24's J2 potential in geocentric spherical coordinates,
where the plugin works in Cartesian ECEF ([G] eq. 2.142), so one shared formula slip cannot hide.
J2 is the gravity model's own declared value, ``core/include/astra/util/geodesy.hpp``'s ``kJ2``
(the plan's Observations: it is the original WGS 84 value, 3e-6 relative from [TR] Table 5.1's).

Bounds are roundoff budgets, not physics guesses: 1e-12 times the largest term magnitude entering
each compared quantity.
"""

from __future__ import annotations

import math
from pathlib import Path
from typing import Any

import numpy as np
import pytest
import yaml

from astra import _core, linearize, trim

STILL_AIR = "aircraft/notional_transport/configs/cruise_still_air.yaml"

# [TR] eqs. 3-1, 3-2, 3-6 and 3-3.
A = 6_378_137.0
FLATTENING = 1.0 / 298.257223563
E2 = FLATTENING * (2.0 - FLATTENING)
OMEGA_IE = 7.292115e-5
GM = 3.986004418e14
J2 = 1.082629989e-3  # the gravity model's declared value (module docstring)

ROUNDOFF = 1e-12

# The test source's diagonal inertia (kg m^2): cruise_still_air's ixx, iyy, izz with ixz = 0.
IXX, IYY, IZZ = 761970.0, 1997120.0, 2567919.0

# (latitude deg, heading deg, true airspeed m/s, altitude m): 12 conditions spanning latitude and
# heading. Case 15's (89.95 N, heading 90, 565.69 ft/s, 10,000 ft), case 16's (equator, heading
# north) and case 11's region (36.019 N, heading 45) are among them; Vol II's exact initial
# conditions are PR 9's.
CONDITIONS = [
    (0.0, 0.0, 230.0, 10_000.0),
    (0.0, 0.0, 172.4, 3_048.0),
    (0.0, 90.0, 230.0, 10_000.0),
    (36.019, 45.0, 172.4, 3_048.0),
    (36.019, -135.0, 230.0, 10_000.0),
    (-45.0, 180.0, 172.4, 3_048.0),
    (-45.0, 45.0, 230.0, 10_000.0),
    (60.0, 90.0, 230.0, 3_048.0),
    (60.0, -135.0, 172.4, 10_000.0),
    (89.95, 90.0, 172.4, 3_048.0),
    (89.95, 0.0, 230.0, 10_000.0),
    (-89.95, -135.0, 172.4, 3_048.0),
]


def radii(lat: float) -> tuple[float, float]:
    """Meridian and transverse radii of curvature: [SL] eqs. 1.6-5 and 1.6-9, [G] eqs. 2.105 and
    2.106."""
    s2 = math.sin(lat) ** 2
    return A * (1.0 - E2) / (1.0 - E2 * s2) ** 1.5, A / math.sqrt(1.0 - E2 * s2)


def earth_rate_n(lat: float) -> np.ndarray:
    """omega_ie^n: [TW] eq. 3.72, [G] eq. 5.41."""
    return OMEGA_IE * np.array([math.cos(lat), 0.0, -math.sin(lat)])


def transport_rate_n(lat: float, alt: float, v_n: np.ndarray, rn: float, re: float) -> np.ndarray:
    """omega_en^n: [G] eq. 5.44, [TW] eq. 3.87."""
    v_north, v_east = v_n[0], v_n[1]
    return np.array(
        [v_east / (re + alt), -v_north / (rn + alt), -v_east * math.tan(lat) / (re + alt)]
    )


def gravity_n(lat: float, lon: float, alt: float) -> np.ndarray:
    """Plumb-bob gravity in geodetic NED: [SL] eq. 1.6-24's J2 gravitation, its gradient taken in
    geocentric spherical coordinates (radial and geocentric north) and rotated through D = lat -
    psi_c into geodetic north and down; plus [SL] eq. 1.6-27's centrifugal term. The ECEF position
    is [SL] eq. 1.6-17."""
    _, re = radii(lat)
    p = np.array(
        [
            (re + alt) * math.cos(lat) * math.cos(lon),
            (re + alt) * math.cos(lat) * math.sin(lon),
            (re * (1.0 - E2) + alt) * math.sin(lat),
        ]
    )
    r = float(np.linalg.norm(p))
    psi_c = math.asin(p[2] / r)
    k = (A / r) ** 2
    g_radial = -(GM / r**2) * (1.0 - 1.5 * J2 * k * (3.0 * math.sin(psi_c) ** 2 - 1.0))
    g_north_c = -3.0 * (GM / r**2) * J2 * k * math.sin(psi_c) * math.cos(psi_c)
    d = lat - psi_c
    gravitation = np.array(
        [
            -math.sin(d) * g_radial + math.cos(d) * g_north_c,
            0.0,
            -(math.cos(d) * g_radial + math.sin(d) * g_north_c),
        ]
    )
    horizontal = re + alt  # the distance from the polar axis is (re + alt) cos(lat)
    centrifugal = (
        OMEGA_IE**2
        * horizontal
        * np.array([-math.sin(lat) * math.cos(lat), 0.0, -(math.cos(lat) ** 2)])
    )
    return gravitation + centrifugal


def c_bn(psi: float, theta: float) -> np.ndarray:
    """[SL] eq. 1.3-10's 3-2-1 C_frd/ned with phi = 0."""
    cp, sp, ct, st = math.cos(psi), math.sin(psi), math.cos(theta), math.sin(theta)
    return np.array([[ct * cp, ct * sp, -st], [-sp, cp, 0.0], [st * cp, st * sp, ct]])


def config(tmp_path: Path, lat: float, lon: float, *, rate_free: bool, **physical: Any) -> Path:
    """cruise_still_air at (lat, lon). ``rate_free``: the notional aero's only rate terms zeroed
    (aero_math.hpp reads omega_b nowhere else) and ixz = 0, a diagonal inertia, leaving
    mass/constant's sign convention to M7 PR 4. The turbofans' thrust lies along body x at
    y = +-4.9022 m, so their yawing moments cancel exactly, and with beta = 0 and aileron = rudder =
    0 nothing produces a side force or a rolling or yawing moment."""
    data = yaml.safe_load(Path(STILL_AIR).read_text(encoding="utf-8"))
    data["initial"]["state"]["p_n"] = [lat, lon, 10_000.0]
    if rate_free:
        aero = data["vehicle"]["models"][0]
        for key in ("croll_p_per_rad", "croll_r_per_rad", "cm_q_per_rad", "cn_r_per_rad"):
            aero[key] = 0.0
        data["physical"]["mass"]["ixz"] = 0.0
    data["physical"].update(physical)
    path = tmp_path / f"trim_{lat:+.4f}_{lon:+.4f}.yaml"
    path.write_text(yaml.safe_dump(data, sort_keys=False), encoding="utf-8")
    return path


def n_frame(condition: tuple[float, float, float, float]) -> tuple[float, float, float, float]:
    lat_deg, psi_deg, speed, alt = condition
    return math.radians(lat_deg), math.radians(psi_deg), speed, alt


def solve(path: Path, speed: float, alt: float, psi: float) -> trim.TrimResult:
    return trim.trim_straight_and_level(
        path, speed, alt, heading_rad=psi, rate_convention="n_frame"
    )


def omega_b(result: trim.TrimResult) -> np.ndarray:
    return result.state[_core.ANGULAR_RATE : _core.ANGULAR_RATE + _core.ANGULAR_RATE_SIZE]


@pytest.mark.parametrize("condition", CONDITIONS, ids=[str(c) for c in CONDITIONS])
def test_lateral_residuals_equal_the_closed_form_on_a_rate_free_source(
    tmp_path: Path, condition: tuple[float, float, float, float]
) -> None:
    lat, psi, speed, alt = n_frame(condition)
    lon = math.radians(-45.0)
    result = solve(config(tmp_path, lat, lon, rate_free=True), speed, alt, psi)
    theta = result.alpha_rad

    # The three solved residuals, recomputed through derivative() at the returned state.
    assert np.max(np.abs(result.solved_residual)) <= 1e-6

    rn, re = radii(lat)
    v_n = speed * np.array([math.cos(psi), math.sin(psi), 0.0])
    w_ie, w_en = earth_rate_n(lat), transport_rate_n(lat, alt, v_n, rn, re)

    # omega_b equals the n frame's own rate, independently computed (Q6).
    expected_omega = c_bn(psi, theta) @ (w_ie + w_en)
    omega_bound = ROUNDOFF * float(np.linalg.norm(w_ie + w_en))
    assert np.max(np.abs(omega_b(result) - expected_omega)) <= omega_bound

    # v_dot_y = e_y . [g - (w_en + 2 w_ie) x v_n] ([G] eq. 5.53, [TW] eq. 3.69, at w_nb = 0).
    g = gravity_n(lat, lon, alt)
    coriolis = np.cross(w_en + 2.0 * w_ie, v_n)
    e_y = np.array([-math.sin(psi), math.cos(psi), 0.0])
    expected_v_dot_y = float(e_y @ (g - coriolis))
    v_bound = ROUNDOFF * max(float(np.linalg.norm(g)), float(np.linalg.norm(coriolis)))
    assert abs(result.residual[1] - expected_v_dot_y) <= v_bound

    # p_dot, r_dot: [SL] eq. 1.7-5 with a diagonal inertia and zero rolling and yawing moments.
    p, q, r = expected_omega
    expected_p_dot = (IYY - IZZ) * q * r / IXX
    expected_r_dot = (IXX - IYY) * p * q / IZZ
    w_bound = ROUNDOFF * float(np.linalg.norm(expected_omega)) ** 2 * IZZ / IXX
    assert abs(result.residual[3] - expected_p_dot) <= w_bound
    assert abs(result.residual[5] - expected_r_dot) <= w_bound

    # Level flight with the attitude held: h_dot = 0 and q_dot_nb = 0, to roundoff.
    sim = _core.Simulation.from_config(
        trim._load_still_air_config(config(tmp_path, lat, lon, rate_free=True)),  # pyright: ignore[reportPrivateUsage]
        _core.default_plugin_dir(),
    )
    for name, value in result.controls.items():
        sim.write_input(name, value)
    derivative = np.array(sim.derivative(result.state))
    assert abs(derivative[_core.POSITION + 2]) <= ROUNDOFF * speed
    q_dot = derivative[_core.QUATERNION : _core.QUATERNION + _core.QUATERNION_SIZE]
    assert np.max(np.abs(q_dot)) <= omega_bound


def test_case_15_lateral_residual_is_the_derived_one(tmp_path: Path) -> None:
    # The M7 plan's critique C-02: about 5.3 m/s^2 of v_dot_y at case 15's start, mostly the
    # transport rate's vertical term times the velocity (v^2 tan(lat) / (R_E + h)).
    lat, psi, speed, alt = n_frame((89.95, 90.0, 172.4, 3_048.0))
    result = solve(config(tmp_path, lat, 0.0, rate_free=True), speed, alt, psi)
    assert 5.0 < result.residual[1] < 5.6


@pytest.mark.parametrize("condition", CONDITIONS[::3], ids=[str(c) for c in CONDITIONS[::3]])
def test_the_notional_transport_itself_meets_the_solved_bound_and_the_n_frame_rate(
    tmp_path: Path, condition: tuple[float, float, float, float]
) -> None:
    # Its aero damps omega_b (the M7 plan's B2 item), so its lateral residuals carry rate terms this
    # file does not derive: they are reported by the trim, never bounded.
    lat, psi, speed, alt = n_frame(condition)
    result = solve(config(tmp_path, lat, 0.3, rate_free=False), speed, alt, psi)
    assert np.max(np.abs(result.solved_residual)) <= 1e-6
    rn, re = radii(lat)
    v_n = speed * np.array([math.cos(psi), math.sin(psi), 0.0])
    w_n = earth_rate_n(lat) + transport_rate_n(lat, alt, v_n, rn, re)
    expected = c_bn(psi, result.alpha_rad) @ w_n
    assert np.max(np.abs(omega_b(result) - expected)) <= ROUNDOFF * float(np.linalg.norm(w_n))


def test_a_non_rotating_round_earth_gives_the_transport_rate_alone(tmp_path: Path) -> None:
    radius = 6_371_000.0
    lat, psi, speed, alt = n_frame((36.019, 45.0, 230.0, 10_000.0))
    path = config(
        tmp_path,
        lat,
        0.0,
        rate_free=True,
        earth={"plugin": "plugins/physical/earth/round", "radius": radius, "rotating": False},
        gravity="plugins/physical/gravity/inverse_square",
    )
    result = solve(path, speed, alt, psi)
    v_n = speed * np.array([math.cos(psi), math.sin(psi), 0.0])
    w_en = transport_rate_n(lat, alt, v_n, radius, radius)
    expected = c_bn(psi, result.alpha_rad) @ w_en
    assert np.max(np.abs(omega_b(result) - expected)) <= ROUNDOFF * float(np.linalg.norm(w_en))


def test_a_flat_earth_gives_zero_rate_and_zero_lateral_residuals(tmp_path: Path) -> None:
    path = config(
        tmp_path,
        0.0,
        0.0,
        rate_free=True,
        earth="plugins/physical/earth/flat",
        gravity="plugins/physical/gravity/constant",
    )
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    data["initial"]["state"]["p_n"] = [0.0, 0.0, -10_000.0]
    path.write_text(yaml.safe_dump(data, sort_keys=False), encoding="utf-8")
    result = solve(path, 230.0, 10_000.0, math.radians(135.0))
    assert np.all(omega_b(result) == 0.0)
    # Gravity rotated into body axes through the quaternion's DCM at heading 135 deg leaves
    # roundoff (6.9e-18 m/s^2 observed), so the bound is the roundoff budget on |g|.
    assert np.max(np.abs(result.lateral_residual)) <= ROUNDOFF * 9.80665


def test_linearize_differentiates_the_n_frame_kinematics(tmp_path: Path) -> None:
    # Critique P-01: under "n_frame" the Euler-rate rows are those of omega_b - omega_in^b(x), with
    # the transport rate evaluated at each perturbed state. At the equator heading north,
    # theta_dot = q - omega_in,y^b = q + v_N / (R_N + h), and v_N = u cos(theta) + w sin(theta), so
    # d(theta_dot)/du = cos(theta) / (R_N + h); a frozen reference rate would give zero.
    path = config(tmp_path, 0.0, 0.0, rate_free=False)
    result = linearize.linearize(path, 230.0, 10_000.0, rate_convention="n_frame")
    base = np.array(result.trim.state, dtype=float)
    f0 = linearize._evaluate(  # pyright: ignore[reportPrivateUsage]
        result.simulation, base, result.x0, result.u0, "n_frame"
    )
    assert np.max(np.abs(f0[6:9])) <= ROUNDOFF * OMEGA_IE  # the Euler rates vanish at x0
    rn, _ = radii(0.0)
    theta = result.trim.alpha_rad
    expected = math.cos(theta) / (rn + 10_000.0)
    # A central difference of a function linear in u up to O(1e-15) curvature: the step's own
    # roundoff, about eps * |q| / 0.1 m/s, bounds the difference.
    assert result.a[7, 0] == pytest.approx(expected, rel=1e-6)
