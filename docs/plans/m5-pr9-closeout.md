# M5 PR 9 (Tier 0): M5 closeout

Recovered from `~/.claude/plans/pasted-content-id-048e-m5-pr-cozy-seahorse.md` on 2026-09-30. Approval: the file records none; the work merged as #71. Never committed: neither the file nor git history says why.

## Context

PRs 1 to 8 of `docs/plans/m5-tooling.md` are merged (`origin/main` = `01a556f`, verified with `git fetch`;
worktree branch `worktree-m5-pr9-closeout` sits on it, clean). The PR 9 row closes the milestone: check M5 off
in PLAN.md with evidence links, record the held-out scope as open items (plan "Validation and risks", Q24),
and flip architecture §12's status line. Docs only: no code, test, golden, tolerance or ADR change, so no full
gate (workflow Tier 0 row).

Verified facts the edits rest on (read this session, not from STATE.md):

- Merged PR heads (`gh pr list`) and the last gated candidate inside each (`git merge-base --is-ancestor`,
  and every later commit touches only STATE/PLAN/lessons/evidence):
  PR 4 #66 `baddd0a`, PR 5 #67 `3ff2eac`, PR 6 #68 `63777fc`, PR 7 #69 `b41bdcb`, PR 8 #70 `5d51da9`.
  Earlier summaries in the same PRs (`3f74e37`, `553077d`, `6a827b8`, `1b4b759`, `8c80489`) are superseded.
- Numbers read from those committed summaries (all `check-full.sh` exit 0, `check: OK`):
  - `baddd0a`: `ctest --preset asan -R 'RunLog|Hdf5|NoAlloc|Health|SimulationTransport'` exit 0, 119 passed;
    R4 `RunLogDeterminism.TwoRunsOfTheDrydenCruiseToDifferentPathsAreByteIdentical` exit 0.
  - `3ff2eac`: `pytest tests/python -k 'log or replay'` exit 0, 34 passed.
  - `63777fc`: `pytest tests/scenarios tests/python -k scenario` exit 0, 64 passed.
  - `b41bdcb`: `pytest tests/python -k montecarlo` exit 0, 58 passed, `test_montecarlo_q19_64_runs_complete`
    listed; gate pytest 333 passed. `just check` → `just pytest` → `pytest -q` over `testpaths =
    ["tests/python", "tests/scenarios"]` (justfile:18,81; pyproject.toml:71), so the Q19 case is in the gate.
  - `5d51da9`: `pytest tests/python -k fgbridge` exit 0, 29 passed.
- Details not in any committed file (R9 replay cases, Q15 scenario list, Q19 dispersions, UDP decoder and
  golden provenance) are cited as "in the PR body".
- `fgbridge` has `pace: bool = False` (`python/astra/fgbridge.py:232,312`); `RealTime` mode is not built in
  `core/` (no symbol), so the writer thread item says so.

## Changes

### 1. PLAN.md milestone block (M5)

- `- [x] **M5 Tooling**`; keep the row text, then an evidence line, clause by clause, each linked to its
  merged head's `docs/reviews/evidence/<head>-gate-summary.log` as `[summary](...)`:
  - HDF5 logging: PR 4 (`baddd0a`: the `RunLog|Hdf5|…` asan suite, 119 passed; R4 byte-identical Dryden logs);
  - replay: PR 5 (`3ff2eac`: `-k 'log or replay'`, 34 passed; the R9 replay cases in the PR body);
  - scenario harness over `tests/scenarios/`: PR 6 (`63777fc`: 64 passed; the Q15 seed scenarios and bands in
    the PR body);
  - 64-run Monte Carlo under `just check`: PR 7 (`b41bdcb`: `test_montecarlo_q19_64_runs_complete` in the
    acceptance run, 58 passed, and collected by `just pytest`, 333 passed in the gate; the Q19 case in the PR
    body);
  - FlightGear packets by a decoder test: PR 8 (`5d51da9`: `-k fgbridge`, 29 passed; the table-built decoder
    over a real UDP socket in the PR body).
  - "Spec and plan: [plan](docs/plans/m5-tooling.md)."
- PR lines: `PR 2 (#64)`, `PR 7 (#69)`, `PR 8 (#70)`; nothing else in them changes (they already have the
  `Plan:`/`Packet:` form and no merge-only facts).
- New line: `PR 9 (#NN): M5 closeout (check-off with evidence links, open items for the held-out scope, the
  finalization ADR's four points and bridge pacing, architecture §12 status). Plan: [plan](...). Packet: in the
  PR body.` `#NN` is filled in by a second commit once the draft PR exists.

### 2. PLAN.md open items (appended in the existing `(origin: …; owner …)` … `Not started.` format)

Checked against the 19 existing items: none duplicates these. Follow-up C (recorder sizing) is related to the
recorder retirement; the new item notes retirement would moot it.

- Logger writer thread for `RealTime` mode (§9) (origin: M5 plan, held-out scope; Joe's call): M5 drains inline
  only; `RealTime` itself is not built.
- CLI, `astra run` with `--strict` (§15, §18) (origin: M5 plan Q24; Joe's call).
- Retire `ExchangeRecorder` once a lockstep test compares HDF5 logs (origin: M5 plan Q12, ADR-0022; Joe's call);
  moots Follow-up C.
- Wind events in scenarios (origin: M5 plan Q14; Joe's call): no wind write seam; wind is a config choice today.
- Closed-loop (autopilot) scenarios and Monte Carlo (origin: M5 plan Q16; Joe's call).
- Lockstep replay (origin: M5 plan Q11; Joe's call): `replay` refuses a `transport` log.
- `astra.fgbridge` pacing (origin: M5 PR 8 review; Joe's call): off by default, so a live bridge sends as fast
  as the sim runs and a real FlightGear reading at 62.5 Hz falls behind; live use needs `pace=True`, or pacing
  becomes the default outside tests.
- **Extend** the existing finalization item (not a new one): retitle to one superseding ADR to ADR-0025's
  Finalization (origin: M5 PR 5, extended by M5 PR 9; Tier 3, scheduling Joe's call) with points
  a. existing text (close_log() vs destructor; flag, not refuse; PR 6/7 consumer assertions until then);
  b. PR 4 review F1 residual (final close failing after `complete = 1`), accepted by Joe as a residual
     2026-09-28; fix is rename-on-complete (temporary name, rename to `log.path` after a successful close;
     complete by construction; covers a killed run);
  c. crash-readable log: metadata held in memory until `close_log()`, so a killed or failed run leaves an
     unopenable file and `allow_incomplete` never recovers a real run; needs a flush at drain;
  d. minor: `load`'s optional cross-checks, `final_state.frame == frames_run` and rate-group names against
     `/summary`'s min/max names; catch only hand-edited files.

### 3. docs/architecture.md §12 status line (line 413 only)

Replace "`astra.log`, `astra.montecarlo`, `astra.scenario`, and `astra.fgbridge` are M5
(`docs/plans/m5-tooling.md`)." with the existing "is PLAN.md M3 PR 6" idiom: `astra.log` is PLAN.md M5 PR 5
(on PR 4's run log), `astra.scenario` M5 PR 6, `astra.montecarlo` M5 PR 7, `astra.fgbridge` M5 PR 8
(`docs/plans/m5-tooling.md`). Nothing else in the architecture (no rev bump).

### 4. STATE.md (via /wrap, ≤ 40 lines)

Rewritten once, before the Codex pass so the pass covers the final diff; it records no review narrative, so a
fix after review does not change it. In flight: PR 9 draft, Tier 0, docs only; last verified: the two docs
checks; next: Joe's decision on PR 9, then M6 planning, Joe's to launch.

## Sequence

1. Edit PLAN.md and architecture.md; `python3 scripts/docs_check.py`; `git diff --check`.
2. Commit, push the task branch, `gh pr create --draft` with a body per `docs/templates/pr-description.md`
   (Tier 0: no full gate, scope and checks stated instead).
3. Fill the PR 9 line's number; STATE.md via `/wrap`; rerun both checks; commit; push.
4. `/review-cycle`: one Codex general pass on `origin/main..HEAD`. A factual error is fixed (one correction
   round); anything else is a follow-up in the PR body.
5. `/human-review`: PR body with checks, Codex verdict and thread id, "Needs Joe's decision" (approval; the
   finalization item's four-point scope as written), follow-ups. Update via `gh api -X PATCH …/pulls/<n>
   -F body=@file`.

## Verification

- `python3 scripts/docs_check.py` exit 0 (checks PLAN.md's new evidence links resolve).
- `git diff --check` clean.
- `git diff --stat origin/main` lists only PLAN.md, docs/architecture.md, STATE.md.
- Every number in the evidence line re-grepped against its linked summary file before commit.
