# Review: M6 PR 6, Monte Carlo analysis

Spec/plan: [plan](../plans/m6-pr6-mc-analysis.md); [M6 plan](../plans/m6-analysis-tooling.md) R5 to R8, R7b, R10, R15
Builder: Claude (claude-opus-5-5)
Reviewer: Codex (companion 1.0.6, model `gpt-6.1-sol` per `~/.codex/config.toml`; fresh context; adversarial)
Scope: committed branch diff `8b4852b..63bcbe9` (`--base origin/main`); the worktree was clean
Validation: `rm -rf build && ./scripts/check-full.sh` exit 0, `check: OK` on `d6574b4`
([summary](evidence/d6574b4-gate-summary.log)); `.venv/bin/pytest tests/python -k mc_analysis` 69 passed
Round: fix 1 (of two autonomous correction rounds)

Pass 1: job `review-muqm7cku-r3c9nt`, Codex session `01a0fb6b-1b7e-7c62-a83d-60c46a775011`, exit 0, verdict
**needs-attention**, 4 findings (1 high, 3 medium). Claude reproduced R-001 and R-003 directly and confirmed R-002 and
R-004 in the code; all four are accepted and fixed in this commit.

## Finding [R-001]

Severity: major
Status: fixed-unverified
Location: `python/astra/mc_analysis.py:913-914` (at `63bcbe9`), `envelope`
Requirement/ADR: plan D8 ("Logs are loaded one at a time, and only the signal column is kept")
Failure: `np.asarray(frame[signal], dtype=np.float64)` is a view of the log's whole rate-group matrix, so every
retained trace kept that matrix alive. Claude's probe on the fixture: a 4,000-byte altitude column whose base is a
(500, 64) array. Memory for a large batch would scale with the signal count, not the one column.
Requested outcome: retain an independent copy of the signal column only.
Builder response: fixed. `np.array(..., copy=True)` with a comment. New test
`test_mc_analysis_the_envelope_keeps_only_each_signal_column` asserts each retained failing trace owns its data
(`flags.owndata`, `base is None`).
Verification: pending (Codex verification pass).
History: round 1, open, then fixed-unverified.
Human exception if needed: none.

## Finding [R-002]

Severity: major
Status: fixed-unverified
Location: `python/astra/mc_analysis.py:1112-1119` (`render_figures`); the same gap in `montecarlo.rerun`
Requirement/ADR: plan D9 ("never into the batch"), M6 plan invariant "Tools read their inputs and never modify them"
Failure: `--out` equal to the batch, inside it, or `.` run from the batch directory wrote `mc-*.svg` into the batch.
`astra mc rerun --out <batch>/sub` would also have created a directory inside the batch.
Requested outcome: refuse an output location that is the batch or inside it, including aliases and symlinks, before
creating anything.
Builder response: fixed. `montecarlo.inside(path, directory)` compares resolved paths. `render_figures` raises
`BatchError` and `rerun` raises `RerunError`, both before any directory is created, and both CLI paths exit 65. New
test `test_mc_analysis_nothing_is_written_into_the_batch` covers:
- analyze, with `--out` equal to the batch, to `batch/figures`, to a symlink alias and to `alias/figures`;
- analyze with the default `.` from inside the batch;
- rerun into `batch/rerun` and `alias/rerun`.

Each case exits 65, and the batch's file hashes are unchanged.
Verification: pending.
History: round 1, open, then fixed-unverified.
Human exception if needed: none.

## Finding [R-003]

Severity: major
Status: fixed-unverified
Location: `python/astra/mc_analysis.py:359-364` (`_comparison`)
Requirement/ADR: plan D4 (`seed` is an allowed numeric column; comparisons exact)
Failure: every literal became a float, so `seed == 11925146632114698671` (the fixture's run 0) matched nothing:
the literal rounds to a different double. Claude confirmed `int(...) == float(...)` is `False` for it.
Requested outcome: integer literals compare exactly with integer columns, including above 2^53.
Builder response: fixed. An integer literal (`[-+]?[0-9]+`) stays a Python `int`, and Python compares `int` with
`float` exactly. New test `test_mc_analysis_r10_an_integer_literal_compares_a_64_bit_seed_exactly` uses 2^60 and
2^60 + 1 with `==`, `>`, `<=` and `!=`.
Verification: pending.
History: round 1, open, then fixed-unverified.
Human exception if needed: none.

## Finding [R-004]

Severity: major
Status: fixed-unverified
Location: `python/astra/cli.py:731-735` (`_mc_rerun`), through `montecarlo.run_one`'s error row
Requirement/ADR: plan D9 (74 for a failed write; 1 only for a completed comparison that differs)
Failure: `run_one` turns every exception into an `error` row, so a failed log write (ENOSPC, `LogError`) became a
row that differs: exit 1, with no log.
Requested outcome: keep write failures typed through the rerun API and exit 74.
Builder response: fixed.
- `run_one` takes `reraise=` (default `()`, so batches are unchanged).
- `rerun` passes `(_core.LogError, OSError, LogHaltError)` and raises `RerunWriteError` with nothing compared. The
  run's own `.partial` has already been discarded by `run_logged`.
- `astra mc rerun` maps `RerunWriteError` to 74 and prints nothing to stdout.

New test `test_mc_analysis_cli_rerun_a_failed_write_exits_74_with_nothing_compared` injects an ENOSPC `OSError` and a
`LogHaltError`.
Verification: pending.
History: round 1, open, then fixed-unverified.
Human exception if needed: none.

## Completion

Open after round 1: four major findings, each fixed-unverified, awaiting the Codex verification pass. None was
rejected or deferred. The reviewer's recommendation is not Joe's approval.
