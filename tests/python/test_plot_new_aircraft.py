"""R4 (M6 PR 5, docs/plans/m6-pr5-plots.md): a `just new-aircraft` copy renders its plots/ starter
set against its own log, with no framework edit.

The throwaway-copy pattern of test_new_aircraft_build.py (its `_copy_sources`, `_env` and
`_snapshot`): the working tree's build inputs are copied into tmp_path, `just new-aircraft gliderx`
runs there, and no git runs in the copy. A fresh copy's config is zeroed and refused at init, so
this test fills it with valid numbers, test-side only: each `key: 0.0` line of the copy's
configs/cruise.yaml gets the notional transport's value (the template's plugins are the notional
transport's, renamed), with a flat-Earth initial state at 10,000 m. Nothing else in the copy
changes.

The copy's six plugins (its own three; earth/flat, gravity/constant and atmosphere/isa1976) are
built with the repo's `release` preset in a build directory outside the copy. That preset's
toolchain string (clang-18, Release, no sanitizers, shared plugins) is the one this venv's
astra._core was built with, so the loader accepts them in this process. Each plugin's DT_NEEDED
libastra_core must resolve to the one already loaded, never the copy's own build of it, so
/proc/self/maps is checked after the load.

The config runs open loop for its 1000 frames with a log block appended to its text (parsed by the
core, never through PyYAML, lessons 2026-09-30). Every package in the copy's plots/ is rendered and
checked as in R1 (test_plot.check_package), the file-based ground_track included.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
from pathlib import Path

import numpy as np

from astra import _core, log, plot
from test_new_aircraft_build import _copy_sources, _env, _snapshot
from test_plot import check_package, log_column

AIRCRAFT = "gliderx"
REPO_ROOT = Path(__file__).resolve().parents[2]

# The notional transport's values (aircraft/notional_transport/configs/cruise.yaml), in the order
# each key appears in the template's config: the two turbofan stations are left, then right.
FILL: dict[str, list[str]] = {
    "wing_area_m2": ["108.78945984"],
    "wingspan_m": ["28.86456"],
    "mean_chord_m": ["3.752088"],
    "r_aero_b_x_m": ["0.3556"],
    "r_aero_b_y_m": ["0.0"],
    "r_aero_b_z_m": ["-1.6256"],
    "cl_0": ["0.2"],
    "cl_alpha_per_rad": ["4.3478260869565215"],
    "cl_elevator_per_rad": ["0.2"],
    "cd_0": ["0.021"],
    "cd_k": ["0.043"],
    "cy_beta_per_rad": ["-1.0"],
    "croll_beta_per_rad": ["-0.09"],
    "croll_p_per_rad": ["-0.4"],
    "croll_r_per_rad": ["0.09"],
    "croll_aileron_per_rad": ["0.0743"],
    "croll_rudder_per_rad": ["0.01"],
    "cm_alpha_per_rad": ["-0.6"],
    "cm_q_per_rad": ["-27.0"],
    "cm_elevator_per_rad": ["-0.8544"],
    "cn_beta_per_rad": ["0.26"],
    "cn_r_per_rad": ["-0.35"],
    "cn_rudder_per_rad": ["-0.2"],
    "x_offset_from_cg_m": ["2.5146", "2.5146"],
    "y_offset_from_cg_m": ["-4.9022", "4.9022"],
    "z_offset_from_cg_m": ["0.0", "0.0"],
    "thrust_ref_n": ["29878.121867743088", "29878.121867743088"],
    "density_ref_kgm3": ["0.4128", "0.4128"],
    "elevator": ["0.026674474137989946"],
    "aileron": ["0.0"],
    "rudder": ["0.0"],
    "throttle": ["0.4985979490507746"],
    "mass": ["37648.0"],
    "ixx": ["761970.0"],
    "iyy": ["1997120.0"],
    "izz": ["2567919.0"],
}
# The flat-Earth initial state: NED meters, so p_n[2] = -10000 is 10,000 m up; the notional trim's
# body velocity and pitch. The heading, euler_nb[0] = psi = 0.6 rad, gives travel on both north and
# east axes, so the ground-track oracle tests each axis against a nonzero displacement.
STATE = {
    "p_n": "[0.0, 0.0, -10000.0]",
    "v_b": "[229.9360238725, 0.0, 5.42447469393253]",
    "euler_nb": "[0.6, 0.023586859573503434, 0.0]",
}
PLUGIN_TARGETS = [
    f"astra_aircraft_{AIRCRAFT}_physical_aero",
    f"astra_aircraft_{AIRCRAFT}_physical_mass",
    f"astra_aircraft_{AIRCRAFT}_hardware_propulsion_turbofan",
    "astra_plugins_physical_earth_flat",
    "astra_plugins_physical_gravity_constant",
    "astra_plugins_physical_atmosphere_isa1976",
]


def _fill(text: str) -> str:
    for key, values in FILL.items():
        pattern = re.compile(rf"^(\s*(?:- )?){re.escape(key)}: 0\.0(?=\s|$)", re.M)
        matches = list(pattern.finditer(text))
        assert len(matches) == len(values), f"{key}: {len(matches)} lines, expected {len(values)}"
        for match, value in reversed(list(zip(matches, values, strict=True))):
            line = f"{match.group(1)}{key}: {value}"
            text = text[: match.start()] + line + text[match.end() :]
    for key, value in STATE.items():
        pattern = re.compile(rf"^(\s*){key}: \[0\.0, 0\.0, 0\.0\]$", re.M)
        text, count = pattern.subn(rf"\g<1>{key}: {value}", text)
        assert count == 1, key
    return text


def _run(argv: list[str], cwd: Path, env: dict[str, str], timeout: int) -> None:
    result = subprocess.run(
        argv, cwd=cwd, env=env, capture_output=True, text=True, timeout=timeout, check=False
    )
    output = result.stdout + result.stderr
    assert result.returncode == 0, f"{' '.join(argv)} exited {result.returncode}:\n{output[-6000:]}"


def _core_libraries() -> set[str]:
    """Every libastra_core file mapped into this process."""
    lines = Path("/proc/self/maps").read_text(encoding="utf-8").splitlines()
    return {line.split(maxsplit=5)[5] for line in lines if "libastra_core" in line}


def check_ground_track_against_position(package: Path, run_log: log.RunLog) -> None:
    """An oracle for the ground track that does not share its method. On a flat Earth the logged
    position state ``v0.state.p_n_0/1`` is NED meters, integrated by RK4, so the drawn north and
    east must equal it minus its first value.

    The bound, derived before the first run: the ground track is a left Riemann sum of the
    frame-start NED velocity, ``sum(v_j * dt_j)``, and the position is the integral of the same
    velocity. On each step the left-endpoint error is at most ``dt_j * |v_(j+1) - v_j|`` (the
    velocity is monotone over a 1 ms step of a smooth flight), so the error at any row is at most
    ``sum_j dt_j * |v_(j+1) - v_j|``, dt times the total variation of that component over the run.
    1e-9 m is added for floating summation (about 1000 * 2.2e-16 * 230 m = 5e-11 m) and RK4's
    position error (O(dt^5) per step). Each bound must also be below 1 mm, so it cannot hide an
    error of meters, such as a swapped axis or the wrong velocity component (about 190 m north and
    130 m east at the 0.6 rad heading), and each axis must travel more than 100 m."""
    (figure,) = plot.resolve(plot.load_package(package), run_log)
    drawn = plot.draw_matplotlib(figure)
    (line,) = drawn.axes[0].get_lines()
    east_drawn = np.asarray(line.get_xdata(), dtype=np.float64)
    north_drawn = np.asarray(line.get_ydata(), dtype=np.float64)
    t, north_position = log_column(run_log, "v0.state.p_n_0")
    _, east_position = log_column(run_log, "v0.state.p_n_1")
    _, north_velocity = log_column(run_log, "v0.derived.v_n_0")
    _, east_velocity = log_column(run_log, "v0.derived.v_n_1")
    steps = np.diff(t)
    for axis, drawn_values, position, velocity in (
        ("north", north_drawn, north_position, north_velocity),
        ("east", east_drawn, east_position, east_velocity),
    ):
        bound = float(np.sum(steps * np.abs(np.diff(velocity)))) + 1e-9
        assert bound < 1e-3, f"{axis}: the bound {bound} m is too loose to discriminate"
        assert abs(position[-1] - position[0]) > 100.0, f"{axis}: too little travel to test"
        error = float(np.max(np.abs(drawn_values - (position - position[0]))))
        assert error <= bound, (
            f"{axis}: drawn differs from the logged position by {error} m > {bound} m"
        )


def test_r4_a_new_aircraft_renders_its_plots_against_its_own_log(tmp_path: Path) -> None:
    root = tmp_path / "repo"
    root.mkdir()
    build = tmp_path / "build"
    env = _env(tmp_path)

    _copy_sources(root)
    _run(["just", "new-aircraft", AIRCRAFT], root, env, timeout=60)
    copy = root / "aircraft" / AIRCRAFT
    template = root / "aircraft" / "_template"
    assert _snapshot(copy / "plots") == _snapshot(template / "plots")
    assert sorted(_snapshot(copy / "plots")) == [
        "controls.yaml",
        "ground_track.py",
        "ground_track.yaml",
        "wrench.yaml",
    ]
    after_copy = _snapshot(root)

    config_path = copy / "configs" / "cruise.yaml"
    text = _fill(config_path.read_text(encoding="utf-8"))
    config_path.write_text(text, encoding="utf-8")
    filled = _snapshot(root)
    changed = sorted(p for p in filled if filled[p] != after_copy.get(p))
    assert changed == [f"aircraft/{AIRCRAFT}/configs/cruise.yaml"]
    assert set(filled) == set(after_copy)

    _run(["cmake", "--preset", "release", "-B", str(build)], root, env, timeout=300)
    jobs = str(os.cpu_count() or 1)
    _run(
        ["cmake", "--build", str(build), "--parallel", jobs, "--target", *PLUGIN_TARGETS],
        root,
        env,
        timeout=900,
    )

    log_path = tmp_path / "gliderx.h5"
    with_log = text + f"log:\n  path: {json.dumps(str(log_path))}\n"
    config = _core.Config.from_string(with_log, str(config_path))
    before = _core_libraries()
    assert len(before) == 1, before
    sim = _core.Simulation.from_config(config, build / "plugins")
    assert _core_libraries() == before, "a plugin mapped a second libastra_core"
    assert not str(next(iter(before))).startswith(str(tmp_path))

    halt = sim.run()
    assert not halt, halt
    sim.close_log()
    run_log = log.load(log_path)
    assert run_log.frames_run == 1000

    check_ground_track_against_position(copy / "plots" / "ground_track.yaml", run_log)

    out = tmp_path / "plots"
    for package in sorted((copy / "plots").glob("*.yaml")):
        check_package(package, run_log, out)
    assert sorted(p.name for p in out.iterdir()) == [
        "controls-controls.svg",
        "ground_track-ground_track.svg",
        "wrench-wrench.svg",
    ]
    # Nothing was written into the copy: no build output, no __pycache__ beside ground_track.py.
    assert _snapshot(root) == filled
    assert not (copy / "plots" / "__pycache__").exists()
