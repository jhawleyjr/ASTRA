# M6 plan critique: dispatch one fresh read-only Codex critique, record it on PR #78

Recovered from `~/.claude/plans/pasted-content-id-c200-codex-plan-mossy-milner.md` on 2026-09-30. Approval: the file records none; it is a read-only critique of the M6 plan (#78). Never committed: the file says the critique makes no commits.

## Context
Joe asked for one independent Codex critique of `docs/plans/m6-analysis-tooling.md` (M6 plan Q16) before he
approves it. The critique is read-only. It makes no commits and no edits to the plan, because Joe's Cowork
session folds the findings in. The only write is the PR body.

What I found (read-only, this session):
- PR #78 is "M6 planning: analysis tooling spec and plan (draft)", head `0d70568` (`0d705683fc1f…`), base `main`.
  The branch diff over `55a0bc3` is that one file, 280 lines. **Note:** GitHub reports `isDraft: false`, but
  AGENTS.md says PRs start as drafts. I'll report this and leave it unchanged.
- The main checkout is already on `m6-planning` at `0d70568` and clean. Git won't check out one branch in two
  worktrees, so the critique worktree will be **detached** at the PR head.
- Companion: `codex@openai-codex` 1.0.6 at
  `/home/joe/.claude/plugins/cache/openai-codex/codex/1.0.6/scripts/codex-companion.mjs`, from the plugin registry.
  The Codex CLI is 0.154.0. `task` accepts `--fresh --effort high --cwd <dir>` and a positional prompt. Without
  `--write` its sandbox is `read-only`. Progress goes to stderr and the final message to stdout. `status` exposes
  the job id and the thread id ("Codex session ID").
- There is a `gh pr edit` GraphQL failure (lesson 2026-09-29), so I'll update the body over REST.

## Steps
1. `git fetch origin`. Confirm that `origin/m6-planning` equals PR #78's `headRefOid` and that `origin/main` is
   `55a0bc3`. Stop and report if either differs.
2. `git worktree add --detach .claude/worktrees/m6-critique <head>`. That path is gitignored. Confirm it's clean.
3. Put the focus in a variable with a **quoted heredoc** (`<<'FOCUS'`). The focus names paths and SHAs and embeds
   no documents. It asks for:
   - a read-only plan critique of `docs/plans/m6-analysis-tooling.md`, base `55a0bc3`, head `<head>`;
   - reading of AGENTS.md, `docs/architecture.md` §9, §12, §15, §16, §18 and ADR-0002, 0005, 0010, 0011, 0025,
     0026, plus the code behind the Repository findings: `python/astra/log*`, `montecarlo`, `scenario`, the
     `core/` logger and HDF5 writer, M5's R4/R9 tests;
   - the six focus items, verbatim in substance:
     1. ADR-0027: does a flush at each drain break M5's R4 or R9, given that replay drains differently from
        `run()`? How does `.partial` plus rename interact with ADR-0025's rule that an existing or unwritable
        path is a ConfigError, with replay's out path, with montecarlo's partial-log cleanup, with scenario's
        log and with the destructor path? What does a SIGKILLed run leave?
     2. Can each R1 to R20 (and R3b) be failed by a plausible bug? The emphasis is R6 to R9 (Benjamini-Hochberg
        and the null allowance), R11's oracle values, and the byte-determinism of the R3 SVG and R13 HTML.
     3. Contradictions with §9, §12, §15, §18 or ADR-0002, 0010, 0011, 0025, and missing requirements.
     4. Q12's exit codes against §15's `--strict` rule.
     5. PR order, dependencies, tiers and gate time.
     6. Scope against PLAN.md's M6 line: what's missing and what should be held out.
   - Output: failed assumptions and evidence-backed findings only. Each finding gives a severity
     (critical/high/medium/low), `file:line` evidence and the requested outcome. The critique ends with an
     overall verdict. It doesn't edit files and doesn't run builds, tests or the gate.
4. Dispatch once, with the Bash tool's own `run_in_background` (no `nohup`/`&`), and no pipe before the status
   capture:
   `node "$CODEX_COMPANION" task --fresh --effort high --cwd <worktree> "$focus" >"$SCRATCH/m6-critique.log" 2>&1; status=$?; printf 'exit=%s\n' "$status" >>"$SCRATCH/m6-critique.log"`
   No `--write` and no `--model`, so the companion default applies. I'll record the model the log reports.
   Then I'll wait for the tool's completion notice.
5. Read the log tail and `exit=`. A nonzero exit, an interrupted run or no final message means the **critique is
   pending**, not clean. In that case I record that and stop. Next, `node "$CODEX_COMPANION" status --all --json`
   with `--cwd` gives the job id, thread id and model.
6. Spot-check each finding's cited `file:line` in the worktree (direct reads, no subagents) and note whether the
   citation holds. This check doesn't filter anything: every finding goes into the PR body.
7. PR body, REST only:
   - `gh api repos/jhawleyjr/ASTRA/pulls/78 --jq .body > $SCRATCH/pr78-body.md`
   - append a "Codex plan critique" section: companion 1.0.6, Codex CLI 0.154.0, route `task --fresh`,
     read-only, effort high, model, job id, thread id, exit status, the base/head SHAs, the verdict, and every
     finding (ID, severity, evidence, requested outcome, my citation check);
   - `gh api -X PATCH repos/jhawleyjr/ASTRA/pulls/78 -F body=@$SCRATCH/pr78-body.md`
   - read back `.body | length` and confirm the section is there.
8. Stop. Report the findings, the ids, the exit status and the non-draft PR state to Joe. No plan edits, no
   commits, no push, no STATE/PLAN changes. The worktree stays for Joe to remove, or I remove it if he asks.

## Verification
- The dispatch's own exit status is captured before any display, plus the log's final message.
- The job id and thread id come from the companion's `status`, not inferred.
- `git -C <worktree> status --short` stays empty after the run, and the main checkout is unchanged, which shows
  the critique was read-only.
- The PR body read back contains the new section.
