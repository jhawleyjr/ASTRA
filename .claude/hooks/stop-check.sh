#!/usr/bin/env bash
# Stop feedback: run the fast check on the first stop attempt. A failed check blocks once so Claude
# can fix within the task's budget or report a blocker. On hook continuation, allow handoff without
# re-running or claiming validation. Stop completion is not task completion or a passed full gate.
# Plan mode skips checks. A busy build blocks the first stop attempt; never run overlapping builds.
# Own timeout is shorter than settings.json's hook timeout, so expiry produces explicit feedback.
set -u
input=$(cat)
field() {
  printf '%s' "$input" | python3 -c 'import json,sys; print(json.load(sys.stdin).get(sys.argv[1], ""))' "$1" 2>/dev/null
}
block() { printf 'BLOCKED by stop-check: %s\n' "$1" >&2; exit 2; }

[ "$(field permission_mode)" = "plan" ] && exit 0
if [ "$(field stop_hook_active)" = "True" ]; then
  echo "stop-check: allowing handoff after prior hook feedback; no validation ran here. Report the previous failure or a completed explicit rerun; do not claim a pass from this exit." >&2
  exit 0
fi
cwd=$(field cwd)
cd "${cwd:-${CLAUDE_PROJECT_DIR:-.}}" 2>/dev/null || cd "${CLAUDE_PROJECT_DIR:-.}" || block "cannot enter $cwd"
# Physical path: /proc/<pid>/cwd is resolved, while $PWD keeps any symlink the session was launched through.
root=$(pwd -P)

for pid in $(pgrep -u "$(id -u)" -x 'just|cmake|ninja|ctest' 2>/dev/null); do
  dir=$(readlink "/proc/$pid/cwd" 2>/dev/null) || continue
  case "$dir" in
    "$root"/.claude/worktrees/*) ;;
    "$root" | "$root"/*)
      block "a build or test is already running under $root (pid $pid, $(cat "/proc/$pid/comm" 2>/dev/null)). Wait for it to finish, read its result, then end the turn." ;;
  esac
done

budget=${ASTRA_STOP_BUDGET:-840}  # seconds; the override exists for the hook test cases only
log=$(mktemp)
trap 'rm -f "$log"' EXIT
timeout -k 10 "$budget" just test-unit >"$log" 2>&1
status=$?
[ "$status" -eq 0 ] && exit 0
{
  if [ "$status" -eq 124 ]; then
    echo "BLOCKED by stop-check: 'just test-unit' exceeded ${budget} s in $root (hung test? concurrent build?)."
  else
    echo "BLOCKED by stop-check: 'just test-unit' failed (exit $status) in $root. Fix within the workflow budget and rerun explicitly, or hand off the failure as blocked; never claim validation passed."
  fi
  grep -n -m 20 -E '^FAILED:|error:|The following tests FAILED' "$log"
  echo "--- last 40 lines of 'just test-unit' ---"
  tail -n 40 "$log"
} >&2
exit 2
