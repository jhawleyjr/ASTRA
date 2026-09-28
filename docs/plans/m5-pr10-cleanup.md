# M5 PR 10: post-M5 cleanup

Spec: Joe's PR 10 instruction (2026-09-28, in session); PLAN.md open items "Wall-clock symbol scan",
"`astra.fgbridge` pacing", "Follow-up A", and the ADR-0025 Finalization item's point (b).
Risk: Tier 2 (a new `just check` step and a changed public default). Plan critique: none, since ADR-0010,
ADR-0023 and architecture §15 pin the design (Codex reviews the diff, per Tier 2).
Status: **draft, plan approval pending (Joe).** Base `origin/main` `b1cae7f` (PR #71 merged), branch
`worktree-m5-pr10-cleanup`. Nothing but this file is written until Joe approves.

## Repository findings

Observed on `b1cae7f`, a clean fresh configure and `just build asan` in this worktree (exit 0).

- **Rule text.** AGENTS.md's Time bullet limits clock access, "in `core/`, `plugins/` and `aircraft/`", to the
  `RealTime` pacer and "the lockstep transport timeout (ADR-0023)". ADR-0010's Consequences say the same and that
  "the determinism guard (§15) scans plugins for wall-clock symbols". Architecture §15 says no such scan exists.
  ADR-0023 names two sites, each confined to one function: `plugins/sim/transport/shm/shm_wait.cpp` and
  `embedded/hal/shm/hal_shm_wait.cpp`, and its Consequences say "two named clock sites in the whole tree".
- **The `RealTime` pacer is not built**, so today's allowlist is exactly those two files and nothing else.
- **What `nm -u` reports (binutils 2.42, clang++-18 with libstdc++, probe TU compiled at `-O0` and `-O2`).**
  Plain C names appear unmangled: `clock`, `clock_gettime`, `ftime`, `gettimeofday`, `time`, `timespec_get`.
  The chrono clocks are `_ZNSt6chrono3_V212steady_clock3nowEv` and `_ZNSt6chrono3_V212system_clock3nowEv`
  (`std::chrono::_V2::steady_clock::now()`, `…::system_clock::now()`). `high_resolution_clock` is an alias of
  `system_clock` in libstdc++, so it produces the `system_clock` symbol. Optimization does not remove them.
  `std::chrono::utc_clock` and `file_clock` do not exist in this toolchain's library, so they cannot appear.
- **The real tree today (53 in-scope TUs in `build/asan`).** Exactly two hit a wall-clock symbol:
  `shm_wait.cpp` (`clock_gettime`) and `hal_shm_wait.cpp` (`clock_gettime`). No other object references any
  symbol in the list. Names a loose substring match would have flagged, and an exact-name match does not:
  `strtod` (autopilot `cli.cpp`), `H5Pset_obj_track_times` (HDF5 property setup), `_ZNSt13runtime_error…`,
  and `clock_nanosleep` in the HAL wait (a sleep, not a clock read; see risks).
- **Objects.** Ninja's `compile_commands.json` carries `file`, `directory` and `output` for every TU, including
  the static-plugins preset's OBJECT-library plugin TUs. It is written at configure time, so a fresh configure has
  it; the objects exist only after a build. Every core/plugins/aircraft/embedded target is in the default target
  (no `EXCLUDE_FROM_ALL` there; the two EXCLUDE_FROM_ALL harnesses are under `tests/`), so `just build <preset>`
  produces all of them.
- **fgbridge.** `python/astra/fgbridge.py`: `LiveBridge.__init__` (line 232) and `send_log` (line 312) default
  `pace=False`; `_Sender.send` does the wall-clock wait. `tests/python/test_fgbridge.py` has 26 constructor or
  `send_log` call sites and one pacing test (line 562 on, `monkeypatch` of `fgbridge.time.monotonic`/`sleep`).
- **Follow-up A text.** `tests/unit/icd/icd_pack_test.cpp` lines 351 to 359, the comment of
  `CasMatchesPublishedIsentropicTableAtTenKilometers`. Item (a) in `docs/reviews/m4-pr2-icd-pack-human.md`
  is the exact wording to apply.
- **Precedent for a completed open item.** PLAN.md's Open items list has no `[x]` entry; completed items are
  removed and named in the completing PR's own line (e.g. M4 PR 7's "duplicate §15 bullet removed"). I follow it.
- **`native_fdm.cxx` is not in `docs/refs/`** (only `net_fdm.hxx` and `native_structs.cxx`). I cannot check the
  "line 122 on" claim locally; the docstring will state it as Joe's reading of FlightGear 2024.1.7, and Codex or
  Joe can confirm it against the source.

## Decisions taken in this plan (Joe: veto or confirm at approval)

1. **The scan covers `embedded/` as well as `core/`, `plugins/` and `aircraft/`.** ADR-0010 and AGENTS.md scope
   the restriction to the first three, but ADR-0023 is explicit that the exception has two sites "in the whole
   tree", one of them in `embedded/hal`, architecture §15 already says the guard "must allowlist" both, and
   principle 1 says no "embedded process reads the wall clock". Those settle it, so I am not asking, but the
   consequence is a check slightly wider than AGENTS.md's sentence. Today it finds nothing new. If Joe wants
   `embedded/` out, the change is deleting one entry from the scanner's root list and the allowlist.
   Not covered: `tests/`, `python/` (the nanobind module and `astra.*` Python are outside ADR-0010's scope), and
   `icd/`.
2. **The allowlist is by source file and must match.** A scanned allowlisted file that has no wall-clock
   reference fails as a stale entry, and a scan that finds no in-scope object fails, so an empty or moved
   compile database cannot pass silently. This is the one addition beyond your list.
3. **The scan runs on all four presets**, after that preset's build (debug, release, static-plugins, asan). It is
   cheap, and static-plugins compiles plugin sources through a different target kind.
4. **`docs/architecture.md`, three sentences.** §15's "No such symbol scan exists today" becomes a description of
   the scan; the `astra.fgbridge` bullet's "off in tests" becomes "on by default, off in tests". ADR-0010 and
   ADR-0023 are accepted and immutable and are not touched, though both say "a future scan"; the architecture
   text carries the update. Say so if you would rather leave architecture out of this PR.

## Mapping and sequence

| Req | Change / owned paths | Contract | Acceptance command |
|---|---|---|---|
| 1 scan | `scripts/wallclock_scan.py` (new), `justfile`: recipe `wallclock-scan preset="asan": (build preset)`, added to `check` after `(test "asan")` for the four presets, `docs/process/validation.md` command table row, `docs/architecture.md` §15 | `wallclock_scan.py [--root DIR] BUILD_DIR`. Reads `BUILD_DIR/compile_commands.json`; a TU is in scope when `file` relative to root starts with `core/`, `plugins/`, `aircraft/` or `embedded/`; runs `nm -u -C` on its `output` object; flags an undefined symbol that is exactly one of `clock`, `clock_gettime`, `gettimeofday`, `time`, `timespec_get`, `ftime`, or matches `std::(__N::)?chrono::(_V2::)?<name>_clock::now()`. Prints `wallclock-scan: <source>: <symbol>` per hit, exits 1; a missing object, a stale allowlist entry, or zero in-scope TUs also exit 1. Exit 0 prints `wallclock-scan: OK (N TUs, allowlisted: <files>)`. Allowlist: `plugins/sim/transport/shm/shm_wait.cpp`, `embedded/hal/shm/hal_shm_wait.cpp` (ADR-0023) | `just wallclock-scan asan` exits 0 on the tree, for each preset |
| 1 control | `tests/python/test_wallclock_scan.py` (new) | Builds a throwaway root in `tmp_path` with `plugins/<a>/<b>/*.cpp`, compiles with `clang++-18 -std=c++23 -c`, writes a matching `compile_commands.json`, runs the script as a subprocess. Cases: `std::chrono::steady_clock::now` fails naming file and `std::chrono::_V2::steady_clock::now()`; `clock_gettime` fails naming file and `clock_gettime`; `system_clock`, `gettimeofday`, `time`, `clock` and `timespec_get` fail (parametrized); the same `clock_gettime` code at an allowlisted relative path passes, and at any other path fails (allowlist is by source file); an allowlisted path with a clean TU fails as stale; a clean tree passes; an empty database fails; a substring lookalike (`strtod`) passes | `.venv/bin/pytest tests/python/test_wallclock_scan.py` |
| 1 mutation | Scratch only, not committed: remove `shm_wait.cpp` from the allowlist, run `just wallclock-scan asan`, expect exit 1 naming `plugins/sim/transport/shm/shm_wait.cpp: clock_gettime`; restore, confirm the marker is gone and re-run to exit 0 (separate commands, lesson 2026-09-26). Result goes in the PR body | |
| 2 pacing | `python/astra/fgbridge.py`: `pace: bool = True` in `LiveBridge` and `send_log`; module and class docstrings corrected (paced by default; reason: FlightGear's socket reader drains every waiting packet and applies the newest, native_fdm.cxx line 122 on, FlightGear 2024.1.7, so an unpaced bridge shows the flight in fast-forward and FlightGear does not fall behind); `tests/python/test_fgbridge.py`: `pace=False` at every construction and `send_log` call; new test `test_pacing_is_on_by_default` (monkeypatched `fgbridge.time.monotonic` and `sleep`, no real wait, no `pace` argument, for both `LiveBridge` and `send_log`; `pace=False` still sleeps zero times) | Public default change, Python only, no C++ | `.venv/bin/pytest tests/python -k fgbridge` |
| 3 Follow-up A | `tests/unit/icd/icd_pack_test.cpp`, comment lines only: 0.807 to 0.808 (max about 0.8074); the 0.029 m/s gap attributed as the item says (M 0.43/0.44 row rounding about 0.026, `.6560` rounding about 0.007, interpolation about -0.004); the "changed-gamma pair" meaning (matched exponents 3.4 and 1/3.4 with the 0.2 and 5 factors held; a fully consistent gamma of 1.41 moves CAS by only 0.010 m/s, which the table test does not catch and which is physically negligible). The 0.807 also appears in the derived interpolation-error term, so I recheck that the arithmetic in that comment still reads `0.01^2 * 0.808 / 8` and its 2.0e-5 Mach result | No code or number in the test changes (the `0.06` tolerance stays) | `git diff` shows comment-only hunks (checked by a whitespace-and-comment diff of the executable lines); `ctest --preset asan -R IcdPack` |
| 4 PLAN.md | Finalization point (b): cite `docs/reviews/m5-pr4-run-log-review.md` (Finding F1) instead of the gate summary; remove the three completed open items; add "PR 10 (#NN)" under M5 after PR 9. No merge hash, date or "approved" | Docs check | `python3 scripts/docs_check.py` |

The `(#NN)` is filled in once the draft PR exists. One builder, no split. Commit order: this plan, the scanner and
its test, the recipe, fgbridge, Follow-up A, PLAN.md and architecture, the gate summary.

## Validation and risks

- **Gate.** `./scripts/check-fast.sh` while iterating, then `./scripts/check-full.sh`, exit 0 with `check: OK`,
  trimmed summary `docs/reviews/evidence/<head>-gate-summary.log` committed. Fresh-configure evidence (lesson
  2026-09-22): before the full gate I delete this worktree's `build/` and run `just wallclock-scan release`, which
  must configure, build and scan on its own through its `(build preset)` dependency; the full gate then runs
  from that state (today `build/` holds only the asan build made for this plan).
- **Named acceptance commands.**
  - `just wallclock-scan asan` (and debug, release, static-plugins): exit 0.
  - `.venv/bin/pytest tests/python -k "wallclock or fgbridge"`: selects `test_wallclock_scan.py` and
    `test_fgbridge.py` only; the negative control and the fgbridge tests, including the new default-pacing test.
- **Limits of the scan (stated in the script's header and the architecture text).** It sees undefined references
  in compiled objects, so it does not see a raw `syscall(SYS_clock_gettime, …)`, an inline-asm `rdtsc`, a
  `dlsym`'d clock, or a clock reached through a third-party library (`clock_nanosleep`, `sleep_for` and friends do
  not read a clock and are deliberately not in the list). It checks compiled TUs of the presets, not sources that
  no target builds. It does not prove the allowlisted sites use the clock only as ADR-0023 allows; that stays
  review's job (each site is one function, ADR-0023).
- **fgbridge risk.** A caller that relied on the unpaced default now waits `frames * dt` of wall time (a 10 s run at
  62.5 Hz packets takes 10 s). The only in-tree callers are the tests, all switched to `pace=False`. Revert: one
  default and one docstring.
- **No human-owned file touched.** Nothing under `tests/golden/**`, no tolerance, no ADR. `icd_pack_test.cpp` is
  under `tests/unit/`.
- **Out of scope**, per the instruction: Follow-up B, any `core/` or plugin behavior change, the Finalization ADR,
  and Python-side clock use.

## Plan critique and decisions

None requested: the architecture pins the design (ADR-0023, §15). Pending Joe: the four decisions above,
in particular 1 (embedded coverage) and 4 (architecture edits).

## Done when

The acceptance commands above pass, `./scripts/check-full.sh` exits 0 with `check: OK` and its trimmed summary
is committed, Codex has reviewed `origin/main..HEAD` (Tier 2) with findings resolved or rejected on evidence, and
the PR body follows `docs/templates/pr-description.md` with the mutation result, the counts, and the Codex
verdict and thread id. Merge is Joe's.
