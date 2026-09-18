#!/usr/bin/env bash
# PreToolUse hook (matchers: Edit|Write|MultiEdit|NotebookEdit and Bash).
# The oracle is human-owned (docs/process/workflow.md §Verification): docs/decisions/**, tests/golden/**,
# and tolerance files change only when Joe says so. Exit 2 = block; the message on stderr goes to Claude.
# Override: ASTRA_TOUCH_GOLDENS=1 in the session's environment (set on the `claude` launch line -- an
# export inside a running session never reaches the hook), or the gitignored marker .claude/touch-goldens
# touched by Joe while the session runs (the SessionStart disarm-goldens.sh hook deletes it).
# The marker is accepted in EITHER the session's own root or the main checkout: a worktree session's
# CLAUDE_PROJECT_DIR is the worktree, which has its own .claude/, so a marker touched in ~/repos/ASTRA
# used to arm nothing at all -- and every golden-touching PR runs in a worktree (docs/lessons.md).
set -u
input=$(cat)
root="${CLAUDE_PROJECT_DIR:-$(pwd)}"
# --git-common-dir is the main checkout's .git: relative (".git") from the main checkout, absolute from a
# worktree. Anything that is not a git tree (the hook tests' temporary roots) falls back to $root itself.
common=$(git --no-optional-locks -C "$root" rev-parse --git-common-dir 2>/dev/null)
case "$common" in
  "") main_root="$root" ;;
  /*) main_root=$(dirname "$common") ;;
  *)  main_root=$(cd "$root/$(dirname "$common")" 2>/dev/null && pwd || echo "$root") ;;
esac
if [ "${ASTRA_TOUCH_GOLDENS:-0}" = "1" ] || [ -e "$root/.claude/touch-goldens" ] \
   || [ -e "$main_root/.claude/touch-goldens" ]; then exit 0; fi

tool=$(printf '%s' "$input" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("tool_name",""))' 2>/dev/null)
# The third alternative used to be a bare `tolerance`, unanchored, matching anywhere in a path
# or command -- it blocked a prose plan doc (docs/plans/nesc-tolerances.md), a hypothetical
# source utility (core/util/tolerance.hpp), and a log redirect (build/tolerance.log), none of
# them a human-owned oracle. A real tolerance file lives under tests/ (e.g. the planned
# tests/physics/nesc/tolerances.yaml); anchor to that so the false positives above pass while
# every real protected asset (inventoried: tests/golden/**, docs/decisions/**, and any
# tests/**/*tolerance* file) stays blocked. See guard-oracle.cases for both directions pinned.
guarded='(^|/)(docs/decisions/|tests/golden/|tests/[A-Za-z0-9_./-]*tolerance)'

block() { echo "BLOCKED by guard-oracle: $1 is human-owned (tests/golden/**, tolerance files, docs/decisions/**). Only a session Joe armed may change it (touch .claude/touch-goldens in this tree or in the main checkout, or launch with ASTRA_TOUCH_GOLDENS=1); ADRs are superseded, never edited." >&2; exit 2; }

case "$tool" in
  Edit|Write|MultiEdit|NotebookEdit)
    # Normalized so a path built with a ".." traversal segment (e.g. "tests/other/../golden/x")
    # cannot dodge the substring check below by never spelling a guarded prefix contiguously.
    path=$(printf '%s' "$input" | python3 -c 'import json,os,sys; t=json.load(sys.stdin).get("tool_input",{}); p=t.get("file_path") or t.get("notebook_path") or ""; print(os.path.normpath(p) if p else "")' 2>/dev/null)
    [ -z "$path" ] && exit 0
    rel="${path#"$root"/}"
    if printf '%s' "$rel" | grep -Eq "$guarded"; then block "$rel"; fi
    ;;
  Bash)
    cmd=$(printf '%s' "$input" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("tool_input",{}).get("command",""))' 2>/dev/null)
    [ -z "$cmd" ] && exit 0
    # The harness's own "cwd" field is the directory this command actually runs in (persisted
    # across Bash calls in a session, same field no-main.sh/stop-check.sh already read).
    cwd=$(printf '%s' "$input" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("cwd",""))' 2>/dev/null)
    # Block only when a guarded path is the TARGET of a write: after a redirect, tee,
    # rm/mv/cp/touch/truncate, or git rm/mv/checkout/restore. Merely naming the path (ls, cat, grep,
    # a commit message, `2>/dev/null`) is fine. Writes hidden inside scripts are caught by the Edit/Write matcher or by review.
    # `sed` is deliberately NOT matched here (R-008/R-012): a fast substring alternative
    # (`\bsed\b...-[a-zA-Z]*i...`) used to false-block a read-only attached `-e` script whose
    # text happened to contain the letter `i` next to a literally-spelled guarded path (e.g.
    # `sed -eiprobe tests/golden/...`). The Python stage below has full, tested sed
    # classification and runs unconditionally regardless of this fast pre-filter -- but removing
    # sed here first (Round 5) also silently dropped this coarse filter's substring-only
    # matching, which did not care about `env`/`command`/`NAME=VALUE` prefixes or whether a
    # literally-spelled absolute path was under this session's own root; Python's
    # `resolve_verb()` and `statement_targets_guarded()`'s outside-root fallback now cover both
    # of those, so no real coverage is lost, but this took two authorized fix rounds to reach.
    g='(docs/decisions/|tests/golden/|tests/[A-Za-z0-9_./-]*tolerance)'
    if printf '%s' "$cmd" | grep -Eq "(>>?[[:space:]]*[^[:space:]|;&]*$g|\btee\b([[:space:]]+-[a-z]+)*[[:space:]]+[^[:space:]|;&]*$g|\b(rm|mv|cp|touch|truncate)\b[^|;&]*$g|\bgit\b[^|;&]*\b(rm|mv|checkout|restore)\b[^|;&]*$g)"; then
      block "a write target in this command"
    fi
    # The check above only catches a guarded path literally spelled out next to its write. A
    # command can reach the same file after changing directory toward it -- inline (`cd
    # tests/golden && printf x > example.hpp`) or because the session's Bash cwd already
    # persisted there from an earlier call (this hook's own "cwd" input). Resolve every write's
    # actual target against the command's starting cwd plus any literal `cd` inside the command
    # text, then recheck against the same guarded pattern. This remains a shell-pattern guard,
    # not a shell interpreter: `cd` to a variable, glob, `~`, or `-` (previous directory) is not
    # followed, and a write after one of those is checked against the un-followed directory
    # instead of the real one -- a known, narrow gap, not a silent one. See guard-oracle.cases.
    verdict=$(GUARD_CMD="$cmd" GUARD_CWD="${cwd:-$root}" GUARD_ROOT="$root" GUARD_PATTERN="$g" python3 - <<'PYEOF' 2>/dev/null
import os, re, shlex

cmd = os.environ.get("GUARD_CMD", "")
root = os.environ.get("GUARD_ROOT", "")
start_cwd = os.environ.get("GUARD_CWD", "") or root
pattern = re.compile(os.environ.get("GUARD_PATTERN", ""))


# A pipeline-continuation newline may be preceded by nothing but whitespace, optionally
# followed by a shell comment (`#` at the start of a word, running to end of line) -- a `#`
# elsewhere (stuck to a preceding non-whitespace character) is ordinary text, not a comment.
PIPE_CONTINUATION_RE = re.compile(r"^\s*(#.*)?$")


def split_statements(text):
    # Best-effort split on shell control operators outside quotes; not a shell parser (a
    # separator inside an unquoted subshell/command-substitution can still be mis-split, in
    # which case the affected statement fails to tokenize below and falls back to a plain
    # substring check instead of being silently skipped). Returns (statement, in_pipeline)
    # pairs: in_pipeline is True for a statement adjacent to a `|` on either side, since bash
    # runs every stage of a pipeline (including a lone `cd`) in a subshell by default -- that
    # `cd` does not persist to any statement after the pipeline, unlike one joined by
    # `;`/`&&`/`||`/newline.
    stmts, buf, i, n, quote = [], [], 0, len(text), None
    seps = []

    def flush(sep):
        stmts.append("".join(buf))
        seps.append(sep)
        buf.clear()

    while i < n:
        c = text[i]
        if quote:
            buf.append(c)
            if c == quote:
                quote = None
            i += 1
            continue
        if c in ("'", '"'):
            quote = c
            buf.append(c)
            i += 1
            continue
        if c == "\\" and i + 1 < n:
            buf.append(c)
            buf.append(text[i + 1])
            i += 2
            continue
        if text[i:i + 2] in ("&&", "||"):
            flush(";"); i += 2; continue
        if c == "\n":
            # A newline immediately after a `|` (nothing but whitespace, or a whitespace-only
            # `#comment` -- itself outside quotes here, since the quote branch above already
            # consumed anything quoted -- since) is pipeline line-continuation, not a statement
            # terminator: bash reads `cmd1 |\ncmd2` and `cmd1 | # note\ncmd2` both as one
            # two-stage pipeline, never as `cmd1 | ; cmd2`. Swallowing it keeps the next real
            # statement correctly tracked as pipeline-adjacent instead of inserting a synthetic
            # `;`-separated empty statement that would sever the chain.
            pending = "".join(buf)
            if PIPE_CONTINUATION_RE.match(pending) and seps and seps[-1] == "|":
                i += 1; continue
            flush(";"); i += 1; continue
        if c == ";":
            flush(";"); i += 1; continue
        if c == "|":
            flush("|"); i += 1; continue
        buf.append(c); i += 1
    if buf or not stmts:
        stmts.append("".join(buf))
    result = []
    for idx, stmt in enumerate(stmts):
        before_pipe = idx > 0 and seps[idx - 1] == "|"
        after_pipe = idx < len(seps) and seps[idx] == "|"
        result.append((stmt, before_pipe or after_pipe))
    return result


def space_out_redirects(s):
    # So `cmd>file` and `cmd > file` tokenize the same way; quote-aware so a `>` inside a
    # quoted argument is left alone.
    out, i, n, quote = [], 0, len(s), None
    while i < n:
        c = s[i]
        if quote:
            out.append(c)
            if c == quote:
                quote = None
            i += 1
            continue
        if c in ("'", '"'):
            quote = c
            out.append(c)
            i += 1
            continue
        if c == ">":
            if i + 1 < n and s[i + 1] == ">":
                out.append(" >> "); i += 2
            else:
                out.append(" > "); i += 1
            continue
        out.append(c); i += 1
    return "".join(out)


# Only a plain literal is followed; a variable/glob/tilde/empty target leaves `cur` unchanged.
LITERAL_TARGET_RE = re.compile(r"^[A-Za-z0-9_./-]+$")
# Commands whose own non-flag arguments are inherently file paths, not just redirect targets.
# `sed` is handled separately below (only its in-place form writes; see `sed_is_inplace`).
FILE_ARG_VERBS = {"tee", "rm", "mv", "cp", "touch", "truncate"}
GIT_WRITE_SUBS = {"rm", "mv", "checkout", "restore"}
# bash's builtin `cd [-L|[-P [-e]] [-@]] [--] [dir]`: recognized options that precede the
# directory operand, plus the `--` separator itself. bash accepts these clustered in one
# token (`-Pe` same as `-P -e`), same as it accepts them separately.
CD_OPT_CLUSTER_RE = re.compile(r"^-[LPe@]+$")

ENV_ASSIGN_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*=")


def resolve_verb(toks):
    """The real command name `command_arg_targets` should classify by, seeing through a
    leading `env`/`command`, one or more `NAME=VALUE` environment-variable-assignment prefixes
    (with or without a preceding `env`/`command`, both valid bash), and a path-qualified name
    resolved to its basename -- not a shell interpreter, just enough that these ordinary
    prefixes cannot dodge a write-capable command's own classification (`env sed -i ...`,
    `LC_ALL=C sed -i ...`, `command sed -i ...`, `/usr/bin/sed -i ...` are all still `sed`)."""
    i = 0
    if i < len(toks) and toks[i] in ("env", "command"):
        i += 1
        while i < len(toks) and toks[i].startswith("-"):
            i += 1
    while i < len(toks) and ENV_ASSIGN_RE.match(toks[i]):
        i += 1
    if i >= len(toks):
        return None, []
    return os.path.basename(toks[i]), toks[i + 1:]

# sed's own short-option classes (GNU sed): a no-arg flag keeps scanning the same clustered
# token; a value-taking flag consumes the rest of the token (if any) or else the next
# separate token as its argument, and that argument is never itself scanned for flags.
SED_NOARG_SHORT = "nsuzErq"
SED_VALUE_SHORT = "efl"


def sed_is_inplace(args):
    """True when sed's own option parsing would select in-place-edit mode: `-i`, a short
    cluster containing it (`-ni`), GNU's attached optional suffix (`-i.bak`), or the long
    form (`--in-place`, `--in-place=SUFFIX`). Walks each token's option cluster character by
    character so a value-taking flag's attached argument (`-eiprobe` is `-e` with the script
    "iprobe", not a clustered `-i`) is never mistaken for a flag."""
    i = 0
    while i < len(args):
        tok = args[i]
        if tok == "--":
            return False
        if tok == "--in-place" or tok.startswith("--in-place="):
            return True
        if tok.startswith("--") or not tok.startswith("-") or tok == "-":
            i += 1
            continue
        j = 1
        while j < len(tok):
            c = tok[j]
            if c == "i":
                return True
            if c in SED_VALUE_SHORT:
                if j == len(tok) - 1:
                    i += 1  # no attached value in this token -- the next token is it
                break
            j += 1
        i += 1
    return False


# A bare `w`/`W` sed command writes to a named file even with no `-i` -- heuristic, not a sed
# parser: a `w`/`W` not preceded by a letter (a digit/`$`/`,`/`;`/`/`/whitespace/start is a
# legitimate address or command boundary in sed's own grammar -- `1w file`, `/re/w file`; a
# preceding *letter* means it is part of a longer word, e.g. "flow"/"raw"/"warning"), followed
# by REQUIRED whitespace then another non-space token -- sed's own `w` command and the `s///w`
# flag below both need visible separation before the filename, so "warning" (no space after
# its "w") does not match even though its "w" is not letter-preceded (R-013: this used to be
# `\s*`, matching "warning" too and false-blocking a plain read-only address/regex).
SED_BARE_W_CMD_RE = re.compile(r"(?<![A-Za-z])[wW]\s+\S")
# An `s<delim>pattern<delim>replacement<delim>flags` substitution whose flags include `w`/`W`
# (e.g. `s/x/y/gw file`, flags cluster "gw" -- the `w` here is letter-preceded, by design, so
# the bare-command regex above deliberately does not match it): matched separately, anchored to
# sed's actual delimiter (`/` is conventional but sed accepts any character), so a `w` inside
# ordinary pattern/replacement text is never mistaken for a flag. The `s` itself is excluded
# only when letter-preceded, same reasoning as `SED_BARE_W_CMD_RE`'s `w` -- a digit/comma/`$`/
# semicolon/start is a legitimate sed address before the command letter (`1s/x/y/gw file`).
SED_SUBST_W_FLAG_RE = re.compile(
    r"(?<![A-Za-z])s(.)(?:\\.|(?!\1).)*?\1(?:\\.|(?!\1).)*?\1[a-zA-Z0-9]*[wW]\s+\S"
)


def sed_script_has_write_command(args):
    # Scan sed's own option *values* (sed_arg_targets: an attached `-e`'s script text, a
    # separate `-e`'s next token, a positional script/filename, ...) rather than the raw
    # argument tokens -- otherwise an attached option's flag prefix sits directly against the
    # script text with nothing to anchor a lookbehind on (`-es/x/y/gw file` has `s` directly
    # after `e`, indistinguishable by lookbehind from a real identifier); isolating the value
    # first means a script's own `s`/`w` always starts scanning from a clean position.
    return any(
        SED_BARE_W_CMD_RE.search(v) or SED_SUBST_W_FLAG_RE.search(v)
        for v in sed_arg_targets(args)
    )


SED_LONG_VALUE_OPTS = ("--expression=", "--file=")


def sed_arg_targets(args):
    """Every argument sed's own option parsing would treat as a value -- a positional
    script/filename, a value-taking short option's attached-or-separate argument, or a long
    `--expression=`/`--file=` value -- so a write command recognized inside an attached or
    long-option value is not silently dropped by keeping only "non-flag" tokens (previously:
    `--expression='w probe.hpp'` was classified write-capable but the whole token was then
    discarded as a flag, so its actual target was never checked against the guarded pattern)."""
    targets, i = [], 0
    while i < len(args):
        tok = args[i]
        if tok == "--":
            targets.extend(args[i + 1:])
            break
        long_val = next((tok[len(opt):] for opt in SED_LONG_VALUE_OPTS if tok.startswith(opt)), None)
        if long_val is not None:
            targets.append(long_val)
            i += 1
            continue
        if tok.startswith("--"):
            i += 1
            continue
        if not tok.startswith("-") or tok == "-":
            targets.append(tok)
            i += 1
            continue
        j = 1
        while j < len(tok):
            c = tok[j]
            if c == "i":
                break  # an optional attached suffix, not a target
            if c in SED_VALUE_SHORT:
                if j < len(tok) - 1:
                    targets.append(tok[j + 1:])
                elif i + 1 < len(args):
                    targets.append(args[i + 1])
                    i += 1
                break
            j += 1
        i += 1
    return targets


def resolve(base, target):
    p = target if target.startswith("/") else os.path.join(base, target)
    return os.path.normpath(p)


def rel_to_root(path):
    root_n = os.path.normpath(root)
    path_n = os.path.normpath(path)
    if path_n == root_n:
        return ""
    prefix = root_n + os.sep
    return path_n[len(prefix):] if path_n.startswith(prefix) else None


def is_flag(tok):
    return tok.startswith("-") and tok != "-"


def redirect_targets(stmt):
    """Tokens right after a `>`/`>>` -- the only targets a bare redirect can name, whatever
    command precedes it (printf/echo/python/ctest/...)."""
    try:
        toks = shlex.split(space_out_redirects(stmt), posix=True)
    except ValueError:
        return None
    targets, i = [], 0
    while i < len(toks):
        if toks[i] in (">", ">>") and i + 1 < len(toks):
            targets.append(toks[i + 1]); i += 2; continue
        i += 1
    return targets


def command_arg_targets(stmt):
    """Non-flag arguments of a command whose arguments are themselves file paths."""
    try:
        toks = shlex.split(stmt, posix=True)
    except ValueError:
        return None
    if not toks:
        return []
    verb, args = resolve_verb(toks)
    if verb is None:
        return []
    if verb == "git":
        sub = next((t for t in args if t in GIT_WRITE_SUBS), None)
        return [] if sub is None else [t for t in args if t != sub and not is_flag(t)]
    if verb == "sed":
        # sed's non-flag arguments are a script/expression plus the file(s) it reads -- a
        # write target only when an in-place flag selects that mode, or the script itself
        # contains a `w`/`W` write command; a plain `sed 'script' file` (or `sed -n ...`)
        # with neither only reads and prints to stdout.
        if not sed_is_inplace(args) and not sed_script_has_write_command(args):
            return []
        return sed_arg_targets(args)
    if verb in FILE_ARG_VERBS:
        return [t for t in args if not is_flag(t)]
    return []


def statement_targets_guarded(stmt, cur):
    r_targets = redirect_targets(stmt)
    c_targets = command_arg_targets(stmt)
    if r_targets is None or c_targets is None:
        # Unbalanced quoting or similar defeats tokenizing this statement: fall back to a
        # plain substring check of its raw text rather than silently passing it through.
        return pattern.search(stmt) is not None
    for t in r_targets + c_targets:
        resolved = resolve(cur, t)
        rel = rel_to_root(resolved)
        # A target outside this session's own root (a worktree session naming the main
        # checkout's absolute path, or another worktree, by its own absolute path -- not this
        # session's relative view of it) has no root-relative form to check; fall back to
        # matching the guarded pattern against the resolved path itself, exactly as the file's
        # own coarse Bash-text pre-filter always did against the raw command text regardless of
        # root. A resolved path unrelated to this repository that coincidentally contains a
        # guarded-looking substring is a rare, safe-direction false block, not a bypass.
        candidate = rel if rel is not None else resolved
        if pattern.search(candidate):
            return True
    return False


def evaluate():
    cur = start_cwd
    for raw, in_pipeline in split_statements(cmd):
        stmt = raw.strip()
        if not stmt:
            continue
        # Peel one layer of `(...)` subshell wrapping when it wraps the *entire* statement --
        # `(sed -i ... tests/golden/x)` -- so the command inside is classified the same as if
        # it were not wrapped. Narrow, not full shell parsing: only helps a subshell containing
        # a single simple statement with no `;`/`&&`/`||`/`|` of its own (those already split
        # this statement apart above, before any subshell-awareness), a known remaining gap.
        if stmt.startswith("(") and stmt.endswith(")"):
            stmt = stmt[1:-1].strip()
            if not stmt:
                continue
        try:
            toks = shlex.split(stmt, posix=True)
        except ValueError:
            if pattern.search(stmt):
                return True
            continue
        if toks and toks[0] == "cd":
            # A `cd` that is part of a pipeline (`cd dir | cmd`, on either side of any `|`)
            # runs in a subshell by default and never changes the invoking shell's directory
            # at all -- tracking it here would let a `cd` timed around a pipeline stage
            # silently reset `cur` for everything after it, which is worse than not tracking
            # it (see guard-oracle.cases for the `cd tests/golden; cd ../.. | cat; printf ...`
            # shape this closes).
            if in_pipeline:
                continue
            # Skip recognized options -- individually or clustered in one token, as bash's
            # own `cd` accepts (`-Pe` same as `-P -e`) -- and an optional `--` separator
            # before reading the directory operand -- previously the raw second token was
            # taken as the target unconditionally, so `cd -- tests/golden` resolved `cur` to a
            # bogus "<cwd>/--" directory instead of following `tests/golden` at all.
            args, i, saw_sep = toks[1:], 0, False
            while i < len(args) and not saw_sep and CD_OPT_CLUSTER_RE.match(args[i]):
                i += 1
            if i < len(args) and args[i] == "--":
                saw_sep = True
                i += 1
            target = args[i] if i < len(args) else None
            if (
                target
                and target != "-"
                and (saw_sep or not target.startswith("-"))
                and LITERAL_TARGET_RE.match(target)
            ):
                cur = resolve(cur, target)
            # else: untracked cd target (variable/glob/`-`/an unrecognized option/empty) --
            # leave cur unchanged.
            continue
        if statement_targets_guarded(stmt, cur):
            return True
    return False


print("BLOCK" if evaluate() else "OK")
PYEOF
)
    [ "$verdict" = "BLOCK" ] && block "a write target reached via this command's working directory"
    ;;
esac
exit 0
