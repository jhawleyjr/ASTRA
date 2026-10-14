"""The 13.x configs' initial airspeed command (M7 PR 11a, docs/plans/m7-pr11a-control-peer.md,
D1 and Joe's amendment 6).

Vol II D.1.14 gives 13.2's step as "a command to decrease speed 5 kt to 282.98 KEAS", so the
initial equivalentAirspeedCommand is the trim's own EAS, 287.98 KEAS. This script computes it from
case 11's committed config, the initial condition of all four 13.x cases, independently of ASTRA:
- the config's initial.state is read as text by derive_residual.py's strict reader (beside this
  file, PR 9's route; amendments 3 and 12 of M7 PR 9's plan);
- the density comes from derive_residual.py's own ISA-1976 (USSA 1976 eqs 18, 33a and 42), the
  sea-level density from its own constants, rho0 = P0 M0 / (R* T0);
- EAS = |v_b| sqrt(rho / rho0) (still air), in knots with the exact knot, 1852/3600 m/s.
It prints that value and 13.2's step, that value minus 5, each as %.17g: the literals the configs
carry as transport.peer.commands.equivalent_airspeed_knots and 13.2's schedule entry. The 13.x
harness checks the peer's own frame-0 EAS against the literal to 1e-9 relative (amendment 6); the
peer reads ICD v1's cas and mach, a separate ISA implementation.

Run from the repository root:
    .venv/bin/python -I -B docs/reviews/evidence/m7-pr11a-residual/ic_eas.py
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import derive_residual as dr  # noqa: E402  (beside this file)

KNOT = 1852.0 / 3600.0  # m/s, exact
CONFIG = "aircraft/f16/configs/case11_trim_check_subsonic.yaml"


def main() -> int:
    state, _controls, log = dr.read_config(dr.ROOT / CONFIG)
    print(f"M7 PR 11a: the initial airspeed command, from {CONFIG} (case 11's trim, 13.x's IC)")
    for line in log:
        if "v_b" in line or "p_n[2]" in line:
            print(line)
    altitude = state["p_n"][2]
    speed = math.sqrt(sum(v * v for v in state["v_b"]))
    rho, _sound = dr.isa(altitude)
    rho0 = dr.ISA_P0 * dr.ISA_M0 / (dr.ISA_RSTAR * dr.ISA_T0)
    eas_knots = speed * math.sqrt(rho / rho0) / KNOT
    print(f"  |v_b| {speed:.17g} m/s; rho {rho:.17g} kg/m^3; rho0 {rho0:.17g} kg/m^3")
    print(f"  equivalent_airspeed_knots (initial) {eas_knots:.17g}")
    print(f"  13.2's step, minus 5 kt            {eas_knots - 5.0:.17g}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
