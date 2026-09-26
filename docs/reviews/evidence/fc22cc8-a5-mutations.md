# M4 PR 5 mutation checks, correction round 1 (candidate `fc22cc8`)

Each mutation was applied to one file of the product sources on the round-1 tree (before the `fc22cc8` gate), the
affected targets rebuilt (asan preset), `ctest --preset asan -R '(Autopilot|EmbeddedBoundary)'` run, and the file
restored. Drivers: session scratchpad `mutate.py` (C++ sources) and `mutate_gains.py` (gains file, pytest); not in the
repo. "Caught" means at least one test failed. The round-0 table for `56ea2ec` is in `56ea2ec-a5-mutations.md`; all
of its mutations were re-run here and are caught again.

## New in round 1

| Mutation | Result | Failing tests |
|---|---|---|
| **F1** beta term with the destabilizing sign: `rudder = kYawRate r + 0.269 beta` (the round-0 defect) | caught | Autopilot.RudderNeverWeakensTheWeathervaneStability, Autopilot.BetaIsNotReadSoANonFiniteBetaDoesNotHold |
| beta term with the stiffening sign: `rudder = kYawRate r - 0.269 beta` (allowed physically, but not the shipped law) | caught | the same two (the rudder must ignore beta today) |
| yaw damper sign flipped: `rudder = -kYawRate r` | caught | Autopilot.RudderYawDamperOpposesYawRate |
| `beta` read again by the hold mask | caught | Autopilot.BetaIsNotReadSoANonFiniteBetaDoesNotHold |
| `--airspeed-mps <= 0` accepted | caught | AutopilotCli.RejectsBadNumbers, AutopilotProcess.BadCommandLinesExitOneWithoutTouchingTheHalOrPrintingASummary |
| **F1 in the design test:** `kSideslip = 0.269` added to `gains.hpp` | caught | test_closed_loop_effective_cn_beta_is_not_below_open_loop (`assert 0.2062 >= 0.26`), and only that test |
| **F2:** `kBankP = 0.055` (the round-0 slow inner loop) | caught | test_closed_loop_meets_the_pole_floors, test_bank_loop_is_fast_and_separated_from_the_heading_loop |
| `kBankP = 0.9` (aileron headroom lost) | caught | test_bank_loop_leaves_aileron_headroom_at_a_full_scale_bank_error (`0.315 <= 0.6 * 0.35`) |
| `kHeadingP = 1.0` (slow heading pole) | caught | test_closed_loop_meets_the_pole_floors (`-0.0167 <= -0.03`) |
| `kPitchRate = 0.02` (slow, underdamped pitch loop) | caught | test_pitch_loop_is_separated_from_the_altitude_and_speed_loops |
| `kYawRate` sign flipped | caught | pole floors, Dutch roll, sampled loop, bank-loop tests |

## Re-run from round 0 (all caught)

dt ignoring timestamps; elevator sign; output clamp removed; anti-windup removed; `WIND_INVALID` not held;
`ATMOSPHERE_INVALID` also held; `t_prev` frozen on held frames; heading not wrapped; bank command not clamped; `theta`
feedback on absolute theta; `mode_flags` zero; exit status remapped; a clock read in the loop; an `astra_core` header in
the include closure. The autopilot linking `astra::core` (round 0: `EmbeddedBoundary_autopilot_links_no_astra_core`) was not
re-run: no CMake or linking file changed in round 1.

The weakened-elevator liveness case for the margin machinery (`test_margin_analysis_can_fail`) still passes: a tenth of
the elevator gains is stable with a phase margin below 45 degrees.
