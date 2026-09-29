# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines, says nothing that changes at merge)

## In flight
Follow-up B (Tier 0, comment-only): PR #74 (draft), branch `worktree-followup-b-citations` off `origin/main`
`ffb395a`, no plan document (Tier 0). Corrects the Stevens & Lewis equation citations in
`core/include/astra/util/frames.hpp` and the comments of `tests/golden/frames/adr0001_cases.hpp` (Joe armed the
golden guard for the session), plus the half-angle citation in `python/astra/trim.py`, `python/astra/linearize.py`,
`tests/python/test_trim.py` and `tests/python/test_linearize.py` (scope widened by Joe in the session).
Stage: implementation, full gate and the single Codex pass are complete for the candidate; human review is next.

## Last verified result
Run this session on `ed0d552`: `./scripts/check-full.sh` exit 0, `check: OK`
(`docs/reviews/evidence/ed0d552-gate-summary.log`). Later commits add only that log, PLAN.md and this file.
Also run this session: the comment-only equivalence check (C++ with comments stripped, Python AST minus
docstrings; EQUAL on all six source files, both negative controls detected), `python3 scripts/docs_check.py`
and `git diff --check origin/main` (re-run after this file's commit). The Codex verdict and thread id are in the
PR body.

## Next step
1. Joe: decide on PR #74. Before merge, rerun CI's `check` with `gh pr ready --undo`, then `gh pr ready`.
2. Then: M6 planning is Joe's to launch. Open items with owners are in PLAN.md (for example the wall-clock scan's
   timed I/O waits).

## Tried / failed
A wrong first attempt at the comment-only check used `clang++-18 -fpreprocessed`, which clang does not have;
`g++ -fpreprocessed -E -P` strips comments and was used instead. No correction rounds and no failed
integrations; the counts are in the PR body.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
