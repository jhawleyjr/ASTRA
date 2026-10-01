# Follow-up B: Stevens & Lewis equation citations (Tier 0, comment-only)

Recovered from `~/.claude/plans/pasted-content-id-1cce-follow-up-b-cozy-fox.md` on 2026-09-30. Approval: the file records none; the work merged as #74. Never committed: neither the file nor git history says why.

## Context

M4 PR 2's review (`docs/reviews/m4-pr2-icd-pack-human.md`, decision record item (b)) found that
`frames.hpp` and the golden header `tests/golden/frames/adr0001_cases.hpp` cite the wrong equation numbers.
They cite 1.3-11 for the DCM and 1.8-16 for the half-angle quaternion. PLAN.md carries this as open item
"Follow-up B". Joe authorized it as Tier 0, with the golden guard armed for this session
(`ASTRA_TOUCH_GOLDENS=1`, confirmed by the SessionStart hook). In this session Joe widened the scope to the
same half-angle miscitation in four Python files. The PR body records that decision as his.

Worktree `worktree-followup-b-citations` at `ffb395a` (= origin/main); the tree is clean.

## Citations verified against the page images (2015 3rd ed., Read tool on the PDF)

Image pages are offset by 14 from book pages. Every formula in the code was matched term by term:

| Item | Book | Matches code |
|---|---|---|
| DCM C_frd/ned from Euler | Eq. (1.3-10), p. 12 | `dcm_from_euler`: all nine entries |
| Euler from DCM | Eq. (1.3-11), p. 12 | `euler_from_dcm`: phi=atan2(c23,c33), theta=-asin(c13), psi=atan2(c12,c11) |
| Half-angle Euler to q | Example 1.8-1 Eq. (1), p. 52 (printed with ±) | `quaternion_from_euler` takes the + sign; all four components match |
| q to DCM | Eq. (1.8-16) array form, Eq. (1.8-18) expansion, p. 53 | `dcm_from_quaternion`: all nine entries |

The citations listed below are correct and are left alone.
- Kinematics "eq. 1.8-15" (p. 51). 1.8-15 is the written-out ½Ωq form. It equals q ⊗ (0, ω), which 1.8-14 states in product notation. The citation is defensible, so it stays unchanged; I'll mention it to Joe.
- `test_lockstep.py:271`, `icd_pack_test.cpp`, and eq. 1.4-4 in `linearize.py`, per Joe.
- `docs/plans/` is historical and stays unchanged.

## Edits (comment/docstring text only; existing lowercase "eq." style kept inside each file)

**`core/include/astra/util/frames.hpp`**
- L24-27 NOLINT rationale: change "(eq. 1.3-11, 1.8-16, 1.8-18)" to "(eq. 1.3-10, 1.3-11, 1.8-18, Example 1.8-1 eq. (1))", then rewrap to 100 columns or fewer.
- L29 section banner: "(Stevens & Lewis eq. 1.3-10, 1.3-11, 1.8-16/1.8-18, Example 1.8-1)".
- L31 `quaternion_from_euler`: "Example 1.8-1 eq. (1), p. 52 (its + sign), half angles a = phi/2, …".
- L43 `dcm_from_euler`: "eq. 1.3-10."
- L60 `dcm_from_quaternion`: "eq. 1.8-16 (array form), expanded in eq. 1.8-18; checked equal to …", rewrapped.
- L75 `euler_from_dcm`: add a leading "eq. 1.3-11: ".

**`tests/golden/frames/adr0001_cases.hpp`** (header comment only, inside `clang-format off`)
- L11 "C_nb from Euler (eq. 1.3-10):"
- L15 "q_nb from Euler (Example 1.8-1 eq. (1), p. 52, + sign), half angles …"
- L18 "C_nb from q (eq. 1.8-16/1.8-18), checked equal …"
- L22 "Euler from C_nb (eq. 1.3-11, 1-based indices): …"
- No data line and no other file under `tests/golden/`.

**Python, half-angle citation only.** Each "eq. 1.8-16" becomes "Stevens & Lewis Example 1.8-1 Eq. (1), p. 52", or "S&L …" where the file abbreviates. Lines are rewrapped to 100 columns or fewer.
- `python/astra/trim.py:88` (docstring)
- `python/astra/linearize.py:4` (module docstring; keep "1.4-4"), `:113`, `:152`
- `tests/python/test_trim.py:125`, `tests/python/test_linearize.py:425` (comments)

**PLAN.md.** Remove the Follow-up B open item (L256-260). Add a line under M5 after PR 10, following its precedent: "PR 11 (#NN): Follow-up B: Stevens & Lewis citations corrected (comment-only) in `frames.hpp`, the golden header's comments, and the half-angle citation in four Python files (scope widened by Joe); the Follow-up B open item is removed. Plan: none (Tier 0). Packet: in the PR body."

**STATE.md.** Rewritten once, at `/wrap`.

## Sequence (the git commands are plain calls; compound commands go in scratchpad scripts, per lessons 2026-09-26/27)

1. Commit the C++ comment fixes, push `worktree-followup-b-citations`, and open the draft PR (template headings, "in progress").
2. Commit the Python comment fixes. Commit the PLAN.md change once the PR number is known. Push after each commit.
3. Check that the changes are comment-only (scratchpad script):
   - C++: `clang++-18 -fpreprocessed -E -P` on the origin/main and HEAD versions of both headers; the diff must be empty.
   - Python: compare `ast.dump` of the old and new files with the docstring `Constant` nodes stripped; they must be equal.
   - `git diff origin/main --stat` must list only the eight files, plus the evidence log once it exists.
4. `./scripts/check-full.sh > build/validation/full.log 2>&1` (background; wait on its handle). It must exit 0 with `check: OK`. Then commit `docs/reviews/evidence/<head>-gate-summary.log` (trimmed per validation.md) and push. Also run `python3 scripts/docs_check.py` and `git diff --check origin/main`.
5. One Codex pass on the final diff `origin/main...HEAD` through `/review-cycle`. The prompt asks Codex to check each citation against the formula it labels. On Tier 0, a finding that is not a factual error becomes a follow-up.
6. Write the PR body with `/human-review` per `docs/templates/pr-description.md`. It includes the verification table above, Joe's scope widening, the checks (command, exit code, counts, gate summary link), the Codex verdict and thread id, and the 1.8-15 note. Write it with the Write tool, then run `gh pr edit --body-file` as its own call.
7. `/wrap`: rewrite STATE.md, commit, push. The PR stays a draft; Joe merges.

## Verification (acceptance)
- `git diff origin/main` shows comment/docstring-only hunks in exactly `frames.hpp`, `adr0001_cases.hpp`, `trim.py`, `linearize.py`, `test_trim.py` and `test_linearize.py`, plus PLAN.md, STATE.md and the gate summary log.
- The step 3 comment-only checks come out empty or equal.
- `./scripts/check-full.sh` exits 0 with `check: OK`. `python3 scripts/docs_check.py` exits 0. `git diff --check` prints nothing.
