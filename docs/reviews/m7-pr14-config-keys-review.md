# Review: M7 PR 14, the generated config-key reference

Spec/plan: [plan](../plans/m7-pr14-config-key-reference.md) (approved by Joe, 2026-10-11, with his coverage
decision, D1 to D3 and amendments A1 to A8)
Builder: Claude (claude-opus-5-5)
Reviewer: Codex (`gpt-6.1-sol`, effort `high`, companion 1.0.6; fresh context): one `adversarial-review` with
the plan's focus, thread `01a12ecf-e1dc-77e3-99f3-b7ed0aa495b7`
Scope: the committed branch diff against `origin/main` (`acdc6b5`); head `d0bd100`, gated at `1993270`. The
tree was clean.
Validation: the full gate at `1993270` (exit 0, `check: OK`; [summary](evidence/1993270-gate-summary.log)).
This fix batch reruns it.
Round: fix 1 (correction round 1 of 2)

## Finding [R-001]

Severity: major (Codex's own scale says medium; mapped up, not down)
Status: fixed-unverified
Location: `tests/python/test_config_keys.py:167-172` (at `d0bd100`), the attribution loop in `_survey_config`
Requirement/ADR: the plan's §1 (a row's type names a scalar getter) and §4.3 to §4.4 (a plugin's consumed keys
equal its documented keys, with no key list)
Failure: `Config::unknown_keys()` reports an empty map or sequence as a leaf (`collect_leaf_paths`). A valid
loopback config with `peer: {}`, `peer.commands: {}` or `peer.schedule: []` builds, and the loop charged the
container's path (`peer`, `peer.commands`, `peer.schedule`) to the plugin as a scalar key, which no row can
document. Adding such a config would fail the equality check. No tracked config writes one today, so the
committed suite passed. Reproduced on `d0bd100`'s `_survey_config`: those two loopback variants are charged
`['peer']` and `['peer.commands', 'peer.faults', 'peer.schedule']`.
Requested outcome: account for a consumed empty container structurally, through the documented keys below it,
with no key list and no weakening of the scalar-key checks. Cover empty `peer`, `commands`, `schedule` and
`faults`.
Builder response: fixed.
- `_survey_config` sorts each leaf by what the core reads at it (`Config.at(leaf).is_map()` or
  `is_sequence()`, on the throwaway `Config`): an empty container is charged to `Survey.containers`, never to
  `Survey.consumed`, so the scalar equality is unchanged.
- A charged container must hold a documented key of its plugin: one that starts with `<container>.` or
  `<container>[]`, a segment boundary only (`_holds_a_documented_key`, `_container_differences`). The equality
  test reports any that does not.
- New tests: `test_config_keys_an_empty_container_counts_as_the_keys_it_would_hold` (empty `peer`; empty
  `commands`, `schedule` and `faults`), through `_survey_config` on a `tmp_path` copy of the loopback fixture;
  `test_config_keys_a_container_holds_only_keys_below_a_segment_boundary` (`peer.sched` does not hold
  `peer.schedule[].frame`, and a container that holds nothing is reported).
Verification: pending (the fix verification pass on this batch's head).
History: round 1, opened by thread `01a12ecf-e1dc-77e3-99f3-b7ed0aa495b7`; fixed-unverified in this commit.
Human exception if needed: none.

## Completion

One finding, major, fixed and awaiting verification. No other finding. Reviewer recommendation is not Joe's
approval. The fix verification pass and the full gate on this batch follow.
