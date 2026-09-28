# Review: M5 PR 8, `astra.fgbridge`

Spec/plan: [plan](../plans/m5-tooling.md) PR 8 row (Q21 to Q23; R18 to R20), Joe's plan-mode amendments in
PR #70's body; [ADR-0026](../decisions/0026-derived-flight-condition-signals.md),
[ADR-0025](../decisions/0025-hdf5-run-log-and-replay.md) (row timing, drain points)
Builder: Claude Opus 5.5
Reviewer: Codex (companion 1.0.6; fresh context; one general review and one adversarial review, both
read-only)
Scope: branch diff `3b5ca4a..f9dbf07` (PR #70; code candidate `8c80489`, `f9dbf07` adds only its gate
summary): `tests/golden/flightgear/fgnetfdm_v24.py`, `scripts/fgnetfdm_layout_check.py`,
`python/astra/fgbridge.py`, `tests/python/test_fgbridge.py`, `PLAN.md`, `docs/process/validation.md`
Validation: `./scripts/check-full.sh` exit 0 with `check: OK` at `8c80489`
([summary](evidence/8c80489-gate-summary.log)); acceptance `.venv/bin/pytest tests/python -k fgbridge`
exit 0, 25 passed
Round: fix 1 (correction round 1 of 2)

## Finding R-001

Severity: major
Status: fixed-unverified
Location: `python/astra/fgbridge.py:247-275` (`LiveBridge.step`/`run`) at `8c80489`
Requirement/ADR: ADR-0025 (drains happen outside `step()`: `run()` between frames, `flush_log()` for
step-driven callers); plan R19 (the live source works for a logged run of any length)
Failure: raised by both passes (general P2; adversarial high). `LiveBridge` drives the run with
`Simulation.step()`, which reserves and commits rows but never drains; only `Simulation.run()` drains
(`core/src/kernel/simulation.cpp:797-807`). A logged live run therefore halts `LOG` once the ring fills
(frame 4096 at the default `buffer_frames`), sticky. The live tests ran at most 400 frames.
Requested outcome: drain between bridge-driven frames, propagate a drain failure, keep the packet of a
frame that completed; a small-buffer regression beyond capacity compared against the log source.
Builder response: fixed. `LiveBridge.step()` calls `sim.flush_log()` after every step, the rule
`astra.lockstep.Session.step` already follows. It returns the step's halt if there is one, else the
drain's. A completed frame's packet is sent before the drain, so a failed drain still leaves frame k's
packet sent (its row was committed). Module and method docstrings say so. New tests:
`test_the_live_bridge_drains_the_log_past_its_ring_capacity` (`buffer_frames: 8`, 100 frames through
`step()` and through `run()`, live packets byte-identical to `send_log`'s) and
`test_a_failed_drain_is_returned_after_the_completed_frames_packet` (a proxy whose `flush_log()` returns
a real halt: `step()` returns it after frame 0's packet). Scratch mutations R1a (drain removed) and R1b
(drain halt dropped) fail them; the earlier 17 mutants are still killed on the fixed code.
Verification: pending
History: round 1, open → fixed-unverified
