# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines, says nothing that changes at merge)

## In flight
M6 PR 5, plots: PR #89, branch `worktree-m6-pr5-plots`. Plan: `docs/plans/m6-pr5-plots.md`. Its stage, results
and pending decisions are in the PR body.
What it delivers:
- `astra.plot`: YAML plot packages (figures, panels, series; `{v}` over every vehicle; display units through the
  bound `core/util/units`; a Python function by module or by a `.py` beside the package).
- matplotlib SVG or PNG files, byte-deterministic for SVG; one offline plotly HTML page.
- Framework, template and notional-transport packages; `astra plot`; matplotlib and plotly pinned.
- Tests for R1 to R4 and R3b, including an independent ground-track oracle (the logged flat-Earth position)
  and a resource check that reads every `srcset` candidate.

## Last verified result
The gate summary is under `docs/reviews/evidence/`. The PR #89 body records each check with its exit code and
candidate revision, the mutation outcomes, and the Codex verdict and thread id.

## Next step
1. Next M6 task: PR 6 (Monte Carlo figures, `astra.mc_analysis`, `astra mc analyze` and `astra mc rerun`) or
   PR 9 (the frame-time benchmark), per `docs/plans/m6-analysis-tooling.md`. Its `/wrap` runs the docs step in
   branch scope.
2. PR 8 (the report) inherits two items:
   - C-12's report part: matplotlib's counter ids and glyph ids repeat across SVG files, so inlined SVGs need
     namespacing;
   - PLAN.md's (M6) open item to replace `tests/python/resource_refs.py`'s enumeration with an approved set.
3. PLAN.md's (M6) open item on the ground track's aspect ratio is open for any later plot PR.

## Tried / failed
- Correction rounds and failed integrations are counted in the PR #89 body.
- The worktree guard refused commands: a `sed` with a variable, a Python heredoc, a variable loop, a `git -C`.
  Each was redone with Edit, a script run by its path, or a plain command; the PR body lists them.
- Two mutants of the first test suite survived (an overwrite, one salt per package) until two tests were added.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
