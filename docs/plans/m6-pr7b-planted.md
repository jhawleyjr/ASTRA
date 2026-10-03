# M6 PR 7b: R9, the planted cause, with its negative control (Tier 2), plus the gate-rerun rule

Spec: [M6 plan](m6-analysis-tooling.md) R9, Q9, Q15 and C-08, as moved by [PR 7's plan](m6-pr7-mc-sizing.md) D8 and
D11, on Joe's approved numbers ("Requirements change", "Probe evidence", STATE.md). The workflow change is Joe's own
text (2026-10-02, this task's request).
Risk: **Tier 2** (Python tests and fixtures, plus a workflow-rule edit Joe authorized). No `core/`, `plugins/`,
`aircraft/`, `icd/`, `tests/golden/**`, tolerance or ADR change; no new dependency.
Base: `origin/main` `a0c7903` (#93 merged). Worktree `m6-pr7b-planted`, branch `worktree-m6-pr7b-planted`, draft PR.
Status: **approved by Joe with changes, 2026-10-02** ("Joe's changes at approval" below). Building.

## Context

PR 7 built `astra.mc_sizing` and moved R9 (the planted cause in the real sim) and C-08's closure here. R9 is the
end-to-end check that `astra.mc_analysis`'s KS and Spearman rankings find a real physical cause in a real batch, with
a stated effect, a strict margin and a negative control. Joe also asked for a workflow rule: after a passing gate, a
Markdown-only change needs only the docs checks, not a new gate.

## Joe's changes at approval (2026-10-02, in substance)

1. D4 builds the config through `astra.montecarlo`'s own code (a one-run batch of the R9 base, or `_base_config`
   plus `_runkit`), not a retyped `from_dict(safe_load(...))`, and compares it with yaml-cpp's file load.
2. Workflow rule: Claude's call on workflow.md's review-fix paragraph ("rerun affected checks and the full gate
   before final approval"), on the `<gated-head>..HEAD` form of `git diff --check` plus `--name-only`, and on the
   wording "Markdown files and evidence logs" (evidence logs are not Markdown). Resolved in "Workflow change".
3. Gate time: the numbers go to Joe whatever their size.
4. At step 3's stop, report each pilot parameter's ρ̂ and jackknife v with N.

## Repository facts (read at `a0c7903`)

- `montecarlo.draw` takes one 64-bit draw per `uniform` and per `seed` dispersion, in spec order
  (`python/astra/montecarlo.py:305`). With `cm_alpha_per_rad` **last** in the main spec, the control spec (the same
  list without it) draws identical inert values and wind seeds for every run index: the control is the same batch
  without the cause.
- `ranking_runs("0.3", m)` is 120 at m = 4 (85, 103, 113, 120 for m = 1..4), so P = 120 means one cause plus three
  ranked inert parameters; the wind seed is a seed dispersion and is never ranked.
- `aero_math.hpp:137-146`: `croll_aileron_per_rad`, `croll_rudder_per_rad` and `cn_rudder_per_rad` multiply the
  aileron and rudder, which `cruise.yaml` fixes at 0.0 open loop. They are exactly inert: no draw changes a run.
- `mc_sizing.check_one(batch, RankingClaim(metric, "top" | "null", r_min))` gives `needed` (already
  `max(projection, RANK_MIN_N)`) or `None` with the reason in `notes`; `batch_seed` reads the master seed from the
  batch's record, so pilot specs must be their own files (a `dataclasses.replace`d seed would be misrecorded).
- `mc_analysis.ks_ranking`, `spearman_ranking`, `outcomes(batch, "max.v0.derived.alpha > 0.23")`; KS returns a
  "too few runs" status with fewer than 5 failing or passing rows.
- The gate runs `pytest -q` once, last in `just check`; `montecarlo.run` uses `os.cpu_count()` workers (32 here).
  pytest keeps its last three `tmp_path` trees, so batches must be removed by a finalizer.
- `-k planted` also selects `test_mc_analysis_r6_*planted*` and `r7_*planted*` (synthetic, fast); they stay in.

## Decisions (for Joe's approval)

- **D1. Specs** (`tests/python/mc_planted_fixtures/`): base config `aircraft/notional_transport/configs/cruise.yaml`,
  no trim (the fixed state, as the probes ran), `duration: 10.0`, `signals: [v0.derived.alpha]`. Dispersions, in this
  order:
  - `vehicle.models.0.croll_aileron_per_rad` uniform [0.05, 0.10];
  - `vehicle.models.0.croll_rudder_per_rad` uniform [0.0, 0.02];
  - `vehicle.models.0.cn_rudder_per_rad` uniform [−0.30, −0.10];
  - `physical.wind.seed: seed`;
  - `vehicle.models.0.cm_alpha_per_rad` uniform [−0.6, +0.6] (main only; the control keeps the config's −0.6).

  Four files: `planted_pilot.yaml` / `planted_control_pilot.yaml` (seed 7071, runs 120) and `planted.yaml` /
  `planted_control.yaml` (seed 707, runs N). A header comment states the criterion and the minimum effect before any
  final run. A test asserts the four agree except seed, runs and the control's missing last dispersion.
- **D2. The pilot runs in the test, every gate** (my reading of "measure what R9's pilot ... adds"). The pilot sizes N
  with the top claim (main) and the null claim at r_min 0.3 (control); `N = max(both, RANK_MIN_N)` must equal the
  final specs' committed `runs`, so a rebuild that moves the sizing fails loudly instead of resizing silently. Pilot
  directories are deleted before the finals run. Peak disk: max(1.34 GB, 2·N·5.6 MB).
- **D3. Assertions on the final batches** (the M6 R9 contract, Joe's numbers):
  - KS on pass/fail at `max.v0.derived.alpha > 0.23`: `cm_alpha_per_rad` has the largest D and the smallest p,
    D ≥ 0.5, D minus the next parameter's D ≥ 0.25 (strict margin, so a tie fails), and BH at q = 0.05 rejects it.
  - Spearman against `max.v0.derived.alpha`: ρ ≥ +0.5, the largest |ρ|, margin ≥ 0.25 over the next |ρ|, BH rejects it.
  - No inert-parameter BH assertion in the main batch (Joe).
  - **C-08 control:** zero failing rows, so KS reports "too few runs" and flags nothing; Spearman runs (the metric
    varies with the wind seed) over m = 3 with no BH rejection; the control's inert and seed columns equal the main
    batch's, run for run.
- **D4. PyYAML route** (Joe's change 1). One test builds the R9 base config through `astra.montecarlo`'s own code
  and through yaml-cpp's file load, runs each logged for R9's 10 s, and requires equal rate-group data. The
  montecarlo side is the module's own reading (`load` and `_base_config`, then `run_one`'s config path, or a
  one-run batch), never a retyped `from_dict(safe_load(...))`; the exact entry point is chosen in the build and
  recorded in the PR body. This checks R9's config only; PLAN.md's (M6) PyYAML item stays open, with a note.
- **Stops (no silent fallback):** N > 357, a `None` projection (a top tie, or a control |ρ̂| ≥ 0.3), or any D3
  assertion failing on seed 707 (the probes put a by-chance control BH flag near 5 %) comes to Joe with the numbers.
  Never a new seed, range or threshold.

## Pilot results (step 3, 2026-10-02; pilots deleted after sizing)

Both pilots at `8066a84`: master seed 7071, 120 runs each, all `ok`, about 1.6 s wall each on 32 workers. The main
pilot had 60 failing and 60 passing runs at `max.v0.derived.alpha > 0.23`; the control had 0 failing. Sized with
`astra mc size ranking --batch ... --metric max.v0.derived.alpha` (`--top` for the main pilot, `--null 0.3` for the
control). v is the jackknife variance of `z = atanh(ρ̂)`. An independent leave-one-out loop with
`scipy.stats.spearmanr` gives the same v to every printed digit.

| pilot | parameter | ρ̂ | v | simultaneous \|ρ\| bounds |
|---|---|---|---|---|
| main (m = 4) | `croll_aileron_per_rad` | 0.288353 | 9.059882e-03 | [0.0590, 0.4888] |
| | `croll_rudder_per_rad` | 0.092312 | 8.718317e-03 | [0, 0.3147] |
| | `cn_rudder_per_rad` | 0.003514 | 8.359082e-03 | [0, 0.2278] |
| | `cm_alpha_per_rad` | 0.996472 | 5.888876e-02 | [0.9882, 0.9989] |
| control (m = 3) | `croll_aileron_per_rad` | 0.023210 | 9.096736e-03 | [0, 0.2464] |
| | `croll_rudder_per_rad` | 0.103681 | 7.944400e-03 | [0, 0.3072] |
| | `cn_rudder_per_rad` | −0.012716 | 9.556401e-03 | [0, 0.2419] |

- Main top: `needed` 30 (the projection is at or below `RANK_MIN_N`). Control null at 0.3: `needed` 130.
- **N = max(30, 130, 30) = 130**, at or below 357. Peak disk is then max(1.34 GB, 2 · 130 · 5.6 MB = 1.46 GB).
- The main pilot's `croll_aileron_per_rad` ρ̂ of 0.288 is about 3σ at n = 120. That coefficient is exactly inert,
  and the metric is almost entirely `cm_alpha_per_rad`, so this ρ̂ is in effect the sample correlation of two
  independent draws, which is 0.297 in this pilot. The stream itself does not correlate them: over 20,000 run
  indices of this spec, every pairwise Spearman correlation of the five draws is within ±0.009 (1/√20000 ≈ 0.007).
  It is chance on seed 7071, and it does not touch R9's assertions, since no inert-parameter BH assertion is made
  in the main batch.

## Joe's go-ahead (2026-10-03, in substance)

- Finals on seed 707 with N = 130 and the threshold unchanged. Joe checked N independently: `croll_rudder`'s upper
  |ρ| bound is 0.2996 at N = 130 and 0.3003 at 129, and the main batch's top claim clears at 30 (0.961 against
  0.648).
- Order: commit the final specs, then the protocol fixture, then the R9 and C-08 tests, and push all of it before
  the first run on seed 707. The first seed-707 result is the result. A failure stops for Joe, and the seed, range,
  threshold and assertions do not change.
- **The threshold's rationale** (Joe's correction, in `planted.yaml`'s header). 0.23 rad (13.2°) is the source's
  stall angle: `docs/refs/aircrafts/737.xml`'s CLalpha table peaks there (CL 1.20) and falls to 0.20 at 0.46 rad.
  `cl_alpha_per_rad` is the straight line through (0, 0.20) and (0.23, 1.20), so the linear model is valid only up
  to 0.23 rad and has no post-stall behavior. A failing run went past the stall angle: a positive or near-zero
  `cm_alpha_per_rad` pitched it up toward the high-α equilibrium, crossing near +0.03 within 10 s. Claude read the
  table, and it holds.
- The main pilot's `croll_aileron` ρ̂ = 0.288 is chance on seed 7071 (about 1 in 180 for any of the three inert
  parameters, Joe's figure), with the 20,000-index stream check above.
- Follow-up: the M7 envelope-monitor open item is unblocked. The notional transport can declare
  `alpha_max_rad = 0.23` from the CLalpha peak, and a halt there would make R9's criterion `status == halt`. It is a
  Tier 3 PR of its own, and scheduling it is Joe's call (PLAN.md's (M7) item records it).

## The first seed-707 result (2026-10-03, at `0c9630e`, pushed before the run)

`.venv/bin/pytest tests/python -k planted -v --durations=0` exited 0: 8 passed (the six `test_planted_*` tests plus
`test_mc_analysis`'s synthetic R6 and R7), 1477 deselected, in 8.09 s. The fixture's setup (both pilots and both
finals) took 6.53 s.

The numbers below come from a deterministic replay of the same specs on the same build (the fixture deletes its
batches). The replay sized the pilots to the same 30 and 130.

| batch | method | parameter | effect | p | BH |
|---|---|---|---|---|---|
| main (58 fail, 72 pass) | KS | `cm_alpha_per_rad` | D = 1.000000 | 4.45e-38 | rejected |
| | | `croll_aileron_per_rad` | 0.158525 | 0.346 | |
| | | `cn_rudder_per_rad` | 0.157567 | 0.355 | |
| | | `croll_rudder_per_rad` | 0.102011 | 0.848 | |
| | Spearman | `cm_alpha_per_rad` | ρ = +0.995767 | 1.46e-134 | rejected |
| | | `cn_rudder_per_rad` | −0.188595 | 0.0317 | |
| | | `croll_aileron_per_rad` | −0.153314 | 0.0816 | |
| | | `croll_rudder_per_rad` | 0.016499 | 0.852 | |
| control (0 fail, 130 pass) | KS | (none) | "too few runs: 0 failing and 130 passing" | | |
| | Spearman | `croll_rudder_per_rad` | −0.107649 | 0.223 | |
| | | `croll_aileron_per_rad` | −0.085849 | 0.332 | |
| | | `cn_rudder_per_rad` | −0.071952 | 0.416 | |

The margins are 0.841 (KS) and 0.807 (Spearman), each against at least 0.25.

## Tests (`tests/python/test_mc_planted.py`; every name starts `test_planted_`)

A module fixture runs the protocol (pilots, sizing, pilot deletion, finals) under `tmp_path_factory` and removes
everything in a finalizer that also runs on failure. Tests: `..._specs_agree`, `..._pilot_sizes_the_final_batches`
(P = `ranking_runs("0.3", 4)` = 120, pilot seed 7071 in the record, N equal to the specs, N ≤ 357, pilots gone),
`..._r9_ks_ranks_the_cause_first_with_margin`, `..._r9_spearman_ranks_the_cause_first_with_margin`,
`..._c08_negative_control_flags_nothing`, `..._r9_config_pyyaml_route_matches_yaml_cpp`.
Acceptance: `.venv/bin/pytest tests/python -k planted`.

## Workflow change (its own commit, first; applies to this PR from the start)

- `docs/process/workflow.md`, Verification: replace "then rerun after substantive fixes/integration" with Joe's rule:
  after a passing full gate, a change limited to Markdown files and evidence logs (docs, plans, README, PLAN.md,
  STATE.md, lessons, `docs/reviews/evidence/` logs) needs only `just docs-check` and
  `git diff --check <gated-head>..HEAD`, not a new gate; `git diff --name-only <gated-head>..HEAD` shows the delta,
  and the PR body names the gated head and that delta. Any other changed file (a code comment or docstring, a test,
  fixture, config, script, hook or setting) makes the gate stale, as do integration and substantive fixes.
- The same file's review-fix paragraph ("rerun affected checks and the full gate before final approval"): the full
  gate is rerun unless the fixes are Markdown-only under Verification's rule (Joe's change 2).
- `.claude/skills/wrap/SKILL.md` step 1: the same rule in place of "mark the full gate stale if a fix touches a file
  the gate builds or tests (a code comment included)", and "run this docs step before the final gate where it can".
- `.claude/hooks/session-start.sh`'s gate line ("after implementation and substantive fixes/integration") does not
  contradict the rule and is not in Joe's authorization: unchanged, noted in the PR body.

## Build order (commit and push after each)

1. Plan renamed to `docs/plans/m6-pr7b-planted.md` (moved from the main checkout's untracked draft) and the workflow
   commit; open the draft PR.
2. Specs (pilot specs, final specs with `runs` pending), the spec-agreement and PyYAML-route tests.
3. Run the pilots once by hand (`astra mc size ranking --batch` on each) and delete them. Stop and report to Joe
   each pilot parameter's ρ̂ and jackknife v with N (Joe's change 4), and ask if N > 357. Commit N.
4. The fixture, R9 and C-08 tests; `-k planted` green on seed 707, or stop and bring Joe the failure.
5. Gate time (Q15): `pytest tests/python -k planted --durations=0` (pilot, finals, total) and the full suite with and
   without `-k "not planted"`; the gate's pytest line against PR 7's 94.2 s. Recorded in
   `docs/reviews/evidence/<head>-planted-timing.log` and the PR body. The numbers go to Joe whatever their size
   (Joe's change 3), with the choice: keep in `just check`, or opt-in like `just benchmark`.
6. Docs step before the gate: PLAN.md (PR 7b line; the PyYAML item's note), `docs/future-work.md:77`,
   architecture §12 status and the M6 note (Joe's file: listed under "Needs Joe's decision"), README check.

## Verification

- `.venv/bin/pytest tests/python -k planted`; regressions `-k 'mc_sizing or mc_analysis or montecarlo'`; pyright,
  `ruff check .`, `ruff format --check .`, `just docs-check`, `git diff --check`.
- `ps` for any other ASTRA build, then `rm -rf build && ./scripts/check-full.sh` in the background, no edits during
  the run; exit 0 and `check: OK`; trimmed summary committed. Later Markdown-only edits follow the new rule.
- `/review-cycle` (Codex, at most two correction rounds), `/human-review` (PR template), `/wrap` before merge.

Rollback: additive; reverting removes the fixtures and the test module, and restores the two workflow sentences.
