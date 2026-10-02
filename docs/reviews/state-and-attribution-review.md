# Review: STATE.md that survives the merge, and commits without a co-author trailer

Spec/plan: [docs/plans/state-and-attribution.md](../plans/state-and-attribution.md)
Builder: Claude (claude-opus-5-5)
Reviewer: Codex companion 1.0.6, fresh context, general `review` (thread
`01a0fa9e-a7a3-7b32-9e17-073c82d805bd`) and `adversarial-review` (thread
`01a0fa9e-a771-7f31-9f9c-f32f2b40e169`, verdict needs-attention)
Scope: branch diff `origin/main` (8e97040) to 75c4c78, committed, clean tree; every path in that diff
Validation: the full gate on d8e947a, exit 0, `check: OK`
([summary](evidence/d8e947a-gate-summary.log)); 75c4c78 adds only that summary log
Round: fix 2 (correction rounds 2/2). Fix 1 was verified by Codex `adversarial-review` (thread
`01a0fab9-9818-7090-a08d-e07e0d4a2636`, base 75c4c78, head a5f7ac6, verdict needs-attention): R-002 and
R-003 verified; R-004 and R-005 verified on their recorded cases; R-001 still open; three new findings,
N-001 to N-003.

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
Fix 2: Codex found that `git commit -F - <<EOF` with `$MSG` in the body still passed, because any heredoc
counted as verified. A heredoc whose delimiter is unquoted (no `'`, `"` or `\`) now has its body checked
for an expansion; escaped `\$` and `` \` `` are literal. One with an expansion is refused, and the refusal
says to quote the delimiter. Two refused cases (`-F - <<EOF` and `-m "$(cat <<EOF ...)"`) and two allowed
ones (an escaped `\$HOME`, and a quoted delimiter with a literal `$HOME`).
Verification: fix 1 not verified (thread `01a0fab9-…`: the unquoted heredoc above); fix 2 pending
History: fix 1, open → fixed-unverified; verification 1, still open; fix 2 → fixed-unverified

## Finding [R-002]

Severity: major
Status: verified
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
Verification: Codex, thread `01a0fab9-9818-7090-a08d-e07e0d4a2636`, head a5f7ac6: fixed
History: fix 1, open → fixed-unverified; verification 1 → verified

## Finding [R-003]

Severity: major
Status: verified
Location: `.claude/hooks/no-main.sh:33-34` (75c4c78)
Requirement/ADR: plan §2 ("What it allows: prose that mentions the trailer mid-line")
Failure: `git commit -m 'Document --trailer Co-authored-by: syntax'` and `-m 'Document -m Co-Authored-By:
syntax'` exited 2. The option regexes matched text inside the message. (adversarial finding 3)
Requested outcome: find options by tokenizing; check message lines separately.
Builder response: fixed. Options come from tokens. A message is checked only for a line that starts with
the trailer, so both are allowed (two cases).
Verification: Codex, thread `01a0fab9-9818-7090-a08d-e07e0d4a2636`, head a5f7ac6: fixed
History: fix 1, open → fixed-unverified; verification 1 → verified

## Finding [R-004]

Severity: major
Status: verified
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
Verification: Codex, thread `01a0fab9-9818-7090-a08d-e07e0d4a2636`, head a5f7ac6: fixed for the
recorded size-limit and scanner-failure cases
History: fix 1, open → fixed-unverified; verification 1 → verified

## Finding [R-005]

Severity: major
Status: verified
Location: `scripts/docs_check.py:177-181` (75c4c78)
Requirement/ADR: plan §1 (STATE.md headings exactly the allowed set)
Failure: an Open threads body of ```` ``` ```` / `~~~` / ```` ``` ```` / `## In flight` / `~~~` /
```` ``` ```` / `~~~` passed `check_state`, though Markdown renders that `## In flight` as a heading.
Toggling on every fence-like line ignored the marker. (adversarial finding 5)
Requested outcome: track the opening fence's character and length; close only on a compatible fence.
Builder response: fixed. `FENCE_RE`; a fence closes only on a run of the same character, at least as
long, with nothing after it. Two tests: the mixed-marker case now fails, naming In flight, and a
four-backtick block holding `~~~` and a three-backtick run stays one block.
Verification: Codex, thread `01a0fab9-9818-7090-a08d-e07e0d4a2636`, head a5f7ac6: fixed for the recorded
mixed-marker case (its regression in the opener rule is N-003)
History: fix 1, open → fixed-unverified; verification 1 → verified

## Finding [N-001]

Severity: major
Status: fixed-unverified
Location: `.claude/hooks/commit_trailer.py:280-289` (a5f7ac6)
Requirement/ADR: plan §2, rule 3
Failure: `git diff --cached` + newline + `git commit -F <trailer file>` exited 0; 75c4c78's hook refused it.
`shlex` treated the newline as whitespace, and `why()` stopped at the statement's first `git`. A
`-m "$MSG"` after `git status` passed the same way.
Requested outcome: keep unquoted newlines as command boundaries; inspect every git invocation.
Builder response: fixed. The tokenizer makes an unquoted newline a separator, like `;`.
`why()` checks every `git` token in a statement rather than breaking after the first, because a comment
consumes the newline after it. Two refused cases and one allowed.
Verification: pending
History: fix 2, open → fixed-unverified

## Finding [N-002]

Severity: major
Status: fixed-unverified
Location: `.claude/hooks/commit_trailer.py:140-144` (a5f7ac6)
Requirement/ADR: plan §2 (ordinary trailer-free `-m` commits pass)
Failure: `git commit -m 'Document `check_state` behavior'` exited 2: tokenizing lost the single quotes,
and the backtick counted as a substitution. 75c4c78's hook allowed it.
Requested outcome: keep single-quoted and escaped backticks literal; still refuse real substitutions.
Builder response: fixed. `protect_literals` (formerly `protect_literal_dollars`) also marks backticks
inside single quotes or after a backslash. Two allowed cases: a single-quoted backtick and an escaped
backtick in double quotes.
Verification: pending
History: fix 2, open → fixed-unverified

## Finding [N-003]

Severity: major
Status: fixed-unverified
Location: `scripts/docs_check.py:182-185` (a5f7ac6)
Requirement/ADR: plan §1 (STATE.md headings exactly the allowed set)
Failure: an Open threads body of ```` ````text`bad ```` / `## In flight` / ```` ``` ```` / ```` ```` ````
passed `check_state`. CommonMark renders `In flight` as a heading, because a backtick fence's info string
cannot contain a backtick; 75c4c78's check caught it.
Requested outcome: apply CommonMark's opener rule; add the regression.
Builder response: fixed. A backtick run whose info string holds a backtick is not a fence. One test
(`test_state_invalid_backtick_fence_does_not_hide_a_heading`).
Verification: pending
History: fix 2, open → fixed-unverified

## Completion

Eight major findings. R-002 to R-005 are verified. R-001 (fix 2) and N-001 to N-003 are fixed-unverified,
pending Codex fix verification of round 2. None rejected or deferred. This is the last autonomous
correction round (AGENTS.md); a finding that survives it goes to Joe.
Settings: Codex confirmed only `attribution` changed, and that `false` is documented from 2.1.281.
