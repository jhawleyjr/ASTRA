# M4 PR 5 gain design evidence

Evidence for `embedded/autopilot/gains.hpp` (plan `docs/plans/m4-pr5-autopilot.md` D7). Design model:
`tests/python/autopilot_design.py` (PR 8's 5x5 altitude augmentation plus the 9-state lateral block, cruise trim at
230 m/s and 10 km). The design model has no saturation and no turbulence; PR 6's nonlinear runs decide. Two rounds:
round 0 (candidate `56ea2ec`) and correction round 1 (Joe's human review, candidate `fc22cc8`).

## Method

A Nelder-Mead search (session scratchpad, not in the repo), several random restarts, minimizing penalties for missing
stricter targets than the test asserts (largest real part <= -0.05 1/s, damping >= 0.4, loop-at-a-time phase margin
>= 60 degrees, gain margin >= 10 dB both directions, sampled max |z| <= 0.9999), rounded to three figures. The test
`tests/python/test_autopilot_design.py` then asserts the plan's thresholds (phase margin 45 degrees, gain margin 6 dB,
damping 0.30, largest real part -0.03 1/s, plus the round-1 criteria below) on the rounded gains that ship.

## Longitudinal (round 0, unchanged in round 1)

Elevator: altitude PID plus theta and q feedback; throttle: tas PI. Search bounds: kAltP 2e-3, kAltI 1e-4, kAltD 2e-2,
kPitchTheta 2, kPitchRate 2, kSpeedP 0.1, kSpeedI 0.02. Result: kAltP 1.54e-4, kAltI 7.97e-6, kAltD 2.24e-4,
kPitchTheta 0.389, kPitchRate 0.184, kSpeedP 0.0353, kSpeedI 7.69e-4. None sits at its bound.

**Pitch inner loop versus the altitude and speed loops (round 1 check; gains unchanged because the pitch loop is
clearly faster).** Pitch loop alone (altitude and speed loops open): short-period pair -0.586 +/- 1.129j (wn 1.272
rad/s, zeta 0.461, time constant 1.7 s), plus two slower real poles (-0.254, -0.0173) that are speed/altitude
dynamics, not attitude. Full closed loop, poles other than the short period: -0.0497 +/- 0.0239j, -0.0510 +/- 0.0285j,
-0.1516; the fastest is 0.152 rad/s. Ratio 1.272 / 0.152 = **8.4** (criterion >= 3, and short-period real part <= -0.5).

## Lateral, round 0 (superseded; candidate `56ea2ec`)

kHeadingP 2.0, kHeadingI 0.0343, kBankP 0.0550, kRollRate 0.243, kYawRate 0.359, kSideslip +0.269. Search: the other five
lateral gains re-optimized at each kHeadingP (bounds kHeadingI 0.3, kBankP 3, kRollRate 3, kYawRate 3, kSideslip 3) with
a small-gain preference. Recomputed for this record on those gains with the round-1 helper:
- Full lateral poles: -1.832, -0.680 +/- 1.550j, -0.0502, -0.0499 +/- 0.0119j.
- **F2:** bank loop alone (heading loop open): -1.831, -0.679 +/- 1.553j, **-0.153** (time constant 6.5 s); heading-loop
  poles -0.0502 and -0.0499 +/- 0.0119j (|lambda| 0.0513); bank/heading bandwidth ratio **2.99**, i.e. the inner loop was
  no faster than the outer one. Aileron demand at a full-scale (0.35 rad) bank error: 0.019 rad (5 percent of the limit).
- **F1:** effective `cn_beta` = 0.26 + (-0.20)(0.269) = **0.206**, below the open-loop 0.26; the rudder's only finite upward
  gain margin (+17.2 dB) came from that sign.

## Lateral, correction round 1 (candidate `fc22cc8`)

Changes to the search: no small-gain preference on the inner loop; `kBankP <= 0.6` (a full 0.35 rad bank error asks for
at most 60 percent of the 0.35 rad aileron limit); `kRollRate, kYawRate <= 2`; and an inner-loop criterion,
**the bank-attitude loop alone has every pole at Re <= -0.6 1/s** (search target; the test asserts <= -0.5, a time
constant of 2 s) with bandwidth at least 3x the heading loop's. The sideslip term was searched both ways:
- `kSideslip` free with the stiffening sign (`kSideslip <= 0`, bound 3, first pass with `kBankP <= 1` and a -0.7 bank
  target): the search pushed it to -3.0 at kHeadingP 1.0 and -2.2 at 1.5 (effective `cn_beta` 0.86 and 0.71) without any
  floor asking for it, spending rudder authority on nothing the criteria required.
- No sideslip term: meets every target with interior gains. **Chosen: no sideslip term (a pure yaw-rate damper).**

Sweep of kHeadingP with the other four lateral gains re-optimized (no sideslip term, `kBankP <= 0.6`):

| kHeadingP | cost | largest real part | min damping | bank-loop poles alone | note |
|---|---|---|---|---|---|
| 1.5 | 1.88 | -0.0312 | 0.433 | Re <= -0.6 | misses the -0.05 target; `kBankP` at its cap |
| 2.0 | 0.66 | -0.0434 | 0.401 | Re <= -0.6 | misses the -0.05 target; `kBankP` at its cap |
| **2.5** | 0.0000 | -0.0514 | 0.460 | -1.341, -1.023, -0.866 +/- 1.712j | **smallest tried value meeting every target** |
| 3.0 | 0.0000 | -0.0653 | 0.448 | -1.58, -0.849, -0.84 +/- 1.723j | meets; bank command saturates at 6.7 degrees |
| 4.0 | 0.0000 | -0.0595 | 0.477 | -1.579, -0.888 +/- 1.70j, -0.815 | meets; saturates at 5.0 degrees |

(An earlier pass of the same search with `kBankP <= 1.0` and a -0.7 bank target pushed `kBankP` to that bound at
kHeadingP 1.0 to 1.5; the 0.6 cap with a -0.6 target replaced it so the shipped `kBankP` is interior.)

Shipped lateral gains (kHeadingP 2.5, rounded): kHeadingI 0.0816, kBankP 0.394, kRollRate 0.360, kYawRate 0.494. Not at a
search bound: `kBankP` 0.394 < 0.6, `kRollRate` 0.360 < 2, `kYawRate` 0.494 < 2, `kHeadingI` 0.0816 < 0.3; `kHeadingP` = 2.5 is
the smallest of the tried grid (1.5, 2.0, 2.5, 3.0, 4.0) that meets the targets, and larger values only shrink the
bank command's linear range (0.35 / kHeadingP: 8.0 degrees at 2.5).

## Results on the shipped gains, from `pytest tests/python/test_autopilot_design.py -s` (`fc22cc8-a5-pytest.log`)

- Open-loop 5x5 phugoid (helper cross-check): 108.5343 s, zeta 0.02730 (PR 8 recorded 108.53 s, 0.0273).
- Longitudinal poles: real parts -0.573, -0.152, -0.0510 (pair), -0.0497 (pair); min damping 0.457; largest real part
  -0.0497.
- **Lateral poles (full closed loop):** -1.415, -0.876 +/- 1.690j (Dutch roll: wn 1.904 rad/s, zeta 0.4603, zeta*wn
  0.876), -0.825, -0.0514 +/- 0.0305j. Largest real part -0.0514; minimum damping 0.460.
- **Bank loop and heading loop reported separately (F2):**
  - bank-attitude loop alone (heading loop open): -1.342, -1.022, -0.866 +/- 1.712j; slowest real part -0.866, time
    constant **1.16 s** (criterion <= 2 s);
  - heading-loop poles (the two slowest of the full system): -0.0514 +/- 0.0305j, |lambda| 0.0598;
  - bank/heading bandwidth ratio **14.5** (criterion >= 3).
- **Yaw damper (F1):** effective `cn_beta` = 0.26 (no sideslip term; criterion: not below the open-loop 0.26). With the
  round-0 value (+0.269) the same check gives 0.2062 and fails (`test_effective_cn_beta_check_can_fail`, and the
  mutation in `fc22cc8-a5-mutations.md`).
- Aileron demand at a full-scale bank error: 0.138 rad, 39 percent of the 0.35 rad limit (criterion: at most 60 percent).
- MIL-F-8785C Category B Level 1 lower bounds: Dutch roll zeta 0.4603 >= 0.08, zeta*wn 0.876 >= 0.15, wn 1.904 >= 0.4;
  short period zeta within 0.30 to 2.00; every slower oscillation zeta >= 0.04 (asserted by the test).
- Loop-at-a-time margins (other channels closed): elevator phase margin 92.7 degrees; throttle 69.6; aileron 93.9; rudder
  106.8; every gain-margin direction is now unlimited (no 0-degree crossing), including the rudder, whose finite +17.2 dB
  in round 0 came from the destabilizing sideslip sign.
- Sampled closed loop (ZOH at the 10 ms exchange period, command applied in the frame it is computed, no extra delay): max
  |z| 0.999498 (longitudinal), 0.999486 (lateral); with one extra exchange of delay (conservative, reported only)
  0.999499 and 0.999486.

Limits of this evidence: margins are loop-at-a-time (not simultaneous multi-loop); the thresholds and inner-loop
criteria are provisional, accepted by Joe (the conventional source, MIL-F-9490D, is not in `docs/refs` and is less strict
below 0.06 Hz); nothing here covers saturation, the bank clamp, or turbulence.
