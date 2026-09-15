#!/usr/bin/env bash
# PreToolUse hook (matchers: Edit|Write|MultiEdit|NotebookEdit and Bash).
# The oracle is human-owned (docs/process/workflow.md §Verification): docs/decisions/**, tests/golden/**,
# and tolerance files change only when Joe says so. Exit 2 = block; the message on stderr goes to Claude.
# Override: ASTRA_TOUCH_GOLDENS=1 in the session's environment, or the gitignored marker
# .claude/touch-goldens touched by Joe while the session runs (the SessionStart hook deletes it).
set -u
input=$(cat)
root="${CLAUDE_PROJECT_DIR:-$(pwd)}"
if [ "${ASTRA_TOUCH_GOLDENS:-0}" = "1" ] || [ -e "$root/.claude/touch-goldens" ]; then exit 0; fi

tool=$(printf '%s' "$input" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("tool_name",""))' 2>/dev/null)
guarded='(^|/)(docs/decisions/|tests/golden/)|tolerance'

block() { echo "BLOCKED by guard-oracle: $1 is human-owned (tests/golden/**, tolerance files, docs/decisions/**). Only a session Joe opened after 'touch .claude/touch-goldens' may change it; ADRs are superseded, never edited." >&2; exit 2; }

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
