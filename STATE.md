# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines, says nothing that changes at merge)

## In flight
M6 PR 2 (Tier 3), ADR-0027's implementation: PR #80, draft, branch `worktree-m6-pr2-finalization`, base
`origin/main` `02bddfa` (#79). Plan: `docs/plans/m6-pr2-finalization.md` (approved by Joe in plan mode with J1 to
J12; J12's byte-scan criterion corrected by Joe mid-task). Stage: human review (step 10). Every gate the tier
requires is met: the acceptance commands, the fresh full gate, and the Codex adversarial review with its fix
verification. The PR body holds the checks, R19b's before and after numbers, the four confirmations, the kill
evidence, the review, the M5 behavior changes and the decisions for Joe.

## Last verified result
Run this session:
- `./scripts/check-full.sh` from a fresh configure (`build/` removed first) on `ede0cf3`: exit 0, `check: OK`,
  1003 s (`docs/reviews/evidence/ede0cf3-gate-summary.log`); CTest 649/650/651/652, pytest 510 passed, 5 skipped.
- The acceptance commands on `ede0cf3`'s content: ASan `RunLog|Hdf5|NoAlloc` 89/89; the pytest `-k` filter 237
  passed, 5 skipped; release `NotionalTransportCruiseRegression` 2/2.
- Codex: pass 1 (`01a0f00a-7bc3-7932-a47c-19a987810f50`) needs-attention, R-001 and R-002, both fixed in
  `ede0cf3`; the verification (`01a0f01e-82fd-7ef0-b9a0-ef2caf4b20d9`) approve, new findings none.

Every commit after `ede0cf3` is docs only: the gate summary, then this STATE and four lessons.

## Next step
1. Joe reads the diff and tests. The PR body's "Needs Joe's decision" has two items: approval of the candidate,
   and R-001's reading that HDF5's own close after a `kLog` halt is no ASTRA write.
2. Before merge, rerun CI's `check` with `gh pr ready --undo` then `gh pr ready` (no GitHub CI until Oct 1: local
   gate mode).
3. Follow-up (J8): `docs/architecture.md` §9 and §12 still mark ADR-0027 "(M6 PR 2, *planned*)". The M6 plan's
   PR 10 row names only §12, so §9 needs an owner.
4. Next M6 task: PR 3 (the `astra` CLI) depends on PR 2; PR 4 (units) and PR 9 (benchmark) depend only on PR 1.

## Tried / failed
- J12's literal "no BTHD" failed on dense attribute storage; Joe gave the corrected criterion.
- Confirmation 4's first attempt read `.o.d` files, found none (ninja folds them into `.ninja_deps`), and was
  discarded; `ninja -t deps` gave the real set, in which `errors.hpp` (a comment-only change) reaches plugins.
- The seeded close-window kills flaked 1 in 20 until they got a ring the run never drains.
- The worktree guard refuses compound commands carrying regex or filter arguments; the workaround is scripts in
  the scratchpad (the 2026-09-27 lesson). Correction rounds and failed integrations are in the PR body (1 of 2; 0).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
