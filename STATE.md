# STATE (rewritten each session, never appended; ≤ 40 lines; written as true once the PR carrying it has merged)

## In flight
Nothing once PR #22 (branch `worktree-m2-environment`) merges. Adds `plugins/physical/earth/
{round,wgs84_rotating}` (geodetic `p_n` per ADR-0003) and `plugins/physical/gravity/
{inverse_square,wgs84_j2}` reading `p_n` directly (no new position-access API -- ADR-0003 already
makes `p_n` geodetic under these models). `core/util/geodesy.hpp`: WGS-84 constants, radii of
curvature, geodetic<->ECEF/NED (`wgs84_j2`'s Groves-eq-2.142 J2 formula needs the latter).
`GravityModel::validate_p_n` (mirrors `EarthModel`'s) lets each gravity model bound its own geometry
independently of the paired Earth model's. Closes PR #21's finding: `cppcoreguidelines-no-malloc`
(Allocations widened) + NOLINT pair in `alloc_counter.cpp`.
**Disclosed, not fixed** (Joe's call, reasoning in `PLAN.md`'s PR 1 line): `validate_p_n` only
checks the initial state; a trajectory continuously approaching (not landing exactly on) the polar
margin or an altitude floor is not re-validated per frame -- needs a new per-frame domain-halting
`HealthMonitor` capability, a plan-reviewed design, not a bolt-on.

## Last verified result
`just check` exited 0 (`check: OK`) on every preset, 140/140 tests each (five runs: implementation,
three Codex-review fixup rounds, one format-only rerun). Plan review (one round, Earth/gravity
position contract): 4 findings folded in before code existed, 1 rejected with reasons. Codex review:
1 finding (`wgs84_rotating` bounded altitude against the wrong, less restrictive radius) -- fixed.
Codex adversarial review, 3 rounds: round 1 found 2 more (cross-model radius mismatch; `wgs84_j2`'s
own near-polar ECEF collapse, verified to ~0.043 m) -- both fixed with a `GravityModel::validate_p_n`
override each. Round 3's last finding is the disclosed gap above -- rejected as out of scope, not
silently dropped.

## Next step
M2 Environment (PLAN.md) PR 1b: `AtmosphereModel` + `plugins/physical/atmosphere/isa1976` (the split
this PR's own size contingency triggered). Declares its valid altitude range; §15's pre-run
"altitude inside range" check joins `HealthMonitor::init`; position-representation check extends to
`physical.atmosphere`. Accept: ISA table points exact.
Precondition: `touch .claude/touch-goldens` before opening (one file needs it).
Opener (model and effort pinned in `.claude/settings.json`; escalation is Joe's):
   `claude --worktree m2-environment-1b --permission-mode plan`

## Tried / failed
Nothing new; the backgrounded-`just check` polling pattern in `docs/lessons.md` worked as documented.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
