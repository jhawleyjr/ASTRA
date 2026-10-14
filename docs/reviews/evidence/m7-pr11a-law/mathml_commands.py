"""The MathML anchor for F16_control.dml (M7 PR 11a, docs/plans/m7-pr11a-control-peer.md, D5).

F16Control.ReferenceMatchesTheMathMlEvaluation (aircraft/f16/tests/control_test.cpp) pastes this
script's output. It checks both C++ transcriptions of the law against the DML file itself: the
DML-units reference (aircraft/f16/tests/control_reference.hpp) and the plugin's SI realization
(aircraft/f16/sim/transport/nesc_control/control_law.hpp). An error common to the two hand-written
transcriptions cannot pass it. The law has no checkData (addendum section 7).

The evaluator is M7 PR 1b's rate_frame.py DaveModel, imported from its committed evidence
directory and not copied. It runs a DAVE-ML file as written: every MathML calculation, every
minValue/maxValue as a saturation, and the piecewise switches and wrap. This script states the
hand-built frames in the DML's own units (ft, KEAS, deg, rad/s, frac), the same decimal literals the
C++ test writes, and prints el, ail, rdr and PWR for each as %.17g.

Run from the repository root (stdlib only; docs/refs is the main checkout's, gitignored):
    python3 -I -B docs/reviews/evidence/m7-pr11a-law/mathml_commands.py > \
        docs/reviews/evidence/m7-pr11a-law/mathml_commands.log
It checks F16_control.dml's SHA-256 against the addendum's section 1.1 first, and exits 1 on a
mismatch.
"""

from __future__ import annotations

import hashlib
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent / "m7-pr1b-rate-frame"))

from rate_frame import DaveModel  # noqa: E402  (the committed evaluator, not a copy)

SHA256 = "bca7789dde61b741de69f697ce8799ee666214d0e1fa203973523774fc3a969b"  # addendum 1.1

# The engaged base frame: case 11's (and 13.x's) trim in the DML's units, rounded to decimal
# literals; the deltas below are each frame's changes from it.
BASE = {
    "throttle": 0.0,
    "longStk": 0.0,
    "latStk": 0.0,
    "pedal": 0.0,
    "sasOn": 1.0,
    "apOn": 1.0,
    "keasCmd": 287.98158203,
    "altCmd": 10013.0,
    "latOffset": 0.0,
    "baseChiCmd": 45.0,
    "altMsl": 10013.0,
    "Vequiv": 287.98158203,
    "alpha": 2.6389,
    "beta": 0.0,
    "phi": 0.0,
    "theta": 2.6389,
    "psi": 45.0,
    "pb": 5.794e-7,
    "qb": -2.7048e-5,
    "rb": -1.3863e-5,
    "throttleTrim": 0.13873817846824657,
    "longStkTrim": 0.1293103079,
}

FRAMES = [
    ("engagement", {}),
    ("13.1 step", {"altCmd": 10113.0}),
    ("13.2 step", {"keasCmd": 282.98158203}),
    ("13.3 step", {"baseChiCmd": 60.0}),
    ("13.4 step", {"latOffset": -1996.67}),
    ("disengaged", {"sasOn": 0.0, "apOn": 0.0}),
    (
        "SAS only, pilot inputs",
        {
            "apOn": 0.0,
            "throttle": 0.3,
            "longStk": 0.2,
            "latStk": -0.4,
            "pedal": 0.1,
            "phi": 10.0,
            "beta": 2.0,
            "pb": 0.1,
            "rb": -0.05,
            "alpha": 4.0,
            "theta": 3.0,
            "Vequiv": 290.0,
        },
    ),
    (
        "SAS only, small inputs",
        {
            "apOn": 0.0,
            "throttle": 0.2,
            "longStk": 0.05,
            "latStk": -0.03,
            "pedal": 0.02,
            "phi": 0.05,
            "beta": 0.02,
            "pb": 0.002,
            "rb": -0.001,
            "Vequiv": 287.9,
        },
    ),
    ("small lateral disturbance", {"phi": 0.04, "beta": 0.015, "pb": 0.001, "rb": -0.0005}),
    ("track-error wrap", {"psi": -170.0, "baseChiCmd": 170.0, "beta": 1.0}),
    (
        "pilot inputs past their limits",
        {"sasOn": 0.0, "apOn": 0.0, "throttle": 1.3, "longStk": -1.4, "latStk": 1.2, "pedal": -1.5},
    ),
    (
        "maneuvering, throttle at 0",
        {
            "Vequiv": 300.0,
            "phi": 20.0,
            "theta": 5.0,
            "alpha": 6.0,
            "beta": -3.0,
            "pb": 0.5,
            "qb": 0.1,
            "rb": -0.2,
            "altMsl": 9900.0,
            "latOffset": 1500.0,
            "baseChiCmd": 30.0,
            "psi": 50.0,
        },
    ),
]

OUTPUTS = ("el", "ail", "rdr", "PWR")


def main_checkout() -> Path:
    out = subprocess.run(
        ["git", "-C", str(ROOT), "rev-parse", "--git-common-dir"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()
    common = Path(out)
    if not common.is_absolute():
        common = (ROOT / common).resolve()
    return common.parent


def main() -> int:
    dml = (
        main_checkout() / "docs/refs/Atmospheric_models/F16_package/F16_S119_source/F16_control.dml"
    )
    actual = hashlib.sha256(dml.read_bytes()).hexdigest()
    print("M7 PR 11a: F16_control.dml's MathML at the hand-built frames (DML units)")
    print(f"F16_control.dml SHA-256 {actual} ({'matches' if actual == SHA256 else 'MISMATCH'})")
    if actual != SHA256:
        return 1
    law = DaveModel(dml)
    for name, delta in FRAMES:
        inputs = dict(BASE)
        inputs.update(delta)
        values = law.evaluate(inputs, OUTPUTS)
        print(f"{name}: " + ", ".join(f"{k} {values[k]:.17g}" for k in OUTPUTS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
