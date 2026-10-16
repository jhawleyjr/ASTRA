# M7b plan critique: one fresh read-only Codex critique of the M7b spec and plan

Status: **critique done; verification pass pending.** The verdict was rework. All six findings, C-01 to C-06,
are accepted and folded into the spec. Five touch a decision of Joe's, and come to him for confirmation. The M7b spec and plan, [m7b-closed-loop-oracles.md](m7b-closed-loop-oracles.md), is the record of every
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

**The run.**
- Companion 1.0.6 (`openai-codex` plugin), Codex CLI 0.160.0.
- `task --fresh --effort high --cwd .claude/worktrees/closed-loop-oracles`, read-only (no `--write`, no
  `--model`).
- The model is the companion's default. The status does not name it; `~/.codex/config.toml` sets `gpt-6.1-sol`
  at reasoning effort high.
- Job `task-mvalnsag-ggaasa`, thread `01a14371-fe1c-7511-84f7-9eddbb256223`, 8 min 44 s.
- Candidate `a02dafd` against base `8e525c5`. Exit 0, a final message, verdict **rework**.
- The worktree stayed clean, and the critique made no edits. Its own probes were read-only Python
  computations.

**Verification.** Claude Code re-read every cited line. These were confirmed:
- `python/astra/trim.py:212-225`: `_n_frame_rate` returns `np.linalg.lstsq(jacobian, -at_rest)`, with no
  structural zero. `_runkit.py:282` writes the solved rates into `omega_b` as they are (C-01).
- The plan's own peer guard: the oracle (a) peer refuses a request whose frame is not the next kN. F's first
  draft took the reply at floor(j/N)·N (C-02).
- `integrator.hpp:106-124`: classical RK4. For the five-state chain, the term it omits is h⁵A⁴Bu/120, in x₁
  only (C-03).
- The plan's shipped pair carried both asymmetries, and its controls table cited that one pair for both
  (C-04).
- R12 and the controls: no control was required to exceed the modeling bound (C-05).
- 1005 = 335 × 3 (C-06).

| # | Sev. | Finding | Verdict | Where it is resolved |
|---|---|---|---|---|
| C-01 | high | The trim's `lstsq` rates leave p and r near 1e-22, not structural zeros, so the shared IC breaks the mirror at frame 0 | accepted | Oracle (c)'s exact lateral zeros, R11 (touches Q9's IC construction) |
| C-02 | medium | The wrong-divisor control halts at the peer's frame guard, and F had no reply at each nominal kN | accepted | F on the recorded hold, the request check, the control's row, R2 (touches Q5's row and R3) |
| C-03 | medium | The order control's omitted term shrinks with u, so "nonzero u" does not carry its margin | accepted | Pre-registered excited frames with stated margins, R1 (touches Q5 and amendment 9's wording) |
| C-04 | medium | The shipped pair cannot attribute a break to the tables, because Ω breaks the mirror first | accepted | Ω-only and tables-only controls against the symmetric baseline, R14 |
| C-05 | medium | No control had to exceed R12's modeling bound | accepted | The doubled-asymmetry defect, and the derivation's envelope and assumptions, R12 (touches amendment 1's acceptance) |
| C-06 | low | Frame 1005 is an exchange frame for divisor 3 | accepted | The step moves to frame 1007 (touches Q6) |

None is rejected. Each resolution keeps Joe's decision (the geometry, the control readings, the modeling-bound
approach, the step's role) and tightens how its acceptance is stated. The five that touch a decision come to
him for confirmation in the PR body.

## Verification pass

*Pending.*
