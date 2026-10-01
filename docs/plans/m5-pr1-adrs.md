# M5 PR 1: ADR-0025, ADR-0026, architecture rev 9, AGENTS.md wall-clock wording

Recovered from `~/.claude/plans/m5-pr-1-tier-crispy-abelson.md` on 2026-09-30. Approval: the file records none; the work merged as #63. Never committed: neither the file nor git history says why.

## Context

The M5 plan (`docs/plans/m5-tooling.md`, approved by Joe 2026-09-26) decided Q1 to Q24. Q1 says the run-log and
derived-signal decisions are expensive to unwind, so they become ADRs before PR 3 and PR 4 (kernel, Tier 3) start.
This PR is docs only (Tier 2): two **proposed** ADRs, architecture rev 9, the Q23 AGENTS.md narrowing, and the
PLAN.md PR 1 line. Branch `worktree-m5-pr1-adrs` off `origin/main` `2cce400`. The `docs/decisions/` guard is lifted
for this session. The pattern to copy is M4 PR 1 (`a3d6071`: ADRs, architecture rev, AGENTS clause, packet,
PLAN line).

## Files

1. **`docs/decisions/0025-hdf5-run-log-and-replay.md`** (new; template `docs/templates/adr.md`, M4 ADR style).
   - Header: Status **proposed**; Owner Joe; Related: elaborates ADR-0005, 0010, 0011, 0022, 0023. It supersedes
     nothing. Two readings are stated rather than superseded: (a) ADR-0005's "plugin versions" is realized as the
     plugin identity from Q9, because the descriptor has no version field; (b) ADR-0022's "M5's logger supersedes
     the recorder" happens in a follow-up after M5 (Q12).
   - Decision, one short paragraph per question:
     - Q5: the config block. `log: {path, buffer_frames}`, default 4096. Absent means no logger object and
       bit-identical behavior. An unknown key, an existing path or an unwritable path is a `ConfigError` naming
       `log.<key>`. Every registered signal is logged.
     - Q3: row timing. Row k is the instant t_k: frame-start state (rigid body and model-owned), the frame-k
       environment sample, the derived signals, the latched controls and the k1 wrench. y(t_N) goes to
       `/final_state`. The disclosed exception is `ax ay az` (ADR-0026).
     - Q4: rings and drain. One ring per rate group. The row is reserved at the very start of `step()`, before
       any side effect, and filled at frame end with no allocation. Drain happens only outside `step()`: `run()`
       at a high-water mark, `flush_log()`, and `close_log()`. `astra.lockstep.Session` drains and exposes both
       calls. `step()` never calls HDF5.
     - Q4/Q7: the halt. `HealthCheck::kLog` is appended after `kTransport`. A full ring halts before any side
       effect, and so does a failed drain write. The halt is sticky, with no `warn`/`off` and no
       `sim.health.log` key.
     - Finalization. `close_log()` is explicit and idempotent, and throws a typed error. It writes `/summary`
       and `/final_state` (frame plus `after_halt`) and sets `complete = 1`. The destructor is best-effort and
       never sets `complete` after a failure. `load` refuses an incomplete file unless `allow_incomplete=True`.
     - Q6: layout. `/rate_<divisor>/frame` (uint64) plus `/values` (float64, rows × columns, chunked,
       uncompressed) with `names`/`units` attributes. Root provenance attributes, plus `/plugins`, `/writes`,
       `/summary` and `/final_state`.
     - Q9/Q10: provenance. The config text is stored without its `log` block. Also stored: git describe from a
       build-time generated TU (`unknown` outside git; it describes the last build), core version and
       `kCoreApiVersion`, and `dt`. The `/plugins` rows hold path, category, name, `core_api_version`,
       `built_with`, and the SHA-256 of the `.so` (or `static`). The descriptor does not change.
     - Q7: determinism. The criterion is byte-identical files: HDF5 time tracking off, fixed library-version
       bounds, the config without `log`, and no nondeterministic value in the file. **Joe's Q7 note:** the
       fallback is written into this record now as a conditional. If PR 2 shows HDF5 cannot give identical
       bytes, the criterion becomes equality of every dataset and attribute. PR 2's evidence and every test that
       uses the fallback must say so. That keeps the accepted record immutable (no later edit).
     - Q8: wall time. None in the file. The Python tools print wall time and the real-time factor to stdout.
       The summary carries per-signal min/max; envelope min/max waits for the envelope monitors.
     - Q11: replay. Re-run from the stored config, the `/writes` (frame, name, value) and the completed frame
       count, checked by byte identity. A halted log replays one more `step()` that must reproduce the halt. A
       log from a run with a `transport` block is refused with its reason. FlightGear playback is a bridge
       source, not replay.
     - Q12: the M4 recorder is kept through M5 and retired in a follow-up.
   - Rejected alternatives: end-of-frame sampling (logged α off by one frame); whole-run preallocation (about
     40 MB per 100k frames, ×64; no RealTime writer); overwriting an existing file; compound per-signal columns;
     wall time in core (breaks ADR-0010); a descriptor version field (ABI change); replay as only "same config
     and seed" (misses writes and halts); `warn`/`off` for `kLog`; replaying lockstep logs; retiring the
     recorder now.
   - Consequences: which plan checks prove it (R2, R4 to R9, R6b) and the PRs that run them (2, 4, 5). This
     record's own check: `docs_check`, `git diff --check`, one Codex review. The costs are stated, and so is
     that whether `kCoreApiVersion` changes is PR 4's decision under ADR-0009.
   - Approval: "Pending Joe's explicit decision."

2. **`docs/decisions/0026-derived-flight-condition-signals.md`** (new, Q13).
   - Context: the derived values exist only inside `icd_pack::pack_sim_to_sw` (`core/include/astra/util/icd_pack.hpp`).
     Scenario checks and the bridge both need them.
   - Decision: for each vehicle with physics, with or without a transport, publish `v<i>.derived.<field>` for the
     22 `astra_sim_to_sw_v1` truth fields plus `v_n_0..2`. They come from a separate `pack_sim_to_sw` call once per
     frame, right after the environment sample and before the exchange and any vehicle `step()`.
     `lockstep_exchange.cpp` is not edited. Units come from the ICD comments (SI). `status_flags` is a double
     holding the bitmask. Invalid fields follow the ICD convention. Timing exception: `ax ay az` come from the
     previous frame, and `PreviousFrame` is supplied only when this `Simulation` completed frame k−1 itself;
     otherwise the invalid bit is set, which always happens at frame 0.
   - Rejected: deriving in Python from the logged state (duplicates the Earth-model and flat/geodetic `p_n`
     math); reusing the exchange's packed struct (it exists only with a transport, on divisor frames);
     computing at frame end (breaks Q3's coherence).
   - Consequences: about 25 signals and one extra pack per physics vehicle per frame. Tests that assert exact
     signal lists are updated deliberately in PR 3. R10's base-vs-candidate recorder SHA-256 must match.
   - Approval pending.

3. **`docs/architecture.md`, rev 9** (status line gains a rev 9 entry: proposed ADR-0025/0026, pending Joe).
   - §5: the per-frame order names the log-row reservation first, then the derived stage after the
     environment sample. The logger sample fills the reserved row with frame-start (t_k) content, citing both
     ADRs.
   - §9: the Logger paragraph gets the `log` block, the ring/drain points, the layout, the config stored without
     `log`, and `kLog`. The Replay paragraph gets the stored writes, the frame count, byte identity, halted-run
     replay and the lockstep refusal.
   - §12: new `astra.log` bullet (load/replay). `astra.scenario`: events are `write_input` only, and the wind
     example is replaced (Q14). `astra.montecarlo`: no wall time in the summary. `astra.fgbridge`: divisor 16
     (62.5 Hz), FlightGear `--native-fdm=socket,in,62.5,,5500,udp`, and wall-clock pacing is Python-only.
   - §15: log overflow and drain failure become `kLog` (terminal, sticky). The post-run summary loses wall time
     and real-time factor and gets per-signal min/max, with envelope min/max deferred and the plugin identity per
     ADR-0025.
   - §16: a note under the table: the M5 row is checked by plan R1 to R21, and the 64-run case runs inside
     `just check`.
   - Consistency edits outside the named sections, needed by Q9/Q12 and disclosed in the packet: §10's
     recorder sentence (retired after M5) and §13's "plugin's version" sentence (the identity per ADR-0025).
4. **`AGENTS.md` line 64** (Q23): "Wall-clock access in `core/`, `plugins/` and `aircraft/` is restricted to the
   `RealTime` pacer (ADR-0010) and the lockstep transport timeout (ADR-0023); it never supplies simulation time."
   The line count stays the same, so AGENTS.md plus CLAUDE.md stays under 100.
5. **`PLAN.md`**: a PR 1 line under M5 (`PR 1 (#NN): ADR-0025/0026, architecture rev 9, AGENTS.md wall-clock
   wording`, with plan and packet links). No merge facts.
6. **`docs/reviews/m5-pr1-adrs-human.md`**: the Tier 2 human packet, via `/human-review`, in the M4 PR 1 packet
   format. It asks Joe to (1) accept or amend ADR-0025/0026, (2) approve the out-of-row §10/§13 edits, and
   (3) authorize a later flip commit (status, Approval and banner only).

## Sequence

1. Recheck the facts the ADRs cite before writing: `HealthCheck`, `HaltReason`, `Simulation::write_input`,
   `vehicle.cpp:507/520`, `lockstep_exchange.cpp:164/182/199/214`, and ADR-0009's API-version rule.
2. Write items 1 to 5. Run `python3 scripts/docs_check.py` and `git diff --check`. Commit, push, then run
   `gh pr create --draft`, with a body holding the brief, tier, scope and pending results.
3. `/review-cycle`: one Codex review of the committed branch diff against `2cce400`. Verify each finding. Fix the
   supported ones, at most two correction rounds, with Codex verifying each fix batch, then rerun both checks.
4. `/human-review` packet (item 6). Put the commands, exit codes, the Codex verdict and thread id, and the
   correction count in the PR body. Commit and push.
5. `/wrap` rewrites STATE.md for the handoff. The ADRs stay **proposed** until Joe decides; the flip is a
   separate commit after he does.

## Verification

- `python3 scripts/docs_check.py` exits 0; `git diff --check` exits 0 (on the final head).
- A grep for British spellings in the diff finds none.
- `wc -l AGENTS.md CLAUDE.md` totals 100 or less.
- One Codex review of the final diff, with its findings resolved or escalated. Its verdict and thread id go in
  the PR body.
- No build or test runs, and no full gate (docs only, per the plan's PR 1 row). This is stated in the PR body.
