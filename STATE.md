# STATE (rewritten each session, never appended; ≤ 40 lines; written as true once the PR carrying it has merged)

## In flight
Nothing once PR #26 (branch `worktree-m2-environment-2b`) merges. Adds `core/util/random.hpp`
(seeded `Rng`, AGENTS.md), `core/util/units.hpp` (ADR-0002 boundary conversions), and
`plugins/physical/wind/dryden` (MIL-F-8785C turbulence) on PR 2's `begin_frame`/`wind_n` split.
Also lands ADR-0012 (per-frame domain validity, docs only) as its own commit, scheduled as PR 2c.
Sourcing: `docs/refs/MIL-F-8785C.pdf` (Joe-supplied), page-cited in `dryden_filter.hpp`,
cross-checked against MathWorks' Dryden block docs; `tests/golden/wind/mil_f_8785c_dryden.hpp` is a
second, independent transcription the tests check against, not the implementation checked against itself.
**Disclosed, not fixed** (Codex adversarial review): Dryden reads Earth-derived altitude every
frame with no per-frame domain check -- the same gap PR 1/1b/2 already disclosed, now scheduled as
PR 2c under ADR-0012, not fixed here (that would be its own, separately-scoped PR).

## Last verified result
`just check` exited 0 (`check: OK`, 230/230 tests) every preset, this session. `DrydenPsd` (Welch
periodogram vs. MIL-F-8785C's closed-form spectra, self-checked first against white noise) passed on
the first real run. Codex `review` flagged both guarded paths this PR touches; both authorized
(session armed, ADR content Joe-supplied). Codex `adversarial-review`: one `high` finding, the
disclosed/scheduled domain-validity gap above, not a new issue.

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
Two clang-tidy findings (`readability-redundant-inline-specifier` on `inline constexpr`
*functions*, `clang-analyzer-security.FloatLoopCounter` on a `double` loop variable) and one
asan/lsan leak (a test helper releasing a `unique_ptr` before a throwing `init()`) each cost one
extra cycle before `just check` passed clean -- all three now in `docs/lessons.md`.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
