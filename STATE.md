# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M3 PR 1 (`PLAN.md` line ~518): `aircraft/_template/` + `aircraft/notional_transport/`
scaffolding — an aircraft-owned `physical/mass` `MassPropertiesModel` plugin each (ADR-0009), a
`configs/cruise.yaml` each, `just new-aircraft <name>`. Tier 1, no plan critique. Brief refined
through several rounds of Joe's corrections (Izz's 0.97 perpendicular-axis factor, the Ixz
S&L-product-vs-tensor sign convention, O1's source switched mid-brief to a direct transcription of
JSBSim's "737" example, `docs/refs/aircrafts/737.xml`) plus four scaffolding directions (numbers
in YAML not code; anonymous-namespace discipline for static-plugins; no `aero`/`turbofan` dirs yet
— PR 3/4; §13 becomes a verbatim `cruise.yaml` embed). Worktree `m3-pr1-scaffolding`, branch
`worktree-m3-pr1-scaffolding`, **committed and pushed** (`fc3b6d7`), **PR**
<https://github.com/jhawleyjr/ASTRA/pull/37> (draft, opened this session) against `main`'s
`91aa358` — PR 0 (`#36`) confirmed **merged** as `91aa358` via `git log` at session start, per
Joe's instruction to verify that way, not STATE.md.

## Last verified result
`just check` green 4x this session (initial, post-format-fix, post-Codex-fix-batch, `docs-check`
alone after the review docs); final run: all four presets, clang-tidy, pyright 0 errors, pytest 38
passed. Targeted: `ctest -R notional_transport_mass` (3/3), `-R template_mass` (1/1), `-R
NotionalTransportCruiseConfig` (1/1); `pytest tests/python/test_new_aircraft.py` (6/6). Manual:
real `just new-aircraft` run + `ASTRA_STATIC_PLUGINS=ON` build, no ODR collision (scratch removed,
not committed); isolated single-target build reproduced then confirmed the R-001 fix. Codex
(general + adversarial + fix-verification, `codex-companion` 1.0.6): 3 findings (R-001 major,
R-002/R-003 medium), all fixed and Codex-verified, "New findings: none" — `docs/reviews/
m3-pr1-scaffolding-review.md`; packet in `docs/reviews/m3-pr1-scaffolding-human.md`.

## Next step
Joe's review/merge decision on PR #37; `PLAN.md`'s PR 1 checkbox stays unchecked until then. Once
merged: PR 2 (write-seam primitive, needs a Codex plan critique) is next; PR 3/4 depend on PR 1's
plugins (now true) and PR 2/3's own ADR. Not blocking: `plugins/physical/mass/constant`'s own
undocumented `ixz` convention (new `PLAN.md` follow-up, unassigned); D8 (trim residual norm, PR
6); O4 superseded by this session's S&L §4.2 sanity-check framing (PR 7's line updated).

## Tried / failed
Correction rounds: 1/2 used (Codex's R-001/R-002/R-003 batch, fixed and Codex-verified in one
round; pre-implementation brief revisions are not review-fix-loop rounds). Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
