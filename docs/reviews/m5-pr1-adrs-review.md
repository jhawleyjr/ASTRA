# Review: M5 PR 1, ADR-0025/0026 and architecture rev 9

Spec/plan: [docs/plans/m5-tooling.md](../plans/m5-tooling.md), PR 1 row (Q3 to Q13, Q23)
Builder: Claude Opus 5.5
Reviewer: Codex (companion 1.0.6, `adversarial-review`, fresh context, read-only)
Scope: committed branch diff `2cce400..a63568d` (ADR-0025, ADR-0026, `docs/architecture.md`, `AGENTS.md`,
`PLAN.md`); the worktree was clean
Validation: `python3 scripts/docs_check.py` exit 0; `git diff --check` exit 0; no build or test (docs only)
Round: fix 2 (F1 fix 1, Codex-verified; J1 fix 2, Joe's own review)

## Finding [F1]

Severity: major (Codex: medium; a contract conflict inside an approved decision)
Status: verified
Location: `docs/decisions/0025-hdf5-run-log-and-replay.md` Replay paragraph (lines 93 to 97 at `a63568d`)
Requirement/ADR: plan Q4, Q7, Q11; ADR-0025 Failure, Provenance and Replay
Failure: replay requires every halted log to reproduce its halt with one more `step()`. The stored config
omits `log`, so `buffer_frames` is not recorded, and neither is the drain schedule. Example: a `step()`-driven
run with `buffer_frames=2` and no `flush_log()` halts `kLog` before frame 2. The replay runs with the default
4096 rows and completes that frame. A failed drain write cannot be reproduced from the config and writes at all.
Requested outcome: Joe decides between refusing `kLog`-halted logs and recording enough metadata to reproduce them.
Builder response: verified against the ADR text. Joe decided on 2026-09-27: refuse, with a named reason. Changes:
- ADR-0025 Replay: refuses a `kLog`-halted log and states that every other halt still replays. A rejected
  alternative (recording `buffer_frames` and the drains) was added, and R9's validation line now names the
  refusal.
- `docs/architecture.md` §9 Replay: the same refusal.
- `docs/plans/m5-tooling.md`: the replay contract, R9, and a Q11 entry under "Joe's decisions".
Verification: Codex `adversarial-review` of `a63568d..24dfe55`, verdict approve, thread
`01a0e1ad-dd41-7b90-a539-beb2280a725e`: F1 fixed consistently in ADR-0025, §9 and the plan; new findings: none
History: initial review, open; fix 1, fixed-unverified; verified
Human exception if needed: not an exception; this is Joe's requirement decision (2026-09-27)

## Finding [J1]

Severity: minor (Joe's review, 2026-09-27)
Status: verified
Location: `docs/decisions/0025-hdf5-run-log-and-replay.md` "Buffering and drain" (at `24dfe55`)
Requirement/ADR: plan Q3; ADR-0025 Row timing and Context
Failure: "fills it at frame end by copying through registry pointers" contradicts row timing. The published
`v<i>.state.*` point at end-of-frame state (ADR-0025 Context), so a pointer copy at frame end would log
t_{k+1} state in row k.
Requested outcome: say the row holds t_k values. State comes from an allocation-free frame-start snapshot; the
other signals come through their registry pointers, which hold frame-k values at frame end. The mechanism stays
PR 4's, checked by R3.
Builder response: fixed with Joe's wording. Checked against the registrations at `2cce400`. Only the state
signals, meaning `v<i>.state.*` and the model-owned state slots, point into the end-of-frame stage buffer. The
others are written once per frame and still hold frame k's values at frame end: the latched `controls.*`, the
k1 `wrench_total`, `mass.mass`, the frame-sampled `wind.*`, and the health and transport counters.
Verification: Joe verified it (his decision on PR #63, 2026-09-27); no new Codex pass, at his direction
History: correction round 2 of 2; fixed and verified

## Completion

Initial pass: one finding (F1), no others. After fix 1, F1 was verified by Codex. J1 came from Joe's own review and was fixed in round 2, the last of two. Critical/major open: 0. Scope not reviewed by Codex: the J1 wording, which Joe verified.
