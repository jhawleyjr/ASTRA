# STATE (rewritten each session, never appended; ≤ 40 lines)

## In flight
M0 PR 1 on branch `m0` (PR #1): bootstrap files, ADR-0001…0011, PLAN/STATE, oracle-guard hook. Review fixes
pushed (guard Bash matcher, attribution, SessionStart hook). Awaiting Joe's squash-merge.

## Last verified result
- Eleven ADRs exist with Context / Decision / Rejected alternatives / Consequences; every §17 alternative
  named (grep-checked).
- `.claude/hooks/guard-oracle.sh`: Edit/Write on decision records, goldens, and `*tolerance*` files exit 2,
  other paths exit 0; Bash blocks only when a guarded path is a write target (redirect, tee, sed -i,
  rm/mv/cp/touch/truncate, git rm/mv/checkout/restore) and passes reads and mere mentions; override honoured.
  15-case probe (plus override) all pass; the probe fires live too, so run it from a script by path.
- `.claude/settings.json`: `attribution.sessionUrl` is `false`; SessionStart removes `.claude/touch-goldens`.
- No build exists yet; `just check` cannot run until PR 2.

## Next step
M0 PR 2 in a fresh session: `claude --worktree m0-build --effort xhigh`. Precondition on this machine:
`./scripts/setup-ubuntu.sh` (installs clang-18, clang-tidy/format-18, just, and the Python venv; clangd-18, cmake,
ninja, gcc, Eigen, HDF5, GoogleTest, yaml-cpp already present). Scope and acceptance: `PLAN.md`, PR 1 description.

## Tried / failed
- Nothing yet.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
