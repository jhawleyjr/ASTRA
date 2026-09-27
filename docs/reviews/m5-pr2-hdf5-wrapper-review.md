# Review: M5 PR 2, HDF5 RAII wrapper

Spec/plan: [docs/plans/m5-tooling.md](../plans/m5-tooling.md), PR 2 row (Q7); ADR-0005, ADR-0025
Builder: Claude Opus 5.5
Reviewer: Codex (companion 1.0.6, `adversarial-review`, fresh context, read-only)
Scope: committed branch diff `19d535a..4693cf7` (`core/signals/hdf5_file.*`, `Hdf5Error`, CMake, the Hdf5
tests and LSan probe, `PLAN.md`); the worktree was clean
Validation: `./scripts/check-full.sh` exit 0 with `check: OK` at `4693cf7`; the fix commit's own gate and
selector results are in the PR body
Round: fix 1 (F1 and F2)

## Finding [F1]

Severity: minor (Codex: medium; only after an HDF5 write failure, which ADR-0025 already makes a terminal halt)
Status: fixed-unverified
Location: `core/src/signals/hdf5_file.cpp` `Dataset::append_rows`, lines 541 to 550 at `4693cf7`
Requirement/ADR: the header's append contract (extent unchanged on failure); ADR-0025 Failure
Failure: after `H5Dset_extent` grows the dataset, a failed write shrinks it back, but the shrink's result was
discarded and its error stack cleared. If the shrink also fails, the dataset keeps unwritten fill rows while the
error claims nothing changed.
Requested outcome: check the rollback and report its failure.
Builder response: fixed. The rollback's result is checked. If it fails, the error says so, with the row counts
and HDF5's stack for the rollback. The header contract now states this case. Fault-injection coverage was
not added: making `H5Dwrite` fail after `H5Dset_extent` succeeded needs an HDF5 shim, which this PR does not
have. New test `Hdf5Dataset.AppendToAReadOnlyFileThrowsAndLeavesTheRowsAsTheyWere` covers the refused-extend
path only, and its comment says so.
Verification: pending
History: initial review, open; fix 1, fixed-unverified
Human exception if needed: none requested; the missing rollback test is disclosed in the PR body

## Finding [F2]

Severity: minor (Codex: medium; only under `std::bad_alloc` while copying strings)
Status: fixed-unverified
Location: `core/src/signals/hdf5_file.cpp` `read_string_attribute`, lines 218 to 228 at `4693cf7`
Requirement/ADR: ADR-0005 (the wrapper owns every HDF5 resource)
Failure: after `H5Aread` of a variable-length string attribute, `values.reserve` or `emplace_back` can throw
before `H5Dvlen_reclaim` runs, leaking every string HDF5 allocated.
Requested outcome: reclaim on every exit path.
Builder response: fixed. A `VariableStrings` guard is armed before `H5Aread`. Its `release()` reports HDF5's
result on the normal path, and its destructor frees the strings best effort on any other exit. Null entries
are safe to free, so a failed read is covered too. No allocation-failure test: the unit suite has no allocator
fault injection (`tests/support/alloc_counter` counts and is linked only by the physics suite).
Verification: pending
History: initial review, open; fix 1, fixed-unverified
Human exception if needed: none requested; the missing test is disclosed in the PR body

## Completion

Open critical or major findings: none. Both are fixed-unverified pending Codex's fix-verification pass. Codex
reported nothing on the byte-identity test, the LSan settlement or the API scope. Validation gap: neither fix
has a fault-injection test (see each finding).
