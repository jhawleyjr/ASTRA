# Review: M6 PR 8b, the batch envelope's size

Spec/plan: [docs/plans/m6-pr8b-envelope.md](../plans/m6-pr8b-envelope.md) (approved by Joe 2026-10-03, with
six changes); the [M6 plan](../plans/m6-analysis-tooling.md)'s R5 and C-10.
Builder: Claude (claude-opus-5-5)
Reviewer: Codex (companion 1.0.6; fresh context): general review, thread
`01a1054b-2dcb-7711-bb51-35f5ed21d347`; adversarial review, thread `01a1054b-2d9b-7e21-883c-e4a60abd0795`,
with a focus naming Joe's known defect (R-001), `_band`'s reduceat edges, the test helpers and the
before/after claims.
Scope: round 1, `--base origin/main` (`8055ce7`), committed head `7b7e791` (the gated head `a2dbf3c` plus
its gate summary); the worktree held exactly that tree during both passes (the builder's draft fix was set
aside as a patch outside the repository).
Validation: full gate at `a2dbf3c`, exit 0, `check: OK`
([summary](evidence/a2dbf3c-gate-summary.log)); the fix batch's gate follows.
Round: fix 1 (one correction round of two).

## Finding R-001

Severity: major
Status: fixed-unverified
Location: `python/astra/mc_analysis.py:1074-1076` and `:1090-1091` at `7b7e791` (`_band`'s `step="post"`
return and its `fill_between` call)
Requirement/ADR: the plan's D1 (every finite frame inside the band; an all-NaN column is a gap) and the
module docstring's Figures paragraph.
Failure: Joe named it before the review; both Codex passes confirmed it independently from matplotlib
3.11.2's source. `fill_between` masks a NaN node before expanding `step="post"`
(`collections.py:1524-1540`) and ends each region at its last finite x (`:1578-1592`), so an all-NaN column
j also removes column j - 1's width, and j - 1's finite frames fall outside the band. Reproductions: Codex
general, 9,001 frames with NaNs at 1000-1010, frames 991-999 outside; Codex adversarial, 2,701 frames with
NaNs at 3-6, frames 1 and 2 outside; the builder's new test, 300,001 frames with column 450
(frames 150,000-150,333) NaN, frames 149,667 onward outside. The exact band loses no finite frame at a NaN.
No test caught it: the spike test's NaN column is only partly NaN, and `_bands` assumed one path per band.
Requested outcome: close each finite region at its last column's right edge before a gap, keep the vertex
bound, and add a regression with an all-NaN column, checked across every path of a split band, NaNs at
shared boundary frames included.
Builder response: fixed in the fix-batch commit. `_band` now returns each column as its own two points,
`(e_j, v_j)` and `(e_(j+1), v_j)` (`np.repeat`), and `draw_envelope` passes no `step`, so a region before a
NaN column ends at that column's left edge, which is the previous column's right edge. A reduced band is
`4 ENVELOPE_COLUMNS + 3` = 3,603 vertices (was `+ 5`); split regions total at most that too. Docstring and
vertex assertions updated. Tests: `_bands` returns every path of a band, and `_band_at`, `_contains` and
`_edges` read them all; new `test_mc_analysis_envelope_band_keeps_the_columns_beside_an_all_nan_column`
(column 450 of 900 NaN over 300,001 frames, its shared boundary frames included: two regions, every finite
frame contained, the 333 frames either side contained, total vertices within the bound). It failed before
the fix at frame 149,667 and passes after. A mutant restoring `step="post"` fails it (and the vertex-count
tests); the exact band passes it.
Verification: pending Codex verification of the fix batch.
History: round 1, open (named by Joe; confirmed by both Codex passes) -> fixed-unverified.
Human exception if needed: none.

## Completion

Open critical/major findings: R-001, fixed and awaiting verification. The adversarial pass checked and
found correct: the last column (it includes the final frame), the boundary frame shared by neighboring
columns, the integer edges, the 901-frame threshold, the test helpers (they reject a band with no coverage
at a finite frame), R5's unchanged `Line2D` checks, and the before/after claims for the product code at
`4e9730a`. New findings beyond R-001: none from either pass. The reviewers ran no build or test of their
own (the general pass's scratch reproductions aside). Reviewer recommendation is not Joe's approval.
