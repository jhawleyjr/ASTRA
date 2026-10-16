# Review: M7 PR 16, the M7 closeout

Spec/plan: [PR plan](../plans/m7-pr16-closeout.md), [milestone plan](../plans/m7-f16-nesc-oracle.md)
Builder: Claude Opus 5.5
Reviewer: Codex (companion 1.0.6, default model; fresh context; adversarial review with a factual-accuracy focus)
Scope: committed branch diff `origin/main` `c3586c3`..`f413b1c` (clean tree): PLAN.md, STATE.md, README.md,
docs/architecture.md, docs/future-work.md, docs/refs/README.md, docs/plans/m7-f16-nesc-oracle.md,
docs/plans/m7-pr16-closeout.md
Validation: `just docs-check` exit 0 and `git diff --check origin/main..HEAD` exit 0 at `f413b1c`; Tier 0,
no full gate
Round: fix 1

## Finding [R-001]

Severity: major
Status: fixed-unverified
Location: PLAN.md:419-422 at `f413b1c`; docs/plans/m7-pr16-closeout.md, the PR 6b fact and amendment 2
Requirement/ADR: AGENTS.md (report evidence faithfully); the plan's "Verified facts"
Failure: The closeout said PR 6b's aero was first gated in PR 6c's `991ee89` summary. `c8f3128` (#113) is an
ancestor of PR 3c's `beffa64` (`git merge-base --is-ancestor` exit 0; 3c's base was `c8f3128`), and
`git diff --stat c8f3128 beffa64 -- aircraft/f16 tests/python/test_f16_generated.py` is empty. So PR 3c's
passing full gate (CTest 729/730/731/732, pytest 2079) already built and ran 6b's aero and its tests.
Requested outcome: attribute the first gate to PR 3c's `beffa64`, keeping the disclosure that 6b had no gate
or code review of its own.
Builder response: fixed. PLAN.md's clause now says the aero was first gated in PR 3c's summary (linked), then
in PR 6c's. The PR 6b review item says PR 3c's and PR 6c's adversarial reviews both ran on bases that already
held it: 3c's PR body gives its scope as `c8f3128..6ca17d5`. The plan's fact and amendment 2 carry a dated
correction note, since Joe's amendment wording rested on the error. The PR body names it under "Needs Joe's
decision".
Verification: pending.
History: fix 1, open → fixed-unverified.
Human exception if needed: none.

## Completion

One major finding, fixed in round 1 of 2, verification pending. The reviewer could not read PR bodies (`gh`
unavailable in its sandbox), so the PR-body disclosures were not independently checked.
