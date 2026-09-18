# Dispatching Codex review from Claude

Claude owns execution and fixes; Codex reviews. Use the installed Codex companion, not a second
Claude reviewer. The native plugin commands are user-facing wrappers; Claude calls their companion
script with the Bash tool. Locate the enabled plugin installation (plugin context or Claude's installed
plugin registry), confirm its `scripts/codex-companion.mjs` exists, and assign its absolute path to
`CODEX_COMPANION`. Do not use an arbitrary first result from a recursive `find`; multiple versions may exist.
Check the installed command/help files if flags differ. Missing runtime/authentication means pending
review; record it rather than claiming success or silently changing models/installations.

## Candidate scope

Inspect `git status --short --untracked-files=all`, staged/unstaged diffs and the intended branch diff.
The installed companion's `--base` selects a **committed branch diff**, even if the worktree is dirty;
`--scope working-tree` selects staged, unstaged and untracked changes, not earlier branch commits.

From the ASTRA checkout, with the verified companion path assigned:

```sh
# Uncommitted candidate, including new files; do not add --base here.
node "$CODEX_COMPANION" review --scope working-tree --wait
# Committed candidate against its intended base (origin/main only when that is the agreed base).
node "$CODEX_COMPANION" review --base origin/main --wait
```

Run only the applicable command for a single-scope candidate. For **commits plus uncommitted changes**,
review both scopes and explicitly give the adversarial pass the combined base/head and working-tree
context; inspect their interaction. Alternatively create an already-authorized candidate commit first
and review the resulting complete branch diff. Do not discard, stash or commit unrelated work to simplify
review. A pass over only one part does not cover the whole candidate. Record scope and revisions in evidence.

For required/requested adversarial review, use the same chosen scope and concrete focus, for example:

```sh
node "$CODEX_COMPANION" adversarial-review --scope working-tree --wait   "Read the assigned spec/plan and docs/agents/review.md. Challenge domain validity, frame signs per ADR-0001, SI units per ADR-0002, determinism and per-frame allocation; report evidence-backed defects only. Do not edit files."
```

Replace that focus with the actual task's artifacts and failure modes. Native `review` takes no custom
focus text; adversarial review does. Preserve raw output, actual model/job identity, scope and findings.
Use a real completed tool process handle; interrupted dispatch or empty output is not clean review.
Read [review-cycle](../../.claude/skills/review-cycle/SKILL.md) for the bounded fix/retest loop.

## Safe prompt passing and exit handling

Focus text is a shell argument (argv), not a file the companion reads; the shell expands it
before `node` ever sees it. Build it in a variable from a **single-quoted** string or a
**quoted** heredoc (`<<'FOCUS'`, quoted delimiter), then pass the variable as one quoted
argument (`"$focus"`) — never a bare double-quoted literal:

```sh
focus=$(cat <<'FOCUS'
Read docs/agents/review.md and the assigned spec/plan. Verify findings R-001, R-002 from
docs/reviews/<feature>-review.md against base <sha1> head <sha2>. Report a verdict per
finding; do not edit files or run builds/tests.
FOCUS
)
node "$CODEX_COMPANION" adversarial-review --base origin/main --wait "$focus" \
  >review.log 2>&1
status=$?
tail -n 60 review.log
printf 'exit=%s\n' "$status"
```

Backticks and `$()` inside a **double-quoted** argument are expanded by the shell before
`node` runs, not by the reviewer: a focus that mentions a command in backticks (for example,
a sentence about `` `just check` ``) executes that command right there and splices its
captured output into the argument. This is a real incident, not a hypothetical: during the
workflow-v3 review, a double-quoted focus containing a backticked `just check` ran the full
local gate, and the substituted output overran the shell's argument length limit, so `node`
never started at all — see `docs/lessons.md`. A single-quoted string or a quoted heredoc
delimiter suppresses this and every other form of shell expansion in the text.

Keep focus text compact and reference repository files and revisions instead of embedding
whole documents: name paths (e.g. `docs/reviews/<feature>-review.md`), base/head revisions,
and finding IDs, and let the reviewer read them itself. Argument length is finite regardless
of quoting; concatenating large documents into a focus can exhaust it even with no quoting
error at all.

Capture the dispatch's actual exit status immediately after the command, before displaying
its output, and never by putting the dispatch at a non-final stage of a pipe — `cmd | tail`
reports `tail`'s exit status, not the dispatch's, so a successful `tail` can mask a launch
that never happened. The example above does this: redirect to a log, read `$?` right after,
then `tail` the log, then report the saved status. A nonzero status, an interrupted launch,
or output with no completed review result is pending review, never clean review — the
existing rule at line 40 above and `review-cycle/SKILL.md` step 3 both already say so; the
addition here is the concrete failure mode and the concrete way to detect it.

## Fix verification

A fresh `review`/`adversarial-review` pass only ever sees the current diff; it cannot see a
prior round's finding IDs, builder responses, or which rejections an earlier reviewer agreed
to, and the workflow's review-fix limits require reviewer verification and reviewer agreement
on any rejection. Treating a clean fresh pass as proof that earlier findings were verified is
an unsupported inference.

To verify a fix batch: first record each finding's ID, severity, status and builder
disposition in `docs/reviews/<feature>-review.md` (the [findings template](../templates/review-findings.md);
`templates/README.md` already names this destination), including the concrete fix (file/line,
what changed) for anything marked fixed. Then dispatch a normal `adversarial-review` (never
`review`, which takes no focus text) with a compact focus, built per the rules above, naming
that file's path, the base/head revisions under verification, and the specific finding IDs to
check:

```sh
focus=$(cat <<'FOCUS'
Verify findings R-001 and R-002 recorded in docs/reviews/<feature>-review.md against base
<sha1> head <sha2>. For each, report whether the recorded fix actually resolves it in the
current diff, or state why not; do not treat an unmentioned finding as agreed. Do not edit
files or run builds/tests.
FOCUS
)
node "$CODEX_COMPANION" adversarial-review --base origin/main --wait "$focus" \
  >review.log 2>&1
status=$?
tail -n 60 review.log
printf 'exit=%s\n' "$status"
```

Require an explicit per-finding verdict; silence about a finding is unverified, never
agreement. Record the reviewer's verdicts back into the findings file's `Verification:` field
and update each finding's status and round history.

## Plan critique

For an expensive unpinned decision, Claude invokes the installed `/codex:rescue` route once with
`--fresh --wait` and an explicit **read-only plan critique** request: include plan text, acceptance commands,
repository/ADR context, and ask for failed assumptions only. Do not request implementation or `--write`.
This is the existing Codex plugin review route, not permission for general exploration subagents.
A resumed implementation conversation is not a fresh critique. If unavailable, leave critique pending.

This command guidance was checked against the locally installed companion 1.0.6 source. Resolve the
enabled installation each session rather than pinning that local version into project configuration.
