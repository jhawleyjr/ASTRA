# M7b plan critique: one fresh read-only Codex critique of the M7b spec and plan

Status: **pending.** The critique has not run yet. Its verification pass follows once its findings are folded
in. The M7b spec and plan, [m7b-closed-loop-oracles.md](m7b-closed-loop-oracles.md), is the record of every
question, requirement and PR. This file records only the critique and its verification pass.

## Context

Joe asked for one fresh, read-only Codex plan critique of the M7b spec and plan, with a verification pass, as
#104 had ([m7-plan-critique.md](m7-plan-critique.md)).
- He approved the plan on 2026-10-15, with his answers to Q1 to Q12 and amendments 1 to 10, before the
  critique. The spec carries them.
- Codex does not edit the plan. Claude verifies each finding against the cited lines, folds the supported ones
  into the spec, and explains each rejected one here.
- A finding that reopens one of Joe's decisions goes back to him.

## Procedure

1. **The candidate.** The draft PR's head on branch `worktree-closed-loop-oracles`, against base `origin/main`
   = `8e525c5`. The worktree must be clean at the head being critiqued.
2. **The dispatch.** The companion is resolved from the installed Codex plugin each session; it is not pinned
   here. Dispatch once, read-only (no `--write`, no `--model`), through the Bash tool's own
   `run_in_background`, capturing the exit status before any display:
   `node "$CODEX_COMPANION" task --fresh --effort high --cwd <worktree> "$focus" > <log> 2>&1; echo "exit=$?" >> <log>`
3. **The focus.** It names paths and SHAs and embeds no documents. It asks for failed assumptions and
   evidence-backed findings only, each with a severity, `file:line` evidence and the requested outcome, and
   ends with a verdict.
   - **Reading list:** AGENTS.md; architecture §5, §10, §11 and §16; ADR-0004, 0013, 0017, 0021, 0022, 0028
     and 0029; `core/include/astra/kernel/integrator.hpp`; `core/src/kernel/simulation.cpp`,
     `lockstep_exchange.cpp` and `vehicle.cpp`; `core/include/astra/util/icd_pack.hpp`, `frames.hpp` and
     `air_data.hpp`; `plugins/physical/earth/round` and `gravity/inverse_square`; `aircraft/f16/physical/aero`,
     `aircraft/f16/common/f16_interp.hpp` and `aircraft/f16/sim/transport/nesc_control`;
     `tests/plugins/nesc_control_variant`; `embedded/autopilot`; `python/astra/lockstep.py`;
     `tests/python/test_lockstep.py`; `cmake/AstraBuildFlags.cmake`; and the M4 configs.
   - **Focus items:**
     1. RK4 exactness and the order claim (M⁵ = 0); the reference's exactness and independence; whether F, L
        and C, and each of (a)'s controls, the order control included, can be failed by a plausible bug.
     2. Whether (b)'s objects and flags check make bitwise equality meaningful; whether each porting control's
        first differing exchange is certain as argued, and stays smaller than M4's bands.
     3. The inventory's completeness: any operation on the F-16 path with the wrong parity, or a permutation,
        under the meridian mirror; signed zeros; the trim's lateral exactness before the step; PR 11a's
        tracked-config guard.
     4. The hooks (`Options::earth_rate`, `build_up`'s β parameter) against ADR-0029 and PR 11a's amendment 1
        (no variant code in the shipped `.cpp`), and R15's bit-identity evidence.
     5. Tiers, dependencies, gate estimates, the decided questions, and contradictions with the architecture
        or an accepted ADR.
     6. Whether amendment 1's modeling-bound derivation is sound and discriminating (amendment 5).
     7. Whether Q4's (R) leaves unseen any exchange-timing defect the brief named (amendment 5).
4. **Reading the result.** Read the log tail and `exit=`. A nonzero exit, an interrupted run or no final
   message leaves the critique **pending**, recorded as such, never as clean. The companion's `status` gives
   the job id, the thread id and the model.
5. **Verification.** Check every finding's cited lines directly, with no subagents. Record each finding with
   its verdict below. Fold the supported ones into the spec and say where; explain each rejection.
6. **The verification pass.** It uses the same route and flags, and checks the critique's resolutions and any
   new finding.
7. **Closing.** Re-run `just docs-check` and `git diff --check origin/main..HEAD`. Commit, push, and update the
   PR body over REST (`gh api -X PATCH repos/<owner>/<repo>/pulls/<n> -F body=@<file>`, lesson 2026-09-29).

## Record

*Pending.*

## Verification pass

*Pending.*
