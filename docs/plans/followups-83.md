# Follow-ups from #83 (Tier 1 mini PR)

Risk: Tier 1 (`.gitignore` and docs; no interface or numerical effect).
Status: **approved by Joe, 2026-09-30, in plan mode**; item 3 held for his main-checkout check.

## Context
Joe set this scope on 2026-09-30 as four follow-ups from #83. It is Tier 1: the `.gitignore` line changes what
session-start reports, with no interface or numerical effect. Branch `worktree-followups-83` sits on
`origin/main` `d5f6e73`, which is current. Draft PR, commit and push as I go, `/wrap` before merge.

## Changes (nothing else)
1. **`.gitignore`**: add `.claude/settings.local.json` under `# Claude Code session-local state`, after
   `.claude/touch-goldens`.
2. **`docs/process/workflow.md` §Model and effort** (line 314): `.claude/settings.json` on main says
   `"model": "opus"`, `"effortLevel": "high"`, `env.CLAUDE_CODE_SUBAGENT_MODEL: "opus"`. New first sentence:
   "`.claude/settings.json` pins `model` `opus` at `effortLevel` `high`, with `CLAUDE_CODE_SUBAGENT_MODEL` `opus`
   for subagents; who changes it, and the no-recon-subagents rule, are in AGENTS.md." The rest stays as is.
3. **`docs/agents/claude-code.md`**: **on hold.** The bracket in the prompt was an unfilled placeholder, and the
   main-checkout check has not run, so nothing has been observed. This file stays unchanged until Joe gives the
   real path. If he gives one in the main checkout's `docs/plans/`, I replace "(inferred from that check, not
   observed)" with his observation, written the way line 25 writes its own. If he gives any other path, I change
   nothing and report it. If he drops the item, the file stays unchanged.
4. **`PLAN.md`** plan links, matched by each provenance line's "merged as #NN":

   | PLAN.md line | Plan |
   |---|---|
   | M0 PR 1 (#1) | `m0-kickoff.md` ("the first PR merged as #1") |
   | M0 PR 2 (#2) / 3 (#6) / 4 (#9) | `m0-pr2-build-core-skeleton.md` / `m0-pr3-python-package.md` / `m0-pr4-stop-hook-ci.md` |
   | M1 PR 1 (#15) / 2 (#16) / 3 (#19) / 4 (#21) | `m1-pr1-frames-integrator.md` / `m1-pr2-config-scheduler.md` / `m1-pr3-earth-gravity-mass.md` / `m1-pr4-health-monitor.md` |
   | M2 PR 1 (#22) / 1b (#24) / 2 (#25) / 2b (#26) / 2c (#27) | `m2-pr1-earth-gravity.md` / `m2-pr1b-atmosphere.md` / `m2-pr2-wind.md` / `m2-pr2b-random-units-dryden.md` / `m2-pr2c-domain-validity.md` |
   | M2 PR 3a (#29) | `m2-pr3-nesc-split.md` ("3a merged as #29") |
   | M5 PR 11 (#74) | `follow-up-b-citations.md` |
   | M4 PR 1 (#51), second link | `m4-pr1-adrs.md` |
   | M5 PR 1 (#63) / 5 (#67) / 6 (#68) / 7 (#69) / 8 (#70) / 9 (#71), second link | `m5-pr1-adrs.md` / `m5-pr5-log-load-replay.md` / `m5-pr6-scenario.md` / `m5-pr7-montecarlo.md` / `m5-pr8-fgbridge.md` / `m5-pr9-closeout.md` |
   | Planning (#78), second link | `m6-plan-critique.md` (its provenance names #78; it has no "merged as") |

   Format: `Plan: none` becomes `Plan: [plan](docs/plans/<file>)`. On M5 PR 11, `Plan: none (Tier 0)` loses
   "(Tier 0)", because that parenthetical explained why there was no plan. For the second links I follow the PR 3d
   precedent: `Plans: [milestone](docs/plans/m4-lockstep.md), [PR](docs/plans/m4-pr1-adrs.md)`, the same for the M5
   lines, and `[critique](docs/plans/m6-plan-critique.md)` on #78. Not linked: #30 (`workflow-v3-review.md`), #49
   (`m3-pr10-closeout.md`), #50 (`m4-planning-session.md`) and #60 (`workflow-calibration.md`).
   Then add this PR's line under M6, after Workflow (#83): `Follow-ups (#NN): ... Plan:
   [plan](docs/plans/followups-83.md). Packet: in the PR body.` `#NN` comes from the draft PR, which opens at the
   first commit.

5. **`docs/agents/claude-code.md`** (Joe added this mid-session, 2026-09-30): one line saying a
   worktree-guard refusal is never a question for Joe. Rerun the command as plain separate commands or use
   Write/Edit, and note it in the PR body.

## Sequence
Commit 1: this plan, renamed with `mv` to `docs/plans/followups-83.md` before its first add, plus items 1 and 2.
Push and open the draft PR. Commit 2: the PLAN.md links plus this PR's line. Item 3 waits for Joe; if it arrives
before the gate, it goes in commit 3. Run the
checks and commit the trimmed gate summary. Run one Codex pass with `/review-cycle` on the final diff
(`--base d5f6e73`). Write the PR body per `docs/templates/pr-description.md` with the mapping table above, then
`/wrap`.

## Verification
- `git check-ignore -v .claude/settings.local.json` names the new `.gitignore` line.
- Every new PLAN.md link target exists: a loop runs `test -f` on each `docs/plans/*.md` path in the diff.
- `python3 scripts/docs_check.py` exits 0. `git diff --check` is clean.
- `rm -rf build && ./scripts/check-full.sh` runs with Bash `run_in_background`. It passes on exit 0 plus
  `check: OK`. The trimmed log is committed as `docs/reviews/evidence/<head>-gate-summary.log`.
- If a write or delete is refused, I do not retry it by another route. I ask Joe.

## This plan
On 2026-09-30 Joe ruled that this PR's own plan follows the `docs/agents/claude-code.md` rule. The `docs/plans/`
exclusion covers only the existing recovered plans. Once Joe approves this plan, I rename it with `mv` (before
its first add) to `docs/plans/followups-83.md`, commit it, and link it from this PR's PLAN.md line.
