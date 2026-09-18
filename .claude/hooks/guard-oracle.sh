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
    # Block only when a guarded path is the TARGET of a write: after a redirect, tee, sed -i,
    # rm/mv/cp/touch/truncate, or git rm/mv/checkout/restore. Merely naming the path (ls, cat, grep,
    # a commit message, `2>/dev/null`) is fine. Writes hidden inside scripts are caught by the Edit/Write matcher or by review.
    g='(docs/decisions/|tests/golden/|tests/[A-Za-z0-9_./-]*tolerance)'
    if printf '%s' "$cmd" | grep -Eq "(>>?[[:space:]]*[^[:space:]|;&]*$g|\btee\b([[:space:]]+-[a-z]+)*[[:space:]]+[^[:space:]|;&]*$g|\bsed\b[^|;&]*[[:space:]]-[a-zA-Z]*i[^|;&]*$g|\b(rm|mv|cp|touch|truncate)\b[^|;&]*$g|\bgit\b[^|;&]*\b(rm|mv|checkout|restore)\b[^|;&]*$g)"; then
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


def split_statements(text):
    # Best-effort split on shell control operators outside quotes; not a shell parser (a
    # separator inside an unquoted subshell/command-substitution can still be mis-split, in
    # which case the affected statement fails to tokenize below and falls back to a plain
    # substring check instead of being silently skipped).
    stmts, buf, i, n, quote = [], [], 0, len(text), None
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
            stmts.append("".join(buf)); buf = []; i += 2; continue
        if c in (";", "\n", "|"):
            stmts.append("".join(buf)); buf = []; i += 1; continue
        buf.append(c); i += 1
    if buf:
        stmts.append("".join(buf))
    return stmts


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
FILE_ARG_VERBS = {"tee", "sed", "rm", "mv", "cp", "touch", "truncate"}
GIT_WRITE_SUBS = {"rm", "mv", "checkout", "restore"}


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
    verb, args = toks[0], toks[1:]
    if verb == "git":
        sub = next((t for t in args if t in GIT_WRITE_SUBS), None)
        return [] if sub is None else [t for t in args if t != sub and not is_flag(t)]
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
        rel = rel_to_root(resolve(cur, t))
        if rel is not None and pattern.search(rel):
            return True
    return False


def evaluate():
    cur = start_cwd
    for raw in split_statements(cmd):
        stmt = raw.strip()
        if not stmt:
            continue
        try:
            toks = shlex.split(stmt, posix=True)
        except ValueError:
            if pattern.search(stmt):
                return True
            continue
        if toks and toks[0] == "cd":
            target = toks[1] if len(toks) > 1 else None
            if target and target != "-" and LITERAL_TARGET_RE.match(target):
                cur = resolve(cur, target)
            # else: untracked cd target (variable/glob/`-`/empty) -- leave cur unchanged.
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
