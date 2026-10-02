#!/usr/bin/env bash
# PreToolUse hook (matcher: Bash). Blocks any git command that would write to main.
# Reads the tool call as JSON on stdin; exit 2 = block, message on stderr goes to Claude.
set -u
input=$(cat)
cmd=$(printf '%s' "$input" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("tool_input",{}).get("command",""))' 2>/dev/null)
cwd=$(printf '%s' "$input" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("cwd",""))' 2>/dev/null)
[ -z "$cmd" ] && exit 0
case "$cmd" in *git*) ;; *) exit 0 ;; esac

# 1. Never push to main, from any branch.
if printf '%s' "$cmd" | grep -Eq 'git[^|;&]*push[^|;&]*(\bmain\b|:main\b)'; then
  echo "BLOCKED: pushing to main is not allowed. Push your task branch and open a PR." >&2; exit 2
fi
# 2. Never commit / merge / rebase / amend while checked out on main.
if printf '%s' "$cmd" | grep -Eq 'git[^|;&]*\b(commit|merge|rebase|cherry-pick|reset)\b'; then
  branch=$(git -C "${cwd:-.}" --no-optional-locks rev-parse --abbrev-ref HEAD 2>/dev/null || echo "")
  if [ "$branch" = "main" ]; then
    echo "BLOCKED: you are on main. Work in a worktree branch (claude --worktree <name>) and open a PR." >&2; exit 2
  fi
fi
# 3. No co-author trailer in a commit message. `"attribution": false` in .claude/settings.json is the setting;
#    this is the check (docs/lessons.md, 2026-10-01). The message is read from the command text (-m, a
#    heredoc), from an -F/--file file (relative to `git -C <dir>` when given, else the hook's cwd), and from
#    --trailer. An -F file that cannot be named or read is refused as unverifiable. Gaps, documented in
#    tests/no-main.cases: -C/-c/--amend --no-edit reuse a message; an inline `cd` before the commit is not followed.
if printf '%s' "$cmd" | grep -Eq 'git[^|;&]*\bcommit\b'; then
  why=$(CMD="$cmd" CWD="${cwd:-.}" python3 - <<'PY'
import os, re, shlex

cmd, cwd = os.environ["CMD"], os.environ["CWD"]
LINE = re.compile(r"(?im)^[\s\"']*co-authored-by\s*:")
MSG_OPT = re.compile(r"(?i)(?:\s-[A-Za-z]*m|\s--message)(?:=|\s*)[\"']?\s*co-authored-by\s*:")
TRAILER_OPT = re.compile(r"(?i)--trailer(?:=|\s+)[\"']?\s*co-authored-by\b")
STMT = re.compile(r"\bgit\b([^|;&\n]*?)\bcommit\b([^|;&\n]*)")
# commit's long options that take a separate value, and its short options whose value follows the letter
LONG_VALUE = {"--message", "--template", "--reuse-message", "--reedit-message", "--author", "--date",
              "--cleanup", "--fixup", "--squash", "--trailer", "--pathspec-from-file"}
SHORT_VALUE = set("mCct")
SHORT_ATTACHED = set("uS")


def tokens(text):
    try:
        return shlex.split(text)
    except ValueError:  # an unbalanced quote, e.g. -m "$(cat <<'EOF' cut at the newline
        return [t.strip("\"'") for t in re.findall(r"\"[^\"]*\"|'[^']*'|\S+", text)]


def message_files(args):
    toks, out, i = tokens(args), [], 0
    while i < len(toks):
        t = toks[i]
        if t == "--":
            break
        if t.startswith("--file="):
            out.append(t[len("--file="):])
        elif t == "--file":
            out.append(toks[i + 1] if i + 1 < len(toks) else "")
            i += 1
        elif t.startswith("--"):
            i += t in LONG_VALUE
        elif t.startswith("-") and len(t) > 1:
            for j, c in enumerate(t[1:], 1):
                if c == "F" or c in SHORT_VALUE:
                    value = t[j + 1:]
                    if not value:
                        i += 1
                        value = toks[i] if i < len(toks) else ""
                    if c == "F":
                        out.append(value)
                    break
                if c in SHORT_ATTACHED:
                    break
        i += 1
    return out


def base_dir(gitopts):
    base, toks = cwd, tokens(gitopts)
    for k, t in enumerate(toks):
        if t == "-C":
            d = toks[k + 1] if k + 1 < len(toks) else ""
            if not d or re.search(r"[$`]", d):
                return None
            base = os.path.join(base, os.path.expanduser(d))
    return base


def why():
    if LINE.search(cmd) or MSG_OPT.search(cmd) or TRAILER_OPT.search(cmd):
        return "the commit message carries a Co-Authored-By trailer."
    for m in STMT.finditer(cmd):
        base = base_dir(m.group(1))
        for f in message_files(m.group(2)):
            if f == "-":
                continue  # stdin: a heredoc in the command text, already scanned above
            if not f or re.search(r"[$`]", f):
                return f"cannot verify the -F message file '{f}' (no literal path)."
            path = os.path.expanduser(f)
            if not os.path.isabs(path):
                if base is None:
                    return f"cannot resolve -F '{f}' against a git -C directory that is not a literal path."
                path = os.path.join(base, path)
            try:
                with open(path, encoding="utf-8", errors="replace") as fh:
                    text = fh.read()
            except OSError:
                return f"cannot read the -F message file '{path}' to check it; write it in an earlier command."
            if LINE.search(text):
                return f"the -F message file '{path}' carries a Co-Authored-By trailer."
    return ""


print(why())
PY
)
  if [ -n "$why" ]; then
    echo "BLOCKED: $why ASTRA commits carry no co-author trailer (.claude/settings.json attribution: false). Remove it." >&2; exit 2
  fi
fi
exit 0
