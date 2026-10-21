"""M7b PR 4's one trim result (docs/plans/m7b-pr4-f16-symmetry.md, section 5; Q9, C-01, N-01).

astra trim refuses a transport block by design, so the oracle's trim is solved on base.yaml.in,
beside this file: case 11's F-16 at case 11's latitude, longitude 0, 10,013 ft and heading 0, on a
non-rotating sphere. This script:
1. re-solves that trim through astra.trim at the same setpoint and rate convention, and checks
   that trim_n_frame.out (astra trim's output, beside this file) is that result, value for value;
2. checks the lateral entries that are exact by construction: v = 0.0, and q_nb's x and z +0.0
   (euler_nb [0, theta, 0]);
3. checks C-01's pre-registered bound on the trim's own n_frame p and r:
   |p|, |r| <= B = c u kappa |omega|, with c = 64, u = 2^-53, and kappa the 2-norm condition
   number of the trim's own Jacobian J_c. J_c and q_dot(0) are rebuilt as
   astra.trim._n_frame_rate builds them (trim.py:212-225), and the script checks that lstsq on
   them reproduces the trim's rates bit for bit;
4. prints the literals the oracle's configs carry, each with repr.

Run from the repository root, after `just python`:
    .venv/bin/python -I -B docs/reviews/evidence/m7b-pr4-trim/trim_check.py
It prints its log to stdout and exits 1 on any mismatch or miss.
"""

from __future__ import annotations

import shutil
import sys
import tempfile
from pathlib import Path
from typing import Any

import numpy as np
import yaml

import astra.trim
from astra import _core

HERE = Path(__file__).resolve().parent
BASE = HERE / "base.yaml.in"
OUT = HERE / "trim_n_frame.out"
AIRSPEED = 172.42091752452774  # m/s, case 11's (Vol II Table 35)
ALTITUDE = 3051.9624000000003  # m, 10,013 ft
C_BOUND = 64  # section 5: c_data + c_LS = 38 + 12 = 50, rounded up to a power of two
U = 2.0**-53


def jacobian(sim: _core.Simulation, state: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """J_c and q_dot(0) as trim.py:212-225 builds them."""
    rate = slice(_core.ANGULAR_RATE, _core.ANGULAR_RATE + _core.ANGULAR_RATE_SIZE)
    still = state.copy()
    still[rate] = 0.0
    at_rest = astra.trim._attitude_rate(sim, still)
    j_c = np.empty((_core.QUATERNION_SIZE, _core.ANGULAR_RATE_SIZE))
    for i in range(_core.ANGULAR_RATE_SIZE):
        unit = still.copy()
        unit[_core.ANGULAR_RATE + i] = 1.0
        j_c[:, i] = astra.trim._attitude_rate(sim, unit) - at_rest
    return j_c, at_rest


def main() -> int:
    failures: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "base.yaml"
        shutil.copyfile(BASE, path)
        result = astra.trim.trim_straight_and_level(
            path, AIRSPEED, ALTITUDE, heading_rad=0.0, rate_convention="n_frame"
        )
        sim = _core.Simulation.from_config(
            astra.trim._load_still_air_config(path), _core.default_plugin_dir()
        )
        j_c, at_rest = jacobian(sim, result.state)

    state = result.state
    omega = state[_core.ANGULAR_RATE : _core.ANGULAR_RATE + _core.ANGULAR_RATE_SIZE]
    v_b = state[_core.VELOCITY : _core.VELOCITY + _core.VELOCITY_SIZE]
    q_nb = state[_core.QUATERNION : _core.QUATERNION + _core.QUATERNION_SIZE]
    print("M7b PR 4: the one trim result (docs/plans/m7b-pr4-f16-symmetry.md, section 5)")
    print(f"  astra.trim on {BASE.name}: airspeed {AIRSPEED!r} m/s, altitude {ALTITUDE!r} m,")
    print("  heading 0, rate convention n_frame")
    print(f"  elevator {result.elevator_rad!r}, throttle {result.throttle!r},")
    print(f"  alpha {result.alpha_rad!r}")
    print(f"  solved residual {[float(v) for v in result.solved_residual]}")
    print(f"  lateral residual {[float(v) for v in result.lateral_residual]}")

    # 1. trim_n_frame.out is this result.
    out: dict[str, Any] = yaml.safe_load(OUT.read_text(encoding="utf-8"))
    expected = {
        "elevator": result.elevator_rad,
        "throttle": result.throttle,
        "alpha": result.alpha_rad,
        "v_b": [float(v) for v in v_b],
        "omega_b": [float(v) for v in omega],
    }
    written = {
        "elevator": float(out["elevator"]),
        "throttle": float(out["throttle"]),
        "alpha": float(out["alpha"]),
        "v_b": [float(v) for v in out["initial"]["state"]["v_b"]],
        "omega_b": [float(v) for v in out["initial"]["state"]["omega_b"]],
    }
    for key, value in expected.items():
        same = written[key] == value
        print(f"  {OUT.name} {key}: {'equal' if same else 'DIFFERS'}")
        if not same:
            failures.append(f"{OUT.name}'s {key} {written[key]!r} against {value!r}")

    # 2. The lateral entries exact by construction.
    for label, value in (("v", v_b[1]), ("q_nb.x", q_nb[1]), ("q_nb.z", q_nb[3])):
        print(f"  {label} {float(value)!r}")
        if value != 0.0:
            failures.append(f"{label} is {float(value)!r}, not 0.0")

    # 3. C-01's bound.
    rates = np.linalg.lstsq(j_c, -at_rest, rcond=None)[0]
    reproduced = bool(np.array_equal(rates, omega))
    print(f"  lstsq on the rebuilt J_c reproduces the trim's rates bit for bit: {reproduced}")
    if not reproduced:
        failures.append("the rebuilt J_c does not reproduce the trim's rates")
    kappa = float(np.linalg.cond(j_c))
    norm = float(np.linalg.norm(omega))
    bound = C_BOUND * U * kappa * norm
    p_hat, r_hat = float(omega[0]), float(omega[2])
    print(f"  kappa(J_c) {kappa!r}, |omega| {norm!r} rad/s")
    print(f"  B = 64 u kappa |omega| = {bound!r} rad/s")
    print(f"  p {p_hat!r} rad/s ({abs(p_hat) / bound:.3g} B)")
    print(f"  r {r_hat!r} rad/s ({abs(r_hat) / bound:.3g} B)")
    for label, value in (("p", p_hat), ("r", r_hat)):
        if not abs(value) <= bound:
            failures.append(f"the trim's {label} {value!r} exceeds C-01's bound {bound!r}")

    # 4. The literals the oracle's configs carry.
    print("  the IC literals (section 5):")
    print(f"    elevator {result.elevator_rad!r}")
    print(f"    throttle {result.throttle!r}")
    print(f"    theta {result.alpha_rad!r}")
    print(f"    u {float(v_b[0])!r}  w {float(v_b[2])!r}  q {float(omega[1])!r}")
    print(f"    replaced by 0.0: p {p_hat!r}, r {r_hat!r}")

    if failures:
        print("FAIL: " + "; ".join(failures))
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
