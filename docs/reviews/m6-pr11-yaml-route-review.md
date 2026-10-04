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
Round: fix 1 (correction round 1 of 2)

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
Verification: pending (a Codex fix-verification pass on the fix commit).
History: fix 1, open → fixed-unverified.
Human exception if needed: none.

## Completion

- General review: no actionable defects.
- Adversarial review: one finding (A-001, fixed-unverified, pending verification). No rejected findings.
- Scope not reviewed: none.
- Validation: the gate reruns on the fix commit. D5's before/after reruns its candidate pass, because `python/`
  changed.
