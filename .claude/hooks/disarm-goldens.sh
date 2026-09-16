#!/usr/bin/env bash
# SessionStart hook. Deletes the oracle-guard marker so a lifted guard never carries into the next
# session (docs/process/workflow.md §Verification). Replaces the inline `rm -f` this hook used to be:
# the marker is accepted in either the session's own root or the main checkout (guard-oracle.sh), so
# both have to be cleared here, or a marker in the main checkout would disarm the guard permanently.
set -u
root="${CLAUDE_PROJECT_DIR:-$(pwd)}"
rm -f "$root/.claude/touch-goldens"
common=$(git --no-optional-locks -C "$root" rev-parse --git-common-dir 2>/dev/null)
case "$common" in
  "") ;;
  /*) rm -f "$(dirname "$common")/.claude/touch-goldens" ;;
  *)  main_root=$(cd "$root/$(dirname "$common")" 2>/dev/null && pwd || echo "")
      [ -n "$main_root" ] && rm -f "$main_root/.claude/touch-goldens" ;;
esac
exit 0
