# Human review packet: M3 PR 8 — trimmed cruise config, regression run, phugoid cross-check

PR / candidate base and head: base `origin/main` `553700f` (M3 PR 7, #45), confirmed as the
current tip via `git fetch` before push. **Final candidate `46b9aca`**: the pre-capture
candidate `ed10734` (approved), plus commit B `3d0d3a6` (the golden, written and committed by the
armed capture script at `4ce6a9f`), commit C `bffc751` (the golden-comparison test, J-1, J-2), and
`46b9aca` (the Joe-authorized R-003 fix). The head adds only this packet, the findings/evidence
records and STATE.md on top. Working tree clean at packet time.
Spec/plan: `docs/plans/pr8-cruise-regression.md` (Revision 2, approved by Joe 2026-09-22 with
five conditions; the J-1/J-2 note records commit C's cycle).
Risk: Tier 3 (`aircraft/**` config; `tests/golden/**` via the capture step).
**Final update (2026-09-22).** All seven acceptance items pass at `46b9aca`. No findings are open;
R-003 was fixed under Joe's authorization and verified.
**The capture script's blob changed after capture.** The golden was produced by
`tests/capture/capture_cruise_regression.sh` blob `afdc8dd8…` at capture revision `4ce6a9f`. J-2
(`bffc751`) and R-003 (`46b9aca`) changed the script since, to blob `5605f493…`. Neither change
touches what a capture computes: J-2 adds a refusal and R-003 narrows the build target. **The manifest
does not name the script's blob.** It records `capture_revision` `4ce6a9f`, which pins the script
that ran (`git rev-parse 4ce6a9f:tests/capture/capture_cruise_regression.sh` gives `afdc8dd8…`),
plus the blobs of the harness `.cpp`, the run header and `cruise.yaml`. The manifest is frozen and
human-owned, so it is not edited to add the script blob.
Decision requested: squash-merge of the PR at the final candidate (Joe's). Push, draft PR, and
`gh pr ready` were authorized by Joe on a green gate and verification.

## What changes and why

- **`cruise.yaml` is trimmed (D19).** `initial.state.v_b = [229.9360238725, 0.0, 5.42447469393253]`,
  `euler_nb = [0, 0.023586859573503434, 0]`, elevator `0.026674474137989946` rad, throttle
  `0.4985979490507746`: `astra.trim`'s still-air result at repr precision, with a provenance
  comment. Re-running the trim on the edited file reproduces the same state bit for bit. The
  comment states that the trim is still-air and that a run from this file begins in seed-42
  turbulence (frame-0 gust (−3.08, 0.91, 6.63) m/s): a turbulence response from trim, not a
  cruise hold (condition 1).
- **Regression run definition** `tests/support/cruise_regression_run.hpp`: the 13-state at frame
  0 and after each of 300 `run(1000)` calls, 301 samples, `bit_cast` patterns. Shared by the test
  and the harness, so the two can't drift.
- **`notional_transport_cruise_regression`** (own executable, label `physics`): two 300 s runs
  bitwise identical, no halt. The golden comparison joins it in commit C.
- **Capture** `tests/capture/`: an `EXCLUDE_FROM_ALL` harness plus `capture_cruise_regression.sh`.
  In order, the script refuses when unarmed (same test as `guard-oracle.sh`), on a dirty tree, when
  baseline `553700f` isn't an ancestor, on any halt, and on any cross-preset byte difference. Only
  then does it write the header and manifest and commit them. It is not wired into `just check`.
  The manifest text carries the same still-air/turbulence statement (condition 1).
- **Phugoid cross-check** `tests/python/test_cruise_phugoid.py`: the trimmed-state residual
  (acceptance 1), then period and damping against PR 7's `A` plus the altitude state (Q2). The
  altitude column comes from `linearize._evaluate` called directly (condition 4). A supporting test
  checks the ḣ row against `derivative()`.
- **Q1:** `test_trim.py`'s untrimmed-guess test sets the pre-D19 values explicitly through
  `Config.from_dict`; its assertion and hand-estimate are unchanged.
- **Other changes:** a comment-only fix in `notional_transport_cruise_config_test.cpp`; the new
  test target added to `astra_finalize_plugins()`'s explicit dependency list in
  `cmake/AstraPlugin.cmake` (the pattern that PLAN.md item already tracks); a PLAN.md PR 7
  follow-up (D11's `A` omits altitude); retained evidence.
- **Not changed:** no new binding (`Simulation.run(frames)` was already bound by PR 5), no
  core/ADR/dependency change, and `astra.trim`/`astra.linearize` are unmodified.

Diff `553700f..46b9aca`: 25 files, +17409/−46. Most of that is four retained full-gate logs (about
14,400 lines) and the golden (1,300 lines).

## Evidence

| Check / acceptance | Result | Log / candidate |
|---|---|---|
| Full gate `./scripts/check-full.sh` | `check: OK`, exit 0; 389/389 per preset; pytest 114 passed | `docs/reviews/evidence/46b9aca-full-gate.log` at **`46b9aca`** |
| Earlier gates (superseded) | `check: OK` at `b4c5f44` (pytest 107), `ad04b9e` (110), `ed10734` (111; 388/388), `bffc751` (113) | `docs/reviews/evidence/{b4c5f44,ad04b9e,ed10734,bffc751}-full-gate.log` |
| R-003 dependency claim | in fresh binary directories under release and static-plugins, building only `capture_cruise_regression` yields a harness whose output hashes identically to the golden (`6120a053…`); the regression test is not built | session scratch (commands recorded in the findings file) |
| Capture (commit B `3d0d3a6`) | armed script, exit 0; `cross_preset_agreement: true (debug release asan static-plugins, byte for byte); no halt`; capture revision `4ce6a9f`; frozen bit patterns hash identically to the pre-arm dry run | `tests/golden/notional_transport/cruise_regression_manifest.md` |
| 1. Trimmed derivative < 1e-6 | `test_cruise_config_initial_state_is_trimmed` passes; D8 max-abs 8.3e-17 (still air) | gate log |
| 2. Bitwise match, four presets | `MatchesTheFrozenTrajectoryBitwise` passes under debug, release, asan and static-plugins | gate log at `bffc751` |
| 3. Capture refuses unarmed | 11 guard tests pass: unarmed; env ≠ 1; dirty tree; no baseline; inherited `GIT_*` isolation; halt; cross-preset mismatch; an existing golden (J-2) with header+manifest, header only, and manifest only; and the authorized re-capture path building only the harness target (R-003). Each of the inherited-`GIT_*`, mismatch, J-2 and R-003 tests was shown to fail against a deliberately broken version | gate log |
| 4. Phugoid period / damping | pass: 108.573 s vs 108.534 s (tolerance 5%); ζ 0.02711 vs 0.02730 (tolerance 0.02). J-1 left ζ unchanged | gate log |
| 5. Two runs bitwise identical | `TwoRunsFromTheSameConfigAreBitwiseIdentical` passes, all four presets | gate log |
| 6. Existing consumers green | all green; list from `grep -rl "notional_transport/configs/cruise.yaml" tests python aircraft --include=*.cpp --include=*.py` | gate log |
| 7. Full gate green | yes, at `46b9aca` | above |
| Pre-arm capture dry run | four presets byte-identical, no halt | `docs/reviews/evidence/b4c5f44-capture-dryrun.md` |

**Dry run** (condition 2). The harness ran under debug, release, asan and static-plugins; all four
outputs have SHA-256 `bd9b9fbe…3142649`, and no run halted. Only test files changed after
`b4c5f44`, so the run is still current. Over the 300 s:

| Quantity (from trim) | Excursion |
|---|---|
| u, body x velocity | max \|Δu\| 7.97 m/s (−2.55 … +7.97) |
| altitude | −141.3 … +120.1 m |
| pitch θ (trim 1.351°) | max \|Δθ\| 3.05° (−2.59 … +3.05) |
| also | max \|φ\| 5.1°; heading −5.9° at 300 s; max \|ω_b\| 0.151 rad/s |

**Both residuals at the config's own initial state:** 8.3e-17 in still air (asserted). With the
frame-0 Dryden gust latched after one `step()`, it is 3.77 m/s², mostly ẇ.

**Phugoid** (condition 5: the table, not a single percentage):

| Source | Damped period | ζ |
|---|---|---|
| Nonlinear run, u − u_trim (asserted) | 108.573 s | 0.02711 |
| PR 7 `A` + altitude, 5×5 (reference) | 108.534 s | 0.02730 |
| **PR 7 `A`, 4×4 (printed, not asserted)** | **125.331 s: 15.4% longer than measured (the measured period is 13.4% shorter than the 4×4)** | **0.05204** |

Sensitivity of the measurement to its reference (plan finding 6):

| Measured signal | Period | vs 5×5 | ζ | vs 5×5 |
|---|---|---|---|---|
| u − u_trim, nonlinear (asserted) | 108.57 s | +0.04% | 0.0271 | −0.0002 |
| u_perturbed − u_unperturbed | 108.70 s | +0.16% | 0.0280 | +0.0007 |
| u − u_trim, linear detrend | 109.02 s | +0.45% | 0.0309 | +0.0036 |
| 5×5 model's own u(t), same method | 108.63 s | +0.09% | 0.0280 | +0.0007 |

The +0.09% method bias comes from the slow height mode's contribution to u (λ = −0.00173 s⁻¹),
which shifts the crossings and peaks. It is not the sampling rate or the interpolation: on the
phugoid component alone the method is off by 2e-7 at 1 Hz, and all modes together give +0.091% at
1, 10 and 100 Hz alike. So the +0.04% is partly fortuitous; the robust statement is agreement
within 0.5% in period and 0.004 in ζ.

**Tolerance slack (condition c of the Q2 decision).** The 5% and 0.02 tolerances were set for the
4×4 comparison. Against the 5×5 they carry slack of at least 10× in period and at least 5× in ζ,
and they stay as written.

**The 4×4 miss** (condition b of the Q2 decision). PR 7's altitude-free `modes(A).phugoid` misses
the nonlinear run by 15.4% in period and 0.025 in ζ. It would fail both tolerances. It is printed,
not asserted. PLAN.md tracks an opt-in altitude state in `astra.linearize` as a D11 revision, which
is Joe's call.

**Altitude coupling.** The energy estimate V·Δu/g = 46.9 m assumes all of the +2 m/s goes into the
phugoid. In the 5×5 it splits into the phugoid (35.4 m amplitude) and the height mode (+35.5 m,
τ ≈ 580 s), whose sum peaks at 64.9 m at t = 53 s. The nonlinear run peaks at 64.9 m at t = 54 s
(perturbed minus unperturbed). The observed 70.2 m includes about 5 m of rotating-Earth climb.
Density changes −0.58% at 47 m and −0.80% at 65 m.

**Gate cost.** Test wall time per preset (debug/release/static-plugins/asan) went from 36/1/90/91 s
(PR 7) to 76/1/163/346 s at `bffc751`. The determinism test runs 300 s twice: about 344 s under
asan and 160 s under static-plugins. The golden test's single run executes in parallel with it, so
it added no wall time (asan 346 s both before and after commit C).

Toolchain: clang-18, Ubuntu 24.04, per the gate log. Seed: Dryden 42 (cruise.yaml).

## Independent review

Builder: Claude (Opus 5.5). Reviewer: Codex (companion 1.0.6), fresh context.
Findings: `docs/reviews/pr8-cruise-regression-review.md`.

- **General review** (`553700f..18ca1e8`): no actionable defects.
- **Adversarial review** (same scope, six-area focus): one finding, **R-001**. The guard test's
  fixture `git` calls inherited `GIT_DIR`/`GIT_WORK_TREE`, so they could have acted on a real
  checkout. I reproduced it (the decoy repository's HEAD moved) and fixed it in `ad04b9e`. The same
  commit added halt and mismatch refusal tests, at the reviewer's suggestion.
- **Fix verification 1** (`ad04b9e`): R-001 holds. New finding **R-002**: the shared-clone fixture
  asserted that no golden exists, so it would break the post-capture gate. Fixed in `ed10734` by
  snapshotting golden files and asserting they are unchanged.
- **Fix verification 2** (`ed10734`): R-001 and R-002 hold, "New findings: none", verdict approve.
  Caveat: the checks are final-state checks, not proof against a write that is later reverted.
- Correction rounds used: 2/2, so any further finding goes to Joe.
- **Reviewer caveat:** Codex did not independently recompute the trim repr values. They are
  checked by the bit-for-bit trim reproduction and by acceptance 1.
- **Commits B and C** (`ed10734..bffc751`, general + adversarial):
  - **Adversarial:** verdict approve, "No material findings". Confirmed: B touches only the two
    golden files; the manifest revisions and blobs match git; the frozen bytes match the dry run;
    C's test compares every value and can't pass vacuously; J-2 runs before any build or write, and
    the clone tests fail without it. Caveat on J-1: the claim that the window's maximum is the
    second peak holds for this response, not for arbitrary slow-mode mixtures.
  - **General:** one finding, **R-003 (minor, needs-human)**. The re-capture path J-2 documents
    can't work as written. After Joe removes the golden, the script's full `cmake --build`
    compiles the regression test, which includes the now-missing header, so the build fails
    before the harness can regenerate it. The failure was safe: the script stopped and wrote
    nothing.
- **R-003 fix** (`46b9aca`; authorized by Joe 2026-09-22, outside the correction budget, rounds
  stay 2/2). Step 4 builds only `--target capture_cruise_regression`, and the script comment
  explains that this target carries the full plugin dependency via `astra_finalize_plugins()`
  under both plugin modes. I checked that claim in fresh build directories. The new guard test
  `test_authorized_recapture_builds_only_the_harness_target` runs the real re-capture path: golden
  removed in a commit, armed, J-2 not firing, and the stub `cmake`'s invocation log showing exactly
  one harness-target build per preset. It fails if the full build is restored. **Codex fix
  verification of the R-003 delta** (`abe3764..46b9aca`): approve, "New findings: none".
- **Open findings:** none.

## Joe's attention

- `aircraft/notional_transport/configs/cruise.yaml`: the literals and provenance comment.
- `tests/capture/capture_cruise_regression.sh`: the refusal order, including J-2's step 1b, and
  the R-003 step-4 build.
- `tests/golden/notional_transport/cruise_regression_manifest.md`: what was frozen, from which
  revision, under which toolchains. See the script-blob note at the top.
- The arming marker has been removed (checked absent in both locations before the R-003 fix).
- `tests/python/test_cruise_phugoid.py`: the 5×5 construction, the measurement, and the
  log-decrement derivation. No reference in `docs/refs/` defines the log decrement.
- Accepted-by-plan disclosure: the trim is still-air on a rotating Earth, so the unperturbed run
  drifts (u −0.19…+0.01 m/s, altitude +5.8 m, heading −3.2° over 300 s).
- **Capture history:** armed at 15:54. The script ran from clean `4ce6a9f` and committed
  `3d0d3a6`; commit C `bffc751` followed; the full gate and Codex review of B and C followed that.
  The capture command itself is recorded in the manifest.
- **Rollback:** revert the branch. No core, API or ICD change.

## Decision record

State: approved (pre-capture scope only; merge decision pending after commits B/C)
Human decision/date/reference: Joe, 2026-09-22, in session: "Pre-capture candidate ed10734
approved (base 553700f)." Arming follows (Joe touches `.claude/touch-goldens` himself). Joe's two
findings for commit C's cycle (not a third correction round): (J-1) the damping test's second
peak window must end at the record end, not at a third crossing, which lands at 292.59 s of a
300 s record; (J-2) the capture script must refuse when a golden already exists, because a
re-capture needs a fresh, explicit authorization, not just arming. Gate cost accepted as written.
Joe, 2026-09-22: "R-003: fix it. Authorized by me, 2026-09-22, outside the correction budget";
push, draft PR, and `gh pr ready` authorized on a green gate and fix verification.
Approved candidate/scope: `ed10734` against base `553700f`, pre-capture. The final candidate
`46b9aca` awaits Joe's squash-merge.
Merge observed: not merged
Release authorization: not requested

Significant tradeoff rationale: the gate's asan leg grows by about 4.3 minutes, and static-plugins
by about 1.2 minutes, to keep the determinism and regression acceptance literal on all four
presets.
Accepted risks: none beyond the plan's disclosures.
Nonblocking follow-ups: PLAN.md "`astra.linearize`'s 9-state `A` omits altitude" (D11 revision,
Joe's call).
Architectural decisions: none.
