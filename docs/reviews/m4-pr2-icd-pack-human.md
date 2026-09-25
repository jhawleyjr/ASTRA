# Human review packet: M4 PR 2, ICD truth packing

PR / candidate base and head: base `a3d6071` (`origin/main` at branch time, #51's merge commit; re-check
before merge). Product candidate `3322d26` (code, tests, ICD header, correction rounds 1 and 2); evidence and packet
commits on top add only retained logs and this document. Branch `worktree-m4-pr2-icd-pack`, not pushed.
Spec/plan/task: [M4 plan](../plans/m4-lockstep.md) PR 2 row, R11, R12a, Q11; approved plan for this PR
with Joe's eight amendments (2026-09-25) and his six correction-round-1 items (below).
Risk: Tier 3 (`icd/` semantics, numerical utility). Binding ADRs 0001, 0002, 0006.
Decision requested: approve behavior for this PR and the squash-merge. No release requested.

## What changes and why

Before: nothing filled `astra_sim_to_sw_v1`; `cas`, `ax/ay/az`, `lat/lon` and `status_flags` had no definition.
After: `astra::icd_pack::pack_sim_to_sw` (new header-only `core/include/astra/util/icd_pack.hpp`) fills the whole
struct from the `StepContext`, frame-start `VehicleState`, `EnvironmentSample`, `EarthModel` and an optional
previous-frame wrench and mass. It is `noexcept`, allocation-free, and zeroes the struct with `memset` first.
Not wired into `Simulation` (PR 3).

Field definitions (written in words in `icd/astra_icd.h`; the implementation pointers live in `icd_pack.hpp`):

- `t = ctx.t` (exactly `VirtualClock::t()`), Euler angles from `q_nb` (code maps (psi,theta,phi) to the ICD's
  (phi,theta,psi)), `p/q/r` = `omega_b` (inertial-relative), `u/v/w` = ground-relative `v_b`.
- `alpha/beta/tas/mach` from the air-relative velocity; `cas` from the subsonic compressible relation with
  `a0 = sqrt(1.4 * (8314.32/28.9644) * 288.15)` (same constants as isa1976), `p0 = 101325`.
- `vertical_speed = -(C_bn v_b)_z`; the header comment says it equals the Earth model's altitude rate, and a test
  checks that against the real flat, round and wgs84_rotating plugins.
- `ax/ay/az` = previous frame's first-stage total force / mass (specific force, gravity excluded, one-frame lag).
- Five `status_flags` bit `#define`s in `icd/astra_icd.h` (Joe's decision): 1 accel invalid (frame 0, no previous,
  non-finite force, non-finite or non-positive mass), 2 lat/lon not modeled (flat Earth), 4 atmosphere invalid
  (mach, cas = 0), 8 CAS supersonic (cas = 0 only), 16 wind non-finite (tas, alpha, beta, mach, cas = 0).
  Every lost field is a defined 0, never NaN.

Diff (`git diff --stat a3d6071 3322d26`): `icd_pack.hpp` (new), `astra_icd.h` (comments and defines only, no struct,
version or static-assert line touched), `icd_pack_test.cpp` (new, 25 tests), `tests/unit/CMakeLists.txt` (+1). No
golden, tolerance, ADR or `frames.hpp` touched. Read-only use of the human-owned
`tests/golden/frames/adr0001_cases.hpp`.

## Correction round 1 (Joe's six items, applied in `cdc9867`; item 1 corrected again in `3322d26`)

1. **CAS anchor.** The sea-level table test was non-discriminating (at p = p0 the forward and inverse relations
   cancel, so `cas = M a0` for any matched exponent pair). Replaced by `CasMatchesPublishedIsentropicTableAtTenKilometers`:
   NACA 1135 Table I "Subsonic flow" prints p/pt (not pt/p): 0.6560 at M 0.80, 0.8807 at M 0.43, 0.8755 at M 0.44.
   At ISA 10 km, `qc = (1/0.6560 - 1) * 26436 = 13862.78 Pa`, target p/pt = 0.879651, interpolated in the printed
   column to Mc = 0.432018, expecting `cas = Mc * a0 = 147.013 m/s`; tolerance 0.06 m/s, derived in the test from the
   rows' rounding (9.8e-5 Mach), the .6560 rounding (4.6e-5) and the interpolation error (2.0e-5). The
   implementation gives 146.984 (0.029 m/s below, inside the budget; the combined table rounding and interpolation,
   of which the .6560 rounding alone is about 0.007 m/s).
   **Provenance history (a defect, corrected in round 2):** the first version of this anchor quoted pt/p rows
   (1.5243, 1.1355, 1.1422) that I had generated from the closed form and described as "printed"; Codex found Table I
   tabulates p/pt. I then downloaded the NTRS scan (document 19930091059) and transcribed the three p/pt values from
   its text layer; each agrees with `(1 + 0.2 M^2)^-3.5` to every printed digit. The scan is OCR text, so please
   spot-check the three values against the page image.
2. **Textbook.** The half-angle formula is printed in Example 1.8-1 Eq. (1), p. 52 (my earlier "not printed" was
   wrong: I searched for stacked-fraction text and missed it). Test header now cites 1.3-10 (DCM), 1.3-11 (Euler from
   DCM), 1.8-10a, Example 1.8-1 Eq. (1), 1.8-16 (q to DCM array form) and 1.8-18 (expansion).
3. **Bit constants.** The contradictory "PR 5 adds its own constants plus a static_assert test" follow-up is removed;
   the `#define`s in `astra_icd.h` are the single definition.
4. **Sea-level test comment.** Fixed: a 340.294 literal is 1.1e-4 m/s below a0 = 340.29411; R = 8314.32/28.9644 =
   287.0531 (sqrt(1.4 * 287.05 * 288.15) = 340.2923 was the wrong figure I had quoted).
5. **ABI header.** `astra_icd.h` comments no longer contain `round.cpp:97`, `VirtualClock::t()` or
   `v<i>.wrench_total.*`; definitions are in words, pointers moved to `icd_pack.hpp`.
6. **Failed logs.** The two FAILED gate logs are removed from `docs/reviews/evidence/`; their rows stay below.

## What each CAS/air-data test actually discriminates

| Test | Discriminates | Does not |
|---|---|---|
| `CasMatchesPublishedIsentropicTableAtTenKilometers` | The exponent pair and factors of the relation (published-table inversion at altitude). Mutants forward 3.5 to 3.4, inverse 2/7 to 0.29, and a changed-gamma pair all fail it. | Sea-level constants alone; supersonic. |
| `CasAtTenKilometersHandComputed` | Implementation slips against the same closed form evaluated in Python (typos, wrong constant). | A wrong relation. |
| `CasEqualsTasAtIsaSeaLevelAgainstTheRealPlugin` | Sea-level constants `p0`, `a0` against the real isa1976 plugin (the 340.294 literal fails it). | The exponents (they cancel at p0). |
| `LowMachCasApproachesEquivalentAirspeed` | The low-Mach limit CAS to EAS within `M^2/8`; also failed the exponent mutants above. | Fine exponent errors below that bound. |

## Evidence

| Check / acceptance | Result | Log or candidate |
|---|---|---|
| A2 part 1: `cmake --build --preset asan --parallel && ctest --preset asan -R IcdPack --no-tests=error` | pass, 25/25, on the `3322d26` tree | this session |
| A2 part 2: `./scripts/check-full.sh` on `3322d26` | **pass**: `check: OK`, `exit=0` | [3322d26-full-gate.log](evidence/3322d26-full-gate.log) |
| Full gate on `cdc9867` (round-1 candidate) | pass: `check: OK`, `exit=0`; superseded by round 2 | [cdc9867-full-gate.log](evidence/cdc9867-full-gate.log) |
| Full gate on `6965165` (round-0 candidate) | pass: `check: OK`, `exit=0`; superseded by the round-1 candidate | [6965165-full-gate.log](evidence/6965165-full-gate.log) |
| Failed gate, candidate `8413365` | failed at `format-check`, exit 123 (files not clang-formatted); log removed per Joe | none |
| Failed gate, candidate `c439661` | failed at `tidy`, exit 1 (8 clang-tidy errors, all in `icd_pack_test.cpp`); log removed per Joe | none |
| Mutation check | round 0: 5 mutants (vertical-speed sign, conjugate quaternion, `az` sign, `340.294` literal, phi/psi swap), each fails 2 to 4 tests. Rounds 1 and 2: 3 CAS-exponent mutants (forward 3.5 to 3.4, inverse 2/7 to 0.29, changed-gamma pair), each fails the altitude table test, re-run against the printed-p/pt version | scratch scripts, not retained |
| Codex adversarial review, round 0 (`a3d6071..6965165`) | approve, no findings | [6965165-m4-pr2-codex-adversarial.log](evidence/6965165-m4-pr2-codex-adversarial.log) |
| Codex fix-verification of Joe's round 1 (`6965165..cdc9867`, head `c29c8d7`) | needs-attention: items 2 to 6 hold, item 1 partial, one medium finding (below) | [c29c8d7-m4-pr2-codex-fixverify.log](evidence/c29c8d7-m4-pr2-codex-fixverify.log) |
| Codex fix-verification of round 2 (`cdc9867..3322d26`) | approve: F-1 resolved, new findings none (Codex recomputed qc, target ratio, Mc and CAS 147.013208, confirmed the three p/pt entries against the NTRS scan, and that exponent mutants give 144.5, 148.1 and 146.7 m/s, all outside 0.06) | [77f75e4-m4-pr2-codex-fixverify2.log](evidence/77f75e4-m4-pr2-codex-fixverify2.log) |

The two failed gates were lint only (no test failed): failed integrations 2/2. The background task wrapper reported
exit 0 for those runs because a trailing `echo` ran last; the logs' own `exit=` lines are what were read. CI is
local-gate mode, so a green GitHub check is not a full-gate result.

Oracle provenance: attitude and rotation direction come from the golden ADR-0001 `composite` and `composite_neg`
cases (numpy); beta and vertical-speed signs from single-axis physical cases; CAS as in the table above. The `1e-11`
tolerance is this PR's; R12b's proposed 1e-12 belongs to the later driver test.

## Independent review

Claude built; Codex (companion 1.0.6) reviewed. Round 0: one adversarial pass over `a3d6071..6965165`, approve, no
findings (Codex did not run builds or tests). Correction round 1 (Joe's six items, above): Codex fix-verification over
the delta `6965165..cdc9867` with an explicit instruction to judge whether each oracle can discriminate, not just pass.
Result: needs-attention. Items 2 to 6 HOLD; item 1 PARTIAL. One medium finding, **F-1** (`icd_pack_test.cpp` CAS
table anchor): NACA 1135 Table I tabulates p/pt, not the pt/p rows quoted, so the anchor's independent-provenance claim
was false and the tolerance was not derived from real table entries (also: `qc` was misstated as 13862.4, and `f'` was
quoted as a lower bound when it varies 0.659 to 0.677). Verified against the scan: the finding is correct. **Fixed in
correction round 2 (`3322d26`)** by transcribing the printed p/pt values and re-deriving the tolerance from them (item 1
above). Codex's nonblocking observations, not acted on: `NonPositivePressureFlagsAtmosphere` covers zero pressure only
(not negative pressure or a bad speed of sound), `SupersonicFlags...` tests M = 1.1 but not the M = 1 boundary, and
`LowMachCasApproachesEquivalentAirspeed` cannot reject a small wrong-sign compressibility correction within its bound.
Round 2 verification (`cdc9867..3322d26`, head `77f75e4`): approve, F-1 resolved, no new findings. Correction rounds used: 2 of 2 (round 1 was Joe's list, round 2 fixed F-1); a new
defect from here goes to Joe.

## Joe's attention

- **Follow-up (one item, not in this PR):** correct the equation citations in `core/include/astra/util/frames.hpp`
  and in the human-owned `tests/golden/frames/adr0001_cases.hpp` together; the golden header cites 1.3-11 for the DCM
  and 1.8-16 for the half-angle form, and the right numbers are 1.3-10, Example 1.8-1 Eq. (1) (p. 52) and 1.8-16 for
  q to DCM. The golden part needs your authorization.
- `icd_pack.hpp` duplicates the isa1976 sea-level constants (file-local there). The plugin identity test catches a
  drift, but there is no shared source of truth. Left to avoid a core-to-plugin dependency.
- `PreviousFrame` is a struct the caller (PR 3) must fill from the published `v<i>.wrench_total.*` signals and the
  mass at frame k-1; PR 3 must pass null at frame 0. This is the main integration risk for PR 3.
- Mach >= 1 gives `cas = 0` plus bit 8. The notional transport cruises subsonic, so this is unexercised in flight.
- Rollback: additive, no callers; reverting the commits restores today's behavior. The bit `#define`s are ABI once
  the autopilot reads them.

## Decision record

State: approved
Human decision/date/reference: Joe, 2026-09-25, approved in session ("Approved: candidate 3322d26 (evidence/packet head 1e63268), M4 PR 2 as scoped"); the p/pt spot-check is accepted on the closed-form agreement plus Codex's scan check. Earlier this session he had requested six corrections (applied), which were followed by one Codex-found fix (round 2).
Approved candidate/scope: product candidate `3322d26`; evidence and packet head `1e63268` (docs only on top of it).
Merge observed: not merged (Joe squash-merges)
Release authorization: not requested

Significant tradeoff rationale: bit constants live in `astra_icd.h` (Joe's decision) so the C++17 autopilot need not
copy magic numbers; the cost is that the ICD header is no longer comment-only.
Accepted risks: duplicated ISA constants (owner: Joe; trigger: any change to isa1976's constants); NACA table values
transcribed from an OCR text layer, not read from the page image (owner: Joe's spot-check).
Nonblocking follow-ups (no further round on this PR):
(a) CAS table test comment, to fix in a follow-up: the `|g''|` bound should read 0.808 (max about 0.8074); the 0.029 m/s gap between the implementation and the printed-value result is mostly the M 0.43/0.44 row rounding (about 0.026), with the .6560 rounding about 0.007 and interpolation about -0.004 (the packet's earlier wording blaming .6560 alone was wrong); and state that the "changed-gamma pair" mutant means matched exponents 3.4 and 1/3.4 with the 0.2 and 5 factors held, and that a fully consistent gamma change (1.41) moves CAS by only 0.010 m/s, which the table test does not catch and which is physically negligible.
(b) Separate Tier 0 PR after this merges (authorized by Joe): correct the equation citations in `core/include/astra/util/frames.hpp` and in the comments of `tests/golden/frames/adr0001_cases.hpp` (comment-only in the golden file, no data change; Joe arms the golden guard for that session). Correct numbers, 2015 edition: DCM Eq. (1.3-10), Euler from DCM Eq. (1.3-11), half-angle Euler-to-quaternion Example 1.8-1 Eq. (1) p. 52, q to DCM Eq. (1.8-16)/(1.8-18).
Architectural decisions: none new; binds ADR-0001, ADR-0002, ADR-0006.
