# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines; says nothing that changes at merge)

## In flight
M5 PR 2 (Tier 2): PR #64 (draft), branch `worktree-m5-pr2-hdf5-wrapper` off `origin/main` `19d535a`, plan
`docs/plans/m5-tooling.md` PR 2 row, binding ADR-0005 and ADR-0025 (Q7). Stage: human review. Joe's packet
is in PR #64's body. Code candidate `587ef23`; later commits add only evidence and this handoff.
The wrapper is `core/include/astra/signals/hdf5_file.hpp` and `core/src/signals/hdf5_file.cpp`, plus
`astra::Hdf5Error`. Settings: time tracking off; bounds (V18, V110), because V110 gives superblock v3 and a
crashed file then will not reopen. Tests: `tests/unit/signals/hdf5_file_test.cpp`, `hdf5_lsan_probe.cpp`.
No LSan suppression was needed, and Q7 byte identity holds, so the fallback is not used.

## Last verified result
This session, at `587ef23`: `./scripts/check-full.sh` exit 0 with `check: OK`
(`docs/reviews/evidence/587ef23-gate-summary.log`). `ctest --preset asan -R Hdf5 --no-tests=error` and the
same under `static-plugins` each exit 0, 21/21. Codex adversarial review of `4693cf7`: needs-attention (F1,
F2). Fix-verification of `587ef23`: approve, no new findings. Thread ids and findings are in the PR body and
in `docs/reviews/m5-pr2-hdf5-wrapper-review.md`. CI `check` has not run on this head: the PR is still a draft.

## Next step
1. Joe: decide on PR #64 (packet in its body). Before merge, `gh pr ready` runs CI's `check`, or
   `gh pr ready --undo` then `gh pr ready` after any later push.
2. M5 PR 3 (Tier 3): the derived signals per ADR-0026. `lockstep_exchange.cpp` stays untouched, and R10
   compares the recorder SHA-256 of base and candidate. It depends only on PR 1.
3. M5 PR 4 (Tier 3): the logger stage, on PR 2 and PR 3. It builds on `astra::hdf5`, adding compound tables
   for `/plugins` and `/writes` itself.
4. Optional, Joe's call: `H5PLset_loading_state(0)`. libhdf5 imports `dlopen` for filter plugins, though
   `astra_core` does not. PR 5 must also check h5py's bundled libhdf5 alongside ours in one process.
5. Before PR 8: Joe places FlightGear's `net_fdm.hxx` (version 24) in `docs/refs/` (Q21).

## Tried / failed
Correction and integration counts: PR #64's body holds them. The first gate ran at `4693cf7` before round 1;
it passed but is superseded. Untestable here: F1's rollback-after-write-failure and F2's `bad_alloc`
reclaim, because there is no HDF5 or allocator fault injection. Both are disclosed in the PR body.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
