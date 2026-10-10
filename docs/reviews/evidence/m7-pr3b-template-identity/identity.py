"""The template copy's cruise.yaml, before and after ADR-0032's migration, on one build.

Evidence for M7 PR 3b only (docs/plans/m7-pr3b-earth-relative-rate.md, "Item 4: what identical
compares"). The gate never runs this file. It was committed before its first run.

    .venv/bin/python docs/reviews/evidence/m7-pr3b-template-identity/identity.py <scratch> <log>

What it does, in order:
1. Preconditions. The worktree is clean. The venv's astra._core is this worktree's build, and its
   git_describe is HEAD's `git describe --always --dirty`, with no -dirty.
2. A throwaway copy of the build inputs goes under <scratch>, outside git
   (test_new_aircraft_build._copy_sources). Then `just new-aircraft gliderx`, filled with
   test_plot_new_aircraft._fill, and one release build directory, with the six plugin targets built
   once.
3. Three aero variants in that one build directory. Only the aero target is rebuilt between them,
   and every plugin library's SHA-256 is compared across each rebuild:
   - after: the candidate's aero, as copied (air_data::omega_eb_b);
   - before: 56b17bd's aero (vs.omega_b()), through new-aircraft.sh's namespace rewrite;
   - planted (reported, never a gate): vs.omega_b() - (-0.0, -0.0, -0.0), which turns a -0.0 rate
     into +0.0.
4. Two initial conditions, the copy's psi = 0.6 rad and psi = -2.5 rad (C_nb's first row all
   negative). Each pass runs in its own child process, with identical config text including one log
   path; the file is renamed after each pass.
5. Each comparison walks both HDF5 files and compares, in raw bytes:
   - the object sets;
   - every dataset's dtype, shape and bytes, with compound tables field by field and row by row;
   - every attribute.
   Raw bytes count +0.0 != -0.0.
6. The expected difference between before and after is exactly one: the sha256 cell of the
   aircraft/gliderx/physical/aero row of /plugins. A planted control first shows the comparison
   reports a single flipped sign bit in /final_state.
7. The worktree is clean again, and the log is written to <log>.

Exit 0 only when every gated check holds. Each pass's halt, frames and git_describe are printed.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[4]
BASE = "56b17bd"
AIRCRAFT = "gliderx"
AERO_PATH = f"aircraft/{AIRCRAFT}/physical/aero"
AERO_SOURCE = f"aircraft/{AIRCRAFT}/physical/aero/aero.cpp"
AERO_TARGET = f"astra_aircraft_{AIRCRAFT}_physical_aero"
MIGRATED_LINE = "    in.omega_b = air_data::omega_eb_b(vs, env);\n"
INERTIAL_LINE = "    in.omega_b = vs.omega_b();\n"
PLANTED_LINE = "    in.omega_b = vs.omega_b() - Eigen::Vector3d::Constant(-0.0);\n"
IC1_EULER = "euler_nb: [0.6, 0.023586859573503434, 0.0]"
IC2_EULER = "euler_nb: [-2.5, 0.023586859573503434, 0.0]"

LINES: list[str] = []


def say(text: str = "") -> None:
    print(text, flush=True)
    LINES.append(text)


def run(argv: list[str], cwd: Path, env: dict[str, str] | None = None) -> str:
    result = subprocess.run(
        argv, cwd=cwd, env=env, capture_output=True, text=True, timeout=1800, check=False
    )
    if result.returncode != 0:
        output = (result.stdout + result.stderr)[-6000:]
        raise SystemExit(f"{' '.join(argv)} exited {result.returncode}:\n{output}")
    return result.stdout


def worktree_clean(label: str) -> None:
    porcelain = run(["git", "status", "--porcelain"], ROOT)
    say(f"{label}: git status --porcelain is {'empty' if not porcelain else 'NOT empty'}")
    if porcelain:
        raise SystemExit(f"the worktree is not clean ({label}):\n{porcelain}")


# --- the child: one pass, in its own process ---


def core_libraries() -> set[str]:
    lines = Path("/proc/self/maps").read_text(encoding="utf-8").splitlines()
    return {line.split(maxsplit=5)[5] for line in lines if "libastra_core" in line}


def child(config_file: str, origin: str, plugin_dir: str) -> None:
    from astra import _core

    before = core_libraries()
    config = _core.Config.from_string(Path(config_file).read_text(encoding="utf-8"), origin)
    sim = _core.Simulation.from_config(config, Path(plugin_dir))
    if core_libraries() != before:
        raise SystemExit("a plugin mapped a second libastra_core")
    halt = sim.run()
    sim.close_log()
    report = {
        "core_file": str(Path(_core.__file__).resolve()),
        "libastra_core": sorted(before),
        "halt": str(halt) if halt else "",
        "frames": int(sim.clock.frame),
    }
    print(json.dumps(report))


# --- the comparison ---


def value_bytes(value: Any) -> bytes:
    import numpy as np

    if isinstance(value, bytes):
        return value
    if isinstance(value, str):
        return value.encode("utf-8")
    array = np.asarray(value)
    if array.dtype == object:
        return b"\x00".join(value_bytes(item) for item in array.ravel())
    return array.dtype.str.encode() + b"|" + str(array.shape).encode() + b"|" + array.tobytes()


def walk(path: Path) -> dict[str, Any]:
    """Every object in the file, keyed by name ('/' for the root): its kind and its attributes."""
    import h5py

    objects: dict[str, Any] = {}
    with h5py.File(path, "r") as file:

        def visit(name: str, obj: Any) -> None:
            objects["/" + name] = obj

        objects["/"] = file
        file.visititems(visit)
        return {
            name: {
                "kind": "dataset" if isinstance(obj, h5py.Dataset) else "group",
                "attrs": {key: value_bytes(obj.attrs[key]) for key in sorted(obj.attrs.keys())},
                "data": dataset_cells(obj) if isinstance(obj, h5py.Dataset) else None,
            }
            for name, obj in objects.items()
        }


def dataset_cells(dataset: Any) -> dict[str, bytes]:
    """A dataset as cells: (field, row) for a compound one, or its dtype, shape and raw bytes."""
    data = dataset[()]
    cells = {"dtype": str(dataset.dtype).encode(), "shape": str(dataset.shape).encode()}
    if dataset.dtype.names:
        for field in dataset.dtype.names:
            for row, item in enumerate(data[field]):
                cells[f"{field}[{row}]"] = value_bytes(item)
    else:
        cells["bytes"] = value_bytes(data)
    return cells


def compare(a: Path, b: Path) -> list[str]:
    left = walk(a)
    right = walk(b)
    differences: list[str] = []
    for name in sorted(set(left) | set(right)):
        if name not in left or name not in right:
            differences.append(f"{name}: present in only one file")
            continue
        one, two = left[name], right[name]
        if one["kind"] != two["kind"]:
            differences.append(f"{name}: {one['kind']} vs {two['kind']}")
        for key in sorted(set(one["attrs"]) | set(two["attrs"])):
            if one["attrs"].get(key) != two["attrs"].get(key):
                differences.append(f"{name} attribute {key}")
        if one["data"] is not None and two["data"] is not None:
            for cell in sorted(set(one["data"]) | set(two["data"])):
                if one["data"].get(cell) != two["data"].get(cell):
                    differences.append(f"{name} {cell}")
    return differences


def aero_row(path: Path) -> int:
    import h5py

    with h5py.File(path, "r") as file:
        paths = [value_bytes(item).decode() for item in file["plugins"][()]["path"]]
    return paths.index(AERO_PATH)


def planted_control(after: Path, scratch: Path) -> list[str]:
    """A copy of the after-log with one float64's sign bit flipped in /final_state."""
    import h5py
    import numpy as np

    planted = scratch / "planted-control.h5"
    shutil.copyfile(after, planted)
    with h5py.File(planted, "r+") as file:
        datasets = [
            dataset
            for dataset in file["final_state"].values()
            if isinstance(dataset, h5py.Dataset) and dataset.dtype == np.float64
        ]
        dataset = datasets[0]
        flat = np.array(dataset[()], dtype=np.float64).ravel()
        flat.view(np.uint64)[0] ^= np.uint64(0x8000000000000000)
        dataset[()] = flat.reshape(dataset.shape)
        say(f"planted control: flipped the sign bit of {dataset.name}[0]")
    return compare(after, planted)


# --- the parent ---


def sha256_files(directory: Path) -> dict[str, str]:
    return {
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(directory.glob("*.so"))
    }


def rebuild_aero(build: Path, copy: Path, env: dict[str, str], label: str) -> None:
    plugins = build / "plugins"
    hashes = sha256_files(plugins)
    jobs = str(os.cpu_count() or 1)
    run(["cmake", "--build", str(build), "--target", AERO_TARGET, "--parallel", jobs], copy, env)
    changed = sorted(
        name for name, digest in sha256_files(plugins).items() if hashes[name] != digest
    )
    say(f"{label}: rebuilt {AERO_TARGET}; plugin libraries whose SHA-256 changed: {changed}")
    if changed != [f"lib{AERO_TARGET}.so"]:
        raise SystemExit(f"{label}: expected only the aero library to change, got {changed}")


def run_pass(
    variant: str, ic: str, config_text: str, origin: str, build: Path, scratch: Path
) -> Path:
    run_dir = scratch / "run"
    run_dir.mkdir(exist_ok=True)
    log_path = run_dir / "run.h5"
    config_file = run_dir / "config.yaml"
    config_file.write_text(
        config_text + f"log:\n  path: {json.dumps(str(log_path))}\n", encoding="utf-8"
    )
    output = run(
        [
            sys.executable,
            str(Path(__file__).resolve()),
            "--child",
            str(config_file),
            origin,
            str(build / "plugins"),
        ],
        ROOT,
    )
    report = json.loads(output.strip().splitlines()[-1])
    if not report["core_file"].startswith(str(ROOT)):
        raise SystemExit(
            f"{variant} {ic}: astra._core is not this worktree's: {report['core_file']}"
        )
    if len(report["libastra_core"]) != 1 or report["libastra_core"][0].startswith(str(scratch)):
        raise SystemExit(f"{variant} {ic}: unexpected libastra_core {report['libastra_core']}")
    kept = scratch / "logs" / f"{variant}-{ic}.h5"
    kept.parent.mkdir(exist_ok=True)
    log_path.rename(kept)
    say(
        f"{variant} {ic}: halt '{report['halt']}', frames {report['frames']}, "
        f"_core {report['core_file']}, libastra_core {report['libastra_core'][0]}"
    )
    if report["halt"] or report["frames"] != 1000:
        raise SystemExit(f"{variant} {ic}: the run did not complete 1000 frames")
    return kept


def git_describe_of(path: Path) -> str:
    import h5py

    with h5py.File(path, "r") as file:
        return value_bytes(file.attrs["git_describe"]).decode()


def swap_aero(copy: Path, old: str, new: str, label: str) -> None:
    source = copy / AERO_SOURCE
    text = source.read_text(encoding="utf-8")
    if text.count(old) != 1:
        raise SystemExit(f"{label}: the aero line to replace occurs {text.count(old)} times")
    source.write_text(text.replace(old, new), encoding="utf-8")


def parent(scratch: Path, log: Path) -> int:
    sys.path.insert(0, str(ROOT / "tests" / "python"))
    from astra import _core
    from test_new_aircraft_build import _copy_sources, _env
    from test_plot_new_aircraft import PLUGIN_TARGETS, _fill

    say(f"M7 PR 3b identity run; root {ROOT}")
    worktree_clean("before")
    head = run(["git", "rev-parse", "HEAD"], ROOT).strip()
    describe = run(["git", "describe", "--always", "--dirty"], ROOT).strip()
    say(f"HEAD {head}; git describe --always --dirty: {describe}")
    if describe.endswith("-dirty"):
        raise SystemExit("git describe is -dirty")
    say(f"parent astra._core: {Path(_core.__file__).resolve()}")
    if not str(Path(_core.__file__).resolve()).startswith(str(ROOT)):
        raise SystemExit("the parent's astra._core is not this worktree's")

    work = scratch / "identity"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    copy = work / "repo"
    copy.mkdir()
    build = work / "build"
    env = _env(work)
    _copy_sources(copy)
    run(["just", "new-aircraft", AIRCRAFT], copy, env)
    config_path = copy / "aircraft" / AIRCRAFT / "configs" / "cruise.yaml"
    ic1 = _fill(config_path.read_text(encoding="utf-8"))
    config_path.write_text(ic1, encoding="utf-8")
    if ic1.count(IC1_EULER) != 1:
        raise SystemExit("the filled config does not carry the expected euler_nb")
    ic2 = ic1.replace(IC1_EULER, IC2_EULER)
    initial = {"ic1 (psi 0.6)": ic1, "ic2 (psi -2.5)": ic2}
    origin = str(config_path)

    after_text = (copy / AERO_SOURCE).read_text(encoding="utf-8")
    if after_text.count(MIGRATED_LINE) != 1:
        raise SystemExit("the copy's aero is not the migrated one")
    run(["cmake", "--preset", "release", "-B", str(build)], copy, env)
    jobs = str(os.cpu_count() or 1)
    run(
        ["cmake", "--build", str(build), "--parallel", jobs, "--target", *PLUGIN_TARGETS], copy, env
    )
    say(f"built {len(PLUGIN_TARGETS)} plugin targets in {build}")

    logs: dict[tuple[str, str], Path] = {}
    for ic, text in initial.items():
        logs[("after", ic)] = run_pass("after", ic, text, origin, build, work)

    base_text = run(["git", "show", f"{BASE}:aircraft/_template/physical/aero/aero.cpp"], ROOT)
    base_text = base_text.replace("aircraft_template_aero_math", f"aircraft_{AIRCRAFT}_aero_math")
    if base_text.count(INERTIAL_LINE) != 1 or "omega_eb_b" in base_text:
        raise SystemExit(f"{BASE}'s aero is not the inertial-rate one")
    (copy / AERO_SOURCE).write_text(base_text, encoding="utf-8")
    rebuild_aero(build, copy, env, "before")
    for ic, text in initial.items():
        logs[("before", ic)] = run_pass("before", ic, text, origin, build, work)

    (copy / AERO_SOURCE).write_text(after_text, encoding="utf-8")
    swap_aero(copy, MIGRATED_LINE, PLANTED_LINE, "planted")
    rebuild_aero(build, copy, env, "planted")
    for ic, text in initial.items():
        logs[("planted", ic)] = run_pass("planted", ic, text, origin, build, work)

    describes = {git_describe_of(path) for path in logs.values()}
    say(f"git_describe in every log: {sorted(describes)}")
    if describes != {describe}:
        raise SystemExit("the logs' git_describe is not HEAD's")

    ok = True
    control = planted_control(logs[("after", "ic1 (psi 0.6)")], work)
    say(f"planted control: {len(control)} difference(s): {control}")
    if not any(item.startswith("/final_state/") for item in control):
        say("FAIL: the comparison did not report the flipped sign bit")
        ok = False

    for ic in initial:
        differences = compare(logs[("before", ic)], logs[("after", ic)])
        expected = [f"/plugins sha256[{aero_row(logs[('after', ic)])}]"]
        holds = differences == expected
        ok = ok and holds
        say(f"before vs after, {ic}: {len(differences)} difference(s): {differences}")
        say(f"  expected exactly {expected}: {'holds' if holds else 'FAILS'}")

    for ic in initial:
        differences = compare(logs[("after", ic)], logs[("planted", ic)])
        aero_sha = f"/plugins sha256[{aero_row(logs[('after', ic)])}]"
        data = [item for item in differences if item != aero_sha]
        say(f"after vs planted (reported, never a gate), {ic}: {len(differences)} difference(s)")
        say(f"  beyond the aero's sha256: {len(data)}: {data[:20]}")

    worktree_clean("after")
    say(f"result: {'every gated check holds' if ok else 'A GATED CHECK FAILED'}")
    log.write_text("\n".join(LINES) + "\n", encoding="utf-8")
    return 0 if ok else 1


def main() -> int:
    if len(sys.argv) == 5 and sys.argv[1] == "--child":
        child(sys.argv[2], sys.argv[3], sys.argv[4])
        return 0
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    return parent(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())


if __name__ == "__main__":
    sys.exit(main())
