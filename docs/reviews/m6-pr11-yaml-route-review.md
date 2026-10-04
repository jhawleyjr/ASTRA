# Review: M6 PR 11, configs read through PyYAML

Spec/plan: [plan](../plans/m6-pr11-yaml-route.md) (approved by Joe, 2026-10-04, with six amendments)
Builder: Claude (claude-opus-5-5)
Reviewer: Codex (`gpt-6.1-sol`, effort `high`, companion 1.0.6; fresh context): a general `review` (thread
`01a10640-1a10-72c3-824c-41ecd768abd1`) and an `adversarial-review` with the plan's focus (thread
`01a10640-1a6c-7142-8a4d-600135e2e96b`)
Scope: the committed branch diff against `origin/main` (`2794a0c`); head `e9f4e05`, gated at `0e90176`. The
tree was clean.
Validation: the full gate at `0e90176` (exit 0, `check: OK`; [summary](evidence/0e90176-gate-summary.log)).
This fix batch reruns it.
Round: fix 2 (correction round 2 of 2, the last autonomous round)

## Finding [A-001]

Severity: major
Status: fixed-unverified
Location: `python/astra/_yaml_route.py:60` (at `e9f4e05`), `yaml.safe_load(text)` outside any translation
Requirement/ADR: the plan's D1 and D2, and amendment 3 (each caller's `error=`: `SpecError`, `ScenarioError`,
`LockstepError`, the core's `ConfigError`; `astra trim` exits 65 on a refusal)
Failure: PyYAML's constructors raise plain `ValueError` or `KeyError`, which no `yaml.YAMLError` wraps, for
scalars the core reads as text. Examples: `0000-01-01` (year 0), `2026-13-45`, `!!bool warn`, `!!int abc`,
`!!float x`. Reproduced at `e9f4e05`: `station: 0000-01-01` in `cruise.yaml`, or `sim.health.nan: !!bool warn`,
escapes every caller's error class, and `astra trim` exits 70. This predates the PR, but it breaks the PR's
refusal contract.
Requested outcome: translate the constructor failure through the caller's error factory, naming origin and key.
Keep the existing `YAMLError` handling.
Builder response: fixed. `load` now re-raises `yaml.YAMLError` unchanged, and refuses any other exception from
`yaml.safe_load` through `error`.
- The message names the key and the core's text, as in `p.yaml: s.nan: PyYAML cannot construct this value
  (KeyError: 'warn'); the core reads 'warn'`.
- `_construction_refusal` finds the key by composing the document (`yaml.compose`) and constructing each scalar
  alone (`_failing_path`, keys included). If none fails alone, the key falls back to "the document".
- Regressions (10, all failing on `e9f4e05`'s loader and passing on the fix):
  - three probe-table rows: `date year 0`, `!!bool warn`, and `!!int abc in a list`, which names `v.1`;
  - `date year 0` and `!!bool warn` through `montecarlo.load` and `scenario.load`;
  - `astra trim` exits 65 for `!!bool warn` and for `station: 0000-01-01`;
  - `lockstep.load_config` raises `LockstepError`.
Verification: Codex fix verification at `8f1db37` (thread `01a10649-e0f3-7871-b33e-64b9ebe68817`): "only
partially fixed". The recorded examples reach all four error classes and `YAMLError` propagates unchanged. But
the diagnostic walk could raise past the factory on a recursive alias, which is V-001 below. Fix 2's
verification is pending.
History:
- fix 1: open → fixed-unverified (`f77b6e8`);
- verification 1: partially fixed (V-001);
- fix 2: fixed-unverified (V-001's fix).
Human exception if needed: none.

## Finding [V-001]

Severity: major
Status: fixed-unverified
Location: `python/astra/_yaml_route.py:134-136` (at `8f1db37`), `_failing_path` with no cycle tracking; and
`load`, which reached the core with a recursive alias
Requirement/ADR: the plan's D1 (refusals through the caller's error factory). Found by the verification pass of
A-001's fix.
Failure: there are two failures.
- From Codex: `cycle: &c [*c]\nk: !!int abc\n`. PyYAML raises `ValueError`, and `_failing_path` then follows
  the self-referencing `SequenceNode` until `RecursionError`, which escapes `_construction_refusal` before
  `error(...)`. Builder probe: `RecursionError: maximum recursion depth exceeded`.
- From the builder, widening it: with no constructor failure (`cycle: &c [*c]\nk: 1\n`), `load` segfaults the
  process (exit -11) inside the core's `Config::unknown_keys()`, which recurses without end on a recursive
  alias.

  That core defect predates this PR: `astra run` on `descent.yaml` plus `extra: &c [*c]` also exits -11. The
  route before this PR crashed too, in `from_dict`. The core fix is out of this PR's scope (`core/`, Tier 3), so
  it is recorded as a PLAN.md open item for Joe.
Requested outcome: cycle-aware diagnostics that fall back to the document-level message, keeping the caller's
error factory, with regressions that put a cyclic alias before an invalid scalar.
Builder response: fixed.
- `load` refuses a recursive alias through `error` before the core reads the text (`_cycle_path` tracks the
  containers on the current path by `id`, so a shared, acyclic alias still passes). Example message:
  `p.yaml: cycle.0: a recursive alias (an anchor used inside itself), which neither reader can carry`.
- `_failing_path` tracks the collection nodes on its path and never follows one twice.
- `_construction_refusal` wraps the whole diagnostic, so any failure inside it falls back to "the document".
- Regressions, 7 new:
  - probe rows `recursive alias`, `recursive map alias` and `recursive alias before !!int abc`;
  - `recursive alias` through `montecarlo.load` and `scenario.load`;
  - `astra trim` exits 65 for `recursive alias` and for `recursive alias, then !!int`.
- On `8f1db37`'s loader, the cycle-then-failure cases fail (`RecursionError`), and the plain-cycle cases crash
  pytest with a segmentation fault (exit 139). On the fix, all 111 pass.
Verification: pending (a Codex fix-verification pass on fix 2).
History: verification 1 of A-001, open → fix 2, fixed-unverified.
Human exception if needed: none for the fix. The core's segfault is an open item, owner Joe.

## Completion

- General review: no actionable defects.
- Adversarial review: one finding (A-001). Its verification found V-001 in the fix delta. Both are
  fixed-unverified after fix 2, the last autonomous correction round. No rejected findings.
- Scope not reviewed: none.
- Validation: the gate reruns on the fix commit. D5's before/after reruns its candidate pass, because `python/`
  changed.
