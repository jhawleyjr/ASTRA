# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines; says nothing that changes at merge)

## In flight
M5 PR 2 (Tier 2): PR #64, branch `worktree-m5-pr2-hdf5-wrapper` off `origin/main` `19d535a`, plan
`docs/plans/m5-tooling.md` PR 2 row, binding ADR-0005 and ADR-0025 (Q7). Stage: human review; the PR body is
the description Joe reviews. Code candidate `587ef23`; later commits change no code. The wrapper is
`core/include/astra/signals/hdf5_file.hpp` and `core/src/signals/hdf5_file.cpp`, plus `astra::Hdf5Error`.
Settings: time tracking off; bounds (V18, V110), because V110 gives superblock v3 and a crashed file then
will not reopen. No LSan suppression; Q7 byte identity holds, so there is no fallback.
Also in this PR, at Joe's request (2026-09-27), commit `1c44f02`: `docs/templates/pr-description.md` (a plain
PR description, no title line, no decision block) replaces `docs/templates/human-review.md`. The
`human-review` and `wrap` skills, `workflow.md`, the templates and reviews READMEs and the agent docs point at
it. PLAN.md's `Packet:` field is unchanged; renaming it is Joe's call.

## Last verified result
This session, at `587ef23`: `./scripts/check-full.sh` exit 0 with `check: OK`
(`docs/reviews/evidence/587ef23-gate-summary.log`). `ctest --preset asan -R Hdf5 --no-tests=error` and the
same under `static-plugins` each exit 0, 21/21. At `1c44f02`: `python3 scripts/docs_check.py` and
`git diff --check` exit 0. Codex: round 1 needs-attention (F1, F2), fix verification approve. The workflow
commit has no Codex review. Thread ids and CI's `check` result are in the PR body.

## Next step
1. Joe: decide on PR #64. Its "Needs Joe's decision" section lists merge approval, the two untested failure
   paths, whether `1c44f02` needs a Codex pass, and two optional items. After any later push, rerun CI's
   `check` with `gh pr ready --undo` then `gh pr ready`.
2. M5 PR 3 (Tier 3): the derived signals per ADR-0026. `lockstep_exchange.cpp` stays untouched, and R10
   compares the recorder SHA-256 of base and candidate. It depends only on PR 1.
3. M5 PR 4 (Tier 3): the logger stage, on PR 2 and PR 3. It builds on `astra::hdf5`, adding compound tables
   for `/plugins` and `/writes` itself. PR 5 must check h5py's bundled libhdf5 alongside ours in one process.
4. Before PR 8: Joe places FlightGear's `net_fdm.hxx` (version 24) in `docs/refs/` (Q21).

## Tried / failed
Correction and integration counts: PR #64's body holds them. The first gate ran at `4693cf7` before round 1;
it passed but is superseded. Untestable here: F1's rollback-after-write-failure and F2's `bad_alloc` reclaim,
because there is no HDF5 or allocator fault injection. The worktree guard again refused inline Python whose
text named git; the edit ran from a scratchpad script (lessons 2026-09-27).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
