# Review: M6 PR 1, ADR-0027 and architecture rev 10

Spec/plan: [M6 plan](../plans/m6-analysis-tooling.md), [PR 1 plan](../plans/m6-pr1-adr0027.md)
Builder: Claude (Opus 5.5)
Reviewer: Codex (companion 1.0.6, CLI 0.154.0, model `gpt-6-astra`, effort `high` from `~/.codex/config.toml`;
fresh context; adversarial)
Scope: the committed branch `be32cbb..16c41a1` (PR #79): ADR-0027, `docs/architecture.md`, `PLAN.md`,
`docs/plans/m6-pr1-adr0027.md`. Docs only. The reviewer could not reach GitHub, so it used the M6 plan's
critique tables for the obligations.
Validation: `python3 scripts/docs_check.py` exit 0 and `git diff --check` clean on the candidate. The full gate
runs on the final candidate after review (PR plan).
Round: fix 1 (correction round 1 of 2).

## Finding [F1]

Severity: major (Codex: high)
Status: fixed-unverified
Location: `docs/decisions/0027-run-log-finalization.md:75-87` at `16c41a1` (item 3)
Requirement/ADR: ADR-0027 item 3 ("a file at `log.path` is one that `close_log()` completed and published");
M6 plan Q19
Failure: `RENAME_NOREPLACE` protects only the destination. Suppose run A's `.partial` is moved aside and run B
then creates the same name. A closes its own HDF5 file and then renames B's still-open file to `log.path`.
Neither the exclusive create nor the absolute path prevents this.
Requested outcome: ownership of the source must stay stable through publication, and publication must be
refused when it is lost, or the guarantee narrowed explicitly. PR 2 acceptance must include the two-run
replacement.
Builder response: confirmed by reading item 3 and `renameat2(2)` semantics. It is a design change, so it went
to Joe, who decided D6 (the PR plan). Fix in ADR-0027:
- item 1: a descriptor kept from HDF5's own after the exclusive create (a `dup()` or a `/proc/self/fd`
  reopen, never a second open by name), held and closed on every path, and the identity check
  (`fstatat(..., AT_SYMLINK_NOFOLLOW)` against the descriptor's (device, inode));
- item 3: steps 6 to 8 (check, rename, close the descriptor), the refusal "the .partial is no longer this
  run's file" with nothing renamed or touched, and the stated residual (the gap between the check and the
  rename);
- item 4: the destructor closes the descriptor;
- item 8: D1's delete uses the check, and tools delete a `.partial` only through a checked core call while
  their `Simulation` is alive;
- two rejected alternatives replace the old "device and inode check" rejection;
- the validation list adds the three replacement cases.

Architecture §9 is updated to match.
Verification: pending (Codex fix verification).
History: round 1, open, then fixed-unverified.
Human exception if needed: Joe's D6, 2026-09-29.

## Finding [F2]

Severity: minor (Codex: medium)
Status: fixed-unverified
Location: `docs/decisions/0027-run-log-finalization.md:104-110` at `16c41a1` (item 5)
Requirement/ADR: M6 plan R16 ("every outcome is no `log.path`", kills "including close")
Failure: a kill after the rename succeeds but before `close_log()` returns leaves a complete file at
`log.path` and no `.partial`. The kill table omits this outcome, and R16's wording cannot hold together with
item 3's sequence.
Requested outcome: make the successful rename the publication boundary, state the outcomes on each side of
it, and reconcile R16.
Builder response: confirmed. Fix: item 3 names the successful rename the *publication point*. Item 5's table
splits close into "before the publication point" (the stated limit, no `log.path`) and "after the publication
point, before `close_log()` returns" (the complete published log, no `.partial`). A paragraph reads R16 as
covering kills before the publication point, and the validation list says PR 2's kill tests accept the
published outcome. The PR plan records this reading. This is a clarification, not a design change; the PR body
flags the R16 reading for Joe.
Verification: pending (Codex fix verification).
History: round 1, open, then fixed-unverified.
Human exception if needed: none. The R16 reading is listed for Joe in the PR body.

## Completion

The initial pass returned `needs-attention`, thread `01a0ef16-fba6-7142-81d3-66368b0cbf6b`, with 2 findings:
1 high and 1 medium. No findings were rejected. Fix verification is pending.
