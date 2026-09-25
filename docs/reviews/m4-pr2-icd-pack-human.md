# Human review packet: M4 PR 2, ICD truth packing

PR / candidate base and head: base `a3d6071` (`origin/main` at branch time, #51's merge commit; re-check
before merge). Product candidate `6965165` (code, tests, ICD header); the evidence commit on top of it
adds only retained logs and this packet. Branch `worktree-m4-pr2-icd-pack`, not pushed.
Spec/plan/task: [M4 plan](../plans/m4-lockstep.md) PR 2 row, R11, R12a, Q11; approved plan for this PR
with Joe's eight amendments (2026-09-25).
Risk: Tier 3 (`icd/` semantics, numerical utility). Binding ADRs 0001, 0002, 0006.
Decision requested: approve behavior for this PR and the squash-merge. No release requested.

## What changes and why

Before: nothing filled `astra_sim_to_sw_v1`; `cas`, `ax/ay/az`, `lat/lon` and `status_flags` had no definition.
After: `astra::icd_pack::pack_sim_to_sw` (new header-only `core/include/astra/util/icd_pack.hpp`) fills the whole
struct from the `StepContext`, frame-start `VehicleState`, `EnvironmentSample`, `EarthModel` and an optional
previous-frame wrench and mass. It is `noexcept`, allocation-free, and zeroes the struct with `memset` first.
Not wired into `Simulation` (PR 3).

Field definitions (all also written into `icd/astra_icd.h` comments):

- `t = ctx.t` (exactly `VirtualClock::t()`), Euler angles from `q_nb` (code maps (psi,theta,phi) to the ICD's
  (phi,theta,psi)), `p/q/r` = `omega_b` (inertial-relative, per your note), `u/v/w` = ground-relative `v_b`.
- `alpha/beta/tas/mach` from the air-relative velocity; `cas` from the subsonic compressible relation with
  `a0 = sqrt(1.4 * (8314.32/28.9644) * 288.15)` (same constants as isa1976), `p0 = 101325`.
- `vertical_speed = -(C_bn v_b)_z`; the header comment records that this equals the Earth model's altitude rate
  for flat, round and wgs84_rotating (checked against the real plugins in a test).
- `ax/ay/az` = previous frame's k1-stage `wrench_total` force / mass (specific force, gravity excluded, one-frame lag).
- Five `status_flags` bit `#define`s in `icd/astra_icd.h` (your authorization): 1 accel invalid (frame 0, no previous,
  non-finite force, non-finite or non-positive mass), 2 lat/lon not modeled (flat Earth), 4 atmosphere invalid
  (mach, cas = 0), 8 CAS supersonic (cas = 0 only), 16 wind non-finite (tas, alpha, beta, mach, cas = 0).
  Every lost field is a defined 0, never NaN.

Diff (`git diff --stat a3d6071 6965165`): 4 files, +781/-10: `icd_pack.hpp` (new, 139), `astra_icd.h` (comments and
defines only, no struct, version or static-assert line touched), `icd_pack_test.cpp` (new, 594, 25 tests),
`tests/unit/CMakeLists.txt` (+1). No golden, tolerance or ADR touched. Read-only use of the human-owned
`tests/golden/frames/adr0001_cases.hpp`.

## Evidence

| Check / acceptance | Result / date / duration | Log or retained artifact / candidate |
|---|---|---|
| A2 part 1: `cmake --build --preset asan --parallel && ctest --preset asan -R IcdPack --no-tests=error` | pass, 25/25, exit 0 (run on the pre-format tree, then re-run on `6965165` after the lint fix: 25/25) | this session; the tests also run inside the full gate below |
| A2 part 2: `./scripts/check-full.sh` on `6965165` | **pass**: `check: OK`, `exit=0` | [6965165-full-gate.log](evidence/6965165-full-gate.log) |
| Earlier full gate on `8413365` | **failed** at `format-check` (exit 123), files not clang-formatted | [8413365-full-gate-FAILED-format.log](evidence/8413365-full-gate-FAILED-format.log) |
| Earlier full gate on `c439661` | **failed** at `tidy` (exit 1): 8 clang-tidy errors, all in `icd_pack_test.cpp` (unnamed parameters, constant case, loop to `ranges::all_of`) | [c439661-full-gate-FAILED-tidy.log](evidence/c439661-full-gate-FAILED-tidy.log) |
| Mutation check of the oracles | 5 mutants each fail 2 to 4 tests: vertical-speed sign, conjugate quaternion (q_nb/q_bn swap), az sign, `340.294` literal for a0, phi/psi swap | run once from a scratch script, not retained in the repo |
| Codex adversarial review | approve, no findings | [6965165-m4-pr2-codex-adversarial.log](evidence/6965165-m4-pr2-codex-adversarial.log) |

The two failed gates were lint only (no test failed) and are counted as two failed integrations; both fixes were
mechanical and confined to the new test file. CI is local-gate mode, so a green GitHub check is not a full-gate
result. Note: the background task wrapper reported exit 0 for the failed runs because a trailing `echo` ran last;
the logs' own `exit=` lines are what were read.

Oracle provenance: attitude and rotation direction come from the golden ADR-0001 `composite` and
`composite_neg` cases (numpy); beta and vertical-speed signs from single-axis physical cases; CAS anchored three
ways: NACA 1135 / Anderson isentropic table value `p0/p = 1.5243` at M 0.8 (tolerance 0.02 m/s from the table's
5-digit rounding, derived in the test), an ISA 10 km value evaluated in Python (`146.98437733646873 m/s`), and the
sea-level `CAS == TAS` identity against the real isa1976 plugin plus a low-Mach EAS bound of `M^2/8`.
The oracle's `1e-11` tolerance (R12b's proposed 1e-12 belongs to the later driver test, not this PR).

## Independent review

Claude built; Codex (companion 1.0.6, thread `01a0d771-f8f3-7f93-8ec9-f35fb61b7fc9`) reviewed the committed branch
diff `a3d6071..6965165` in one adversarial pass with a numerical focus (frames, signs, units, CAS constants,
invalid-input handling, oracle independence, false comments). Verdict approve, no findings. Codex did not run builds
or tests, so its statement is a reading, not a repeat of the gate. Fix rounds: 0. Open critical/major/minor: none.

## Joe's attention

- **Textbook citation mismatch (your amendment 1).** I checked `~/repos/ASTRA/docs/refs/textbooks/` (Johnson, Lewis,
  Stevens 2015). In that edition Eq. 1.3-10 is the DCM, 1.3-11 is Euler angles from the DCM, 1.8-16 is the quaternion
  vector rotation, 1.8-18 is q to DCM, and 1.8-9a/10a is the axis-angle quaternion; the Euler-to-quaternion half-angle
  form is not printed. The test header cites those real numbers. The human-owned golden's header cites 1.3-11 for the
  DCM and 1.8-16 for the half-angle form, so it disagrees; I did not touch it. Your call whether to fix that comment.
- `icd_pack.hpp` duplicates the isa1976 sea-level constants (file-local there). The plugin identity test would catch
  a drift, but there is no shared source of truth. Left as is to avoid a core-to-plugin dependency.
- `PreviousFrame` is a struct the caller (PR 3) must fill from the published `v<i>.wrench_total.*` signals and the
  mass at frame k-1; PR 3 must pass null at frame 0. This is the main integration risk for PR 3.
- Mach >= 1 gives `cas = 0` plus bit 8. The notional transport cruises subsonic, so this is unexercised in flight.
- Rollback: additive, no callers; reverting the commits restores today's behavior. The bit `#define`s are ABI once
  PR 5's autopilot reads them.

## Decision record

State: pending
Human decision/date/reference: pending
Approved candidate/scope: pending
Merge observed: not merged
Release authorization: not requested

Significant tradeoff rationale: bit constants live in `astra_icd.h` (your decision) so the C++17 autopilot need not
copy magic numbers; the cost is that the ICD header is no longer comment-only.
Accepted risks: duplicated ISA constants (owner: Joe; trigger: any change to isa1976's constants).
Nonblocking follow-ups: PR 5 adds its own constants plus a test including both headers and `static_assert`ing equality
(your instruction); optional fix of the golden header's equation numbers (requires your authorization, `tests/golden/**`).
Architectural decisions: none new; binds ADR-0001, ADR-0002, ADR-0006.
