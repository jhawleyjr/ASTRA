# M7 PR 1a: ADR-0028, ADR-0030, ADR-0031 and architecture rev 11

Spec: [M7 plan](m7-f16-nesc-oracle.md) (approved by Joe, 2026-10-04, at `3381693`): Q2 to Q4, Q16, Q20, R8,
R18, and the PR table's row 1a.
Risk: Tier 3. It writes accepted-record candidates for the oracle (ADR-0013's family) and a core interface
(`AeroModel`, `HealthMonitor`). Docs only: no code, tests, goldens or build files.
Status: **approved by Joe, 2026-10-04, in plan mode.** D1 to D7 are accepted as recommended, as amended by his
amendments 1 to 12, which are folded in below (marked A1 to A12). **ADR-0028, ADR-0030 and ADR-0031 were
accepted by Joe, 2026-10-05**, as written at `975cec6`, with both choices outside D1 to D8 confirmed: ADR-0030's
`*_timeout_ms` exception, and ADR-0031's skipped q̄ or n_z publishing 0. He also approved architecture rev 11.
Base: `origin/main` `9772031` (#104), re-checked after `git fetch`. Worktree `m7-pr1a`, branch
`worktree-m7-pr1a`. Joe arms the `docs/decisions/` guard; Claude never arms it.

## Context

The M7 plan decided three things and put all three records in PR 1a, because none needs P1 or P2:
- the F-16 oracle (Q2 to Q4);
- ADR-0002's identifier wording (Q20);
- the envelope declaration (Q16).

ADR-0028 must be accepted before PR 6b and PR 8 start, and ADR-0031 before PR 13. This PR writes the records,
brings the architecture to rev 11, and adds PR 1a's PLAN.md line. Nothing is implemented.

## Repository findings (read at `9772031`)

- **Matching on the time grid (A3).** ADR-0013 says `t = frame * dt` "compares equal to the sample instants
  as doubles". That claim is about the sample instants, so the comparison is `frame * dt` (frame = 100k) with
  the decimal instants k/10 read as doubles. They differ at 325 of k = 0..1800. Against `k * 0.1` computed in
  double, the count is 296 (both verified this session).
  - Nothing relies on the claim. The harness steps by frame index (`nesc_cases_test.cpp:157`, `sim.run(99)`
    after each sample), and `nesc_band_common.hpp:9` defines sample k as frame 100k.
  - ADR-0028 states the frame-index rule and discloses the remark. ADR-0013 is not edited.
- **The generator** (`tests/golden/nesc/generate_nesc_band.py`):
  - `SAMPLE_COUNT = 301` (`:101`) and `NEAREST_SAMPLE_TOLERANCE_S = 1e-6` (`:103`);
  - yaw wrapped by a circular mean (`:332`, `wrap_to_pi` at `:270`), the rule longitude takes;
  - `excluded()` (`:278`) applies only footnoted rules.
- **Unfootnoted cells (A1).**
  - Vol II's Table 80 (p.594) marks some cells "0.000 – data provided but not included in calculation", with
    no footnote. Among them: SIM 5's yaw, pitch, roll and longitude, and both members' latitude, speed of
    sound and temperature.
  - Table 78 (p.592) marks SIM 5's Euler angles the same way for cases 1 to 10, and the generator never
    excluded them.
- **ADR-0020 says "exactly one deliberate, non-empty override each"** on `AeroModel`/`PropulsionModel`. It
  also names "any future envelope-monitor hook" as a reason the marker types exist.
- **ADR-0026** publishes `v<i>.derived.{alpha, beta, mach, tas, az, status_flags, …}` at the frame-start
  point, through the one `pack_sim_to_sw` call. It says "No second derivation exists anywhere".
  - `az` is frame k−1's specific force, and `ACCEL_INVALID` is set at frame 0.
  - q̄ is in no log: no atmosphere signal is registered, and the derived set has no density (A6).
- **Health.** `HealthCheck` ends at `kLog`.
  - The `sim.health` keys are `nan`, `quaternion`, `domain` and `input`, with the global counters
    `health.*` (`health_monitor.cpp:90`).
  - `_runkit.WARN_COUNTERS` and `HEALTH_SIGNALS` (`_runkit.py:63`) mirror them.
- **§15's pre-run list** has "airspeed inside the aero model's declared envelope", halt by default. It was
  never built, and it would refuse case 12 at Mach about 2.
- **Unit suffixes today.**
  - Config keys: `wing_area_m2`, and `w20_knots`, which is non-SI and converted at load by
    `units::knots_to_meters_per_second` (`dryden.cpp:125`).
  - C++ identifiers: `AtmoState`'s fields and `w20_m_s_`.
  - Signal names: none in `core/`, `plugins/` or `aircraft/` (`moment_b_m` is the m axis). The test plugin's
    `counting_wind.last_altitude_m` is outside ADR-0002's scope.
  - ICD fields: none.
  - Q20's `alpha_max_rad` does not exist yet, so ADR-0030 cites only keys that exist.
- **The spec and the plan disagree** in three places, settled by D1, D3 and D7.

## Recomputation (step 2, 2026-10-04, read-only scratch, not committed)

A script imported the generator's constants and `CHANNELS` and read the main checkout's
`docs/refs/Atmospheric_checkcases`, Vol I pp.13 and Vol II pp.592-594 (`pdftotext`). It found:
- **The preview table reproduces.** Every band in the M7 plan's preview, and each case's zero-accepting list,
  match to the digits shown. So do 13.1's t ≥ 2 s force_z (1.361e5 N) and case 15's full-window force_z
  (3.69e4 N). 13.3's force_z band, 6.258e4 N, is 68.6 % of Table 7's weight, 9.12e4 N.
- **Time offsets.** Within the Vol II durations, `sim_05`'s largest offset from k/10 is 6.1e-6 s in cases 11,
  12, 15 and 16, 7.63e-7 s in 13.1 to 13.3, and 1.53e-6 s in 13.4. `sim_02` and `sim_04` are exact. The M7
  plan's "1.5e-6 s in 13.1 and 13.2" is over `sim_05`'s whole 60 s file. Within 20 s the offset is below
  ADR-0013's 1e-6 s.
- **Rows and columns.** Every member has every grid point within its duration. The duplicate
  `feVelocity_ft_s_Z` columns are equal in every row of all eight cases.
- **Footnotes and cells.** Table 79 and 80's footnotes (a) to (f) and the "0.000 – data provided but not
  included in calculation" key are as A1 states. Within `CHANNELS`, the unfootnoted 0.000 cells are:
  - SIM 5's longitude, yaw, pitch and roll;
  - both members' latitude and temperature.
  Speed of sound and local gravity are not channels.
- **Vol I §6.2.7** (p.13): "a minimum of three data sets (i.e., parameter trajectories) were necessary to
  warrant inclusion in this assessment."
- **SIM 2.** Its gated distance from the band is far outside in the trim cases (case 11's altitude at 98
  times `tol_abs`). This is reported, never gated.

## Joe's decisions (2026-10-04; D1 to D7 as recommended, with A1 to A12)

- **D1, R8's lists.** ADR-0028 records the rule: every regeneration reports each channel's floor ratio and
  its zero-accepting channels, and a zero-accepting channel is never evidence.
  - It records the measured preview as context, labeled a preview.
  - PR 8's generator report, in its PR body, is the record of the generated lists.
  - R8 in the M7 plan gets a dated one-line amendment saying so.
- **D2, ADR-0028's relation to ADR-0013 to ADR-0015.**
  - It supersedes ADR-0013 *in part, for cases 11 to 16 only*:
    - the 1e-6 s matching tolerance becomes 1e-5 s;
    - `max over t` becomes the max over the gated window;
    - "every sample" becomes every gated sample.
  - It elaborates ADR-0014 (floors unchanged) and ADR-0015 (absolute-only, and SIM 2 excluded as a
    participant for the whole case, by Table 79's footnote (b)). Cases 1 to 10 keep ADR-0013's text.
  - Footnotes govern exclusion (A1). Footnotes (c) to (f) explain transients and exclude nothing; ADR-0028
    quotes their text. An unfootnoted "0.000 – data provided but not included in calculation" cell in
    Table 80 (p.594) is not an exclusion, following the Table 78 (p.592) precedent for cases 1 to 10, which
    `excluded()` never applied. Both tables are cited.
  - Disclosed: excluding SIM 5's Euler angles would leave one member and no band.
- **D3, ADR-0031's relation to ADR-0020.** It supersedes only ADR-0020's "exactly one … override each"
  sentence, and only for `AeroModel`. `PropulsionModel` is unchanged.
- **D4, the declaration's shape.** `AeroModel::envelope() const noexcept` returns five closed intervals:
  - the quantities: α, β, Mach, q̄ and the normal load factor n_z;
  - each bound may be ±∞ (undeclared), and the default is all undeclared;
  - it is queried once after `init()`;
  - a NaN bound or lo > hi is a `ConfigError` naming the plugin path.
  Several aero models are each checked on their own, each naming its plugin. Lateral load factor is out of
  scope.
- **D4's rule (A4).** An aero model declares the domain of its own data: its table breakpoints, or, along an
  axis where the data has none, a stated validity limit.
  - ADR-0031 records only the mechanism and the rule. It gives no F-16 limit, no config key name and no
    `kCoreApiVersion` number, since P1, PR 3 or PR 13 could contradict them.
  - It may cite the notional transport's 0.23 rad, which Joe decided.
- **D5, what the check reads.**
  - It uses ADR-0026's derived values for the same frame, at the frame-start point right after the derived
    stage, with no second derivation:
    - α, β and Mach from the derived signals;
    - q̄ = ½·ρ·tas², with ρ from the same frame-start environment sample;
    - n_z = −az/g0, with g0 = 9.80665 m/s² as a named units constant.
  - Status bits skip quantities (A5):
    - `WIND_INVALID` skips α, β, Mach and q̄;
    - `ATMOSPHERE_INVALID` skips Mach and q̄;
    - `ACCEL_INVALID` skips n_z (frame 0 included).
    A skipped quantity is never compared as a placeholder zero.
  - Disclosed (A5): n_z, read from `az`, sees an excursion one frame late, so a load-factor halt lands one
    frame after the excursion.
  - A halt stops the frame before the exchange and any `step()`, and the clock does not advance.
- **D6, health config, counters and signals.**
  - One `HealthCheck::kEnvelope`, appended after `kLog`. Its halt source names the plugin and the quantity,
    prebuilt at init.
  - Five action keys under `sim.health.envelope.{alpha, beta, mach, qbar, load_factor}`, `warn` by default,
    and five always-registered counters `health.envelope.*`.
  - The reason for one check rather than five values (A6): the halt already names the quantity in its source,
    the action and the counter are per quantity anyway, and five enum values would add five Python enum names
    and `/summary` spellings for one mechanism.
  - The check publishes the two quantities it computes itself, q̄ and n_z, as per-vehicle signals with no unit
    suffix (A6): for example `v<i>.envelope.qbar` and `v<i>.envelope.load_factor`, with the final names left
    to PR 13. Every quantity a counter counts is then in the log and in the summary's min/max.
  - The tools' sets gain the counters, and `astra run --strict` exits 2 on an envelope-only warning.
  - Every log gains the counters and the two signals. PR 13 lists the column-pinning tests that move.
  - §15's pre-run "airspeed inside the declared envelope" bullet is removed. Frame 0's check, under the
    configured action, replaces it.
- **D7, §6 joins rev 11** for ADR-0030.
- **D8, Codex finding F-01 (Joe, 2026-10-04, in session).** The first draft added a ρ-only q̄ skip that no
  status bit names, so a logged q̄ of 0 could not be told from a valid zero. Joe chose to drop that skip:
  A5's table stands exactly.
  - Every skip maps to a bit already logged in `v<i>.derived.status_flags`. A skipped q̄ or n_z publishes 0,
    decoded from that row. There is no new status signal.
  - Non-finite ρ is not a skip. `vehicle.cpp`'s environment check tests density's finiteness and reports
    `kNonFiniteEnvironment` under `sim.health.nan` (halt by default, counted in `health.nan`). q̄ is computed
    regardless. A non-finite q̄ counts as outside a declared q̄ interval, and the published q̄ carries the NaN
    for that frame.
  - Only a quantity with at least one finite bound is checked. One with no declared bound is never counted,
    NaN included.
  - Disclosed: a finite ρ ≤ 0 from a defective atmosphere plugin gives q̄ ≤ 0, which passes when the q̄ lower
    bound is undeclared. Nothing refuses a non-positive density today, and fixing that is not ADR-0031's job.

## Obligations carried to later PR plans

| PR | Obligation |
|---|---|
| 6a, 13 | The F-16's Mach limit (A4). If the F-16 aero has no Mach dependence, its declared Mach limit is a validity limit stated by P1. If P1 states none, case 12's expected nonzero Mach counter (R18, N-07) has no basis, and the expectation returns to Joe before PR 13 fixes it |
| 11a | R12 compares the peer's EAS against TAS·√(ρ/ρ0) from "the sim's own atmosphere signals", and no atmosphere signal is logged (A12). PR 11a's plan states where ρ comes from |

## Deliverable 1: `docs/decisions/0028-f16-nesc-oracle.md` (written proposed; accepted by Joe, 2026-10-05)

Status, then the related records (D2).

**Context.**
- The inventory:
  - three members at 10 Hz;
  - the durations and their exceptions;
  - `sim_05`'s float32 times and its duplicate column.
- Tables 79 and 80, with their footnotes quoted (a) to (f), and the unfootnoted cells (A1).
- Vol I's three-data-set rule, and why the members differ.
- The measured preview with its zero-accepting channels (D1).

**Decision.**
- **Members.** SIM 4 and SIM 5 only, with SIM 2 excluded by a whole-case rule. SIM 2's distance, as a ratio
  to `tol_abs` over the gated window, is reported and never gated. Footnotes govern, and unfootnoted cells
  exclude nothing (A1, D2).
- **Window.** t ≥ 8 s in every case, so the first gated sample is 80. The band's max and the pass test run
  over gated samples only. The 0 to 8 s samples are still compared and logged, never asserted.
- **Durations.** From Vol II Tables 35 to 43. Extra rows are ignored, and a member short of a duration is an
  error.
- **Matching.** Within 1e-5 s, the float32 half-ulp bound below 256 s (7.6e-6 s); the observed maximum is
  6.1e-6 s.
- **Alignment** is by frame index: sample i is frame 100i. The grid remark (A3) is disclosed.
- **Longitude** uses yaw's circular mean and spread. Duplicate columns must agree.
- **The band** is ADR-0013's scalar per channel and case:
  - the reference is the members' mean, their midpoint;
  - `tol_abs = max(3·max_gated |sim4 − sim5|, floor)`;
  - ADR-0014's floors and ADR-0015's absolute-only test apply, and k = 3 is unchanged.
- **The aero channels (A2).** Each `aero_body*` channel is compared with ASTRA's aero-only,
  frame-start-stage wrench about the CG, obtained by the M7 plan's Q5 route, never `wrench_total`. This is a
  pointer, not a Q5 record.
- **Reports** per D1.

**Rejected.**
- all three members;
- a per-channel SIM 2 list (ADR-0015's reason);
- excluding the unfootnoted cells;
- a 2 s window;
- a per-sample band;
- interpolation.

**Consequences.**
- The two-member ensemble falls below Vol I's three-set rule; this is disclosed.
- The maneuver cases' force and moment bands are loose, while their kinematic channels discriminate.
- 13.1's and 13.2's onset falls in the ungated span. R22 covers it.
- `nesc_band_common.hpp` gains a per-case sample count and first gated sample.
- Cases 1 to 10 regenerate byte-identical.
- R9's negative controls must fail discriminating bands.
- PR 8 implements this record, and Joe runs the generator.

**Before writing.** A read-only scratch script recomputes every cited number from the main checkout's
`docs/refs/Atmospheric_checkcases`:
- the 6.1e-6 s maximum;
- the duplicate-column equality;
- the preview table;
- the Table 80 cells against the CSV columns.
It is not committed. A mismatch goes to Joe and is never edited to fit.

## Deliverable 2: `docs/decisions/0030-unit-suffixes-in-identifiers.md` (written proposed; accepted by Joe, 2026-10-05)

It supersedes only ADR-0002's bullet "No unit suffixes in identifiers (`altitude`, not `altitude_m`) inside
the framework…".

**Decision.**
- Signal names, write-seam input names included, carry no unit suffix.
- C++ identifiers may carry one.
- A config key may carry a suffix naming the unit of the value as written. A non-SI suffix is converted at
  load by a named `core/util/units` function and stored in SI (ADR-0002's third bullet stands).
- ICD field names stay ADR-0006's.

**Context.** The examples in the findings.

**Rejected.**
- every identifier unsuffixed;
- suffixes required everywhere.

**Consequences.**
- No code changes.
- The test-plugin signal is disclosed.
- The PLAN.md (M7) item is closed.

## Deliverable 3: `docs/decisions/0031-aero-envelope-declaration.md` (written proposed; accepted by Joe, 2026-10-05)

**Decision.** D3 to D6 with A4 to A6: the mechanism, the data-domain rule, what is read, the skip table,
the one-frame n_z lag, the check, the counters and the two published signals.

**Context.**
- §15's unbuilt bullet;
- ADR-0020's anticipated hook;
- M6's R9 batch;
- the notional transport's 0.23 rad.

**Rejected.**
- a declaration on every `WrenchSource`;
- config-only limits;
- a separate init check;
- five `HealthCheck` values (A6's reason);
- re-deriving α, β and Mach in the monitor.

**Consequences.**
- A `kCoreApiVersion` bump (unnumbered) and stale-plugin rejection.
- The new log columns, and the tools' counters.
- M6's R9 unchanged under `warn`.
- PR 13 implements it.

## Deliverable 4: `docs/architecture.md` rev 11

The status line gains rev 11, proposed until Joe accepted the three records (2026-10-05).
- §6: the units paragraph per ADR-0030.
- §7: `AeroModel`'s `envelope()`, *planned, PR 13*.
- §12: `astra.trim`'s planned changes (PR 5), and the tools' envelope counters (PR 13).
- §15:
  - the envelope bullet per ADR-0031 (*planned, PR 13*);
  - the pre-run envelope bullet removed;
  - the post-run sentence: the envelope's min/max are the summary's min/max of the derived α, β and Mach and
    of the published q̄ and n_z signals (A6).
- §16: the M7 row cites ADR-0028 and ADR-0031, plus an M7 note.
- §18: step 4 on a flat-Earth copy (*planned, PR 5*), and the tutorial (*planned, PR 15*).

## Deliverable 5: `PLAN.md` and the M7 plan

- `PLAN.md`:
  - the PR 1a line;
  - the (M7) ADR-0002-wording item removed;
  - the (M7) envelope item noted "shape decided by ADR-0031; PR 13".
- `docs/plans/m7-f16-nesc-oracle.md`:
  - R8's dated amendment (D1);
  - the two obligations above added to its "Obligations carried to PR plans" table, as Joe's PR 1a
    decisions.
- This plan becomes `docs/plans/m7-pr1a-adrs.md`, moved from the main checkout's `docs/plans/`, with nothing
  left untracked there.

## Execution

1. `git fetch`, then EnterWorktree `m7-pr1a`. Confirm that `git rev-parse HEAD` equals the fetched
   `origin/main` (A7). If it does not, remove that worktree and run
   `git worktree add .claude/worktrees/m7-pr1a -b worktree-m7-pr1a origin/main`. Then move this draft in and
   rename it.
2. Recompute the cited numbers (scratch, read-only, from the main checkout's `docs/refs`).
3. Write rev 11, PLAN.md, the M7 plan's amendments and this plan. Commit, push, and open the draft PR.
4. Write the three ADRs (A8).
   - The first ADR Write is the guard check. There is no probe file. If it is refused, stop until Joe arms
     the guard.
   - While the guard is lifted, write only the three ADR files: nothing else under `docs/decisions/`, and
     nothing under `tests/golden/**`.
   - Commit and push.
5. `python3 scripts/docs_check.py`; `git diff --check origin/main..HEAD`. A scratch script, written with
   Write, imports docs_check's `check_links()` and runs it on the three ADRs, `docs/architecture.md` and both
   plan files, which `WORKFLOW_DOC_ROOTS` does not cover (A9).
6. `/review-cycle`: an adversarial Codex review (committed-branch scope, effort high).
   - It checks consistency with Q2 to Q4, Q16, Q20, D1 to D7 and A1 to A12; with ADR-0002, 0013 to 0015, 0020
     and 0026; with rev 11; and with the code facts cited.
   - Its read-only input includes the main checkout's `docs/refs` path, Vol II pp.592-594 and the CSVs, so it
     can check every number and footnote ADR-0028 cites (A10).
   - At most two correction rounds. A change to a decision goes to Joe.
7. `./scripts/check-full.sh` in the background on the final candidate: exit 0 and `check: OK`. Commit the
   trimmed `docs/reviews/evidence/<head>-gate-summary.log`.
8. `/human-review` for the PR body. It holds:
   - the commands, exit codes and counts;
   - the link-check result beside docs-check's;
   - the Codex verdict and thread id;
   - `git diff --name-only origin/main..HEAD` (A8).
9. ADR acceptance (A11). The status lines change to accepted only on Joe's explicit in-session instruction
   naming each ADR. If that write is refused, it is not retried by any other route (lesson 2026-09-29).
   - Rev 11's entry in the architecture status line changes in the same commit. That commit is Markdown
     only (Joe's amendment to step 9).
   - Afterward, rerun `python3 scripts/docs_check.py`, the scratch link check and
     `git diff --check <gated-head>..HEAD`.
   - The PR body names the gated head, and lists `git diff --name-only <gated-head>..HEAD` as the
     Markdown-only delta.
10. `/wrap`.

## Verification

- docs-check, the scratch link check and `git diff --check origin/main..HEAD`, all exit 0.
- The full gate: exit 0 with `check: OK`.
- Every number and footnote in ADR-0028 traced to the scratch recomputation or a cited page.
- Codex's adversarial verdict, with its findings verified and resolved.
- Joe's explicit acceptance of ADR-0028, 0030 and 0031, named one by one.
