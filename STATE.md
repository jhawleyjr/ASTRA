# STATE (rewritten each session, never appended; ≤ 40 lines)

## In flight
M0 PR 4 on branch `worktree-m0-hooks`, PR #9 open, rebased onto `main` after #8: Stop hook
(`.claude/hooks/stop-check.sh`, wired in `.claude/settings.json`), 7 hook cases in `just hooks-test`, `check`
required on `main` (ruleset applied), `/retro` wording fix, docs in the present tense. Codex review ran
(`--base origin/main --wait`); its one finding (the guard compared a logical path with `/proc` physical paths)
is fixed. Awaiting Joe: squash-merge (CI `check` must pass; strict, so the branch must be current), then `/retro`.

## Last verified result
- `just check` exited 0 (`check: OK`) on the tree rebased onto `main` after #8: 4 presets × 16 ctest cases,
  clang-tidy, clang-format, hooks 32/32, ruff, pyright strict, pytest 26 passed.
- Stop hook: with `EXPECT_EQ(kVelocity, 3)` changed to 4, the hook exited 2 naming
  `StateVector.LayoutPartitionsThirteenStatesAsAdr0003Says`, both with a Stop JSON piped in directly and as
  the live Stop event when a turn ended (the block text came back as the next prompt); reverted, exit 0.
- Busy-tree guard: with a `just` process held in the worktree, exit 2 naming its pid; quiet tree, exit 0.
- Ruleset: `gh api repos/jhawleyjr/ASTRA/rules/branches/main` lists `required_status_checks`, context
  `check`, integration 15368, strict. PR #9 shows it as required.
- `/codex:setup`: Codex ready, logged in, review gate off. LSPs: clangd resolved `kVelocity` to
  `core/include/astra/kernel/state.hpp`; pyright found `__version__`.
- Not exercised: the hook's 840 s budget branch (exit 124) and the eight-block cap. CI on PR #9 pending at wrap.

## Next step
M0 closes when PR #9 merges; Joe runs `/retro`. Then M1 in a fresh session:
`claude --worktree m1-rigid-body --permission-mode plan` (model and effort per CLAUDE.md: `opusplan` at
`high`; Joe escalates) with the M1 line from PLAN.md and architecture §16 row M1. Follow-up for M1: when
physics tests join ctest, label the unit suite and have the Stop hook run `ctest -L unit` (architecture §5
says the Stop hook runs `tests/unit/`), so the fast check stays fast.

## Tried / failed
- Busy-tree probe with a build started in the background in the same response returned 0 once: the build had
  not spawned when the hook scanned. Probe with a held `just --working-directory <tree> --justfile <stub> slow`.
- The worktree session's Bash guard refused heredoc file writes, chained `git`, and `printf | hook`; used the
  Write and Edit tools and a JSON file redirected into the hook (docs/lessons.md).
- A CLAUDE.md line about the Stop hook: dropped, PR #8 took the file to its 100-line cap.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
