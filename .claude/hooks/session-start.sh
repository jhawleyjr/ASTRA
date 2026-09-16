#!/usr/bin/env bash
# SessionStart hook. Whatever this prints on stdout is added to Claude's context at the start of every session,
# so a fresh session begins knowing what is in flight without anyone typing "read the repo".
# Read-only: only `git --no-optional-locks` (docs/lessons.md, index.lock), never writes.
set -u
root="${CLAUDE_PROJECT_DIR:-$(pwd)}"
cd "$root" || exit 0
g() { git --no-optional-locks "$@" 2>/dev/null; }

echo "=== ASTRA session context (from .claude/hooks/session-start.sh) ==="
branch=$(g rev-parse --abbrev-ref HEAD || echo '?')
gitdir=$(g rev-parse --git-dir); common=$(g rev-parse --git-common-dir)
if [ -n "$gitdir" ] && [ "$gitdir" != "$common" ]; then
  main_checkout=$(cd "$(dirname "$common")" && pwd)
  echo "Worktree session on branch '$branch' (main checkout: $main_checkout)."
  dirty=$(git -C "$main_checkout" --no-optional-locks status --short 2>/dev/null)
  if [ -n "$dirty" ]; then
    echo "WARNING: the main checkout has uncommitted changes that this worktree cannot see (docs/lessons.md):"
    printf '%s\n' "$dirty" | sed 's/^/    /'
  fi
else
  echo "Branch '$branch' in the main checkout. Work happens in worktrees: restart with 'claude --worktree <name>' before editing."
fi

echo; echo "--- git log -10 ---"; g log --oneline -10
echo; echo "--- STATE.md ---"; cat STATE.md 2>/dev/null || echo "(no STATE.md)"
echo; echo "--- PLAN.md: unchecked items ---"; grep -E '^\s*- \[ \]' PLAN.md 2>/dev/null || echo "(none)"
echo; echo "--- gate ---"
if [ -f justfile ]; then echo "justfile present: 'just check' is the gate; it runs once, after the work."
else echo "No justfile yet (pre-PR 2): there is nothing to run."; fi
# The marker cannot be checked here: the SessionStart hook that deletes it runs in this same event.
[ "${ASTRA_TOUCH_GOLDENS:-0}" = "1" ] && echo "Oracle guard LIFTED for this session (ASTRA_TOUCH_GOLDENS=1)."
echo "Read CLAUDE.md conventions before writing code; ADRs in docs/decisions/ are immutable."
exit 0
