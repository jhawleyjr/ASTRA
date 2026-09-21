# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
**PR #40 squash-merged by Joe** (`6590b7c`, 2026-09-20): ADR-0019/0020/0021 + PR 3d on `origin/main`.
`PLAN.md`'s PR 3d checkbox closed with the merge SHA. This worktree confirmed fresh off `origin/main`.

**`docs/plans/pr4-notional-aero-turbofan.md` (Revision 2) approved by Joe, 2026-09-20**, after two
review rounds: round 1 found a real side-force sign bug (JSBSim's SIDE axis is +right; S&L's literal
`-C` slot would have flipped it), a self-referential test oracle, a missing control-sign table, V=0
checks misplaced, diagnostic signals, and a dangling dependency -- all fixed, plus a Codex plan
critique (scoped to the `_math.hpp` shape) that found and fixed two more issues (an unused `mach`
field in `Inputs`; a turbofan `wrench_b()` test gap that couldn't catch a config/sampled-density
mix-up). Round 2 (approval-conditioned) added: the ICD/ADR-0001 elevator/aileron/rudder sign
convention written into `PLAN.md`'s own follow-up list (JSBSim's sense adopted, approved by Joe);
the Mach drop flagged explicitly as a deviation from Joe's own original instruction; exact-equality
(not undefined relative) tolerance for every zero-valued expected component; the empty `R4`
placeholder row removed, `R1`-`R10` renumbered throughout.

Plan, `PLAN.md`, and this file committed this session; PR 4 implementation starts next, Tier 3.

## Last verified result
Full gate for PR 3d/#40: `check: OK`, exit 0 (prior session) -- `docs/reviews/pr3d-adr0020-human.md`.
No gate run this session yet for PR 4 (plan/docs only up to the commit below).

## Next step
Implement PR 4 per the approved plan: `aircraft/notional_transport/physical/aero/{aero.cpp,
aero_math.hpp}` and `.../hardware/propulsion/turbofan/{turbofan.cpp, turbofan_math.hpp}`, mirrored
into `aircraft/_template/`; tests per the plan's R1-R10 (`tests/unit/{aero,turbofan}/`,
`tests/physics/notional_transport/`); `cruise.yaml`/`architecture.md` §13 updates;
`cmake/AstraPlugin.cmake` test-executable list edit (R10). `./scripts/check-full.sh` before human
review. PR 4a (ADR-0021) remains independent, not started.

## Tried / failed
Correction rounds: 0/2 used this session. Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
