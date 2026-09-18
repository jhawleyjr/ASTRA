# ADR-0014: Per-channel-kind resolution floor on the NESC oracle band

Status: **accepted**, 2026-09-18
Owner: Joe
Related/supersedes: elaborates ADR-0013 (does not supersede it). Governs `PLAN.md`'s M2 PR 3b.

## Context

ADR-0013's band formula (`tol_abs = k * max_t(spread)`, `k = 3`) is exact where the ensemble genuinely
disagrees, but several channels are analytically zero for a given case: case 1's latitude; every aero
**force** in cases 1–3 (case 1's sphere and case 2's brick both have `C_D = 0`; case 3's brick also has
`C_D = 0`, zero drag specifically — its damping **moments** are deliberately nonzero, since that case
exercises real rotational damping, not a degenerate channel, and is not one of the examples this ADR treats
as degenerate). For those genuinely-zero channels, six independent tools' own floating-point roundoff on a
value that should be exactly zero produces a nonzero `spread`, but one with no physical meaning — `tol_abs`
computed purely from ADR-0013's formula can be vacuously tight (rejecting ASTRA's own equally-legitimate
roundoff) or, if the roundoff happens to be larger, vacuously loose. ADR-0013 anticipated needing "the
second judgment number its formula doesn't have" without specifying it.

## Decision

Add a per-channel-kind absolute floor: `tol_abs = max(k * max_t(spread), floor)`, where `floor` depends
only on the channel's physical kind, not on the case:

| Channel kind | Floor |
|---|---|
| Angle (latitude, longitude, Euler) | 1e-12 rad |
| Length (altitude) | 1e-9 m |
| Linear velocity | 1e-9 m/s |
| Angular rate | 1e-12 rad/s |
| Force or moment | 1e-12 (N or N·m) |
| Density, pressure, temperature, speed of sound | 1e-12 relative (folded into `tol_rel`, not `tol_abs`, since these are always-positive quantities with no natural zero to floor an absolute term against) |

These are round numbers well inside double precision's own roundoff floor for the magnitudes this data
spans (verified this session: the tightest genuinely non-zero band found, case 9's yaw, sits 20× above its
own floor — the floor does not mask a real disagreement at the scale this data actually exhibits).

## Rejected alternatives

- **A single flat floor for every channel kind.** Rejected for the same reason ADR-0013 rejected one global
  tolerance: an angle in radians and a force in newtons do not share a meaningful common floor.
- **No floor; accept that a degenerate channel occasionally produces a vacuous band.** Rejected: a band wide
  enough to admit an obviously wrong answer is explicitly not a test (ADR-0013's own non-discriminating-
  channel language) — the failure mode this floor exists to prevent is a *false pass* on a broken zero, not
  (only) a false failure.
- **Drop degenerate channels from comparison entirely.** Rejected: cases 1–3's near-zero force/moment
  channels are exactly where a sign error or an unintended nonzero constant is easiest to miss if nothing
  checks them at all.

## Consequences and validation

`tests/golden/nesc/generate_nesc_band.py` implements this formula for every channel it emits; the
generator's own stderr report (mirroring `generate_isa1976_table.py`'s `check_transcribed_row_tolerances`)
prints each channel's floor-vs-computed-`tol_abs` ratio so a future regeneration that moves a channel
toward its own floor is a visible signal, not silently absorbed.

## Approval

Accepted, 2026-09-18. Drafted with `docs/plans/nesc-harness.md` and accepted as part of Joe's plan approval
that day; the Context section above was corrected the same day (Codex plan critique caught a factual
overclaim about case 3's aero moments — the Decision itself, the floor formula and its table, never
changed) and Joe explicitly re-accepted this ADR with the corrected Context before this file was written.
