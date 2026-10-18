# M7b PR 1: ADR-0034, the in-house oracle rule, and architecture rev 16

Spec: [M7b plan](m7b-closed-loop-oracles.md), approved by Joe on 2026-10-15: the PR table's row 1, Q1 as
decided, amendment 1, Q12, and the acceptance table (R1 to R16), which the record must be able to govern.
Joe's brief of 2026-10-17 (items 1 to 5), his three answers in plan mode, and his seven approval amendments
(folded in below) set this PR's scope.
Risk: Tier 3. The record sets the oracle rule for every in-house oracle from M7b on, under AGENTS.md's
human-owned boundary. Docs only: no code, test, config, golden or tolerance change, and no edit to an accepted
ADR.
Status: **approved by Joe, 2026-10-17, on his word**, with amendments 1 to 7; Readings B to H confirmed. After
approval he added one item: Context's sentence that no existing check carries all three elements.
Base: `origin/main` = `7a92115` (#121), read with `git ls-remote` in plan mode; `git fetch` confirms it
after approval. Worktree `m7b-pr1-oracle-adr`, branch `worktree-m7b-pr1-oracle-adr`. No PR is open, and no
`docs/decisions/0034-*` exists.

## Context

M7b's three oracles have no external ensemble. Their references are built here: an exact rational solution
(a), a second implementation (b), and the model's own mirror and rotated images (c). The tree's rules do not
cover that:
- ADR-0013 governs an external ensemble band. Its reference comes from the members and its tolerance from
  their disagreement, "never fitted to ASTRA's own output", with k tightened, never widened. ADR-0028 applies
  it to the F-16's two members.
- ADR-0029 Decision 8 sets the bitwise port criterion, for the NESC laws only.
- validation.md's numerical section says only "never derive a tolerance from ASTRA's own output", and step 6
  says to escalate "when ... a new case has no independent reference".

None of them covers an in-house reference, a bound fixed before the first run, or required negative controls.
The tree has prior art, cited in the record's Context for what each piece shows (paths read at `7a92115`):
- **M7 PR 9's residual check**, `NescF16.Case1{1,2}TrimResiduals` in
  `tests/physics/nesc/nesc_f16_open_loop_test.cpp`, shows two elements.
  - Its reference is independent. It was derived from the DML alone by
    `docs/reviews/evidence/m7-pr9-residual/derive_residual.py`, which "imports nothing from astra, links no
    plugin and reads no generated table", and which reads the config's scalars as text.
  - Its bounds were pre-registered in the plan (1e-12 times |g|, or times the largest part), and a miss goes
    to Joe.
  - It has no negative controls of its own. PR 9's controls (the elevator's sign, and SIM 2's trim, in
    `tests/physics/nesc/nesc_f16_negative_controls_test.cpp`) belong to the ADR-0028 band check. *(The draft
    of this brief said otherwise; it is corrected to the files.)*
- **M7 PR 7's F16Evaluator**, `tests/physics/nesc/f16_evaluator_test.cpp`, also shows two.
  - Its 1e-12 normwise bound was pre-registered, and "a miss goes to Joe; it never becomes a wider number".
  - Its controls each fail: `AnUnsetEarthRateOrAltitudeFailsTheCheck` (NaN and zero) and
    `StaleControlsFailTheCheck`.
  - Its comparison runs the same plugin code on both sides by design (the expected miss is exactly 0), so the
    record cites it for those two elements, not for independence.
- **M7 PR 11a's R11 bound** shows the cost when the rule is not fixed in advance. It missed on its first run
  and changed twice, each change a dated deviation of Joe's in that plan's Q2 (lesson 2026-10-13).

So the corrected prior art shows that no existing check carries all three elements together (an independent
reference, a pre-registered bound and negative controls that fail), which is the gap the record closes (Joe,
after approval). ADR-0034 makes this discipline the rule before PR 2's first run. The M7b plan's PR table
makes ADR-0034's acceptance PR 2's dependency.

## Where the candidate lives (Joe's answer 1)

- **The candidate** is `docs/plans/m7b-pr1-adr-0034-candidate.md`, with Status: proposed. There is only ever
  one copy. Codex reviews it there, and any fix round edits it there, with the guard unarmed.
- **Joe reads the review-fixed candidate first.** Only then is he asked to accept it and arm the guard.
- **Acceptance is one armed turn and one commit:**
  - `git mv` the candidate to `docs/decisions/0034-in-house-oracle-rule.md`;
  - the Status line;
  - the Approval section;
  - architecture rev 16's status line, which gains the accepted date.
  Nothing else in the record changes at the move, so Codex's review of the candidate carries over. The PR
  body says so and names the reviewed head.
- **Why this departs from PR 1a and 1b.** Those PRs wrote their proposed records into `docs/decisions/` in
  armed sessions (`80eabf8`, `5f246a3`). This PR departs from that on purpose, for two reasons, and the PR
  body states them:
  - `docs/templates/adr.md` says to keep a draft with the feature plan.
  - Lesson 2026-10-09 says to arm right before the guarded writes, in a turn that finishes them. This route
    needs one armed turn. The 1a/1b route needs one for each review fix.
- **The §16 citation** (amendment 1). `scripts/docs_check.py` validates relative links only in its workflow
  roots and in PLAN.md, and it deliberately excludes `docs/architecture.md`. No row of architecture.md links
  an ADR. So §16 cites *ADR-0034* as every other row cites a record: in italics, unlinked, in the first
  sentence after "negative controls that fail". Nothing resolves at acceptance. PLAN.md and README.md carry
  no ADR-0034 link either, and docs-check passes at every commit.

## The record: ADR-0034 (outline)

Title: "ADR-0034: In-house oracles: an independent reference, a bound fixed before the first run, and
negative controls". The house style follows ADR-0028, ADR-0029 and ADR-0032: short bullets, backticked
paths, no Markdown links, about two screens.

**Header** (answer 1, item 5):
- Status: proposed. Date: 2026-10-17. Owner: Joe.
- Related/supersedes: "supersedes nothing; extends ADR-0029 Decision 8; elaborates ADR-0013 and
  validation.md's numerical section".
- Drafted with the M7b plan (Q1, amendment 1, Q12) and this brief. It governs PLAN.md's M7b PR 2 to 4 and
  every in-house oracle after them.

**Context.** The gap and the prior art above.

**Decision.**
1. **Scope** (answer 2). The line is drawn by where the reference comes from, not by the type of test.
   - **Definition.** An in-house oracle is a gated check that a §16 row or a plan's acceptance table cites as
     evidence that a model, law, path or port matches a reference, and whose reference is computed or built
     in this repository rather than placed under `docs/refs`. ADR-0013 and ADR-0028 keep the external
     ensemble, and ADR-0034 takes the in-repo reference.
   - **No dodge.** A check that a plan's acceptance depends on counts as cited, and a PR may not take a check
     out of the rule by leaving it uncited. This is the counterpart of validation.md's step 6: a new case
     with no independent reference either becomes an in-house oracle under this record or goes to Joe. A
     tolerance is never derived from ASTRA's own output.
   - **The border with unit tests.** An expected value that a reviewer can verify by reading it (a closed
     form, a textbook value, ADR-0001's sign pins, a checkData shot) makes a unit test, under validation.md.
     An expectation that needs a run of the path under test, a derived bound, or a comparison set makes an
     oracle, under this record.
   - **Frozen regressions** (M3 PR 8's 300 s trajectory, the R1 golden) are not oracles under this record,
     as PLAN.md's M3 row already says. They stay under the goldens boundary.
   - **Not retroactive.** No existing test gains an obligation. PR 7's and PR 9's checks are prior art, not
     governed by Decision 1.
2. **The reference shares no code with the path under test.** No header, library, function or generated
   table on the compared path is common to both. Two clarifications follow.
   - *Shared inputs are named, not hidden* (Reading B). These are the config text, ICD v1's structs, and the
     platform's compiler and libm. The PR's plan states what each one cannot see, for example a libm defect
     common to both sides of (b).
   - *A relation between two runs* (Reading C). In the mirror and the heading rotation, the code under test
     runs on both sides by design. The reference is the relation itself, which the test writes from its
     derivation (the parity mask, the rotation back by −Δψ, the wrap) and never computes by calling the code
     under test. The plan states the relation's blind spot: a defect that is itself sign-symmetric passes a
     mirror.
3. **The bound is derived and fixed in the PR's plan before the first run.**
   - "The first run" means the first run that produces the compared output of the path under test. Three
     things may be measured before it (Reading D, with amendment 3):
     - the model's own data and constants (the β tables, Ω);
     - the platform's libm (R10's parity probe runs before any flight);
     - the reference alone.
   - After it, the bound, its derivation and the comparison set never move without Joe. A miss stops the PR
     and goes to him. A change he makes is a dated deviation beside the original text in the plan, never
     absorbed into it, as PR 11a's Q2 recorded its two (Reading E).
4. **The four allowed bound kinds.** The plan names each bound's kind.
   - **Bitwise**, or by value (`==`, so +0.0 equals −0.0), with the plan saying which.
   - **Roundoff, by derivation**: a running bound that charges each rounding at eps·|its own result|,
     through the operation order read from the code with the lines cited. It has no fitted constant (lesson
     2026-10-13).
   - **A modeling bound**, from a stated, measured asymmetry of the model. It is carried through a stated
     sensitivity estimate, whose envelope and assumptions are stated, and a pre-registered planted defect must
     exceed it by a stated margin (amendment 1, C-05).
   - **A measured sensitivity yardstick**, only where a derivation cannot reach. Its factor and its
     perturbation set are fixed before any run, from a stated estimate. A planted defect must fail it by
     orders of magnitude (Q12). Its perturbed runs are part of the oracle. This exact term is the one §16's
     row and R13 use.
   No other kind is allowed. A case that none of the four can bound goes to Joe, never to an ad hoc bound.
5. **Negative controls.** Each oracle carries them. Each control is pre-registered in the plan to fail on a
   named object: a stream, field, frame or exchange, with a margin where the plan states one.
   - A control that does not fail as registered stops the PR and goes to Joe (Reading F, amendment 2's rule
     for (b) made general). That covers a control that passes, and one that first fails on another object,
     frame or exchange.
   - Controls are planted only in test code: a test plugin, a variant that holds the shipped code through its
     header, or a `test_defect` key. They are never planted in `core/` or in a shipped default path (Reading
     G).
6. **Where the constants live.** In the tests, each beside its derivation or a citation of the plan's
   section, and not under `tests/golden/`. They fall under the review rule: Codex and Joe check the plan's
   value against the test's.
7. **Oracle data.** Bounds and comparison sets are oracle data under AGENTS.md's boundary. Narrowing a
   comparison (dropping a field, frame, stream or run) counts the same as widening a tolerance.
8. **Decision 8, extended.** ADR-0029 Decision 8's criterion governs any sim-side peer and its port, in either
   order of writing, since (b) writes the peer second (Reading H). It brings four requirements:
   - bitwise equality on Decision 8's objects, never fewer;
   - both builds' floating-point flags, stated before the first comparison;
   - a difference counts as a porting defect until shown otherwise;
   - the expression-order rule in ADR-0029's Consequences.

**Rejected alternatives.**
- **A paragraph in validation.md's numerical section instead of a record** (Q1's alternative). The rule sets
  what a bound may be and who may move it, which is Joe's call, and it extends an accepted record's criterion.
  Both need an immutable record that later PRs cite, not a procedure file that a workflow PR may edit.
- **The constants under `tests/golden/`.** That tree holds the outputs of generators that Joe runs over
  external data (ADR-0013). These constants are derivations, and a reviewer reads each one beside the test it
  bounds. A guarded file would also split a constant from its derivation, and the plan's pre-registration,
  which Joe approves, is already the human checkpoint. The trade-off is disclosed in Consequences: review
  guards these constants, not the hook.
- **A fixed relative bound as a fifth kind** (amendment 2; source: the M7b plan's Q12), for example 1e-9 of
  a channel's range, stated as an estimate. Joe did not choose it, because it states a number without deriving
  it. This is why there are four kinds and not five.

**Consequences and validation.**
- **Before its first run,** each of PR 2 to 4's plans pre-registers:
  - the reference, with its independence argument;
  - its shared inputs and their blind spots;
  - each bound, with its kind and derivation;
  - the comparison set;
  - each control's named object.
- **The disclosed costs:**
  - the hook does not guard these constants, so a moved bound is caught by review;
  - a miss stops the PR;
  - shared inputs hide common-mode defects.
- **Acceptance:** Joe accepts the record by name, and it then gates PR 2. Its **Approval** section says that
  the record remains proposed until Joe gives his decision, as the template does.

Readings B to H go beyond the letter of Joe's list. Each is traced to the M7b plan or to a lesson, and Joe
confirmed all of them at approval.

## Architecture rev 16, and the other edits

- **The status line** gains one clause (amendment 5): "rev 16, 2026-10-17: §16's M7b row cites ADR-0034
  (proposed; supersedes nothing) and names heading invariance's bound as Q12's measured sensitivity yardstick
  (R13; the M7b plan's amendment 11)". At acceptance, "proposed" becomes "**accepted by Joe** <date>". The
  amendment-10 history goes in amendment 11's line and in the PR body, not in the status line.
- **§16's M7b row** gets two edits, each listed separately in the PR body's Docs impact section with its reason.
  The first sentence gains *ADR-0034*, in italics and unlinked, after "negative controls that fail".
  - *The "and" tidy* (Joe's brief, item 2).
  - *The wording correction* (answer 3). The last sentence becomes:
    > The F-16 under `F16_control` flies 13.3's course change and 13.4's side step on a non-rotating Earth as
    > mirrored pairs, equal under the mirror where every operation is sign-symmetric, with the shipped F-16
    > models within a pre-registered modeling bound; 13.3's on a flat Earth is heading-invariant within a
    > pre-registered measured sensitivity yardstick.
  Oracle (a)'s "to stated roundoff", earlier in the row, stays: its F, L and C bounds are derived roundoff,
  the record's second kind.
- **The M7b plan, with two edits and no others:**
  - In "Plan critique", delete "The verification pass is next." The bullet keeps its second sentence, the
    link to the critique record.
  - Under "Joe's decisions", after the paragraph that follows amendment 10, add one line: "11. **2026-10-17
    (Joe):** §16's M7b row names heading invariance's bound as the measured sensitivity yardstick of Q12 and
    R13, where rev 15, under amendment 10, said "stated roundoff"; Q2's quote stays as the rev 15 text it
    records."
- **README.md's Roadmap.** A new first bullet under "Next, in order:" puts M7b before M8: "**M7b Closed-loop
  oracles:** an exact sampled-data check, a sim-side peer bitwise equal to M4's embedded autopilot, and F-16
  symmetry tests, each with an independent reference and a pre-registered bound." This closes STATE.md's open thread.
- **PLAN.md:** PR 1's line under M7b.

**Files touched:**
- this brief, as `docs/plans/m7b-pr1-oracle-adr.md`;
- the candidate;
- `docs/architecture.md`;
- `docs/plans/m7b-closed-loop-oracles.md`;
- `README.md` and `PLAN.md`;
- STATE.md, at `/wrap`;
- the gate summary under `docs/reviews/evidence/`.
At acceptance, the candidate moves to `docs/decisions/`, and nothing else moves.

## Execution

1. **The worktree.**
   - `git fetch origin`, then EnterWorktree `m7b-pr1-oracle-adr`, and confirm that HEAD equals `origin/main`.
   - If it does not, run `git worktree add .claude/worktrees/m7b-pr1-oracle-adr -b worktree-m7b-pr1-oracle-adr
     origin/main` instead.
   - `mv` this draft into the worktree as `docs/plans/m7b-pr1-oracle-adr.md`, before its first add. Nothing
     stays untracked in the main checkout. Its status line already reads "approved by Joe, 2026-10-17, on his
     word" (amendment 6). If a write of that line is refused, as lesson 2026-09-29 describes, it is not retried
     by any other route: carry on, name the refusal in the PR body, and Joe instructs the write.
2. **The first commit.**
   - Write the candidate, with Status: proposed.
   - Then write rev 16, the row's two edits, the M7b plan's two edits, README.md and PLAN.md.
   - Commit, push, and open the draft PR.
3. **The checks.**
   - `just docs-check`.
   - A scratch link check, written with Write, that calls `docs_check.check_links()` on the candidate and both
     plan files (amendment 1). It is expected to report nothing at every commit, and the PR body says so.
   - `git diff --check origin/main..HEAD`.
4. **`/review-cycle`.** An adversarial Codex review, on committed-branch scope, of the candidate at its
   `docs/plans` path.
   - It reviews against ADR-0013, ADR-0029 and the M7b plan (Q1, amendments 1, 2 and 11, Q12, R1 to R16),
     plus the row and rev 16.
   - It also checks whether each M7b oracle, as planned, can comply.
   - At most two correction rounds, done unarmed. A finding that changes a decision goes to Joe.
5. **The full gate.** Run `./scripts/check-full.sh` in the background on the review-fixed candidate. It passes
   on exit 0 and `check: OK`. Commit the trimmed `docs/reviews/evidence/<head>-gate-summary.log`.
6. **`/human-review`** writes the PR body. It holds:
   - the commands and their exit codes;
   - the link check's result;
   - Codex's verdict and thread id, and the reviewed head;
   - the reason for departing from 1a/1b;
   - Docs impact, with the row's two edits listed separately;
   - `git diff --name-only origin/main..HEAD`, and the correction-round count.
7. **Hand Joe the candidate, and stop** (amendment 7). In chat: the candidate's path, the reviewed head, and
   Codex's verdict. Joe arms after reading, not before.
8. **Acceptance** (Joe accepts the record by name and arms the guard). In the same turn, do the one commit
   from answer 1. It is a Markdown-only delta, so it needs `just docs-check`, the link check and `git diff
   --check <gated-head>..HEAD`, and the PR body names the gated head. A guard refusal is never retried by
   another route: stop and ask Joe to re-arm.
9. **Integration.** `git fetch`, and merge `origin/main` in if it moved. A non-Markdown change in it makes the
   gate stale.
10. **`/wrap`, then stop.**
    - STATE.md's next step becomes PR 2, eligible once ADR-0034 is accepted, and the README thread is removed.
    - If Joe has not accepted the record by then, the candidate stays proposed in `docs/plans/`, and STATE.md
      says so.
    - The merge is Joe's.

## Verification

- `just docs-check` exits 0 at every commit. The scratch link check reports nothing at every commit.
  `git diff --check origin/main..HEAD` exits 0.
- `git diff --name-only origin/main..HEAD` lists only the files named above. Nothing changes under `core/`,
  `plugins/`, `aircraft/`, `embedded/`, `icd/`, `python/` or `tests/`. No accepted ADR is edited, and
  `docs/decisions/` gains only the one moved file.
- `./scripts/check-full.sh` exits 0 with `check: OK`, as Tier 3 requires.
- Codex's adversarial verdict: each finding verified, then resolved or rejected with its reason.
- Each of Joe's brief items 1 to 4, answers 1 to 3 and amendments 1 to 7 is traced to the text.

## Risks and rollback

- **The record over-reaches.** Decision 1's border and its "not retroactive" clause guard against it capturing
  the tree's unit tests. Reading C guards against it forbidding (c)'s same-code relation.
- **A guard refusal or an auto-compact before the armed write** (lesson 2026-10-09): stop and ask Joe to
  re-arm.
- **Rollback.** Documents only, so the PR reverts cleanly. Before acceptance, nothing is under
  `docs/decisions/`.
