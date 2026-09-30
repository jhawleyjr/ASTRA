# Review: M6 PR 3, the `astra` CLI

Spec/plan: [PR plan](../plans/m6-pr3-cli.md) (D1 to D8, J1 to J6); [M6 plan](../plans/m6-analysis-tooling.md) R14,
R15, C-14; [ADR-0027](../decisions/0027-run-log-finalization.md) items 1, 3, 5 and 8; architecture §15.
Builder: Claude (Opus 5.5)
Reviewer: Codex (`gpt-6-astra`, `model_reasoning_effort = "high"` in the local Codex config; companion 1.0.6;
fresh context; adversarial review with Joe's focus: exit-code precedence, stdout and stderr, the log file's fate)
Scope: committed branch diff `origin/main` `da52239` to `4a39bca` (PR #81); worktree clean. Paths: `python/astra/cli.py`,
`python/astra/_runkit.py`, `python/astra/scenario.py`, `pyproject.toml`, `tests/python/test_cli.py`,
`tests/python/cli_fixtures/halt_after_warn.yaml`, `PLAN.md`, `docs/plans/m6-pr3-cli.md`.
Validation: on `4a39bca`, `./scripts/check-full.sh` from a fresh configure exit 0 with `check: OK` (pytest 566 passed,
5 skipped); `.venv/bin/pytest tests/python -k cli` 59 passed. The fix batch reruns both (PR body).
Round: fix 1 (pass 1: thread `01a0f374-1ac6-77a0-ad62-3026dd421a53`, verdict needs-attention).

## Finding [R-001]

Severity: minor (Codex: medium)
Status: fixed-unverified
Location: `python/astra/cli.py:256-257` at `4a39bca`
Requirement/ADR: plan D2 (`--log` against a config that has a `log` block exits 64)
Failure: with `log: {path: []}` and `--log cli.h5`, the message's `config.text("log.path")` raised `ConfigError`
before `_usage()`, so `main()` reported an internal error and exited 70. Reproduced by Claude
(`astra run <config> --log <path>`: exit 70, traceback).
Requested outcome: 64 whatever the block holds, with a message that cannot raise.
Builder response: fixed. `_existing_log_path()` reads the path for the message without raising (`<not a string>`,
`<none>`). The conflict test is now parametrized over a path, a nonscalar path, a block without a path and a
scalar block, each 64 with the usage line and both names.
Verification: pending (a focused Codex verification pass).
History: fix 1, open to fixed-unverified.
Human exception if needed: none.

## Finding [R-002]

Severity: minor (Codex: medium)
Status: fixed-unverified
Location: `python/astra/cli.py:419-429` at `4a39bca`
Requirement/ADR: plan, `astra trim` refusals (a config error exits 65); D3 (70 is a defect only)
Failure: `Config.from_file` accepts an empty document, and `astra.trim` then reads the file with PyYAML and raises
`AttributeError` (`None.pop`, default setpoint; `None.get`, explicit setpoint) or `TypeError` (a list
document). Exit 70 with a traceback. Reproduced by Claude for all three. `astra run` already exits 65 for them
(the core's `ConfigError`, `sim.dt` missing).
Requested outcome: 65 naming the config, for empty and non-map documents, with default and explicit setpoints.
Builder response: fixed. `_trim` refuses a document whose root is not a map before calling `astra.trim`
("the document must be a map", 65). A PyYAML parse failure in `astra.trim`'s own read of the file (YAML 1.1,
which can refuse a file yaml-cpp accepts) is now also a config error (65). New test: empty, list and scalar
documents, each with the default and an explicit setpoint, exactly 65 and that message.
Verification: pending (a focused Codex verification pass).
History: fix 1, open to fixed-unverified.
Human exception if needed: none.

## Completion

Pass 1 had no critical or major findings; two minor findings, both fixed in this batch and awaiting verification.
None rejected, none deferred. Correction rounds used: 1 of 2. Failed integrations: 0. The reviewer's
recommendation is not Joe's approval.
