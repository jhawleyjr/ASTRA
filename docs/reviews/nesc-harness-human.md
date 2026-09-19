# Human review packet: M2 PR 3b — NESC ten-case check harness

PR / candidate base and head: base `93592f1685cf64d09d1c23d63dadff569041828f` (= `origin/main`'s
tip at candidate creation, confirmed via `git fetch origin main && git log --oneline
HEAD..origin/main` — empty, no rebase needed). Head as of this line's own last update:
`1a6e1aeacf7b9851befb1682fad0db42cb9807d6`, branch `worktree-m2-nesc-harness`, 15 commits (this
line necessarily lags the commit that writes it by one — check `gh pr view 31 --json headRefOid`
for the exact current head). Draft PR:
<https://github.com/jhawleyjr/ASTRA/pull/31> — **draft, not marked ready** per instruction.
Spec/plan/task: `docs/specs/nesc-harness.md` (approved), `docs/plans/nesc-harness.md` (approved,
includes the cases 4/5 "Known gap found during implementation" section and Joe's recorded decision)
Risk: 3 — `plugins/physical` consumers (new configs only, no plugin code changes), `tests/golden/**`
(new ADR-0014, new NESC band generator/goldens), tolerance derivation (ADR-0013 elaboration)
Decision requested: approve this candidate's behavior (case 1 absorption, the ten-case harness, the
cases 4/5 disclosed-gap disposition already recorded below) toward a future merge decision. Not
requesting merge/ready-for-review now — per instruction, this session stops at this packet.

## What changes and why

Absorbs PR 1's single-case `nesc_case1_test.cpp` (deleted, with its dedicated config and golden) into
a ten-case harness (`tests/physics/nesc/nesc_cases_test.cpp`) checking all ten NASA/TM-2015-218675
atmospheric check cases against per-channel oracle bands generated offline from the report's own
six-tool ensemble (ADR-0013, elaborated by the new ADR-0014 resolution floor). Adds:

- `docs/decisions/0014-nesc-per-channel-tolerance-floor.md` (accepted ADR).
- `tests/golden/nesc/generate_nesc_band.py` (human-owned generator) + `nesc_band_common.hpp`
  (shared golden schema) + 10 generated `case{01..10}_..._band.hpp` files.
- `tests/physics/nesc/case{01..10}_*.yaml` (10 case configs, initial conditions from Vol II Tables
  25–34, cross-checked against the raw CSVs — see "Joe's attention" for what that check caught).
- `tests/physics/nesc/nesc_cases_test.cpp` (the harness) + `tests/physics/CMakeLists.txt` wiring.
- A geopotential-consistency self-check added to the existing, already-guarded
  `tests/golden/atmosphere/generate_isa1976_table.py` (PLAN.md's own settled addition; output
  unchanged, only a new stderr check).

Deletes (absorbed, not lost — case 1 is now one row of the ten-case harness, checked against the
ensemble band like every other case, not against PR 1's independently-integrated DOP853 reference):
`tests/physics/nesc_case1_test.cpp`, `tests/configs/nesc_case1_dragless_sphere.yaml`,
`tests/golden/nesc/case1_dragless_sphere.hpp`.

**Behavior**: 8 of 10 cases pass. Cases 4 and 5 are `GTEST_SKIP()`ed — see "Joe's attention" below;
this was Joe's own explicit decision earlier in this session, already recorded in `docs/plans/
nesc-harness.md` and `PLAN.md`, not a proposal awaiting his read.

## Evidence

| Check / acceptance | Result / date / duration | Log or retained artifact / candidate |
|---|---|---|
| `ctest --preset asan -R physics.Nesc` | 8 passed, 2 skipped, 0 failed, 2026-09-18 | Interactive session output; cases 4/5 skip reason printed inline |
| `ctest --preset {debug,release,static-plugins} -R physics.Nesc` | Same (8/2/0) on every preset | Interactive session output |
| `just check` (full gate, all 4 presets, format/tidy/docs/hooks/pyright/pytest) | **`check: OK`**, exit 0, 2026-09-18 | `/tmp/just_check5.log` (session-local; not yet committed as retained evidence — recommend copying into `docs/reviews/evidence/` before merge if Joe wants it retained past this session) — `100% tests passed, 0 tests failed out of 283` × 4 presets, `0 errors, 0 warnings, 0 informations` (pyright), `32 passed` (pytest) |
| NESC band generator (`generate_nesc_band.py`) | Ran clean, 2026-09-18, `ruff check`/`ruff format --check`/`pyright` all clean | Regenerated after every fix; committed output matches the last clean run |
| ISA generator's new geopotential check | Passed, worst residual 0.281 m vs. 0.5 m tolerance | `check_geopotential_consistency()`'s own stderr report, this session |

Reference/toolchain: NASA/TM-2015-218675 Vol I/II (main checkout `docs/refs/`, gitignored, read
directly this session — page citations throughout the plan/configs). Every case's `initial.state`
cross-checked (R8) against Vol II Tables 25–34's own printed values plus multiple participants' own
t=0 CSV rows, tolerant of known per-tool initialization noise (documented in the plan). No
performance/allocation regression expected or measured beyond the ~1 minute the ten cases add to
`just check`'s wall time (measured before implementation: ~111 s across all 4 presets combined, per
the plan's own first-step measurement — not re-measured after the cases 4/5 skip, which only removes
time).

CI is local-gate mode (`GATE=local`); the evidence above is this session's own local full-gate run,
not a GitHub Actions result.

## Independent review

Builder: Claude (this session). Reviewer: Codex, fresh context, `--scope working-tree` (dirty
candidate, no commit to diff against `--base`).

- **General review: clean.** `node codex-companion.mjs review --scope working-tree --wait` —
  "No actionable defects found in the current changes." Codex independently ran the release-preset
  binary's `Nesc.*` filter itself and confirmed 8 passed / cases 4-5 skipped as documented. Did not
  itself run a fresh build or the full gate (this session's own full-gate evidence above covers that).
  Raw output: `/tmp/codex_review.log` (session-local).
- **Adversarial review: incomplete, not clean — Codex hit a hard usage limit mid-review.** Dispatched
  with a focus naming ADR-0001/0003/0012/0013/0014, the frame/unit conventions, the generator's
  tol_abs/tol_rel-as-single-scalar formula, the yaw-wrap/no-relative-term rule, the Tables 77/78 and
  t=0 exclusions, and the pre-step sampling sequence. First attempt hit a hard Codex usage limit
  mid-pass (recorded honestly, not reported as a pass — `docs/reviews/nesc-harness-review.md` has the
  full account). **Re-run 2026-09-19 after Joe reported the quota reset**, `--base origin/main`
  against the committed candidate (`93592f1..4344fa2`): completed. **Verdict: needs-attention, one
  finding** — Codex independently reproduced all 210 committed channel bands from the raw CSVs and
  confirmed every other claim this packet and the plan make (the `v_north`/SIM 2 contamination, the
  frame/unit/sampling/exclusion correctness, the frozen-wrench diagnosis) — see `docs/reviews/
  nesc-harness-review.md` for the full per-claim verdict. The one finding: this packet's own
  characterization of `tol_rel` as "loose but not currently masking a failure" was false for
  `case04`/`case05`'s `force_y` specifically, which is genuinely non-discriminating (see "Joe's
  attention" below). **Fixed and re-validated same session**: a generator-side detection check added
  (ADR-0013 already required this, never implemented before), full gate re-run clean
  (`check: OK`, all 4 presets), disclosure corrected in this packet and the plan. Finding recorded and
  resolved in `docs/reviews/nesc-harness-review.md` (status: `fixed`, not independently re-verified by
  a second Codex pass — Claude's own recomputation against the committed data is the verification of
  record; see that file for why a second dispatch was judged unnecessary for a diagnostic-only,
  no-golden-data-change fix). Raw logs: `/tmp/codex_adversarial2.log` (session-local).

One fix round this session: correction rounds 1/2 (the `force_y`/`tol_rel` disclosure fix above).
Failed integrations: 0/2.

## Joe's attention

**Cases 4/5, already your decision (option (a), this session)** — `GTEST_SKIP()`ed with a reason
citing `docs/plans/nesc-harness.md`'s "Known gap found during implementation" section: a body
spinning at Vol II Tables 28/29's 10–30 deg/s under real drag exposes that `Eom`'s applied wrench is
frozen across one frame's four RK4 sub-stages while gravity is recomputed fresh each sub-stage
(`core/src/kernel/eom.cpp`), producing a measurable lateral-velocity/attitude drift NASA's own
six-tool ensemble does not show. Confirmed by isolating the mechanism (zeroing `C_D` at runtime
collapses the lateral-velocity drift to floating-point noise); confirmed again, this time at Joe's
own request, across the remaining channels — with `C_D = 0`, `yaw`/`pitch`/`roll` also collapse to
matching the reference exactly (not just `v_east`/`longitude`), while `force_x` fails for
an unrelated, expected reason instead (trivially zero force vs. the reference's real nonzero drag —
a changed scenario, not the coupling bug), which in turn drags `v_down`/`altitude`/`density`/
`pressure`/`temperature` along with it (faster, dragless free-fall). Full account in `docs/plans/
nesc-harness.md`'s "Known gap" section. `PLAN.md`'s new **PR 3c** (per-RK4-substage wrench
evaluation) is the closer — not started in this worktree, needs its own ADR + Codex plan critique +
plan review before any code.

**Three further disclosures, Joe's second review round on the packet itself** (docs-only additions,
no golden/generator/code change this round — full derivations in `docs/plans/nesc-harness.md`'s
"Known gap" section):
1. **`v_north`'s own band is contaminated, so "cases 4/5 never fail on `v_north`" is not verified
   evidence the coupling spares it.** `case04`/`case05`'s `v_north` tolerance (`0.009144` m/s) is
   exactly `3 ×` SIM 2's own known 0.01 ft/s t=0 initialization artifact (already why SIM 2 is
   excluded from `force_x` in these two cases per Table 78 — but that exclusion was never extended
   to velocity). This session's own earlier debug capture shows `case04`'s actual `v_north` at t=30 s
   is `-0.0048504` m/s — larger in magnitude than the `v_east` value that *did* fail at the same
   instant — sitting inside a tolerance ~9144x looser than a clean channel's floor. **PR 3c must
   exclude SIM 2 from `v_north` and regenerate before a `v_north` pass means anything.**
2. `tol_rel` (one scalar per channel per case, `k` times the worst per-sample relative spread) is
   loose — 48–76% relative allowance — on several channels that cross zero at some point in the run
   (`case02` `rate_p`, `case03` `roll`, `case09` `v_down`, `case04`/`case05` `force_z`), the same
   class of issue the yaw fix removed, just less severe (bounded, not unbounded) and not currently
   masking a failure (`tol_abs` alone still rejects an all-zero comparison at some sample for each of
   these four). **`case04`/`case05`'s `force_y` is the one exception, and this session's own first
   disclosure of it was wrong to lump it in as merely "loose": a Codex adversarial review found, and
   Claude independently confirmed against the committed golden data, that `force_y`'s combined
   tolerance accepts an all-zero signal at every one of its 301 samples despite a real −24.8 N peak —
   genuinely non-discriminating, exactly ADR-0013's own "obviously wrong answer" concern, which
   ADR-0013 already requires recording but this generator never actually implemented.** Fixed this
   session: `generate_nesc_band.py`'s new `check_non_discriminating()` (report-only, no golden data
   changed) flags exactly this one channel/case pair and nothing else, confirming the other three are
   correctly not flagged. **PR 3c must resolve `force_y` in cases 4/5, alongside `v_north`, before
   either channel's pass means anything.** Recommended follow-up, not actioned this round: an ADR
   elaborating ADR-0013 to drop `tol_rel` globally, not only for yaw — needs Joe's own decision,
   touches the accepted oracle formula.
3. Band widths vary a lot by which participants happen to contribute to a given case, not only by
   physics — e.g. `case01`'s `temperature` band is ~250x `case02`'s, fully explained by `case01`
   including `sim_03` as a contributor (a real, if larger, disagreement from that one participant)
   while `case02` has no `sim_03` file at all. Not a bug; noted so it is not mistaken for one later.

**Real errors this session's own cross-checks caught before they reached a config or a committed
golden** (worth reading as evidence the R8/independent-recomputation discipline is load-bearing, not
ceremony):
- Cases 2/3's initial altitude: Vol II Tables 26/27 print `[0, 0, 0]`, but the prose directly above
  each table and every participant's own t=0 CSV row both say 30,000 ft — the printed table entry is
  the one in error (a NASA-document transcription slip, `tests/physics/nesc/case02_*.yaml`'s own
  header comment has the full account). Caught by R8's CSV cross-check, not assumed from the table.
- Six inertia values (sphere ×3, brick ×3) in an earlier plan draft were computed 10× too small — a
  genuine hand-arithmetic slip, caught by the required Codex plan critique before any config was
  written, independently re-verified before being fixed (`docs/plans/nesc-harness.md`'s "Plan critique
  and decisions").
- A yaw `tol_rel` gap (a relative-spread term computed at a wrap instant could have silently
  poisoned the whole case's yaw tolerance) and an unsound R8 single-participant check were also
  Codex-plan-critique findings, independently verified and fixed before implementation.

**Critical files to read, in end-to-end order**: `tests/golden/nesc/generate_nesc_band.py`
(the oracle — human-owned, never run by `just check`, produces the ten committed bands) →
`tests/physics/nesc/case01_dropped_sphere.yaml` (representative config; `case04`/`case09` show the
gravity-override and body-frame-velocity-rotation cases respectively) →
`tests/physics/nesc/nesc_cases_test.cpp` (the harness: sampling sequence, frame conversions, yaw
wrap) → `docs/decisions/0014-nesc-per-channel-tolerance-floor.md` (the new ADR).

**Assumptions/risks not yet independently adversarially reviewed** (see "Independent review" above):
frame/unit correctness in the generator and harness, the ADR-0013/0014 formula implementation, and
domain-validity/allocation behavior on the touched per-frame path. General review found nothing;
these specific angles are what the still-pending adversarial pass exists to check.

**Compatibility**: deleting `nesc_case1_test.cpp` removes the one place ASTRA was judged against an
independently-integrated DOP853 reference (tighter in position/attitude, looser in velocity) rather
than the NESC ensemble — a real, if narrow, change in what case 1's own oracle is, disclosed in the
plan, not implied as strictly more coverage.

**Rollback**: 7 commits on `worktree-m2-nesc-harness`, all new files plus three plain deletions, no
production/plugin code touched; closing PR #31 without merging, or a single `git revert` range once
merged, fully reverts. No squash has happened yet (draft, not marked ready).

## Decision record

State: pending
Human decision/date/reference: pending (the cases 4/5 disclosed-gap sub-decision is already recorded
above and in `docs/plans/nesc-harness.md`/`PLAN.md` — this packet's own `State` is about the whole
candidate, which still needs your read, and specifically the adversarial-review gap below)
Approved candidate/scope: pending
Merge observed: not merged
Release authorization: pending / not requested

Significant tradeoff rationale: cases 4/5 accepted as a disclosed gap rather than fixed in this PR —
a real fix (recomputing `wrench_b()` per RK4 sub-stage) is an `Integrator`/`Eom`/`WrenchSource`
contract change out of scope for a check-case harness; landing 8/10 cases now against holding the
whole PR for an architecture change was your explicit call, recorded 2026-09-18.
Accepted risks: cases 4/5's frozen-wrench/spin coupling — owner: `PLAN.md`'s M2 PR 3c (not yet
started); matters more once M3 introduces a real aircraft with sustained coupled spin+force dynamics,
per your own framing when accepting this. Two explicit PR 3c preconditions, both confirmed this
session (the second by independent Codex adversarial review, not just Claude's own analysis):
`v_north`'s band is contaminated by SIM 2's t=0 artifact, and `force_y`'s band is genuinely
non-discriminating (accepts an all-zero signal) — PR 3c cannot judge whether its fix resolves either
channel until both are excluded/regenerated correctly first.
Nonblocking follow-ups: an ADR elaborating ADR-0013 to drop `tol_rel` globally (not only for yaw) —
`tol_abs` alone still rejects an all-zero comparison for `case02` `rate_p`, `case03` `roll`, `case09`
`v_down`, and `case04`/`case05` `force_z` (confirmed by the new `check_non_discriminating()` gate, not
just asserted), so this is not currently masking a failure beyond the one `force_y` finding already
fixed — but all carry a 48–76% relative allowance doing little discriminating work. Needs your own
decision since it touches the accepted oracle formula.
Architectural decisions: ADR-0014 (accepted, this candidate). PR 3c (not yet started) will need its
own new/superseding ADR to ADR-0004 and `docs/architecture.md` §5 — not part of this candidate.

**Adversarial review complete, one finding fixed and re-validated** (`docs/reviews/
nesc-harness-review.md` has the full record): frame/unit correctness, the ADR-0013/0014 band
formula, the yaw wrap, the SIM 2 `v_north` contamination, the pre-step sampling sequence, and the
cases 4/5 frozen-wrench diagnosis were all independently checked against the actual code and
committed data, not just the prose describing them, and confirmed. The one finding (`force_y`'s own
non-discriminating band) is fixed (a new generator-side detection gate, ADR-0013 already required it)
and the full gate re-run clean. **Packet is ready for your decision.**
