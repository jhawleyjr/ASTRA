# M0 PR 4 — Stop hook, skills check, `check` required on `main`

Recovered from `~/.claude/plans/m0-pr-4-the-snoopy-seal.md` on 2026-09-30. Approval: the file records none; the work merged as #9. Never committed: it predates `docs/plans/`, whose first plan was committed on 2026-09-19 (#31). Spelling normalized to American English.

## Context

Last PR of M0. `docs/process/workflow.md` §Verification already specifies the Stop hook ("runs the fast
subset (build + unit tests) and blocks the turn from ending until it passes; Claude Code caps this at 8
consecutive blocks") and CI's `check` job exists since PR 2, but nothing enforces either yet: a turn can end
on a red tree, and a red PR can be merged. This PR wires both, confirms the two skills, and closes M0.

Verified against the Claude Code hooks reference this session: the Stop event's stdin JSON carries `cwd`,
`permission_mode`, `stop_hook_active`, `last_assistant_message`, `turn_number`; exit 2 + stderr blocks the
stop and the text goes to Claude; Claude Code stops honoring a Stop hook after eight consecutive blocks
without progress (so no home-made loop counter); a hook that exceeds `timeout` is canceled, which is a
silent pass, not a block; `timeout` is in seconds (default 600); `statusMessage` is supported; hook edits in
settings files are picked up mid-session by the file watcher; hooks run in the session's current directory.
The Codex plugin's own Stop hook exits early while its review gate is off (it stays off per CLAUDE.md).

Live GitHub: ruleset `protect-main` id `23269666`; the CI check run is named `check` from app id `15368`
(github-actions). REST field names for the rule confirmed from the GitHub docs.

## Changes

### 1. Stop hook — `.claude/hooks/stop-check.sh` (new, executable) + `.claude/settings.json`

Same shape as `no-main.sh` / `guard-oracle.sh`: bash, `set -u`, `python3` for JSON, exit 2 = block, one
`BLOCKED by stop-check:` header on stderr.

- Read stdin. If `permission_mode` is `plan`, exit 0 (plan mode cannot have changed the tree). Absent or
  any other mode runs the check.
- `cd` to the JSON `cwd` (fallback `$CLAUDE_PROJECT_DIR`, then `.`); `just` finds the justfile upward from
  there, which is right in both launch modes (`CLAUDE_PROJECT_DIR` stays at the main checkout after a
  mid-session EnterWorktree; `cwd` follows the worktree).
- **Busy-tree guard:** if any `just|cmake|ninja|ctest` process of this user has `/proc/<pid>/cwd` under
  the tree (excluding `<tree>/.claude/worktrees/*`), block: "a build or test is already running under
  <tree> (pid N, name); wait for it, read its result, then end the turn". Two ninjas in one `build/asan`
  (a backgrounded `just check` still running when the turn ends, which this repo does) would corrupt or
  spuriously fail both.
- Run `timeout -k 10 840 just test` (default preset `asan`: configure + build + ctest, the documented "fast
  subset"; GNU timeout signals the whole process group so ninja/ctest die too) with all output to a
  `mktemp` log removed by an EXIT trap. Exit 0 on success, print nothing.
- Failure: header (exit status, or "exceeded 840 s: hung test? concurrent build?" on 124, plus "rerun
  `just test` for the full output"), then `grep -n -m 20 -E '^FAILED:|error:|The following tests FAILED'`
  of the log so a compiler error that scrolled past the tail is still shown, then `tail -n 40`; exit 2.
- No early exit on `stop_hook_active`: the check itself is the measure of progress; the eight-block cap
  bounds it. No skip override: a red tree that only a golden change could fix ends after eight warm
  blocks, and Joe can always interrupt (interrupts do not fire Stop).

`settings.json` (edit with the Edit tool, then `git diff` it; `attribution.sessionUrl: false` must survive —
lessons.md):

```json
"Stop": [{ "hooks": [{ "type": "command",
  "command": "\"$CLAUDE_PROJECT_DIR\"/.claude/hooks/stop-check.sh",
  "timeout": 900, "statusMessage": "stop-check: just test" }] }]
```

Sequencing: run `just check` (a warm `build/asan`) **before** editing `settings.json`, so the first live
Stop is not a cold ASan build. From then on every turn end costs a warm `just test` (a few seconds).

### 2. Hook test cases — `.claude/hooks/tests/run.sh` + `stop-check.cases` (new)

`run.sh` grows two placeholders, substituted before `$ROOT` (longest name first: `$ROOT_ON_MAIN`,
`$ROOT_GREEN`, `$ROOT_RED`, then `$ROOT`) and listed in its header comment: `$ROOT_GREEN`, a temp dir with a
stub justfile whose `test` recipe exits 0, and `$ROOT_RED`, one whose `test` prints ctest-style "The
following tests FAILED:" lines and exits 1 (probed this session: `just` exits 1 with the recipe output).
`just` 1.21 exports no `JUST*` variables into recipes, so the nested `just` inside `just check` →
`hooks-test` cannot see the real justfile. Every case names a `cwd`; without one the hook would run the
real `just test` inside the gate. Cases (expected exit, then JSON):

- `0` plan mode, cwd `$ROOT_RED` — plan mode is never checked
- `0` cwd `$ROOT_GREEN`: `default`/active false; `acceptEdits`/active true; `permission_mode` absent
- `2` cwd `$ROOT_RED`: `default`/active false; `acceptEdits`/active true (no early exit); mode absent
  (only plan mode skips)

The "no justfile above `$ROOT`" case from the first draft is dropped: it depends on `TMPDIR` and tests a
state the hook cannot reach. The busy-tree and timeout paths are verified by hand (Verification 3), not by
cases.

### 3. Ruleset — `.github/rulesets/main.json`, then `gh api -X PUT`

Append to `rules` (PUT replaces the whole `rules` array, so the file carries all four rules;
`bypass_actors` untouched):

```json
{ "type": "required_status_checks",
  "parameters": { "strict_required_status_checks_policy": true, "do_not_enforce_on_create": false,
                  "required_status_checks": [ { "context": "check", "integration_id": 15368 } ] } }
```

Apply: `gh api -X PUT repos/jhawleyjr/ASTRA/rulesets/23269666 --input .github/rulesets/main.json`.
Verify: `gh api repos/jhawleyjr/ASTRA/rules/branches/main` lists `required_status_checks` with `check`, and
`gh api repos/jhawleyjr/ASTRA/rulesets/23269666` shows it. Applied before the PR opens, so the PR page shows
`check` as required.

**Assumption to confirm at review: `strict_required_status_checks_policy: true`** (branch must be up to date
with `main` before merge). It is what makes "main is green by construction" true when two PRs overlap, and
matches workflow.md's "rebase onto main one at a time; the gate catches semantic ones". Cost: after another
merge, an open PR needs "Update branch" (or a rebase) and a ~3.5 min CI rerun before Joe can merge. Say
"non-strict" and I set it false.

### 4. Skills — `.claude/skills/retro/SKILL.md` only

`/retro` step 1 reads "the three skills"; only `/wrap` and `/retro` exist since PR #5 dropped `/task`.
Change to "the skills in `.claude/skills/`". `/wrap` is followed step by step at the end of this session
(it is user-invoked only); it changes only if a step turns out wrong when followed. Checked already: STATE.md
has exactly the five sections `/wrap` names, PLAN.md uses the `- [ ]` form the session hook greps, the
opener form matches README. `/retro` is Joe's to run after M0 merges; every input it names exists
(`.claude/rules/` is "if present").

### 5. Docs (small, tense and pointers only)

- `README.md` "Protecting main": the ruleset paragraph says `check` will be added in M0; make it "is
  required" with the PUT command and how to find the id (`gh api repos/jhawleyjr/ASTRA/rulesets`), and add
  the Stop hook to the "Local hook" bullet.
- `scripts/setup-ubuntu.sh` closing message: the POST line becomes "already applied; update with
  `gh api -X PUT repos/jhawleyjr/ASTRA/rulesets/<id> --input .github/rulesets/main.json`" (a second POST
  duplicates or 422s).
- `CLAUDE.md` "Git and session rules", one bullet: "A Stop hook (`.claude/hooks/stop-check.sh`) runs `just
  test` when a turn ends and blocks until it passes (skipped in plan mode; blocks while a build is already
  running in the tree); fix the failure, never route around it." File is 96 lines, cap 100.
- `.github/workflows/check.yml` header and `tests/unit/CMakeLists.txt` line 1: "PR 4 adds / from PR 4" →
  present tense.

Follow-up for M1, recorded in STATE.md "Next step", not done here: architecture §5 says the Stop hook runs
`tests/unit/`; when physics and golden tests join ctest, label the unit suite and have the hook run
`-L unit` rather than letting it grow with the gate.

### 6. Review, PR, wrap

1. `just check` green (warms `build/asan`), then add the Stop hook to settings, rerun `just hooks-test`,
   commit on `worktree-m0-hooks`.
2. `/codex:setup` via the Skill tool (no gate flags; readiness only). Then the review: `/codex:review` is
   user-invoked only (`disable-model-invocation`), so run exactly what it runs, from the worktree:
   `node ~/.claude/plugins/cache/openai-codex/codex/1.0.6/scripts/codex-companion.mjs review "--base origin/main --wait"`
   (with `--base` it reviews the committed branch diff, so it runs after the commit). Findings are claims
   to verify against the code; fix what holds, rerun `just check` if code changed, commit.
3. Apply the ruleset (step 3), push, open the PR with the acceptance commands and their results in the body.
4. `/wrap` steps by hand: rewrite `STATE.md`, tick PR 4 and M0 in `PLAN.md` (only if acceptance passed),
   `docs/lessons.md` only if something bit, print the M1 opener (`--effort xhigh`).

## Verification

1. `just check` exits 0 (all presets, tidy, format, hooks-test including the new stop-check cases, ruff,
   pyright, pytest).
2. Deliberately broken unit test blocks: change `EXPECT_EQ(astra::state::kVelocity, 3)` to `4` in
   `tests/unit/kernel/state_test.cpp`, then
   `printf '{"hook_event_name":"Stop","permission_mode":"default","cwd":"%s","stop_hook_active":false}' "$PWD" | .claude/hooks/stop-check.sh; echo exit=$?`
   → `exit=2` with `StateVector.LayoutPartitionsThirteenStatesAsAdr0003Says` in stderr; revert →
   `exit=0`. Then, once, end a turn with the break in place: the real Stop event must block and hand the
   failure back; revert. The message before that turn ends says the break is deliberate, so if the hook did
   not fire the state is explicit and the fix is a one-line revert.
3. Busy-tree guard: start `just build` in the background, pipe the same Stop JSON → `exit=2` naming the
   pid; after it finishes → `exit=0`. Timeout path: `timeout -k 10 1 just test` from the shell exits 124,
   which is the status the hook maps to a block (the hook's own budget is not exercised in-session).
4. `gh api repos/jhawleyjr/ASTRA/rules/branches/main` shows `required_status_checks` with `check`; the PR
   page shows `check` as required.
5. CI `check` passes on the PR.
