# M7 plan critique: one fresh read-only Codex critique of the M7 spec and plan

Status: **critique pending** (dispatched after the draft PR opens; the record below is filled from the
companion's own output). This file started as the session's plan-mode draft and was moved into the worktree
(`docs/agents/claude-code.md`). The M7 spec and plan, [m7-f16-nesc-oracle.md](m7-f16-nesc-oracle.md), is the
record of every question, requirement and PR; this file records only the critique.

## Context

Joe asked for one fresh, read-only Codex plan critique of the M7 spec and plan before he sees it, as M6's
planning PR (#78) had ([m6-plan-critique.md](m6-plan-critique.md)). He approved the session plan on
2026-10-04 with eleven amendments (aero-only channels, the M10 transport refusal, command schedules, signs
and scales at the law boundary, the equivalence object, the gated window, a measured band preview, trim
unknowns, the LQR reference, provisional table questions, PR table hygiene). The spec carries them.
Codex does not edit the plan. Claude verifies each finding against the cited lines, folds supported ones
into the spec, and explains rejected ones here.

## Procedure

1. Candidate: the draft PR's head on branch `m7-planning`, base `origin/main` = `9c534c5`. The worktree
   must be clean at the head being critiqued.
2. The companion is resolved from the installed Codex plugin each session (not pinned here). Dispatch
   once, read-only (no `--write`, no `--model`), through the Bash tool's own `run_in_background`, with the
   exit status captured before any display:
   `node "$CODEX_COMPANION" task --fresh --effort high --cwd <worktree> "$focus" > <log> 2>&1; echo "exit=$?" >> <log>`
3. The focus names paths and SHAs and embeds no documents. It asks for failed assumptions and
   evidence-backed findings only, each with a severity, `file:line` evidence and the requested outcome,
   ending with a verdict. It asks Codex to read AGENTS.md, architecture §7, §9 to §12, §15, §16, §18,
   ADR-0002, 0006 to 0010, 0013 to 0017, 0020 to 0022, 0024 to 0027, `nesc_cases_test.cpp`,
   `generate_nesc_band.py`, `nesc_band_common.hpp`, `trim.py`, `cli.py`, `montecarlo.py`, `scenario.py`,
   `log.py`, `lockstep.py`, `icd_pack.hpp`, `lockstep_exchange.hpp`, the loopback transport and
   `vehicle.cpp`. The focus items:
   1. The oracle: two members, footnotes (a) and (b), the 8 s window against 13.1's and 13.2's 5 s
      commands, the 1e-5 s matching tolerance, the longitude wrap, the scalar band against Q4's per-sample
      alternative, and the band preview's reading.
   2. The aero-only channels (Q5): whether the independent-evaluator route is sound under the purity
      contract and ADR-0016, and what it misses.
   3. Whether each R1 to R21 can be failed by a plausible bug, emphasizing R4, R5 to R7, R9, R12 and R13.
   4. The sim-side peer and the port (Q8 to Q13) against ADR-0007, ADR-0017, ADR-0022 and ADR-0024: the
      timing, the command schedules, the signs and scales, the EAS derivation, the bitwise criterion and
      its excluded datasets, and the tools' transport refusals.
   5. Trim (Q6, Q7): the rate convention, the unknowns, the thresholds, and cases 15 and 16.
   6. PR order, tiers, dependencies, binding ADRs and acceptance commands; open-item coverage.
   7. Contradictions with the architecture or accepted ADRs, and missing requirements.
4. Read the log tail and `exit=`. A nonzero exit, an interrupted run or no final message leaves the
   critique **pending**, recorded as such, never as clean. The companion's `status` gives the job id,
   thread id and model.
5. Check every finding's cited lines directly (no subagents). Record each with its verdict below; fold the
   supported ones into the spec and say where; explain each rejection.
6. Re-run `python3 scripts/docs_check.py` and `git diff --check origin/main..HEAD`, commit, push, and
   update the PR body over REST (`gh api -X PATCH repos/<owner>/<repo>/pulls/<n> -F body=@<file>`, lesson
   2026-09-29).

## Record

Pending.
