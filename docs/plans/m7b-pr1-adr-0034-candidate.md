# ADR-0034: In-house oracles: an independent reference, a bound fixed before the first run, and negative controls

Status: proposed
Date: 2026-10-17
Owner: Joe
Related/supersedes: supersedes nothing; extends ADR-0029 Decision 8; elaborates ADR-0013 and validation.md's
numerical section. No accepted record is edited.

Drafted with:
- `docs/plans/m7b-closed-loop-oracles.md`: Q1, Q12, amendments 1, 2 and 11, and the acceptance table;
- `docs/plans/m7b-pr1-oracle-adr.md`: Joe's answers and amendments of 2026-10-17.

Governs PLAN.md's M7b PR 2 to 4, and every in-house oracle after them.

## Context

- **What governs an oracle today.**
  - ADR-0013 governs an external ensemble band. The reference is the participating simulations' mean, and
    the tolerance comes from their disagreement. It "is **never** fitted to ASTRA's own output", and k "may be
    tightened as cases prove out, never widened". ADR-0028 applies it to the F-16's two members.
  - ADR-0029 Decision 8 makes bitwise equality the criterion between the NESC laws' sim-side peer and their
    `embedded/` port, on named objects, with both builds' floating-point flags stated before the first
    comparison.
  - `docs/process/validation.md`'s numerical section never invents or relaxes a threshold (step 3). It
    escalates when "a new case has no independent reference", and it never derives a tolerance from ASTRA's
    own output (step 6).
- **The gap.** M7b's three oracles have no external ensemble. Their references are built in this repository:
  - (a) the exact rational solution of a linear plant under sampled state feedback;
  - (b) a second implementation of M4's autopilot law, sim-side, compared with `embedded/autopilot`;
  - (c) the F-16's own mirror image and heading-rotated image.

  None of the records above covers an in-house reference, a bound fixed before the first run, or required
  negative controls. Decision 8 covers the NESC laws only.
- **Prior art.** Two M7 checks each carry two of the three elements.
  - M7 PR 9's `NescF16.Case1{1,2}TrimResiduals` (`tests/physics/nesc/nesc_f16_open_loop_test.cpp`):
    - its reference is independent: it was derived from the DML alone by
      `docs/reviews/evidence/m7-pr9-residual/derive_residual.py`, which "imports nothing from astra, links no
      plugin and reads no generated table";
    - its bounds were pre-registered in its plan, and a miss goes to Joe;
    - it has no negative controls of its own. PR 9's controls
      (`tests/physics/nesc/nesc_f16_negative_controls_test.cpp`) belong to the ADR-0028 band check.
  - M7 PR 7's `F16Evaluator` (`tests/physics/nesc/f16_evaluator_test.cpp`):
    - its 1e-12 bound was pre-registered, and a miss goes to Joe and never becomes a wider number;
    - its controls each fail (`AnUnsetEarthRateOrAltitudeFailsTheCheck`, `StaleControlsFailTheCheck`);
    - its comparison runs the same plugin code on both sides by design, so its reference is not independent.

  No existing check carries all three elements together. That is the gap this record closes.
- **The cost of a rule not fixed in advance.** M7 PR 11a's R11 bound missed on its first run and changed
  twice. Each change is a dated deviation of Joe's in `docs/plans/m7-pr11a-control-peer.md`'s Q2 (lesson
  2026-10-13).

## Decision

1. **Scope.** The line is drawn by where the reference comes from, not by the kind of test.
   - **An in-house oracle** is a gated check that meets two conditions:
     - a §16 row or a plan's acceptance table cites it as evidence that a model, law, path or port matches a
       reference;
     - its reference is computed or built in this repository, rather than placed under `docs/refs`.

     ADR-0013 and ADR-0028 keep the external ensemble, and this record takes the in-repo reference.
   - **A check that a plan's acceptance depends on counts as cited.** A PR may not take a check out of this
     rule by leaving it uncited. This is the counterpart of validation.md's step 6:
     - a new case with no independent reference either becomes an in-house oracle under this record, or goes
       to Joe;
     - a tolerance is never fitted to ASTRA's own output: no bound is set from the discrepancy it judges. A
       bound may still be evaluated on a run's values by a formula fixed in advance, as a running roundoff
       bound is on the recorded operands and results (Decision 4). The measured sensitivity yardstick is the
       one kind whose scale comes from extra runs rather than from a formula, within Decision 4's limits.
   - **The border with unit tests.**
     - An expected value that a reviewer can verify by reading it is a unit test, under validation.md. That
       covers a closed form, a textbook value, ADR-0001's sign pins, and a checkData shot.
     - An expectation that needs a run of the path under test, a derived bound, or a comparison set is an
       oracle, under this record.
   - **A frozen regression is not an oracle** under this record, as PLAN.md's M3 row says. That covers M3
     PR 8's 300 s trajectory and the R1 golden, which stay under the goldens boundary.
   - **Not retroactive.** No existing test gains an obligation. The prior art above is cited, not governed.
2. **The reference shares no code with the path under test.** No header, library, function or generated
   table on the compared path is common to both, other than the shared inputs named below.
   - **Shared inputs are named, not hidden.** These may be common to both sides:
     - the config text;
     - ICD v1's structs (ADR-0006);
     - the platform's compiler and libm.

     The PR's plan names each one and states what it cannot see, for example a libm defect common to both
     sides of a port comparison.
   - **A relation between two runs.** In a mirror or a rotation, the code under test runs on both sides by
     design. The reference is then the relation itself.
     - The test writes the relation from its derivation: the parity mask, the rotation, the wrap.
     - The test never computes the relation by calling the code under test.
     - The plan states the relation's blind spot. For example, a defect that is itself sign-symmetric passes a
       mirror.
3. **The bound is derived and fixed in the PR's plan before the first run.**
   - **The first run** is the first run that produces the compared output of the path under test. Before the
     first run, nothing of the compared output of the path under test is measured. What may be measured is the
     model's own data and constants, the platform's libm through a parity probe, the reference alone, and facts
     about the build or the gate that are not that output.
   - **After the first run,** the bound, its derivation and the comparison set never move without Joe. A miss
     stops the PR and goes to him.
   - **A change Joe makes** is recorded as a dated deviation beside the original text in the plan, never
     absorbed into it. `docs/plans/m7-pr11a-control-peer.md`'s Q2 records its two deviations this way.
4. **Four kinds of bound are allowed.** The plan names each bound's kind.
   - **Bitwise, or by value.** By value is `==`, under which +0.0 equals −0.0. The plan says which applies.
   - **Roundoff, by derivation.** A running bound charges each rounding at eps·|its own result|:
     - through the operation order read from the code, with the lines cited;
     - carried to the output by the computation's own coefficients;
     - evaluated on the run's recorded operands and results by the formula fixed in the plan, which is not
       fitting;
     - with no fitted constant (lesson 2026-10-13).
   - **A modeling bound.** It is derived from a stated, measured asymmetry of the model.
     - It is carried through a stated sensitivity estimate, whose envelope and assumptions are stated.
     - A pre-registered planted defect must exceed it by a stated margin, so an over-conservative bound cannot
       pass unseen.
   - **A measured sensitivity yardstick,** only where a derivation cannot reach.
     - The bound is a factor times the divergence measured under a perturbation set.
     - The factor and the set are fixed before any run, from a stated estimate, and never adjusted afterward,
       except by Joe under Decision 3.
     - Its perturbed runs are part of the oracle.
     - A planted defect must fail it by orders of magnitude.
     - **Why it is not a fitted tolerance.** It is the one kind whose scale is measured from extra runs, the
       perturbed ones, rather than given by a formula fixed in advance. Those runs measure the path's
       sensitivity to the fixed perturbation set, and they never read the discrepancy the oracle judges, so
       the bound is not fitted to it. validation.md's step 6 sends a case that no derivation
       reaches to Joe, and the M7b plan's Q12 is his decision for heading invariance. Each later use is
       pre-registered in a plan that Joe approves. ADR-0013's and ADR-0028's external bands keep their
       prohibition unchanged.

   No other kind is allowed. A case that none of the four can bound goes to Joe, never to an ad hoc bound.
5. **Negative controls.** Every oracle carries them.
   - **Each control is pre-registered** in the plan to fail on a named object: a stream, field, frame or
     exchange. Where the plan states a margin, or a first failing frame or exchange, that is part of the
     registration.
   - **A control that does not fail as registered stops the PR** and goes to Joe. That covers a control that
     passes, and a control that first fails on another object, frame or exchange.
   - **A control is planted only in test code:** a test plugin, a variant that holds the shipped code through
     its header, or a `test_defect` key. It is never planted in `core/`, or in a shipped default path.
6. **The pre-registered constants live in the tests,** not under `tests/golden/`. Each sits beside its
   derivation, or a citation of the plan's section. They come under the review rule: the reviewer checks each
   test's value against the plan's.
7. **Bounds and comparison sets are oracle data** under AGENTS.md's human-owned boundary. Narrowing a
   comparison counts as widening a tolerance, and either one needs Joe's explicit authorization. Narrowing
   means dropping a field, a frame, a stream or a run.
8. **ADR-0029 Decision 8, extended.** Its criterion governs any sim-side peer and its port, in either order of
   writing. That includes a sim-side peer written after its port, as M7b's oracle (b) is. The criterion
   requires:
   - bitwise equality on Decision 8's objects, never fewer, plus any objects the PR's plan adds;
   - both builds' floating-point flags, stated before the first comparison;
   - a difference treated as a porting defect until shown otherwise. A stated floating-point cause that makes
     bitwise equality infeasible goes to Joe;
   - the expression-order rule of ADR-0029's Consequences.

## Rejected alternatives

- **A paragraph in validation.md's numerical section instead of a record** (the M7b plan's Q1 alternative).
  - The rule sets what a bound may be and who may move one, and that is Joe's call. The rule also extends an
    accepted record's criterion.
  - Both need an immutable record that later PRs cite. validation.md is a procedure document, which a
    workflow PR may edit.
- **The pre-registered constants under `tests/golden/`.**
  - That tree holds the outputs of generators that Joe runs over external data (ADR-0013). These constants are
    derivations, which a reviewer reads beside the test they bound. A guarded file would split each constant
    from its derivation.
  - The plan's pre-registration, which Joe approves, is already the human checkpoint.
  - The trade-off is disclosed below: review, not the hook, guards these constants.
- **A fixed relative bound as a fifth kind** (the M7b plan's Q12), for example 1e-9 of a channel's range,
  stated as an estimate.
  - It states a number without deriving one.
  - Joe chose the measured yardstick instead. That is why there are four kinds, not five.

## Consequences and validation

- **What each oracle PR's plan pre-registers before its first run** (M7b's PR 2 to 4 first):
  - the reference, and its independence argument;
  - its shared inputs, and what each one cannot see;
  - each bound, with its kind and its derivation;
  - the comparison set;
  - each negative control's named object.
- **The costs, disclosed.**
  - `.claude/hooks/guard-oracle.sh` does not guard constants inside the tests. Review catches a moved bound by
    comparing the plan's value with the test's; the hook does not.
  - A miss stops the PR, and so does a control that does not fail as registered.
  - A shared input hides a defect common to both sides, and a relation hides a defect that respects the
    relation. Each plan states these blind spots, and never claims to see them.
- **Unchanged.**
  - ADR-0013's and ADR-0028's bands, and their generators;
  - ADR-0029's text;
  - every existing test (Decision 1).
- **Acceptance.**
  - Joe accepts this record by name. It then gates M7b's PR 2, per the M7b plan's PR table.
  - Before Joe reads it, an adversarial Codex review checks it against ADR-0013, ADR-0029 and the M7b plan.

## Approval

Proposed. It remains proposed until Joe gives his explicit decision, with its date.
