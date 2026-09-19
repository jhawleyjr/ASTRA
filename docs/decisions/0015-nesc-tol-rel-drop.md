# ADR-0015: NESC oracle — drop `tol_rel`, convert atmosphere floors to absolute, exclude SIM 2 from
cases 4 and 5

Status: **accepted**, 2026-09-19
Owner: Joe
Related/supersedes: elaborates ADR-0013 (does not supersede it) and, for atmosphere channels' floor
basis, ADR-0014 (does not edit or supersede its Decision table). Governs a small PR ahead of
`PLAN.md`'s M2 PR 3c. Drafted with `docs/plans/nesc-tol-rel-removal.md`, revised twice during Joe's
review (Q1/Q2/Q3, then a further Q2 revision to a full-case SIM 2 exclusion) and accepted with this
final scope.

## Context

ADR-0013's pass criterion is `|astra - reference| <= tol_abs + tol_rel * |reference|`. `tol_abs` already
bounds the ensemble's worst absolute disagreement at every sample time (ADR-0014's floor included);
`tol_rel`, a single case-wide scalar (`k` times the *worst* per-sample relative spread over the whole
run), only ever adds slack on top of that bound, and adds the most slack on channels whose reference
passes near zero at some point in the run. PR #31's packet measured this on four channels (`case02`
`rate_p` 95%, `case03` `roll` 48%, `case09` `v_down` 60%, `case04`/`case05` `force_z`/`force_y` 72%/76%);
Codex's adversarial review of PR #31 additionally found `force_y` in cases 4/5 genuinely
non-discriminating under the old formula.

**This session's own recomputation against the raw CSVs found the `force_y`/`force_z` looseness was
never an ordinary zero-crossing property of the pass criterion — it is SIM 2 contaminating the ensemble
itself, and the contamination is not limited to those two channels.** Computed this session
(`k * max_t(max-min)`, with vs. without SIM 2, respecting every already-accepted exclusion, method
verified to reproduce every currently-committed `tol_abs` exactly): in cases 4 and 5 specifically, SIM 2
dominates the spread of nearly every compared channel — `altitude` (ratio ~1686/1702), `v_down`
(~839/847), all three angular rates (~500-800, except case05's `rate_p` at ~11), `latitude` (effectively
unbounded, ~3e12), `force_y`/`force_z` (~4800-10300, matching the packet's own numbers), and
`density`/`pressure`/`temperature` (~44-1700) — not only the already-excluded `force_x` and Euler/moment
channels. A control check across the other eight cases, same method, shows SIM 2 contaminating no
channel by more than ~10x there (one exception, cases 07/08 `force_x`, is floor-dominated noise at
~1.6e-6 N, not a real concern) — this is specific to cases 4 and 5, the two round-Earth cases, and
consistent with Table 78 footnote b's own language for the already-excluded `force_x`: "unexplained
X-body force **and local gravity differences**."

**The mechanism is partially understood, not fully.** SIM 2's total aero-force *magnitude* tracks the
other three participants (sims 4/5/6) to within ~0.4-1% at every sampled time in case04, while its
individual body-axis *components* diverge by up to ~20% — consistent with a wrong-attitude-driven
misprojection (Table 78 footnote a's own "large integration errors"). That explains the force channels'
divergence, but not `altitude`, `v_down`, the rates, position, or the atmosphere channels, which do not
derive from a force-vector projection. The broader trajectory divergence across nearly every channel is
the actual, broader cause this exclusion addresses; footnote b's own "local gravity differences" wording
is the likely explanation (a different or slightly wrong gravity model for SIM 2's round-Earth handling
would plausibly perturb its whole trajectory, not only its aero-force projection), offered here as the
probable mechanism, not independently proven beyond the footnote's own text and this survey's numbers.

**`reference` is biased by SIM 2 too, not only `tol_abs`** — ADR-0013 defines `reference` as the mean
across participating sims, so a divergent SIM 2 pulls the mean itself. Computed this session:
`altitude`'s reference at t=30 s shifts by 1.38 m (case04) / 1.38 m (case05) once SIM 2 is excluded;
`v_down`'s shifts by 0.081 m/s in both cases — both close to the naive `spread / k / 4` estimate,
consistent with a four-participant mean pulled by one outlier roughly in proportion to its own deviation.

## Decision

**Drop `tol_rel` entirely from the pass criterion.** `ChannelBand` loses its `tol_rel` field; the pass
criterion becomes `|astra - reference| <= tol_abs`. This cannot cause a false failure relative to the
ensemble's own disagreement: `tol_abs` alone already equals or exceeds `k` times the worst absolute
spread the six-tool ensemble itself exhibits at every sample time; the relative term never tightened the
bound, only loosened it.

**Exclude SIM 2 entirely from cases 4 and 5 — every channel, one rule, not a per-channel list.**
`excluded()` gains `if sim == 2 and case.num in (4, 5): return True`, applied before the existing
footnote-a/d (Euler/moment, every case) rule and superseding the existing footnote-b (`force_x`, cases
4/5 only) rule, which becomes redundant and is deleted (its citation preserved in the new rule's
comment). This affects both `reference` (recomputed over the remaining 3 participants) and `tol_abs` for
every channel not already excluded by Table 78 in these two cases. Predicted values, computed this
session and checked against the regenerated output before this ADR was accepted: `altitude` 16.56 ->
0.0098 m (case04), 16.61 -> 0.0098 m (case05); `v_down` 0.973 -> 0.0012 m/s / 0.976 -> 0.0012 m/s;
`v_north`/`latitude` land on their ADR-0014 floors (`1e-9` m/s / `1e-12` rad); `force_y`/`force_z` drop
to ~1 mN from ~6-10 N; `density`/`pressure`/`temperature` tighten by 40-1700x. `force_x`, `yaw`, `pitch`,
`roll`, and every moment stay byte-identical (already excluded before this ADR).

**Atmosphere channels (density, pressure, temperature) keep a floor, converted from relative to
absolute.** ADR-0014's `1e-12` factor now multiplies each channel's ISA-76 sea-level value (density
1.225 kg/m^3, pressure 101325.0 Pa, temperature 288.15 K — `tests/golden/atmosphere/
generate_isa1976_table.py:50-51`'s `SEA_LEVEL_TEMPERATURE_K`/`SEA_LEVEL_PRESSURE_PA`, and
`isa1976_table.hpp:85,108`'s transcribed/freshly-computed sea-level density), giving 1.225e-12 kg/m^3,
1.01325e-7 Pa, 2.8815e-10 K. Verified never binding across all thirty currently-committed atmosphere
values (smallest ratio ~7.9e6, case02 temperature) and against cases 4/5's own post-exclusion values
(smallest margin there ~2.2e5x). Speed of sound is not an emitted NESC channel at all
(`generate_nesc_band.py`'s `CHANNELS` and `nesc_band_common.hpp`'s `CaseBand` have no such entry),
despite ADR-0014's table listing it as a fourth atmosphere-kind row — a pre-existing inconsistency in
that accepted (immutable) table, disclosed here, not fixed there.

**`check_non_discriminating()` is retained even though it becomes a structural tautology.** With every
channel kind's floor now strictly positive (this ADR's atmosphere conversion removes the last `0.0`
floor), `tol_abs > 0` is a structural guarantee, not merely a fact about today's data — so the function's
guard (`peak > 2*tol_abs`) and its `accepts_zero` check (`abs(v) <= tol_abs` for every sample) are
mutually exclusive by construction. Kept anyway as a permanent structural-invariant check: it
continuously verifies that guarantee against whatever the generator actually computes and would
immediately flag a future change (a reintroduced relative term, a floor lowered to zero) that broke it.

## Rejected alternatives

- **Keep `tol_rel` but tighten `omit_relative_term`'s guard further.** Already tried once (the guard
  compares a sample's `|mean|` against `tol_abs`, generalizing the yaw-wrap fix) and still left four
  channels 48-95% loose. A guard narrow enough to prevent all of these would need per-channel, possibly
  per-case tuning — reintroducing exactly the kind of judgment-number proliferation ADR-0013 rejected a
  global tolerance for.
- **Keep `tol_rel` only for channels `check_non_discriminating()` does not flag.** Rejected: still
  carries 48-76% slack on several channels for no discriminating benefit, and makes the oracle's own
  formula depend on a per-channel exception list re-derived by hand after every regeneration.
- **Exclude `force_y`(/`force_z`) from harness comparison instead of excluding the contaminated
  participant.** Rejected on review: discards exactly the channels most sensitive to PR 3c's own
  frozen-wrench defect, and ADR-0014 already rejected dropping a channel from comparison entirely for
  the identical reason ("exactly where a sign error ... is easiest to miss if nothing checks it at
  all").
- **Exclude SIM 2 per channel (`v_north`, `force_y`, `force_z` only), not the whole case.** This ADR's
  own working draft did exactly this; rejected once a broader survey showed the same contamination on
  nearly every other channel. A per-channel exclusion list produces mutually inconsistent references
  within one case: `force_y`'s reference would be sims 4/5/6's mean while `altitude`'s stayed a four-sim
  blend including SIM 2's divergent trajectory — two different, incompatible notions of "the ensemble's
  trajectory" inside the same `CaseBand`. A single participant-level exclusion keeps one self-consistent
  reference trajectory for the whole case, which is what `reference` is supposed to mean.
- **No replacement floor for atmosphere channels once `tol_rel` is gone.** Considered in an earlier
  draft of this ADR; rejected by Joe — ADR-0014 already rejected "no floor" once, and a `0.0` `FLOOR_ABS`
  entry is exactly the edge case `check_non_discriminating()` exists to guard against. Converting the
  existing `1e-12` factor to an absolute floor against each channel's own ISA-76 sea-level value keeps a
  floor without inventing a new judgment number.

## Consequences and validation

- `nesc_band_common.hpp`, `generate_nesc_band.py`, all ten `case*_band.hpp` files, and
  `nesc_cases_test.cpp` change together; cases 01-03/06-10 are byte-identical except for the removed
  `tol_rel` field; cases 04/05 change on every non-already-excluded channel, per the predicted values
  above, checked against the actual regenerated diff before this ADR's acceptance was finalized.
- `PLAN.md`'s PR 3c bullet loses both of its originally-named preconditions and inherits a
  self-consistent, 3-participant band for every channel in cases 4/5, plus a non-gating dry-run baseline
  table (this task's own R8) showing which channels already pass against it and which still fail.
- A broader SIM 2 contamination beyond the excluded case was surveyed across all ten cases as part of
  this decision (not a separate, unactioned finding): the exclusion is total for cases 4/5 precisely
  because the survey showed contamination on nearly every channel there, and specific to those two cases
  because the same method found no comparable contamination elsewhere.
- Acceptance: `ctest` all four presets, `physics.Nesc` filter, 8 passed / 2 skipped / 0 failed,
  unchanged from PR #31; `just check` clean. Any case that newly fails is reported, not tolerated.

## Approval

Accepted, 2026-09-19. Drafted with `docs/plans/nesc-tol-rel-removal.md` across three review rounds (Q1
atmosphere floor, Q2 SIM 2 exclusion scope revised from a single channel to three channels to the full
case, Q3 packet-editing question); Joe explicitly approved the brief and this ADR's final scope the same
day, authorizing this file's own write under `touch-goldens`.
