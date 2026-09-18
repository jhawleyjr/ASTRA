# Human review packet: workflow-v3 (12-step workflow, process/tooling candidate)

PR / candidate base and head: `origin/main`=`40b484b` (confirmed current tip of `main` at
packet time — no PR yet, no rebase pending), head `e9e25a3` on branch `workflow-v3`, clean tree
(nothing uncommitted). 8 commits ahead of `main`. This is a **process/tooling candidate**: the
twelve-step workflow docs, `.claude/hooks/guard-oracle.sh`, its regression-case fixture,
`scripts/docs_check.py`, `justfile`/CI wiring, and templates — not a physics/product change.
Spec/plan/task: no repository feature spec (workflow tooling, not a `PLAN.md` milestone item);
`docs/reviews/workflow-v3-review.md` is the running findings/evidence record for this whole
candidate.
Risk: process/tooling, but `.claude/hooks/guard-oracle.sh` guards `tests/golden/**`,
`docs/decisions/**`, and tolerance files — the repository's human-owned oracle boundary — so a
defect there has real consequence even though nothing here touches physics/product code.
Decision requested: **not a clean approval request**. This candidate has open major findings in
the guard-oracle hook (below); the request is Joe's disposition of those findings — accept as
documented residual risk, authorize a further bounded fix batch, or another decision — before
this can be considered for merge.

## What changes and why

Four fix rounds on top of the original `22a5009` "New workflow" commit, all previously reviewed
except this session's own work:

1. Process-gap fixes to the twelve-step workflow docs, review-dispatch recipe, `docs-check`
   tooling, and CI scope classification (D1-D8, R-001–R-005) — **all verified** by fresh Codex
   review, several rounds ago.
2. **This session**: found and corrected a stale handoff — `STATE.md` still described a prior
   session's doc reconciliation as uncommitted after it had actually already been committed
   (`e081d88`), and `docs/reviews/workflow-v3-review.md`'s R-001–003 `Status:` fields still read
   "fixed-unverified" after Codex had already verified all three — both corrected in place,
   history preserved.
3. **This session**: fixed two Joe-reported `guard-oracle.sh` bugs, each reproduced directly
   against the pre-fix hook before fixing (no real file written, JSON-on-stdin interface only):
   - R-007: `cd`'s second token was read as the directory literally; `cd -- tests/golden && ...`
     misread `--` as the target and bypassed the guard.
   - R-008: a read-only `sed -n ...` (no `-i`) reached via a guarded directory was blocked as if
     it were a write.
4. **This session**: dispatched fresh Codex review of the R-007/R-008 candidate (`e115c75`).
   General review: clean. Adversarial fix-verification: R-004/R-005 confirmed holding, but
   R-006 (an earlier, already-committed fix), R-007, and R-008 were each found still
   incomplete — four new findings (R-009–R-012). All four reproduced directly and fixed in a
   second batch (`60caf9b`).
5. **This session**: dispatched one more — the last authorized — Codex re-verification pass
   against `60caf9b`. R-007 and R-010 confirmed holding. **R-006, R-008, R-009, R-011, and
   R-012 were found still incomplete again**, plus one new false-block (R-013). All six
   reproduced directly against the current hook. Per this session's explicit bound ("this
   authorization does not permit unlimited correction rounds") and Codex's own conclusion
   ("this authorized review batch is complete"), no further autonomous fix was attempted.

## Evidence

| Check / acceptance | Result / date / duration | Log or retained artifact / candidate |
|---|---|---|
| `just hooks-test` | 104/104 passed, this session | reproducible via `.claude/hooks/tests/run.sh`; not separately logged |
| `just docs-check` | OK, 24 Markdown files, this session | reproducible; not separately logged |
| Full local gate (`./scripts/check-full.sh`) on `e115c75` | `EXITSTATUS=0`, `check: OK`, 274/274×4, elapsed 4:25.61, this session | `docs/reviews/evidence/round4-full-gate.log` |
| Full local gate on `60caf9b` | `EXITSTATUS=0`, `check: OK`, 274/274×4, elapsed 4:20.61, this session | `docs/reviews/evidence/round4-fixverify-full-gate.log` |
| Full local gate on `1a12623` (prior session) | `EXITSTATUS=0`, `check: OK`, 274/274×4, cited from commit message, duration not recorded | `docs/reviews/evidence/1a12623-full-gate.log` (retained this session before being overwritten) |
| Codex general review of `e115c75` | no actionable regressions | `docs/reviews/evidence/e115c75-codex-review-general.log` |
| Codex adversarial fix-verification, `e115c75`, R-004–R-008 | needs-attention (4 new findings) | `docs/reviews/evidence/e115c75-codex-adversarial-r004-r008.log` |
| Codex adversarial fix-verification, `60caf9b`, R-006–R-012 | needs-attention (5 reopened, 1 new) | `docs/reviews/evidence/60caf9b-codex-r006-r012.log` |

Toolchain: AGENTS.md-pinned Ubuntu 24.04, clang-18, CMake 3.28+, Eigen 3.4, HDF5, yaml-cpp,
GoogleTest, Python 3.12+. `codex-cli` 0.154.0, model `gpt-6-astra`, reasoning effort `low` for
every Codex dispatch this session. `GATE=local` throughout — CI is intentionally local-gate
mode; no green GitHub check was substituted for a full-gate result. No numerical/physics
changes in this candidate; nothing to plot.

## Independent review

Builder: Claude (Sonnet 5, `opusplan` at high effort per `.claude/settings.json`). Reviewer:
Codex (`codex-cli` 0.154.0, `gpt-6-astra`), fresh context each dispatch, general + adversarial.
Findings record: `docs/reviews/workflow-v3-review.md` (the authoritative, most detailed source —
this packet summarizes it).

**Verified** (no further action needed): R-001, R-002, R-003 (Codex review of `b2ef1ac`),
R-004, R-005 (Codex review of `e115c75`), R-007, R-010 (Codex review of `60caf9b`).

**Open — major, in `.claude/hooks/guard-oracle.sh`, all reproduced directly this session**:

- **R-006 / R-009** (pipeline `cd` tracking): `cd tests/golden; cd ../.. | cat; printf x >
  probe.hpp` is blocked correctly (fixed by R-009), but the same shape with an actual newline
  after the pipe (`cd tests/golden; printf x |` + newline + `cd ../..; printf x > probe.hpp`)
  still bypasses the guard — exits `0` (should block).
- **R-008 / R-011** (sed write-command detection): a bare `w`/`s///w` sed write is now caught
  (fixed by R-011), but a digit-addressed `w` (`sed -n '1w probe.hpp' ...`), a flag-prefixed `w`
  on a substitution (`s/x/y/gw probe.hpp`), and the long `sed --expression=` form all still
  bypass detection or have their target discarded — each exits `0` from a guarded cwd (should
  block).
- **R-008 / R-012** (sed `-i` misclassification): the Python-side fix is correct in isolation,
  but a separate, earlier Bash-text substring pre-filter (independent of the Python fix) still
  misreads `-i` inside an attached `-e` script and false-blocks a read-only direct-path
  inspection (`sed -eiprobe tests/golden/nesc/case1_dragless_sphere.hpp`, no `cd`) — exits `2`
  (should pass). This one is a false block (safe direction), not a bypass, but still violates
  this candidate's own stated goal of preserving read-only inspection.

**Open — minor**:

- **R-013** (new): the R-011 write-command heuristic matches "w" inside ordinary sed
  address/regex text, false-blocking a plain read-only `sed -n '/warning/p' ...` — exits `2`
  (should pass). Recoverable via the existing marker/authorization override; not a bypass.

No findings rejected. No critical findings at any point — all open items are guard-tooling
correctness gaps in a hook whose failure mode is either an over-permissive bypass (blast radius
limited to writes an agent's own session attempts against `tests/golden/**`/
`docs/decisions/**`/tolerance files — still recoverable via git, and every write in this repo
passes through review before merge regardless) or an over-restrictive false block (recoverable
immediately via the existing marker), not an active production or safety defect.

## Joe's attention

- **`.claude/hooks/guard-oracle.sh` is weaker than its own documentation and this candidate's
  own commit messages currently claim.** The R-006/R-008/R-009/R-011/R-012 bypasses mean an
  agent session (with no malicious intent, just an unusual but valid shell construction) could
  currently write to a guarded path without the marker/authorization override catching it. This
  is the single most consequential thing in this candidate.
- Every open finding above was independently reproduced against the actual current hook in this
  session (not taken on Codex's word) — the evidence is concrete command strings and exit
  codes, all in `docs/reviews/workflow-v3-review.md`.
- No dependency, interface, or compatibility changes. No deleted behavior of consequence beyond
  `scripts/tokens.py`'s retirement (already reviewed/approved in an earlier round, R-002/D5
  section of the findings doc).
- Rollback: this entire candidate is 8 commits on an unmerged branch; reverting is `git checkout
  main` / discarding the branch, no product code or `main` history affected either way.
- `docs/reviews/evidence/` (5 retained log files, ~10,600 lines total) is new: durable,
  git-tracked validation/review evidence that previously lived only in gitignored
  `build/validation/`. This is itself part of this candidate's diff and worth a glance if Joe
  wants to confirm the retention approach before it becomes the standing pattern.

## Decision record

State: pending
Human decision/date/reference: pending
Approved candidate/scope: pending
Merge observed: not merged
Release authorization: pending / not requested

Significant tradeoff rationale: this session chose to stop at the explicitly authorized bound
("this authorization does not permit unlimited correction rounds") rather than continue
autonomous fix rounds against Codex's own newly-found issues, even though the guard-oracle gaps
are real and arguably fixable quickly — the tradeoff was disclosure-and-handoff over chasing a
fully green state without renewed authorization.
Accepted risks: none accepted yet — the six open findings above are unresolved, not
risk-accepted; this decision is Joe's to make.
Nonblocking follow-ups: none recorded (every open item above is a blocker, not a deferred
minor, except R-013 which is minor/recoverable but still open).
Architectural decisions: none — no ADR implicated by this candidate.

If `changes-required`: these are implementation defects (guard-oracle.sh correctness gaps),
routing to step 9 (fix, retest, re-review) once Joe authorizes a further bounded batch — not a
planning defect, requirements gap, or architecture conflict.

Joe's explicit decision or squash-merge records approval in this solo-maintainer repo; no
GitHub approving-review badge is required. This packet is not itself a request to merge.
