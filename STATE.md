# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M4 PR 7 (M4 closeout), Tier 0 to 1, docs only: branch `worktree-m4-pr7-closeout` off `origin/main` `0fcae33` (PR 6
#57). First candidate `a2e9215`: PLAN.md M4 check-off with each clause cited to PR 6's evidence (packet
`docs/reviews/m4-pr6-lockstep-acceptance-human.md`, `docs/reviews/evidence/4e418e1-*`), PR 1 to PR 7 sub-items,
duplicate §15 bullet removed, open M4 follow-ups as PLAN.md items. Round 1 (`3b24a0c`, Joe's review): architecture
§7/§10 stale M4 status wording; moderate hold altitude in the R2 disclosure. Round 2 (Joe's review of `3b24a0c`):
architecture §10's `Transport` paragraph opening rewritten to the built interface (`transport.hpp`) and ADR-0022's
ownership split (Joe-authorized, accuracy only); PLAN.md PR 7 sub-item reworded to match. Stage: round 2 done, A1
and Codex recorded below; Joe's review next. Not pushed (stop before push, per Joe). PR 7 is merged once
`origin/main` contains it. Plan row: `docs/plans/m4-lockstep.md` PR 7 (A1).

## Last verified result
This session. `a2e9215`: A1 exit 0 both; Codex general review, no actionable defects
(`docs/reviews/evidence/a2e9215-codex-review.log`). `3b24a0c`: A1 exit 0 both; Codex delta review (`6086259..3b24a0c`)
no actionable defects (`docs/reviews/evidence/3b24a0c-codex-review.log`). `cbf01d0` (round 2): A1 exit 0
both; Codex adversarial review of the delta only (`--base 0a6313a`, focus: transport.hpp, lockstep_exchange.hpp,
ADR-0022/0023; thread `01a0dfe2-0c38-7d10-a6a9-463ce0431bfd`): exit 0, verdict approve, "New findings: none"
(`docs/reviews/evidence/cbf01d0-codex-review.log`; one of its combined `cat` reads exited 1, command truncated in
the log). Later commits: this record only.
`docs_check.py` skips PLAN.md, STATE.md, architecture.md. No product/test/golden/tolerance/ADR change, so no gate
run. M4's acceptance evidence is prior-session: PR 6 `4e418e1` gate `check: OK` (`evidence/4e418e1-full-gate.log`).
The PR 6 packet's "trace SVGs not rendered" note is superseded: Joe rendered and checked them during his PR 6 review.

## Next step
1. Joe: review round 2, then push, open the PR and squash-merge (or decide otherwise; the budget is exhausted).
2. After Joe's squash-merge, the next PR adds PR 7's merge commit to its PLAN.md sub-item and to the M4 entry (M3 PR
   9/10 precedent); otherwise the next task is Joe's to assign (M5 Tooling planning or a PLAN.md follow-up).
3. Open follow-ups, all PLAN.md items: moderate throttle jitter; `OPTIONAL_GAIN_NAMES`/`kSideslip` removal;
   follow-ups A, B (Joe arms the guard), C; the wall-clock symbol scan; Joe-owned: the stale
   `tests/golden/dynamics/capture_r1_golden.cpp`. Architecture §12's Python status omits PR 6's `astra.lockstep`
   and `Simulation.exchange_recorder_bytes()` (left alone, outside the authorized edits).

## Tried / failed
Correction rounds: 2/2 (round 1: Joe's review of `6086259`; round 2: Joe's review of `3b24a0c`); failed
integrations: 0/2. The budget is exhausted: any new defect goes to Joe.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
