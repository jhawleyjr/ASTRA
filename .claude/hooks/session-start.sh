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
  echo "Branch '$branch' in the main checkout. Preserve an explicitly assigned task branch; otherwise create a worktree for new work. Never implement on main."
fi

echo; echo "--- git log -10 ---"; g log --oneline -10
echo; echo "--- STATE.md ---"; cat STATE.md 2>/dev/null || echo "(no STATE.md)"
echo; echo "--- PLAN.md: unchecked items ---"; grep -E '^\s*- \[ \]' PLAN.md 2>/dev/null || echo "(none)"
echo; echo "--- gate ---"
if [ -f justfile ]; then echo "Fast: scripts/check-fast.sh. Full: scripts/check-full.sh (just check), after implementation and substantive fixes/integration; commit the trimmed gate summary and put results in the PR body (AGENTS.md)."
else echo "No justfile found: validation unavailable; diagnose checkout/tooling before claiming completion."; fi
# The marker cannot be checked here: the SessionStart hook that deletes it runs in this same event.
[ "${ASTRA_TOUCH_GOLDENS:-0}" = "1" ] && echo "Oracle guard LIFTED for this session (ASTRA_TOUCH_GOLDENS=1)."
echo "Read AGENTS.md and CLAUDE.md; architecture and accepted ADRs bind the task."
echo "Claude plans/builds/validates/fixes/integrates; Codex reviews/adversarially reviews; Joe approves/merges."
echo "Use /plan-feature, /review-cycle, /human-review and /wrap as appropriate. Read STATE's linked artifacts and resume the first unfinished gate."
echo "A stopped turn or green local-mode CI is not a passed full gate; a blocked handoff states its failures; correction/integration counts live in the PR body (AGENTS.md)."
exit 0
