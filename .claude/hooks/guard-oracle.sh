#!/usr/bin/env bash
# PreToolUse hook (matchers: Edit|Write|MultiEdit|NotebookEdit and Bash).
# The oracle is human-owned (docs/process/workflow.md §Verification): docs/decisions/**, tests/golden/**,
# and tolerance files change only when Joe says so. Exit 2 = block; the message on stderr goes to Claude.
# Override for a session Joe started with `/task --touch-goldens`: the marker file .claude/touch-goldens
# (gitignored) or ASTRA_TOUCH_GOLDENS=1 in the environment.
set -u
input=$(cat)
root="${CLAUDE_PROJECT_DIR:-$(pwd)}"
if [ "${ASTRA_TOUCH_GOLDENS:-0}" = "1" ] || [ -e "$root/.claude/touch-goldens" ]; then exit 0; fi

tool=$(printf '%s' "$input" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("tool_name",""))' 2>/dev/null)
guarded='(^|/)(docs/decisions/|tests/golden/)|tolerance'

block() { echo "BLOCKED by guard-oracle: $1 is human-owned (tests/golden/**, tolerance files, docs/decisions/**). Only a session started with /task --touch-goldens may change it; ADRs are superseded, never edited." >&2; exit 2; }

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
    if printf '%s' "$cmd" | grep -Eq "$guarded" && printf '%s' "$cmd" | grep -Eq '(>|\btee\b|\bsed\b[^|;&]*-i|\brm\b|\bmv\b|\bcp\b|\btouch\b|\btruncate\b|\bgit\b[^|;&]*\b(rm|mv|checkout|restore)\b)'; then
      block "a path named in this command"
    fi
    ;;
esac
exit 0
