# Review: M5 PR 4, the run log stage in Simulation

Spec/plan: [ADR-0025](../decisions/0025-hdf5-run-log-and-replay.md),
[m5-tooling.md](../plans/m5-tooling.md) (Run log contract, PR 4 row)
Builder: Claude (Opus 5.5)
Reviewer: Codex (companion 1.0.6; fresh context; general review, then adversarial review)
Scope: committed branch diff `6efd1b1..1d69d99` (code as of `3f74e37` plus the gate evidence file),
clean tree. General review thread `01a0e41e-50a2-78a0-98db-de097944d3d5`: no actionable defects.
Adversarial review thread `01a0e41e-50f5-7c72-b3c0-cdcff1e023bd`: needs-attention, F1 to F3 below.
Validation: full gate at `3f74e37` exit 0, `check: OK`
([summary](evidence/3f74e37-gate-summary.log)); the fix batch's own validation is in the PR body.
Round: fix 1

## Finding [F1]

Severity: major (reported high)
Status: fixed-unverified (partly; the rest is needs-human)
Location: `core/src/kernel/run_log.cpp:383-389` at `1d69d99`
Requirement/ADR: ADR-0025 Finalization ("never sets complete = 1 after any failure")
Failure: `complete = 1` was set before the ring datasets and groups were closed, so a close failure
after the marker could leave the marker on disk while `close_log()` threw.
Requested outcome: publication conditional on a successful close, with coverage after the marker.
Builder response: accepted in part. Every object but the file now closes, and the file flushes,
before the marker; the marker is the last write and reaches the disk inside the file's own close.
It certifies only data already flushed. The remaining window (the file's close itself failing after
writing part of the header) cannot be closed within one HDF5 file: a rename-on-complete or sidecar
protocol would change ADR-0025's contract, so it goes to Joe (PR body, "Needs Joe's decision"). The
existing `kComplete` injection still fires before the marker; no injection can model a partial
close.
Verification: pending
History: round 1, open -> fixed-unverified / needs-human (residual window)
Human exception if needed: pending

## Finding [F2]

Severity: major (reported high)
Status: fixed-unverified
Location: `core/src/signals/hdf5_file.cpp:56-60` at `1d69d99`
Requirement/ADR: ADR-0025 Failure (a failed drain write is a halt, not a crash); the exit workaround
this PR added
Failure: `H5dont_atexit()`'s result was ignored. Once libhdf5 is initialized (by a host or another
extension), it fails (checked with libhdf5 1.10.10: -1 after `H5open()`), HDF5's own exit hook stays
installed, and a real write failure can still crash the process at exit.
Requested outcome: check the result; refuse logging with a typed diagnostic when safe teardown cannot
be established; a subprocess test that initializes HDF5 first.
Builder response: fixed. `H5dont_atexit()` now runs when libastra_core loads; if it fails,
`File::create` refuses to write, so a log is a `ConfigError` naming `log.path`. Test:
`tests/python/test_simulation.py::test_a_log_is_refused_when_libhdf5_was_initialized_before_astra`
(a child process calls `H5open()` through ctypes before importing `astra._core`).
Verification: pending
History: round 1, open -> fixed-unverified

## Finding [F3]

Severity: minor (reported medium)
Status: fixed-unverified
Location: `core/src/kernel/simulation.cpp:496-498` at `1d69d99`; `python/astra/trim.py`
Requirement/ADR: plan m5-tooling.md (trim works on a config without its `log` block)
Failure: trimming (or linearizing) a YAML that contains `log` built a `Simulation` with the log, so it
created and finalized a zero-frame file at the run's path; the real run then found the path taken.
Requested outcome: strip `log` from analysis configs; a trim-then-run regression.
Builder response: fixed. `trim._load_still_air_config`, which `astra.linearize` and
`astra.lockstep.trim_for` also use, drops `log`. Test:
`tests/python/test_trim.py::test_trim_of_a_logged_config_creates_no_log_file`.
Verification: pending
History: round 1, open -> fixed-unverified

## Completion

Pending Codex fix verification. Open critical: 0. Open major: 0 once verified, apart from F1's
residual window, which needs Joe's decision.
