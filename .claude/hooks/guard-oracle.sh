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
guarded='(^|/)(docs/decisions/|tests/golden/)|tolerance'

block() { echo "BLOCKED by guard-oracle: $1 is human-owned (tests/golden/**, tolerance files, docs/decisions/**). Only a session Joe armed may change it (touch .claude/touch-goldens in this tree or in the main checkout, or launch with ASTRA_TOUCH_GOLDENS=1); ADRs are superseded, never edited." >&2; exit 2; }

case "$tool" in
  Edit|Write|MultiEdit|NotebookEdit)
    path=$(printf '%s' "$input" | python3 -c 'import json,sys; t=json.load(sys.stdin).get("tool_input",{}); print(t.get("file_path") or t.get("notebook_path") or "")' 2>/dev/null)
    [ -z "$path" ] && exit 0
    rel="${path#"$root"/}"
    if printf '%s' "$rel" | grep -Eq "$guarded"; then block "$rel"; fi
    ;;
  Bash)
    cmd=$(printf '%s' "$input" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("tool_input",{}).get("command",""))' 2>/dev/null)
    [ -z "$cmd" ] && exit 0
    # Block only when a guarded path is the TARGET of a write: after a redirect, tee, sed -i,
    # rm/mv/cp/touch/truncate, or git rm/mv/checkout/restore. Merely naming the path (ls, cat, grep,
    # a commit message, `2>/dev/null`) is fine. Writes hidden inside scripts are caught by the Edit/Write matcher or by review.
    g='(docs/decisions/|tests/golden/|tolerance)'
    if printf '%s' "$cmd" | grep -Eq "(>>?[[:space:]]*[^[:space:]|;&]*$g|\btee\b([[:space:]]+-[a-z]+)*[[:space:]]+[^[:space:]|;&]*$g|\bsed\b[^|;&]*[[:space:]]-[a-zA-Z]*i[^|;&]*$g|\b(rm|mv|cp|touch|truncate)\b[^|;&]*$g|\bgit\b[^|;&]*\b(rm|mv|checkout|restore)\b[^|;&]*$g)"; then
      block "a write target in this command"
    fi
    ;;
esac
exit 0
