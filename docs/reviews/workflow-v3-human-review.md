# Human review packet: workflow-v3 (12-step workflow, process/tooling candidate)

PR / candidate base and head: `origin/main`=`40b484b` (confirmed current tip of `main` at
packet time — no PR yet, no rebase pending), head `4956c13` on branch `workflow-v3`, clean tree
(nothing uncommitted). 13 commits ahead of `main`. This is a **process/tooling candidate**: the
twelve-step workflow docs, `.claude/hooks/guard-oracle.sh`, its regression-case fixture,
`scripts/docs_check.py`, `justfile`/CI wiring, and templates — not a physics/product change.
Spec/plan/task: no repository feature spec (workflow tooling, not a `PLAN.md` milestone item);
`docs/reviews/workflow-v3-review.md` is the running findings/evidence record for this whole
candidate (see its "Final status" section for the fullest detail; this packet summarizes it).
Risk: process/tooling, but `.claude/hooks/guard-oracle.sh` guards `tests/golden/**`,
`docs/decisions/**`, and tolerance files — the repository's human-owned oracle boundary — so a
defect there has real consequence even though nothing here touches physics/product code.
Decision requested: **not a clean approval request**. This candidate has narrowed but still-open
findings in the guard-oracle hook (below); the request is Joe's disposition of those findings —
accept as documented residual risk, authorize a differently-scoped fix, or another decision.

## What changes and why

Updates the prior packet for this same candidate (still on file at this path, now superseded):
Joe read that packet and replied "authorize a bounded fix batch for R-006/R-008/R-009/R-011/
R-012/R-013" — the six findings it disclosed as open. This session ran three fix-and-verify
rounds against that authorization:

1. **Round 5**: fixed the originally-reported shape of each of the six findings. Fresh Codex
   review (general clean; adversarial fix-verification) found R-007/R-010 held, but R-006,
   R-008, R-009, R-011, R-012 were each still incomplete, plus **two genuine regressions this
   round itself introduced** (removing sed from a coarse fast-path pre-filter, to fix a false
   block, also silently dropped that pre-filter's coverage for command prefixes like `env`/
   `LC_ALL=C` and for absolute paths outside the session's own root).
2. **Round 6**: fixed the newly-found gaps, including both self-introduced regressions. Fresh
   Codex review found R-007/R-010/R-012 (attached/separated forms)/R-013 (ordinary-word forms)
   held, but reported **one more regression** (shell control-flow keywords like `then`/`do`/
   brace-groups also weren't recognized as command prefixes) plus narrower remaining variants
   of R-006/R-009 (a pipeline comment) and R-008/R-011 (a sed address modifier, nested/combined
   prefix forms).
3. **Round 7**: fixed the keyword-prefix regression and, as a direct side effect of how that fix
   was written (a loop instead of a single check), also closed the combined/nested-prefix
   variants without further code changes. **Stopped here**: Codex's own explicit conclusion on
   this round's predecessor was "this authorized batch is complete; no further autonomous
   correction round," and this session's own authorization said the same ("does not permit
   unlimited correction rounds"). Round 7's fixes are locally verified only, not redispatched.

Every fix in every round was independently reproduced against the actual current hook before
being recorded (JSON-on-stdin interface, no real file ever written) — none taken on Codex's
word or on reasoning alone.

## Evidence

| Check / acceptance | Result / date / duration | Log or retained artifact / candidate |
|---|---|---|
| `just hooks-test` | 144/144 passed, this session | reproducible via `.claude/hooks/tests/run.sh`; not separately logged |
| `just docs-check` | OK, 24 Markdown files, this session | reproducible; not separately logged |
| Full local gate on `bbded20` (Round 5) | `EXITSTATUS=0`, `check: OK`, 274/274×4, elapsed 4:25.59 | `docs/reviews/evidence/round5-full-gate.log` |
| Full local gate on `7c9f20f` (Round 6) | `EXITSTATUS=0`, `check: OK`, 274/274×4, elapsed 4:25.29 | `docs/reviews/evidence/round6-full-gate.log` |
| Full local gate on `4956c13` (Round 7, current head) | `EXITSTATUS=0`, `check: OK`, 274/274×4, elapsed 4:29.57 | `docs/reviews/evidence/round7-full-gate.log` |
| Codex general + adversarial, `bbded20` | needs-attention (2 regressions + 3 variants found) | `docs/reviews/evidence/bbded20-codex-*.log` |
| Codex general + adversarial, `7c9f20f` | needs-attention (1 regression + 3 variants found) | `docs/reviews/evidence/7c9f20f-codex-*.log` |
| Prior rounds (R-001–R-005, R-007, R-010, R-012, R-013 verification) | see table in prior packet history | `docs/reviews/evidence/e115c75-*.log`, `60caf9b-codex-r006-r012.log` |

Toolchain: AGENTS.md-pinned Ubuntu 24.04, clang-18, CMake 3.28+, Eigen 3.4, HDF5, yaml-cpp,
GoogleTest, Python 3.12+. `codex-cli` 0.154.0, model `gpt-6-astra`, reasoning effort `low` for
every Codex dispatch. `GATE=local` throughout. No numerical/physics changes; nothing to plot.

## Independent review

Builder: Claude (Sonnet 5, `opusplan` at high effort). Reviewer: Codex (`codex-cli` 0.154.0,
`gpt-6-astra`), fresh context each dispatch, general + adversarial. Four independent passes on
this candidate across Rounds 4–6 (`e115c75`, `60caf9b`, `bbded20`, `7c9f20f`); Round 7 verified
by this session only. Full detail: `docs/reviews/workflow-v3-review.md`.

**Verified by Codex, no further action needed**: R-001–R-005, R-007, R-010, R-012, R-013 (each
finding's own originally-reported scope — R-013's verification explicitly excludes the
separately-disclosed limitation below, which was never its own numbered finding).

**Still open — real, reproduced bypasses, narrower than what Joe originally authorized this
batch against, not yet Codex-verified as fixed**:

- **R-006 / R-009** (pipeline `cd` tracking): the originally-reported shapes and a plain
  multiline-continuation variant are fixed and Codex-confirmed. A narrower variant remains: a
  pipeline comment containing its own `;` (`cd tests/golden; printf x | # note;` + newline +
  `cd ../..; printf x > probe.hpp`) still corrupts directory tracking — exits `0` (should
  block).
- **R-008 / R-011** (sed write-command detection): every specifically-reported write form
  (bare `w`, `s///w`, digit-addressed, flag-clustered, long `--expression=`) is fixed and
  Codex-confirmed. Two narrower gaps remain: a target that reaches sed only via an `xargs` pipe
  (never appears as a literal command-line token at all, so this guard — which only ever
  inspects the command's own text — cannot see it); and a GNU sed address modifier (`/x/I`,
  case-insensitive) between an address and its command letter, which still defeats both
  write-command regexes' adjacency assumptions.

**Still open — a false block (safe direction), not a bypass, explicitly accepted rather than
chased further**: a `w` inside a regex/pattern region that happens to contain real whitespace
(`/w probe/p` — matching the literal text "w probe", not a write command) is still misread as a
write command. Recoverable immediately via the existing `.claude/touch-goldens` marker /
`ASTRA_TOUCH_GOLDENS=1` override.

No findings rejected. No critical findings at any point — every open item is a guard-tooling
correctness gap (an over-permissive bypass, blast radius limited to a write an agent's own
session attempts, still recoverable via git and caught by ordinary review before any merge; or
an over-restrictive false block, recoverable immediately via the existing marker), not an
active production or safety defect.

## Joe's attention

- **The pattern across four Codex passes is itself the most important thing here, more than any
  single remaining finding.** Each round's fix closed the reported shape but Codex's next pass
  found a narrower variant or (twice) a fresh regression from the fix itself. The remaining
  surface is now small and specific (three items, all disclosed above with exact reproduction
  commands), but this session's own assessment is that a fifth round would plausibly find a
  fifth narrower gap — token/regex-based classification of arbitrary shell/sed syntax has a
  long tail that incremental patching does not obviously terminate. See the review doc's "Final
  status" section for this session's explicit (non-default) recommendation: either accept
  today's much-reduced residual risk as a documented limitation, or invest in a fundamentally
  different approach (real sed/shell parsing, or a conservative "block first" stance for sed
  near guarded directories) rather than a fifth incremental round.
- Every open finding above was independently reproduced against the actual current hook in this
  session (not taken on Codex's word) — concrete command strings and exit codes, all in
  `docs/reviews/workflow-v3-review.md`.
- No dependency, interface, or compatibility changes. No deleted behavior of consequence beyond
  `scripts/tokens.py`'s retirement (already reviewed/approved in an earlier round).
- Rollback: this entire candidate is 13 commits on an unmerged branch; reverting is `git
  checkout main` / discarding the branch, no product code or `main` history affected either way.
- `docs/reviews/evidence/` now holds 15 retained log files (full-gate runs and Codex review
  transcripts) — durable, git-tracked validation/review evidence that previously lived only in
  gitignored `build/validation/`.

## Decision record

State: pending
Human decision/date/reference: pending
Approved candidate/scope: pending
Merge observed: not merged
Release authorization: pending / not requested

Significant tradeoff rationale: this session chose to stop the fix-verify cycle at Round 7
rather than continue chasing Codex's fourth pass's findings with a fifth autonomous round, per
both Codex's own explicit conclusion and this session's authorization bound — the tradeoff was
disclosure of a small, specific, well-evidenced remaining surface over continuing to chase a
fully green state without renewed authorization from Joe.
Accepted risks: none formally accepted yet — the three open items above are unresolved, not
risk-accepted; this decision is Joe's to make. The `/w probe/p` false block is a candidate for
"accept as-is" given it is a false block, not a bypass, and is already recoverable via the
marker.
Nonblocking follow-ups: none recorded (each open item above is either a blocker or the one
explicitly-flagged accept-candidate false block, not a deferred minor).
Architectural decisions: none — no ADR implicated by this candidate.

If `changes-required`: these are implementation defects (guard-oracle.sh correctness gaps),
routing to step 9 (fix, retest, re-review) if and when Joe authorizes a differently-scoped
batch — not a planning defect, requirements gap, or architecture conflict.

Joe's explicit decision or squash-merge records approval in this solo-maintainer repo; no
GitHub approving-review badge is required. This packet is not itself a request to merge.
