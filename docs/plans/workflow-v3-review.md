# Workflow v3 review — corrected

Recovered from `~/.claude/plans/review-the-proposed-twelve-step-typed-moon.md` on 2026-09-30. Approval: the file shows Part 1 awaiting Joe's approval and Part 2 held for it; workflow v3 merged as #30. Never committed: it predates `docs/plans/`, whose first plan was committed on 2026-09-19 (#31).

## Context

`22a5009` ("New workflow") on `workflow-v3` replaces v2 with a twelve-step, risk-tiered
workflow: 38 files, +1012/-344. M2 PR 3b (the NESC ten-case harness) will be the first task
to run under it, so defects cost twice.

Joe asked for findings with evidence, an independent Codex pass, and no edits. Scope is now
split: **Part 1** is the small `review-dispatch.md` + `lessons.md` update he asked for
mid-review and is the only thing awaiting approval here. **Part 2** is the corrected broader
plan, shown for his review and **held** until he approves that scope separately.

**Candidate scope:** Joe asked for working-tree Codex scope. There is none —
`git status --porcelain` and `git ls-files --others --exclude-standard` are both empty. The
whole candidate is `22a5009` against `origin/main` = `main` = `40b484b`. Both reviews were
dispatched `--base origin/main --scope branch`, covering 100% of it; `--scope working-tree`
would have reviewed an empty diff and reported a false clean.

## Correction: what actually happened to the failed reviewer launch

My earlier F13/F14 diagnosis was wrong. The corrected account:

I passed the adversarial focus as a **double-quoted** argument containing literal backticks
around a command name — `` `just check` `` — in the clause about checks with no executable
form. Bash performed command substitution **before** `node` was reached and **executed the
full gate**. Its captured stdout was substituted into the argv, which overran `ARG_MAX`, so
`node` never launched:

```
/bin/bash: line 4: /usr/bin/node: Argument list too long
```

The trailing `| tail -200` succeeded, so the pipeline reported exit 0 and the launch failure
was masked.

This mechanism explains every observation, including the one I had flagged as unresolved:
command substitution captures **stdout only**, so `ctest`'s summaries were swallowed into
the argv while `just`'s recipe echoes and clang-tidy's "N warnings generated" (both
**stderr**) leaked into the log. That is why the captured log looked like a gate run with
its test output missing. `build/*/.ninja_log` end times confirm a real incremental gate ran
in preset order (debug 19:19:46 → python 19:23:22).

Three things I asserted that were wrong, and are withdrawn:

- **Not** the companion returning success on a failed launch. The 0 came from `tail`, the
  last element of the pipeline. The companion never ran at all.
- **Not** merely a long handwritten focus. What I typed was a few thousand characters; the
  overrun came from the substituted gate output.
- **`default: check` is irrelevant.** Nothing probed with a bare `just`; `just check` was
  executed exactly as I wrote it. Changing the default recipe would not have prevented this,
  and I am no longer proposing it as a fix.

Consequently I am **not** proposing a `[codex]` log-marker contract. The requirement is the
ordinary one: a **completed review result**. The lesson is the quoting and the masked exit
status.

## What I actually executed (vs. inspected)

| Command | Result |
|---|---|
| `just hooks-test` | **51 passed, 0 failed** |
| Codex `review --base origin/main --scope branch --wait` | completed, 34 s, **no findings** |
| Codex `adversarial-review` (1st attempt) | **did not run** — see correction above |
| Codex `adversarial-review` (re-run, compact focus) | completed, **needs-attention**, 2 medium |
| Markdown link + anchor check (ad-hoc python, all repo `*.md`) | **0 broken, 0 bad anchors** |
| `wc -l AGENTS.md CLAUDE.md` | 68 + 9 = **77 ≤ 100** |
| SKILL.md frontmatter, 4 files | 4/4 valid; `name` matches directory |
| British-spelling grep over the workflow surface | none |
| CI `Scope` classifier re-run in bash, 3 inputs | see D5 |
| `guard-oracle.sh` driven with 7 synthetic tool calls | see D4 |

**Not executed:** `./scripts/check-full.sh` / `just check` deliberately (though the botched
substitution ran a partial gate as an accident, which is **not** evidence of anything).
`./scripts/check-fast.sh` not run.

**Sanitizer status, corrected scoping:** `docs/lessons.md` records a LeakSanitizer/ptrace
failure, and STATE.md carries it forward as a general blocker. That result belongs to the
environment where it occurred; I should not assume it holds here. Part 1's verification runs
the required local validation **in this working environment** and records what actually
happens, rather than inheriting the claim.

## Part 1 — the documentation update (awaiting approval)

Documentation only: two files. No product code, no plugin files. No review or build
launched. Detailed guidance stays in `review-dispatch.md`; `/review-cycle` already links
there, so no SKILL.md changes.

### A. `docs/agents/review-dispatch.md` — new section "Safe prompt passing and exit handling"

1. **Pass focus text literally.** Build it with a **single-quoted** string or a **quoted**
   heredoc (`<<'FOCUS'`) assigned to a variable, then pass `"$focus"`. State plainly that
   backticks and `$()` inside a **double-quoted** argument are evaluated by the shell before
   the reviewer sees them — a focus that quotes a command name *runs that command*, and its
   output is spliced into the argument. Single quotes and a quoted heredoc delimiter prevent
   it. Cite this incident: a backticked `just check` executed the gate and the substituted
   output overran `ARG_MAX`, so the reviewer never launched.
2. **Keep focus compact; reference repository files instead of embedding documents.** Name
   paths, revisions and finding IDs and let the reviewer read them. Focus text is argv and
   is length-limited.
3. **One safe launch example that captures the reviewer's actual exit status.** Record the
   status immediately after the command, then display the log, then report the status —
   never as the last element of a pipe, because a successful `tail` masks a failed launch.
   Consistent with the existing pattern at `docs/process/validation.md:44–50`.
4. **A failed or interrupted launch stays "review pending."** Reinforce
   `review-dispatch.md:40`: the pass counts only on a **completed review result**. A
   nonzero status, an aborted launch, or absent output is pending review, never clean
   review.

### B. `docs/lessons.md` — one appended entry

Append-only; existing entries untouched. Factual content: during the `22a5009` workflow
review, an `adversarial-review` focus passed as a double-quoted argument contained literal
backticks around `just check`; Bash executed the gate and substituted its stdout into the
argv, `node` failed with `Argument list too long`, and a trailing `tail` made the pipeline
report exit 0 so the failure was silent. Note that stdout was captured while stderr leaked,
which is why the log looked like a gate run with its test output missing. Rule: single-quote
or quoted-heredoc any focus text, keep it compact with file references, and capture the real
exit status before displaying a log.

### Verification for Part 1 (no review, no build launched)

1. Extract the new example verbatim and run it against **harmless stubs** in the scratchpad —
   a script that prints a line and exits with a chosen code — substituted for the companion.
   Assert the reported status equals the stub's real exit code for **both** a success stub
   and a failure stub, proving `tail` does not mask failure.
2. Demonstrate the quoting rule with a stub too: show that a double-quoted focus containing
   a backticked harmless command (e.g. `echo`) is substituted, and that the single-quoted
   and quoted-heredoc forms pass the text through literally. No gate command anywhere.
3. `bash -n` the extracted example.
4. Re-run the markdown link/anchor check over both edited files.
5. `git diff --check`.
6. Required local validation in **this** environment, recorded accurately, per Joe's
   instruction: `just hooks-test` (expect 51 passed), then `./scripts/check-full.sh`
   redirected to a retained log, reading its real exit status and the `check: OK` line. If
   the sanitizer fails **here**, that is reported as an environment result for this
   environment — not generalized, and not called a pass.

## Part 2 — corrected broader plan (held for Joe's approval of scope)

Defects are separated from optional cleanup, per Joe's instruction.

### Defects

**D1 — the twelve-step table never names implementation or integration.**
`workflow.md:26–39`. Grepping the table for `implement|build|code` hits only row 3 ("map
requirements to changes") and row 9 ("Builder resolves findings"). The operating model's
Claude verbs are plan / implement / validate / fix / integrate; *implement* and *integrate*
appear in no row, while row 7's exit condition already presumes a diff. `/wrap` is told to
"Find the first unfinished gate" from this table (`wrap/SKILL.md:17`), so the commonest real
state — code half-written — has no gate to name.

*Fix, preserving the original twelve-step conceptual mapping:* keep all twelve rows, their
numbers and titles. Add a short "stage transitions" note under the table making explicit
that implementation's durable result (the bounded diff plus tests) lands across steps 5–6
before step 7's gate, and giving integration an explicit transition and exit condition
("combined tree passes the full gate") ahead of step 11. Update `wrap/SKILL.md`'s gate list
to name implementation and integration. No renumbering, no deleted rows.

**D2 — the two-round budget has no durable, task-specific home.**
Stated four times — `workflow.md:124`, `workflow.md:128` ("A fresh context … does not reset
the budget"), `review-cycle/SKILL.md:15`, `wrap/SKILL.md:25` — but its only homes are
`templates/task-status.md:17` and `templates/review-findings.md:8`, both **optional**
(`templates/README.md:3`; `workflow.md:181` "otherwise STATE/PR suffice"). STATE.md's
mandated sections (`wrap/SKILL.md:50–53`) have no round-count field and the current file
carries none. For the default single-package task the budget is unenforceable across exactly
the fresh context it is written to survive.

*Fix:* require both counts — review/fix rounds **and** failed integrations — in a durable
task-scoped location: the PR body or task status when one exists, mirrored in STATE.md's
handoff so a fresh session reads them without chat. Name the field in `wrap/SKILL.md` step 4
so it is rewritten every handoff.

**D3 — the documented workflow constraints have no reproducible check.**
`validation.md:61–63` prescribes checking "Markdown links, command names, and the root
instruction line budget." None is executable: `justfile:15` (`check`) has no such step;
`hooks-test` reads only `.claude/hooks/tests/*.cases` (`justfile:82–83`); `format-check` and
`format` both `-path ./.claude -prune` (`justfile:60,65`). **Nothing reads
`.claude/skills/**`.** CI's `Scope` step classifies `.claude/skills/wrap/SKILL.md` as
`code=false` *and* `hooks=false` (re-ran both greps), so a skills-only PR runs one step:
`bash -n` on two scripts. Since full validation is deliberately local and the local gate
omits the process files, a broken frontmatter or dead link ships green.

Codex found the sharper edge independently: STATE.md claims "All four active **skill
validators** … passed," but no such validator exists in the repo — an untraceable claim,
which the project's own honesty rule forbids.

*Fix:* one minimal, reproducible recipe (`just docs-check`) covering exactly the documented
constraints — markdown links/anchors, SKILL.md frontmatter and name-matches-directory, the
AGENTS.md+CLAUDE.md line budget, `bash -n scripts/*.sh`, `git diff --check` — wired into
`check`; extend CI's hooks pattern to include `.claude/skills/`; reword `validation.md` to
name the recipe; replace STATE.md's untraceable sentence with the command name. Each check
must be shown to fail on a deliberately broken input, or it is not a check.

**D4 — prior findings and dispositions are not supplied to fix-verification reviews.**
Raised by Codex; I verified it. `review-dispatch.md:19–23` gives commands that pass only a
diff target, and `:37` records that native `review` takes no focus text. Yet
`review-cycle/SKILL.md:29` requires verifying "previous findings," and `workflow.md:116`
requires reviewer verification plus agreement for a rejection. A fresh `review` cannot see
finding IDs, builder responses, or which rejections were agreed, so a clean second pass
proves only that the new diff looks fine.

*Fix:* define a fix-verification dispatch — record findings and their dispositions in
`docs/reviews/<feature>-review.md` (the template exists; `templates/README.md:13` already
names that destination), then pass a compact focus naming that path, the base/head
revisions and the finding IDs requiring verification. Require a per-finding verdict; silence
is not agreement. Part 1's quoting and exit-status rules apply to that dispatch.

**D5 — the oracle guard's unanchored `tolerance` pattern produces false positives.**
`guard-oracle.sh:26` `guarded='(^|/)(docs/decisions/|tests/golden/)|tolerance'` and `:43`
`g='(docs/decisions/|tests/golden/|tolerance)'`. The `tolerance` alternative is unanchored,
matching anywhere in a path or command. Driving the hook directly (strings assembled at
runtime so the probe itself was not a match):

```
exit=2  Write docs/plans/nesc-tolerances.md          <- a prose plan document
exit=2  Write core/util/tolerance.hpp
exit=2  Bash  ctest --preset asan -R nesc > build/tolerance.log   <- a log redirect
exit=2  Write tests/physics/nesc/tolerances.yaml     <- correct and intended
exit=0  Write tests/physics/nesc/case01.yaml
```

My first probe attempt was itself blocked, because the command text contained
`> build/tolerance.log`. `guard-oracle.sh` is **unchanged** in `22a5009`
(`git log main..HEAD` on that path is empty), so this is inherited — but v3 newly tells
agents to write `docs/plans/<feature>.md` and to redirect gate output to a log, and PR 3b is
the tolerance PR.

*Prerequisite before any narrowing, per Joe:* first **inventory every protected asset** —
enumerate what actually exists under `tests/golden/**` and `docs/decisions/**`, every
tolerance-bearing file in the tree, and what PR 3b will add — then propose a pattern, and
demonstrate in `guard-oracle.cases` **both** that the false positives above now pass **and**
that every real protected asset is still blocked. Narrowing an oracle boundary is Joe's
authorization, not mine.

**D6 — `workflow.md:84–85` misstates the disarm.** "SessionStart removes both markers."
`disarm-goldens.sh:8–15` removes only the *file* marker, in each of its two accepted
locations; a hook cannot unset an env var in a running session, as `session-start.sh:31`
notes. Reword to say the file marker is deleted in both locations and
`ASTRA_TOUCH_GOLDENS` lasts as long as the launched session.

**D7 — CI `Scope` reports `code=true` on an empty diff (latent).** `check.yml:64–66`: with
`$changed` empty, `printf` emits one blank line matching no prefix, so `grep -qv` succeeds.
Re-ran it: confirmed. Inert while `GATE=local`; a flip to full would run `setup-ubuntu.sh` +
`just check` for a no-op diff. Guard with `[ -n "$changed" ]`.

**D8 — the human packet has three entry points.** `review-cycle:31`, `wrap:37` and
`human-review:9`, with only "do not create duplicate artifacts" (`human-review:11`)
preventing two packets for one candidate. State in `human-review/SKILL.md` that it is
idempotent against an existing packet for the same candidate. (Cross-invocation is otherwise
acyclic and each skill is invocable where it claims; no coverage gap beyond D1's.)

### Optional cleanup — not defects

- **Four near-duplicate placeholder READMEs.** `docs/specs`, `docs/plans`, `docs/tasks`,
  `docs/reviews` each contain only a 6–9 line README; plans vs specs differ by 6 lines. The
  information is already in `docs/README.md:10–11` and `templates/README.md:7–15`, and
  `workflow.md:17–18` warns against exactly this. Deleting them is tidying, not a fix.
- **`docs/architecture.md:53`** still lists `skills (/task /wrap /retro)`; neither exists and
  the four current skills are unnamed. Accepted doc, so Joe's call.
- **`PLAN.md:14,22–23,28–30`** reference `/retro` and `scripts/tokens.py` in checked M0/M1
  rows. Leaving them as history is defensible.
- **`justfile:11 default: check`.** Unrelated to this incident and withdrawn as a fix. If
  Joe wants a bare `just` to be inert anyway, that is a standalone preference.

### Open questions for Joe — not actions

- **`scripts/tokens.py`.** Its docstring names `/retro` as a consumer and that skill is
  gone; in-repo grep finds references only in `PLAN.md:22,28`. I have **not** established it
  has no consumers — Joe may invoke it directly or from outside the repo, and it passes
  `ruff`. I am not calling a tested utility obsolete. Question: keep it and give
  `workflow.md` §Model and effort one line naming it as the cost-review tool, or retire it?
- **"No recon subagents"** (`workflow.md:193`, `claude-code.md:11`) conflicts with the
  harness's plan-mode instruction to launch Explore/Plan agents. Recorded as information
  only: I am **not** proposing repository guidance that overrides higher-priority harness
  instructions. In this session I read the 38 files directly.

### Status of the candidate

`workflow.md:50` puts "build/CI/hook/workflow behavior" in **tier 2**; `workflow.md:56`
allows skipping the product gate only at tier 0, and `22a5009` changes `stop-check.sh`,
`settings.json` and `check.yml`. So the candidate sits at **step 7** until a real local full
gate is recorded in this environment — which Part 1's verification does. Both Codex passes
evaluated `22a5009` and are invalidated by any substantive change (`workflow.md:122`).

### Codex evidence and its limits

Enabled installation resolved from `~/.claude/plugins/installed_plugins.json`:
`codex@openai-codex` 1.0.6 (three copies exist on disk, so resolved from the registry rather
than `find | head -1`, per `review-dispatch.md:7`).

General pass — completed, 34 s: "No actionable defects were identified … runtime hook tests
and the full validation gate were not run in this read-only review."

Adversarial pass (re-run, job `review-mu6c7gfe-zb8eac`) — **needs-attention**, read-only, no
builds: (1) fresh dispatch does not carry finding history → **D4**; (2) skills have no
reproducible validation entry point, and STATE.md's "four skill validators" do not exist →
**D3**.

Codex raised 2 of these items. It did not raise D1, D2, D5, D6, D7 or D8, and it did not
catch my dispatch error. Those remain **my claims, not independently verified**.
