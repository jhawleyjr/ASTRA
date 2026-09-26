# M4 PR 5 gain design evidence

Evidence for `embedded/autopilot/gains.hpp` (plan `docs/plans/m4-pr5-autopilot.md` D7). Design model:
`tests/python/autopilot_design.py` (PR 8's 5x5 altitude augmentation plus the 9-state lateral block, cruise
trim at 230 m/s and 10 km). The design model has no saturation and no turbulence; PR 6's nonlinear runs decide.

## Method

A Nelder-Mead search (session scratchpad, not in the repo) on those models, four random restarts, minimizing
penalties for missing stricter targets than the test asserts (largest real part <= -0.05 1/s, damping >= 0.4,
loop-at-a-time phase margin >= 60 degrees, gain margin >= 10 dB both directions, sampled max |z| <= 0.9999) plus a
small preference for gains below stated bounds, then rounded to three figures. The test
`tests/python/test_autopilot_design.py` then asserts the plan's thresholds (phase margin 45 degrees, gain margin
6 dB, damping 0.30, largest real part -0.03 1/s) on the rounded gains that ship.

## Longitudinal (elevator: altitude PID plus theta and q feedback; throttle: tas PI)

Search bounds: kAltP 2e-3, kAltI 1e-4, kAltD 2e-2, kPitchTheta 2, kPitchRate 2, kSpeedP 0.1, kSpeedI 0.02.
Result (cost 0.0008): kAltP 1.54e-4, kAltI 7.97e-6, kAltD 2.24e-4, kPitchTheta 0.389, kPitchRate 0.184,
kSpeedP 0.0353, kSpeedI 7.69e-4. None sits at its bound.

## Lateral (aileron: heading PI to a clamped bank command, bank and roll-rate feedback; rudder: r and beta)

Sweep of kHeadingP, the other five lateral gains re-optimized at each (bounds kHeadingI 0.3, kBankP 3, kRollRate 3,
kYawRate 3, kSideslip 3):

| kHeadingP | cost | largest real part | min damping | note |
|---|---|---|---|---|
| 0.5 | 4.03 | -0.0098 | 0.400 | misses the real-part target |
| 1.0 | 3.01 | -0.0199 | 0.400 | misses |
| 1.5 | 0.97 | -0.0403 | 0.401 | misses |
| **2.0** | 0.0004 | -0.0500 | 0.401 | **smallest value meeting every target** |
| 3.0 | 0.0005 | -0.0500 | 0.433 | meets; bank command saturates at 6.7 degrees of heading error |
| 4.0 | 0.0007 | -0.0500 | 0.753 | meets; saturates at 5.0 degrees |
| 6.0 | 0.0009 | -0.0500 | 0.865 | meets; saturates at 3.3 degrees |

kHeadingP = 2.0 is therefore not a search bound: below it the stricter targets fail (the heading integrator's slow
pole is too slow), and above it the linear range of the bank command (0.35 / kHeadingP) only shrinks. Rounded lateral
gains at 2.0: kHeadingI 0.0343, kBankP 0.0550, kRollRate 0.243, kYawRate 0.359, kSideslip 0.269.

## Result on the shipped (rounded) gains, from `pytest tests/python/test_autopilot_design.py -s`

- Open-loop 5x5 phugoid (helper cross-check): 108.5343 s, zeta 0.02730 (PR 8 recorded 108.53 s, 0.0273).
- Longitudinal poles: real parts -0.573, -0.152, -0.0510 (pair), -0.0497 (pair); min damping 0.457 (>= 0.30);
  largest real part -0.0497 (<= -0.03). Lateral: -1.832, -0.680 (Dutch roll pair, wn 1.693 rad/s, zeta 0.4016,
  zeta*wn 0.680), -0.0502, -0.0499 (pair); largest real part -0.0499.
- MIL-F-8785C Category B Level 1 lower bounds: Dutch roll zeta 0.4016 >= 0.08, zeta*wn 0.680 >= 0.15, wn 1.693 >= 0.4;
  short period zeta within 0.30 to 2.00; every slower oscillation zeta >= 0.04 (asserted by the test).
- Loop-at-a-time margins (other channels closed): elevator phase margin 92.7 degrees; throttle 69.6; aileron 111.0;
  rudder 93.4 with +17.2 dB gain margin upward; every other gain-margin direction is unlimited (no 0-degree crossing).
- Sampled closed loop (ZOH at the 10 ms exchange period, command applied in the frame it is computed, no extra
  delay): max |z| 0.999498 (longitudinal), 0.999501 (lateral); with one extra exchange of delay (conservative,
  reported only): 0.999499 and 0.999502.

Limits of this evidence: the margins are loop-at-a-time (not simultaneous multi-loop), the pole floors and margins are
provisional thresholds accepted by Joe (the conventional source, MIL-F-9490D, is not in `docs/refs` and is less strict
below 0.06 Hz), and nothing here covers saturation, the bank clamp, or turbulence.
