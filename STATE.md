# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M4 PR 3 (core seam: `Transport`, `LockstepExchange`, loopback, recorder, `kTransport`), Tier 3, **approved by Joe
2026-09-25**: product candidate `add18b1`; later commits are documents only (packet, evidence, this file). Branch
`worktree-m4-pr3-core-seam` off `origin/main` `794158d` (#52's merge, confirmed unmoved 2026-09-25),
[draft PR #53](https://github.com/jhawleyjr/ASTRA/pull/53), awaiting Joe's squash-merge (not merged; stays draft).
Plan: `docs/plans/m4-pr3-core-seam.md` (Q-A 8→9, Q-B scripted loopback, Q-C capacity from `sim.frames`, Q-D 5000 ms,
plus Joe's addition: with a transport every halt is sticky). Packet: `docs/reviews/m4-pr3-core-seam-human.md`
(decision record: approved). M4 plan: `docs/plans/m4-lockstep.md`.

## Last verified result
Full gate `./scripts/check-full.sh` on `add18b1`: `check: OK`, exit 0, four presets 450/450, pytest 116
(`docs/reviews/evidence/add18b1-full-gate.log`). A3 `ctest --preset asan -R '(Transport|Loopback|NoAlloc|SimulationTransport)'
--no-tests=error`: 63/63 incl. both new `NoAlloc` tests (`add18b1-a3.log`); M3 frozen regression passes unmodified.
Codex plan critique: 6 findings (F-1 to F-6), all accepted before approval. Codex adversarial on `d9ab7de`: approve,
no findings (`d9ab7de-codex-adversarial.log`); `add18b1` differs only by the Python API-version pin. GitHub `check`
(local-gate mode, not a full gate) was in progress on the PR head at handoff.

## Next step
1. Joe squash-merges PR #53. Then M4 PR 4 (`plugins/sim/transport/shm` + `embedded/hal` shm backend on the shared
   layout header, futex protocol, one-deadline waits; Tier 3, its own plan critique on futex ordering). PR 4 must
   honor `Transport::wait_ack`'s contract (one deadline per publication, then `kExpired`) and decide what
   `close(kAfterFailure)` does on the wire (ADR-0024).
2. Follow-up A (comment-only PR): PR 2's CAS table test comment nits (`docs/reviews/m4-pr2-icd-pack-human.md`).
3. Follow-up B (Tier 0 PR, authorized): citations in `util/frames.hpp` and `tests/golden/frames/adr0001_cases.hpp`
   comments (Joe arms the guard): DCM 1.3-10, Euler 1.3-11, Example 1.8-1 Eq. (1) p. 52, q to DCM 1.8-16/18.
4. Follow-up C (Joe, not scheduled): `LockstepExchange` recorder sizing for an absurd `sim.frames` (`bad_alloc` at
   init, not a `ConfigError`; `sim_frames + 1` wraps near `UINT64_MAX`).
PLAN.md untouched (M4 check-off, duplicate §15 bullets: M4 PR 7). Unscheduled, Joe-owned: the stale
`tests/golden/dynamics/capture_r1_golden.cpp` (retired `astra::Eom` symbols, ADR-0021) needs his authorization.

## Tried / failed
Correction rounds: 0/2. Failed integrations: 2/2 (pyright on `126cfed`: `int()` on a nanobind `enum.Enum`;
pytest on `d9ab7de`: `tests/python/test_package.py` still pinned API version 8). Logs removed at Joe's request, rows
kept in the packet. A new defect goes to Joe.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
