## Summary

[Two to four sentences: what the PR does and why, the plan row or task it implements, its risk tier and the
reason. Base and head revisions, and whether the tree was clean.]

## Changes

[Concrete before/after behavior, by component. The diff stat and the main files. Name any unexpected scope, or
say there is none.]

## Checks

| Command | Result | Candidate |
|---|---|---|
| `./scripts/check-full.sh` | [exit code, `check: OK`, summary counts] | [revision; link to `<head>-gate-summary.log`] |
| [acceptance commands from the plan] | [exit code, counts] | [revision] |

[Measured numbers where they matter (thresholds and values, not pass/fail), toolchain, anything not run and
why. CI is in local-gate mode, so a green `check` on GitHub is not a full-gate result.]

## Codex review

[Reviewer and scope, verdict and thread id for each pass, each finding with its fix or rejection, the
fix-verification verdict, correction rounds and failed integrations used. If no review has run, say it is
pending.]

## Where to look

[The consequential files, tests and decisions to read first, the assumptions behind them, the risks, any
untested path, and how to revert.]

## Needs Joe's decision

[Each decision requested, one line apiece: approval of this candidate, any exception (a suppression, a
fallback, a waived check), any open question. Leave Joe's answer out: his PR comment or squash-merge records
it.]

## Follow-ups

[Nonblocking items and where each is tracked; "none" is valid.]
