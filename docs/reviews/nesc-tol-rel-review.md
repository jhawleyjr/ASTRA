# Review: NESC oracle ADR-0015 — drop tol_rel, absolute atmosphere floors, exclude SIM 2 from cases 4/5

Spec/plan: `docs/plans/nesc-tol-rel-removal.md` (approved by Joe across three review rounds);
`docs/decisions/0015-nesc-tol-rel-drop.md` (accepted)
Builder: Claude (this session)
Reviewer: Codex, fresh context, general + adversarial (`node codex-companion.mjs review` /
`adversarial-review`)
Scope: committed candidate, PR #32, branch `worktree-m2-tol-rel` against `origin/main`
(`6f6acd6cd86e80bca5420770c9273826528d9374`), head `9e66a9a` — no uncommitted changes at dispatch
time (`git status --short --untracked-files=all` empty), so both passes cover the whole candidate.
Validation: `just check` (full gate, all 4 CMake presets) — `check: OK`, exit 0, 283 tests each preset;
`ctest --preset {asan,debug,release,static-plugins} -R 'physics.Nesc'` — 8 passed, 2 skipped, 0 failed,
each preset. Regenerated golden diff checked channel-by-channel against a table predicted independently
from the raw CSVs before regenerating (recorded in the brief).
Round: initial (general, clean) + initial (adversarial, clean)

## General review

Dispatched `review --base origin/main --wait`, 2026-09-19. Result: "No actionable defects found."
Codex independently recomputed all ten committed bands from the raw CSVs and independently reran the
release-preset ASan `Nesc.*` test binary (leak detection disabled), confirming 8 passed / 2 skipped.
Did not run a fresh build or the full gate itself (this session's own full-gate evidence above covers
that). Raw output: `codex_review.log` (session-local, `/tmp/claude-1000/.../scratchpad/`).

## Adversarial review

Dispatched `adversarial-review --base origin/main --wait`, 2026-09-19, with a focus naming ADR-0015
(and ADR-0013/0014), the `tol_abs`-only pass criterion, the `excluded()` blanket SIM-2/cases-4-5 rule,
the new per-channel atmosphere floors and their ISA-76 sea-level sourcing, `check_non_discriminating()`'s
structural-tautology claim, byte-identical-ness of already-SIM-2-excluded channels in cases 4/5, and the
diff's scope boundary (no `core/`/`plugins/`/`WrenchSource`/`Integrator`/`Eom` touch).

Verdict (Codex's own words): "approve... No material defects found in 9e66a9a against origin/main.
Read-only recomputation matched all case 4/5 references and absolute tolerances to raw CSVs; previously
excluded channels remain identical apart from tol_rel removal. All other exclusions and eight cases'
numeric data are unchanged. All 30 atmosphere floors are nonbinding (minimum margin 220,257×); all 210
bands have positive tolerances and cannot trigger the retained check. No production code changed. The
only allowlist exception is the requested plan brief. Analysis commands exited 0; no builds or tests
ran." Raw output: `codex_adversarial.log` (session-local).

Independently confirms, against the actual committed data (not just the prose describing it): the
`tol_abs`-only pass criterion is correctly and exclusively implemented; the `excluded()` rule's scope
is exactly cases 4/5 and nothing else changed; the atmosphere floors never bind anywhere (minimum
margin 220,257×, consistent with this session's own ~220,000× figure for case02's temperature); and
`check_non_discriminating()`'s retained-as-tautology rationale holds given the actual `FLOOR_ABS` table
across all 210 committed channel bands.

## Completion

Critical/major open count: 0. Minor: 0. Rejected findings: none. Deferred: none. No findings recorded
(zero findings is a valid, evidence-backed outcome here, not an unreviewed gap — both passes ran
against the full committed candidate with a fresh build/test recomputation, not a self-review or a
prose-only read). Scope not reviewed: PR 3c's own non-gating dry-run baseline table (informational,
not part of this candidate's own behavior) and the physics/numerical correctness of cases 4/5
themselves (out of scope — `GTEST_SKIP()`ed, not claimed as passing; PR 3c's own job).

Reviewer recommendation is not Joe's approval. This candidate (head `9e66a9a`) is ready for the
human-review packet; any subsequent substantive change requires renewed review and validation.
