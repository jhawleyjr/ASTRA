# STATE (rewritten each session, never appended; ≤ 40 lines; written as true once the PR carrying it has merged)

## In flight
Nothing once PR #26 (branch `worktree-m2-environment-2b`) merges. Adds `core/util/random.hpp`
(seeded `Rng`, AGENTS.md), `core/util/units.hpp` (ADR-0002 boundary conversions), and
`plugins/physical/wind/dryden` (MIL-F-8785C turbulence) on PR 2's `begin_frame`/`wind_n` split.
Also lands ADR-0012 (per-frame domain validity, docs only) as its own commit, scheduled as PR 2c.
Sourcing: `docs/refs/MIL-F-8785C.pdf` (Joe-supplied, not committed -- `docs/refs/README.md`),
page-cited in `dryden_filter.hpp`, cross-checked against MathWorks' Dryden docs;
`tests/golden/wind/mil_f_8785c_dryden.hpp` (constants + the two spectral forms, moved there this
session from `dryden_psd_test.cpp`, a Codex finding) is the guarded oracle the tests check against.
**Disclosed, not fixed** (Codex adversarial review): Dryden reads Earth-derived altitude every
frame with no per-frame domain check -- the same gap PR 1/1b/2 already disclosed, now scheduled as
PR 2c under ADR-0012, not fixed here (that would be its own, separately-scoped PR).

## Last verified result
`just check` exited 0 (`check: OK`, 230/230 tests) every preset, this session, at HEAD (`e625c32`
plus this commit's own `.gitignore`/README fix, no code changed since) -- supersedes 0b46ec0's
claim, stale once 63b02f8/e625c32 landed on top of it. Joe's own review of those two fix commits
(not re-run here, per instruction): the guard blocks edits/new files under `tests/golden/`, still
allows `tests/physics/`/`plugins/`; the moved spectral forms match §3.7.1.2 p.47 to 5e-13.

## Next step
M2 Environment (PLAN.md) PR 2c: per-frame domain validity (ADR-0012). `EarthModel::in_domain`/
`GravityModel::in_domain`; `validate_p_n` reimplemented as that plus input sanity;
`HealthCheck::kOutOfDomain` under `sim.health`'s `domain` key, checked post-step against the state
and a one-frame lookahead; the atmosphere `valid_range()` check moves from init-only to per-frame.
Closes PR 1's polar-approach gap and PR 1b's atmosphere-range gap. Accept: a trajectory toward
`kMaxLatitude`/the altitude floor halts naming the offending plugin before any state goes
non-finite; a `valid_range()` excursion halts naming `physical.atmosphere`; a longitude-pi crossing
does not halt; `domain: off` restores current behavior; zero allocations over 1000 frames. Opener:
`claude --worktree m2-environment-2c --permission-mode plan`

## Tried / failed
Two clang-tidy findings (`inline constexpr` *functions*, a `double` loop counter) and one asan/lsan
leak (a `unique_ptr` released before a throwing `init()`) each cost one extra cycle before
`just check` passed clean -- all three now in `docs/lessons.md`.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
