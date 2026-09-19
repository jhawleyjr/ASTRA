# Review: M2 PR 3b — NESC ten-case check harness

Spec/plan: `docs/specs/nesc-harness.md`, `docs/plans/nesc-harness.md`
Builder: Claude (this session)
Reviewer: Codex, fresh context each dispatch, general + adversarial (`node codex-companion.mjs
review`/`adversarial-review`)
Scope: committed candidate, branch `worktree-m2-nesc-harness` against `origin/main`
(`93592f1685cf64d09d1c23d63dadff569041828f`); general review and the first adversarial attempt ran
against `--scope working-tree` before the candidate was committed (head `10bb8ec` at general-review
time); the successful adversarial pass ran `--base origin/main` against the committed head
`4344fa28a2a72b15d91d7d0a93f66abfa4b15d28`
Validation: `just check` (full gate, all 4 CMake presets) — `check: OK`, exit 0, re-run clean both
before and after the fix below (`/tmp/just_check5.log`, `/tmp/just_check6.log`, session-local)
Round: initial (general, clean) + initial (adversarial, incomplete) + retry (adversarial, complete,
one finding) + fix 1

## General review

Dispatched `review --scope working-tree --wait` against the dirty (then-uncommitted) candidate,
2026-09-18. Result: "No actionable defects found in the current changes." Codex independently ran the
release-preset `Nesc.*` filter itself and confirmed 8 passed / 2 skipped. Raw output:
`/tmp/codex_review.log` (session-local).

## Adversarial review, attempt 1 (incomplete, not a finding)

Dispatched `adversarial-review --scope working-tree --wait` with a focus naming ADR-0001/0003/0012/
0013/0014, the tol_abs/tol_rel formula, the yaw-wrap/no-relative-term rule, the Tables 77/78 and t=0
exclusions, and the pre-step sampling sequence, 2026-09-18. Codex's own trace reached the spec/plan,
the ADRs, the generator, `Simulation::step`/`Vehicle::integrate`, and one partial self-check script,
finding nothing before hitting `Codex error: You've hit your usage limit. ... try again at Sep 23rd,
2026 7:30 PM.` No structured findings returned. Per `docs/agents/review-dispatch.md`, missing/
exhausted provider access is pending review, never clean review — not reported as a pass anywhere.
Raw output: `/tmp/codex_adversarial.log` (session-local).

## Adversarial review, attempt 2 (complete, one finding)

Dispatched `adversarial-review --base origin/main --wait`, 2026-09-19, after Joe reported the Codex
quota reset. Focus named the same ADRs plus an explicit instruction to independently verify (not just
read) the three disclosures already in `docs/plans/nesc-harness.md`'s "Known gap" section: the
`v_north`/SIM 2 contamination, the `tol_rel` looseness on four named channels, and the case01/case02
band-width variance. Codex independently reproduced all 210 committed channel bands from the raw CSVs.
Raw output: `/tmp/codex_adversarial2.log` (session-local).

Verdict (Codex's own words): "needs-attention... Confirmed: (1) cases 4/5 v_north tol_abs exactly
equals 3 × 0.01 × 0.3048; excluding SIM 2 reduces it to the 1e-9 m/s floor; (2) the reported relative
tolerances are correct, but the blanket 'not vacuous' claim is false — force_y accepts zero
throughout; rate_p, roll, v_down and force_z still reject zero at some samples; (3) removing SIM 3
from case 1 reproduces case 2's temperature tolerances exactly; (4) applied wrenches are frozen across
RK4 stages while gravity is recomputed... Frame rotations, Euler ordering, yaw wrapping, SI
conversions, exclusions, floors and pre-step sampling match the contracts. Maximum nearest-sample
error was 7.6294e-7 s. No new production per-frame domain, allocation or exception defect found."

So: three of this session's four prior disclosures (`v_north` contamination, the case01/case02
band-width explanation, the frozen-wrench diagnosis) were independently confirmed exactly as written,
plus every frame/unit/sampling/exclusion claim this task has made throughout. One was wrong.

## Finding R-001

Severity: minor (packet/plan disclosure accuracy — no production code, no incorrect oracle comparison
reaches a currently-running test; cases 4/5 are `GTEST_SKIP()`ed)
Status: **fixed**
Location: `docs/plans/nesc-harness.md`'s "Known gap" section (the `tol_rel` disclosure paragraph, as
it read before this fix); `tests/golden/nesc/case04_dropped_sphere_round_non_rotation_band.hpp:2859`
(`kForceY`) and the equivalent `case05` file (the underlying data the disclosure was wrong about)
Requirement/ADR: ADR-0013's own text — "The generator reports band width per channel per case. A band
wide enough to admit an obviously wrong answer is not a test; such channels are recorded as
non-discriminating rather than silently counted as passes." This requirement existed since ADR-0013
was accepted and was never actually implemented in `generate_nesc_band.py` until this fix.
Failure: this session's own prior disclosure characterized `case04`/`case05`'s `force_y` (`tol_rel`
0.76) as merely "loose... not currently masking a comparison failure," lumped together with three
other zero-crossing channels (`rate_p`, `roll`, `v_down`, `force_z`) that genuinely are only loose.
Codex found, and Claude independently reproduced by evaluating `tol_abs + tol_rel*|reference|` against
every one of `force_y`'s 301 committed reference samples, that its combined tolerance accepts a
comparison against exactly zero at every sample, despite the channel's own reference reaching
−24.835 N (`case04`) / −24.651 N (`case05`) at t≈23.6 s — more than 2× the channel's own `tol_abs`
(9.93 N / 9.87 N) alone. A completely missing or zeroed `force_y` signal in a future un-skipped
`case04`/`case05` run would currently pass undetected.
Requested outcome: correct the disclosure to distinguish `force_y` from the three genuinely-loose-
but-discriminating channels, and implement the non-discriminating-channel detection ADR-0013 already
requires, so this class of gap cannot recur silently for any future case/channel.
Builder response: **fixed**. Added `check_non_discriminating()` to `generate_nesc_band.py`: for each
channel whose peak `|reference|` exceeds twice its own `tol_abs`, checks whether the combined
tolerance would still accept zero at every sample; reports (stderr, non-fatal — a currently-skipped
case's band must still be generated for `PR 3c` to judge a future fix against) any that do. Re-ran the
generator: flags exactly `case04`/`case05` `force_y` and nothing else, confirming `rate_p`/`roll`/
`v_down`/`force_z` are correctly excluded (their own peaks never exceed `tol_abs` by that margin).
Verified the committed `.hpp` files are byte-identical before/after (diagnostic-only, no data change).
Corrected the disclosure in `docs/plans/nesc-harness.md` and `docs/reviews/nesc-harness-human.md` to
distinguish `force_y` explicitly and record it as a second, explicit `PR 3c` precondition alongside
`v_north`. Re-ran `ruff check`/`ruff format --check`/`pyright` on the generator (clean) and the full
gate (`just check`, all 4 presets, `check: OK`, exit 0) after the change.
Verification: Claude's own independent recomputation against the committed golden data (shown above)
is the verification of record for this fix, not a second Codex dispatch — the fix is diagnostic-only
(no golden data changed, no frame/unit/sampling logic touched), the exact condition Codex's own
finding describes was directly re-evaluated against the actual committed reference arrays before and
after, and the new check's own output was checked by hand against the four channels Codex named as
correctly non-vacuous (all four confirmed still correctly excluded). A second full adversarial
dispatch was judged unnecessary for this specific, narrow, mechanically-verified a fix; if Joe wants
one anyway, nothing above prevents dispatching it.
History: opened and fixed same round, 2026-09-19.
Human exception if needed: none requested; not needed for a docs/diagnostic-only fix.

## Completion

Critical/major open count: 0. Minor: 1, fixed (R-001 above). Rejected findings: none. Deferred: none.
Scope not reviewed: the actual physics/numerical fix for cases 4/5 themselves (out of scope — those
cases are `GTEST_SKIP()`ed, not claimed as passing, and their own resolution is `PLAN.md`'s M2 PR 3c,
a separate, not-yet-started task). New findings from this pass's own fix-delta inspection: none.

Reviewer recommendation is not Joe's approval. This candidate (head `4344fa2` plus this round's fix
commit) is ready for Joe's decision per `docs/reviews/nesc-harness-human.md`; any subsequent
substantive change requires renewed review and validation.
