# M6 PR 10 (Tier 0): M6 closeout

Status: **approved by Joe with amendments 1 to 6, 2026-10-04**, in plan mode. Decisions: D1 yes (both), D2
the PLAN.md open item, D3 restated per amendment 1 (below; Joe's choice comes at human review), D4 as stated,
D5 yes. Base `origin/main` `3a50f6f`; worktree `m6-pr10-closeout`. The plan-mode draft was moved here from the
main checkout and deleted there.

## Context

M6's PRs 1 to 9, 7b and 8b, and 11 to 14 are merged (`origin/main` = `3a50f6f`, verified with `git fetch`).
The PR 10 row of `docs/plans/m6-analysis-tooling.md` closes the milestone. It checks M6 off in PLAN.md with
evidence links, records the held-out scope as open items, and flips architecture §12's status. It also carries
Joe's two #102 notes and his choice on the review-record thread. Docs only: no code, test, golden, tolerance
or ADR change, so no full gate (Tier 0 row). Acceptance: `just docs-check` (`scripts/docs_check.py`, `bash -n`
on both check scripts, and `git diff --check` on the working tree), `git diff --check origin/main..HEAD` (lesson
2026-10-04), and one fresh Codex pass (amendment 4). Model:
`docs/plans/m5-pr9-closeout.md`.

## Verified facts (read this session, not from STATE.md)

**Merged M6 PRs** (`gh pr list`). Squash merges on main, PR heads, and the last gated candidate inside each
head. A candidate counts when it is an ancestor of the head (`git merge-base --is-ancestor <gated> <head>`
exit 0 for all 15, run on the task branch; in planning, `no-main.sh` blocks the word `merge` on main, so
`git rev-list --count head..cand` = 0 stood in) and `git log --name-only cand..head` touches only STATE/PLAN/lessons/
evidence, or a disclosed Markdown delta:

| PR | # | squash | head | gated | full-gate numbers in the summary (all exit 0, `check: OK`) | acceptance (in the PR body) |
|---|---|---|---|---|---|---|
| 1 | 79 | 02bddfa | 72d965a | 574f9fe | CTest 621/622/623/624; pytest 473 | Tier 3 adversarial review approved through `574f9fe` |
| 2 | 80 | da52239 | faee982 | ede0cf3 | 649/650/651/652; 510 passed, 5 skipped | asan `RunLog\|Hdf5\|NoAlloc` 89/89; `-k 'log or replay or montecarlo or scenario or lockstep'` 237 passed; regression 2/2 |
| 3 | 81 | daea24d | 4fdde8b | e1a8dec | 649–652; 579 passed | `-k cli` 72 passed |
| 4 | 82 | 7f61ac1 | 2f6fa56 | 2de663a | 658–661; 1204 passed | `Units` 16/16; `-k 'units or fgbridge'` 656 passed |
| 5 | 89 | 8e97040 | d719245 | 8126aff | 658–661; 1314 passed | `-k plot` 110 passed (`3919ab0`, same code) |
| 6 | 92 | 5858a61 | e955815 | 3a09f45 | 658–661; 1402 passed | `-k mc_analysis` 75 passed |
| 7 | 93 | a0c7903 | b4ed628 | 80f239e | 669–672; 1479 passed | `-k mc_sizing` 77 passed (`3897d06`) |
| 7b | 94 | 51410e2 | 614ef24 | f184b4c | 669–672; 1485 passed | `-k planted` 8 passed; timing log `f184b4c-planted-timing.log` |
| 8 | 95 | 8055ce7 | b7e02cb | 02152f2 | 669–672; 1687 passed | `-k report` 228 passed |
| 8b | 96 | 72877da | 1f519c5 | efbac92 | 669–672; 1698 passed | `-k 'mc_analysis or report'` 313, `-k plot` 138 (`2579fd7`) |
| 9 | 91 | 7e65a85 | 0344df4 | 100f491 | 669–672; 1402 passed | smoke `FrameStats\|FrameBenchmark` 11/11 |
| 11 | 97 | 6458e04 | c297671 | 53db70f | 669–672; 1822 passed | `-k pyyaml_route` 112 passed |
| 12 | 100 | fa3a3c0 | d0b04e7 | 02af820 | 669–672; 1834 passed | `-k panel_scale` 8 passed |
| 13 | 101 | d851b8f | 17175d2 | 74b6505 | 669–672; 1843 passed | `-k envelope_overlay` 9 passed |
| 14 | 102 | 3a50f6f | 95c4a7b | 5a08393 | 669–672; 1863 passed | `-k plotly_size` 20 passed |

Every pytest count above also has "5 skipped" except PR 1's. Earlier summaries in the same PRs (`4bb7607`,
`c2fc49f`, `e87ff45`, `adbfbed`, `cfb91d4`, `fde0e20`, `d6574b4`, `3897d06`, `e3951fe`, `3aadfa7`, `6d1a26e`,
`a2dbf3c`, `0e90176`) are superseded. Post-gate deltas outside STATE/PLAN/lessons/evidence, each disclosed in
its PR body:
- PR 1: `c11c41d` (Joe's ADR-0027 and §9 wording) and `3371d07` (the acceptance), both docs only, with no
  gate rerun and no Codex pass, waived by Joe;
- PR 6 and PR 14: README edits from `/wrap`;
- PR 11: `main`'s #99 merged in after the gate, with no new gate, per Joe.

The committed summaries hold only full-gate counts, so every acceptance count is cited "in the PR body".

**R-IDs → the PR that passed each** (PR bodies and PR plans): R1, R2, R3, R3b, R4 → PR 5 (R3b's resource check
became PR 8's allow-list; PR 12 updated R4's template ground track); R5 → PR 6 (kept through PRs 8b and 13);
R6, R7, R7b, R8, R10 → PR 6; R9 → PR 7b; R11, R12 → PR 7; R13 → PR 8; R14 → PR 3; R15 → PR 3 (`run`, `trim`),
PR 5 (`plot`), PR 6 (`mc analyze`, `mc rerun`), PR 7 (`mc size`), PR 8 (`report`); R16, R16b, R17, R18,
R18b → PR 2; R19 → PR 9; R19b → PR 2 (before and after), then PR 9 (the benchmark tracks it); R20 → every PR's
gate above (PR 10 excepted, Tier 0); R21 → PR 4.

**Held-out scope against existing items:**
- Closed-loop scenarios and Monte Carlo: an existing (M10) item, origin M5 Q16. It does not name M6's analysis
  side or `astra run`'s refusal (exit 65) of a `transport` config.
- M11's milestone row names `RealTime` and tracking the benchmark over time. The (M11) benchmark-maximum item
  and the writer-thread item exist.
- §15 envelope monitors: an existing (M7) item.
- A plotting UI or live plotting, PDF, and Sobol or adaptive sampling: no item, and not in future-work.md's
  "Later" list.

**Joe's #102 note (a), probed** (`.venv/bin/python`, `_uniform`'s rule on `t = frame * dt`, rows from frame 0,
at 50, 6,000 and 300,000 rows). Exact divisors 1 to 100: 1–8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 56, 64,
80, 96. 25, 50 and 100 are not exact. So Joe's list is right, but it gives examples, not the full set (6, 8
and others are also exact). The full set goes in the PR body; the PLAN line keeps Joe's examples and cites
the set as "in the PR body" (amendment 3).

**Housekeeping (done, D1):** PR 14 had no local worktree or branch, only `origin/worktree-m6-pr14-plotly-size`
(= `95c4a7b`, #102's head), deleted with `git push origin --delete worktree-m6-pr14-plotly-size`. The leftover
local worktree was PR 13's (`.claude/worktrees/m6-pr13-envelope-overlays`, 0 uncommitted paths, its branch
at `17175d2` = #101's head, remote already gone): `git worktree remove`, then `git branch -D`. The PR body's
housekeeping line records all three (amendment 5).

**§12's entries (amendment 2):** neither PR 9's benchmark (`tests/benchmark/`, described in §16's M6 note and
`docs/process/validation.md`) nor PR 11's `astra._yaml_route` has an entry under §12 (`grep` of
architecture.md: neither name appears in §12). So the flipped status line adds neither. PRs 8b, 12, 13 and
14 refine listed items (the Monte Carlo figures, `astra.plot`) and need no entry. Whether §12 should describe
`_yaml_route` (how `astra.montecarlo`, `astra.scenario`, `astra.trim` and `astra.lockstep` read a config) is
a question for the repo-scope docs step. Any addition to §12 is Joe's file, listed under "Needs Joe's
decision".

## Decisions (Joe, 2026-10-04)

- **D1. Housekeeping: yes, both.** Done; see "Housekeeping" above.
- **D2. docs-check's committed-whitespace hole: the PLAN.md open item**, `(unscheduled)`, as future-work's
  "not milestones" gate items are. A script fix changes gate behavior, which makes the PR Tier 1 or above with
  a full gate. It also needs a design choice: what base `git diff --check` reads when `origin/main` is absent
  or shallow (CI's `GATE=local`, a fresh clone), and whether `hooks-test` gets a case. Origin: lesson
  2026-10-04, Joe's #102 note.
- **D3. Where a fix's final Codex verification goes: two live options, Joe's choice at human review.**
  Amendment 1 corrected the first draft. Every PR's `/wrap` docs commit already moves the head past the
  reviewed candidate under the Markdown-only delta rule (PR 14's `95c4a7b` came after Codex's pass on
  `14bac8f`). So "no head moves" does not set (a) apart, and since a `/wrap` commit exists on every PR,
  "nothing may follow" is not a weakness of (b).
  - (a) **The PR body only.** The review record ends at the fix batch it describes (status `fixed`, with the
    fix), and its last line points to the PR body's Codex section. AGENTS.md already puts every review
    result there. The verdict has one home, never two copies that can drift, and `/wrap` can run before or
    after the pass. Cost: `docs/reviews/*-review.md` alone never shows the last round verified; a reader
    needs the PR.
  - (b) **The `/wrap` docs commit updates the record.** It exists on every PR, so the file is complete in the
    repository, with no extra commit and no record-only commit. Costs:
    - `/wrap` must run after the last verification, so its own STATE/README edits fall outside that pass, as
      PR 14's did (an accepted Markdown-only delta, but it fixes the order);
    - the verdict lives in two places, the file and the PR body;
    - review-dispatch's "only in the commit that carries the fix batch it describes" gains an exception.

  *Recommendation: (a), more narrowly than in the draft.* It needs no rule about whether `/wrap` runs before or
  after the pass, and the verdict keeps one home. (b) is sound, and the better pick if Joe wants
  `docs/reviews/` to be self-contained. Either choice edits `docs/agents/review-dispatch.md` and
  `docs/templates/review-findings.md` (workflow policy, Tier 2 like #88 and #90), so neither lands in this
  Tier 0 PR. PR 10 rewrites the (M6) item as an `(unscheduled)` workflow item that states both options and
  the recommendation. Joe's choice is written into it at human review. No (M6) item stays open once M6 is
  checked off, and STATE's thread points to the item.
- **D4. Held-out items: as stated.** Three new `(unscheduled)` items: a plotting UI or live plotting; PDF
  output; Sobol and adaptive sampling (one item, as the plan groups them: both need designed sample sets).
  The existing (M10) closed-loop item is extended with the M6 plan's held-out scope: analysis, report and
  rerun over closed-loop batches, and `astra run`'s `transport` refusal. No new item for M11 (`RealTime`,
  benchmark tracking) or M7 (envelope monitors), since their milestone row or items already hold them; the
  PR body cites each.
- **D5. Note (a)'s wording: yes.** Joe's examples with "such as", plus the probe's full set in the PR body.

## Changes

### 1. PLAN.md, M6 block
- `- [x] **M6 Analysis tooling**`, keeping the row text, then "Evidence, clause by clause:" in M5's form. Each
  clause links its merged head's `docs/reviews/evidence/<head>-gate-summary.log` as `[summary](…)`, cites
  acceptance counts "in the PR body", and gives the R-IDs it rests on:
  - plot packages over run logs: PR 5 (`8126aff`; R1–R4, R3b); refined by PR 12 (`02af820`) and PR 14 (`5a08393`);
  - Monte Carlo visualization: PR 6 (`3a09f45`; R5); its envelope's size, PR 8b (`efbac92`); its overlays, PR 13
    (`74b6505`);
  - failure-to-draw correlation: PR 6 (R6–R8, R7b, R10) and PR 7b (`f184b4c`; R9 with its negative control, the
    timing log linked);
  - sizing from the question and the parameter count: PR 7 (`80f239e`; R11, R12);
  - the run report with provenance: PR 8 (`02152f2`; R13);
  - the frame-time benchmark: PR 9 (`100f491`; R19, R19b);
  - `astra run --strict`: PR 3 (`e1a8dec`; R14, R15's `run`/`trim`);
  - the superseding ADR (Tier 3): PR 1 (`574f9fe`; ADR-0027; Joe's two docs-only commits after the gate,
    waived, in the PR body) and PR 2 (`ede0cf3`; R16–R18b, R19b);
  - beyond the row: PR 4 (`2de663a`; R21) and PR 11 (`53db70f`; #99 merged after the gate, in the PR body);
  - R15's other cases and R20 (every PR's gate; PR 10 Tier 0) in one closing sentence; "Spec and plan:
    [plan](docs/plans/m6-analysis-tooling.md)."
- PR 14's line: "lossless and always on" gains ", though not always `x0`/`dx`: at dt = 1 ms a rate group's
  t keeps an explicit array at divisors such as 25, 50 and 100, while divisors such as 1 to 5, 7, 10, 20 and 40
  rebuild exactly (the full set in the PR body)" (note a, D5, amendment 3).
- New line after PR 9's: `PR 10 (#NN): M6 closeout (check-off with evidence links, held-out scope items, the
  docs-check and review-record items, PR 14's divisor clause, architecture §12 and §16 status). Plans:
  [milestone](…), [PR](docs/plans/m6-pr10-closeout.md). Packet: in the PR body.` `#NN` is filled in once the
  draft exists. No merge-only fact goes in any line.

### 2. PLAN.md open items (in the existing `(origin: …; …)` … `Not started.` form)
- New, D4: plotting UI or live plotting; PDF output; Sobol and adaptive sampling (each with origin: M6 plan,
  Non-goals and "Held out"; Joe's call).
- Extended, D4: the (M10) closed-loop item.
- New, D2: `(unscheduled)` docs-check reads only the uncommitted diff (origin: lesson 2026-10-04, M6 PR 14
  review; Joe at #102).
- Rewritten, D3: the (M6) review-record item becomes `(unscheduled)`, with options (a) and (b) and the
  recommendation; Joe's choice is added at human review.

### 3. docs/architecture.md (Joe's file; each edit listed under "Needs Joe's decision")
- §12 status line (l. 419): "M6's additions below … are each marked with their M6 PR … are built" becomes
  "M6's additions below are built, each marked with its PLAN.md M6 PR (`docs/plans/m6-analysis-tooling.md`)",
  keeping the PR list as written. The list gains no PR 9 or PR 11 entry, since §12 carries neither (amendment
  2, above). No revision bump (M5 precedent).
- §16 M6 note (l. 598): "partly built: PRs 1 to 9 and 7b are built, and PR 10's closeout is *planned*" becomes
  "built by PLAN.md M6 PRs 1 to 9, 7b, 8b and 11 to 14". The M6 row's text needs no change.

### 4. /wrap, repo-scope docs step, plus STATE.md
- Run the docs step in repo scope, per `.claude/skills/wrap/SKILL.md`. Already found: README's roadmap ("M0 to
  M5 are done … Next: M6") moves M6 to done. No stale code comment was found (the `git grep` for
  planned/M6 PR/not yet came back clean). Any stale comment in a code file is reported to Joe, not edited.
- STATE.md: Next step: M7 planning (`/plan-feature`), Joe's to launch. Open threads: the review-record thread
  points to D3's item; the others stay. ≤ 40 lines, written before the Codex pass.

## Sequence

1. Done: D1's housekeeping; `EnterWorktree` `m6-pr10-closeout` on `origin/main` `3a50f6f`; this plan moved
   here; then the main-checkout draft deleted, and `pr10-docs-step-notes.md` with its MEMORY.md line.
2. Done: `git merge-base --is-ancestor` for all 15 PRs, exit 0 each.
3. Edits 1 to 3; `just docs-check`; `git diff --check origin/main..HEAD`; every number re-read against its
   linked summary. Commit, push, `gh pr create --draft` (template, Tier 0: scope and checks, no full gate).
4. Fill in `#NN`; `/wrap` (repo-scope docs step, STATE.md); `just docs-check`; `git diff --check
   origin/main..HEAD`; commit; push. `/wrap` runs before step 5, so the Codex pass covers STATE.md, README and
   any other docs-step fix (amendment 6).
5. `/review-cycle`: one fresh Codex pass on `origin/main..HEAD`. A factual error gets one correction round;
   anything else becomes a follow-up in the PR body. After a correction round, rerun `just docs-check` and
   `git diff --check origin/main..HEAD`, and record both against the new head.
6. `/human-review`: the PR body with the checks, Codex's verdict and thread id, the housekeeping line (the
   remote branch deletion and the worktree and branch removal), note (a)'s full divisor set, "Needs Joe's
   decision" (D3's choice and the architecture edits), and follow-ups.

## Verification
- `just docs-check` exits 0 (`scripts/docs_check.py`, so the new evidence links resolve; `bash -n` on both
  check scripts; `git diff --check` on the working tree), recorded as such in the PR body.
- `git diff --check origin/main..HEAD` is clean, recorded in the PR body.
- `git diff --stat origin/main` lists only PLAN.md, docs/architecture.md, README.md, STATE.md and
  docs/plans/m6-pr10-closeout.md, plus any docs-step fix the PR body names.
- Every number and SHA in the evidence lines is re-read from its linked summary file before the commit.
