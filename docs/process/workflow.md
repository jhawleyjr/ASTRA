# ASTRA Development Workflow (v2, 2026-09-14)

Status: **accepted** 2026-09-14. Replaces the 6DoF-Digital phase/task process without copying it.

## What the research said, in one paragraph

Across Anthropic's current docs, OpenAI's harness-engineering write-up, the martinfowler.com spec-driven-development critiques, and the practitioners who aren't selling a framework (Cherny, Willison, Reed, Osmani), the same shape emerges: **short standing instructions, executable checks, fresh contexts, tiny durable state files.** What consistently rots is ceremony — long specs written before code exists ("writing the program twice"), mandated artifacts per step, and hard preconditions. Anthropic's best-practices page now leads with verification, not planning: "Give Claude a check it can run… It's the difference between a session you watch and one you walk away from," and adds "If you could describe the diff in one sentence, skip the plan." The frameworks that went heavy (BMAD, spec-kit, Superpowers) have all since shipped "lite" modes, which is the tell. Two empirical results matter for us: repository instruction files mostly don't improve task success but do raise cost, *except* for non-standard conventions (ETH, Feb 2026); and agent-written tests are mostly observational rather than verifying (Feb 2026), so **the human owns the oracle**.

## Why the old process was slow — and the design response

| Old 6DoF-Digital | Why it hurt | v2 response |
|---|---|---|
| Task brief = "the entire brief… if it's not in here, it does not exist": verbatim spec excerpts, exact interfaces, complete file list → `scope.json`, named tests with values, wrong-implementations table, gate list, plus 1–2 adversarial review passes — all written in Cowork with no compiler | Front-loaded the hardest thinking to the point of least information. Opus-hours spent on paperwork; a short brief was treated as a defect | A task is **3–10 lines**: intent, acceptance check as a command, pointers to the ADRs that bind it. The agent does its own recon in the repo with plan mode. Detail emerges from code and tests, not prose |
| Seven mandated hops per task across Cowork and Claude Code, each ending in a handoff ritual | Every hop = context rebuild + a `STATE.md` rewrite + a doc sweep | **One surface.** Claude Code on Joe's machine plans, builds, tests, reviews, and commits. Cowork is for design conversations and research, never a gate in the loop |
| Task pinned to a commit SHA; `scope.json` hook; `.claude/current-task.json` arming; amend-only revise rounds; `--no-optional-locks` everywhere | Preconditions that tested *state* instead of *outcomes*. A push to origin broke the run | **Precondition: the worktree is cut from current `main`, which is green by construction (CI-gated). Postcondition: `check` passes and the task's acceptance command passes.** The gate runs once, after the work, never before it. Nothing is pinned by hash except reference data and goldens (which are not allowed to change silently — see rules) |
| Retrospective must produce a diff to skills/rules | Rules accumulated faster than they were tested; drift between tree copy and account copy of skills had to be token-diffed | Rules are added only after something bites, in one place (`CLAUDE.md` or `.claude/rules/`), and reviewed for deletion each milestone |

## The loop

```
  Joe + Claude (Cowork or Claude Code)         Claude Code, one session per task
  ────────────────────────────────────          ─────────────────────────────────────────────
  PLAN.md: milestones as checkboxes    ──►     opener → STATE.md + git log already in context (hook)
  ADRs when a decision is made                        → plan mode: recon + 5-line approach, Joe OKs
                                                      → worktree branch from fresh main
                                                      → implement against the acceptance check
                                                      → Stop hook: fast check must pass
                                                      → /codex:review --base origin/main (other model)
                                                      → commit, push branch, open PR
                                               /wrap  → STATE.md rewritten, lessons appended
  Joe merges (skim physics; trust the gate)   ◄──     
```

**Task size:** one PR, one verifiable outcome, one context window. Examples: "quaternion attitude propagation; torque-free spin keeps |q| and angular momentum within 1e-9 over 1000 s"; "lockstep shared-memory transport; autopilot process steps in lockstep for 10k frames with zero missed frames." If the diff can be described in one sentence, skip plan mode and just do it.

**Fresh context per task.** After two failed corrections in a session, `/clear` and rewrite the opening prompt instead of arguing.

## Files that carry state (the whole set)

| File | Size | Rule |
|---|---|---|
| `CLAUDE.md` (symlinked as `AGENTS.md`) | < 100 lines | One file: build/test commands, the conventions that are *non-standard* (frame/axis/quaternion conventions, units), pointers to the rest, plus Claude Code's own process (worktree, model, hooks, review commands) under its own marked-Claude-Code-only section. `AGENTS.md` is a symlink to it, so Codex reads the same file directly by its own name. Anything an agent would get right without being told is deleted |
| `PLAN.md` | one screen | Milestones as checkboxes with a one-line acceptance each. Edited in place; no task IDs, no phase files |
| `STATE.md` | ≤ 40 lines, rewritten never appended | What's in flight, last verified result, next step, "tried X, failed because Y" |
| `docs/decisions/NNNN-*.md` | one screen each | ADRs: context, decision, **rejected alternatives**, consequences. Immutable once accepted; supersede, don't edit. The rejected options are what stop an agent relitigating settled choices |
| `docs/lessons.md` | append-only | Numerical/build gotchas that bit us. Compound-engineering's "each unit of work makes the next easier" |
| Auto-memory | automatic | Claude Code's own; corrections land here without ceremony |

Kept from the old process because it earned its place: `STATE.md` with the honesty rules ("a trace matched" ≠ "verified"), decision records, acceptance-as-command, and the append-only lessons log. Dropped: `PHASE.md`, per-task `.md` briefs, `runs/<Tid>/{REPORT,REVIEW}.md`, `gates.json`, `scope.json`, `CLOSEOUT.md`, `LOG.md`, and the account-vs-tree skill diffing.

## Verification: the gate, not the spec

One command, **`just check`** (`just` from apt, with CMake presets underneath for the build configurations), is the definition of done and the only thing anyone argues about:

1. Configure + build with clang-18, `-Werror`, ASan/UBSan preset.
2. Unit tests (GoogleTest) and Python tests (pytest).
3. **Physics oracle tests** — the human-owned part. Conservation laws (energy and angular momentum in torque-free motion, quaternion norm), analytic solutions (symmetric-top precession, ballistic arc in vacuum, phugoid period of the linearized notional transport), trim residual, and the NASA 6-DoF check cases (NASA/TM-2015-218675) once the core exists, with JSBSim as a cross-check.
4. Golden-trajectory regression with per-channel absolute + relative tolerances, stored as data.
5. `clang-tidy` and `clang-format --dry-run --Werror`.

Wiring, all deterministic and cheap:

- **Stop hook** runs the fast subset (build + unit tests) and blocks the turn from ending until it passes. Claude Code caps this at 8 consecutive blocks, which is the right amount of stubbornness.
- **PreToolUse hook** denies edits to `tests/golden/**`, tolerance files, and `docs/decisions/**` unless Joe lifts it, two ways: `ASTRA_TOUCH_GOLDENS=1` **on the `claude` launch line** (an `export` inside a running session never reaches the hook, which inherits the `claude` process's environment), or `touch .claude/touch-goldens` while the session runs. The marker counts in **either the session's own root or the main checkout**, so one `touch ~/repos/ASTRA/.claude/touch-goldens` arms a worktree session too -- before 2026-09-16 it did not, because a worktree session's `CLAUDE_PROJECT_DIR` is the worktree and every golden-touching PR runs in one. `disarm-goldens.sh` (SessionStart) deletes both, so a lifted guard never carries into the next session. This is the one hard precondition in the whole design, and it tests the thing that actually matters: *the oracle does not move without a human deciding it should.* Widened tolerance, narrowed comparison, regenerated golden, or a deleted/weakened test is always a blocking review finding.
- **`/goal`** for unattended runs: "all tests in `tests/physics/` pass and `just check` exits 0, or stop after 20 turns."
- **CI**: GitHub Actions on `ubuntu-24.04`; the job is named `check` and `main` requires it. **`GATE: local` since 2026-09-16** (`.github/workflows/check.yml`): `just check` runs on Joe's machine once, after the task and before the push, and CI runs only what is cheaper there than locally -- today the hook cases. `ctest` is ~556 s per preset (the 7 `physics` tests are ~95% of it), so four presets is ~37 minutes a run: it overran the timeout and does not fit the monthly runner allowance while the same tree is tested locally every task. The gate moved, it did not go away, and the run summary says so on every run. `GATE: full` puts it back; do the `physics`-on-asan-only split first (~28 of the 37 minutes). **PRs open as drafts**: the trigger omits `synchronize`, so pushes are free and `gh pr ready` fires one run on the final tree -- and a later push blocks the merge until it re-runs, so the merged SHA is always the tested one.

**Editor-grade feedback between gate runs (not part of the gate):** the `clangd-lsp` and `pyright-lsp` plugins give Claude diagnostics after every edit and semantic navigation (go-to-definition, find-references, call hierarchy) instead of grep. They shorten the loop but never replace `just check` — clangd's diagnostics are not a `-Werror` sanitizer build. Setup lives in M0: `apt install clangd-18` (aliased so the plugin finds `clangd`), `CMAKE_EXPORT_COMPILE_COMMANDS=ON` in every preset with `compile_commands.json` linked at the repo root (without it clangd reports false errors on every Eigen include), `pyright` pointed at the venv via `pyrightconfig.json`, and nanobind `.pyi` stubs generated at build so pyright sees the compiled module's types. One line in `CLAUDE.md` telling Claude to prefer the LSP tools over grep for symbol lookups.

## Review

Review is done by a different model from a fresh context: Codex, via OpenAI's `codex` plugin (Codex reads `AGENTS.md`, the shared conventions file, so it knows them). The session runs it itself from Bash — `node <plugin>/scripts/codex-companion.mjs review --base origin/main --wait` — because the `/codex:review` and `/codex:adversarial-review` slash commands carry `disable-model-invocation` and are only wrappers of that script. Codex's tokens are on the OpenAI plan; Claude spends only what it takes to launch the review and read the result. For **physics-touching** diffs add the script's `adversarial-review --base origin/main --wait` with focus text naming the ADRs and oracles the change must satisfy (signs per ADR-0001, units per ADR-0002, no per-frame allocation, determinism). The plugin's stop-time review gate stays **off**: a review on every turn is the endless-review failure mode, and the Stop hook is for the fast check. Findings are treated as claims to verify against the code, not as instructions (Anthropic's own warning: a reviewer asked to find gaps will find some even when the work is sound).

**Plan review, from M1.** When a plan makes a design choice that architecture.md and the ADRs do not already pin, and being wrong would be expensive to unwind (in M1: the EOM and quaternion kinematics, the integrator interface, the health monitor; not mass properties, flat Earth, or test scaffolding), Claude runs one `/codex:rescue` round *inside plan mode, before the plan is shown to Joe*: the plan text and the acceptance command inlined (Codex cannot see the plan file), with "state what must be true for this plan to satisfy the acceptance command and ADR-XXXX…; check each; report only what fails." One round. Findings that hold are folded into the plan; rejected findings are named in the plan with the reason, so Joe reads the plan and the critique together when approving it. Plans touching only pinned designs skip this. Still never automated on every plan, and never a critique subagent — the trigger above is the whole point: a critique on every plan is how costs run away in the other direction. For **physics-touching** changes — anything under `core/kernel/`, `core/util/` (frames, quaternions, geodesy), `plugins/physical/`, or `tests/golden/` — Joe reads the diff. For everything else, the gate is the review. No separate review session, no `REVIEW.md`, no ACCEPT/REVISE state machine; a review finding is either fixed in the same session or becomes a line in `PLAN.md`.

## Git

- Branch per task via `claude --worktree <name>` (branches from the current remote default, so "start from fresh main" is automatic; edits to the main checkout are blocked by the tool).
- **PRs open as drafts** (`gh pr create --draft`). CI triggers on `opened`/`reopened`/`ready_for_review`, never on `synchronize`, so pushes to a draft cost nothing and `gh pr ready` fires exactly one run against the final tree. A push after that leaves the head SHA without a `check` and the merge blocks until `gh pr ready --undo && gh pr ready` re-runs it, so the SHA that merges is always the SHA that was tested.
- Claude commits, pushes the branch, and opens the PR itself (GitHub is integrated); Joe squash-merges. Commit attribution via `attribution.commit` settings. `main` is protected so nothing lands without the PR.
- No SHAs anywhere in process files. If two worktrees collide, rebase onto main one at a time; git catches textual conflicts, the gate catches semantic ones.

## Where Cowork fits

Cowork sessions are for **thinking that produces documents**: architecture, ADRs, research, milestone planning, and occasional fresh-eyes review of a PR when Joe wants a second opinion untainted by having written the code. Their outputs land in the Project and in `docs/`.

**Optional in-loop use, added 2026-09-16 after M2 PR 1b.** Joe may paste a plan into Cowork before approving it, or take a design question there instead of settling it in the coding session. A convenience, never a required hop: no PR waits on it. It pays when a fresh context reads the question better than the session that just wrote the plan -- in 1b it caught a `constexpr std::pow` clang-18 rejects and a golden with no oracle but its own implementation. One pass, then the session owns it; two hops is the old process growing back.

## Skills (two, small)

- **`/wrap`** — end of session: rewrite `STATE.md`, append to `docs/lessons.md` if anything bit, ensure the branch is pushed and the PR is open **as a draft** (§Git), print the one-line opener for next time.
- **`astra-retro`** — at each milestone, run by Joe **from Cowork** (it was a Claude Code `/retro` skill until M2): list rules that have not fired since the last retro and propose deleting them; list things that bit twice and propose at most one rule to add; propose a process change if the milestone showed one. Produces git-apply patches or nothing, and never commits.

## Model and effort (revised 2026-09-14 after M0; supersedes the 2026-09-14 morning decision)

M0 ran on `fable` at `high`/`xhigh` with subagents inheriting the model. One day cost about 3.7M output tokens and
100M cache-read tokens, roughly $300 of API-equivalent, and Fable burns Max-plan usage about twice as fast as Opus
with a 50%-of-weekly cap. That is not sustainable on a 20x plan and was not buying correctness: nothing in M0-M5 is
a problem no one has seen, and correctness comes from the human-owned oracle, not from thinking tokens.

- **Standing default: `opusplan` at `high`**, pinned in `.claude/settings.json`: Opus 5 in plan mode, Sonnet 5
  once the plan is approved. Anthropic's guidance: use the model's default effort; higher effort generates roughly
  7x the tokens. If the automatic switch does not happen, `/model sonnet` right after approving the plan.
- **Subagents on Sonnet** (`CLAUDE_CODE_SUBAGENT_MODEL=sonnet` in the settings `env` block). No recon subagents on
  a tree this size; the session reads the files itself.
- **Escalation is Joe's, by Anthropic's rule**: if Claude had the context and tried and still got it wrong, the
  model is too small (`/model opus` for the rest of the session, `/model fable` only after Opus has failed);
  if it got it wrong by skipping a file or not running the tests, the effort is too low (`/effort xhigh` for the
  rest of the session). Neither is a standing setting. `max` never.
- **Physics-touching diffs** are checked by Joe reading them and by the oracle tests, not by a higher effort level.
- **Context is the multiplier.** Cache reads scale with context size times turns. Build and test output goes to a
  file with only the tail shown; one PR per session; `/clear` rather than continuing after a merge.
- **Review** is Codex (§Review), on the OpenAI plan, so it costs Claude usage almost nothing.
- Measure: the per-session token script (transcripts under `~/.claude/projects/*ASTRA*/**/*.jsonl`) after each
  PR; `/usage` for the 5-hour and weekly windows.

## Milestones for v1 (seed for `PLAN.md`)

The authoritative acceptance criteria are in `docs/architecture.md` §16, and the live milestone state is `PLAN.md`. The list below is the original 2026-09-14 seed, kept as history: it is **not** maintained, and where it disagrees with `PLAN.md` or §16, they win.

- **M0 Bootstrap** — repo skeleton, CMake presets with `compile_commands.json` exported, `justfile`, `just check` green on an empty core plus one dummy plugin, hooks installed, CI on PRs, `clangd-lsp` + `pyright-lsp` working (nanobind stubs, pyright on the venv), ADR-0001…0011.
- **M1 Rigid body** — EOM + integrators + mass properties + flat Earth; torque-free and ballistic oracles pass; health monitor halts on injected faults.
- **M2 Environment** — ISA atmosphere, gravity, wind/turbulence, selectable Earth (flat/round/WGS-84); NASA check cases 1–10.
- **M3 Notional transport** — aero + propulsion plugins and the notional narrow-body; trim and linearize at cruise; phugoid/short-period match the linear model.
- **M4 Lockstep** — shared-memory transport, straight-and-level autopilot as a separate process outside the sim tree; 10k-frame lockstep with zero missed frames; transport-health halt when the peer dies.
- **M5 Tooling** — HDF5 logging + replay, scenario harness, FlightGear bridge, Monte Carlo runner.

## Decisions on the open items (all decided 2026-09-14)

1. **`just check`** is the single entry point; CMake presets underneath.
2. **Claude pushes branches and opens PRs itself**; Joe merges. GitHub is integrated.
3. **M0 starts directly in Claude Code**, with the architecture doc as its only input.
4. **LSP plugins: yes**, `clangd-lsp` and `pyright-lsp`, set up as part of M0. They are feedback, not the gate.
5. **CI from M0**, GitHub Actions on `ubuntu-24.04`.

## Reusing this elsewhere

The repo is the documentation -- copy the files and edit them. What is generic and what is ASTRA:

- **Generic**: `.claude/hooks/` (all five, plus `tests/run.sh` and the `.cases` files), `.claude/settings.json`,
  `.github/workflows/check.yml`, `.github/rulesets/main.json`, `CLAUDE.md` symlinked as `AGENTS.md`, and the four
  state files (`PLAN.md`, `STATE.md`, `docs/decisions/`, `docs/lessons.md`) as empty templates.
- **Change per project**: the gate command and the `justfile` behind it; `guard-oracle.sh`'s guarded-path regex
  (what counts as the oracle here); `stop-check.sh`'s fast subset; `session-start.sh`'s printout; `check.yml`'s
  `GATE`, scope allow-list and toolchain step; every word of `CLAUDE.md`.
- **Do not port the numbers.** The token measurements, the 556 s, the preset list are this project's. Port the
  habit of measuring instead.

What must survive the port, or it is the shape without the substance: one command is the definition of done;
the human owns the oracle and a hook enforces it; state lives in a few small files rewritten not appended; and
"verified" means a command ran and exited 0 in this session.

## Sources

- Anthropic best practices, `/goal`, hooks, worktrees, memory, code review: https://code.claude.com/docs/en/best-practices · https://code.claude.com/docs/en/goal · https://code.claude.com/docs/en/hooks-guide · https://code.claude.com/docs/en/worktrees · https://code.claude.com/docs/en/memory · https://code.claude.com/docs/en/code-review · https://code.claude.com/docs/en/desktop
- LSP plugins: https://claude.com/plugins/clangd-lsp · https://claude.com/plugins/pyright-lsp · https://karanbansal.in/blog/claude-code-lsp/ · https://www.amazingcto.com/lsp-in-claude/
- Anthropic engineering: https://www.anthropic.com/engineering/effective-harnesses-for-long-running-agents · https://www.anthropic.com/engineering/harness-design-long-running-apps · https://www.anthropic.com/engineering/building-c-compiler · https://claude.com/blog/the-new-rules-of-context-engineering-for-claude-5-generation-models
- OpenAI harness engineering: https://openai.com/index/harness-engineering/
- Spec-driven development critiques: https://www.martinfowler.com/articles/exploring-gen-ai/sdd-3-tools.html · https://ranthebuilder.cloud/blog/i-tested-three-spec-driven-ai-tools-here-s-my-honest-take/ · https://addyosmani.com/blog/agentic-engineering/
- Framework pain reports: https://github.com/obra/superpowers/issues/1260 · https://dev.to/arch4g/my-experience-using-the-bmad-framework-on-a-personal-project-patience-required-28aa · https://www.pulumi.com/blog/claude-code-orchestration-frameworks/ · https://ralphloop.sh/blog/ralph-loop-failure-modes
- Practitioners: https://infoq.com/news/2026/01/claude-code-creator-workflow/ · https://harper.blog/2025/05/08/basic-claude-code/ · https://simonwillison.net/2025/Oct/7/vibe-engineering/ · https://every.to/chain-of-thought/compound-engineering-how-every-codes-with-agents · https://towardsdatascience.com/governed-context-managing-context-rot-in-claude-code/
- Evidence on instruction files and agent-written tests: https://arxiv.org/abs/2602.11988 · https://arxiv.org/html/2602.07900v1 · https://htek.dev/articles/vibe-testing-when-ai-agents-goodhart-your-test-suite
- Numerical validation: https://arxiv.org/html/2605.09360v1 · https://arxiv.org/html/2512.21373 · https://ntrs.nasa.gov/citations/20150001263 · https://github.com/JSBSim-Team/jsbsim · https://github.com/mpusz/mp-units
