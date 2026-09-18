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

## Plan critique

For an expensive unpinned decision, Claude invokes the installed `/codex:rescue` route once with
`--fresh --wait` and an explicit **read-only plan critique** request: include plan text, acceptance commands,
repository/ADR context, and ask for failed assumptions only. Do not request implementation or `--write`.
This is the existing Codex plugin review route, not permission for general exploration subagents.
A resumed implementation conversation is not a fresh critique. If unavailable, leave critique pending.

This command guidance was checked against the locally installed companion 1.0.6 source. Resolve the
enabled installation each session rather than pinning that local version into project configuration.
