# Review: M5 PR 7, `astra.montecarlo`

Spec/plan: [plan](../plans/m5-tooling.md) PR 7 row (Q17 to Q20; R14 to R17, R15b), Joe's approval
amendments in PR #69's body; [ADR-0025](../decisions/0025-hdf5-run-log-and-replay.md)
Builder: Claude Opus 5.5
Reviewer: Codex (companion 1.0.6; fresh context; one general review and one adversarial review, both
read-only)
Scope: branch diff `6227a17..1b4b759` (PR #69; `3db7bec` adds only the gate summary): `python/astra/_runkit.py`,
`python/astra/scenario.py`, `python/astra/montecarlo.py`, `tests/python/test_montecarlo.py`,
`tests/python/montecarlo_fixtures/cruise_64.yaml`, `pyproject.toml`, `docs/process/validation.md`, `PLAN.md`
Validation: `./scripts/check-full.sh` exit 0 with `check: OK` at `1b4b759`
([summary](evidence/1b4b759-gate-summary.log)); acceptance `.venv/bin/pytest tests/python -k montecarlo`
exit 0, 52 passed
Round: fix 1 (correction round 1 of 2)

## Finding R-001

Severity: major
Status: fixed-unverified
Location: `python/astra/montecarlo.py:502-506` (`_error_row`) at `1b4b759`
Requirement/ADR: plan R15 (byte-identical `summary.parquet`); Joe's amendment 2 (the summary does not
depend on `out_dir`)
Failure: raised by both passes (general P2; adversarial high). With `trim` set, a drawn value the config
refuses (negative mass) fails inside `astra.trim`, whose Config origin is `apply_trim`'s random temporary
copy (`/tmp/astra_scenario_trim_<random>/cruise.yaml`); the mass plugin's `ConfigError` names it, and only
`out_dir` was normalized, so identical batches wrote different summaries. The R15 error-batch test ran
without trim.
Requested outcome: a stable identifier for the trim copy in recorded messages, with byte-identity coverage
for a trimmed batch with error rows; `astra.scenario`'s messages unchanged.
Builder response: fixed. `_runkit.TRIM_DIR_PREFIX` names the prefix (value unchanged, so scenario behavior is
the same); `_error_row` replaces `<tempdir>/astra_scenario_trim_<random>` with `<trim_copy>`. New test
`test_montecarlo_trimmed_error_rows_do_not_depend_on_the_trim_copy_path`: the bad-mass batch with trim,
`workers=1` vs `workers=4`, byte-identical, messages carry `<trim_copy>/cruise.yaml` and no temporary path.
Scratch mutation R1 (normalization removed) fails it.
Verification: pending
History: round 1, open → fixed-unverified

## Finding R-002

Severity: major
Status: fixed-unverified
Location: `python/astra/montecarlo.py:236-240` (`Rng.truncated_normal`) and the `clip` check in
`_distribution`, at `1b4b759`
Requirement/ADR: plan Q20 (a batch completes or fails; it never hangs), Joe's brief (define `clip`)
Failure: general P2. Any finite `clip > 0` was accepted; the redraw keeps a fraction erf(clip/sqrt 2) of
draws, so `clip: 1.0e-12` needs about 1.25e12 draws on average, and the parent waits on workers without a
timeout.
Requested outcome: refuse impractical clips or bound the sampler's cost.
Builder response: fixed. `clip` must be at least `MIN_CLIP` = 1 standard deviation (at least 68 % of draws
kept), refused at spec load naming the key. Docstring updated. New `clip-below-one` refusal case; scratch
mutation R2 (the old `> 0` rule) fails it. The floor is listed for Joe in the PR body.
Verification: pending
History: round 1, open → fixed-unverified

## Finding R-003

Severity: major
Status: fixed-unverified
Location: `python/astra/montecarlo.py:578-581` (`_run_pool` start-up) and `_stop`, at `1b4b759`
Requirement/ADR: plan Q20/R15b (an interrupt terminates the pool; no worker is left alive)
Failure: adversarial high. A SIGINT after `process.start()` returned but before `processes.append(process)`
left a started worker outside cleanup, blocked in `recv`; an interrupt inside `_stop` abandoned the workers
not yet killed or joined.
Requested outcome: defer SIGINT across spawning and registration and through cleanup, and cover the start-up
and shutdown boundaries.
Builder response: fixed. `_sigint_deferred()` holds SIGINT (main thread only) and re-raises it, or reports it,
at the end of the block. Start-up registers the pipe and process before `start()` and runs under it; `_stop`
runs under it, skips join for a never-started process, and empties its lists, so a second call is safe;
`_abort` holds SIGINT across the stop and the log removal and reports a held interrupt as
`BatchInterrupted`. The success path now stops the workers inside the `try`, so an interrupt there is also
a batch failure. New tests: the deferral itself; a SIGINT right after the first `start()` (both workers
start, then `BatchInterrupted` naming every run, no live worker, no file); a SIGINT inside the first
`close()` of a finished batch (all three workers closed, `BatchInterrupted` with no incomplete runs, logs
kept, no summary). Scratch mutations R3 and R4 (either deferral removed) fail them.
Verification: pending
History: round 1, open → fixed-unverified

## Finding R-004

Severity: major
Status: fixed-unverified
Location: `python/astra/montecarlo.py:705-708` (`_write_summary`) and `run`, at `1b4b759`
Requirement/ADR: plan Q20 (no `summary.parquet` for a failed batch)
Failure: adversarial medium. The summary was renamed into place before `table.to_pandas()`, and `run` printed
to stdout afterward outside any handler, so a conversion failure, an interrupt or a `BrokenPipeError` raised
with a published summary, and bypassed `BatchError`/`BatchInterrupted`.
Requested outcome: failure handling through summary creation and reporting; no summary left on failure.
Builder response: fixed. `_write_summary` builds the DataFrame first and removes its partial file on any
failure; `run` wraps summary writing and the report, and on any exception removes `summary.parquet` (with
SIGINT held) and raises `BatchError` or `BatchInterrupted` with no incomplete runs, keeping the complete
logs. New test: a failing `print` gives `BatchError` naming `BrokenPipeError`, three logs and no summary.
Scratch mutation R5 (withdrawal removed) fails it.
Verification: pending
History: round 1, open → fixed-unverified

## Completion

Four findings, all major, none critical; all four fixed in correction round 1 and awaiting Codex
verification. None rejected or deferred. The general review ran no tests; the adversarial review ran no
builds or tests. Reviewer recommendation is not Joe's approval.
