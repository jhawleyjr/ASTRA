# STATE.md that survives the merge, and commits without a co-author trailer

Status: approved by Joe (2026-10-01) with five changes, folded in below and marked (J1) to (J5). Tier 2 (workflow behavior: a hook and `.claude/settings.json`).
Base: `origin/main` at 8e97040. Worktree and branch `worktree-state-and-attribution`, draft PR at the
first commit. This file is committed as `docs/plans/state-and-attribution.md`.

## Context

1. `/wrap` step 5 (`.claude/skills/wrap/SKILL.md:80-86`) still requires `## In flight` with the branch and
   "actual workflow stage", though #54 and #60 said STATE.md records nothing that changes at merge. The
   template wins: main's STATE.md lists M6 PR 5 (#89) as in flight, and it is merged.
2. `.claude/settings.json` has `"attribution": {"commit": "", "sessionUrl": false}`. #88's commits had no
   trailer; #89's squash commit has `Claude <noreply@anthropic.com>` as a co-author trailer (checked with
   `git log --format='%(trailers:key=Co-authored-by)'`). `claude --version` is **2.1.287 (Claude Code)**,
   at or above 2.1.281, so `"attribution": false` is safe to set.
3. Plot follow-ups from Joe's 60 s run, recorded as PLAN.md open items only.

Joe's authorization: the task prompt covers the hook and settings edits below. AGENTS.md and workflow.md
edits are his and are listed under "Needs Joe's decision".

## Decisions already made (Joe, 2026-10-01)

- STATE.md has exactly three `##` headings, in this order: `## Next step`, `## Open threads`,
  `## Honesty rules` (verbatim). No `## In flight`, `## Last verified result` or `## Tried / failed`.
- A paused or blocked wrap describes its branch under `## Next step` and says it applies to that branch
  only. There is no extra heading, so docs_check passes on a paused branch too.
- An open thread that is already a PLAN.md open item gets a one-line pointer to it, not a copy. For example,
  the (M6) resource-check item is a pointer. C-12's report part is not in PLAN.md, so it stays written out.
- docs_check also enforces the 40-line cap.

## Changes

### 1. STATE.md

- **`.claude/skills/wrap/SKILL.md` step 5**, rewritten. STATE.md is written as main will read it after the
  squash-merge, in at most 40 lines, under exactly the three headings above. Every line has to stay true
  after the merge:
  - the next task, stated directly;
  - open threads that outlive the PR, as pointers when PLAN.md already holds them;
  - the honesty rules, verbatim.

  A PR's stage, approval, CI rerun, results, correction and integration counts, and pending decisions go in
  its PR body only. A wrap of blocked or paused work is read only by the session that resumes that branch.
  It may describe the branch under `## Next step`, and it says so explicitly. The pre-merge wrap rewrites
  it. Remove "Include the branch/PR ..., actual workflow stage" and the `## In flight` requirement. The
  skill's line 11 ("still need only STATE and a brief/PR body") is unchanged.
- **STATE.md header line** becomes `# STATE (written at /wrap as main reads it after the merge; ≤ 40 lines,
  never appended)`.
- **AGENTS.md "Read first"** (Joe's): the STATE.md clause becomes "session handoff, rewritten once at
  `/wrap` as main reads it after the merge: the next step and open threads; a PR's stage lives in its PR
  body". Keep AGENTS.md plus CLAUDE.md under 100 lines.
- **docs/process/workflow.md** (Joe's):
  - §Sources of truth (line 11): a short clause on the shape.
  - Step-12 row (line 39): "next action/owner visible in STATE (next task) or the PR body (this PR's stage)".
  - Line 162: "STATE.md's `Tried / failed` section links there" becomes "STATE.md never copies them".
  - Line 313's "writes STATE once" gets the same "as main reads it after the merge" wording.
- **docs/agents/claude-code.md:34**: the same wording as workflow.md line 313.
- **scripts/docs_check.py**: add `check_state(root, problems)`, called from `main()`. It is structural, not a
  phrase list (lessons 2026-09-28).
  - Constants: `STATE_HEADINGS = ["Next step", "Open threads", "Honesty rules"]` and `STATE_LINE_CAP = 40`.
  - It collects every line matching `^## ` outside fenced code blocks and requires the list to equal
    `STATE_HEADINGS`, so missing, extra, misordered and renamed headings are all reported. The problem
    message names the found and the expected lists.
  - Over 40 lines is a problem. A missing STATE.md is a problem too: the file is required.
  - (J1) The body under `## Honesty rules` (surrounding blank lines stripped) must equal the
    `STATE_HONESTY_RULES` constant in docs_check.py, the one source of that text, so "verbatim" is a check.
- **tests/python/test_docs_check.py** gets new tests, which reuse the module loader already there:
  - an `## In flight` STATE.md fails, and the message names `In flight`;
  - the right three headings pass;
  - the headings in the wrong order fail;
  - a 41-line file fails;
  - a `## ` line inside a fenced block is ignored;
  - (J1) Honesty rules text that differs from the constant fails;
  - the repository's own STATE.md passes.

  (J4) The fail-before-pass the PR cites: implement the check, run `python3 scripts/docs_check.py` and see
  it fail on the current STATE.md, naming `In flight`. Then rewrite STATE.md and see it pass. A test that
  fails with AttributeError before `check_state` exists is not cited.
- **STATE.md rewrite** (the new shape; the final text comes from `/wrap`, at most 40 lines):
  - `## Next step`: M6 PR 6 (Monte Carlo figures, `astra.mc_analysis`, `astra mc analyze`, `astra mc rerun`)
    or PR 9 (the frame-time benchmark), per `docs/plans/m6-analysis-tooling.md`.
  - `## Open threads`:
    - PR 8 (the report) inherits C-12's report part, written out: matplotlib's counter and glyph ids repeat
      across SVG files, so inlined SVGs need namespacing.
    - PR 8 also inherits a pointer to PLAN.md's (M6) `resource_refs.py` open item.
    - A pointer to PLAN.md's (M6) plot open items (the ground-track aspect, plus the three below), for any
      later plot PR.
  - `## Honesty rules`: verbatim from the current file.

### 2. Co-author trailer

- **`.claude/settings.json`**: the `attribution` object becomes `"attribution": false`, made with the Edit
  tool. Verification: load `git show origin/main:.claude/settings.json` and the new file with
  `python3 -c json`, drop `attribution` from both, and assert the two dicts are equal. Also check
  `git diff .claude/settings.json` shows only the attribution lines. This also hides the PR footer.
- **`.claude/hooks/no-main.sh`**: add rule 3. The parsing is in the hook's existing `python3` style.
  - **When it applies:** the command matches `git[^|;&]*\bcommit\b`, the same shape as rule 2.
  - **What it refuses (exit 2):** any of the following, matched case-insensitively.
    - A line of the command text that starts (after whitespace or quotes) with `co-authored-by:`. This
      covers heredocs, whether as `-m "$(cat <<'EOF' ...)"` or `-F - <<'EOF'`.
    - An `-m`/`--message` value that begins with the trailer, as in `-m "Co-Authored-By: ..."`.
    - `--trailer` with a `co-authored-by` key.
    - An `-F <path>`, `-F<path>`, `-<flags>F <path>`, `--file=<path>` or `--file <path>` whose file
      contains such a line. The path is resolved against the hook's `cwd`.
    - (J3) With `git -C <dir> commit -F <relative path>`, the path resolves against `<dir>` (itself
      resolved against `cwd`). A `-C` value it cannot parse is refused, not guessed.
    - An `-F` file that the hook cannot read. This refusal is reported as unverifiable: write the file in
      an earlier command, or use `-m`. `-F -` is stdin, and the text scan above has already covered it.
  - **What it allows:** prose that mentions the trailer mid-line.
  - **Documented gaps** (comments in the hook and the cases file): `-C`/`-c`/`--amend --no-edit` reuse a
    message; a file named through a variable is refused, not read; a later statement in a compound command
    that merely prints a trailer-shaped line is false-blocked, which is the safe direction.
- **`.claude/hooks/tests/no-main.cases`** and **`run.sh`**: `run.sh` writes `msg-trailer.txt` and
  `msg-clean.txt` into `$ROOT_ON_BRANCH`. The cases run with `cwd` `$ROOT_ON_BRANCH`.
  - **Refused (2):** `-m` (second paragraph); a `-m "$(cat <<'EOF' ...)"` heredoc; a lowercase trailer in
    `-F - <<'EOF'`; `-F msg-trailer.txt`; `--file=$ROOT_ON_BRANCH/msg-trailer.txt`; `-F missing.txt`;
    `--trailer "Co-authored-by: ..."`; (J3) `git -C sub commit -F msg-trailer.txt`
    where the file exists only under `sub/`.
  - **Allowed (0):** plain `-m`; a clean heredoc; `-F msg-clean.txt`; a `-m` that mentions "a Co-Authored-By
    trailer" mid-line.
- **Commits on this branch**: none may carry the trailer. Commit messages avoid the literal string entirely,
  so a plain `grep -ic` is zero as well. This session's own commits are the hook's first live test.
  (J2) `"attribution": false` probably cannot take effect in this running session, so its clean commits
  prove the hook, not the setting. The PR body says so. Follow-ups gets one item: check the next session's
  first commit for the trailer.
- **docs/lessons.md**, one appended entry (2026-10-01): a setting that stopped working after a CLI update
  (`attribution.commit: ""`: #88's commits had no trailer and #89's had one; settings reference and
  anthropics/claude-code#45137), and a rule that lost to its own template (`/wrap` step 5's `In flight` vs
  "nothing that changes at merge", #54/#60). Rule: fix both with a check (the hook, docs_check), not a
  sentence.

### 3. PLAN.md

- Three (M6) open items, origin "M6 PR 5, Joe's look at a 60 s run, 2026-10-01", each "Not started":
  - (J5) legends from the `ax.legend(loc="upper right")` call in `python/astra/plot.py` cover data: TAS's end, alpha's right edge,
    beta and phi. Place them outside the axes, which keeps the SVG deterministic;
  - `python/astra/plots/flight.yaml` puts TAS and CAS on one panel spanning 270 to 475 kt, which flattens
    both;
  - a 60 s run gives a 25 MB HTML (every 1 ms frame). Consider an opt-in decimation for HTML, and perhaps
    SVG.
- This PR's one line, in the workflow PR list near #83/#85/#88:
  `Workflow (#NN): STATE.md written as main reads it after the merge (docs_check enforces its shape); no co-author trailer (settings + hook). Plan: <link>. Packet: none.`

## Not in scope

- `session-start.sh`'s "resume the first unfinished gate" line. It is still accurate, and the hook edit
  isn't authorized.
- Rewriting earlier PR commits. Nothing in `.claude/settings.json` beyond `attribution` changes.

## Verification

1. `claude --version` (done: 2.1.287), recorded in the PR body.
2. `.venv/bin/pytest tests/python/test_docs_check.py -q` passes.
3. `python3 scripts/docs_check.py`: fails on the old STATE.md (naming `In flight`), then exits 0 after the
   rewrite. This is the fail-before-pass the PR cites (J4).
4. `.claude/hooks/tests/run.sh`: every case passes, including the new rule-3 cases.
5. The settings key-equality script exits 0, and `git diff origin/main -- .claude/settings.json` shows only
   `attribution`.
6. `git log origin/main..HEAD --format='%(trailers:key=Co-authored-by)'` is empty, and
   `git log origin/main..HEAD --format=%B | grep -ic co-authored-by` prints 0.
7. `git diff --check`.
8. `rm -rf build && ./scripts/check-full.sh`, in the background, waiting on its pid in bounded foreground
   steps (lessons 2026-10-01). Exit 0 plus `check: OK`; commit
   `docs/reviews/evidence/<head>-gate-summary.log`.
9. Codex review via `/review-cycle` on the final diff. Then `/wrap` (branch-scope docs step) and a PR body
   per `docs/templates/pr-description.md`, with Docs impact, the `claude --version` output, the
   docs_check fail-before-pass, the J2 caveat, and the AGENTS.md and workflow.md edits under "Needs Joe's decision".

Rollback: revert the squash commit. Before this PR, settings.json only needs `attribution` restored.
