# Review: M7b PR 4, oracle (c), the F-16's mirror and heading symmetry under F16_control

Spec/plan: [docs/plans/m7b-pr4-f16-symmetry.md](../plans/m7b-pr4-f16-symmetry.md); the
[M7b plan](../plans/m7b-closed-loop-oracles.md), oracle (c); ADR-0029; ADR-0034.
Builder: Claude (Opus 5.5).
Reviewer: Codex, companion 1.0.6 (Codex CLI 0.160.0), its default model (`~/.codex/config.toml`: `gpt-6.1-sol`
at reasoning effort high). Fresh context, read-only, two passes:
- the general `review --base origin/main`, thread `01a15d59-b08c-7fa0-beb1-baf07783ddec`;
- `adversarial-review --base origin/main` with the PR's focus, thread `01a15d59-b188-7ff3-a8f5-56533448dc54`.

Scope: the branch diff `fe922b0..6734b08`, 14 commits, with a clean tree. The code head is `def7d91`, and the
commit after it is evidence and Markdown only. The plan's own critique is in the plan's §18.
Validation:
- the full gate on `def7d91`, exit 0 and `check: OK`, 1294 s ([summary](evidence/def7d91-gate-summary.log));
- the second flight at `9116375` ([log](evidence/9116375-f16-symmetry-second-flight.log)), after the first flight
  at `a1c86ed` ([log](evidence/a1c86ed-f16-symmetry-first-flight.log)), whose heading miss led to §9's dated
  deviation (Joe's fourth decision);
- R15's before-and-after ([log](evidence/m7b-pr4-r15/r15_before_after.log)), and C1's pre-flight facts
  (`evidence/m7b-pr4-trim/`, `evidence/m7b-pr4-inventory/`).

Round: initial only.

**The general review:** no actionable defects. Codex ran the release symmetry binaries (24 tests passed) and `git
diff --check`. It did not rerun the full gate.

**The adversarial review:** verdict approve, with no material findings. Its focus carried Joe's two requests:
- **The floor** in `f16_symmetry_heading_test.cpp` (`floor_at_resolution`) matches §9's deviation. It is defined
  per channel at the whole base run's maximum, as the spacing above, and applied uniformly, with F = 2²⁴ and P
  unchanged.
- **No other constant moved.** The code diff `a1c86ed..HEAD` holds only the floor and its comments.

Codex also found that the masks, the read order, the stage rebuild, parts (a) to (e), the controls and the wrap
replay match their registrations. R15's evidence supports unchanged shipped defaults, and no shipped `.cpp`,
protected path or `unit` label changed. Both release test executables exited 0, with 24 of 24 passing. It
inspected the R15 and full-gate evidence without rerunning them, and edited no file.

## Completion

- Critical and major findings open: none. No finding was raised, rejected or deferred.
- Correction rounds used: 0 of 2.
- **The scope reviewed:** the branch diff `fe922b0..6734b08`, by both passes.
- **Validation:**
  - the full gate on `def7d91`, exit 0, 1294 s;
  - `ctest --preset release -R F16Symmetry`: 24 of 24 at `9116375`, and again in the gate;
  - the smokes in every preset and the asan acceptance command (62 of 62) at the first flight, `a1c86ed`. The
    deviation changed only the release-only heading test, and the gate on `def7d91` ran every preset.
- Codex's approval is not Joe's. The merge is his.
