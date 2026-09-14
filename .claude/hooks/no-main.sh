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
exit 0
