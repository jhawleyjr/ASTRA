# M7 plan critique: one fresh read-only Codex critique of the M7 spec and plan

Status: **critique and verification pass done**. Both verdicts were rework, and all 19 findings (C-01
to C-11, N-01 to N-08) are accepted and folded into the spec. Joe decided Q25 on 2026-10-04: one
verification pass after his eight amendments, recorded below. This file started as the session's
plan-mode draft and was moved into the worktree (`docs/agents/claude-code.md`). The M7 spec and plan,
[m7-f16-nesc-oracle.md](m7-f16-nesc-oracle.md), is the record of every question, requirement and PR; this
file records only the critique.

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

**The run.**
- Companion 1.0.6 (`openai-codex` plugin), Codex CLI 0.160.0.
- `task --fresh --effort high --cwd .claude/worktrees/m7-planning`, read-only (no `--write`, no
  `--model`).
- The model is the companion's default; the log does not name it, and `~/.codex/config.toml` sets
  `gpt-6.1-sol`.
- Job `task-muure8f3-4om5ig`, thread `01a10a5a-b750-77e1-94e2-4021b1562130`, 10 min 14 s.
- Candidate `8fd7f32` against base `9c534c5`. Exit 0, a final message, verdict **rework**.
- The worktree stayed clean, and the critique made no edits.

**Verification.** Claude Code re-read every cited line. These were confirmed:
- `_runkit.py:250-264` and `:63-64`; `cli.py:501`, `:522`, `:656-673` and `:694`; `_core.cpp:661`;
- `wrench_source.hpp:61-69`, `core_api.hpp.in:18-20` and `icd_pack.hpp:125-128`;
- SIM 5's case 15 CSV row at t = 0 (yawing moment 857.276 lbf·ft, side force -63.005 lbf, rolling moment
  -225.830 lbf·ft).
C-01's magnitudes were checked against the Coriolis term alone, 2Ω·sin(lat)·V, about 0.015 m/s² in case
11.

| # | Sev. | Finding | Verdict | Where it is resolved |
|---|---|---|---|---|
| C-01 | high | Q7's 1e-4 lateral trim bound is incompatible with cases 11 and 12 (about 0.017 and 0.082 m/s² under Q6's rates) | accepted | Q7, R4, PR 5 |
| C-02 | medium | Case 15 mixed derivative residuals with aero forces; CSV yawing moment 857.28 vs the prose's 786 lbf·ft | accepted | Q7 (separate checks, the CSV used, the t = 0 aero comparison reported) |
| C-03 | high | Trim's consumers (`trim_fragment`, `astra trim`'s bound and default altitude) rebuild the old state | accepted | Findings, the trim contract, R4, PR 5 |
| C-04 | high | The law's input units and the trim-to-normalized mapping are unspecified | accepted | Contract, R11, R23, new Q26 |
| C-05 | high | The aileron conversion stops before the aero model's own convention | accepted | Contract, R2, PR 6a and 6b |
| C-06 | medium | Equivalence does not check the ungated command onset | accepted | Q3, R22 |
| C-07 | medium | The independent evaluator ignores ADR-0021's state bindings, and its self-check ran on M2 only | accepted | Q5, PR 6a, PR 7 |
| C-08 | medium | R12 included supersonic case 12, and flagged inputs were unhandled | accepted | Contract, R12 |
| C-09 | medium | R13's exclusions too broad; streams, counts and `/final_state` omitted | accepted | R13 |
| C-10 | medium | PR 3 needs a `kCoreApiVersion` bump and a stale-plugin rejection | accepted | Contract, R15, PR 3 |
| C-11 | medium | Envelope monitors stop before the tools' health consumers | accepted | Contract, R18, PR 13 |

None rejected. One resolution departs from the requested outcome. C-02 asked for dimensional numerical
bounds on the case-15 aero comparison. The spec instead reports that comparison without asserting it,
because t = 0 lies in the ungated span and SIM 4's untrimmed start (pitching moment -42,923 lbf·ft) leaves
no member-derived tolerance there. The derivative checks get the derived bounds C-01 asks for. C-04's
choice between an SI realization of the law and the DML's own units is Joe's (Q26).

## Verification pass (Q25)

Joe decided the spec's Q1 to Q26 on 2026-10-04 and gave eight amendments:
- 1: PR 1 split into 1a and 1b;
- 2: Q7's aero and engine terms, which need P1;
- 3: PR 5's blast radius;
- 4: the new Q27, 13.4's lateral-deviation logic;
- 5: Q11 and Q12 fixed before any run;
- 6: the propulsion's frame-start state in Q5's self-check;
- 7: Q13's two implementations and one trim result;
- 8: case 12 under the envelope monitors.
Once they were folded in, one fresh read-only Codex task (same companion route and flags as above)
checked C-01 to C-11's resolutions and the amendments.

**The run.**
- Companion 1.0.6, Codex CLI 0.160.0, `task --fresh --effort high`, read-only, the companion's default
  model.
- Job `task-muussfet-1xnlr9`, thread `01a10a7e-71ff-7c40-a6e6-70aca3dad1aa`, 8 min 19 s.
- `8fd7f32..d436581` against base `9c534c5`. Exit 0, a final message, verdict **rework**.
- The worktree stayed clean and unedited while the pass read it.

**Status of the earlier findings and the amendments.**
- C-01, C-02, C-05 to C-08, C-10 and C-11 were resolved.
- C-03, C-04 and C-09 were partially resolved; N-03, N-01 and N-06 close them.
- Amendments 1, 2 and 5 to 7 were implemented.
- Amendments 3, 4 and 8 were partially implemented; N-04, N-02 and N-07 close them.

**Verification.** Claude Code re-read every cited line. These were confirmed:
- `_runkit.py:232`, which rejects any of six residuals above 1e-6;
- `test_cruise_phugoid.py:197` and `autopilot_design.py:122`, both reaching trim through `linearize`;
- `trim.py:104` and `_core.cpp:179`, the 13-element state against the full combined-state length;
- `nesc_cases_test.cpp`, which has no warn-counter assertion.

| # | Sev. | New finding | Verdict | Where it is resolved |
|---|---|---|---|---|
| N-01 | high | The contract and R11 used customary-unit law inputs, contradicting Q26's SI law | accepted | Contract (the law runs in SI), R11 |
| N-02 | high | Q27's formula is a straight line in the initial plane, not the declared rhumb line (0.242 m at 20 s, 2.184 m at 60 s) | accepted | Q27 (Joe's amendments: the initial-plane line named, the rhumb line reported, the step error d(20 s) − 609.6 m), R11 fixtures |
| N-03 | high | `_runkit.apply_trim` keeps the six-residual 1e-6 rejection | accepted | The trim contract, PR 5 |
| N-04 | medium | `test_cruise_phugoid` and `autopilot_design.build_plants` missing from PR 5's callers | accepted | Q6's list, PR 5's selector |
| N-05 | high | A model-owned engine state has no trim definition; trim builds 13 elements | accepted | Q7, R4, the new conditional PR 5b |
| N-06 | medium | Q13 kept the exclusions C-09 removed from R13 | accepted | Q13 |
| N-07 | medium | Case 12's exemption let unrelated envelope warnings pass | accepted | Q16, R18 |
| N-08 | low | The C-01 summary contradicted amendment 2's ordering | accepted | The C-01 row |

None rejected. Joe decided Q27 at the same time, with two amendments: the course named as the initial-plane
line, with the rhumb line a reported variant; and the inputs stated (`lat` and `lon`, with h0 fixed). He
also had PR 5's selector reach every caller. Both are folded in with these findings.
