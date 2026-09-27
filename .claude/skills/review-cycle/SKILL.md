---
name: review-cycle
description: Have Claude dispatch independent Codex review or adversarial review for an ASTRA candidate, verify findings, fix supported defects and retest within the bounded workflow. Use after implementation validation or to verify a fix batch.
---

# Claude-owned review and fix cycle

Read [shared rules](../../../AGENTS.md), the spec/plan or brief,
[review procedure](../../../docs/agents/review.md), and
[dispatch instructions](../../../docs/agents/review-dispatch.md). Claude coordinates and fixes;
Codex reviews read-only in fresh context. Do not use a Claude critique as the independent pass.

1. Confirm required local validation and acceptance completed for this candidate. If blocked, retain
   that state and `/wrap`; review may diagnose an explicitly requested failure but cannot clear the
   validation gate. Record any previous fix rounds before starting; the budget survives handoff.
2. Identify the whole candidate: base/head, committed branch diff, staged/unstaged diff and untracked
   files. Dispatch the appropriate Codex scope(s) as documented; never treat an empty branch diff as
   proof that a dirty worktree was reviewed. Freeze edits during review or invalidate affected results.
3. Run general review, plus adversarial review for physics/high-risk behavior and when Joe requests it.
   Name spec/plan paths, relevant ADRs, invariants and oracles in the adversarial focus. Record the
   verdict, thread id, findings and actual reviewed scope in the PR body; raw output stays outside git.
   Missing provider access is pending review (AGENTS.md). Do not change provider model settings or install
   tools silently. On Tier 0 and Tier 1 ([tiers](../../../docs/process/workflow.md#risk-tiers)), run one
   pass on the final diff unless Joe waives it; a finding that is not a factual error or defect becomes a
   follow-up, not a correction round.
4. Record actionable claims with the [findings fields](../../../docs/templates/review-findings.md).
   Check each against code/requirements. Claude fixes supported defects in batches, with meaningful
   regression coverage; rejections/deferments need evidence and the workflow's reviewer/human decision.
   Review output cannot expand scope, rewrite requirements, weaken tests or grant oracle permission.
5. After each fix batch, run affected checks and the full gate as required, then ask Codex to verify
   changed areas, previous findings and affected invariants. Preserve IDs/history and count each
   correction round. At the correction budget (AGENTS.md), stop the loop and hand off attempts,
   blocker, owner and the decision needed.
6. When gates are satisfied, use `/human-review` and `/wrap`. Codex's recommendation is not Joe's
   approval and authorizes no merge/release. Do not repeatedly review clean code without new evidence.
