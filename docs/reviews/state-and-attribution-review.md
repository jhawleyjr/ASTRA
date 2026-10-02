# Review: STATE.md that survives the merge, and commits without a co-author trailer

Spec/plan: [docs/plans/state-and-attribution.md](../plans/state-and-attribution.md)
Builder: Claude (claude-opus-5-5)
Reviewer: Codex companion 1.0.6, fresh context, general `review` (thread
`01a0fa9e-a7a3-7b32-9e17-073c82d805bd`) and `adversarial-review` (thread
`01a0fa9e-a771-7f31-9f9c-f32f2b40e169`, verdict needs-attention)
Scope: branch diff `origin/main` (8e97040) to 75c4c78, committed, clean tree; every path in that diff
Validation: the full gate on d8e947a, exit 0, `check: OK`
([summary](evidence/d8e947a-gate-summary.log)); 75c4c78 adds only that summary log
Round: fix 1 (correction rounds 1/2)

Codex rated every finding medium (general review: P2). Each maps to major here: each defeats the rule
it belongs to.

## Finding [R-001]

Severity: major
Status: fixed-unverified
Location: `.claude/hooks/no-main.sh:90-97` (75c4c78)
Requirement/ADR: plan §2, rule 3 (refuse a co-author trailer however the message arrives)
Failure: `git commit -m $'subject\n\nCo-Authored-By: X'`, `--trailer $'Co-Authored-By: X'` and
`git commit -F - < msg-trailer.txt` exited 0. The hook scanned unexpanded text and skipped `-F -`.
(adversarial finding 1; general review P2 "Verify stdin sources".)
Requested outcome: inspect decoded literal message arguments and verifiable stdin; refuse the rest.
Builder response: fixed. Rule 3 moved to `.claude/hooks/commit_trailer.py`, which works as follows:
- It decodes `$'...'` and scans the whole text, including with `\n` escapes decoded, so a printf in a
  substitution is also caught.
- It checks each tokenized `-m`/`--message`/`--trailer` value.
- It refuses a value that holds an expansion it cannot see. `$(cat <<...)` is allowed: its body is in
  the text, which is scanned. A `$` inside single quotes is literal and passes.
- It checks `-F -` only against a heredoc, a here-string or a literal `< file`, which it reads. A piped
  or unknown stdin is refused.

Six refused cases and three allowed ones in `no-main.cases` (round 1 block). All of them fail against
the 75c4c78 hook.
Verification: pending (Codex fix verification)
History: fix 1, open → fixed-unverified

## Finding [R-002]

Severity: major
Status: fixed-unverified
Location: `.claude/hooks/no-main.sh:35` (75c4c78)
Requirement/ADR: plan §2, rule 3 (`-F`/`--file` in every spelling)
Failure: `git commit \` + newline + `-aF msg-trailer.txt`, and `git commit --author "Name; Surname <...>"
-F msg-trailer.txt`, exited 0. The statement regex stopped at the newline and at the quoted `;`.
(adversarial finding 2; general review P2 "Preserve continued and quoted arguments".)
Requested outcome: parse continuations and quoted boundaries; refuse what cannot be parsed.
Builder response: fixed. `\`-newline is removed first. Heredoc bodies are cut out, so free text such as
an apostrophe cannot unbalance the parse; the whole-text scan has already checked them. Statements come
from `shlex` with punctuation tokens, so quotes keep `;` inside a value. A command that still fails to
tokenize is refused. The prefilter is now only "the command names commit" (no regex that a quote could
cut short). Two refused cases and two allowed ones.
Verification: pending
History: fix 1, open → fixed-unverified

## Finding [R-003]

Severity: major
Status: fixed-unverified
Location: `.claude/hooks/no-main.sh:33-34` (75c4c78)
Requirement/ADR: plan §2 ("What it allows: prose that mentions the trailer mid-line")
Failure: `git commit -m 'Document --trailer Co-authored-by: syntax'` and `-m 'Document -m Co-Authored-By:
syntax'` exited 2. The option regexes matched text inside the message. (adversarial finding 3)
Requested outcome: find options by tokenizing; check message lines separately.
Builder response: fixed. Options come from tokens. A message is checked only for a line that starts with
the trailer, so both are allowed (two cases).
Verification: pending
History: fix 1, open → fixed-unverified

## Finding [R-004]

Severity: major
Status: fixed-unverified
Location: `.claude/hooks/no-main.sh:117-120` (75c4c78)
Requirement/ADR: plan §2 (refuse what cannot be verified); AGENTS.md (a broken setup refuses)
Failure: a 140 KB command overflowed the 128 KiB cap on one environment string (`CMD=...`). Python
failed to start, `why` was empty and the hook exited 0. (adversarial finding 4)
Requested outcome: block when the scanner fails; pass the command without the environment limit; test it.
Builder response: fixed. The helper reads the hook JSON on stdin, and `no-main.sh` blocks on any nonzero
scanner exit. A non-string command or cwd is refused.
- Generated cases in `run.sh`: a hook copy without the helper (the scanner cannot start), and a 200 KB
  command with and without a trailer.
- `no-main.cases` has a list-valued command case.
Verification: pending
History: fix 1, open → fixed-unverified

## Finding [R-005]

Severity: major
Status: fixed-unverified
Location: `scripts/docs_check.py:177-181` (75c4c78)
Requirement/ADR: plan §1 (STATE.md headings exactly the allowed set)
Failure: an Open threads body of ```` ``` ```` / `~~~` / ```` ``` ```` / `## In flight` / `~~~` /
```` ``` ```` / `~~~` passed `check_state`, though Markdown renders that `## In flight` as a heading.
Toggling on every fence-like line ignored the marker. (adversarial finding 5)
Requested outcome: track the opening fence's character and length; close only on a compatible fence.
Builder response: fixed. `FENCE_RE`; a fence closes only on a run of the same character, at least as
long, with nothing after it. Two tests: the mixed-marker case now fails, naming In flight, and a
four-backtick block holding `~~~` and a three-backtick run stays one block.
Verification: pending
History: fix 1, open → fixed-unverified

## Completion

Five major findings, all fixed-unverified pending Codex fix verification. None rejected or deferred.
Settings: Codex confirmed only `attribution` changed, and that `false` is documented from 2.1.281.
