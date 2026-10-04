# Review: M6 PR 8, the run report (`astra.report`, `astra report`)

Spec/plan: [plan](../plans/m6-pr8-report.md), approved by Joe on 2026-10-03 with his six pre-approval points and
three additions (D1a, D1b and D2 to D10); [M6 plan](../plans/m6-analysis-tooling.md) R13, R15, C-11 to C-13.
Builder: Claude (Opus 5.5).
Reviewer: Codex (companion 1.0.6; model `gpt-6.1-sol` at effort `high` from `~/.codex/config.toml`; fresh
context). Two passes on the same candidate:
- general `review --base origin/main`, thread `01a104ad-4618-7820-92c2-11d4da4b24dc`;
- `adversarial-review --base origin/main` with a focus on the allow-list and R3b, id namespacing,
  determinism, provenance and escaping, the scenario consistency rule, no behavior change from the shared
  helpers, the tests' independence and the CLI's codes: thread `01a104ad-834c-7990-ad7b-fb410bf643f2`.
Scope: the committed branch diff, base `51410e2` (origin/main), head `08e286f`. The tree was clean (no staged,
unstaged or untracked files).
Validation: `rm -rf build && ./scripts/check-full.sh` exit 0 with `check: OK` on `6d1a26e`, the same code as
`08e286f`, which adds only the gate summary.
Round: initial review; fix 1 (`3aadfa7`); verification of fix 1; fix 2, for J-001 from Joe's review of #95 (this commit).

The general pass gave one finding (G-001, P2). The adversarial pass's verdict was "needs-attention", with two
medium findings (A-001, A-002) and two nonblocking notes (N-1, N-2). Codex reproduced G-001, A-001 and A-002
with read-only probes; Claude checked each against the code in this session (the scenario harness's write
order for A-001, `html.parser`'s self-closing handling for A-002). All five are fixed in this batch, each with
a regression.

## Finding G-001

Severity: major (general: P2)
Status: verified
Location: `python/astra/report.py:415-416` at `08e286f`
Requirement/ADR: ADR-0027's finalization states; plan D2 (a log is read strictly); R13
Failure: `log_report` checked only `frames_run is None`. A caller of the Python API that passes
`log.load(path, allow_incomplete=True)` gets a destructor-finalized log, or a complete but unpublished
`.partial`, with `frames_run` set, and the report showed it as an ordinary finished run. The CLI was not
affected (it reads strictly).
Requested outcome: refuse any log that is not complete and published.
Builder response: fixed. `log_report` raises `ReportError` unless `state == complete`, `published` and
`frames_run` is set, naming the state and whether the name is a `.partial`. Regression
`test_report_refuses_a_log_that_is_not_complete_and_published`: a real complete `.partial` copy read with
`allow_incomplete=True`, and a destructor-state log.
Verification: fix 1 verification pass (adversarial-review with a focus on the five IDs, thread
`01a104b9-4a8c-7183-a5b8-9bcf93d9d6a1`) on head `3aadfa7`, fix delta `08e286f..3aadfa7`: verdict approve, fixed (unpublished and destructor logs are refused, where the base accepted them; the regression fails against the base); new findings: none.
History: round 1, open → fixed-unverified → verified.
Human exception if needed: none.

## Finding A-001

Severity: major (adversarial: medium)
Status: verified
Location: `python/astra/report.py:281-285` at `08e286f`
Requirement/ADR: plan D2 (the scenario consistency check)
Failure: `check_scenario` required only the events strictly before `frames_run` to be in the log's `/writes`.
`astra.scenario._drive` writes a frame's events before that frame's step, so a run that halts at frame
`frames_run` has written its events at that frame too. Codex added an event at frame 512 to
`atmosphere_ceiling_halt` (which halts at 512 and has no writes): `check_scenario` accepted it and the report
was written, for a log that cannot be that scenario's.
Requested outcome: require the events at the halted frame; keep allowing events strictly after it to be
unwritten; regression including CLI code 65.
Builder response: fixed. The rule is now every event at or before `frames_run` (module docstring updated).
Regression `test_report_scenario_event_on_the_halted_frame_must_be_logged`: the frame-512 event is refused
through the API and by `astra report` with 65 and no file; an event at frame 600 of the same log, in a copy
without `expect_halt`, is still accepted, and the report shows `verify`'s unexpected-halt failure.
Verification: fix 1 verification pass (adversarial-review with a focus on the five IDs, thread
`01a104b9-4a8c-7183-a5b8-9bcf93d9d6a1`) on head `3aadfa7`, fix delta `08e286f..3aadfa7`: verdict approve, fixed (an unlogged event at frames_run is refused, later events stay allowed, the CLI exits 65 before writing; the regression fails against the base); new findings: none.
History: round 1, open → fixed-unverified → verified.
Human exception if needed: none.

## Finding A-002

Severity: major (adversarial: medium)
Status: verified
Location: `tests/python/resource_refs.py:235-236` at `08e286f`
Requirement/ADR: plan D9 (the allow-list), C-11
Failure: `disallowed('<html><head><style/>body{background:url(https://example.com/x.png)}</style></head></html>',
REPORT)` returned `[]`. `handle_startendtag` checked the element but never turned on CSS scanning; an HTML
parser ignores the slash on `style`, so the text after it is a live stylesheet loading an external URL. No
ASTRA output exercised it.
Requested outcome: reject a self-closing `style` in HTML, or scan what the browser sees; add the snippet as a
negative test.
Builder response: fixed. A self-closing `<style/>` or `<script/>` is flagged in every profile (nothing ASTRA
writes uses one; the module docstring states the rule). The reproduced snippet and a `<script/>` twin are in
`CONSTRUCTS`, so each is checked in all three profiles and named by its own line in REPORT.
Verification: fix 1 verification pass (adversarial-review with a focus on the five IDs, thread
`01a104b9-4a8c-7183-a5b8-9bcf93d9d6a1`) on head `3aadfa7`, fix delta `08e286f..3aadfa7`: verdict approve, fixed (self-closing style and script are flagged in all three profiles; the regression fails against the base); new findings: none.
History: round 1, open → fixed-unverified → verified.
Human exception if needed: none.

## Finding N-1

Severity: minor (adversarial: nonblocking)
Status: verified
Location: `tests/python/test_report.py`, `test_report_batch_sizing_rows_equal_mc_sizing_check`, at `08e286f`
Requirement/ADR: R13 (the batch's sizing check)
Failure: the ranking claim's parameter table was compared by names only; a wrong rho, interval or note cell
would have passed.
Requested outcome: compare every parameter cell.
Builder response: fixed. Every cell (parameter, rho, the interval, the |rho| bounds, the note) is compared with
`mc_sizing.check`'s values in the report's format.
Verification: fix 1 verification pass (adversarial-review with a focus on the five IDs, thread
`01a104b9-4a8c-7183-a5b8-9bcf93d9d6a1`) on head `3aadfa7`, fix delta `08e286f..3aadfa7`: verdict approve, coverage fixed (a mutation of any of the seven parameter cells fails the assertion, where only names were checked before; a test-only fix); new findings: none.
History: round 1, open → fixed-unverified → verified.
Human exception if needed: none.

## Finding N-2

Severity: minor (adversarial: nonblocking)
Status: verified
Location: `python/astra/report.py:193-195` (`inline_svg`) at `08e286f`
Requirement/ADR: plan D8 (inlined figures keep `astra plot`'s bytes, ids namespaced)
Failure: the id and reference substitutions also ran inside matplotlib's label comments, so a label such as
`id="sample"`, `xlink:href="#data"` or `url(#temperature)` was rewritten in the comment. No visible effect,
but the inline figure was no longer `astra plot`'s bytes outside the namespacing.
Requested outcome: confine the substitutions to markup.
Builder response: fixed. `inline_svg` splits the text at comments and namespaces only the markup between
them. Regression `test_report_inline_svg_leaves_label_comments_as_drawn`: a figure whose label holds all three
forms keeps its comments exactly, and its ids still check clean.
Verification: fix 1 verification pass (adversarial-review with a focus on the five IDs, thread
`01a104b9-4a8c-7183-a5b8-9bcf93d9d6a1`) on head `3aadfa7`, fix delta `08e286f..3aadfa7`: verdict approve, fixed (label comments stay byte-identical while markup ids and references are namespaced; the regression fails against the base); new findings: none.
History: round 1, open → fixed-unverified → verified.
Human exception if needed: none.

## Finding J-001

Severity: major (Joe's review of #95, from a Cowork check)
Status: fixed-unverified
Location: `tests/python/resource_refs.py`, `_Scanner.handle_data` and comment handling, at `12c9536`
Requirement/ADR: plan D9 (the allow-list), C-11; R13 and R3b rely on it
Failure: three constructs passed `disallowed` in every profile. A character reference in `<style>` text
(`<svg><style>&#64;import "https://example.com/x.css";</style></svg>`, and `&#117;&#114;&#108;(` or `&commat;`
forms), and a tag inside it (`<svg><style><image href="https://example.com/a.png"/></style></svg>`).
Inside an SVG, inline or a standalone file, a browser decodes the references and parses the tags, but
`html.parser` hands style text over raw. And an abrupt comment, `<!--><img src="https://example.com/a.png"><!-- -->`
or `<!--->...`: a browser closes it at once, so the `<img>` is live, while Python 3.12's `html.parser` reads on
to the next `-->` and reports the markup as comment data. Claude reproduced all of them in this session.
(`--!>` was already caught: this Python ends the comment there and sees the element.)
Requested outcome: flag them; regressions.
Builder response: fixed. In every profile, `<style>` text holding `&` (a character reference) or `<` (markup)
is flagged, and so is a comment whose data starts with `>` or `->` or contains `--!>`. Nothing ASTRA writes uses
either (every report kind, every SVG and the plotly page still check clean). On a Python that parses abrupt
comments per spec, the hidden markup is visible and flagged as elements, so both behaviors are covered. The
module docstring states the three rules. Regressions: the seven probes are in `CONSTRUCTS`, checked in all
three profiles and named by their own line in REPORT.
Verification: pending (fix 2 verification pass).
History: round 2 (Joe-directed), open → fixed-unverified.
Human exception if needed: none.

## Completion

G-001, A-001, A-002, N-1 and N-2 are verified (fix 1 verification pass, approve, new findings: none). J-001,
from Joe's review of #95, is fixed-unverified, pending the fix 2 verification pass on the fix delta. No finding
was rejected or deferred. Scope not reviewed: none (the whole committed diff). Validation after fix 2: the
affected tests, `just ruff`, pyright, and a fresh full gate on the fix head (recorded in the PR body). Codex's
recommendation is not Joe's approval.
