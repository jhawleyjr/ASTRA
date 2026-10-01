# Review: README rewrite (Tier 0)

Spec/plan: [plan](../plans/readme-rewrite.md)
Builder: Claude (Opus 5.5)
Reviewer: Codex (companion 1.0.6; fresh context; adversarial review asked to check every README claim against the
code at head)
Scope: committed branch diff `367a84a` (origin/main) to `3b574bc` (PR #86): `README.md`, `docs/README.md`,
`docs/process/workflow.md`, `docs/plans/readme-rewrite.md`; worktree clean.
Validation: every README command and Python snippet run once in the worktree; `python3 scripts/docs_check.py` and
`git diff --check` clean. Results are in the PR body.
Round: pass 1 (thread `01a0f5fa-dc82-7651-bfe8-36271dec070e`, needs-attention); fix 1 (`437db3c`); fix verification
(thread `01a0f5fe-14a3-7b13-9fb6-ff8000921eb9`, approve, no new findings).

## Finding [R-001]

Severity: minor (Codex: medium)
Status: verified
Location: `README.md:105-108` at `3b574bc` ("Run a config", the Python example)
Requirement/ADR: [ADR-0027](../decisions/0027-run-log-finalization.md) (only `close_log()` publishes); the plan's
"Run from a config" item names `close_log()`.
Failure: with a `log` block added to the config, as the README invites, `Simulation.run()` only steps and drains,
and the destructor never publishes. The run leaves a `.partial` that strict `astra.log.load` and `replay` refuse.
Requested outcome: call `sim.close_log()` after `sim.run()`.
Builder response: fixed. The example calls `sim.close_log()` after `run()`, with a comment saying it publishes the
log when there is a `log` block. Verified: the snippet exits 0 on `cruise.yaml` (no log block), and with a `log`
block appended the run publishes and `astra.log.load` reports state `complete`.
Verification: Codex fix verification on `367a84a..437db3c`, thread
`01a0f5fe-14a3-7b13-9fb6-ff8000921eb9`: approve; the fix is verified, no new findings.
History: pass 1, open; fix 1, fixed-unverified; fix verification, verified.

## Finding [R-002]

Severity: minor (Codex: medium)
Status: verified
Location: `README.md:261-263` at `3b574bc` ("Write a plugin", step 4)
Requirement/ADR: `pyproject.toml` (`build-dir = "build/python"`, `editable.rebuild = false`); the README's own
add-an-aircraft steps.
Failure: `just build` builds the asan preset only. Python and the `astra` command load plugins from
`build/python/plugins`, which `just build` does not rebuild, so a new plugin is missing there and an edited one
runs its old binary. The aircraft check in this PR hit the same refusal (`no library ... build/python/plugins/...`,
exit 65) until `just python` ran.
Requested outcome: add `just python` before using the plugin from Python or the CLI.
Builder response: fixed. Step 4 now reads `just build`, then `just python`, and says why.
Verification: Codex fix verification on `367a84a..437db3c`, thread
`01a0f5fe-14a3-7b13-9fb6-ff8000921eb9`: approve; the fix is verified, no new findings.
History: pass 1, open; fix 1, fixed-unverified; fix verification, verified.

## Completion

Two findings, both factual errors in how-tos (Tier 0 makes them a correction round, not follow-ups), both fixed in
fix 1. Correction rounds: 1/2. No finding rejected or deferred. Codex's recommendation is not Joe's approval.
