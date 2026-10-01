# Plan: a docs step in /wrap, and a repo-scope docs sweep

Status: approved by Joe in plan mode, 2026-10-01, with one change: the stale `Eom` comments in `core/`
are fixed here rather than left as a follow-up. Tier 2 (workflow behavior), set by Joe 2026-10-01.
Branch `worktree-wrap-docs-sweep`, base `origin/main` `bca4be0` (includes #87: checked).
Conventional name at commit: `docs/plans/wrap-docs-sweep.md` (`mv` this draft, then commit it).

## Context

Docs drift because nothing in the workflow requires them to be brought current. build.md step 5 says
only "Update docs that explain changed behavior", and `/wrap` never looks at docs. A read of today's
docs against the tree confirms the drift (findings below): architecture still marks M6 PRs 2 to 4
*planned*, docs/refs/README.md says the NESC generator is "not yet in the tree", and so on. The goal
is that every `/wrap` leaves the documentation current. This PR adds the step and runs it once, in
repo scope, over the whole repo.

## Changes to the workflow surface

1. **`.claude/skills/wrap/SKILL.md`**: a new step 1, **Docs**, in "Update the handoff artifacts".
   Placing it first puts it before the PLAN.md and STATE.md steps, as asked, and also before the PR
   description, which then carries its result. Contents:
   - *Branch scope* (default): from the diff against the base plus the working tree, list everything a
     doc could describe: files, commands and flags, config keys, signal names, Python APIs, plugins,
     behavior, gate contents and workflow rules. Search the repo (docs and code comments) for each one
     and read what each hit says. Always re-read README.md's features, quick start, how-tos, layout and
     roadmap; docs/README.md; PLAN.md's open items; docs/future-work.md; and architecture.md's
     *planned* markers.
   - *Repo scope* (when Joe asks, and at every milestone closeout): read every current doc against the
     tree: the twelve paths Joe listed, plus code comments that point at milestones, PRs or future work
     ("planned", "not yet", "later", "TODO", "M6 PR").
   - *Either scope*: verify each statement against the code, never against another doc. Fix every
     false, stale or incomplete statement in this PR. Joe's documents (AGENTS.md, CLAUDE.md,
     workflow.md, architecture.md) are edited and each edit is listed under "Needs Joe's decision".
     Historical records stay as written (docs/plans/**, docs/reviews/**, docs/specs/**, accepted ADRs,
     with a contradicted ADR becoming a superseding-ADR item for Joe, docs/lessons.md, which is append
     only, and tests/golden/**). Close or update a PLAN.md open item only when its condition holds.
   - Record the result in the PR body's "Docs impact" section. A blocked or mid-task wrap runs branch
     scope on the work so far and says the result is partial.
   - Docs edits are part of the candidate: rerun `just docs-check`. Mark the full gate stale if an edit
     touches a file the gate builds or tests (a code comment counts).
   - Small consistency edits in the same file: the intro's "does not start a new implementation loop"
     sentence exempts this step's fixes. The PR-description step names the Docs impact section.
2. **`docs/templates/pr-description.md`**: a `## Docs impact` section between Changes and Checks.
   It covers the scope run (branch, repo, or partial), and either each stale statement with its fix,
   grouped by file, with Joe's documents marked, or "none affected".
3. **`docs/agents/build.md` step 5**: replace "Update docs that explain changed behavior" with a
   pointer to `/wrap`'s docs step.
4. Consistency, since these docs describe `/wrap` or the PR body and would otherwise go stale:
   - `.claude/skills/human-review/SKILL.md` gets a Docs impact bullet.
   - `docs/agents/claude-code.md`'s `/wrap` bullet mentions the docs step.
   - workflow.md's Coordination paragraph on `/wrap` mentions the docs step (one clause; Joe's doc).

## Repo-scope sweep: findings (verified against the tree in planning; recheck each while editing)

Joe's documents are marked **[Joe]**. Each fix is the smallest edit that makes the statement true.

**docs/architecture.md [Joe]**
- §9 Config and Finalization, §9 Replay, §12 `astra.log` bullet: "(M6 PR 2, *planned*)". This is
  built (`core/src/kernel/run_log.cpp`, `python/astra/log.py`). Remove the markers. The Config
  bullet's "(PR 2 chooses)" is now settled: PR 2 holds the parent directory as a descriptor.
- §12 Status line says "none is built yet". PRs 2 to 4 are built. Name them, and keep *planned* on
  PRs 5 to 8.
- §12 Units bullet: "(M6 PR 4, *planned*)" is built (`python/bindings/_core.cpp`, `test_units.py`).
- §12 `astra.cli`: "(M6 PR 3 onward, *planned*)". `run` and `trim` are built; `plot`, `mc` and
  `report` are still planned.
- §15's exit-code table is marked "(M6 PR 3, *planned*; no `astra` command exists yet)". Remove the
  marker and add 70 (`EX_SOFTWARE`) and 74 for a failed write of the summary to stdout, matching
  `python/astra/cli.py`. This satisfies PLAN.md's (M6) markers open item, which is then removed (see
  PLAN.md below).
- §12 has no `astra.lockstep` bullet (`python/astra/lockstep.py`, M4 PR 6). Add one line.
- §12 `astra.scenario`: the example syntax (`at: 30.0` with a `write_input` name; "altitude within
  30 m ... for t > 60") does not match `scenario.py`'s schema (`{at, write, value}`,
  `{signal, window, <kind>}`). Restate it in the real schema.
- §13 claims "every key and value identical to the shipped file", but `cruise.yaml` now carries the
  trimmed `initial_controls` (elevator 0.026674…, throttle 0.498597…), `v_b` and `euler_nb`. Sync
  those values; comments may stay trimmed.
- §4 tree:
  - `.claude/` lists "rules", which does not exist.
  - `python/astra/` omits `lockstep.py` and `cli.py`.
  - `tools/` claims "CLI runner, fg bridge", but both live in `python/astra/` and `tools/` does not
    exist. Keep it as a later path, without the built items.
  - notional_transport configs list 2 of the 4.
  - `plugins/physical/` omits `wind/table` and `mass/constant`.
  - `kernel/` lists "EOM"; the class is `RigidBody6DofDynamics` (ADR-0021).
- §14 and §15's per-frame path names `compute`, which ADR-0020 removed. It is `wrench_b`.
- §15 cites `tests/physics/test_no_alloc`; the file is `tests/physics/no_alloc_test.cpp`.
- §15's last paragraph says `tests/unit/health/` feeds a dead peer and an overrun into a stub sim.
  `health_monitor_test.cpp` has neither. The transport halts are tested under `tests/lockstep/`
  (name the file), and the overrun waits for `RealTime` (M11).
- §15 pre-run: "Trim residual (when `initial.trim` is used)". No such key exists (trim is Python);
  mark it *planned*, with no milestone (Joe's call).
- §7 "Control inputs in v1": "ideal actuator ... until M4" should say until M8's actuator models
  (PLAN.md M8).
- §10 Exchange recorder: "Until M5's HDF5 logger exists" / "a follow-up retires it" should point at
  PLAN.md's M10 open item, since the logger exists.
- §16 M6 note: "the M6 row is *planned*" should say PRs 1 to 4 are built and the rest is planned.
- Check while editing: §11's "~200-line" autopilot (the `.cpp`/`.hpp` total is 645 lines), and
  §18 step 5's `transport.name` against the transport block's real keys.
- Left as is, and reported: §18 step 4, `astra trim` on a template copy. It states the requirement,
  and PLAN.md's (M7) open item tracks the bug. §19's `Eom::begin_frame()` is in an open question
  that ADR-0021 frames.

**AGENTS.md [Joe]**
- "State vector (ADR-0003): 13 doubles, fixed layout" is true of the rigid-body block only. ADR-0021
  makes the buffer runtime-sized. Amend it on the same line, keeping the 100-line budget.
- Per-frame path `compute` should be `wrench_b` (ADR-0020).

**docs/process/workflow.md [Joe]**: only the `/wrap` clause above. I found nothing else false:
`.claude/settings.json`, the hooks and `check.yml` match.

**docs/process/validation.md**
- The check-full row omits `docs-check`, which `just check` runs.
- The docs-check scope omits README.md, docs/README.md and docs/reviews/README.md
  (`scripts/docs_check.py` covers them).
- "the planned separate embedded example is C++17": it is built (M4).
- "`step`/`compute`/..." should name `wrench_b`.

**docs/refs/README.md** (contents checked by `ls` of the main checkout's `docs/refs/`)
- The NESC Vol I, Vol II and trajectories entries say `generate_nesc_band.py` and the case YAMLs are
  "not yet in the tree" or "once it lands". They exist (`tests/golden/nesc/`, `tests/physics/nesc/`).
- Missing entries, each with its citers:
  - `aircrafts/737.xml` (notional_transport aero, turbofan and configs).
  - `net_fdm.hxx`, `native_fdm.cxx` and `native_structs.cxx` (`fgbridge.py`,
    `tests/golden/flightgear/fgnetfdm_v24.py`, `scripts/fgnetfdm_layout_check.py`).
  - `textbooks/` (Stevens & Lewis, Zipfel, Nutaro, Groves, Titterton, MIL-HDBK-1797, Moin,
    Numerical Recipes, `wgs84fin.pdf`).
  - `astra_framework_intent_and_state_audit.md` (ADR-0019).
- README.md says this file "lists each document and what cites it", which is true only after this
  fix.

**docs/future-work.md**
- "Architecture §1 lists visualization as already present": rev 10 marks it *planned* for M6.
- Documentation: "nothing walks through it yet". README's "Add an aircraft" and "Write a plugin"
  how-tos now sketch the on-ramp, but the tutorial (M7) is still unwritten.
- Workflow "Plan approval": #83 already commits plan-mode drafts with an awaiting-approval status
  (via `/wrap` and claude-code.md). `/plan-feature` itself is unchanged. Say what holds.
- Check while editing: the `summary.parquet` contents against `montecarlo.py`.

**docs/agents/claude-code.md**: "this PR's own session" in the plan-draft bullet meant #87. Name it.

**PLAN.md**
- Remove the (M6) architecture-markers open item; its condition holds after the §9/§12/§15 fixes
  above. Note that this is ahead of the PR 10 closeout Joe had assigned it to (a Joe decision).
- Add this PR's line under M6.
- All other open items' conditions were checked and still hold, for example: `capture_r1_golden.cpp`
  still includes `eom.hpp`, `trim.py` keeps its literals, `OPTIONAL_GAIN_NAMES` remains, and
  `wallclock_scan.py` lacks `epoll_pwait2`. Leave them.

**Code comments** (comment-only; no behavior change)
- `.github/workflows/check.yml` header: "local (current, M2-M3)" and "today that is nothing but the
  hook cases". It also runs docs-check, and the setting is still current.
- `scripts/docs_check.py:31`: "`just new-aircraft` not yet in the justfile". It exists.
- `tests/unit/wind/wind_determinism_test.cpp:19`: "There is no logger until M5". Restate it as
  history (the logger exists since M5).
- `core/` comments that speak of the retired `Eom` as current: `simulation.hpp:310`,
  `simulation.cpp:347,708`, `environment_sample.hpp:3`, `mass_properties_model.hpp:19`,
  `wind_model.hpp:58`. Restate them in terms of `RigidBody6DofDynamics` / ADR-0021 (Joe, at plan
  approval: comment-only edits are Tier 0 even in core paths, precedent #74). Comments that name
  `Eom` as history ("was `Eom::operator()` before this ADR") stay.

**No change found**: README.md (rechecked against `cli.py`, the configs and the tree while editing),
CLAUDE.md, docs/README.md, docs/agents/README.md, review.md, review-dispatch.md, docs/templates/*
apart from item 2 above, plan-feature/review-cycle SKILL.md.

## Sequence

1. `mv` and rename this draft, then make items 1 to 4 above. Commit, push, and open the draft PR.
2. Apply the sweep fixes file by file, re-verifying each against the code. Commit them in coherent
   groups: Joe's docs, other docs, code comments, PLAN.md. Push.
3. Checks: `python3 scripts/docs_check.py`, `git diff --check`, then `rm -rf build` and
   `./scripts/check-full.sh` in the background, capturing the exit code and `check: OK`. Commit
   `docs/reviews/evidence/<head>-gate-summary.log`.
4. One Codex `adversarial-review --base origin/main` pass on the final diff
   (`docs/agents/review-dispatch.md`, focus via a quoted heredoc). Ask it to check each doc fix
   against the code. Triage the findings within the Tier 2 budget.
5. `/wrap` with the new step in branch scope over this PR's diff; put its result in Docs impact.
   Write the PR body per the template: the findings grouped by file, Joe's documents marked, and the
   guard refusal noted (a planning `grep` with `--exclude-dir=.git` was refused; I reran it as a
   plain `grep`).

## Verification

- `python3 scripts/docs_check.py` exits 0 with `docs-check: OK`; `git diff --check` is clean.
- `./scripts/check-full.sh` from a fresh configure exits 0 with `check: OK`; I commit the trimmed
  summary.
- Codex: the verdict, thread id and findings, with each disposition, go in the PR body.
- Read-back: `gh pr view --json body` shows Changes, Docs impact, Checks, Codex review, Needs Joe's
  decision (each [Joe] edit, the early removal of the PLAN.md item, and any rev-11 note for
  architecture) and Follow-ups.
