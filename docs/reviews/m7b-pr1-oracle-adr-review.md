# Review: M7b PR 1, ADR-0034 (the in-house oracle rule) and architecture rev 16

Spec/plan: [PR brief](../plans/m7b-pr1-oracle-adr.md), [milestone plan](../plans/m7b-closed-loop-oracles.md)
Builder: Claude Opus 5.5
Reviewer: Codex (companion 1.0.6, default model, `model_reasoning_effort = "high"` from `~/.codex/config.toml`;
fresh context; adversarial review against ADR-0013, ADR-0028, ADR-0029, validation.md and the M7b plan),
thread `01a14c53-61e5-74d0-bc4b-7ba50ff237cf`; Joe (human review of the candidate at `26da332`, 2026-10-17)
for J-001 to J-003, with a Codex verification pass on their delta
Scope: committed branch diff `origin/main` `7a92115`..`895cebe` (clean tree): the candidate
`docs/plans/m7b-pr1-adr-0034-candidate.md`, the brief, `docs/architecture.md`,
`docs/plans/m7b-closed-loop-oracles.md`, README.md, PLAN.md
Validation: `just docs-check` exit 0, the scratch link check (candidate and both plans) 0 problems, and
`git diff --check origin/main..HEAD` exit 0 at `c070df3`; the full gate's result is in the PR body
Round: fix 2 (the last autonomous correction round), then Joe's three edits at human review (J-001 to
J-003), made on his instruction and not an autonomous correction round

Initial pass: needs-attention, one finding (R-001). Codex reports the cited paths, test names and quotes check
out, and the architecture edits match amendment 11.
Verification pass of fix 1: Codex (same companion and effort), thread `01a14c56-c74d-7af0-bd28-f7a995f3de73`,
`895cebe`..`6e61178`: R-001 resolved; one new finding (R-002), introduced by fix 1's wording.
Verification pass of fix 2: Codex (same companion and effort), thread `01a14c60-b7f5-7420-bacb-5ee3ffb783b4`,
`6e61178`..`26da332`: approve; R-001 and R-002 hold; "New findings: none".

## Finding [R-001]

Severity: major (Codex medium)
Status: verified
Location: `docs/plans/m7b-pr1-adr-0034-candidate.md:63-65` and `:109-113` at `895cebe`
Requirement/ADR: validation.md's numerical section, step 6; the M7b plan's Q12 and R13; ADR-0013
Failure: Decision 1 says "a tolerance is never derived from ASTRA's own output", but Decision 4's measured
sensitivity yardstick computes its bound from perturbed runs of the path under test, as Q12 requires for R13.
Read literally, R13 cannot satisfy both clauses, so a reviewer would hold two conflicting acceptance rules.
Requested outcome: distinguish prohibited fitting to the judged discrepancy from the pre-registered
sensitivity computation, and state how it relates to validation.md step 6 while keeping ADR-0013's and
ADR-0028's external-band prohibition.
Builder response: fixed. Verified against the candidate's text, Q12 (`m7b-closed-loop-oracles.md:666-676`) and
validation.md step 6. No decision changes; Q12 already decided the yardstick.
- Decision 1's clause now reads "a tolerance is never fitted to ASTRA's own output: no bound is set from the
  discrepancy it judges", and names the yardstick as the one bound computed from runs of the path under test,
  within Decision 4's limits.
- Decision 4's yardstick gains "Why it is not a fitted tolerance": its runs measure sensitivity to the fixed
  perturbation set and never read the judged discrepancy; validation.md step 6 sends a case no derivation
  reaches to Joe, and Q12 is his decision for heading invariance; each later use is pre-registered in a plan
  Joe approves; ADR-0013's and ADR-0028's external bands keep their prohibition.
Verification: Codex, thread `01a14c56-c74d-7af0-bd28-f7a995f3de73`, at `6e61178`: "resolved ... The text
distinguishes fitting from Q12/R13's pre-registered sensitivity measurement, identifies Joe's decision under
validation step 6, and preserves ADR-0013/0028's external-band prohibition."
History: initial review, open; fix 1, fixed-unverified; verification pass, verified.
Human exception if needed: none.

## Finding [R-002]

Severity: major (Codex medium)
Status: verified
Location: `docs/plans/m7b-pr1-adr-0034-candidate.md:66-67` and `:116-118` at `6e61178`
Requirement/ADR: the M7b plan's oracle (a) F, L and C (`m7b-closed-loop-oracles.md:249-266`) and R2; the
candidate's own Decision 4 (the roundoff kind)
Failure: fix 1 called the yardstick "the one bound computed from runs of the path under test" (Decision 1) and
"the one kind computed from ASTRA's own runs" (Decision 4). But F charges each rounding at its own result, L
evaluates γ_(n+1)·(Σ|K_i x_i| + |G r|) on the recorded state, and C accumulates those bounds: each is computed
from a run's values without being fitted to the judged discrepancy. Read literally, the exclusivity contradicts
the roundoff kind and PR 2's required checks.
Requested outcome: qualify both statements to separate an empirical, perturbation-derived scale from an
analytic formula evaluated on recorded values, keeping the prohibition on fitting to the judged discrepancy.
Builder response: fixed. Verified against `m7b-closed-loop-oracles.md:249-266` (F, L, C). No decision changes.
- Decision 1's clause now says a bound may be evaluated on a run's values by a formula fixed in advance, as a
  running roundoff bound is on the recorded operands and results, and that the yardstick is the one kind
  whose scale comes from extra runs rather than from a formula.
- Decision 4's roundoff kind gains: "evaluated on the run's recorded operands and results by the formula fixed
  in the plan, which is not fitting".
- Decision 4's "Why it is not a fitted tolerance" now reads "the one kind whose scale is measured from extra
  runs, the perturbed ones, rather than given by a formula fixed in advance".
Verification: Codex, thread `01a14c60-b7f5-7420-bacb-5ee3ffb783b4`, at `26da332`: approve; R-001 and R-002
hold; "New findings: none".
History: verification pass of fix 1, open; fix 2, fixed-unverified; verification pass of fix 2, verified.
Human exception if needed: none.

## Finding [J-001]

Severity: major (Joe, human review)
Status: fixed-unverified
Location: `docs/plans/m7b-pr1-adr-0034-candidate.md:77-78` at `26da332`
Requirement/ADR: the candidate's own Decision 2; ADR-0006 (ICD v1's structs in `icd/astra_icd.h`)
Failure: Decision 2's first sentence says no header on the compared path is common to both sides, and its next
bullet allows ICD v1's structs, which are the header `icd/astra_icd.h` that both the peer and
`embedded/autopilot` include. The decision contradicts itself.
Requested outcome (Joe's text): the first sentence becomes "No header, library, function or generated table on
the compared path is common to both, other than the shared inputs named below."
Builder response: fixed, with Joe's text verbatim. No other line of Decision 2 changes.
Verification: pending (Codex verification pass of J-001 to J-003).
History: human review, open; Joe's edit, fixed-unverified.
Human exception if needed: none.

## Finding [J-002]

Severity: major (Joe, human review)
Status: fixed-unverified
Location: `docs/plans/m7b-pr1-adr-0034-candidate.md:93-97` at `26da332`
Requirement/ADR: the candidate's own Decision 3; the M7b plan's PR 2 and PR 3
Failure: "Only three things may be measured before it" is an allow-list that the M7b plan already breaks. PR 2
measures the base gate's wall time on `origin/main` first, and PR 3 reads both builds' flags from
`compile_commands.json` before the comparison.
Requested outcome (Joe's text): the prohibition is on the compared output. "Before the first run, nothing of
the compared output of the path under test is measured. What may be measured is the model's own data and
constants, the platform's libm through a parity probe, the reference alone, and facts about the build or the
gate that are not that output."
Builder response: fixed, with Joe's text verbatim after the bullet's definition of the first run, which stays.
The three-item list it replaces goes, and with it that list's example ("such as a table's asymmetry or a fixed
Earth rate").
Verification: pending (Codex verification pass of J-001 to J-003).
History: human review, open; Joe's edit, fixed-unverified.
Human exception if needed: none.

## Finding [J-003]

Severity: major (Joe, human review)
Status: fixed-unverified
Location: `docs/plans/m7b-pr1-adr-0034-candidate.md:116` at `26da332`
Requirement/ADR: the candidate's own Decisions 3 and 4
Failure: Decision 4's yardstick says its factor and set are "never adjusted afterward", while Decision 3 lets
Joe change a bound by a dated deviation. The two disagree.
Requested outcome (Joe's text): "never adjusted afterward, except by Joe under Decision 3."
Builder response: fixed, with Joe's text verbatim.
Verification: pending (Codex verification pass of J-001 to J-003).
History: human review, open; Joe's edit, fixed-unverified.
Human exception if needed: none.

## Completion

R-001 and R-002 are verified; both autonomous correction rounds are used (2 of 2). J-001 to J-003 are Joe's
edits at human review, not a correction round; their Codex verification is pending. Joe also verified, and
left standing, the ADR-0013 quotations, the hook's name `guard-oracle.sh` and the prior-art paths. A further
finding goes to Joe.
