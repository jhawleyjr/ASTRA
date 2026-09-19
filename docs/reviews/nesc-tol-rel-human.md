# Human review packet: NESC oracle ADR-0015 — drop tol_rel, absolute atmosphere floors, exclude SIM 2 from cases 4/5

PR / candidate base and head: base `6f6acd6cd86e80bca5420770c9273826528d9374` (= `origin/main`'s tip
at candidate creation, confirmed via `git fetch origin && git log --oneline HEAD..origin/main` —
empty, no rebase needed). Head: `9e66a9a`, branch `worktree-m2-tol-rel`, 1 commit. Draft PR:
<https://github.com/jhawleyjr/ASTRA/pull/32> — draft, not marked ready.
Spec/plan/task: `docs/plans/nesc-tol-rel-removal.md` (approved by Joe across three review rounds —
Q1 atmosphere floor, Q2 SIM-2 exclusion scope revised twice to a full-case exclusion, Q3 packet-
editing question). ADR: `docs/decisions/0015-nesc-tol-rel-drop.md` (accepted, 2026-09-19).
Risk: 3 — `tests/golden/**` (tolerance-derivation formula change, participant exclusion), new
accepted ADR (`docs/process/workflow.md`#risk-tiers).
Decision requested: approve this candidate (the `tol_rel` removal, the atmosphere floor conversion,
the cases-4/5 SIM 2 exclusion, and the two open items below) toward a merge decision. Not requesting
merge/ready-for-review now — this packet is for your read first.

## What changes and why

Elaborates ADR-0013/ADR-0014 without superseding either. Three changes, landed together because the
last two are preconditions PR 3c (`PLAN.md`'s M2 closer) would otherwise have had to redo itself:

1. **Drops `tol_rel` from the NESC pass criterion entirely.** `ChannelBand` loses its `tol_rel`
   field; `nesc_cases_test.cpp::expect_in_band` now compares `|actual - reference| <= tol_abs` alone.
   `tol_abs` already bounds the ensemble's worst absolute disagreement at every sample time
   (ADR-0013/0014); `tol_rel` (a single case-wide scalar) only ever added slack, measured in PR #31's
   own packet at 48-95% on four zero-crossing channels and genuinely non-discriminating on a fifth
   (`force_y` in cases 4/5). Dropping it cannot cause a false failure relative to the ensemble by
   construction.
2. **Converts ADR-0014's atmosphere-channel floor from relative to absolute.** Density/pressure/
   temperature's floor was `1e-12` relative (scaling against each sample's own `|reference|`); with
   `tol_rel` gone, that has nothing left to scale. Converted to `1e-12 x` each channel's ISA-76
   sea-level value (`tests/golden/atmosphere/generate_isa1976_table.py`/`isa1976_table.hpp` — the same
   sea-level constants that file already uses and cites). Verified never binding: smallest margin
   across all thirty currently-committed atmosphere values is `~7.9e6x` (case02 temperature); Codex's
   own independent recomputation found the same minimum margin at `220,257x`.
3. **Excludes SIM 2 entirely (every channel) from cases 4 and 5.** A survey (computed independently
   against the raw CSVs, reproduced by Codex's own recomputation) found SIM 2 dominating the ensemble
   spread of nearly every compared channel in these two round-Earth cases specifically — not only the
   previously-known `v_north`/`force_x`/`force_y` — consistent with Table 78 footnote b's own
   "unexplained X-body force and local gravity differences" language. One rule in `excluded()`
   (`sim == 2 and case.num in (4, 5)`) replaces the old footnote-b-only `force_x` rule; a per-channel
   exclusion list was considered and rejected (would have produced mutually inconsistent references
   within one `CaseBand` — see ADR-0015's rejected alternatives).

`check_non_discriminating()` is retained even though every channel kind's floor is now strictly
positive, making its guard condition and its `accepts_zero` check mutually exclusive by construction
— kept as a permanent structural-invariant check, not a live edge case.

**Diff shape** (`git diff --stat 6f6acd6 HEAD`): 17 files, 6575 insertions / 5687 deletions. Ten
`case*_band.hpp` regenerated (cases 01-03/06-10: ~38 lines each, only the removed `tol_rel` field
line per channel plus a header-comment reflow; cases 04/05: ~5500 lines each, every channel not
already SIM-2-excluded changed). `generate_nesc_band.py` (205 lines changed), `nesc_band_common.hpp`
(20 lines), `nesc_cases_test.cpp` (6 lines — `expect_in_band` only). New: `docs/decisions/0015-*.md`
(144 lines), `docs/plans/nesc-tol-rel-removal.md` (406 lines, the spec/plan brief). `docs/plans/
nesc-harness.md` (79 lines) and `PLAN.md` (16 lines) updated to record resolution and the R8 baseline.
No `core/`, `plugins/`, or `WrenchSource`/`Integrator`/`Eom` file touched.

**Compatibility**: internal test-only data (`ChannelBand`); no production code references it. Cases
4/5 stay `GTEST_SKIP()`ed — no change in what currently runs or passes.

## Evidence

| Check / acceptance | Result / date / duration | Log or retained artifact / candidate |
|---|---|---|
| `ctest --preset {asan,debug,release,static-plugins} -R 'physics.Nesc'` | 8 passed, 2 skipped, 0 failed, every preset, 2026-09-19 | Interactive session output |
| `just check` (full gate, all 4 presets, format/tidy/docs/hooks/pyright/pytest) | **`check: OK`**, exit 0, 2026-09-19 (third attempt — first two caught by the gate itself: an unformatted `nesc_band_common.hpp` and a >100-char line in the generator's own docstring, both plain mechanical fixes, not tolerance/logic changes) | `/tmp/.../scratchpad/just_check_final.log` (session-local); `100% tests passed, 0 tests failed out of 283` × 4 presets, `0 errors, 0 warnings, 0 informations` (pyright), `32 passed` (pytest) |
| Regenerated golden diff vs. predicted table | Byte-identical for cases 01-03/06-10 except the removed `tol_rel` field; cases 04/05 match a table of fifteen predicted `tol_abs` values per case, computed independently from the raw CSVs *before* regenerating, verified programmatically (not by eye) | `docs/plans/nesc-tol-rel-removal.md`'s prediction tables; `tests/golden/nesc/case{04,05}_*.hpp` |
| Generator floor-ratio report, cases 04/05 | Matches the predicted floor-governed channel set exactly (see "Reconciliation" note below for one summary-sentence omission, not a computation error) | Generator stderr, this session |
| R8 non-gating dry run | Cases 4/5 skip lifted locally only (never committed), one preset (`asan`), per-channel `max\|diff\|/tol_abs` captured, skips restored, `git diff`/`git status` verified clean before the final gate ran | `docs/plans/nesc-harness.md`'s "R8 dry-run baseline for PR 3c" table |

Reference/toolchain: same as PR #31 (NASA/TM-2015-218675, `docs/refs/`, gitignored, read directly this
session for the SIM 2 survey). No performance/allocation regression — no production/per-frame code
changed.

CI is local-gate mode (`GATE=local`); the evidence above is this session's own local full-gate run,
not a GitHub Actions result.

## Independent review

Builder: Claude (this session). Reviewer: Codex, fresh context, `--base origin/main` against the
committed candidate (`9e66a9a`) for both passes — no uncommitted changes at dispatch time, so both
cover the whole candidate.

- **General review: clean.** "No actionable defects found." Codex independently recomputed all ten
  committed bands from the raw CSVs and independently reran the release-preset ASan `Nesc.*` binary,
  confirming 8 passed / 2 skipped. Did not run a fresh build or the full gate itself.
- **Adversarial review: clean, verdict "approve".** Focus named ADR-0015 (and ADR-0013/0014), the
  `tol_abs`-only pass criterion, the `excluded()` blanket rule's exact scope, the new atmosphere
  floors' sourcing, `check_non_discriminating()`'s structural-tautology claim, and the diff's scope
  boundary. Codex's own words: "No material defects found... Read-only recomputation matched all case
  4/5 references and absolute tolerances to raw CSVs; previously excluded channels remain identical
  apart from tol_rel removal. All other exclusions and eight cases' numeric data are unchanged. All 30
  atmosphere floors are nonbinding (minimum margin 220,257×); all 210 bands have positive tolerances
  and cannot trigger the retained check. No production code changed."

Zero findings from either pass — recorded as a valid, evidence-backed outcome (not an unreviewed gap)
in `docs/reviews/nesc-tol-rel-review.md`. No fix rounds needed. Correction rounds: 0/2. Failed
integrations: 0/2.

## Joe's attention

**Two items you flagged on the regenerated diff, both addressed, neither changing any committed
value**:

1. **`case04`'s `rate_p` reconciliation.** It is genuinely floor-governed (`tol_abs = 1e-12` rad/s,
   generator's own `floor_ratio = 1.000`) and was already correctly predicted in the brief's full
   per-channel table — the acceptance table's own R7 *summary sentence* just didn't repeat it by name
   alongside `latitude`/`v_north`. Reconciled with a note in `docs/plans/nesc-tol-rel-removal.md`
   (verified against the actual regenerated output, all fifteen predicted case04/05 values, not just
   this one). No data changed; a documentation completeness gap in the brief's own condensed summary,
   not a defect in the generator or the golden files.
2. **Open finding, recorded not diagnosed: `case05`'s `altitude`/`v_down` misses under the R8 dry run
   are ~500-900x `case04`'s**, at the final sample (t=30 s): `altitude` -0.0147 m (`case04`) vs.
   -13.748 m (`case05`, ~936x); `v_down` +0.00152 m/s (`case04`) vs. +0.801 m/s (`case05`, ~527x).
   Both of `case05`'s misses would have passed undetected under the pre-ADR-0015, SIM-2-contaminated
   band (old `altitude` `tol_abs` 16.61 m, old `v_down` `tol_abs` 0.976 m/s, both well above the
   actual miss) — the same "hidden by an inflated tolerance" pattern this task's own `v_north`
   disclosure already established, now shown on two more channels, and specific to `case05` (the
   rotating-Earth case) rather than shared evenly with `case04`. Recorded with full evidence in
   `docs/plans/nesc-harness.md`'s new "Open finding" subsection and `PLAN.md`'s PR 3c bullet. No
   diagnosis or fix attempted here (PR 3c's own scope, per its own non-goal); **where in `PLAN.md`
   this should be tracked beyond what is already there is explicitly left to you.**

**R8 dry-run baseline, broader than previously disclosed**: with both PR 3c preconditions resolved,
the non-gating dry run shows only the three angular rates and three moments passing cleanly in *both*
cases against the tightened bands — `latitude`/`altitude`/`v_down`/`temperature` newly fail in both;
`density`/`pressure`/`force_y`/`force_z` fail in case05 only. Notably, `case04`'s own `force_x` now
*passes* (ratio 0.798) where the original disclosure grouped it with case05 as a failing channel;
case05's own force_x ratio (221.6) is consistent with that original figure's order of magnitude,
case04's is not. Full table in `docs/plans/nesc-harness.md`. This is PR 3c's starting point, not a
finding chased in this PR.

**Critical files to read, in end-to-end order**: `docs/decisions/0015-nesc-tol-rel-drop.md` (the
decision) → `tests/golden/nesc/generate_nesc_band.py` (the oracle, especially `excluded()` and
`FLOOR_ABS`) → `tests/golden/nesc/case04_dropped_sphere_round_non_rotation_band.hpp` (representative
of the largest regenerated diff) → `tests/physics/nesc/nesc_cases_test.cpp` (the one-line harness
change) → `docs/plans/nesc-harness.md`'s "R8 dry-run baseline for PR 3c" and "Open finding" sections.

**Assumptions/risks not independently re-derived this round**: frame/unit correctness, the yaw-wrap
logic, and every Table 77/78 exclusion outside cases 4/5 are unchanged from PR #31 and were not
re-verified from scratch (Codex's adversarial review did independently confirm they are unchanged
from base, which is the relevant claim for *this* diff).

**Rollback**: 1 commit on `worktree-m2-tol-rel`, no production/plugin code touched; closing PR #32
without merging, or a single `git revert` once merged, fully reverts `tol_rel`, the floor basis, and
the exclusion set to exactly what PR #31 left.

## Decision record

State: pending
Human decision/date/reference: pending
Approved candidate/scope: pending
Merge observed: not merged
Release authorization: pending / not requested

Significant tradeoff rationale: none — every change here is a narrowing (dropping slack), a
like-for-like floor conversion (same `1e-12` factor, different anchor), or an evidence-based
participant exclusion (never a widening), consistent with AGENTS.md's tolerance-narrowing-only rule.
Accepted risks: `case05`'s `altitude`/`v_down` open finding above — owner: `PLAN.md`'s M2 PR 3c (not
yet started); PLAN.md placement beyond the current entry is Joe's own decision, not made here.
Nonblocking follow-ups: none beyond the open finding above (already tracked, not blocking this
decision).
Architectural decisions: ADR-0015 (accepted, this candidate). PR 3c (not yet started) still needs its
own new/superseding ADR to ADR-0004 and `docs/architecture.md` §5 — not part of this candidate.
