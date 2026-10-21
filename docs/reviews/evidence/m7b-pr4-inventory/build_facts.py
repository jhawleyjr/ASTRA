"""M7b PR 4's build facts (docs/plans/m7b-pr4-f16-symmetry.md, section 6; PR 3's section 5 form).

For every translation unit on oracle (c)'s flight path, in every preset the gate builds and in
build/python, this script reads the unit's compile command from compile_commands.json and checks
three things with M7b PR 3's own helpers (tests/python/test_autopilot_peer.py), unchanged:
1. its floating-point token set is PR 3's expected one: none in the Debug presets, and -O3 in
   release and build/python. No response or configuration file is passed;
2. the driver's effective cc1 floating-point set (-###, which runs nothing) is PR 3's: -fmath-errno,
   -ffp-contract=on, -fno-rounding-math, -target-cpu x86-64 and -tune-cpu generic, plus -O3 in
   release and build/python, with no -target-feature;
3. the target: no __FMA__ or __FMA4__, __SSE2_MATH__ defined, and __FLT_EVAL_METHOD__ 0.
It evidences the build only. It is not a test, and it runs nothing of the path under test.

Run from the repository root, after building every preset and `just python`:
    .venv/bin/python -I -B docs/reviews/evidence/m7b-pr4-inventory/build_facts.py [--c2]
Without --c2 it checks C1's units; with it, C2's as well (the variants and the flight tests). It
prints one line per build and unit, and exits 1 on any difference.
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tests/python"))

import test_autopilot_peer as tap  # noqa: E402  (M7b PR 3's flag helpers, unchanged)

C1_UNITS = {
    "core simulation": "core/src/kernel/simulation.cpp",
    "core vehicle": "core/src/kernel/vehicle.cpp",
    "core rigid body": "core/src/kernel/rigid_body_6dof_dynamics.cpp",
    "core exchange": "core/src/kernel/lockstep_exchange.cpp",
    "f16 aero": "aircraft/f16/physical/aero/aero.cpp",
    "f16 engine": "aircraft/f16/hardware/propulsion/engine/engine.cpp",
    "f16 mass": "aircraft/f16/physical/mass/mass.cpp",
    "f16 peer": "aircraft/f16/sim/transport/nesc_control/nesc_control.cpp",
    "peer variant": "tests/plugins/nesc_control_variant/nesc_control_variant.cpp",
    "earth round": "plugins/physical/earth/round/round.cpp",
    "earth flat": "plugins/physical/earth/flat/flat.cpp",
    "gravity inverse square": "plugins/physical/gravity/inverse_square/inverse_square.cpp",
    "gravity constant": "plugins/physical/gravity/constant/constant.cpp",
    "atmosphere isa1976": "plugins/physical/atmosphere/isa1976/isa1976.cpp",
    "probe test": "tests/physics/nesc/f16_symmetry_probe_test.cpp",
}
C2_UNITS = {
    "aero variant": "tests/plugins/f16_aero_variant/f16_aero_variant.cpp",
    "smoke test": "tests/physics/nesc/f16_symmetry_smoke_test.cpp",
    "mirror test": "tests/physics/nesc/f16_symmetry_mirror_test.cpp",
    "heading test": "tests/physics/nesc/f16_symmetry_heading_test.cpp",
}
# The flight tests are registered only in a Release build (section 12).
RELEASE_ONLY = {"mirror test", "heading test"}


def main() -> int:
    names = dict(C1_UNITS) | (C2_UNITS if "--c2" in sys.argv[1:] else {})
    builds = {preset: ROOT / "build" / preset for preset in tap.PRESETS}
    builds["python"] = ROOT / "build/python"
    failures: list[str] = []
    print("M7b PR 4: the build facts (docs/plans/m7b-pr4-f16-symmetry.md, section 6)")
    for build, directory in builds.items():
        wanted = {
            n: p
            for n, p in names.items()
            if (build == "release" or n not in RELEASE_ONLY)
            and (build != "python" or "test" not in n)
        }
        units = tap.load_units(build, directory, wanted)
        expected_cc1 = tap.CLANG_CC1 | ({"-O3"} if build in ("release", "python") else set())
        for unit in units.values():
            try:
                tokens = tap.check_tokens(unit, tap.EXPECTED_TOKENS[build])
                cc1 = tap.effective_cc1(unit)
                assert cc1 == expected_cc1, f"effective cc1 {sorted(cc1)}"
                target = tap.check_target(unit)
            except AssertionError as error:
                failures.append(f"{build} {unit.name}: {error}")
                print(f"  {build:14s} {unit.name:24s} DIFFERS: {error}")
                continue
            print(
                f"  {build:14s} {unit.name:24s} tokens {sorted(tokens)} cc1 {sorted(cc1)}"
                f" fma {target['fma']} sse2_math {target['sse2_math']}"
                f" eval_method {target['eval_method']}"
            )
    if failures:
        print("STOP: " + "; ".join(failures))
        return 1
    print("every unit matches PR 3's expected sets")
    return 0


if __name__ == "__main__":
    sys.exit(main())
