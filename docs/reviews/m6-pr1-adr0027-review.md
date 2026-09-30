# Review: M6 PR 1, ADR-0027 and architecture rev 10

Spec/plan: [M6 plan](../plans/m6-analysis-tooling.md), [PR 1 plan](../plans/m6-pr1-adr0027.md)
Builder: Claude (Opus 5.5)
Reviewer: Codex (companion 1.0.6, CLI 0.154.0, model `gpt-6-astra`, effort `high` from `~/.codex/config.toml`;
fresh context; adversarial)
Scope:
- Initial pass: the committed branch `be32cbb..16c41a1` (PR #79). It covered ADR-0027, `docs/architecture.md`,
  `PLAN.md` and `docs/plans/m6-pr1-adr0027.md`; docs only. Thread `01a0ef16-fba6-7142-81d3-66368b0cbf6b`, exit
  0, verdict needs-attention. The reviewer could not reach GitHub, so it used the M6 plan's critique tables for
  the obligations.
- Verification pass 1: the fix delta `16c41a1..e24dd1e` within `origin/main..e24dd1e`. Thread
  `01a0ef7c-0e14-7252-8bf5-a2e1813226b0`, exit 0, verdict needs-attention: F1 verified, F2 incomplete in
  architecture §9, and one new finding (N1).

Validation: `python3 scripts/docs_check.py` exit 0 and `git diff --check` clean on each candidate. The full gate
runs on the final candidate after review (PR plan).
Round: fix 2, correction round 2 of 2 and the last autonomous round. Anything more goes to Joe.

## Finding [F1]

Severity: major (Codex: high)
Status: verified (verification pass 1). Its mechanism was later amended by D7 (see N1).
Location: `docs/decisions/0027-run-log-finalization.md:75-87` at `16c41a1` (item 3)
Requirement/ADR: ADR-0027 item 3 ("a file at `log.path` is one that `close_log()` completed and published");
M6 plan Q19
Failure: `RENAME_NOREPLACE` protects only the destination. Suppose run A's `.partial` is moved aside and run B
then creates the same name. A closes its own HDF5 file, then renames B's still-open file to `log.path`. The
exclusive create and the absolute path do not prevent this.
Requested outcome: ownership of the source stays stable through publication, and publication is refused when
it is lost, or the guarantee is narrowed explicitly. PR 2's acceptance includes the two-run replacement.
Builder response: confirmed against item 3 and the semantics of `renameat2(2)`. This is a design change, so it
went to Joe, who decided D6 (the PR plan). The fix in `e24dd1e`:
- item 1: a kept descriptor, and the identity check (`fstatat(..., AT_SYMLINK_NOFOLLOW)` against the
  descriptor's (device, inode));
- item 3: the check before the rename; the refusal "the .partial is no longer this run's file", which renames
  nothing and touches no file; and the stated residual (the gap between check and rename);
- item 8: D1's delete uses the check;
- rejected alternatives: two added;
- validation: three replacement cases added;
- architecture §9: updated to match.

Verification: Codex verification pass 1, at `e24dd1e`: verified against D6.
History: round 1: open, then fixed-unverified. Verification 1: verified.
Human exception if needed: Joe's D6, 2026-09-29.

## Finding [F2]

Severity: minor (Codex: medium)
Status: fixed-unverified (round 2)
Location: `docs/decisions/0027-run-log-finalization.md:104-110` at `16c41a1` (item 5), and
`docs/architecture.md:352` at `e24dd1e`
Requirement/ADR: M6 plan R16 ("every outcome is no `log.path`", with kills "including close")
Failure: a kill after the rename succeeds but before `close_log()` returns leaves a complete file at
`log.path` and no `.partial`. The kill table omits this outcome, and R16's wording cannot hold alongside item
3's sequence.
Requested outcome: make the successful rename the publication boundary, state the outcome on each side of it,
and reconcile R16.
Builder response:
- Round 1 (`e24dd1e`): ADR-0027 item 3 names the *publication point*. Item 5's table splits close into before
  and after that point, a paragraph reads R16 as covering kills before it, and the PR plan records the
  reading. This is a clarification, and the PR body flags the R16 reading for Joe.
- Round 2: verification pass 1 found architecture §9 still saying a killed run leaves "nothing or a `.partial`"
  and putting every kill during close under the stated limit. §9 now says what a kill before the publication
  point leaves, applies the stated limit only to kills before it, and says a kill after it leaves the complete
  published log and no `.partial`.

Verification: verification pass 1: fixed in the ADR, incomplete in architecture §9. Round 2's fix is pending
verification.
History: round 1: open, then fixed-unverified. Verification 1: partially fixed. Round 2: fixed-unverified.
Human exception if needed: none. The R16 reading is listed for Joe in the PR body.

## Finding [N1]

Severity: minor (Codex: medium)
Status: fixed-unverified (round 2)
Location: `docs/decisions/0027-run-log-finalization.md:104-105` at `e24dd1e` (item 3: "every refusal also closes
the kept descriptor")
Requirement/ADR: ADR-0027 item 8 and M6 plan R18b (a tool leaves no `.partial` of its own after a handled
error, including a publication collision)
Failure: `close_log()` closes the kept descriptor at every refusal. A tool that handles `EEXIST` while its
`Simulation` is alive then has no descriptor for the identity check, so the checked delete that item 8 requires
cannot run.
Requested outcome: an ownership handoff compatible with D6 that keeps the file's identity available for the
tool's cleanup after a refusal, with explicit release rules, and an R18b `EEXIST` case that checks the cleanup.
Builder response: confirmed, as a conflict between D6(a) and D6(e). It went to Joe, who decided D7 (the PR
plan). The fix:
- ADR-0027 item 1: the descriptor is a separate open file description (a `/proc/self/fd` reopen, never a
  `dup()`), held from the create until the `Simulation` is destroyed, and closed only by the destructor. The
  identity check names where the log lives: `.partial` before the publication point, `log.path` after.
- item 3: the descriptor stays open after every outcome.
- item 4: the destructor closes it and deletes nothing.
- item 7: replay deletes a mismatched `out` through `discard_log()`.
- item 8: the core deletes in exactly two cases, each checked: a failing init, and
  `Simulation::discard_log()`. The latter is bound, works in every state, is idempotent, and reports a
  mismatch without deleting. Tools delete only through `discard_log()`.
- rejected alternatives: three added.
- consequences: the added costs, and why `kCoreApiVersion` stays the same.
- validation: R18b's collision case through `discard_log()`, a published-then-discarded case, a mismatch case,
  and `load(log.path)` while the `Simulation` is alive.
- architecture §9: updated.

Verification: pending (Codex verification pass 2).
History: verification 1: new, open. Round 2: fixed-unverified.
Human exception if needed: Joe's D7, 2026-09-29.

## Completion

Initial pass: 2 findings, 1 high and 1 medium. Verification pass 1: F1 verified, F2 partially fixed, and 1 new
medium finding (N1). No finding was rejected. Round 2's fixes (F2 in architecture §9, N1) are pending
verification. This is the last autonomous correction round; any further finding goes to Joe.
