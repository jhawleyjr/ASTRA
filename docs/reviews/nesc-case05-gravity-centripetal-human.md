# Human review packet: `gravity/inverse_square` centripetal-term fix (case05 gravity anomaly)

PR / candidate base and head: base `41d2c1a` (`main`'s current tip, confirmed via `git log -1` in this
worktree, no upstream advance since); head is an **uncommitted working-tree diff** in
`.claude/worktrees/m2-case05-gravity` — no commit or PR created yet, per the task's own scope (a
worktree-local fix, not yet pushed).
Spec/plan/task: `docs/plans/nesc-case05-gravity-centripetal.md` (approved by Joe, 2026-09-19, including
D1's resolution — a `rotating` bool config key mirroring `earth/round`'s own, reading
`astra::geodesy::kOmegaIe` — and D2's decision, option (a), disclosed only). Originating `PLAN.md` item:
"Investigate `case05`'s `altitude`/`v_down` anomaly" (now checked off, see below).
Risk: 3 — `plugins/physical/gravity/inverse_square` feeds every EOM step (`Eom::operator()` via
`gravity_.g_n(vs)`); consumed by the human-owned NESC oracle comparison in `tests/physics/nesc/**`
(no golden file itself touched).
Decision requested: **approve behavior/merge** of this working-tree diff as a task-branch candidate
(commit + draft PR), or request changes.

## What changes and why

**Before**: `gravity/inverse_square` returned pure Newtonian gravitation only —
`g_n = [0, 0, g0*(radius/(radius+h))^2]` — regardless of whether the paired Earth model rotates. This
violated `gravity_model.hpp`'s own accepted contract ("g_n is plumb-bob gravity ... so the EOM carries no
separate centripetal term"), already correctly met by `gravity/wgs84_j2`. NESC check-case 5 (`earth/round
rotating: true` + `gravity/inverse_square`) had no way for Earth's rotation to reach gravity, producing a
missing-force bias: ASTRA's simulated sphere fell faster than NASA's six-tool ensemble reference by
`-13.748 m` (altitude) / `+0.801 m/s` (`v_down`) at t=30 s — three orders of magnitude larger than
case04's own (non-rotating, otherwise identical) `-0.0147 m` / `+0.0015 m/s` miss.

**After**: `gravity/inverse_square` takes a new `rotating` config key (bool, default `false`, mirroring
`earth/round`'s own key and its unconditional use of `astra::geodesy::kOmegaIe`); when true, `g_n()` adds
the centripetal correction `omega^2 * (radius+h) * [-sin(lat)*cos(lat), 0, -cos(lat)^2]` (NED), derived
from and cross-checked against two independent textbook sources read directly this session
(`docs/refs/`, main checkout): Stevens/Lewis/Johnson eq. 1.6-27 (p.33) and Groves eq. 2.133 second line
(p.70), which agree with each other and with `eom.cpp`'s own matching term (eq. 1.7-16b) to the last
sign. Case05's config now sets `rotating: true`; case04's sets it `false` (explicit, matching its
`earth.rotating: false`). At the default (`rotating` unset/false), the added term is exactly `0.0`
(bit-identical to pre-fix, verified — see Evidence).

**Diff summary** (`git diff --stat`, base `41d2c1a`): 6 files, +293/-26.

| File | Change |
|---|---|
| `plugins/physical/gravity/inverse_square/inverse_square.cpp` | The fix itself: `rotating` config key, `omega_` member, extended `g_n()`, rewritten header comment with both citations |
| `tests/unit/gravity/inverse_square_test.cpp` | 5 new tests: independent ECEF-oracle cross-check, hand-computed literal, radius-scaling check, longitude-independence, bit-cast regression for the default path |
| `tests/physics/nesc/case04_dropped_sphere_round_non_rotation.yaml` | +1 line: explicit `physical.gravity.rotating: false` |
| `tests/physics/nesc/case05_dropped_sphere_round_rotation.yaml` | +1 line: `physical.gravity.rotating: true` |
| `PLAN.md` | Checks off the originating item with the finding/fix/evidence; updates PR 3c's own stated baseline for the 5 channels this fix changes; adds a new unchecked follow-up item for D2(b) |
| `docs/plans/nesc-harness.md` | Append-only "Resolved" note under its own prior "Open finding" section |

**Not touched**: `tests/golden/**`, `docs/decisions/**`, `core/`, `Eom`, `Integrator`, `WrenchSource`,
`wgs84_j2`, `wgs84_rotating`, `earth/round`'s own kinematics. `touch-goldens` was never armed or needed.
Cases 4/5 remain `GTEST_SKIP()`ed in `tests/physics/nesc/nesc_cases_test.cpp` — unchanged, 0 net diff on
that file (confirmed by `git diff --stat` after the temporary local dry run below was reverted). The
frozen-wrench characteristic (PR 3c's own separate, not-yet-started scope) is untouched by this fix and
remains present, equally, in both cases.

**Unexpected scope**: none beyond the plan's own stated shape. The new unchecked `PLAN.md` follow-up item
(D2(b), an init-time earth/gravity rotation-consistency check) is new tracking, not new code — explicitly
deferred by Joe (D2(a)) for this candidate.

## Evidence

| Check / acceptance | Result / date / duration | Log or retained artifact / candidate |
|---|---|---|
| `ctest --preset asan -R 'InverseSquareGravity'` | 8/8 pass (2026-09-19) | New R2/R3 unit tests; caught and fixed one real test-helper bug along the way (`std::to_string(double)`'s 6-decimal truncation silently corrupted `g0`'s precision by exactly the truncated amount — fixed with a `max_digits10` formatter). **This fix also changed the pre-existing `load()` fixture helper** (`tests/unit/gravity/inverse_square_test.cpp`, used by the three original `InDomain*`/`ValidatePn*` tests): it now calls the same `precise()` formatter instead of `std::to_string(radius)`. Their pass/fail results and tolerances are unchanged (`radius = 1000.0` round-trips identically under both formatters), but the shared helper's own source did change — "existing tests pass unmodified" below means existing test *expectations* are unchanged, not that the test file itself was untouched. |
| `ctest --preset {asan,debug,release,static-plugins} -R 'InverseSquareGravity\|RoundEarthDrop\|SimulationPhysicalConfig\|SimulationHalt'` | 47/47 pass, every preset (2026-09-19) | Confirms bit-identity of the default (non-rotating) path across every existing caller; existing test expectations unchanged, one shared fixture helper fixed (previous row), including M2 PR 1's own `RoundEarthDrop.MatchesTheInverseSquareClosedForm` closed-form accept test |
| `ctest --preset {asan,debug,release,static-plugins} -R 'physics.Nesc'` | 8 passed / 2 skipped / 0 failed, every preset (2026-09-19) | Cases 1-3/6-10 unaffected (different gravity plugin); cases 4/5 stay skipped |
| Skip-lifted local dry run, `ctest --preset asan -R 'physics.Nesc.Case0[45]'` with `GTEST_SKIP()` locally removed, then restored (2026-09-19) | See table below | `git diff --stat` on `nesc_cases_test.cpp` = empty after restoration, confirmed |
| `./scripts/check-full.sh` (`just check`) | `check: OK`, exit 0 (2026-09-19, run twice) | First run failed `format-check` on the new test file (recorded honestly, not glossed over); fixed with `clang-format-18 -i`, reran clean: all 4 presets, 288/288 tests, pyright 0 errors, pytest 32/32 |
| `python3 scripts/docs_check.py` | `docs-check: OK (24 Markdown files checked)` (2026-09-19, after the doc/PLAN.md edits following review) | Confirms the post-review documentation corrections didn't break doc-link/consistency checks |

**Numerical evidence — the actual measurement, not a pass/fail label** (t=30 s / sample 300, asan
preset):

| Channel | case04 (unchanged) | case05 pre-fix | case05 post-fix |
|---|---|---|---|
| altitude diff | -0.014685 m | -13.748270 m | **-0.014545 m** |
| v_down diff | +0.0015207 m/s | +0.800743 m/s | **+0.0015042 m/s** |
| density | ratio 0.114 (pass) | ratio 37.45 (FAIL) | **0 failures logged (pass)** |
| pressure | ratio 0.157 (pass) | ratio 53.57 (FAIL) | **0 failures logged (pass)** |
| temperature | ratio 1.49 (FAIL) | ratio 1405.84 (FAIL) | **ratio 1.488 (FAIL, matches case04)** |

Case05's altitude/v_down/temperature now land at case04's own pre-existing (frozen-wrench, PR 3c's own
scope, unrelated to this fix) miss level — within ~1% of case04's own numbers, not merely closer.
**Correction, this session**: an earlier draft of this evidence claimed the two cases' numbers "agree to
4 significant figures" — Codex's independent adversarial-review recomputation showed this was an
overclaim (the actual agreement is ~1% relative, not 4-significant-figure precision); fixed in this
packet and its source documents before this review was finalized. The substantive result — same order of
magnitude as case04, not three orders of magnitude away — is correct and unaffected. Every other compared
channel improved or stayed the same in case05 (full table: `docs/plans/nesc-case05-gravity-centripetal.md`'s
own R4/R8 section); none regressed.

Toolchain: Ubuntu 24.04, clang-18, the repository's existing CMake presets — no new dependency, no
toolchain change. Reference provenance: NASA/TM-2015-218675 Vol II Table 73 (radius, `g0`, Earth rotation
rate — already in the configs before this fix); Stevens/Lewis/Johnson and Groves textbook citations (new,
both verified by direct PDF read at their main-checkout paths this session, not taken on trust).

CI note: `.github/workflows/check.yml` runs `GATE=local` (docs/hooks/lint scope only); the full-gate
evidence above is the actual local-mode validation this decision rests on, not a GitHub-reported result.

## Independent review

Claude (Sonnet 5) builder; Codex reviewer, fresh context, `--scope working-tree` (uncommitted candidate).
Full record: `docs/reviews/nesc-case05-gravity-centripetal-review.md`.

- **General review**: no actionable defects.
- **Adversarial review** (tier-3 physics focus: sign/frame re-derivation against ADR-0001/ADR-0003,
  cross-check against `wgs84_j2`'s own Groves term and `eom.cpp`'s actual use of `g_n()`, bit-identity,
  ECEF-oracle independence/tolerance soundness, ADR-0012 domain validity, per-frame allocation/exception
  constraints, honesty of the D1 disclosure, plausibility of the R4/R8 numbers): **verdict approve**, no
  material findings. One accuracy issue in the builder's own reporting (the "4 significant figures"
  overclaim above) was caught and fixed the same round — not a code defect, not a rejected/deferred
  finding, no fix-verification round needed.

No critical/major/minor findings outstanding. No fix rounds consumed (0/2).

## Joe's attention

- **Critical file**: `plugins/physical/gravity/inverse_square/inverse_square.cpp`'s `g_n()` — the sign
  and frame of the centripetal correction. Independently re-derived and cross-checked three ways this
  session (this session's own derivation, Stevens/Lewis/Johnson, Groves) and again by Codex's adversarial
  review; still worth your own read given it feeds every EOM step for any config using this plugin.
- **Critical test**: `NonRotatingLeavesGCorrectionExactlyZeroAwayFromTheEquator` — the `std::bit_cast`
  regression pin explained in its own comment (the `+0.0`/`-0.0` sign-of-product subtlety that motivated
  it). Worth reading alongside the implementation to see why the correction is added component-wise to an
  existing `{0,0,g}` vector rather than constructed standalone.
- **Accepted risk, disclosed not enforced (D2, your own explicit decision, 2026-09-19, option (a))**:
  `physical.earth.rotating` and `physical.gravity.rotating` are two independently-configured booleans
  that a config author must set consistently by hand — exactly today's actual case05 bug, restated as a
  residual rather than eliminated. Symmetric, currently-latent gap also disclosed:
  `gravity/wgs84_j2`'s own unconditional rotation has the same property in the other direction (no
  current config exercises it). Follow-up tracked as a new unchecked `PLAN.md` item (D2(b)); not started,
  needs its own plan review before code (it touches `core/`, not just `plugins/physical/`).
- **Rollback**: revert this diff; no data migration, no golden/ADR file touched, no ABI change (`GravityModel`
  vtable unchanged, no `kCoreApiVersion` bump needed).
- **Dependencies**: none new.
- **Interfaces**: `gravity/inverse_square`'s config surface gains one optional key; no interface/ABI
  change.
- **This candidate is not yet a branch/PR** — it is the worktree's own uncommitted diff. If you approve,
  the next step is committing on the task branch and opening a draft PR (this worktree's own branch,
  per `docs/process/workflow.md`'s Git conventions), not merging directly.

## Decision record

State: pending
Human decision/date/reference: pending
Approved candidate/scope: pending
Merge observed: not merged

Significant tradeoff rationale: D1 (a `rotating` bool reading a hardcoded WGS-84 constant, rather than a
numeric `omega` config key) ties an otherwise-generic point-mass plugin to a WGS-84-specific constant it
does not otherwise depend on — accepted (Joe, this session) because it keeps one single source of truth
for the number and matches `earth/round`'s own precedent exactly, against the alternative's cost of a
duplicated literal.
Accepted risks: D2's disclosed-only rotation-consistency gap (above), owner Joe, follow-up trigger: a
future config pairing `earth/round rotating: true` with `gravity/inverse_square` and omitting the
matching key, or a future config pairing `gravity/wgs84_j2` with a non-rotating Earth model — both silent
today. The pre-existing frozen-wrench characteristic in cases 4/5 (PR 3c's own scope) is unaffected by
this fix and remains an accepted, previously-recorded gap.
Nonblocking follow-ups: `PLAN.md`'s new D2(b) item (earth/gravity rotation-consistency check at init,
needs its own plan review); PR 3c itself (per-RK4-substage wrench evaluation), already tracked,
unaffected by this candidate.
Architectural decisions: none. No new or superseding ADR — this candidate implements
`gravity_model.hpp`'s already-accepted plumb-bob contract, not a new design decision.
