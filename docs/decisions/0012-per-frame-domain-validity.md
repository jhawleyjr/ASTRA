# ADR-0012 — Per-frame domain validity is a HealthMonitor check over a model-owned predicate, checked post-step with a one-frame lookahead

Status: **accepted**, 2026-09-16. Elaborates ADR-0003 (`p_n`'s meaning is the Earth model's) and ADR-0004
(one derivative functor); `docs/architecture.md` §15. Closes the gaps disclosed in `PLAN.md`'s M2 PR 1 and
PR 1b lines.

## Context

`EarthModel::validate_p_n` and `GravityModel::validate_p_n` run once, at init, and report by throwing
`ConfigError`. The properties they check — finiteness, latitude inside `geodesy::kMaxLatitude`, altitude
above `-M(lat)` (or `-radius_`) — are not properties of the *input*; they are what the position kinematics
need at every evaluation. `round`'s `p_n_dot` divides by `cos(lat)` and `(radius_ + h)`; `wgs84_rotating`'s
divides by `(M(lat) + h)` and `(N(lat) + h)`; `omega_en_n` carries a `tan(lat)`. A trajectory that
*continuously approaches* either boundary without landing on it runs on large-but-finite degraded
kinematics, and nothing observes it: the existing `HealthMonitor` checks only NaN/Inf and quaternion norm,
which fire on the eventual overflow, not on the approach. `AtmosphereModel::valid_range()` has the same
shape — checked once at init, and `at()` returns an all-NaN `AtmoState` outside it, unobserved until a
consumer reads it into a wrench.

Two constraints shape the answer. The per-frame path may not throw or allocate (§14), so the run-time form
must be an observation, not an exception. And the init and run checks are **not the same predicate**:
`validate_p_n` also rejects `lon` outside `[-pi, pi]` and flags degree-valued latitudes, which are
input-sanity checks. Nothing in the tree wraps longitude, and nothing is singular in it, so re-running the
init predicate every frame would halt any flight crossing the antimeridian.

## Decision

`EarthModel` and `GravityModel` each gain one non-pure virtual:

    // The SINGULARITY subset of validate_p_n: is this model's own math defined at p_n?
    // Default true (flat Earth, constant gravity divide by nothing). Per-frame path: noexcept, no alloc.
    [[nodiscard]] virtual bool in_domain(const Eigen::Vector3d& p_n) const noexcept;

Each model's `validate_p_n` is reimplemented as `in_domain` **plus** its input-sanity checks, so there is
one definition of "singular" per model and the init and run paths cannot drift. The input-sanity checks
(longitude range, degrees-vs-radians) stay init-only, by name, with a comment saying why.

`HealthMonitor` gains a fourth check, `HealthCheck::kOutOfDomain`, configured by a `domain` key under
`sim.health` (`off`/`warn`/`halt`, default `halt`) and publishing `health.domain`, exactly like the three
that exist. `Vehicle::integrate` calls it once per frame, immediately after `check_state` and before
`post_step_renormalize`, against two points:

1. the post-step `p_n`, and
2. a one-frame lookahead, `p_n + dt * p_n_dot(post-step state)`.

The lookahead is what makes a post-step check sufficient: a state close enough to a boundary that the next
frame's RK4 stages would cross it fails *this* frame, before any stage evaluates there. `HaltReason::source`
names the offending model's plugin path.

Atmosphere needs no new interface. The monitor already has the Earth model and the atmosphere model; it
checks `earth->altitude(vs)` against `atmosphere->valid_range()` under the same `domain` action, which is
the check `Simulation::from_config` already performs at init.

## Rejected alternatives

- **Per-stage checking inside the RK4 loop.** The exact form of the check, and what M2 PR 1's disclosure
  originally called for. It requires `Eom::operator() const noexcept` to report a status out — a `mutable`
  flag or a changed return type — which reopens the single-derivative contract ADR-0004 pins, for a check
  whose purpose is catching an approach. The one-frame lookahead catches the same trajectories one frame
  earlier, at one extra `p_n_dot` call per vehicle per frame and no interface change. If a future model
  has a domain a single frame can jump clean across, this decision is superseded, not patched.
- **Post-step only, no lookahead.** What Codex proposed against PR 1 and what that PR rejected. It cannot
  see an excursion that a stage takes and the final state returns from, which is exactly the failure mode.
- **Reusing `validate_p_n` per frame.** Halts any antimeridian crossing (see Context). The split into
  `in_domain` plus input sanity exists precisely to make that mistake unavailable.
- **A uniform `domain_status` across Earth, gravity and atmosphere.** Atmosphere's domain is an altitude
  interval it already publishes through `valid_range()`; forcing it into a `p_n` predicate would hand it a
  representation it deliberately never sees (M2 PR 1b).
- **A configurable margin instead of the lookahead.** A margin has to be expressed in radians for latitude
  and metres for altitude, so it is two numbers per model with no principled value. `dt * p_n_dot` is the
  distance the state actually moves, in the state's own units, and needs no configuration.

## Consequences

- Closes M2 PR 1's polar-approach gap and PR 1b's mid-run atmosphere-range gap. It does **not** close PR
  1b's WGS-84/ISA geoid datum gap, which is a wrong reference surface, not a domain excursion, and still
  needs a geoid model in its own PR.
- `flat` and `gravity/constant` take the default `true` and pay one predicate call per frame; M1's tests
  and M2's dragless NESC cases are unaffected in behavior.
- Acceptance: a trajectory driven toward `kMaxLatitude` halts naming the Earth plugin, at least one frame
  before any state vector goes non-finite; the same for an altitude driven toward the floor; a
  configured-atmosphere run leaving `valid_range()` mid-run halts naming `physical.atmosphere`; an
  east-west trajectory whose longitude passes `pi` does **not** halt; `sim.health: { domain: off }` restores
  the current behavior; zero allocations over 1000 frames with the check active.
- Must land before M2 PR 3, whose drag `WrenchSource` is the first consumer that can read a NaN
  `AtmoState` into the EOM.
