# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
Workflow-v3, branch `workflow-v3`, main checkout, committed through `60caf9b` (pushed). Fixed
and verified R-007/R-010 (`cd` `--`/option/cluster parsing). Fixed R-009/R-011/R-012 for their
*originally reported* shapes, but Codex's final authorized re-verification pass on `60caf9b`
found each still incomplete and reported one new false-block (R-013) — see review doc for full
detail. **`.claude/hooks/guard-oracle.sh` currently has confirmed, reproduced gaps**: a
multiline `cd`-after-pipe-then-newline still bypasses directory tracking (R-006/R-009); a
digit/flag-addressed sed `w` and the long `sed --expression=` write form still bypass write
detection (R-008/R-011); a separate, untouched Bash-text pre-filter regex still false-blocks an
attached `-e` script on a direct literal path (R-008/R-012); and the new sed-write heuristic
false-blocks a read-only script merely containing "w" in address/regex text (R-013). All six
reproduced directly against `60caf9b` this session (JSON-on-stdin, no real file written).

## Last verified result
`just hooks-test`: 104/104. `just docs-check`: OK, 24 files. Full local gate on `60caf9b`:
`EXITSTATUS=0`, `check: OK`, 274/274×4, `docs/reviews/evidence/round4-fixverify-full-gate.log`
(no code changed since, so not rerun for this doc-only reconciliation). Codex: general review
of `60caf9b` clean; adversarial fix-verification of R-006–R-012 against `60caf9b`: R-007/R-010
verified, R-006/R-008/R-009/R-011/R-012 needs-attention (reopened), R-013 new — retained at
`docs/reviews/evidence/60caf9b-codex-r006-r012.log`. **This is the last review pass authorized
for this batch**; the six open items above are not Codex-verified as fixed and no further
autonomous fix was attempted, per this session's explicit bound.

## Next step
Prepare Joe's `/human-review` packet disclosing the six open guard-oracle findings as blockers,
not resolved — his decision: authorize a further bounded fix batch, accept as documented
residual risk, or something else. Then run `/wrap`; do not resume `PLAN.md`'s M2 PR 3b
meanwhile. R-001–R-005, R-007, R-010 are the only findings actually verified.

## Tried / failed
Correction rounds: 2/2 — budget exhausted (see `workflow-v3-review.md`'s Completion). This
session's entire Round 4 (R-007 through R-013, plus the STATE/review-doc reconciliation) was
the one bounded fix batch authorized beyond that budget; its fix-verify cycle is now closed per
its own explicit bound — six findings remain open, handed to Joe. Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
