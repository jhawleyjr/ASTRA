# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
PR 3c: per-RK4-substage wrench evaluation (`PLAN.md`'s M2 closer, now checked off, along with PR 3a/
3b retroactively and the M2 row itself — all ten NESC cases pass under all four presets). Worktree
`m2-pr3c-substage-wrench`, branch `worktree-m2-pr3c-substage-wrench`, **committed (`d9906ea`, packet
fix `4ee118e`, STATE.md `3339e98`) and pushed**, **PR** <https://github.com/jhawleyjr/ASTRA/pull/34>
against `main`'s `a3d23f7`, **approved by Joe 2026-09-19 and marked ready for review** — not yet
merged (Joe's own squash-merge is the outstanding action). PR body is the human-review packet
(`docs/reviews/pr3c-substage-wrench-human.md`). Spec/plan `docs/plans/pr3c-substage-wrench.md`,
approved by Joe 2026-09-19 after one Codex plan critique (6 findings, fixed) and one correction
round (Joe's own 2 findings plus a second Codex critique's 3 more, all fixed pre-implementation).
ADR: `docs/decisions/0016-per-rk4-substage-wrench-evaluation.md`, accepted 2026-09-19 after the dry
run below — `touch-goldens` was armed by Joe for that one file only; nothing under `tests/golden/`
was written this session.

## Last verified result
Full validation and both required review passes complete and clean, this session. `just check`:
`check: OK`, exit 0 (first run failed `format-check` on new comment line-wraps, fixed with
`clang-format-18 -i`, reran clean) — all four presets (debug/release/static-plugins/asan), 290/290
tests each, `hooks: 144/0`, pyright 0 errors, pytest 32/32. NESC dry run (asan, cases 4/5
skip-lifted, kept lifted — not re-skipped): all ten cases pass; full per-channel
`max(|diff|)/tol_abs` table (21 channels × 2 cases) computed against the actual implementation,
every value ≤ 1.0, worst `pressure` at 0.194 (table in ADR-0016). `v_north`, the frozen-wrench
code's worst channel at 4.85 million× `tol_abs`, now misses by 0.0033–0.0044. The "reduce `dt`
instead" alternative was measured against the real unmodified code and rejected (`v_north` still
>1.2 million× `tol_abs` at 4x cost). Codex general review: no actionable defects. Codex adversarial
review (focus: state-revert after a non-k1 fault, health-counter semantics under warn/halt, Dryden
bitwise determinism, ADR-0001/0002 units/frames on the new `wrench_total` signals): verdict
**approve**, no material findings, including an independent re-run of the Dryden determinism tests.
Zero fix rounds needed. Full record in the plan doc's own "Plan critique" / "Correction round"
sections and the human-review packet.

## Next step
PR #34 approved by Joe and marked ready; **Joe's own squash-merge** is the only outstanding action.
Nothing outstanding on the review side. One new disclosed, unstarted `PLAN.md` follow-up item: the
atmosphere-domain-check misattribution window (same accepted-gap class as ADR-0012's gravity/Earth
instance), needs its own plan review before code, not urgent (no NESC case exercises it).

## Tried / failed
Correction rounds: 1/2 used (plan-review stage, before implementation — both Joe's own findings and
a second Codex critique pass's findings, all fixed; implementation/review stage itself needed zero
further rounds). Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
