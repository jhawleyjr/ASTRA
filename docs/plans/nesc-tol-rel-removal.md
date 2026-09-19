# Brief (spec + plan): NESC oracle — drop `tol_rel` (ADR-0015, elaborates ADR-0013/ADR-0014)

Status: draft, revised again per Joe's full-case-exclusion decision (2026-09-19), pending Joe's explicit
approval of this revision. No code, golden, or `docs/decisions/` writes have happened yet.
Owner: Joe (requirement owner); Claude plans/builds/validates
Risk: 3 — `tests/golden/**`, tolerance derivation (oracle-formula elaboration), `docs/decisions/**`
(`docs/process/workflow.md`#risk-tiers)
Intent/approval: Joe's task instruction, 2026-09-19 (this session), his Q1-Q3 decisions the same day, and
his further revision of Q2 to a full-case SIM 2 exclusion (quoted inline below where it changes the
design). Pending final approval of this revision.
Architecture/ADRs: ADR-0013 (accepted — ensemble band, `tol_abs`/`tol_rel` formula, `k = 3`); ADR-0014
(accepted — per-channel-kind floor, elaborates ADR-0013, does not supersede it); this brief's own
ADR-0015 draft below (elaborates both, supersedes neither).
System context: `tests/golden/nesc/generate_nesc_band.py` (human-owned, run by Joe, never by `just
check`) produces the ten committed `tests/golden/nesc/case{01..10}_*_band.hpp` files that
`tests/physics/nesc/nesc_cases_test.cpp` compares ASTRA's own trajectory against, per `ChannelBand`'s
schema in `tests/golden/nesc/nesc_band_common.hpp`. This task touches only that generator, that shared
schema, the ten generated files, the harness's comparison expression, and one new ADR — no plugin,
`core/`, or config code changes (PR 3c's own scope, explicitly out of bounds here).

## Outcome and boundaries

**Outcome**: the NESC oracle's pass criterion drops the `tol_rel * |reference|` term entirely, becoming
`|astra - reference| <= tol_abs` where `tol_abs = max(k * max_t(spread), floor)` (ADR-0013's `tol_abs`
term, unchanged in mechanism, floored per ADR-0014's table — see "Atmosphere floor" below for the one
place that table's own basis changes). `tol_abs` alone already bounds the ensemble's worst absolute
disagreement at every sample time; `tol_rel` only ever added slack on top of that bound, and — because
it is a single case-wide scalar — it added the most slack on exactly the channels where a relative term
is least meaningful. Dropping it cannot cause a false failure relative to the ensemble's own
disagreement, by construction.

**SIM 2 is excluded entirely from cases 4 and 5 — every channel, not a per-channel list.** Revised twice
this session: the original task instruction proposed excluding `v_north` alone (plus recording, not
comparing, `force_y`); this brief's first draft implemented that; Joe's Q2 review changed it to
excluding SIM 2 from `v_north`/`force_y`/`force_z` specifically and comparing all three normally; a
broader survey (below) then showed SIM 2 diverges on nearly every channel in these two cases, and Joe's
final decision is to exclude it entirely, once, for both cases. This is **ASTRA's own data-driven
exclusion, extending Table 78 footnote b's own reasoning** ("unexplained X-body force and **local
gravity differences**"), not a NASA footnote itself — footnote b names only X-body force; this session's
own computation against the raw CSVs is the evidence for the broader exclusion, labeled as such in
`excluded()`'s comments and the module docstring (the same standing as the existing t=0 airspeed
exclusion, "PLAN.md's own addition").

**Non-goals** (unchanged): no EOM/`Integrator`/`WrenchSource` changes (PR 3c, separate, unstarted). No
change to cases 4/5's `GTEST_SKIP()` disposition — this PR's regenerated bands are not run as part of
its own acceptance gate; a non-gating dry run (below) reports what they would show, for PR 3c's benefit,
without ungating this PR.

**Invariants preserved**: ADR-0013's `reference = mean across participating sims`,
`tol_abs = k * max_t(spread)`, `k = 3`, the 10 Hz no-interpolation grid, the generator's human-owned/
never-run-by-`just check` status, every existing Table 77/78 + t=0 exclusion outside cases 4/5, and
ADR-0014's floor values for angle/length/velocity/rate/force channels. `check_non_discriminating()` is
retained per the task instruction (rationale unchanged from the prior revision: every channel kind's
floor is now strictly positive once Q1's atmosphere conversion lands, making the check a permanent
structural-invariant guard rather than a live edge case).

## Why full-case exclusion, not per-channel (evidence, computed this session)

**The survey's own numbers argue for excluding the participant, not chasing channels one at a time.**
Computed against the raw CSVs (`k * max_t(max-min)`, respecting every already-accepted exclusion,
verified to reproduce every currently-committed `tol_abs` byte-for-byte before any of this task's own
changes):

**Case 04** (`old` = current committed, all 4 sims where not already excluded; `new` = SIM 2 excluded
entirely; floor = ADR-0014/Q1's per-channel-kind floor):

| Channel | Old `tol_abs` | New `tol_abs` | Floor | Floor-governed? |
|---|---|---|---|---|
| latitude | 4.0998e-08 rad | **1e-12 rad** | 1e-12 | yes (new) |
| longitude | 1e-12 rad | 1e-12 rad | 1e-12 | yes (already) |
| altitude | 16.560 m | **9.8212e-03 m** | 1e-9 | no |
| v_north | 9.1440e-03 m/s | **1e-9 m/s** | 1e-9 | yes (new) |
| v_east | 1e-9 m/s | 1e-9 m/s | 1e-9 | yes (already) |
| v_down | 0.97253 m/s | **1.1595e-03 m/s** | 1e-9 | no |
| yaw | (already SIM-2-excluded, Table 78 footnote a) | unchanged | — | — |
| pitch | 2.3214e-10 rad | 2.3214e-10 rad | 1e-12 | no (unchanged: already excluded) |
| roll | 3.3673e-10 rad | 3.3673e-10 rad | 1e-12 | no (unchanged: already excluded) |
| rate_p | 5.9905e-10 rad/s | **1e-12 rad/s** | 1e-12 | yes (new) |
| rate_q | 1.1981e-09 rad/s | **1.5046e-12 rad/s** | 1e-12 | no |
| rate_r | 1.2051e-09 rad/s | **2.2572e-12 rad/s** | 1e-12 | no |
| force_x | 9.0629e-04 N | 9.0629e-04 N | 1e-12 | no (unchanged: already excluded, footnote b) |
| force_y | 9.9322 N | **9.6220e-04 N** | 1e-12 | no |
| force_z | 5.8572 N | **1.2124e-03 N** | 1e-12 | no |
| moment_l/m/n | 1e-12 (each) | 1e-12 (each) | 1e-12 | yes (unchanged: already excluded) |
| density | 1.2798e-03 kg/m^3 | **2.9221e-05 kg/m^3** | 1.225e-12 | no |
| pressure | 116.21 Pa | **1.8527 Pa** | 1.01325e-7 | no |
| temperature | 0.10608 K | **6.3859e-05 K** | 2.8815e-10 | no |

**Case 05**: same shape (real Earth rotation keeps `longitude`, `v_east`, `rate_p` off their floors —
`longitude` 9.991e-09 -> 3.302e-12 rad, `v_east` 2.537e-03 -> 3.166e-06 m/s, `rate_p` 6.576e-10 ->
5.853e-11 rad/s; `latitude` and `v_north` still land on their floors, `1e-12` rad and `1e-9` m/s;
`altitude` 16.612 -> 9.7608e-03 m; `v_down` 0.97622 -> 1.1532e-03 m/s; `force_y` 9.8717 -> 9.5694e-04 N;
`force_z` 5.8206 -> 1.2078e-03 N; `density` 1.2842e-03 -> 2.9221e-05 kg/m^3; `pressure` 116.80 -> 1.8527
Pa; `temperature` 0.10643 -> 6.3467e-05 K). `force_x`, `pitch`, `roll`, and every moment stay
byte-identical in both cases (already SIM-2-excluded before this task).

**Confirmed**: exactly 3 participants (sims 4, 5, 6) remain for every channel at every one of the 301
sample times in both cases, well clear of the generator's own `< 2 contributing participants` assertion.

**Reference is biased too, not only `tol_abs`** — ADR-0013's `reference = mean across participating
sims`, so a divergent SIM 2 pulls the mean itself, not just the spread. Computed this session: `altitude`
at t=30 s shifts by **1.3797 m** (case04: 4948.683 m with SIM 2 -> 4947.303 m without) and **1.3841 m**
(case05); `v_down` at t=30 s shifts by **0.08101 m/s** (case04: 264.2124 -> 264.2934 m/s) and **0.08132
m/s** (case05). Both are close to the naive estimate Joe named (`spread / k / (participant count)`:
`16.56/3/4 ~= 1.38` m for altitude, `0.973/3/4 ~= 0.081` m/s for v_down) — consistent with one
four-participant mean being pulled by one outlier roughly in proportion to its own deviation divided by
the participant count.

**Sanity check**: case04's clean altitude band (`9.8212e-03` m) is the same order of magnitude as
case01's currently-committed, currently-passing altitude band (`1.4826e-03`... committed value
`1.9052e-03` m, `docs/plans/nesc-harness.md`'s own citation) — not an implausibly tight band relative to
what ASTRA already matches elsewhere in this same channel.

**Mechanism, offered as partial, not complete**: the force-components-vs-magnitude check from this
brief's prior revision (SIM 2's total aero-force magnitude tracks sims 4/5/6 to ~0.4-1% while its
components diverge up to ~20%) explains a wrong-attitude-driven *misprojection* of force alone — it does
not, by itself, explain SIM 2's divergent `altitude`/`v_down`/rates/position/atmosphere. The broader
trajectory divergence — visible across nearly every channel — is the actual cause this exclusion
addresses; it is consistent with, and likely the same root cause implied by, Table 78 footnote b's own
"local gravity differences" language (SIM 2 using a different or slightly wrong gravity model
specifically for the two round-Earth cases would plausibly perturb its whole trajectory: position,
velocity, attitude via the coupled dynamics, and hence the body-frame force projection too) — offered as
the likely explanation, not independently proven beyond the footnote's own wording and this survey's
own numbers.

## Contract and failure behavior

**`ChannelBand`** (`tests/golden/nesc/nesc_band_common.hpp`) drops its `tol_rel` field. Its remaining
`tol_abs` field keeps its existing meaning and units (ADR-0013/0014), unchanged in every case/channel
except every non-already-SIM-2-excluded channel in `case04`/`case05` (predicted table above).

**Pass criterion** (`tests/physics/nesc/nesc_cases_test.cpp::expect_in_band`): `|actual - reference| <=
tol_abs` (yaw still compared on the wrapped difference, unchanged). No channel/case computes or applies
a relative term anywhere in the harness or the generator. No exclusion parameter is added to
`run_nesc_case` — the participant is excluded in the generator, not the channel in the harness.

**Atmosphere channels' floor** (unchanged from the prior revision, Q1): density/pressure/temperature's
floor converts from ADR-0014's `1e-12` relative factor to an absolute one against each channel's ISA-76
sea-level value (density 1.225 kg/m^3, pressure 101325.0 Pa, temperature 288.15 K —
`tests/golden/atmosphere/generate_isa1976_table.py:50-51`, `isa1976_table.hpp:85,108`), giving
1.225e-12 kg/m^3, 1.01325e-7 Pa, 2.8815e-10 K. Verified never binding across all thirty currently-
committed atmosphere values (smallest ratio ~7.9e6, case02 temperature) and, per the table above,
non-binding for case04/05's own post-exclusion values too (smallest atmosphere ratio there is
`temperature`'s `6.39e-5 / 2.8815e-10 ~= 2.2e5`, still far clear). Speed of sound is not an emitted/
compared NESC channel (`CHANNELS`/`CaseBand` have no such entry) — ADR-0014's table lists it as a fourth
atmosphere-kind row; noted as a pre-existing, harmless inconsistency in that accepted table, not fixed.

**`check_non_discriminating()`**: unchanged rationale from the prior revision — drops the `tol_rel` term
from its `accepts_zero` expression; retained as a permanent structural-invariant check (every channel
kind's floor is now strictly positive, so the guard and `accepts_zero` are mutually exclusive by
construction), not because a live edge case remains.

**Implementation shape for the exclusion**: one new rule in `excluded()`:

```
if sim == 2 and case.num in (4, 5):
    return True  # this session's own data-driven finding: SIM 2 diverges broadly across nearly
                 # every channel in these two round-Earth cases (altitude, v_down, rates, position,
                 # atmosphere, and the aero force channels already known from footnote b) -- not a
                 # NASA footnote itself, but consistent with and likely the same root cause as Table
                 # 78 footnote b's own "local gravity differences" language for the already-excluded
                 # X-body force. Subsumes footnote b's own force_x-only rule for these two cases
                 # (citation kept below for cases outside 4/5, where footnote b does not apply).
```

placed before the existing footnote-a/d (Euler/moment, every case) and footnote-b (`force_x`, cases 4/5
only) rules; the footnote-b rule's own `case.num in (4, 5)` branch becomes unreachable dead logic once
this rule is added and should be deleted (its citation moves into the new rule's comment, per above),
while its Table-78-footnote-a/d neighbor stays for every *other* case.

## Acceptance

| ID | Observable requirement | Executable check |
|---|---|---|
| R1 | Every currently-passing case (8/10) still passes under all four presets once `tol_rel` is gone | `ctest --preset {asan,debug,release,static-plugins} -R 'physics.Nesc'` — 8 passed, 2 skipped, 0 failed, each preset. A newly-failing case is a real finding to report to Joe, not to tolerate or paper over. |
| R2 | Cases 01-03 and 06-10: every `tol_abs` and `reference` value byte-identical to PR #31's. Cases 04/05: every channel not already SIM-2-excluded (i.e. everything except `force_x`, `yaw`, `pitch`, `roll`, the three moments) changes in both `tol_abs` and `reference`, matching the predicted table above; `force_x`/`yaw`/`pitch`/`roll`/moments in 04/05 stay byte-identical | `git diff` on `tests/golden/nesc/case*_band.hpp` against `main` checked against the prediction table above, channel by channel, for cases 04/05; byte-identical diff (only `tol_rel` lines removed) for every other case |
| R3 | `force_y`/`force_z` (and every other channel) are compared normally in cases 4/5 against a fully-SIM-2-excluded band; no channel is excluded from harness comparison | `grep -n force_y tests/physics/nesc/nesc_cases_test.cpp` shows no exclusion parameter or conditional in `run_nesc_case`; `expect_in_band` is the only changed function in that file |
| R4 | `check_non_discriminating()` is retained; structural-invariant rationale documented | Generator run reports zero flagged channels across all ten cases |
| R5 | No `core/`/`plugins/`/`Eom`/`Integrator`/`WrenchSource` file touched; exclusion is exactly `sim == 2 and case.num in (4, 5)`, nothing narrower or broader | `git diff --stat` scope check; `excluded()`'s diff is one new rule plus deletion of the now-subsumed footnote-b branch |
| R6 | Full gate stays clean | `just check` (all 4 presets, format/tidy/docs/hooks/pyright/pytest, `ruff check`/`ruff format --check`) — `check: OK`, exit 0 |
| R7 | Generator's own floor-ratio report for cases 04/05 shows exactly the floor-governed channels predicted above (`latitude`, `v_north` newly; `longitude`, `v_east`, moments already floor-governed in case04; case05's real rotation keeps `longitude`/`v_east`/`rate_p` off their floors) and no other channel unexpectedly at its floor | Generator stderr, cases 04/05 lines, read by hand against the prediction table |
| R8 | A non-gating dry run establishes PR 3c's baseline: temporarily lift `GTEST_SKIP()` for cases 4/5, run once (one preset, e.g. `asan`), capture per-channel pass/fail and `max\|diff\| / tol_abs` for every sample, restore the skips, commit nothing from the dry run itself | `ctest --preset asan -R 'physics.Nesc.Case0[45]'` with the skips locally (uncommitted) removed; report table placed in the human-review packet and in `PLAN.md`'s PR 3c bullet as its stated baseline; if any channel besides the already-known lateral/attitude ones (`v_east`, `longitude`, `yaw`, `pitch`, `roll`, `force_x`) fails, that is reported plainly, not diagnosed or fixed in this PR |

Human-owned goldens/tolerances change in *mechanism* here (formula loses a term; floor basis converts;
one participant is excluded from two cases entirely on this session's own evidence) — exactly the kind
of change AGENTS.md requires Joe's explicit authorization for, requested by this brief. Every change is
a narrowing, a like-for-like floor conversion, or an evidence-based participant exclusion — never a
widening.

**Reconciliation, post-implementation (Joe's review of the regenerated diff)**: `case04`'s `rate_p`
is floor-governed (`tol_abs = 1e-12` rad/s, `floor_ratio = 1.000` in the generator's own stderr
report), but R7's own one-line summary above named only `latitude`/`v_north` as newly floor-governed
in `case04` and did not repeat `rate_p`. This is a completeness gap in that summary sentence, not a
computation error: the fuller per-channel prediction table in "Why full-case exclusion, not
per-channel" above (`| rate_p | 5.9905e-10 rad/s | 1e-12 rad/s | 1e-12 | yes (new) |`) already listed
`case04`'s `rate_p` as newly floor-governed, and the regenerated output matches that table exactly
(verified programmatically against all fifteen predicted values per case, `rate_p` included). R7's
condensed sentence should be read together with the fuller table, not in place of it.

## Repository findings

Observed, this session (file/symbol-cited):

- `tests/golden/nesc/nesc_band_common.hpp:29-37` — `ChannelBand::tol_rel`, the field to remove.
- `tests/golden/nesc/generate_nesc_band.py`: `FLOOR_REL` (:110-112, delete), `FLOOR_ABS`'s `atmosphere`
  entry (:102-109, replaced by three per-channel sea-level floors keyed by `channel.key`, since an
  absolute floor no longer scales uniformly across the shared `atmosphere` kind the way a relative one
  did), `omit_relative_term()` (:283-294, delete), the relative-spread block in `compute_channel()`
  (:344-355, delete), yaw's `result.tol_rel = 0.0` (:329, delete), `report()`'s atmosphere-branch
  `tol_rel`/`FLOOR_REL` use (:432-434, rewrite to use `tol_abs`/the new per-channel floor uniformly for
  every channel including atmosphere), `check_non_discriminating()`'s `accepts_zero` expression (:414,
  drop `tol_rel`), `render_case_header()`'s third literal field (:498, delete). `excluded()` (:259-273):
  one new rule (`sim == 2 and case.num in (4, 5)`), the existing footnote-b `force_x`/cases-4-5 branch
  deleted (subsumed), the footnote-a/d Euler/moment branch kept for every other case.
- `tests/physics/nesc/nesc_cases_test.cpp::expect_in_band` (:76-87): only line that changes in this file
  (drops the `tol_rel` term). `run_nesc_case` and the case04/case05 `TEST()` bodies are not touched.
- Ten `tests/golden/nesc/case{01..10}_*_band.hpp` files: generated, not hand-edited. `docs/refs/
  Atmospheric_checkcases/` (main checkout, gitignored) has 4 participant CSVs for cases 4/5 (sims 2, 4,
  5, 6); excluding SIM 2 entirely leaves exactly 3 for every channel at every sample time (confirmed
  this session, not merely asserted). This worktree has no `.venv/` — confirm the worktree's own Python
  setup before running the generator.
- `tests/golden/atmosphere/generate_isa1976_table.py:50-51`, `isa1976_table.hpp:85,108` — Q1's floor
  sources, unchanged from the prior revision.
- `docs/plans/nesc-harness.md`'s "Known gap" section and its "Follow-up recommended, not actioned this
  round" line need updating to record: full SIM 2 exclusion for cases 4/5 (not the earlier per-channel
  proposal), the reference-bias/shift numbers above, and R8's dry-run table once produced.
- `PLAN.md`'s PR 3c bullet needs updating: both original preconditions resolved via the full exclusion;
  add the dry-run table (R8) as PR 3c's stated baseline, and note the channels that dry run shows failing
  beyond the already-known lateral/attitude/`force_x` set, if any, without diagnosing them here.
- `docs/reviews/nesc-harness-human.md` — left untouched per Joe's Q3 decision.
- ADR-0014's Decision table — accepted, immutable, not edited; ADR-0015 records the atmosphere floor's
  converted basis and the pre-existing speed-of-sound inconsistency, disclosed only.

Assumption-vs-repository mismatches: none material beyond what Joe's decisions above already resolve.

## Mapping and sequence

| Requirement | Change / owned paths | Dependencies | Acceptance command |
|---|---|---|---|
| R1, R2 | `generate_nesc_band.py`: remove `FLOOR_REL`, `omit_relative_term()`, the relative-spread block, yaw's `tol_rel` assignment; replace atmosphere's shared floor with three per-channel sea-level floors; replace `excluded()`'s footnote-b branch with the blanket `sim==2, case in (4,5)` rule; update `report()`/docstring | None | Generator run, stderr inspected by hand |
| R1, R2 | `nesc_band_common.hpp`: remove `ChannelBand::tol_rel` | Lands with the generator change | Compiles once headers regenerate |
| R2, R7 | Regenerate all ten `case{01..10}_*_band.hpp`, `clang-format-18 -i` | Needs `docs/refs/` (main checkout), `touch-goldens` armed | `git diff` checked against the predicted table above, channel by channel |
| R1, R3 | `nesc_cases_test.cpp`: `expect_in_band` drops the `tol_rel` term; no other change | Needs regenerated headers | `ctest --preset asan -R 'physics.Nesc'`, then all four presets |
| R4 | `check_non_discriminating()`: drop `tol_rel`; docstring restates the structural-invariant rationale | Same generator change | Generator stderr shows zero flags |
| R8 | Non-gating dry run: locally remove cases 4/5's `GTEST_SKIP()`, run once, capture per-channel `max\|diff\|/tol_abs`, restore the skips, discard the local edit (never committed) | Needs R2's regenerated bands | `ctest --preset asan -R 'physics.Nesc.Case0[45]'`, output captured to the report table, skips restored before any commit |
| R6 | `docs/decisions/0015-*.md` written (draft below, filed after approval); `docs/plans/nesc-harness.md`/`PLAN.md` updated, including R8's dry-run table | Needs Joe's explicit approval before the ADR file is written | `just check` |
| R6 | Full gate | All of the above landed | `just check`, all 4 presets, `check: OK` |

One Claude builder, single package.

## Validation and risks

- **Primary risk**: dropping `tol_rel` could flip a currently-passing case to failing. Checked by
  running all four presets, not inferred. A failure is reported, not re-tolerated.
- **R8's dry run is deliberately non-gating**: cases 4/5 stay `GTEST_SKIP()`ed in the committed
  candidate; the dry run exists only to hand PR 3c a concrete, current baseline (which channels already
  pass against the tightened band, which still fail and by how much) and is explicitly not something
  this PR's own acceptance depends on passing.
- **Rollback**: revert this PR's commits; `tol_rel`, the floor basis, and the exclusion set return
  exactly as PR #31 left them.
- **Compatibility**: no interface change — `ChannelBand` is internal test-only data.
- **New dependency / toolchain**: none.
- **Human-owned files touched**: `tests/golden/nesc/**` and `docs/decisions/0015-*.md`, both guarded;
  `touch-goldens` already armed this session; Joe's explicit authorization for the ADR file requested by
  this brief.

## Plan critique and decisions

No separate Codex plan critique dispatched — this remains a strict simplification/tightening of an
already-accepted formula, with every underlying claim independently verified against the raw CSVs this
session (reproducing Joe's own numbers, then extending the survey and the reference-shift computation on
his instruction). `/review-cycle`'s ordinary post-implementation independent review still applies before
the human-review packet.

## Draft ADR-0015 (to be filed as `docs/decisions/0015-nesc-oracle-drop-tol-rel.md` on approval)

> # ADR-0015: Drop `tol_rel` from the NESC oracle's pass criterion, and exclude SIM 2 entirely from
> cases 4 and 5
>
> Status: proposed
> Date: 2026-09-19
> Owner: Joe
> Related/supersedes: elaborates ADR-0013 (does not supersede it) and, for atmosphere channels' floor
> basis, ADR-0014 (does not edit or supersede its Decision table). Governs a small PR ahead of
> `PLAN.md`'s M2 PR 3c.
>
> ## Context
>
> ADR-0013's pass criterion is `|astra - reference| <= tol_abs + tol_rel * |reference|`. `tol_abs`
> already bounds the ensemble's worst absolute disagreement at every sample time (ADR-0014's floor
> included); `tol_rel`, a single case-wide scalar (`k` times the *worst* per-sample relative spread over
> the whole run), only ever adds slack on top of that bound, and adds the most slack on channels whose
> reference passes near zero at some point in the run. PR #31's packet measured this on four channels
> (`case02` `rate_p` 95%, `case03` `roll` 48%, `case09` `v_down` 60%, `case04`/`case05` `force_z`/
> `force_y` 72%/76%); Codex's adversarial review of PR #31 additionally found `force_y` in cases 4/5
> genuinely non-discriminating under the old formula.
>
> **This session's own recomputation against the raw CSVs found the `force_y`/`force_z` looseness was
> never an ordinary zero-crossing property of the pass criterion — it is SIM 2 contaminating the
> ensemble itself, and the contamination is not limited to those two channels.** Computed this session
> (`k * max_t(max-min)`, with vs. without SIM 2, respecting every already-accepted exclusion, method
> verified to reproduce every currently-committed `tol_abs` exactly): in cases 4 and 5 specifically, SIM
> 2 dominates the spread of nearly every compared channel — `altitude` (ratio ~1686/1702), `v_down`
> (~839/847), all three angular rates (~500-800, except case05's `rate_p` at ~11), `latitude`
> (effectively unbounded, ~3e12), `force_y`/`force_z` (~4800-10300, matching the packet's own numbers),
> and `density`/`pressure`/`temperature` (~44-1700) — not only the already-excluded `force_x` and
> Euler/moment channels. A control check across the other eight cases, same method, shows SIM 2
> contaminating no channel by more than ~10x there (one exception, cases 07/08 `force_x`, is
> floor-dominated noise at ~1.6e-6 N, not a real concern) — this is specific to cases 4 and 5, the two
> round-Earth cases, and consistent with Table 78 footnote b's own language for the already-excluded
> `force_x`: "unexplained X-body force **and local gravity differences**."
>
> **The mechanism is partially understood, not fully.** SIM 2's total aero-force *magnitude* tracks the
> other three participants (sims 4/5/6) to within ~0.4-1% at every sampled time in case04, while its
> individual body-axis *components* diverge by up to ~20% — consistent with a wrong-attitude-driven
> misprojection (Table 78 footnote a's own "large integration errors"). That explains the force
> channels' divergence, but not `altitude`, `v_down`, the rates, position, or the atmosphere channels,
> which do not derive from a force-vector projection. The broader trajectory divergence across nearly
> every channel is the actual, broader cause this exclusion addresses; footnote b's own "local gravity
> differences" wording is the likely explanation (a different or slightly wrong gravity model for SIM
> 2's round-Earth handling would plausibly perturb its whole trajectory, not only its aero-force
> projection), offered here as the probable mechanism, not independently proven beyond the footnote's
> own text and this survey's numbers.
>
> **`reference` is biased by SIM 2 too, not only `tol_abs`** — ADR-0013 defines `reference` as the mean
> across participating sims, so a divergent SIM 2 pulls the mean itself. Computed this session:
> `altitude`'s reference at t=30 s shifts by 1.38 m (case04) / 1.38 m (case05) once SIM 2 is excluded;
> `v_down`'s shifts by 0.081 m/s in both cases — both close to the naive `spread / k / 4` estimate,
> consistent with a four-participant mean pulled by one outlier roughly in proportion to its own
> deviation.
>
> ## Decision
>
> **Drop `tol_rel` entirely from the pass criterion**, unchanged from this ADR's own earlier draft:
> `ChannelBand` loses its `tol_rel` field; the pass criterion becomes `|astra - reference| <= tol_abs`.
>
> **Exclude SIM 2 entirely from cases 4 and 5 — every channel, one rule, not a per-channel list.**
> `excluded()` gains `if sim == 2 and case.num in (4, 5): return True`, applied before the existing
> footnote-a/d (Euler/moment, every case) and superseding the existing footnote-b (`force_x`, cases 4/5
> only) rule, which becomes redundant and is deleted (its citation preserved in the new rule's comment).
> This affects both `reference` (recomputed over the remaining 3 participants) and `tol_abs` for every
> channel not already excluded by Table 78 in these two cases. Predicted values, computed this session
> and to be checked against the regenerated output: `altitude` 16.56 -> 0.0098 m (case04), 16.61 -> 0.0098
> m (case05); `v_down` 0.973 -> 0.0012 m/s / 0.976 -> 0.0012 m/s; `v_north`/`latitude` land on their
> ADR-0014 floors (`1e-9` m/s / `1e-12` rad); `force_y`/`force_z` drop to ~1 mN from ~6-10 N;
> `density`/`pressure`/`temperature` tighten by 40-1700x. `force_x`, `yaw`, `pitch`, `roll`, and every
> moment stay byte-identical (already excluded before this ADR).
>
> **Atmosphere channels (density, pressure, temperature) keep a floor, converted from relative to
> absolute**, unchanged from this ADR's own earlier draft: ADR-0014's `1e-12` factor now multiplies each
> channel's ISA-76 sea-level value (density 1.225 kg/m^3, pressure 101325.0 Pa, temperature 288.15 K),
> giving 1.225e-12 kg/m^3, 1.01325e-7 Pa, 2.8815e-10 K — verified never binding, including against
> cases 4/5's own post-exclusion values (smallest margin there ~2.2e5x). Speed of sound is not an
> emitted NESC channel at all, despite ADR-0014's table listing it — a pre-existing inconsistency,
> disclosed, not fixed.
>
> **`check_non_discriminating()` is retained even though it becomes a structural tautology**, unchanged
> rationale from this ADR's own earlier draft: every channel kind's floor is now strictly positive, so
> `tol_abs > 0` is a structural guarantee, and the function's guard and its `accepts_zero` check become
> mutually exclusive by construction. Kept as a permanent invariant check regardless.
>
> ## Rejected alternatives
>
> - **Keep `tol_rel` but tighten `omit_relative_term`'s guard further**, or **keep `tol_rel` only for
>   channels `check_non_discriminating()` does not flag.** Both rejected as in this ADR's earlier draft:
>   still leave several channels loose, or reintroduce per-channel judgment-number proliferation.
> - **Exclude `force_y`(/`force_z`) from harness comparison instead of excluding the contaminated
>   participant.** Rejected (Joe's Q2 review of this ADR's first draft): discards exactly the channels
>   most sensitive to PR 3c's own frozen-wrench defect, and ADR-0014 already rejected dropping a channel
>   from comparison entirely for the identical reason.
> - **Exclude SIM 2 per channel (`v_north`, `force_y`, `force_z` only), not the whole case.** This ADR's
>   own second draft did exactly this; rejected once the broader survey showed the same contamination on
>   nearly every other channel. A per-channel exclusion list produces **mutually inconsistent
>   references within one case**: `force_y`'s reference would be sims 4/5/6's mean while `altitude`'s
>   stayed a four-sim blend including SIM 2's divergent trajectory — two different, incompatible notions
>   of "the ensemble's trajectory" inside the same `CaseBand`. A single participant-level exclusion keeps
>   one self-consistent reference trajectory for the whole case, which is what `reference` is supposed to
>   mean.
>
> ## Consequences and validation
>
> - `nesc_band_common.hpp`, `generate_nesc_band.py`, all ten `case*_band.hpp` files, and
>   `nesc_cases_test.cpp` change together; cases 01-03/06-10 are byte-identical except for the removed
>   `tol_rel` field; cases 04/05 change on every non-already-excluded channel, per the predicted table.
> - `PLAN.md`'s PR 3c bullet loses both of its originally-named preconditions and inherits a
>   self-consistent, ~3-participant band for every channel in cases 4/5, plus a non-gating dry-run
>   baseline table (this task's R8) showing which channels already pass against it.
> - Acceptance: `ctest` all four presets, `physics.Nesc` filter, 8 passed / 2 skipped / 0 failed,
>   unchanged from PR #31; `just check` clean. Any case that newly fails is reported, not tolerated.
>
> ## Approval
>
> Pending. Drafted with this brief; to be accepted, and this file written to `docs/decisions/`, only
> after Joe's explicit read and approval.

## Questions and decision

| # | Question | Answer/reference |
|---|---|---|
| Q1 | Atmosphere channels' floor | **Answered (Joe, 2026-09-19), unchanged this revision**: absolute floor converted from ADR-0014's `1e-12` factor applied to each channel's ISA-76 sea-level value. Verified never binding, including for cases 4/5 post-exclusion. |
| Q2 | SIM 2 / cases 4-5 exclusion scope | **Answered, revised twice (Joe, 2026-09-19)**: final decision is full-case exclusion (every channel), not the original `v_north`-only instruction nor the intermediate `v_north`/`force_y`/`force_z`-only revision. Evidence: the broader survey above; rejected as a per-channel design because it would produce mutually inconsistent references within one `CaseBand`. |
| Q3 | `docs/reviews/nesc-harness-human.md` | **Answered (Joe, 2026-09-19), unchanged**: leave untouched; ADR-0015 cites its line, does not edit it. |

Joe's spec/plan approval: **pending** — this revised brief and ADR-0015 draft, including R7/R8's new
acceptance items, await Joe's explicit approval before any code, golden, or `docs/decisions/` write
happens.
