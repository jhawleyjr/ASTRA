# M7 PR 16 (Tier 0): M7 closeout

Status: **approved by Joe with amendments 1 to 6, 2026-10-15**, in plan mode. Decisions: D1 as drafted, his
approval of the §16 text at human review (both texts verbatim in the PR body under "Needs Joe's decision"); D2
recorded, plus an `(unscheduled)` review item (amendment 2); D3 yes; D4 yes, all four; the ADR clause reads PR
1a, 1b and 3c (amendment 5); step 5 per amendment 6. `git fetch`: `origin/main` = `c3586c3`, unchanged; worktree
`m7-closeout`. The plan-mode draft was moved here from the main checkout, which keeps no copy. No Codex plan
critique (Joe's brief). Model: `docs/plans/m6-pr10-closeout.md`, `docs/plans/m5-pr9-closeout.md`.

Amendments (Joe, 2026-10-15):
1. D1: the §16 row and note stand as drafted, for Joe's approval at human review; both go verbatim in the PR
   body under "Needs Joe's decision".
2. D2: record it and add the item. The PLAN.md clause says #113 merged at its commit 4 on Joe's decision of
   2026-10-10, the accepted exception to the Tier 3 review and gate, and that its aero was first gated in PR
   6c's `991ee89` summary. One `(unscheduled)` open item: a Codex adversarial review of PR 6b's `aircraft/f16`
   diff, `856fff0..c8f3128`, with the `F16Aero` shots and the regeneration test as its evidence; Joe's call
   when. STATE's items list names it.
3. D3: yes. The five §7, §12 and §15 markers become "planned, M8 (M7's PR 13, moved at the M7 closeout)"; the
   rev 14 line names those sections; the two code comments stay and STATE carries the thread.
4. D4: yes, all four.
5. The ADR clause reads PR 1a, 1b and 3c; the brief's "1a, 1b" was wrong.
6. Sequence step 5: if `origin/main` moved, merge it in and rerun both checks. If the merge touches any file
   this PR edits, rerun the Codex pass on the combined diff and record the second thread id; otherwise record
   the fetch result and the unchanged verdict in the PR body.

Done on the task branch: `git merge-base --is-ancestor <cand> <head>` exit 0 for all 13 gated PRs, and for
`c8f3128` in `991ee89`. `c8f3128^` = `856fff0`. Outside `aircraft/f16/`, PR 6b's diff holds only its plan and
`tests/python/test_f16_generated.py` (the regeneration test).

## Context

M7's scheduled PRs are merged. PR 16 of `docs/plans/m7-f16-nesc-oracle.md` closes the milestone. It checks M7
off in PLAN.md with evidence clause by clause, re-homes what left M7, rewrites architecture §16's M7 row to
the delivered scope, adds a dated closeout line to the M7 plan, and rewrites STATE.md. It is docs only, with no
code, test, config, golden, tolerance or ADR change, so there is no full gate (the Tier 0 row). Acceptance:
`just docs-check`, `git diff --check origin/main..HEAD`, and one fresh Codex pass on the final diff.

## Verified facts (read this session from git and gh, not from STATE.md)

`git ls-remote origin main` = `c3586c38` = local `main`, with a clean tree. `gh pr list --state merged`
gives #104 to #119. The gated candidates come from each squash's added `docs/reviews/evidence/*-gate-summary.log`.
Ancestry: `git rev-list --count <head>..<cand>` = 0 for every candidate. In planning, `no-main.sh` blocks the
word `merge` on main, so this stands in for `git merge-base --is-ancestor`, which reruns on the task branch.
`git log --name-only cand..head` lists the delta after each candidate. All summaries below are exit 0 with
`check: OK`. CTest counts are debug/release/static-plugins/asan, and pytest counts carry 5 skipped.

| PR | # | squash | head | last gated | CTest | pytest | delta after the gate (each disclosed in its PR body) |
|---|---|---|---|---|---|---|---|
| planning | 104 | 9772031 | 800f8d4 | — (Tier 0) | | | |
| 1a | 105 | 37e8f4e | 6be926e | c236ade | 669–672 | 1863 | log, STATE, Joe's acceptance commit (ADRs' Status, rev 11, §16 note) |
| 8 | 106 | f5d31c8 | 387f59d | 6f19bd8 | 673–676 | 1863 | Markdown, logs; `018f5de` failed (tidy), full log committed |
| 6a | 107 | 6e0f3de | 4b4830f | — (Tier 0) | | | |
| 5 | 108 | 2f0a1eb | fe6a98a | 0750b65 | 673–676 | 1912 | log, README, STATE |
| 1b | 109 | 56b17bd | 7ce07f2 | 6a54518 | 673–676 | 1912 | Markdown (ADRs, architecture), log |
| 3b | 110 | 58b681f | 9e1fc3f | 0661f80 | 687–690 | 1912 | Markdown, log |
| 4 | 111 | e4c9c63 | 08ffecb | 1fb5d08 | 692–695 | 1912 | log, PLAN, STATE |
| 2 | 112 | 856fff0 | efc52d0 | 5bdb079 | 699–702 | 1940 | Markdown, logs; `a1254fc` failed (tidy) |
| **6b** | 113 | c8f3128 | 23274d5 | **none** | | | see D2 |
| 3c | 114 | c87f202 | 360d5fe | beffa64 | 729–732 | 2079 | Markdown (ADR-0033's acceptance, architecture), log |
| 6c | 115 | acdc6b5 | 52ee7f1 | 991ee89 | 738–741 | 2079 | Markdown, log |
| 7 | 116 | 5a22e9a | aadf35d | ce9dfa8 | 742–745 | 2079 | Markdown, log |
| 14 | 117 | bff4c4e | c1b865a | 852d8a7 | 742–745 | 2110 | its log only (`1993270`, `5b2d129` superseded) |
| 9 | 118 | 86b17ce | c5b838d | 4b19a49 | 751–754 | 2110 | log, STATE (`faa5f14` superseded) |
| 11a | 119 | c3586c3 | 23768bc | b01e083 | 800–803 | 2110 | Markdown, logs; `2ff2e71` (ruff) and `d4c0aeb` (tidy) failed, full logs committed; `09cf396` superseded by fix round 1 |

Further facts the edits rest on:
- **ADR-0033 came from PR 3c (#114), not from PR 1a or 1b.** ADR-0028, 0030 and 0031 came from #105, and
  0029 and 0032 from #109. All six are accepted (Status lines 2026-10-05, 10-09 and 10-11). So the ADR clause
  reads "PR 1a, 1b and 3c".
- **PR 6b has no gate or code review of its own.** #113's body ends "Pending: the implementation, the full
  gate, Codex adversarial review". #113 merged at commit 4 (the aero) on Joe's Q1 decision of 2026-10-10. Its
  only Codex pass was the plan critique, thread `01a1277c-…`. The 6b squash `c8f3128` is an ancestor of
  `991ee89`, so its aero was first full-gated in PR 6c's summary. 6c's adversarial review ran on
  `--base origin/main` = `c87f202`, which already held 6b, so no Codex pass has read 6b's aero diff.
- The open items with an (M7) prefix are exactly two (PLAN.md l. 564, rotation consistency; l. 600, the
  envelope monitors). The three unscheduled M7 items (PR 11b, 12, 15) and the (M9) ICD v2 item exist already.
- Requirements and their M7 status (the M7 plan's Acceptance table and PR lines):
  - Delivered: R1 to R5, R8, R12, R16, R17, R19, R22, R23 (11 and 13.x), R9 (11, 12 and 13.x), and the peer
    halves of R10 and R11.
  - R6: met for 13.1 and 13.2. Not met for 13.3's and 13.4's six aero channels, a recorded deviation.
  - R21: per PR, with 6b's exception (D2).
  - Not delivered: R7, R13, R14, the port halves of R10 and R11 (PR 11b, 12: unscheduled), R20 (PR 15:
    unscheduled), R18 (PR 13: to M8), R15 (PR 3: to M9).
- Stale "M7 PR 13" markers sit outside §16 and §18: architecture §7 (l. 292, 329), §12 (l. 487) and §15 (l. 616,
  622). Two code comments carry them too, `aircraft/f16/physical/aero/aero_math.hpp:86` and
  `aircraft/f16/tests/aero_test.cpp:756`, and so does ADR-0031 l. 17 ("Governs PLAN.md's M7 PR 13"; immutable).
- The docs step finds:
  - README l. 8 and the roadmap (l. 420): "M0 to M6 are done … Next: M7";
  - `docs/future-work.md` l. 23–24: M7 "Takes the open items on Earth/gravity rotation consistency, … §15
    envelope monitors";
  - `docs/future-work.md` l. 164: "no tutorial … yet (M7)".

## Decisions for Joe (recommendations as defaults)

- **D1. §16's exact wording (approved at human review, as for M6).** See "§16 text" below. It also rewrites
  §16's M7 note (l. 655), which still says "planned, by PLAN.md M7 PRs 1a to 16", as M6 PR 10 rewrote M6's.
- **D2. PR 6b's missing gate and review, recorded rather than repaired (recommended).** The PLAN.md clause
  says: "merged at its commit 4 on Joe's Q1 (2026-10-10), with no full gate or Codex code review of its own;
  first gated in PR 6c's summary; its evidence is `F16Aero`'s 16 checkData shots and the byte-for-byte
  regeneration test". Alternative: an `(unscheduled)` open item for a Codex review of 6b's aero files. That
  is your call; I add it only if you say so. *Joe (amendment 2): record it and add the item.*
- **D3. The other "M7 PR 13" markers (§7, §12, §15) become "*planned*, M8 (M7's PR 13, moved at the M7
  closeout)" (recommended).** M8's own plan numbers its PRs. "M7's PR 13" stays the design's name, matching
  ADR-0031's immutable "Governs PLAN.md's M7 PR 13". This edits five architecture lines beyond your item 4. The
  two code comments are not edited (docs only). STATE carries a thread for the next PR that edits either file.
- **D4. Docs step, in repo scope, as at every milestone closeout:**
  - README: M0 to M7 done; next M8; "The F-16 joined in M7".
  - `future-work.md` l. 23–24: the envelope monitors move to M8 and rotation consistency to M9.
  - `future-work.md` l. 164: the tutorial is unscheduled.
  - PLAN.md l. 655: the notional-transport item's "and the F-16 will (PR 6b)" becomes "does (M7 PR 6b)".

## Changes

### 1. PLAN.md, M7 block
- `- [x] **M7 F-16 and the NESC oracle**`. The row text stays, with its last sentence replaced by: "Cases 15
  and 16 (PR 11b), the port (PR 12) and the tutorial (PR 15) left M7 on Joe's decision of 2026-10-13 (the open
  items below); the envelope monitors (PR 13, ADR-0031) moved to M8, and rotation consistency (PR 3) to M9,
  at the closeout (Joe, 2026-10-15)."
- Then "Evidence, clause by clause" in M6's form. Each clause links `[summary](docs/reviews/evidence/<cand>-gate-summary.log)`,
  cites acceptance counts "in the PR body", and gives its R-IDs:
  - **The F-16 from the template**, its aero, mass and engine pinned to the DAVE-ML check data:
    - PR 6b: R2, `F16Aero`'s 16 shots and the regeneration test, with D2's disclosure;
    - PR 6c: R1, R3, `F16Mass` and `F16Engine`'s 9 shots; 738–741, 2079 (`991ee89`).
  - **Cases 11 and 12 open loop**: PR 9. R5, R4's F-16 half, R9 for 11 and 12; 751–754, 2110 (`4b19a49`).
  - **Cases 13.1 to 13.4**: PR 11a. R6, R9 for 13.x, R10's and R11's peer halves, R12, R22, R23; 800–803, 2110
    (`b01e083`). The line says **R6 is recorded as not met for 13.3's and 13.4's six aero channels**, never as a
    pass, with the measurement linked (`docs/reviews/evidence/m7-pr11a-r6/`).
  - **Trim at every case's initial condition**:
    - PR 5: R4; 673–676, 1912 (`0750b65`);
    - PR 9: the configs rebuild the trim bit for bit, `Case1{1,2}TrimResiduals`;
    - PR 11a: the four configs carry case 11's trim, R23.
  - **Planted defects**:
    - PR 9: the elevator's sign fails force_z; SIM 2's zero-inertial trim fails rate_q in 11 and 12; Izx's
      sign is reported;
    - PR 11a: the elevator fails pitch in all four, the aileron roll in 13.3 and 13.4; Izx's sign and the
      wrong divisor are reported.
  - **The config-key reference**: PR 14. R19 and R17's reference half; 742–745, 2110 (`852d8a7`).
  - **ADR-0028 to ADR-0033**:
    - PR 1a: 0028, 0030, 0031; 669–672, 1863 (`c236ade`; Joe's acceptance commit after the gate);
    - PR 1b: 0029, 0032; 673–676, 1912 (`6a54518`);
    - PR 3c: 0033; 729–732, 2079 (`beffa64`).
  - Beyond the row:
    - PR 2: R16; 699–702, 1940 (`5bdb079`);
    - PR 3b: ADR-0032 in the core; 687–690, 1912 (`0661f80`);
    - PR 4: R17; 692–695, 1912 (`1fb5d08`);
    - PR 7: Q5's aero-only route; 742–745, 2079 (`ce9dfa8`);
    - PR 8: R8 and the eight band headers; 673–676, 1863 (`6f19bd8`);
    - PR 6a: Tier 0.
  - The closing sentence:
    - R21 is each summary above; PR 6a and 16 are Tier 0, and 6b per D2.
    - ADR-0028's bands for 15 and 16 stay generated and unflown.
    - R7, R13, R14, R18, R20, R15, and the port halves of R10 and R11, left with the moved and unscheduled
      work.
- A new PR line: `PR 16 (#NN): M7 closeout …`. Plans: [milestone](…) and [PR](docs/plans/m7-pr16-closeout.md).
  Packet: in the PR body. `#NN` is filled in once the draft exists, and no line carries a merge-only fact.

### 2. PLAN.md open items
- The l. 600 envelope item becomes **(M8)**: "M8's first PR, M7's PR 13 design unchanged (ADR-0031, the M7
  plan's row 13 and Q16): it also sets case 12's explicit Mach action and the NESC harness's warn-counter
  policy (N-07). The reason is that M8's tuning and M10's halts consume the envelope; moved at the M7 closeout
  (Joe, 2026-10-15)." The rest of the item stays.
- The l. 564 rotation item becomes **(M9)**: "re-prefixed at the M7 closeout (Joe, 2026-10-15), still on Joe's
  go, with its own plan review and `kCoreApiVersion` past 11 (the M7 plan's row 3, R15)". It also gets PR 11a's
  Q4 sentence: `nesc_control` refuses no Earth model, and `F16Control.TrackedConfigsPairThePeerWithTheRotatingEarth`
  enforces the pairing.
- No other item has an (M7) prefix (verified). The three unscheduled M7 items stay as they are.

### 3. docs/architecture.md (Joe's file)
- Status header: append "rev 14, 2026-10-15: §16's M7 row and note and §18's tutorial marker (and §7, §12,
  §15's envelope markers, D3) for the M7 closeout (PLAN.md M7 PR 16); no ADR changes."
- §16 row and note: the text below (D1).
- §18 l. 683: "*Unscheduled* (PLAN.md M7 PR 15, Joe 2026-10-13; PLAN.md's open item): a `docs/tutorial.md` that
  walks these five steps from the F-16 path, with a test running its commands on a throwaway `just
  new-aircraft` copy."
- §7, §12, §15: D3's marker only. §10 and §11 stay as PR 11a set them.

#### §16 text (D1, for approval at human review)

Row:

> | M7 | `aircraft/f16`, built from the template, flies NESC atmospheric cases 11, 12 and 13.1 to 13.4 inside
> bands derived from the participating simulations' own disagreement (members, window and band shape per
> *ADR-0028*, which supersedes ADR-0013 in part for these cases and elaborates ADR-0014 and ADR-0015). Every
> channel is gated except 13.3's and 13.4's six aero force and moment channels. Those are reported: a recorded
> deviation, never a pass. Its mass, aero and propulsion reproduce the NESC DAVE-ML model, table edges
> included, checked against the model's own check data. Trim meets its residual thresholds at every case's
> initial condition. The NESC `F16_control` law runs sim-side at the exchange stage as an in-process peer
> (*ADR-0029*). Each planted defect fails a discriminating band, or is reported where the measurement put it
> inside every band. The generated config-key reference is current. Not in M7: cases 15 and 16 (their
> ADR-0028 bands generated and unflown), the `embedded/` port and the tutorial are unscheduled (Joe,
> 2026-10-13). The envelope monitors (*ADR-0031*) moved to M8, and the refusal of Earth and gravity models
> that disagree on rotation moved to M9 |

Note:

> M7 note (rev 11, amended rev 12 and 14): the M7 row is built, by PLAN.md M7 PRs 1a, 1b, 2, 3b, 3c, 4, 5,
> 6a to 6c, 7, 8, 9, 11a and 14, and closed out by PR 16. `docs/plans/m7-f16-nesc-oracle.md` R1 to R6, R8,
> R9, R12, R16, R17, R19 and R21 to R23 check it, PR by PR, with the peer halves of R10 and R11. R6 is not met
> for 13.3's and 13.4's six aero channels. Its oracle rests on *ADR-0028*: SIM 4 and SIM 5 only, a gated
> window of t ≥ 8 s, ADR-0013's scalar band with k = 3, and the aero channels compared with an aero-only,
> frame-start-stage wrench, never `wrench_total`. Its identifier wording rests on *ADR-0030*, its
> flight-software rows on *ADR-0029*, and the F-16's body rate and altitude on *ADR-0032* and *ADR-0033*.
> *ADR-0031*'s envelope declaration is accepted and unbuilt (M8).

### 4. docs/plans/m7-f16-nesc-oracle.md (two lines, nothing else)
- Under Status: "*Closed out 2026-10-15 (PLAN.md M7 PR 16).* Delivered: …. Left M7: …." It names what was
  delivered: the F-16 (6b, 6c), cases 11, 12 and 13.1 to 13.4 (9, 11a, with R6 not met for 13.3's and 13.4's
  six aero channels), trim, the planted defects, the config-key reference, and ADR-0028 to 0033. It names what
  left: 11b, 12 and 15 unscheduled (Joe, 2026-10-13); 13 to M8 and 3 to M9 (Joe, 2026-10-15).
- Under "Done when": "*Closed out 2026-10-15:* not every R passed. R6 is a recorded deviation, R7, R10's and
  R11's port halves, R13, R14 and R20 left unscheduled, R18 went to M8 and R15 to M9, and PR 6b merged without
  a gate of its own (PLAN.md's M7 row)."

### 5. STATE.md (by /wrap, ≤ 40 lines, as main reads it after the merge)
- Next step:
  1. The oracle planning PR Joe briefs: closed-loop oracles in place of M7 PR 11b, 12 and 15 (a sampled-data
     check on a linear plant, the port pattern on M4's autopilot, symmetry tests).
  2. M8 planning, starting with PR 13 (ADR-0031; case 12's Mach action and the warn-counter policy).
  3. PR 3 (M9) on Joe's go.
  4. The key-block rule for new plugins stays (current item 4).
- Open threads:
  - Closed: PR 11a's and the M7 threads.
  - Kept: the D8 comments, the evaluator wrap artifact, the notional-transport rate-frame item, trim and
    bands, lessons, and PLAN.md's items list.
  - Added: the two code comments that still say "M7 PR 13".

## Sequence
1. On approval: `git fetch`; `EnterWorktree` `m7-closeout` from `origin/main`; move this plan in and delete
   the draft; rerun `git merge-base --is-ancestor <cand> <head>` for all 13 gated PRs, expecting exit 0 for
   each.
2. Edits 1 to 4. Re-read every number and SHA against its linked summary. Run `just docs-check` and
   `git diff --check origin/main..HEAD`. Commit and push, then `gh pr create --draft` (Tier 0: scope and
   checks, no full gate).
3. Fill in `#NN`. Run `/wrap`'s repo-scope docs step (D4) and write STATE.md. Rerun both checks, then commit
   and push. `/wrap`'s docs edits land before step 4, so the Codex pass covers them.
4. `/review-cycle`: one fresh Codex pass on `origin/main..HEAD`. A factual error gets one correction round,
   with both checks rerun. Anything else becomes a follow-up in the PR body.
5. `git fetch`. If `origin/main` moved, merge it in and rerun both checks. If the merge touches any file this
   PR edits, rerun the Codex pass on the combined diff and record the second thread id. Otherwise record the
   fetch result and the unchanged verdict in the PR body (amendment 6).
6. `/human-review`: the PR body with the checks, Codex's verdict and thread id, and "Needs Joe's decision"
   (D1's text, D2, D3). Then `/wrap`'s final handoff, and stop. The merge is Joe's.

## Verification
- `just docs-check` exits 0, so the new evidence links resolve.
- `git diff --check origin/main..HEAD` is clean.
- `git diff --stat origin/main` lists only PLAN.md, STATE.md, README.md, docs/architecture.md,
  docs/future-work.md, docs/plans/m7-f16-nesc-oracle.md and docs/plans/m7-pr16-closeout.md.
- No path under `core/`, `plugins/`, `aircraft/`, `tests/`, `configs`, `docs/decisions/` or `tests/golden/`
  changes.
