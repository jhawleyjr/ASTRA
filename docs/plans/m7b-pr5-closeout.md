# M7b PR 5 (Tier 0): the M7b closeout

Status: **approved by Joe with amendments 1 to 9, 2026-10-21**, in plan mode (1 to 7 with D1 to D3; 8 and 9,
wording, at his second approval). D1 yes (amendment 2), D2 yes (amendment 3), D3 yes; the reading of "the three
test plugins' pattern" is confirmed. Moved here from the main checkout's plan-mode draft (`mv`, before its first
add); the main checkout's `docs/plans/` then matched `origin/main`'s tracked list.
Model: [M7 PR 16](m7-pr16-closeout.md). Brief: Joe, 2026-10-21 (items 1 to 3). Sources: [the M7b
plan](m7b-closed-loop-oracles.md) (row 5, "Carried items", "Done when", "Validation, risks and rollback", (b));
[PR 4's plan](m7b-pr4-f16-symmetry.md) §15, §19, §20; ADR-0029 Decision 8; ADR-0034 Decision 8 and
Consequences; architecture §4, §10, §16, §18; #122 to #125's bodies; lessons 2026-10-18 and 2026-10-20.
Name: on step 1 this draft is moved (`mv`, before its first add) from the main checkout's `docs/plans/` to the
worktree's `docs/plans/m7b-pr5-closeout.md`; the main checkout's `git status` is then confirmed clean and the PR
body says so (amendment 7).
Tier 0: Markdown only; no execution, build, test or policy behavior changes, so no full gate. Acceptance: `just
docs-check`, `git diff --check origin/main..HEAD`, one fresh Codex pass on the final diff; a finding that is not
a factual error becomes a follow-up, not a correction round.

## Joe's amendments (2026-10-21), folded in below

1. The row's words changed in **two** dated deviations: PR 2's C grid, and PR 4's R12 decomposition with the R13
   floor. PR 3's stop-hook first run (R5) is a dated *process* deviation, kept in R5's evidence note only. The
   §16 note's three phrases and the PLAN.md M7b line enumerate the same items.
2. §18 step 5's sentence: the accepted artifact is the reference; a new law follows ADR-0029 Decision 8's order
   (sim-side peer first, the port proven bitwise against it); PR 3 proved a transcribed peer against the shipped
   program, not the reverse.
3. Each tree entry this PR touches shows every top-level directory; the rest of the tree stays schematic and the
   PR body says so. A half-fixed entry is a new incomplete statement.
4. Joe's gate-timing item stays verbatim. Its +94 s is the untimed share of PR 4's +107 s against PR 3's gate on
   `a459cd9` (1187 s); the R16 clause and future-work's line name that base.
5. The verification's file list is a floor; the gate is the exclusion check plus both checks.
6. The (M9) Earth-rate sentence is a repo-scope fix under Docs impact; the two stale remote branches go under
   "Needs Joe's decision", not STATE.md.
7. The draft's move and a clean main-checkout `git status`, stated in the PR body.

Joe's second approval (2026-10-21), two wording amendments:
8. ADR-0034 Decision 8 says only that the criterion governs a peer and its port "in either order of writing"; it
   does not say which side is the reference. §18's sentence and the §16 note cite it for the order being free,
   and cite the M7b plan's "Validation, risks and rollback", (b), for `embedded/autopilot` being M4's accepted
   artifact and so the reference.
9. §4's `f16/` entry names all eight directories, each with a one-clause gloss in the tree's style.

## Verified facts (plan mode, read-only)

- `git ls-remote origin main` = `469e01f` = local `main` (#125's squash); tree clean; `git fetch` runs at step
  1. `git worktree list` shows only the main checkout, `git worktree prune --dry-run` prints nothing, and `git
  branch --list 'worktree-m7b-pr4*'` is empty. STATE.md's step 2 names PR 5. On GitHub,
  `origin/worktree-m7b-pr3-autopilot-peer` and `origin/worktree-m7b-pr4-f16-symmetry` remain (amendment 6).
- Each gated candidate is in its head (`git rev-list --count head..cand` = 0), each head's tree equals its
  squash's, and each post-gate delta is Markdown and evidence only. Every summary reads exit 0, `check: OK`;
  pytest counts carry 5 skipped.

  | PR | # | squash | head | gated | CTest (d/r/s/a) | pytest | gate |
  |---|---|---|---|---|---|---|---|
  | 0 | 121 | 7a92115 | abcb1d5 | — (Tier 0) | | | |
  | 1 | 122 | 406c407 | 8d20c46 | 895cebe | 800–803 | 2110 | about 20 min |
  | 2 | 123 | 106c0cb | f9e6fbd | 0bbc4fa | 806–809 | 2135 | 1178 s |
  | 3 | 124 | fe922b0 | d5ad977 | a459cd9 | 824–827 | 2160 | 1187 s |
  | 4 | 125 | 469e01f | b59bbc4 | def7d91 | 838/849/840/841 | 2160 | 1294 s |

  Base gate on `406c407`: 1162 s. PR 4's +107 s against `a459cd9` = CTest −3.21 s + pytest +15.97 s + 94 s in
  untimed steps (`def7d91-gate-summary.log`, #125's "Gate time").
- PR 2 to 4 left `docs/architecture.md` untouched (`git show --stat` of each squash). #124's Docs impact leaves
  §4 and §10 to PR 5; PR 3's plan §13 and PR 4's §15 leave §18 to PR 5.
- Top-level directories: `_template` {configs, hardware, physical, plots, tests}; `notional_transport`
  {configs, hardware, physical, plots, sim} (no `tests/`); `f16` {common, configs, hardware, physical, plots,
  sim, tests, tools}; `tests/` {autopilot_peer, benchmark, capture, configs, embedded, golden, icd, lockstep,
  physics, plugins, python, scenarios, support, unit}. §4 shows none of `plots/`, `sim/` (notional), `f16`, and
  6 of `tests/`' 14.
- Schematic remainder, not edited (amendment 3): the root omits `scripts/`, `cmake/`, `.github/`; `docs/` shows
  3 of its 13 entries; `python/astra/` lists 8 modules, not `plot`, `report`, `mc_analysis`, `mc_sizing`,
  `_yaml_route`, `_runkit` or `plots/`.
- Carried comments at `469e01f`: `aircraft/f16/tests/aero_test.cpp:756`, `independent_evaluator.hpp:9-11`, the
  D8 comments at `engine.cpp:12-13` and `environment_sample.hpp:96-97`. PR 4 fixed `aero_math.hpp:86`.

## Does every requirement hold? Yes

| R | PR | Evidence | Note |
|---|---|---|---|
| R1–R4 | 2 | `0bbc4fa` summary; `1d7fb2c-sampled-data-first-run.log` | **Deviation 1 (R2):** C's reference on a 2⁻⁴⁰⁰ grid, the grid's effect in C's bound (PR 2 §11 item 3); F and L exact. Order control ≥ 10^9.31 against 10⁸ |
| R5–R9 | 3 | `a459cd9` summary; `6da7622-fp-flags.log`; `eab4adf-autopilot-peer-first-run.log` | R5's stop-hook first run is a dated process deviation (PR 3 §16); its run of record is step 7b. It does not change the row's words. R9's exit 65: `test_autopilot_peer_configs_exit_65_under_astra_run` |
| R10–R15 | 4 | `def7d91` summary; first and second flight logs; `m7b-pr4-{trim,inventory,r15}/` | **Deviation 2 (R12, R13):** R12 is the evaluation-level decomposition by value (PR 4 §8); the doubled control fails part (c) at 169,566 and 33,778,928 ulps, as registered. R13 passed at the second flight with the yardstick floored per channel (§9, §19, §20). `aircraft/f16/tests` unchanged |
| R16 | 1–4 | each summary; each body's deltas and Codex threads | Against 1001 s: +177, +186, +293 s. PR 2's per-preset times are in its summary beside the base's |

So M7b is checked. The PLAN.md line and the §16 note name the same two deviations and three phrases.

## Changes

### 1. PLAN.md
- `- [x] **M7b Closed-loop oracles**`, text unchanged, then "Evidence, by requirement" in the M7 line's form:
  oracle (a) PR 2 (R1–R4), oracle (b) PR 3 (R5–R9, with R5's process-deviation note), oracle (c) PR 4
  (R10–R15), ADR-0034 PR 1; each with its summary and first-run or flight logs and counts. Then: "The row's words
  changed in two dated deviations by Joe: PR 2's C reference on a 2⁻⁴⁰⁰ grid (R2), and PR 4's R12
  evaluation-level decomposition by value with R13's yardstick floored per channel." Then R16: the summaries,
  the gates against 1001 s, and PR 4's +107 s against PR 3's `a459cd9` (CTest −3.2 s, pytest +16.0 s, +94 s
  untimed). No merge-only fact.
- `PR 5 (#NN): M7b closeout …`. Plans: [milestone], [PR]. Packet: in the PR body.
- The three unscheduled M7 items keep their text and gain one sentence each:
  - PR 11b: if scheduled, its law is a sim-side in-process peer like `nesc_control` (Decision 8's first side),
    and oracle (c)'s mirror pairs (`tests/physics/nesc/f16_symmetry.hpp`) model a check of a lateral path the
    bands cannot discriminate;
  - PR 12: if scheduled, it is Decision 8's own order, `nesc_control` the reference and the port the newer side,
    under the criterion M7b PR 3 exercised in the reverse order: bitwise on Decision 8's objects, both builds'
    flags stated before the first comparison, ulp-sized porting controls with pre-registered first breaks
    (ADR-0034; `tests/python/test_autopilot_peer.py`);
  - PR 15: §18 step 5 now states Decision 8's order and PR 3's reverse-order example.
- Joe's item, verbatim: `- [ ] (unscheduled) check-full.sh prints a wall time per step (each preset's build,
  tidy, pyright, the python build, each CTest run, pytest, the scans), so a gate delta can be attributed; PR 4's
  +94 s fell in untimed steps.` followed by `(origin: M7b PR 4's gate, #125; Joe 2026-10-21). Not started.`

### 2. docs/architecture.md (Joe's; each edit verbatim under "Needs Joe's decision")
- Status line: "rev 17, 2026-10-21: §16's M7b row and note, §4's tree, §10's in-process transports and §18's
  step 5, for the M7b closeout (PLAN.md M7b PR 5); no ADR changes."
- §4 (amendment 3): `_template/` gains `plots/` and `tests/`; `notional_transport/` gains `sim/transport/autopilot/`,
  `plots/` and the two `_autopilot_peer` configs; `f16/` is added with all eight of its directories, each glossed
  (amendment 9): `common/` (the shared table interpolation), `configs/` (NESC cases 11, 12 and 13.1 to 13.4),
  `hardware/propulsion/engine/`, `physical/{aero, mass}/`, `plots/` (its plot packages),
  `sim/transport/nesc_control/` (the `F16_control` law as an in-process peer), `tests/` (check data and peer
  tests) and `tools/` (the aero tables' generator); `tests/` lists all 14, `plugins/` as test-only plugins discovered like `plugins/`, never shipped. No other entry
  changes; the PR body names the schematic remainder above.
- §10: an **`autopilot`** bullet (M7b PR 3; *ADR-0029*, *ADR-0034*): M4's law transcribed from
  `embedded/autopilot` as an in-process C++23 peer and proven bitwise equal to it over M4's scenario, the accepted
  program as the reference; `astra run` refuses its configs, as any `transport` block. A **test peers** bullet:
  `state_feedback_peer`, `nesc_control_variant`, `autopilot_peer_variant` under `tests/plugins/`, defects and hooks
  selected in test code only. One clause on `nesc_control`: PR 4's header-only hooks are selected only by test
  variants, and the default is bit-identical (R15).
- §16 row: "*Planned* (PLAN.md M7b)." removed; nothing else changes. A new note, verbatim:

  > M7b note (rev 17): the M7b row is built, by PLAN.md M7b PRs 1 to 4, and closed out by PR 5.
  > `docs/plans/m7b-closed-loop-oracles.md` R1 to R16 check it, PR by PR: R1 to R4 by PR 2, R5 to R9 by PR 3,
  > R10 to R15 by PR 4, and R16 by each PR's gate and review. Two of Joe's dated deviations (*ADR-0034* Decision
  > 3) set what three of the row's phrases mean. PR 2's: oracle (a)'s "to stated roundoff" holds C against the
  > exact recursion carried on a 2⁻⁴⁰⁰ grid, the grid's effect in C's bound, while F and L stay exact
  > (`docs/plans/m7b-pr2-sampled-data.md` §11). PR 4's: "within a pre-registered modeling bound" is the
  > evaluation-level decomposition, by value: at every RK4 stage operand and exchange of the shipped pairs, the
  > shipped evaluations differ from the symmetric ones only by the four signed-β tables and Ω, and the trajectory
  > asymmetry is reported, not bounded (`docs/plans/m7b-pr4-f16-symmetry.md` §8); and "within a pre-registered
  > measured sensitivity yardstick" floors the yardstick per channel at one ulp of the base run's largest value,
  > its factor and perturbation set unchanged (same plan, §9, §19 and §20). Oracle (b) applies *ADR-0029*
  > Decision 8's criterion in the reverse order of writing, which *ADR-0034* Decision 8 allows: the peer was
  > transcribed from `embedded/autopilot` and proven bitwise against it, because that program is M4's accepted
  > artifact and so the reference (`docs/plans/m7b-closed-loop-oracles.md`, "Validation, risks and rollback", (b)).
- §18 step 5 (amendments 2 and 8), added sentences: "*ADR-0029* Decision 8's criterion holds in either order of
  writing (*ADR-0034* Decision 8). For a new law, follow Decision 8's order: write it sim-side as an in-process
  peer, as `nesc_control` is, prove it, then port it to `embedded/` and prove the port bitwise against the peer's
  run. Where one side is already accepted, it is the reference. M7b PR 3 ran the reverse order:
  `aircraft/notional_transport/sim/transport/autopilot` was transcribed from `embedded/autopilot`, M4's accepted
  artifact (`docs/plans/m7b-closed-loop-oracles.md`, "Validation, risks and rollback", (b)), and proven bitwise
  against it, not the other way around."

### 3. docs/plans/m7b-closed-loop-oracles.md (D3)
Under Status, "*Closed out 2026-10-21 (PLAN.md M7b PR 5).*"; under "Done when", the two deviations by R (R2;
R12 and R13), worded as in PLAN.md. No other line changes.

### 4. `/wrap`'s repo-scope docs step
Every doc in `/wrap`'s repo list, read against the tree, plus milestone-pointing code comments. Known so far:
- README: roadmap "M0 to M7b are done", next M8; the Verification feature names the three in-house oracles
  (ADR-0034); the notional transport feature names the peer configs.
- `docs/future-work.md`: the gate-time line adds 1187 s on `a459cd9` (PR 3) and 1294 s on `def7d91` (PR 4),
  +107 s against `a459cd9`, of which +94 s fell in untimed steps (PLAN.md's per-step timing item); the
  milestone map gains an M7b entry after M7.
- PLAN.md's (M9) Earth-rate item gains: "M7b PR 4's `Options::earth_rate` (`kNone`) is selected only by a test
  variant; the shipped peer still subtracts Ω." Recorded under Docs impact as a repo-scope fix (amendment 6).
- Stale code comments are not edited; each stays or becomes an open thread.

### 5. STATE.md (by `/wrap`, ≤ 40 lines, as main reads it after the merge)
- Next: M0 to M7b done; M8 planning (M7's PR 13 envelope item) and M7's PR 3 (M9 rotation), each on Joe's go,
  order his; the key-block rule.
- Open threads kept: `aero_test.cpp:756`, the D8 comments, the evaluator's wrap artifact, PR 11b's `rate_frame.py`
  obligation, the rate-frame item, trim, lessons, PLAN's items list with the gate-timing item. No branch item.

## Sequence
1. `git fetch origin`; `EnterWorktree` `m7b-pr5-closeout` from `origin/main` (`469e01f` or later); `mv` this draft
   to `docs/plans/m7b-pr5-closeout.md`; confirm the main checkout's `git status --short` is empty.
2. Edits 1 to 3. Re-read every SHA, count and time against its summary. `just docs-check`; `git diff --check
   origin/main..HEAD`. Commit, push, `gh pr create --draft`.
3. Fill `#NN`; step 4's docs step; STATE.md; rerun both checks; commit and push.
4. `/review-cycle`: one fresh Codex pass on `origin/main..HEAD`. A factual error gets one correction round with
   both checks rerun; anything else becomes a follow-up.
5. `git fetch`; if `origin/main` moved, merge it in, rerun both checks, and rerun the Codex pass on the combined
   diff if the merge touches a file this PR edits.
6. `/human-review`: checks, Codex verdict and thread, Docs impact (the (M9) sentence; the schematic remainder),
   "Needs Joe's decision" (the architecture edits verbatim; the two remote branches), the draft's move and the
   clean main checkout. Then `/wrap`'s handoff, and stop. The merge is Joe's.

## Verification (the gate, amendment 5)
- `git diff --name-only origin/main..HEAD -- core plugins aircraft tests embedded icd python scripts cmake
  docs/decisions .claude` is empty.
- `just docs-check` exits 0; `git diff --check origin/main..HEAD` is clean.
- Expected at least: PLAN.md, STATE.md, README.md, docs/architecture.md, docs/future-work.md,
  docs/plans/m7b-closed-loop-oracles.md, docs/plans/m7b-pr5-closeout.md. Repo scope may add others (docs/README.md,
  docs/agents/, docs/process/); a list, not a gate.
- The PR body records the scope and the checks run, never a full-gate claim.
