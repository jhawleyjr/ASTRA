# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines; says nothing that changes at merge)

## In flight
M5 PR 1 (Tier 2, docs only): PR #63, branch `worktree-m5-pr1-adrs` off `origin/main` `2cce400`, plan
`docs/plans/m5-tooling.md`. ADR-0025 (HDF5 run log and replay, Q3 to Q12) and ADR-0026 (derived
flight-condition signals, Q13) were **accepted by Joe 2026-09-27**, ADR-0025 with his J1 amendment (the row fill
uses an allocation-free frame-start state snapshot). Also in the PR: architecture rev 9 (§5, §9, §10, §12 with
the 62.5 Hz bridge divisor, §13, §15, §16 note); AGENTS.md's wall-clock rule scoped to `core/`, `plugins/` and
`aircraft/`; Joe's F1 decision (replay refuses a `kLog`-halted log, named) in ADR-0025, §9 and the plan.
The packet is in PR #63's body. Joe asked for it there, not as a committed `docs/reviews/*-human.md`.

## Last verified result
This session, on the branch: `python3 scripts/docs_check.py` exit 0 and `git diff --check` exit 0, after each
fix and after the status flip. No build or test ran (docs only, per the plan's PR 1 row). Codex adversarial
review: needs-attention with F1, then fix verification: approve with no new findings. Thread ids are in the PR
body. CI `check` passed on the flip commit. This handoff commit changes only STATE.md and docs/lessons.md; its
own `check` rerun result is in the PR body.
Prior-session: M4 acceptance evidence at `4e418e1` (`docs/reviews/evidence/4e418e1-*`).

## Next step
1. PR #63: Joe's decision is recorded in its body. Any later push needs the `gh pr ready --undo` /
   `gh pr ready` rerun of CI's `check` before merge.
2. M5 PR 2 (Tier 2): `find_package(HDF5 COMPONENTS C)` and the RAII wrapper in `core/signals/` (time tracking
   off, version bounds fixed), unit tests with temp files, ASan/LSan behavior with libhdf5 settled, and a
   byte-identity test of two identical writes. If byte identity fails, the ADR-0025 Q7 fallback applies and
   PR 2's evidence and every test using it say so. Acceptance and binding ADRs: the plan's PR 2 row.
3. M5 PR 3 (Tier 3): the derived signals per ADR-0026; `lockstep_exchange.cpp` untouched, and R10's
   base-vs-candidate recorder SHA-256. It can follow PR 1 independently of PR 2.
4. Before PR 8: Joe places FlightGear's `net_fdm.hxx` (version 24) in `docs/refs/` (Q21).

## Tried / failed
Correction and integration counts: PR #63's body holds them. The budget of two correction rounds is spent on
this PR. `gh pr edit --body-file` still fails on the `projectCards` GraphQL error (lessons 2026-09-19); the REST
`gh api -X PATCH` route worked.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
