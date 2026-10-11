# M7 PR 2: `Config` cycle safety

Status: **approved by Joe, 2026-10-10, in plan mode**, with his amendments 1 to 3 folded in (below).
Owner: Joe. Builder: Claude (one builder). Tier: **2** (`core/config`, `python/`; Q19).
Base: `origin/main` = `e4c9c63` (#111), read with `git ls-remote` in plan mode and confirmed by `git fetch` on
approval. Branch and worktree: `worktree-m7-pr2`, from `origin/main`; this file started as the session's plan-mode
draft and was moved into the worktree (`docs/agents/claude-code.md`). PR 6b runs in `m7-pr6b`; nothing here
touches `aircraft/`.

## Joe's amendments (2026-10-10)

1. A fourth text reproducer, the duplicate key reversed: `extra: 1\nextra: &c [*c]\nk: 1\n` (§3, §4).
2. The key-cycle test asserts the refusal at the enclosing map's path: yaml-cpp 0.8.0 parses `? &k [*k]` then
   `: 1` (Joe probed it) (§4).
3. No depth cap (accepted); the residual is recorded in one line (§1).
Sources: the [M7 plan](m7-f16-nesc-oracle.md)'s PR table row 2, R16 and Q19; PLAN.md's (M7) Config cycle item;
lessons 2026-10-04 (yaml-cpp 0.8 does not merge `<<` and keeps the first duplicate key, PyYAML the last). No
Codex plan critique: Joe's brief settles the design, and the change is bounded.

## Context

A recursive YAML alias makes `Config::unknown_keys()` (`collect_leaf_paths`, `core/src/config/config.cpp:129`)
and `from_dict`'s emitter (`value_to_yaml`, `python/bindings/_core.cpp:106`) recurse without end, and the process
segfaults (exit −11). `_yaml_route` refuses the cycles PyYAML keeps, but not the two its construction erases. Outcome:
every cycle is a `ConfigError` naming the key, at load, before any run starts or file is written; `astra run` exits 65.

## 1. The core: refuse a cycle at load (`core/src/config/config.cpp`)

- A new file-local `refuse_cycles(node, path, ancestors, origin)` runs in `Config::from_file` and
  `Config::from_string` right after `YAML::Load`, before the `Document` exists. Those are the only two factories
  (the constructor is private), so every `Config`, `from_dict`'s included, is acyclic, and `unknown_keys()`,
  `emit_without()` and every getter are cycle-safe by construction. The document is never mutated after load.
- **Identity:** `YAML::Node::is()` (yaml-cpp 0.8, `node/impl.h:195`: equal `node_ref`). An alias is the same
  `detail::node` as its anchor (yaml-cpp's builder pushes the anchored node again), so `is()` is true exactly for
  "the same node". `ancestors` is a `std::vector<YAML::Node>` holding the maps and sequences on the current path
  only: pushed before the children, popped after. Reaching a node that `is()` an ancestor is refused; a node
  reached twice by sibling paths (`a: &x [1]` with `b: *x`) is never an ancestor of itself and still loads.
- The walk iterates entries (not lookups), so it sees both entries of a duplicate key, whichever holds the cycle.
  It also walks key nodes (`? &k [*k] : 1`), which the emitter would otherwise loop on; a cycle inside a key is named
  at the enclosing map's path.
- **Refusal:** `ConfigError(origin, <path where the cycle closes>, "a recursive alias: an anchor used inside
  itself, reached again from '<ancestor path or the top level>'")`. So: `extra: &c [*c]` → `at 'extra.0'`; the
  self-merge `m: &m {<<: *m}` → `at 'm.<<'` (yaml-cpp keeps `<<` as a key); `extra: &c [*c]` then `extra: 1` →
  `at 'extra.0'`.
- **No depth cap.** The ancestor check is exact for cycles, and an acyclic document is finite. Its depth is already
  what `collect_leaf_paths` and the emitter recurse through, bounded by yaml-cpp's parser `DepthGuard` (2000 levels
  per nesting, `depthguard.h`) and growing past it only linearly in the text through chained aliases. A cap would add
  an arbitrary refusal of a legal config. Shared aliases are walked once per use, as `unknown_keys()` already does.
- **Residual (amendment 3):** past yaml-cpp's `DepthGuard` (2000), depth grows only through chained aliases,
  which the walk visits once per use as `collect_leaf_paths` already does, so a document deep enough to threaten
  the stack costs quadratic time first; no legal config is refused.
- `config.hpp`: comments only (`from_file`/`from_string` also refuse a recursive alias).

## 2. The emitter: `value_to_yaml` (`python/bindings/_core.cpp`)

- Gains the dotted path, the origin and an ancestor stack `std::vector<PyObject*>` (`handle.ptr()`, Python's `id`)
  of the dicts, lists and tuples on the current path, pushed and popped around their children. Reaching an ancestor
  throws `astra::ConfigError(origin, path, "a recursive value: a dict or list that contains itself, reached again
  from '<ancestor>'")`, which nanobind maps to `_core.ConfigError`. A value shared by two siblings is carried twice,
  as today. No depth cap, for the same reason. The `TypeError` for an unsupported type is unchanged.

## 3. `_yaml_route`'s guard goes (`python/astra/_yaml_route.py`)

`_cycle_path` and its call go. With the core refusing at load, `load()`'s `Config.from_string` raises before any
walk of the dict, and the refusal reaches the caller through `error` as "PyYAML reads it, but the core does not:
config '...' at 'extra.0': a recursive alias ...". I expect no cycle the core cannot see: every cycle in the text
is a cycle in yaml-cpp's graph (yaml-cpp does not merge, so a self-merge is a `<<` cycle). The hard case is
amendment 1's reversed duplicate, `extra: 1\nextra: &c [*c]\nk: 1\n`: yaml-cpp's lookup keeps the first entry (a
scalar) and PyYAML the last, so PyYAML's dict is cyclic while the core's lookup sees no cycle, and `_walk` would
loop on that dict. What keeps the tools safe is that `from_string`, iterating entries, refuses this text at
`extra.0` before `load()` walks the dict; §4's tests are the evidence. If the core loads it, it is the cycle the
core cannot see, and the guard stays for it (brief item 4), reported to Joe. `_failing_path`'s own
`active` set stays (it walks `yaml.compose` nodes). The docstring loses the cycle refusal and the V-001 residual.
For `cycle: &c [*c]` with `k: !!int abc`, PyYAML's construction error still wins, but without its "the core reads
..." suffix, since the core now refuses the document.

## 4. Tests (pre-registered)

- `tests/unit/config/config_test.cpp`, +7 `Config.*` tests: the three reproducers, each through `from_string`,
  with the key path and message asserted (`extra.0`, `m.<<`, `extra.0`); the cycle in the second duplicate entry,
  with exactly amendment 1's text (`extra: 1\nextra: &c [*c]\nk: 1\n` → `extra.0`); a root cycle (`&r {a: *r}` →
  `a`); a cycle through a key (`? &k [*k]` then `: 1`, which yaml-cpp 0.8.0 parses), refused at the enclosing
  map's path (the top level); one shared acyclic alias (sequence and map) that loads,
  lists `a.0` and `b.0` in `unknown_keys()` and round-trips through `emit_without`. `from_file` is covered by the
  `astra run` tests. The `ctest -N` count for `-R Config` rises by exactly 7; the before and after counts go in the PR body.
- `tests/python/test_pyyaml_route.py` (selected by R16's `-k`):
  - The probe table's two cycle rows assert the core's refusal (`at 'cycle\.0': a recursive alias`, `at
    'm\.self'`); new rows: the self-merge (`at 'm\.<<'`), the overwritten cycle (`at 'extra\.0'`), and a shared
    alias `a: &x [1]\nb: *x\n` (unchanged).
  - `from_dict`: a list containing itself, a dict containing itself, a cycle through a tuple, each a
    `ConfigError` naming the key; a list shared by two keys loads.
  - The probe table also gains the reversed duplicate (`at 'extra\.0'`), and a test that `load()` on it raises
    the core's refusal with `_walk` never entered (a spy on `_yaml_route._walk`).
  - The four tools, on the three residual forms (the self-merge, the overwritten cycle and the reversed
    duplicate, each prepended to the tool's base config, `k: 1` included): `DESCENT_REFUSED_AT_READ` gains all
    three (so `montecarlo.load` and `scenario.load`), `test_pyyaml_route_trim_cli_refusal_exits_65` gains three
    rows (`astra trim`, 65), and `test_pyyaml_route_lockstep_refusals` gains all three (`load_config` and
    `trim_for`), each asserting the core's refusal at the key. The existing `recursive alias` entries' regexes
    move to the core's refusal.
- `tests/python/test_cli.py`: `astra run` on `descent.yaml` plus each of the four reproducers, as a subprocess
  (`astra_script`), so a regression shows as −11, not a crashed pytest: exit 65, empty stdout, stderr `astra run:
  config error: ` naming the key, and no file in the run directory.

## 5. Blast radius and compatibility

- Every tracked config loads unchanged: `test_pyyaml_route_tracked_configs_need_no_repair` passes each (≥ 30)
  through the core's `from_string`, and the full gate runs every config. The pyyaml_route round-trip tests pass
  with only the cycle regexes moved (§4). No numerical path changes, so no acceptance value moves; no file under
  `tests/golden/**` or tolerance file is touched.
- **No `kCoreApiVersion` bump (stays 10):** `config.hpp` changes in comments only; `Config` keeps its two members
  (`std::shared_ptr<Document>`, `std::string`) and has no virtual functions; `Document` lives only in `config.cpp`
  and is unchanged; the walk is a file-local function. A plugin receiving `const Config&` sees the same layout and
  calls the same symbols. `_core.cpp` is the Python extension, not the plugin boundary.
- Init-time only: no per-frame path is touched.

## 6. Checks

`ctest --preset asan -R Config --no-tests=error` (with `ctest -N` counts before and after);
`.venv/bin/pytest tests/python -k 'pyyaml_route or cli'` (R16); the full gate on the final candidate
(`./scripts/check-full.sh`, exit 0 and `check: OK`, trimmed summary committed as evidence);
`git diff --check origin/main..HEAD`.

## 7. Order

Plan approved (the Status line written in an edit Joe approves manually); implementation; full gate; one Codex
review, at most two correction rounds; `/human-review`; Joe reads the diff and tests. `/wrap` closes PLAN.md's
(M7) Config cycle item and adds the PR 2 line, and writes STATE.md as main reads it after the merge; if PR 6b
merges first, `origin/main` is merged into the branch before `/wrap` and STATE.md covers both.
