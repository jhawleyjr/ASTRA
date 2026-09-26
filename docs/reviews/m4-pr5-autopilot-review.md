# Review: M4 PR 5, embedded/autopilot and the standalone embedded build

Spec/plan: [PR 5 plan](../plans/m4-pr5-autopilot.md)
Builder: Claude (Sonnet 5, model ID `claude-sonnet-5`)
Reviewer: Joe's human review of `56ea2ec` (round 0 findings H-1, H-2, H-3); Codex, fresh context, for the
fix verification (see Verification fields and the Completion section)
Scope: base `34eec2d`; round-0 candidate `56ea2ec`; correction round 1 candidate `fc22cc8` (fix delta
`56ea2ec..fc22cc8`, product and tests), later commits are evidence documents only
Validation (round 1, candidate `fc22cc8`): `./scripts/check-full.sh` exit 0, `check: OK`
([log](evidence/fc22cc8-full-gate.log)); A5 commands separately, all exit 0
([standalone](evidence/fc22cc8-a5-standalone.log), [ctest 45/45](evidence/fc22cc8-a5-ctest.log),
[pytest 19/19](evidence/fc22cc8-a5-pytest.log)); [mutation checks](evidence/fc22cc8-a5-mutations.md)
Round: fix 1 (correction rounds 1 of 2; failed integrations 1 of 2, from round 0)

## Finding H-1

Severity: major
Status: verified
Location: `embedded/autopilot/autopilot.cpp` (rudder law) and `gains.hpp` (`kSideslip`) at `56ea2ec`;
`tests/embedded/autopilot_test.cpp` `RudderYawDamperOpposesYawRateAndSideslip`
Requirement/ADR: plan D3/D7 (yaw damper); `aero_math.hpp:146` yaw moment
`N = q S b (cn_beta beta + cn_rudder delta_r + ...)`, `cruise.yaml` `cn_beta = +0.26`, `cn_rudder = -0.20`
Failure: `rudder = kYawRate r + kSideslip beta` with `kSideslip = +0.269` commands nose-left rudder for `beta > 0`,
a negative yaw moment against the positive weathervane moment. Effective `cn_beta` falls from 0.26 to 0.206. The rudder
loop's only finite upward gain margin (+17.2 dB) came from that sign. The test pinned `rudder > 0` for `beta > 0`
under a name saying it opposes sideslip.
Requested outcome: constrain the beta term to the stiffening sign or drop it; rewrite the test to assert the physical
meaning through `cn_rudder`'s sign; add a design-test assertion that closed-loop effective `cn_beta` is not below open
loop; add a mutation check flipping the beta sign; say which was chosen and why.
Builder response: **fixed; the beta term was dropped** (a pure yaw-rate damper). The search showed every floor and the
new inner-loop criterion are met without it, while the stiffening-sign variant only spent rudder authority (the search
drove it to its bound with no criterion asking for it). Changes: `kSideslip` removed from `gains.hpp`; the law is
`rudder = clamp(kYawRate r)`; `beta` is no longer a hold-mask field; the old test is replaced by
`RudderYawDamperOpposesYawRate` (asserts `cn_rudder * rudder < 0` for `r > 0`, `> 0` for `r < 0`, with the sign
convention in a comment) and `RudderNeverWeakensTheWeathervaneStability` (the moment times `beta` is never negative
for either sign of beta, and the rudder ignores beta today); `BetaIsNotReadSoANonFiniteBetaDoesNotHold`. The design
helper reads `cn_beta`/`cn_rudder` from `cruise.yaml` and `test_closed_loop_effective_cn_beta_is_not_below_open_loop`
asserts effective `cn_beta` >= open loop (it accepts an optional `kSideslip` in `gains.hpp`, so adding the round-0 value
fails it: `0.2062 >= 0.26`, and only that test). Mutations: see [`fc22cc8-a5-mutations.md`](evidence/fc22cc8-a5-mutations.md).
Verification: Codex adversarial verification of `56ea2ec..a0eec8c` (thread `01a0dee1-c13a-71d1-a3be-02b89dd1018f`, [log](evidence/a0eec8c-codex-verify.log)), verdict approve: H-1 fix holds: positive rudder gives a negative yaw moment, removing the +0.269 beta term removes the -0.0538 contribution to Cn_beta, no operational sideslip dependency remains (optional helper references exist to support the mutations). Codex noted that 0.26 and 0.2062 are the native (AERORP) coefficients, not total CG derivatives; the moment transfer does not change this feedback increment, so the comparison stands.
History: round 0 found by Joe; round 1 fixed; verified by Codex on `a0eec8c`.
Human exception if needed: none.

## Finding H-2

Severity: major
Status: verified
Location: `embedded/autopilot/gains.hpp` (`kBankP = 0.055`), `tests/python/test_autopilot_design.py` (slowest-pole-only
floor) at `56ea2ec`
Requirement/ADR: plan D7 (gain design); Joe's review criterion: bank-attitude closed-loop time constant of 2 s or less,
or bandwidth at least 3x the heading loop's
Failure: `kBankP = 0.055` gave about 1.1 degrees of aileron for a full 20 degree bank command; the bank loop alone had a
slowest pole at -0.153 (time constant 6.5 s) against heading-loop poles of |lambda| 0.051 (ratio 2.99), i.e. the inner loop
was about as slow as the outer one. Cause: the search's small-gain preference plus a floor that only looked at the
slowest pole of the whole system.
Requested outcome: re-search the lateral gains without the small-gain preference on the inner loop, with the inner-loop
criterion added and asserted; report heading and bank poles separately; do the same separation check for pitch versus
altitude, changing those gains only if the pitch loop is not clearly faster; keep every existing floor and margin and
the `kHeadingP`/bank-limit reasoning current.
Builder response: **fixed.** Lateral gains re-searched (`kHeadingP` 2.5, `kHeadingI` 0.0816, `kBankP` 0.394, `kRollRate`
0.360, `kYawRate` 0.494) with no small-gain preference on the inner loop, the bank-loop criterion (every pole of the
bank loop alone at Re <= -0.6 in the search, asserted at <= -0.5, time constant <= 2 s and bandwidth >= 3x the
heading loop's), and `kBankP <= 0.6` so a full-scale bank error asks for at most 60 percent of the aileron limit
(asserted from `limits.hpp`). Shipped: bank-loop time constant 1.16 s, bank/heading bandwidth ratio 14.5, aileron
demand 39 percent of the limit at a full-scale bank error. Pitch versus altitude/speed: short period 1.272 rad/s
against a fastest outer pole of 0.152 rad/s, ratio 8.4, time constant 1.7 s, so the longitudinal gains are unchanged;
asserted in `test_pitch_loop_is_separated_from_the_altitude_and_speed_loops`. `kHeadingP` = 2.5 is the smallest tried
value (1.5, 2.0, 2.5, 3.0, 4.0) meeting every target and not at a search bound. Evidence:
[gain design](evidence/m4-pr5-gain-design.md).
Verification: Codex adversarial verification of `56ea2ec..a0eec8c` (thread `01a0dee1-c13a-71d1-a3be-02b89dd1018f`, [log](evidence/a0eec8c-codex-verify.log)), verdict approve: H-2 fix holds: the bank and pitch feedback matrices use the right states, actuator columns and signs; pole selection and separation arithmetic match the recorded 1.16 s, 14.5x and 8.4x; fixed dimensions, nonempty reductions, missing-pair exceptions and stability assertions prevent vacuous acceptance. Caveat recorded: these ratios are modal separation measures, not measured frequency-response bandwidths.
History: round 0 found by Joe; round 1 fixed; verified by Codex on `a0eec8c`.
Human exception if needed: none.

## Finding H-3

Severity: minor
Status: verified
Location: `embedded/autopilot/cli.cpp` (`kAirspeed`) at `56ea2ec`
Requirement/ADR: plan D2
Failure: `--airspeed-mps 0` or a negative value was accepted.
Requested outcome: reject `--airspeed-mps <= 0` at parse time (exit 1), with a CLI test.
Builder response: **fixed.** `assign` requires the airspeed to be finite and positive; `AutopilotCli.RejectsBadNumbers`
gains `0`, `-230`, `-0.0`; `AutopilotProcess.BadCommandLinesExit...` gains a zero-airspeed process case (exit 1, empty
stdout). Mutation: accepting `<= 0` again is caught by both.
Verification: Codex adversarial verification of `56ea2ec..a0eec8c` (thread `01a0dee1-c13a-71d1-a3be-02b89dd1018f`, [log](evidence/a0eec8c-codex-verify.log)), verdict approve: H-3 fix holds: finite-and-positive parsing rejects zero, negative values and negative zero before HAL initialization, matching the unit and process tests.
History: round 0 found by Joe; round 1 fixed; verified by Codex on `a0eec8c`.
Human exception if needed: none.

## Completion

Critical/major open: 0 (H-1 and H-2 verified fixed by Codex on `a0eec8c`); minor: H-3 verified. Rejected findings: none.
Minor deferrals: none. Codex's verdict on `56ea2ec..a0eec8c`: approve. New findings from the verification's own
fix-delta, callers and invariants inspection: **none.** Scope: the fix delta and its callers; the round-0 candidate was
reviewed by Codex on `34eec2d..3dc9ca7` (no actionable findings). Not covered: builds and tests (Codex ran none; ours
are the gate and A5 logs), the modal-separation-versus-frequency-response caveat above. Reviewer recommendation is not
Joe's approval.
