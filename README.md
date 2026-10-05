# ASTRA — Aerospace Simulation, Test, and Research Architecture

ASTRA is a vehicle-agnostic six-degree-of-freedom simulation framework, with a C++23 core and Python bindings,
for aircraft and atmospheric flight. Its purpose is to run an aircraft's real embedded flight software against
the simulation over a lockstep transport: all-digital first, hardware in the loop later. The framework is the
product ([ADR-0019](docs/decisions/0019-vehicle-agnostic-purpose.md)). You build a vehicle from plugins without
editing the core. `aircraft/notional_transport` is the worked example: an example twin-jet, not a validated
airliner. The F-16 joins in M7, checked against the NESC atmospheric check cases. The guidance, navigation and
control software is the project's own; no third-party flight stack runs in the loop
([direction](docs/future-work.md#direction-joe-2026-09-29)).

## Features

Everything below is on `main` today.

- **6-DoF core.** Body-axis rigid-body equations of motion (`RigidBody6DofDynamics`, Stevens & Lewis form)
  integrated by an in-house fixed-step RK4 at 1 kHz, with forward Euler kept for tests
  ([ADR-0004](docs/decisions/0004-fixed-step-rk4-in-house.md)). Wrenches are evaluated at every RK4 sub-stage
  ([ADR-0016](docs/decisions/0016-per-rk4-substage-wrench-evaluation.md)), and a model may own continuous
  state of its own ([ADR-0021](docs/decisions/0021-dynamics-model-owned-state.md)). The quaternion is
  renormalized after every step. The per-frame path does no heap allocation, and a test holds it to that
  (`tests/physics/no_alloc_test.cpp`).
- **Conventions.** FRD body axes, NED navigation frame, and a Hamilton scalar-first `q_nb`
  ([ADR-0001](docs/decisions/0001-frames-and-attitude.md)); SI units inside, converted only at the boundaries
  by `core/util/units` ([ADR-0002](docs/decisions/0002-si-units-and-signal-metadata.md)); an integer frame
  counter as the source of time ([ADR-0010](docs/decisions/0010-integer-frame-counter-time.md)).
- **Environment models** ([plugins/physical/](plugins/physical/)). The ISA-1976 atmosphere; uniform, tabulated
  and Dryden wind (MIL-F-8785C, seeded); constant, inverse-square and WGS-84 J2 gravity; flat, round and
  rotating WGS-84 Earth models; constant mass properties.
- **Plugins and the aircraft template.** Every selectable model is its own shared object. The build discovers
  it, a config names it by path, and it is loaded at init and checked against the core's API version and
  toolchain ([ADR-0009](docs/decisions/0009-plugin-architecture.md)). A static-plugins build links them all
  into one binary instead. A plugin's `CMakeLists.txt` is one line. `aircraft/_template` and `just
  new-aircraft` start a new aircraft whose own tests build and run with no framework edit.
- **The notional transport** ([aircraft/notional_transport/](aircraft/notional_transport/)). Linear-coefficient
  aero, constant mass, and a thrust-lapse turbofan, with parameters from JSBSim's example 737. Cruise configs
  for still air and for light and moderate Dryden turbulence.
- **Trim and linearization.** `astra.trim` solves straight-and-level trim on `Simulation.derivative`.
  `astra.linearize` builds central-difference A and B matrices and reports the short-period, phugoid,
  Dutch-roll, roll and spiral modes.
- **Lockstep transport and the embedded autopilot.** The ICD is versioned C11 structs
  ([icd/astra_icd.h](icd/astra_icd.h), [ADR-0006](docs/decisions/0006-icd-versioned-c11-structs.md)),
  exchanged over shared memory with futex sequence words
  ([ADR-0022](docs/decisions/0022-lockstep-exchange-stage.md),
  [ADR-0024](docs/decisions/0024-shm-layout-header-and-protocol.md)). The peer is
  [embedded/autopilot](embedded/autopilot/), a C++17 straight-and-level autopilot (altitude, airspeed and
  heading hold) in its own process. It links only `icd/` and `embedded/hal/`, never the core
  ([ADR-0008](docs/decisions/0008-monorepo-embedded-separate-cmake.md)). `astra.lockstep` drives the pair. A
  missed exchange or a dead peer halts the run.
- **HDF5 run logs and replay.** An optional `log` block records every registered signal. Two runs of one config
  give byte-identical files, and a log is published only on a clean close
  ([ADR-0025](docs/decisions/0025-hdf5-run-log-and-replay.md),
  [ADR-0027](docs/decisions/0027-run-log-finalization.md)). `astra.log.load` reads a log into pandas.
  `astra.log.replay` re-executes the run and compares the bytes.
- **Scenarios.** `astra.scenario` runs YAML scenarios (a config, optional trim, input events, checks on logged
  signals). Every file in `tests/scenarios/` is a pytest item.
- **Monte Carlo.** `astra.montecarlo` runs dispersed batches across worker processes. Each run's seed comes from
  the master seed and the run index. It writes one log per run and a `summary.parquet`, which also records the
  spec and base config the batch ran.
- **FlightGear bridge.** `astra.fgbridge` sends FlightGear FGNetFDM v24 packets over UDP from a live run or a
  log.
- **The `astra` CLI.** `astra run` runs a config open loop, prints a post-run summary and exits with a code
  that says what happened. It refuses a config with a `transport` block until M10. `astra trim` prints a trim
  and the config fragment that applies it. `astra plot` renders a plot package against a run log.
  `astra mc analyze` analyzes a Monte Carlo batch, and `astra mc rerun` re-executes one of its runs.
  `astra mc size` sizes a batch from the claim it must answer, or checks that claim against a batch.
  `astra report` writes one self-contained HTML report of a log, a scenario's log or a batch.
- **Monte Carlo analysis.** `astra.mc_analysis` draws a batch's draws by outcome and a signal's envelope across
  its run logs. It ranks the dispersed parameters against a pass/fail criterion (KS with Benjamini-Hochberg)
  or a continuous metric (Spearman and partial rank correlation), and fits a depth-3 tree for interactions.
  `astra mc rerun` re-executes one run from the batch's record of its spec, with full logging and plots.
- **Monte Carlo sizing.** `astra.mc_sizing` sizes a batch from the claim it answers: a failure probability,
  a percentile, a statistic's precision, or a parameter ranking. On a batch, probability and percentile give
  exact Clopper-Pearson verdicts, and precision and ranking give nominal projections.
- **Plot packages.** `astra.plot` draws reusable YAML plot packages over any run log: deterministic SVG (or PNG)
  files through matplotlib, or one offline interactive HTML page through plotly.
- **Health monitoring.** Every check is a signal in the log and names its source. NaN or Inf in the state, a
  wrench, a control input or the environment; quaternion-norm drift; and per-frame domain validity
  ([ADR-0012](docs/decisions/0012-per-frame-domain-validity.md)) are each `off`, `warn` or `halt`
  (`sim.health`). Transport and log failures always halt. The envelope, rate and real-time checks in
  [architecture §15](docs/architecture.md#15-health-monitoring) are not built yet.
- **Verification.** NASA's NESC atmospheric check cases 1 to 10 (`tests/physics/nesc/`,
  [ADR-0013](docs/decisions/0013-nesc-check-case-oracle.md)), ISA table points, the Dryden PSD, torque-free
  energy and momentum conservation, and RK4's convergence order against Euler.
- **Frame-time benchmark.** `just benchmark <out.json>` times each `step()` of the notional transport's
  cruise configs and of `tests/configs/dummy_1000.yaml`, and logged runs end to end through `close_log()`, in a
  Release build. It writes JSON with the
  mean, median, p99 and maximum frame cost and the margin against `dt`, only after checking that every timed
  frame ran. The gate runs it only as a smoke, which asserts no speed.

## Quick start

ASTRA builds on Ubuntu 24.04 with apt packages only: clang-18, CMake 3.28+, Ninja, just, Eigen 3.4, the HDF5 C
API, yaml-cpp and GoogleTest, plus Python 3.12+. There is no vcpkg or Conan. From the repository root:

```sh
./scripts/setup-ubuntu.sh    # once: apt packages (sudo) and the .venv dev tools
just build                   # the asan preset; `just build release` for an optimized build
./scripts/check-fast.sh      # build and run the unit tests
./scripts/check-full.sh      # the whole gate: four presets, physics, Python, static analysis (tens of minutes)
just python                  # build astra._core and install the astra package into .venv
source .venv/bin/activate
astra trim aircraft/notional_transport/configs/cruise.yaml
astra run aircraft/notional_transport/configs/cruise.yaml --log build/cruise.h5
```

`./scripts/check-full.sh` passes only on exit 0 with its final `check: OK` line. `astra run` refuses a log path
that already exists, so choose a new one for each run.

## How-tos

The Python snippets run from the repository root, with `.venv` active after `just python`.

### Run a config

`astra run <config> [--strict] [--log PATH]` runs `sim.frames` frames, closes the log and prints the summary. It
exits 0, or 1 on a halt, 2 under `--strict` when a warn counter is nonzero, 64 on a usage error, 65 on a config
or plugin error, 70 on an internal error (`EX_SOFTWARE`, a defect, with its traceback on stderr) and 74 when the
log cannot be written, closed or published or the summary cannot be written to stdout
([python/astra/cli.py](python/astra/cli.py)). From Python:

```python
from astra import _core

config = _core.Config.from_file("aircraft/notional_transport/configs/cruise.yaml")
sim = _core.Simulation.from_config(config, _core.default_plugin_dir())
halt = sim.run()
sim.close_log()  # publishes the log when the config has a log block; without it, no log is published
print(sim.clock.frame, halt.check.name if halt else "no halt")
```

Config keys are in [architecture §13](docs/architecture.md#13-configuration). An unknown key is an error.

### Trim and linearize

```python
from astra import linearize, trim

config = "aircraft/notional_transport/configs/cruise.yaml"
t = trim.trim_straight_and_level(config, 230.0, 10000.0)  # true airspeed m/s, altitude m
print(t.elevator_rad, t.throttle, t.alpha_rad)
lin = linearize.linearize(config, 230.0, 10000.0)
print(linearize.modes(lin.a))
```

Trim is still-air, straight and level, and needs a round or WGS-84 Earth.
See [trim.py](python/astra/trim.py) and [linearize.py](python/astra/linearize.py).

### Log, load and replay

Add `log: { path: run.h5 }` to a config, or pass `astra run --log run.h5`. Then:

```python
from astra import log

run = log.load("build/cruise.h5")       # strict: refuses a log that was not closed cleanly
print(run.frames_run, run.halt, list(run.rates[1].columns)[:5])
print(log.replay("build/cruise.h5", "build/cruise_replay.h5"))  # True: byte-identical
```

The file layout and the finalization rules are in [ADR-0025](docs/decisions/0025-hdf5-run-log-and-replay.md)
and [ADR-0027](docs/decisions/0027-run-log-finalization.md).

### Plot a log

```sh
astra plot build/cruise.h5 flight --out build/plots          # a framework package: flight, attitude, health
astra plot build/cruise.h5 aircraft/notional_transport/plots/cruise.yaml --out build/plots --html
```

A plot package is a YAML file of figures, panels and series. `{v}` in a signal name stands for each vehicle's
index, and `unit:` picks a display unit (ft, kt, ft/s, deg, deg/s), converted by `core/util/units`. Each figure
becomes `<package>-<figure>.svg` (`--format png` for PNG); `--html` adds one interactive page that works offline.
That page holds every sample. On a long run, `--html-columns N` keeps it small: each trace keeps each of N
columns' ends and extremes, and the figure's title says so. No file is overwritten. An aircraft keeps its own packages in `aircraft/<name>/plots/`; the template's starter
set includes a plot drawn by a Python function. The format is in [plot.py](python/astra/plot.py).

### Write a scenario

A scenario names a config, an optional trim, input events and checks on logged signals
([tests/scenarios/elevator_step.yaml](tests/scenarios/elevator_step.yaml)):

```yaml
config: aircraft/notional_transport/configs/cruise_still_air.yaml
duration: 1.5
events:
  - { at: 1.0, write: controls.elevator, value: 0.016674474137989947 }
checks:
  - signal: v0.derived.q
    window: { from: 1.1, to: 1.5 }
    between: { lo: 1.6e-3, hi: 8.8e-3 }
```

Put it in `tests/scenarios/` and run `.venv/bin/pytest tests/scenarios`. The schema is in
[scenario.py](python/astra/scenario.py)'s docstring.

### Run a Monte Carlo batch

```python
from pathlib import Path

from astra import montecarlo

SPEC = """\
config: aircraft/notional_transport/configs/cruise.yaml
trim: {true_airspeed: 230.0, altitude: 10000.0}
duration: 5.0
runs: 4
seed: 7
dispersions:
  physical.mass.mass: {normal: {sigma: 0.02, mode: relative, clip: 3.0}}
  physical.wind.seed: seed
signals: [v0.derived.altitude]
"""

if __name__ == "__main__":  # the workers are spawned processes, which re-import this script
    spec = Path("build/mc_spec.yaml")
    spec.write_text(SPEC)
    montecarlo.run(spec, "build/mc_batch", workers=4)  # run_0000.h5 ... and summary.parquet
```

Run it as a script: the workers start with `spawn`, so the batch must sit under a `__main__` guard.

The spec format is in [montecarlo.py](python/astra/montecarlo.py)'s docstring. The 64-run acceptance case is
[cruise_64.yaml](tests/python/montecarlo_fixtures/cruise_64.yaml).

### Analyze a batch

```bash
astra mc analyze build/mc_batch --signal v0.derived.altitude --out build/mc_figures
astra mc analyze build/mc_batch --criterion 'status == halt or max.v0.derived.alpha > 0.3' \
    --metric max.v0.derived.alpha
astra mc rerun build/mc_batch 3 --out build/mc_run_3   # one run again, with its log and plots
```

`analyze` prints the outcome counts and each ranking with its effect sizes, p-values and Benjamini-Hochberg
decisions, then the tree's rules. It writes `mc-hist`, `mc-scatter` and one `mc-envelope-<signal>` figure. The
criterion grammar and the methods are in [mc_analysis.py](python/astra/mc_analysis.py)'s docstring. `rerun`
needs a batch written by this version or later, which records its spec. Its plugins come from the current
build, so a rebuild can change the result, and the comparison it prints shows that.

### Size a batch

```bash
astra mc size probability --p-max 0.01                     # 299 runs: P(fail) <= 1 % at 95 %, none failing
astra mc size percentile --quantile 0.95 --two-sided       # 93 runs: Wilks 95/95, two-sided
astra mc size probability --p-max 0.01 --batch build/pilot # the N a fresh final batch needs
astra mc size check build/mc_batch claims.yaml             # every claim against the final batch
```

Probability and percentile give calibrated verdicts (met, violated or undetermined). Precision and ranking
give nominal projections, never verdicts. A pilot only sizes: run the final batch with another master seed
and take the verdict from it alone. The options, the claims file and the stated limits are in
[mc_sizing.py](python/astra/mc_sizing.py)'s docstring.

### Report a run

```bash
astra report build/cruise.h5 --out build/cruise.html            # flight, attitude and health figures
astra report build/step.h5 --scenario tests/scenarios/elevator_step.yaml --out build/step.html
astra report build/mc_batch --claims claims.yaml --signal v0.derived.altitude --out build/mc.html
```

A report is one HTML file that opens offline: no script, nothing loaded from outside it, and the same bytes
from the same inputs. A log report holds the summary (frames, halt, health counters), every figure of its plot
packages (`--package` replaces the default three) and the provenance read from the log, its config text
included. With `--scenario`, it adds each check's rows, worst value and result. A batch report holds the
outcome counts, the sizing check of a claims file, the rankings and tree of `astra mc analyze` (the same
`--criterion`, `--metric` and `--q`) and its figures. The sections are in [report.py](python/astra/report.py)'s
docstring.

### Stream to FlightGear

Start FlightGear with `fgfs --fdm=null --native-fdm=socket,in,62.5,,5500,udp`, then send a log:

```python
from astra import fgbridge

fgbridge.send_log("build/cruise.h5")  # paced to wall time, UDP 127.0.0.1:5500
```

`fgbridge.LiveBridge` streams a running `Simulation` instead. A flat-Earth config is refused, because FlightGear
needs latitude and longitude. See [fgbridge.py](python/astra/fgbridge.py).

### Run lockstep with the embedded autopilot

```python
from pathlib import Path

from astra import _core
from astra import lockstep as ls

config_path = "aircraft/notional_transport/configs/cruise_light_turbulence.yaml"
setpoints = ls.Setpoints(altitude_m=10000.0, airspeed_mps=230.0, heading_rad=0.0)
trim = ls.trim_for(config_path, setpoints)
config = ls.load_config(config_path, segment=ls.new_segment_name(), frames=10000)
autopilot = Path(_core.default_plugin_dir()).parent / "embedded/autopilot/astra_autopilot"
result = ls.run_closed_loop(config, ls.autopilot_argv(autopilot, config, setpoints, trim))
print(result.halt, result.exchanges, result.missed)
```

The config's `transport` block selects the shared-memory transport and maps ICD commands to inputs.
`astra.lockstep` supplies the segment name, launches and reaps the peer, and cleans up the segment. See
architecture §10 and §11 ([architecture.md](docs/architecture.md)) and
[lockstep.py](python/astra/lockstep.py). Your own flight software builds against `icd/` and `embedded/hal/`.

### Add an aircraft

```sh
just new-aircraft my_aircraft   # copies aircraft/_template, rewriting plugin paths and namespaces
just build                      # discovers aircraft/my_aircraft and builds its plugins and tests
just python                     # rebuilds build/python, whose plugins the astra command loads
astra trim aircraft/my_aircraft/configs/cruise.yaml
astra run aircraft/my_aircraft/configs/cruise.yaml
```

Before the last two commands, fill in your aircraft. The template's numbers are zeroed and marked `TODO`, and
zeros are refused at init. Its models live in `aircraft/my_aircraft/physical/{aero,mass}` and
`hardware/propulsion`, and their parameters are set in `configs/cruise.yaml`.

The template's config uses a flat Earth, which `astra run` accepts and `astra trim` refuses, because trim reads
altitude from a geodetic position ([PLAN.md](PLAN.md)'s open M7 item). For `astra trim`, make these edits to
`configs/cruise.yaml`:

1. `physical.earth: plugins/physical/earth/wgs84_rotating`, in place of `plugins/physical/earth/flat`.
2. `initial.state.p_n: [0.0, 0.0, 10000.0]`. Under this Earth model it reads as latitude (rad), longitude
   (rad) and altitude (m).
3. A nonzero `initial.state.v_b`, such as `[230.0, 0.0, 0.0]`, or pass `--airspeed 230` to `astra trim`.

`gravity/constant` can stay. `astra trim` prints the trimmed `initial.state` and controls to paste back into the
config.

The copy carries the template's plot packages in `aircraft/my_aircraft/plots/`. After `astra run ... --log
build/my_aircraft.h5`, `astra plot build/my_aircraft.h5 aircraft/my_aircraft/plots/controls.yaml` draws them.

The steps are [architecture §18](docs/architecture.md#18-how-a-user-adds-an-aircraft-the-on-ramp-this-design-must-keep-true).
If one needs more than it says, that is a framework bug.

### Write a plugin

1. Subclass an interface in [core/include/astra/interfaces/](core/include/astra/interfaces/), such as
   `AtmosphereModel`, `AeroModel` (override `wrench_b`) or `PropulsionModel`.
2. Export one descriptor: `ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin()`.
3. Put `astra_add_plugin(SOURCES my_model.cpp)` in the directory's `CMakeLists.txt`, under `plugins/<kind>/` or
   `aircraft/<name>/<kind>/`. The plugin's path is derived from its directory.
4. `just build`, then `just python`, because Python and the `astra` command load plugins from `build/python`,
   which `just build` does not rebuild. Then name the path in a config, for example
   `atmosphere: plugins/physical/atmosphere/isa1976`.

[plugins/physical/atmosphere/isa1976/](plugins/physical/atmosphere/isa1976/) is a complete example. `step()` and
`wrench_b()` must not allocate, throw or do I/O. Init may do all three, and its errors name the config key. A
test executable that loads plugins registers itself with `astra_add_plugin_test()`. See
[architecture §4 and §7](docs/architecture.md#4-repository-layout-and-the-plugin-model).

## Architecture at a glance

The scheduler is single-threaded. Each 1 ms major frame runs: environment (atmosphere, wind, gravity) →
derived flight-condition signals → transport exchange, when due → each model's `step()` → input latch → RK4,
with every vehicle wrench re-evaluated at each sub-stage → post-step (quaternion renormalization) → logger
sample. `Simulation` holds a list of vehicles, of length one in v1
([ADR-0011](docs/decisions/0011-one-vehicle-as-list-of-one.md)).

Plugins live in two roots. `plugins/` holds shared models, filed by kind (`physical/`, `hardware/`, `sim/`).
`aircraft/<name>/` holds one aircraft's own models, filed by the same kinds. A config picks one plugin per slot
from either root. Anything that knows a wing area or a thrust curve is a plugin, never the core.

Conventions that are easy to get wrong:

| Topic | Convention |
|---|---|
| Frames | Body FRD (x forward, y right, z down); navigation NED |
| Attitude | `q_nb` rotates n to b; Hamilton, scalar-first `(w, x, y, z)`; Euler angles 3-2-1, outputs only |
| State | `p_n(3) v_b(3) q_nb(4) ω_b(3)`; `p_n` is NED meters on a flat Earth, geodetic latitude, longitude (rad) and altitude (m) on a round one |
| Units | SI inside the core, plugins and aircraft; signal names carry no unit suffix, the registry carries the unit |
| Time | `t = frame × dt`, never accumulated; `dt = 1 ms`, and every rate is an integer divisor of it |
| Randomness | `std::mt19937_64` with in-house transforms; every stochastic plugin takes a seed from config |

The design is in [docs/architecture.md](docs/architecture.md), and the decisions are in
[docs/decisions/](docs/decisions/). [ADR-0000](docs/decisions/0000-founding-decisions.md) indexes the decisions
made before code existed and the open questions deliberately deferred.

## Repository layout

| Path | Contents |
|---|---|
| `core/` | `libastra_core`: kernel, interfaces, plugin loader, config, signals, logging, utilities |
| `plugins/` | Shared models by kind: environment, mass, transports |
| `aircraft/` | One folder per aircraft: `_template/` and `notional_transport/` |
| `icd/` | `astra_icd.h`, the C11 frames exchanged with flight software |
| `embedded/` | A separate C++17 project: the HAL and the example autopilot |
| `python/` | The `astra` package (`trim`, `linearize`, `log`, `scenario`, `montecarlo`, `mc_analysis`, `mc_sizing`, `plot`, `report`, `fgbridge`, `lockstep`, `cli`) and the nanobind bindings |
| `tests/` | Unit, physics, lockstep and Python tests, scenarios, the frame-time benchmark, and human-owned goldens |
| `scripts/` | Setup, check wrappers and docs and scan tools |
| `cmake/` | The plugin helpers (`astra_add_plugin`, `astra_add_plugin_test`) |
| `docs/` | Architecture, ADRs, process, plans and reviews ([index](docs/README.md)) |
| `.claude/`, `.github/` | Agent hooks and skills; CI and the `main` ruleset |
| `justfile`, `CMakePresets.json`, `pyproject.toml` | Commands, build presets, the Python package |

## Verification

Oracles are independent of the code under test, and they are human-owned. Goldens, tolerances and accepted ADRs
change only with Joe's explicit authorization. The NESC cases are scored against the spread of the participating
simulations ([ADR-0013](docs/decisions/0013-nesc-check-case-oracle.md)).

`./scripts/check-fast.sh` wraps `just test-unit`, for iterating. `./scripts/check-full.sh` wraps `just check`,
the definition of done. It builds and tests the debug, release, static-plugins and asan presets, then runs the
wall-clock scan, the embedded standalone build, Python tests, formatting, clang-tidy, pyright, the hook cases and
the docs check. The [justfile](justfile) is the source of truth for every command. CI runs with `GATE=local` to
save GitHub Actions minutes, so a green `check` on GitHub is not evidence that the full gate ran. See
[validation](docs/process/validation.md).

## Roadmap

M0 to M6 are done: bootstrap, rigid body, environment, the notional transport, lockstep with the embedded
autopilot, tooling, and analysis tooling. Next, in order:

- **M7 F-16 and the NESC oracle:** `aircraft/f16` from the template; NESC cases 11, 12, 13.1 to 13.4, 15 and
  16; a tutorial and a config-key reference.
- **M8 Actuators and controller tuning:** actuator models with rate and position limits, gains loaded at
  startup, a tuning tool, frequency-domain and handling-qualities analysis.
- **M9 Sensors and ICD v2:** IMU, GNSS, barometric, air-data and magnetometer models; nonstandard days, gusts,
  shear and microbursts.
- **M10 Navigation, faults and closed-loop Monte Carlo:** an INS/GNSS EKF in `embedded/`, fault injection, and
  closed-loop scenarios and Monte Carlo.
- **M11 Real-time:** `RealTime` mode on a PREEMPT_RT kernel, with the frame-time benchmark tracked over time.

Later work includes ground contact, fuel burn, and the FPGA link for hardware in the loop.
[PLAN.md](PLAN.md) holds the checklist and open items, and
[architecture §16](docs/architecture.md#16-verification-map) holds each milestone's acceptance.
[Future work](docs/future-work.md) holds the detail.

## Development

AI agents do the work under a written workflow. Claude plans, builds, validates, fixes and integrates. Codex
reviews each candidate independently, in fresh context. Joe is the sole maintainer: his explicit decision or his
squash-merge is the approval record, and he owns requirements, architecture and the oracles. Each task has its
own branch and worktree. PRs start as drafts, the depth of process follows a risk tier, and
[STATE.md](STATE.md) is the session handoff. No release pipeline is configured yet.

The rules are in [AGENTS.md](AGENTS.md), and the procedure is in [the workflow](docs/process/workflow.md).
[docs/README.md](docs/README.md) indexes the documentation, and [docs/templates/](docs/templates/) holds the
optional artifacts.

## References

`docs/refs/` holds reference documents placed by hand: MIL-F-8785C, the NESC check-case reports and
trajectories, and FlightGear sources. They are gitignored and never committed. The tree cites them by page. A
git worktree does not contain them, so read them from the main checkout. [docs/refs/README.md](docs/refs/README.md)
lists each document and what cites it.
