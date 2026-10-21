# M7b PR 4: oracle (c), mirror and heading symmetry of the F-16 under F16_control

Status: **approved by Joe, 2026-10-20, on his word.**
- He accepted decisions 1 to 5 as recommended. Decision 1 was superseded the same day by his R12 answer, a dated
  deviation (§8).
- His amendments 1 to 8 are folded in before the plan's first commit ("Joe's decisions", below).
- His second approval (2026-10-20) accepted the plan as revised, including the state-1 correction and §8's
  deviation as recorded, and added amendments 9 to 11, also folded in before the first commit.
- Nothing of PR 4 is built or run.
- The Codex plan critique ran on 2026-10-20, with verdict rework. Its four findings, P-01 to P-04, are accepted,
  and their proposed resolutions await Joe's decision before the first flight (§18).
- This file was moved (`mv`, before its first add) from the main checkout's plan-mode draft into the worktree as
  `docs/plans/m7b-pr4-f16-symmetry.md`. Nothing stays in the main checkout.

Owner: Joe. Builder: Claude, one builder.
Tier 3: the third in-house oracle under ADR-0034, with two hooks in shipped Tier 3 files
(`aircraft/f16/physical/aero` and `aircraft/f16/sim/transport/nesc_control`), and pre-registered by-value and
yardstick bounds.
Base: `origin/main` = `fe922b0` (#124, PR 3 squash-merged), read from the local ref in plan mode. Step 1 of §16
confirms it with `git fetch`. STATE.md's step 2 names PR 4.
- **The PR 3 worktree is already gone.** `git worktree list` shows only the main checkout, `git worktree prune
  --dry-run` prints nothing, and `git branch --list 'worktree-m7b-pr3*'` is empty. Step 1 only confirms this.

Binding: ADR-0001, ADR-0004, ADR-0020, ADR-0021, ADR-0028, ADR-0029 (Decisions 1, 2, 7 and 8), and ADR-0034
(Decisions 1 to 8, the relation-as-reference clause of Decision 2, and its Consequences).
Sources: [the M7b plan](m7b-closed-loop-oracles.md):
- oracle (c) in full, and repository findings 2 and 3;
- Q9 to Q12 as decided, amendment 1, PR table row 4, and R10 to R16;
- the critique's C-01, C-04 and C-05, and the verification's N-01, with its note that `BuildUp`'s fields are
  mutable ([critique record](m7b-plan-critique.md)).

[PR 3's plan](m7b-pr3-autopilot-peer.md) gives the pattern: §1 for the gate record, §5 for the build facts, §14
and §16 for the stop hook's early run (lesson 2026-10-18), and §16 and §17 for the critique and first-run records.
[PR 11a's plan](m7-pr11a-control-peer.md) gives the peer, `Options`, the variant pattern (amendment 1: no variant
code in a shipped `.cpp`) and the tracked-config guard.

## Context

Oracle (c) checks the F-16 under `F16_control` against its own mirror image and its own heading-rotated image.
- **The mirror pairs.** On a non-rotating sphere, 13.3's course change and 13.4's side step are flown to the right
  and to the left of a meridian. Under the meridian mirror every quantity keeps or flips its sign, and none is
  permuted. Where every operation is sign-symmetric, the two runs are equal by value at every frame.
- **The variant pairs** remove the model's two known asymmetries: Ω, through `Options::earth_rate`, and the four
  signed-β tables, through `build_up`'s parameter. They must be equal by value.
- **The shipped pairs** keep both asymmetries. Their gate is the evaluation-level decomposition (§8): at every
  recorded state and exchange, the shipped evaluations may differ from the symmetric ones only by the four
  tables' signed-β reading and by Ω. Their trajectory asymmetry is reported against the inventory's static
  estimate.
- **Heading invariance.** On a flat Earth, 13.3's maneuver is flown at three headings, and each rotated run must
  match the base heading within Q12's measured yardstick.
- **The five controls** each break the check they are registered to break.

ADR-0034's Consequences list what this plan fixes before the first flight: the reference and its independence
argument; the shared inputs and what each cannot see; each bound, with its kind and its derivation; the
comparison set; and each control's named object.

**The first flight** is the first run that produces compared output of the path under test. The inventory
evidence, the parity probe, the trim's own figures and R15's before-and-after of the shipped configuration are
facts about the model, the platform and the build, and they may precede it (Joe's brief, item 2).
- A nominal miss is a defect until shown otherwise. A stated roundoff or libm cause goes to Joe.
- Nothing registered here moves after the first flight without Joe (ADR-0034 Decisions 3 and 7).

## Joe's decisions (2026-10-20)

**Decisions 1 to 5, accepted as recommended:**
1. ~~R12's bound is a formula fixed here and evaluated on the judged run's recorded operands.~~ Joe accepted it on
   this reading: the bound is the formula, κ, F_f, the steps and the envelope fixed in the plan, and the first
   flight supplies only R's recorded operands and the resulting max |ê_f|, which are reported.
   - **Superseded, 2026-10-20 (Joe), by his R12 answer.** The gate is now the evaluation-level decomposition, by
     value (§8). The first-order predictor, κ, F_f, the difference steps and the |β|, α and rate envelope are
     dropped.
   - The reason is amendment 4's estimate: the saturation reversal (§8).
2. **Q12's perturbation set and its "largest divergence"** (§9).
   - "One ulp" of an exactly zero component (x and z at ψ0 = 0) is ulp(1) = 2⁻⁵², not the smallest subnormal,
     2⁻¹⁰⁷⁴. At the literal reading those two runs move the lateral channels by about 1e-323, while every rotated
     run carries about 1e-17 of attitude roundoff from frame 0.
   - "The largest divergence among those runs" at a record is the maximum over the eight runs and over every
     record up to and including that one: a running maximum.
3. **Heading invariance's IC** (§9). It is the one trim result's elevator, throttle, θ and v_b on the flat Earth,
   with ω_b = 0 and `gravity/constant`'s default g, in every heading run.
   - ω_b is 0 because a flat Earth's n frame does not rotate (`trim.py:19`).
   - The small off-trim start is the same, rotated, in every run.
4. **C-01's bound takes one constant as an assumption** (§5). The LAPACK Users' Guide (§4.5) gives the
   least-squares drivers' backward error as p(n)·ε, with p(n) "a modestly growing function of n". This plan takes
   p = m·n = 12 for the 4×3 solve. Every other term is counted from the cited lines.
5. **R15's "one build"** (§11): one libastra_core build, with `git_describe` equal in both passes, as PR 3's O11
   reads it.
   - The "before" pass loads C1's builds of the two hooked plugins from a scratch copy of the plugin directory.
   - Both passes run in one process at one clean worktree state.

**"The first commit"** of the brief's item 2 is C1, the first code commit, after the plan's own commit.

**Amendments 1 to 8** (Joe, 2026-10-20), each folded into the section named:
1. The wrap control's first differing record, with its derivation (§10).
2. The tables-only and sign-drop controls' registered window and expected fields, with their derivation (§10).
3. The predictor's request-field predictions (ax, ay, az, vertical_speed).
   - **Lapsed with the predictor** (decision 1's supersession).
   - The trajectory report (§8) covers every request field by its recorded values, these four included.
4. A per-channel estimate for κ (§8). It found a saturation reversal no first-order bound can absorb, and Joe's R12
   answer followed from it.
5. The heading base run on the same route as the eight perturbed runs, plus a pre-flight bitwise assertion (§9).
6. The citation that makes Q10's mask rule true (§2).
7. The eight perturbed runs flown once, in one heading test, and a per-frame basis for the release time estimate
   (§1, §12).
8. R15's log records that `git diff C1..C2 -- core icd` is empty, and names both plugins' SHA-256 for each pass
   (§11).

**Amendments 9 to 11** (Joe's second approval, 2026-10-20):
9. The doubled control's margin is stated at its registered object (§10): the extra cl1 and cn1 against ulp(cl)
   and ulp(cn), and Δṗ and Δṙ against ulp(ṗ) and ulp(ṙ). The odd part against ulp of the table entry is only an
   intermediate.
10. The wrap control's re-evaluation uses the same Inputs, Modes, Trim and References the shipped `publish` used,
    with the route by which the variant reaches each (§2). chi_err need only saturate, not equal ±1.
11. §8's report defines a reversal, and pairs R's and L's reversal exchanges in order, with the count on each side.

**Joe's R12 answer (2026-10-20):** option 1, the injection-level check, by value, recorded here as a dated
deviation (§8).
- The doubled control's registered first failure is part (c).
- **One correction, made here and flagged for Joe.** Joe predicted the first failure at frame S + 1, which holds
  where the prefix is lateral-zero. The doubled control flies with Ω on, where N-01 excludes that prefix: frame 0's
  reply already carries about 0.0055° of aileron (§6). So the first state with β ≠ 0 and a nonzero latched aileron
  is state 1, and §10 registers that.

## 1. The gate record (item 1): no new base measurement

PR 3's gate was 1187 s on `a459cd9`, and PR 2's was 1178 s on `0bbc4fa`. PR 4 measures only its own full gate,
after the first flight, in PR 3's §1 form:
1. **Load check** (lessons 2026-10-02 and 2026-10-04):
   - run `pgrep -af '^just (check|test|build)'`, `pgrep -af 'python[0-9.]* -m [p]ytest'` and
     `pgrep -af 'bin/[p]ytest'`;
   - exclude the check's own shell and its ancestors, and `readlink /proc/<pid>/cwd` for each remaining hit;
   - ignore the other project's `just autopilot` dispatcher;
   - `ps -C clangd -o pid,pcpu,etime` must show clangd idle;
   - record `/proc/loadavg` and `nproc` before and after.
2. `just venv` if the worktree has no `.venv`, untimed. The record says whether it ran.
3. `rm -rf build && ./scripts/check-full.sh` from the clean worktree at the gated head, through the Bash tool's
   `run_in_background`, timed. It passes only on exit 0 plus its `check: OK` line.
4. Record:
   - the wall time and its delta against 1187 s (`a459cd9`), 1178 s (`0bbc4fa`) and 1001 s;
   - per-preset CTest times and counts against PR 3's gate (debug 111.01 s, release 11.96 s, static-plugins
     228.97 s, asan 427.26 s);
   - pytest against 151.80 s (2160 passed, 5 skipped);
   - PR 4's own tests per preset, from the log.

**The estimate, and its per-frame basis** (amendment 7).
- **The basis.** In the last committed full log with per-test release times (`d4c0aeb-full-gate.log`, PR 11a),
  `NescF16.Case13p3ClosedLoop` took 0.07 s for 30,001 frames and `Case13p4ClosedLoop` took 0.13 s for 60,001.
  That is about 2.2 to 2.3 µs per F-16 frame in release, with PR 9's harness included.
- **The release estimate.** PR 4's release flights total about 1.08 M frames, about 2.5 s at that rate. §8's
  evaluations add about 0.72 M `Simulation::derivative` calls: four per recorded state, over the shipped pairs'
  180,004 states. The doubled control evaluates only through its first failure. At an assumed 1 µs per call, that
  is about 0.7 s.
- **Total:** about 3.2 s of release test time, about the M7b table's "about 4 s".
  - The earlier draft's "about 10 s" is withdrawn; it had no basis.
  - Under `--parallel`, the release wall time is set by the longest test, about 1 s.
- **The other presets:** the smokes and the probe stay under 10 s in each. pytest is unchanged, with no new
  pytest.
- The measured times go in this record and the PR body.

The full log stays outside git. Its trimmed summary is committed as `docs/reviews/evidence/<head>-gate-summary.log`.

## 2. What is built

Code goes only where the brief names. There is no `core/`, `icd/`, `embedded/`, golden or tolerance change.
`kCoreApiVersion` stays 11, F16Aero's check data is unchanged, and nothing under `aircraft/f16/tests` changes (R15
needs no edit there).

| File | Content |
|---|---|
| `aircraft/f16/sim/transport/nesc_control/control_law.hpp` | Hook 1, the law's half: `enum class EarthRate { kWgs84Rotating, kNone }`; `inputs_from(frame, EarthRate = kWgs84Rotating)`; `refusal_bits(frame, mask = kRefusedStatusBits)` |
| `aircraft/f16/sim/transport/nesc_control/nesc_control_peer.hpp` | Hook 1, the peer's half: `Options::earth_rate` (default `kWgs84Rotating`) and the refused mask (Q10). Its header comment names the option |
| `aircraft/f16/physical/aero/aero_math.hpp` | Hook 2: `enum class BetaTables { kSigned, kAbsolute }` and `build_up(in, BetaTables = kSigned)` (Q11). The `:86` comment is updated from "M7 PR 13" to M8's envelope item (ADR-0031) |
| `tests/plugins/nesc_control_variant/nesc_control_variant.cpp` | Three new `test_variant` names: `default_options`, `earth_rate_none` and `earth_rate_none_unwrapped_track_error` |
| `tests/plugins/f16_aero_variant/{CMakeLists.txt, f16_aero_variant.cpp}` | New. Modes `signed_tables`, `abs_beta_tables`, `abs_beta_tables_cl0_unsigned` and `signed_tables_doubled_odd` |
| `tests/physics/nesc/f16_symmetry.hpp` | The oracle's config text and its switches, the masks, the run helper, the record comparisons, and §8's evaluators |
| `tests/physics/nesc/f16_symmetry_probe_test.cpp` | R10's parity probe and the trim checks (every preset) |
| `tests/physics/nesc/f16_symmetry_smoke_test.cpp` | The smokes and the variant pins (every preset) |
| `tests/physics/nesc/f16_symmetry_mirror_test.cpp` | The variant pairs, the shipped pairs, and the mirror and doubled controls (release) |
| `tests/physics/nesc/f16_symmetry_heading_test.cpp` | Heading invariance and the wrap control, in one test (release) |
| `tests/physics/CMakeLists.txt` | Two executables, both labeled `physics` (§12) |

**Hook 1** (Q10), in the shipped header only, after PR 11a's `Options` precedent (`nesc_control_peer.hpp:72-79`):

```cpp
// control_law.hpp
// What the law subtracts from ICD v1's inertial body rate (ADR-0029 Decision 7; the M7b plan's Q10).
enum class EarthRate : std::uint8_t {
  kWgs84Rotating,  // geodesy::kOmegaIe, under any Earth model (the shipped peer; the M7 plan's Q4)
  kNone,           // nothing: tests/plugins/nesc_control_variant only (M7b PR 4)
};
[[nodiscard]] inline Inputs inputs_from(const astra_sim_to_sw_v1& frame,
                                        EarthRate earth_rate = EarthRate::kWgs84Rotating) noexcept {
  const Eigen::Vector3d omega_eb = earth_rate == EarthRate::kWgs84Rotating
                                       ? earth_relative_rate(frame)
                                       : Eigen::Vector3d{frame.p, frame.q, frame.r};
  // ... unchanged from :336 on ...
}
```

- The peer passes `options_.earth_rate` to `inputs_from`.
- **The refused mask.** At `init` the peer fixes it: `kRefusedStatusBits & ~ASTRA_ICD_STATUS_LATLON_NOT_MODELED`
  under `kNone` with no `lateral_offset`, and `kRefusedStatusBits` otherwise. The shipped default is unchanged.
- **Why no path reads lat or lon under `kNone` without an offset** (amendment 6):
  - lat is read by `earth_relative_rate` (`control_law.hpp:318-319`), which `inputs_from` calls only under
    `kWgs84Rotating`;
  - lat and lon are read by `update_lateral_deviation` (`nesc_control_peer.hpp:337-354`), which returns at
    `:339-341` unless `offset_.configured`. Only there are `law::base_point` (`:345`) and, through
    `cross_track_at`, `law::cross_track` (`:351`, `:190-193`) called. `cross_track` is called nowhere else in the
    shipped peer.

  This is ADR-0029 Decision 2's "paths that read lat or lon".
- `nesc_control.cpp` is unchanged: no variant code in a shipped `.cpp` (PR 11a's amendment 1). It reads no new
  key, so the key block and the config-key reference do not change.

**Hook 2** (Q11):

```cpp
// aero_math.hpp
// The sign of beta at which the four control-derivative tables are read (the M7b plan's Q11).
enum class BetaTables : std::uint8_t {
  kSigned,    // at beta, as F16_aero.dml reads them (shipped)
  kAbsolute,  // at |beta|: tests/plugins/f16_aero_variant only (M7b PR 4's symmetric baseline)
};
[[nodiscard]] inline BuildUp build_up(const Inputs& in,
                                      BetaTables beta_tables = BetaTables::kSigned) noexcept {
  // ... unchanged to :233 ...
  const double table_beta = beta_tables == BetaTables::kSigned ? in.beta : b.absbeta;
  b.dclda = interp::lookup(kBeta2, kAlpha1, tables::kDclda, table_beta, in.alpha);
  // ... the same for dcldr, dcnda and dcndr; unchanged from :240 on ...
}
```

At the default, each lookup receives `in.beta`, the same double as today. `aero.cpp` is unchanged.

**The peer variant's three names.** `test_variant` stays required, and each existing name is unchanged.
- `default_options`: `Options{}`, which changes nothing. This is the shipped peer on a non-rotating Earth (the
  M7b plan, oracle (c)'s last paragraph).
- `earth_rate_none`: `Options{.earth_rate = kNone}`.
- `earth_rate_none_unwrapped_track_error`: `kNone` plus the wrap control, planted with public law functions only
  and no shipped hook.
  - `publish` keeps a copy of the request, then calls the shipped `publish`.
  - `read_reply` recomputes x = χ_est − χ_cmd from `inputs_from(request, kNone)` and the public `commands()`, with
    `control_law.hpp:241-244`'s own expressions.
  - When |x| > kHalfTurn and the reply is finite, the unwrapped law's φ_cmd would be −sign(x)·kPhiLimit exactly,
    because 10·|x| > 10π exceeds kPhiLimit.
  - **The re-evaluation** (amendment 10) calls `law::evaluate` with the same arguments the shipped `publish` passes
    (`nesc_control_peer.hpp:155-158`), changing only the two command fields below:
    - the same `Inputs`: `law::inputs_from(request, kNone)`, the call the peer makes with `options_.earth_rate` =
      `kNone`;
    - the same `Modes`: `law::Modes{}`, the value `publish` passes;
    - the same `Trim` and `References`: the peer's public `trim()` and `references()` accessors (`:179-180`), set
      at `init` and never changed after;
    - the commands: the public `commands()` (`:181`) after the shipped `publish`, which has applied the schedule
      and the lateral deviation for that exchange, with `course = χ_est − sign(x)·1.0` and
      `lateral_deviation_error = 0`.
  - **Why the result is exact.** After the subtraction, chi_err is about ±1 rad, not exactly ±1. It need only be
    large enough that kTrackErrorGain·chi_err saturates, that is |chi_err| ≥ kPhiLimit/|kTrackErrorGain| = 3°, and
    1 rad is far beyond that. So `saturate` returns ±kPhiLimit exactly (`control_law.hpp:206-208`, `:246`), the
    same −sign(x)·kPhiLimit as the unwrapped law.
  - The variant overwrites only the reply's aileron and rudder, the two outputs that read φ_cmd.
  - With |x| ≤ π, the reply is the shipped one.

**The aero variant** (`tests/plugins/f16_aero_variant`, descriptor `test`, `f16_aero_variant`).
- `aero.cpp`'s `F16Aero` sits in an anonymous namespace, so the variant carries its own copy of that plugin
  boundary, `aero.cpp:55-80`. A smoke pins that copy bitwise to the shipped plugin (§12).
- The variant includes `aero_math.hpp` by relative path, and reads `test_variant` from its own model block.
- **Its modes:**
  - `signed_tables`: `build_up(in)`, the shipped arithmetic, used for the pin and for §8's evaluators where the
    plugin path matters;
  - `abs_beta_tables`: `build_up(in, kAbsolute)`;
  - `abs_beta_tables_cl0_unsigned`: `kAbsolute`, then `clt = abs_cl0` for every β, then recompose;
  - `signed_tables_doubled_odd`: `kSigned`. Each of the four tables gets T(β) + (T(β) − T(−β))/2, with T(−β) from
    `interp::lookup` at −β, then recompose.
- **Recompose** recomputes cl1, cn1, cl and cn in `aero_math.hpp:240-249`'s own expression order. `BuildUp`'s
  fields are mutable (N-01's note).
- A pin checks recompose: applied to an unmodified `BuildUp`, it gives `build_up`'s fields bitwise at seeded
  inputs (§12).

## 3. The reference, and its independence (ADR-0034 Decision 2)

- **The reference is the relation itself.** The code under test runs on both sides by design.
- **The mirror map** is written in the test as sign constants from the derivation in §7, with the record masks
  there:
  - position (lat, −lon, h);
  - v_b → (u, −v, w), q_nb → (w, −x, y, −z) and ω_b → (−p, q, −r);
  - controls u → (elevator, −aileron, −rudder, throttle).
- **The heading relation** is the rotation of the horizontal position by −Δψ and the wrap of ψ − ψ0, both
  evaluated in `long double` (64-bit significand, asserted by `static_assert`) with `cosl`, `sinl` and
  `remainderl`.
- **What the relation never does:** call `frames::`, `icd_pack::`, `law::`, `air_data::` or a plugin.
- **R12's evaluations are not a reference** (§8). Each part compares the shipped model's own evaluations under the
  mask, or against a variant that differs from it by construction in one named place. Their criterion is by
  value.
- **Blind spots, disclosed:**
  - A defect that is itself sign-symmetric passes the mirror. Examples: a wrong magnitude on an odd channel's
    gain, a wrong even coefficient, or a wrong table value at both ±β.
  - A heading-independent defect passes invariance: any body-axis defect does.
  - R12's parts sample f at the recorded frame-start states with their latched controls, not at RK4's stage
    states.
  - R12's parts do not evaluate the packer (`icd_pack`) at the shipped pairs' states. The variant pairs cover it
    at theirs. The critique is asked about both of these (§16, step 4).

## 4. Shared inputs, and what each cannot see

| Shared input | What it cannot see | Mitigation |
|---|---|---|
| The config text, built in the test: the IC, the commands, the limits, the divisor | A wrong value shared by both runs; any symmetric IC passes the mirror | The IC's lateral entries are literal zeros, and the commands are written ±, by construction (§5, §7). The trim evidence and its checks (§5) |
| The one trim result | A wrong trim, shared by every run, which stays symmetric | Its residuals are reported. The trim is NESC's subject (case 11), not this oracle's |
| ICD v1's structs | A layout defect common to both runs | ADR-0006's static asserts |
| The platform: clang-18, glibc's libm, SSE2 doubles, the floating-point environment | A libm function that is not exactly odd or even; a common miscompile | R10's probe in every preset (§6); PR 3's §5 build facts, restated from C1's `compile_commands.json`; both runs on one build |
| The relation's own arithmetic | Its rounding, in the heading comparison | `long double`: at most about 1e-19 relative, far below the yardstick (§9) |

## 5. The IC and the one trim result (Q9, C-01, N-01)

**The trim** (C1, before any flight), on the precedent of PR 9's `base11.yaml.in`.
- **The base config,** `docs/reviews/evidence/m7b-pr4-trim/base.yaml.in`:
  - the F-16's aero, engine and mass;
  - `earth/round` with `rotating: false`, and `gravity/inverse_square` with `rotating: false`;
  - ISA-1976 and still air;
  - `p_n` = [0.6286530522161018, 0.0, 3051.9624000000003]: case 11's latitude, longitude exactly 0 and 10,013 ft;
  - heading 0.

  The `.in` suffix keeps it out of `test_config_keys.py`'s survey and out of PR 11a's tracked-config guard.
- **The command:** `astra trim base.yaml --airspeed 172.42091752452774 --altitude 3051.9624000000003 --heading 0
  --rate-convention n_frame`, written to `trim_n_frame.out`.
- **The check:** `trim_check.py` (`.venv/bin/python -I -B`) re-solves the trim through `astra.trim`, prints every
  figure, asserts C-01's bound below, and exits 1 on a miss. Its log is committed.

**The IC the config writes** (Q9 and C-01):
- the trim's own text for the elevator, the throttle, θ (α), u, w and q;
- a literal 0.0 for φ, ψ, v, p, r and lon, and for the aileron and the rudder;
- the peer's `trim` block, with the same elevator, throttle and θ texts;
- commands `altitude_ft: 10013`, `equivalent_airspeed_knots: 287.98158203347975` and `course_deg: 0`. The trim's
  airspeed and altitude are case 11's, so its EAS is the 13.x configs' literal (PR 11a's `ic_eas.py`).

These literals are written into this plan, dated, and into the test with a comment citing this section, before the
first flight.

**Why the lateral zeros are exact.** At heading north on a non-rotating sphere, v_E = 0.
- So ω_en^n = (v_E/r, −v_N/r, −v_E tan(lat)/r) = (0, −v_N/r, 0) (`round.cpp:115-121`).
- C_nb at (ψ, θ, φ) = (0, θ, 0) maps that vector to (0, −v_N/r, 0). The exact p and r are therefore 0.

**C-01's bound, pre-registered here** (decision 4). The trim's n_frame rate is ω̂ = lstsq(J_c, −q̇_c(0))
(`trim.py:212-225`), with J_c read off the core at the three unit rates.
- **The conditioning.** In exact arithmetic J = Ξ(q)/2 has orthogonal columns of equal norm, so κ₂(J) = 1. The
  exact system is consistent, so the least-squares κ² term vanishes (Higham 2002, §20.1).
- **The bound:** |p̂|, |r̂| ≤ B = c·u·κ̂·‖ω̂‖₂, with u = 2⁻⁵³ and κ̂ = κ₂(J_c) computed from the trim's own J_c.
- **c = 64**, rounded up from c_data + c_LS = 38 + 12 = 50:
  - c_data = 2 × 19, for J_c and for q̇_c(0). The 19 counts the core's q̇ chain:
    - `rotate_b_to_n`'s two 4-term Hamilton products (`frames.hpp:96-108`, `:142-148`): 8;
    - ω_en's division (`round.cpp:115-121`): 1;
    - C_nb·ω (`rigid_body_6dof_dynamics.cpp:33-35`): 3;
    - ω_b − ω_in: 1;
    - `quaternion_derivative` (`frames.hpp:112-116`): 5;
    - the trim's own difference q̇(e_i) − q̇(0) (`trim.py:224`): 1.
  - c_LS = 12, decision 4's assumed `dgelsd` constant.
- **The expected size.** ‖ω̂‖ ≈ v_N/r ≈ 2.7e-5 rad/s, so B ≈ 1.9e-19 rad/s. C-01's reproduction left about 1e-22.
- **A miss** stops the PR before any flight and goes to Joe.

**The pre-step prefix** (C-01, N-01).
- **Which runs.** Both runs of every pair or control flown with Ω = 0: the two variant pairs, the tables-only
  control and the sign-drop control.
- **The check.** Every odd field (§7) of all three recorder streams is exactly 0 by value at every record up to and
  including the step's: states with frame ≤ S, requests with frame ≤ S, and replies with frame < S. S is 15,000 for
  13.3's shape and 20,000 for 13.4's.
- **The excluded runs.** The shipped pairs, the Ω-only control and the doubled control are left out: the Ω
  subtraction makes their frame-0 aileron and rudder nonzero (§6).

**The trim checks** (`astra_f16_symmetry_tests`, every preset):
- `F16SymmetryTrim.TheTrimsLateralRatesAreRoundoff` holds `trim_n_frame.out`'s p̂ and r̂, the J_c-derived κ̂ and
  ‖ω̂‖ as literals citing this section, and asserts the bound.
- `F16SymmetryTrim.TheConfigRoutesQuaternionIsTheTrims` asserts that the config route's frame-0 q_nb, from
  euler_nb [0, θ, 0], equals (cos(θ/2), +0, sin(θ/2), +0) bitwise, with `std::cos` and `std::sin` of θ/2
  (amendment 5).

## 6. The asymmetry inventory and the parity probe (R10): commit C1, before any flight

**The inventory evidence** (`docs/reviews/evidence/m7b-pr4-inventory/inventory.py` and its log). It imports
nothing from `astra`, and reads only the generated header's text and `trim_n_frame.out`. It prints:
- **For each of `dclda`, `dcldr`, `dcnda` and `dcndr`:** the largest |T(−β) − T(β)| and where it falls;
  |T(10°) − T(−10°)| and T(0) at α = 0° and 5°; and the odd slope (T(10°) − T(−10°))/(2·10°) at the trim's α.
  - Plan mode's read-only scratch script reproduced finding 2's figures exactly: 0.079 (β ±30°, α 20°), 0.031,
    0.035 and 0.044.
  - The odd slopes at α ≈ 2.64° are +0.0084, +0.0042, −0.0199 and −0.0056 per rad.
- **The Ω term at the trim.**
  - b = C_nb·Ω(cos lat, 0, −sin lat), about (6.09e-5, 0, −4.01e-5) rad/s.
  - The law's frame-0 p and r: about −6.09e-5 and +4.01e-5 rad/s.
  - A plain-Python replay of `evaluate`'s lateral rows at the frame-0 request gives the reply's aileron and rudder,
    about +0.0055° and +0.0029° in plan mode. Both must be nonzero: the Ω-only control's precondition, and the
    reason the doubled control first fails at state 1 (§10).
- **The doubled control's pre-flight margin** (§10; Joe's R12 answer, point 2; amendment 9):
  - β(1) from frame 0's stage-1 side force, v̇ = (q̄S/m)·cy0, with cy0 = 0.021·dail + 0.086·drdr at frame 0's
    reply (`aero_math.hpp:208`);
  - the four registered numbers of §10: Δcl1 against ulp(cl), Δcn1 against ulp(cn), Δṗ against ulp(ṗ), and Δṙ
    against ulp(ṙ), at that state;
  - as intermediates only, each odd part and its ratio to ulp of the table entry.
- **The shipped pairs' static estimate** (amendment 4; §8's report): the per-channel figures §8 states, from the
  formulas there.
- **What stops for Joe:** any figure that moves from finding 2, or from the M7b inventory's Ω row (−6e-5, +4e-5;
  0.005° to 0.007°; 0.003°), at its printed precision.

**The bitwise rows**, argued from the code, which the variant pairs then test:
- **The EOM** (`rigid_body_6dof_dynamics.cpp:23-59`): every term of each component carries that component's
  parity. C_nb's C₁₂, C₂₁, C₂₃ and C₃₂ entries are odd, and the rest even.
- **The inertia.** I⁻¹'s (0,1), (1,0), (1,2) and (2,1) entries are exact zeros (`mass.cpp:56`: `0.0 - kIxy` with
  kIxy = 0).
- **The Earth model.** `round.cpp:99-121` has lat_dot even, lon_dot odd, and an axial transport rate.
- **Gravity.** `inverse_square.cpp:114-133` gives (0, 0, g(r)) plus an exact-zero centripetal term.
- **The quaternion** keeps its (e, o, e, o) parity through `frames.hpp:96-130`.
- **The packer** (`icd_pack.hpp:72-140`): copies; vertical speed even; ay odd.
- **The aero**, term by term at `aero_math.hpp:203-266`, except the four tables.
- **The peer** at `control_law.hpp:224-289`.
  - The saturations are exactly odd: `std::min(std::max(x, −L), L)` returns ±L exactly.
  - The cross-track at χ = 0 is d = (−dn·0) + de·1, value-equal to de.
- **Signed zeros.** `atan2` is never called with a zero y and a negative x, because ψ and φ stay within ±90° and
  u > 0. The test asserts that envelope on every record (§7). The β-sign branch at ±0 gives +absCl0(0) = 0 in both
  runs.

**The libm calls on the bitwise path with an odd argument:** only `atan2`, in ψ, φ and β (`frames.hpp:82`,
`air_data.hpp:43`).
- `asin`, `hypot`, `sin` and `cos` are called there only on even arguments, the same bits in both runs.
- `sin` and `cos` of odd arguments appear only on the Ω path (`control_law.hpp:316-319`), which no bitwise pair
  flies.

**The parity probe** (`F16SymmetryParity.*`, every preset, physics label).
- **The method.** Arguments are seeded by `std::mt19937_64` with the in-house uniform u = (x >> 11)·2⁻⁵³
  (AGENTS.md). Each comparison is bitwise, through `std::bit_cast<std::uint64_t>`, so the sign of zero counts.
- **The functions,** from Joe's list plus the M7b plan's Assumptions:

  | Function | Property | Ranges (2¹⁸ draws each), then edges |
  |---|---|---|
  | `atan2(y, x)` | odd in y | (U[−1, 1], U(0, 1]) for ψ and φ; (U[−60, 60], U[100, 300]) for β; both signs log-uniform over [2⁻¹⁰⁷⁴, 2¹⁰] for each argument. Edges: y and x in {±0, ±2⁻¹⁰⁷⁴, ±2⁻¹⁰²², ±1, ±DBL_MAX, ±∞}, every pair |
  | `asin(x)` | odd | U[−1, 1]; log-uniform over [2⁻¹⁰⁷⁴, 1], both signs. Edges ±0, ±2⁻¹⁰⁷⁴ and ±1 |
  | `sin(x)`, `cos(x)` | odd, even | U[−2π, 2π]; log-uniform over [2⁻¹⁰⁷⁴, 2²⁰], both signs. Edges ±0, ±2⁻¹⁰⁷⁴ and ±DBL_MAX |
  | `hypot(x, y)` | even in each argument | (U[100, 300], U[−60, 60]); log-uniform for both. Edges with ±0 and ±∞ |
  | `remainder(x, y)` and `remainderl` | odd in x, even in y | x in U[−20, 20], with y = 2π (and 2π in `long double`); log-uniform. Edge ±0 |

- **Seeds** 1, 2 and 3, one per range, as constants citing this section. Non-finite results (sin, cos and
  remainder of ±∞) are left out.
- **The build facts** (PR 3's §5 form, evidence only, no new test).
  - Each preset's `compile_commands.json` gives the floating-point flag set and the driver's cc1 arguments for
    every translation unit on the flights' path: libastra_core, the three F-16 plugins, the two variants and the
    two test executables.
  - The expected sets are PR 3's: none in the Debug presets, `-O3` in release and `build/python`, and no FMA
    target feature.
  - A difference stops for Joe.
- **A probe failure stops the PR before any flight.** The item becomes roundoff, and that goes to Joe.

## 7. The mirror pairs (Q9, R11): geometry, map, comparison set and objects

**The models:** `earth/round` (`rotating: false`), `gravity/inverse_square` (`rotating: false`), ISA-1976 and still
air, with §5's IC.

**The runs.** R is the course +15° or the offset +2,000 ft (right), and L is −15° or −2,000 ft (left).
- 13.3's shape: `schedule: [{frame: 15000, course_deg: ±15}]`, 30,001 frames.
- 13.4's shape: `course_deg: 0` and `lateral_offset: {frame: 20000, offset_ft: ±2000}`, 60,001 frames.
- Both shapes hold the altitude and EAS commands at the IC's values, at divisor 10, with the 13.x configs'
  `commands` and `limits` text.
- Both runs of a pair fly in one test process, on one build and one worktree state (lessons 2026-10-04 and
  2026-10-08). The flights keep their recorders in memory and write no log.

| Pair or control | Peer (`nesc_control_variant`) | Aero |
|---|---|---|
| Variant pairs, 13.3 and 13.4 | `earth_rate_none` | `f16_aero_variant` `abs_beta_tables` |
| Shipped pairs, 13.3 and 13.4 | `default_options` | `aircraft/f16/physical/aero` (shipped) |
| Controls | §10 | §10 |

**The record masks.** σ is −1 for an odd field and +1 for an even one. Double fields compare by value (`==`, so
+0.0 equals −0.0) as R = σ·L. Integer fields must be equal.
- **State** (`RecordedState`: frame, y[13]):
  - odd: y[1] lon, y[4] v, y[7] q_x, y[9] q_z, y[10] p, y[12] r;
  - even: y[0] lat, y[2] h, y[3] u, y[5] w, y[6] q_w, y[8] q_y, y[11] q;
  - equal: frame.
- **Request** (`astra_sim_to_sw_v1`):
  - odd: phi, psi, p, r, v, beta, lon, ay;
  - even: t, theta, q, u, w, alpha, tas, cas, mach, altitude, vertical_speed, lat, ax, az;
  - equal: magic, version, frame, status_flags, reserved.
- **Reply** (`astra_sw_to_sim_v1`):
  - odd: aileron, rudder;
  - even: elevator, throttle[0], throttle[1];
  - equal: magic, version, frame, mode_flags, fault_flags.
- **The recorder header:** every field equal, with counts of 3,001 and 30,001 for 13.3, and 6,001 and 60,001 for
  13.4.

**The bound for the variant pairs** (kind: by value, ADR-0034 Decision 4):
- every record of all three streams, at every frame and exchange, satisfies its mask;
- neither run halts;
- §5's prefix holds in both runs of both pairs.

**The envelope guard,** asserted on every record of both runs of every pair and control: |ψ| < 90°, |φ| < 90° and
u > 0. Outside it, §6's signed-zero argument does not hold, so a departure stops the PR; it is not a miss.

**The report** gives the first differing record, with its stream, index, field and both values.

## 8. The shipped pairs (R12): the evaluation-level decomposition, by value

**Deviation, 2026-10-20 (Joe), from the M7b plan's amendment 1 and from C-05's resolution.** The M7b plan's text is
not edited. The shipped pairs are gated by a by-value decomposition of the model's mirror defect at every recorded
state and exchange, not by a modeling bound on their trajectory asymmetry.

**The reason** is amendment 4's per-channel estimate, using the inventory's injections at |β| = 1.5° and
|dail| ≈ 1 and the loop's static gains during the roll-in.
- **The roll-in.** Both surfaces saturate at the step, and so does the lateral stick.
  - The stick's Δφ gain is about 176 per rad (`control_law.hpp:101`), against φ_cmd saturated at 30°.
  - NESC 13.3's reference (the band header, read in plan mode) has p peaking at 2.7 rad/s at 15.2 s, and the roll
    moment reversing from +2.4e5 to −2.35e5 N·m within 0.4 s.
- **The injections.** ΔCl ≈ 4.4e-4 and ΔCn ≈ 1.0e-3.
- **What they do, channel by channel:**
  - **p:** ΔCl over the roll damping |Clp|·b/2V ≈ 0.0114 gives Δp ≈ 0.038 rad/s, about 1.4 % of p (≈ 2⁻⁶).
  - **φ:** about 0.4e-2 to 1.1e-2 rad over the roll-in, about 1 to 2 % of φ (≈ 2⁻⁶).
  - **β:** ΔCn against the weathercock stiffness (about 0.21 per rad) gives Δβ ≈ 2.5e-3 to 5e-3 rad, 10 to 20 % of
    β (2⁻³ to 2⁻²).
  - **The aileron reply:** the unsaturated lateral stick differs between R and L by about 0.8 units at the reversal,
    against its ±1 range. The stick crosses its 2-unit span in about one exchange, so R and L can reverse the
    aileron at different exchanges: a 43° difference over one exchange, which no first-order term predicts.
- **The consequence.** Any κ fixed before the flight is either unsound at those reversals or above the doubled
  control's factor of about 2, so C-05 would have stopped the PR by design.

**The argument** (Joe's R12 answer):
- The variant pairs show that the closed loop's propagation is mirror-symmetric by value over the whole
  trajectory.
- Parts (a) to (d) show, at every recorded state and exchange of the shipped pairs, that the shipped evaluations
  differ from the symmetric ones only by the four tables' signed-β reading and by Ω.
- Together they account for the shipped pairs' whole mirror defect, which is what amendment 1 asked for.

**The evaluators.**
- f(x, u) is the 13-component state derivative, read through `Simulation::derivative()` (`simulation.hpp:262`)
  on an evaluator `Simulation` built from the pair's config text without its `transport` block.
- The controls are written through `write_input` (`simulation.hpp:249`), as `trim.py`'s `residual3` does.
- f_shipped uses the pair's own aero (the shipped plugin, or the doubled control's variant). f_abs uses the same
  text with the aero replaced by `f16_aero_variant` `abs_beta_tables`.
- M acts on states and on f's components by §7's state mask, and on controls as u → (elevator, −aileron, −rudder,
  throttle).
- x is each recorded state, and u is its latched controls: the reply of the last exchange at or before that frame.

**The four parts.** Each is a named object, evaluated by value over both runs of each shipped pair.
- **(a)** At every recorded state, f_shipped(x, u) − M·f_shipped(M·x, M·u) is zero in every component except ṗ
  (index 10) and ṙ (index 12).
- **(b)** At every recorded state, f_abs(x, u) − M·f_abs(M·x, M·u) is zero in every component.
- **(c)** At x′, whichever of x and M·x has β ≥ 0, f_shipped(x′, u′) == f_abs(x′, u′) in every component.
  - x′ is chosen by the sign of v: x when v ≥ 0, which includes ±0, and M·x otherwise.
  - At x′ the two evaluations take the same doubles, except that the four lookups read β at +0 where β = −0.
    Neither evaluation's table entries at that row are zero, so by value is exact there.
  - With (b), it follows that D(x, u) = M·(f_abs − f_shipped)(M·x, M·u) exactly when x′ = x, and
    D(x, u) = (f_shipped − f_abs)(x, u) when x′ = M·x. That is the tables' term and nothing else.
- **(d)** At every recorded request, law_kNone(req, cmd) == M·law_kNone(M·req, M·cmd), evaluated through
  `control_law.hpp`'s public functions: `inputs_from(·, kNone)`, `evaluate` with the peer's trim and references,
  and `to_icd`.
  - The comparison covers all four outputs, with the request mask of §7 and the commands mirrored: course and
    lateral deviation negated.
  - cmd is reconstructed from the config's schedule and offset. For 13.4 the cross-track goes through
    `law::base_point` and `law::cross_track` from the frame-0 request.
  - The shipped law equals law_kNone on `inputs_from`'s Ω-subtracted rates by construction: that branch is
    `inputs_from`'s only difference (hook 1). The `default_options` pin (§12) shows it end to end.

**Why ṗ and ṙ are the only components the tables can reach.**
- The four tables enter only cl1 and cn1 (`aero_math.hpp:240-241`), so cl and cn (`:247`, `:249`), so the roll and
  yaw moments (`:261-265`).
- The MRC-to-CM transfer adds only d·Fz to pitch and −d·Fy to yaw (`:264-265`). No force coefficient reads a table
  term: cx, cy and cz are at `:244-246`, and cy0 (`:208`) and the damping terms at signed β are exactly odd by the
  inventory.
- In the EOM, ω̇ = I⁻¹(M − ω×Iω) (`rigid_body_6dof_dynamics.cpp:50-52`), with I⁻¹'s (0,1), (1,0), (1,2) and (2,1)
  entries exactly zero (Ixy = Iyz = 0, `mass.cpp:56`). So a roll or yaw moment reaches only ṗ and ṙ.
- v̇ (`:45-46`), the pitch rate's derivative, the quaternion rates and the position rates read no aero moment.

**The bound kind:** by value (ADR-0034 Decision 4's first kind), under this deviation. A failed part means an
asymmetry outside the inventory, and it stops the PR.

**The envelope guard** (§7) and the N-01 prefix exclusion (§5) hold for the shipped pairs as stated. The |β|, α
and rate envelope, κ, F_f and the difference steps are dropped with the predictor.

**Reported, not a bound** (each pair's largest asymmetry against the inventory's estimate):
- **Per field** of all three streams: max |e_f|, where e_f = R_f − σ_f·L_f, and the record where it falls, against
  the inventory's static estimate (§6, from the formulas above).
  - Pre-step: the Ω term's offsets, starting with an aileron asymmetry of about 2 × 0.0055°.
  - Post-step: Δp ≈ 0.038 rad/s, Δφ ≈ 0.4e-2 to 1.1e-2 rad and Δβ ≈ 2.5e-3 to 5e-3 rad.
- **The aileron reversals** of R and L (amendment 11), since that is where the asymmetry is amplified.
  - **A reversal** is an exchange at which the reply aileron's sign differs from the previous exchange's. The sign
    is taken by value, as −1, 0 or +1, so ±0 both count as 0.
  - **The report** lists, in order, R's and L's reversal exchanges as pairs (R_i, L_i), each with its difference
    in exchanges, and gives the count on each side.
  - When the counts differ, the unpaired reversals are listed after the pairs, so a run with an extra reversal is
    visible.

**C-05 under this deviation:** the doubled control must fail part (c), as §10 registers.

## 9. Heading invariance (Q12, R13): the measured yardstick

**The flight:** `earth/flat`, `gravity/constant` (default g) and ISA-1976, in still air.
- **The IC** is §5's trim on the flat Earth (decision 3): p_n = (0, 0, −3051.9624000000003), v_b (u, 0, w),
  ω_b = 0, the trim's elevator, throttle and θ, and euler_nb = [ψ0, θ, 0].
- **The shape** is 13.3's: a course step of +15° at frame 15,000, 30,001 frames, with the IC's altitude and EAS
  commands.
- **The peer** is `nesc_control_variant` `earth_rate_none`. With no lateral offset, nothing refuses the flat Earth
  (hook 1).
- **The aero** is the shipped one: a heading rotation does not change β.

**The headings:**
- ψ0 = 0 (the base): course 0°, then 15°;
- ψ0 = 0.6 rad: euler_nb[0] = 0.6, course 0.6 rad written in degrees, then that value plus 15;
- ψ0 = 172.5°: euler_nb[0] = the %.17g of `degrees_to_radians(172.5)`, course 172.5°, then 187.5°. This turn
  crosses ±180°.

Each course text, and its conversion's difference from the intended double, is logged as a one-time injection
inside F's estimate.

**The perturbation set P** (Q12; decision 2): eight runs at ψ0 = 0.
- Each moves one component of q0 = (cos(θ/2), +0, sin(θ/2), +0) by +δ_i and by −δ_i.
- δ_i = ulp(|q_i|) for a nonzero component, and 2⁻⁵² for x and z, which are exactly zero.
- The write goes through `Vehicle::state()` and then `sync_stage_buffer()` before frame 0 (`vehicle.hpp:241-264`).
  The config's `initial.state` takes only `euler_nb` (`simulation.cpp:238-266`).

**The base run's route** (amendment 5). The base run writes its unperturbed q0 through `Vehicle::state()` and
`sync_stage_buffer()` too, so the base and P share one route, and no route difference enters Y.
- Its q0 is the config route's own frame-0 q_nb.
- Before the first flight, `F16SymmetryTrim.TheConfigRoutesQuaternionIsTheTrims` (§5) asserts that this equals
  (cos(θ/2), +0, sin(θ/2), +0) bitwise.
- The two rotated runs take q0 from the config (euler_nb [ψ0, θ, 0]), the route by which "only the initial
  quaternion's heading changed".

**The compared quantities** (the brief's list), each at every record of the base run against the rotated run:
- v_b and ω_b, from the state stream;
- φ, θ, α, β and altitude, from the requests;
- wrapped ψ − ψ0, from the request psi: `remainderl((long double)ψ − ψ0, 2π_L)`;
- the five reply fields;
- the horizontal position, from the state, rotated back by −Δψ in `long double`;
- the state's altitude, −D.

**The bound** (kind: measured sensitivity yardstick, ADR-0034 Decision 4). For each channel c and record k,
|d_c(k)| ≤ F·Y_c(k).
- d_c is the rotated run's value minus the base run's.
- Y_c(k) = max over p ∈ P and over records s ≤ k of |x_p,c(s) − x_base,c(s)| (decision 2's running maximum).

**F = 2²⁴,** fixed from this estimate of per-frame against initial injection:
- the rotated run differs from the base by rounding at every frame, over N = 30,001 ≤ 2¹⁵ frames;
- each frame injects at most 2⁵ ulps of the quantities it touches, relative to P's one ulp;
- along directions the attitude perturbations excite at least 2⁻⁴ as strongly;
- so 2¹⁵·2⁵·2⁴ = 2²⁴. The one-time injections (the rotated q0 and the course texts, ≤ 4 ulps) fit well inside it.

F and P are never adjusted after any run.

**One test** (amendment 7). `F16SymmetryHeading.ThreeHeadingsAndTheUnwrappedControl` flies the base run, the eight
perturbed runs, the two rotated runs and §10's wrap control once each, 12 runs in all. It reports each part
separately.

**Reported:** for each heading and each channel, max d/(F·Y), and Y's largest value.

## 10. The five controls (ADR-0034 Decision 5, C-04, R14)

Each control fails on its named object as registered. A control that does not fail as registered, or that first
fails elsewhere, stops the PR.

| Control | Peer, aero, shape | Registered failure |
|---|---|---|
| Ω only | `default_options`, `abs_beta_tables`, 13.3 | **The bitwise mirror, from engagement.** The first differing record is reply 0, in exactly {aileron, rudder}. State 0 and request 0 are equal under the mask. The precondition is §6's frame-0 replay: a nonzero aileron and rudder |
| The tables only | `earth_rate_none`, the shipped aero, 13.3 | **The bitwise mirror, in the first exchange interval after the step.** §5's prefix is exactly lateral-zero in both runs. No record differs at or before the step. The first differing record is a state with frame in (S, S + 10], or the reply at S + 10, and the fields expected first are state p and r |
| absCl0's sign dropped | `earth_rate_none`, `abs_beta_tables_cl0_unsigned`, 13.3 | The same as the tables only |
| The doubled asymmetry | `default_options`, `signed_tables_doubled_odd`, 13.3 and 13.4 | **§8's part (c).** In each run, the first record failing any part is the first recorded state with β ≠ 0 and a nonzero latched aileron, in ṗ or ṙ (predicted: state 1). Parts (a), (b) and (d) hold through it |
| The unwrapped track error | `earth_rate_none_unwrapped_track_error`, the shipped aero, flat Earth, ψ0 = 172.5° | **Heading invariance, by orders of magnitude.** The first differing record from the nominal 172.5° run is the reply at exchange k\*, in exactly {aileron, rudder}. Everything before it is bitwise equal. After it, max \|d\|/(F·Y) ≥ 10³ against the base run in each of wrapped ψ − ψ0 and the rotated-back N and E |

**The tables-only and sign-drop window** (amendment 2). S = 15,000.
- At exchange S, the request is lateral-zero in both runs. The `kNone` law gives exactly mirrored replies (φ_cmd =
  ±kPhiLimit, saturated), so the reply at S is equal under the mask.
- β first becomes nonzero inside frame S. At stage 1, cy0's dail and drdr terms (`aero_math.hpp:208`) give v̇ ≠ 0,
  and ṗ gives p, so p·w adds to v̇ at stage 2 (`rigid_body_6dof_dynamics.cpp:45-46`). From stage 2 on, β ≠ 0 with
  the step's aileron latched.
- The table asymmetry (signed tables), or the dropped sign of absCl0 (`abs_cl0(|β|)` in both runs), then enters cl
  and cn. It reaches only ṗ and ṙ (§8), so state p and r first differ in state S + 1.
- The registration allows the whole first exchange interval, (S, S + 10], or the reply at S + 10, in case the
  first difference rounds away at S + 1.

**The doubled control's registration** (Joe's R12 answer, point 2, with the correction in "Joe's decisions").
- Frame 0's reply carries the Ω-driven aileron and rudder (about +0.0055° and +0.0029°, §6). At stage 1 of frame 0,
  cy0's control terms give v̇ = (q̄S/m)·cy0 ≠ 0, so v and β are nonzero at state 1, with that aileron latched.
- The doubled model's four tables then differ from the |β| model at β > 0 by (T(β) − T(−β))/2, so f_doubled and
  f_abs differ in ṗ and ṙ at x′.
- **The margin, at the registered object** (amendment 9), confirmed before the first flight from the header text
  by `inventory.py` (§6). It is evaluated at β(1), with frame 0's dail and drdr, the IC's α, and p = r = 0.
  `inventory.py` prints four numbers, and the registration rests on them:
  1. the extra cl1, Σ over `dclda` and `dcldr` of the odd part times dail or drdr, against ulp(cl) at that state;
  2. the extra cn1, the same over `dcnda` and `dcndr`, against ulp(cn);
  3. Δṗ = (I⁻¹·q̄Sb·(Δcl1, 0, Δcn1))_x, against ulp(ṗ) at that state;
  4. Δṙ, the z component, against ulp(ṙ).

  Each odd part, (T(β) − T(−β))/2 from the breakpoint slopes about zero, and its ratio to ulp of the table entry
  are printed too, as intermediates only.
- **Plan mode's estimates**, before `inventory.py` replaces them:
  - |β(1)| ≈ 6e-10 rad, from v̇ = (q̄S/m)·cy0 at frame 0's dail ≈ −2.75e-4 and drdr ≈ +9.7e-5;
  - Δcl1 ≈ β·(0.0084·dail + 0.0042·drdr) ≈ −1.2e-15, against cl ≈ 1.6e-5 (ulp ≈ 3.4e-21): about 3e5;
  - Δcn1 ≈ β·(−0.0199·dail − 0.0056·drdr) ≈ 3.0e-15, against cn ≈ −1.8e-6 (ulp ≈ 2.1e-22): about 1e7;
  - with q̄Sb ≈ 3.4e6 N·m and F16_inertia.dml's tensor, Δṗ ≈ −3e-13 rad/s² against ṗ ≈ 4.2e-3 (ulp ≈ 8.7e-19), about
    3e5, and Δṙ ≈ 1.2e-13 against ṙ ≈ −7.6e-5 (ulp ≈ 1.4e-20), about 8e6.

  So the per-axis sums do not cancel, and both differences sit well above roundoff at state 1.
- 13.4's shape has the same prefix as 13.3's through frame 20,000. It is flown too, for the trajectory report
  beside its shipped pair.

**The wrap control's first difference** (amendment 1), with χ_cmd = `degrees_to_radians(187.5)` ≈ 3.27249 rad
after the step.
- **Before the crossing,** ψ ∈ (3.0107, π], so x = β + ψ − χ_cmd lies in about (−0.26, −0.13]. With |x| ≤ π, the
  wrapped and the unwrapped law are the same, and the run equals the nominal bitwise.
- **At the crossing,** ψ goes to about −π, so x ≈ −6.41 rad.
  - The wrapped law adds 2π, giving about −0.13 rad (−7.5°).
  - kTrackErrorGain·x is then +1.31 for the wrapped law and +64.1 for the unwrapped one. Both exceed kPhiLimit
    = 0.5236, so φ_cmd = +kPhiLimit in both (`control_law.hpp:73-75`, `:244-246`).
  - The replies stay bitwise equal past the crossing.
- **The first difference** is at k\*, the first exchange after the crossing at which kTrackErrorGain·wrap(x) <
  kPhiLimit: |wrap(x)| below kPhiLimit/|kTrackErrorGain| = 3°, about 184.5° of true heading.
  - There the wrapped φ_cmd unsaturates, while the unwrapped one stays at +kPhiLimit: x ≈ −6.33, and
    kTrackErrorGain·x ≈ 63.3.
  - The two replies differ in exactly the two outputs that read φ_cmd: aileron and rudder.
- **After k\*,** the unwrapped law keeps banking right, which is the wrong way. It circles, so ψ and the position
  diverge by order 1 rad and by kilometers, against F·Y ≲ 2e-6 rad in ψ: about 10⁵ to 10⁶.

## 11. R15: the hooks leave shipped behavior bit-identical (decision 5)

**The evidence** is `docs/reviews/evidence/m7b-pr4-r15/r15_before_after.py` and its log, run before the first
flight.
1. At C1, which has no hook, run `just python`. Copy `build/python`'s plugin directory to scratch, and record the
   SHA-256 of the two plugins the hooks touch: `aircraft/f16/physical/aero` and
   `aircraft/f16/sim/transport/nesc_control`.
2. At C2 (the hooks, committed, clean tree), run `just python`. The "before" directory is a copy of
   `build/python`'s plugin directory with those two files replaced by C1's.
3. **The log records** (amendment 8):
   - that `git diff C1..C2 -- core icd` is empty, so the swap compares only the two hooked plugins;
   - both plugins' SHA-256 for each pass, before and after.
4. In one process on C2's `_core`, fly each config with `plugin_dir` = before and then after:
   - `aircraft/f16/configs/case13p3_subsonic_heading_change.yaml`: `exchange_recorder_bytes()` byte-equal;
   - `aircraft/f16/configs/case13p4_subsonic_lateral_side_step.yaml`: the same;
   - `aircraft/f16/configs/case11_trim_check_subsonic.yaml`, case 11's open-loop run. It has no transport and so no
     exchange recorder, so the driver records the 13 rigid-body doubles and the four latched controls at every
     frame, in the recorder's state layout, and those bytes must be equal.
5. Each run also gets a `log` block. The logs must be equal object by object, except `/plugins`' SHA-256 of the two
   swapped plugins, which is named. `git_describe` must be equal in both passes: as PR 3's O11 reads it, that shows
   one libastra_core build.
6. `git rev-parse HEAD` and `git status --porcelain` are recorded before the first run and after the last, and must
   be equal.

Also, F16Aero's check data (`aircraft/f16/tests/f16_checkdata.hpp`) and F16Control's tests are unchanged: `git diff
origin/main -- aircraft/f16/tests` is empty, and they pass in every preset in the gate. A difference anywhere stops
the PR and goes to Joe.

## 12. Smokes, pins, labels and the stop hook

**`astra_f16_symmetry_tests`, every preset** (it links `alloc_counter.cpp`):
- `F16SymmetryParity.*` (§6), `F16SymmetryTrim.TheTrimsLateralRatesAreRoundoff` and
  `F16SymmetryTrim.TheConfigRoutesQuaternionIsTheTrims` (§5).
- `F16SymmetryTrim.TheIcIsLateralZero`: at the projected IC, `derivative()` has v̇_y = ṗ = ṙ = 0 by value, with the
  shipped and with the |β| aero. The solved residuals are ≤ 1e-6 (M3's D8).
- `F16SymmetrySmoke.DefaultOptionsEqualsTheShippedPeer`: the tracked 13.3 config, 1,000 frames, with the shipped
  plugin and with `default_options`. The recorders must be byte-equal.
- `F16SymmetrySmoke.SignedTablesEqualTheShippedAero`: §7's shipped-pair text with its step at frame 500, 1,000
  frames, with the shipped aero and with `signed_tables`. The recorders must be byte-equal.
- `F16SymmetrySmoke.RecomposeIsBuildUpBitwise`: §2's recompose, on seeded inputs, gives `build_up`'s fields
  bitwise.
- `F16SymmetrySmoke.EachHookAndDefectFliesWithoutAllocating`: each configuration in §7, §9 and §10, with its step
  moved to frame 500, flown for 1,000 frames. Zero allocations over the steps, no halt, and 100 exchanges.
- `F16SymmetrySmoke.EarthRateNoneRefusesAFlatEarthOnlyWithALateralOffset` (Q10's rule):
  - `kNone` on a flat Earth with a lateral offset halts `kTransport` at frame 0, with `LATLON_NOT_MODELED` in
    `transport.peer.refusal`;
  - `kNone` without the offset flies;
  - `default_options` on a flat Earth halts, which is the shipped behavior.
- `F16SymmetrySmoke.UnknownVariantsAreConfigErrorsNamingTheKey`, for both variant plugins.

**`astra_f16_symmetry_flight_tests`, registered only in a Release build** (`tests/lockstep/CMakeLists.txt:66`'s
precedent):
- `F16SymmetryMirror.{Case13p3,Case13p4}VariantPair` (§7);
- `F16SymmetryShipped.{Case13p3,Case13p4}Pair` (§8);
- `F16SymmetryControl.{OmegaOnly, TablesOnly, Cl0SignDropped, DoubledAsymmetryCase13p3,
  DoubledAsymmetryCase13p4}` (§10);
- `F16SymmetryHeading.ThreeHeadingsAndTheUnwrappedControl` (§9, §10).

Each ctest test runs in its own process, so each flies what it needs. Only the heading test needs the eight
perturbed runs, and it flies them once (amendment 7).

**The labels and the stop hook** (lesson 2026-10-18).
- Both executables carry the `physics` label, and no new test carries `unit`. The stop hook runs `just test-unit`,
  which is the asan preset's `-L unit` (`.claude/hooks/stop-check.sh`), so it runs none of them.
- The existing unit suites the hook does run include F16Aero and F16Control (`aircraft/f16/tests`, label `unit`).
  They run against the hooks at the first turn end after C2 compiles. That counts as those suites' first run under
  the hooks, which is R15's "unchanged" half; it is not an oracle run.
- The flights and smokes stay held to the first-flight step (§16, step 7).

## 13. What may precede the first flight, and what was read or measured in plan mode

**Before the first flight** (§16, steps 4 to 6):
- the trim and C-01's check;
- the config-route assertion;
- the inventory and the doubled control's pre-flight margin;
- the parity probe in every preset;
- the build facts;
- R15's before-and-after.

None of them produces compared output of the path under test.

**Plan mode ran, read-only, a scratch `python3 -I` on stdout:**
- the four tables' figures and odd slopes (§6), from the header text;
- the Ω term and the frame-0 aileron and rudder, from case 11's θ. The oracle's own trim replaces those in C1;
- NESC 13.3's reference roll, p, r, roll and yaw moments and side force around the step, read from the band header
  (§8's reason).

**Read, not run:** `d4c0aeb-full-gate.log`'s release durations (§1). Nothing was built or flown.

## 14. Acceptance

| R | Check | Kind | Command |
|---|---|---|---|
| R10 | §6 | bitwise (the probe); figures (the inventory) | `ctest --preset <p> -R F16SymmetryParity -N`, then the run, in debug, release, asan and static-plugins (C1); `inventory.py`'s log |
| R11 | §5, §7 | by value | the release flights below |
| R12 | §8, parts (a) to (d) | by value, under §8's dated deviation from amendment 1 and C-05's resolution. The trajectory report is not a bound | the release flights below; the PR body's per-field report |
| R13 | §9 | measured yardstick | the release flights below |
| R14 | §10 | each control's registered object | the release flights below |
| R15 | §11 | byte-equal | `r15_before_after.py`'s log; the asan command below |
| — | the flights | — | `ctest --preset release -R F16Symmetry -N` (count), then `ctest --preset release -R F16Symmetry --no-tests=error` |
| — | the smokes and the hooks | — | `ctest --preset asan -R 'F16Symmetry\|F16Aero\|F16Control' -N` (count), then the run with `--no-tests=error`. The `\|` is for this table only; in the shell it is a plain `|` (lesson 2026-10-11) |
| R16 | the full gate | — | `./scripts/check-full.sh`: exit 0 and `check: OK`, with §1's record in the PR body |
| — | review | — | an adversarial Codex review of this plan before the first flight, and of the diff after it, with at most two correction rounds |
| — | before the merge | — | `git fetch`, and merge `origin/main` in if it moved |

Every pre-registered constant in the tests carries a comment citing this plan's section (ADR-0034 Decision 6):
- the IC literals and C-01's c;
- the probe's seeds, ranges and counts;
- the masks and §8's component indices;
- F and P;
- the registered records and windows;
- the 10³ margin.

## 15. Docs in this PR

- `PLAN.md`: the M7b block's PR 4 line.
- This plan, and the review record, `docs/reviews/m7b-pr4-f16-symmetry-review.md`.
- The evidence: `m7b-pr4-trim/`; `m7b-pr4-inventory/`, with the probe's per-preset results and the build facts;
  `m7b-pr4-r15/`; `<head>-f16-symmetry-first-flight.log`; and `<head>-gate-summary.log`.
- Not touched: architecture (§16's row stays planned, and §4, §10 and §18 are PR 5's), the ADRs, and the M7b plan,
  whose amendment 1 this plan departs from by a dated deviation (§8). `STATE.md` is written at `/wrap`.
- Carried: `aero_math.hpp:86` is updated (§2). This PR edits neither `aero_test.cpp` nor
  `independent_evaluator.hpp`, so their carried items stay open.

## 16. Sequence after approval

1. `git fetch origin`. Confirm that `origin/main` is `fe922b0` or later and that STATE.md's step 2 names PR 4.
   Confirm with `git worktree prune --dry-run` and `git branch --list 'worktree-m7b-pr3*'`.
2. Create the worktree `m7b-pr4-f16-symmetry` from `origin/main` (`EnterWorktree`).
3. Move this draft in as `docs/plans/m7b-pr4-f16-symmetry.md` (`mv`, before its first add). Commit, push, and open
   the draft PR (Tier 3).
4. **The Codex adversarial plan critique.** It is fresh and read-only: `task --fresh --effort high`, with no
   `--write` and no `--model`, in the background, on PR 3's route. It is given ADR-0034, ADR-0029, the M7b plan's
   oracle (c), and this plan. It is told that R12's gate is the evaluation-level decomposition (§8), and its focus
   is:
   - whether parts (a) to (d) are sufficient to confine the shipped pairs' mirror defect to the two inventoried
     sources;
   - whether any component of f other than ṗ and ṙ can carry a table term;
   - the two disclosed gaps (§3): the packer at the shipped states, and f sampled only at frame-start states;
   - the inventory's completeness and the masks;
   - §9's F, P, running maximum and base route;
   - §10's registrations and their derivations;
   - the hooks against PR 11a's amendment 1;
   - R15's one-build argument.

   Verify each finding against its cited lines and fold in each supported one. A finding that changes a
   registered object, bound, control or decided item goes to Joe before the first flight.
5. **C1, the pre-flight facts.** The trim evidence, `inventory.py`, the probe and trim tests, and their CMake.
   Build every preset and `build/python`.
   - Run `trim_check.py`, `inventory.py`, and the R10 and trim commands, counts first. State the build facts.
   - Write the IC literals, p̂, r̂, B, the inventory's figures and the doubled control's margin into §5, §6 and §10
     as dated literals.
   - Commit and push. A failed item stops for Joe.
6. **C2: the hooks, the variants, every F16Symmetry test, and the CMake.** Build every preset and `build/python`.
   - Nothing of the flights or smokes runs (§12).
   - Run R15 (§11), and commit its log. A difference stops for Joe.
7. **The first flight.** Run §14's release command, counts first, then the smokes in debug, asan and
   static-plugins, then the asan acceptance command. Write the first-flight log.
   - A nominal miss, an envelope departure, or a control that does not fail as registered stops the PR and goes to
     Joe.
8. §15's docs, then the full gate with §1's record, the evidence and the PR body.
9. `/review-cycle`: Codex reviews the diff against `--base origin/main`, with at most two correction rounds. Run
   `git diff --check origin/main..HEAD` first.
10. `git fetch`, and merge `origin/main` in if it moved. A code change from that merge reruns the gate.
11. `/human-review`, then `/wrap`, then stop. The merge is Joe's.

## 17. Risks and rollback

- **R12's parts are exact, by value.** A shipped pair that fails one shows an asymmetry outside the inventory,
  which is a defect until shown otherwise, and it stops the PR.
  - The parts do not bound the trajectory asymmetry; they explain it. The shipped pairs' trajectory asymmetry,
    reversals included, is reported only.
- **The two disclosed gaps** (§3): the packer at the shipped states, and f sampled only at frame-start states.
  The critique is asked to weigh them. A finding there goes to Joe before the first flight.
- **The doubled control's first failure** rests on frame 0's Ω-driven aileron (§6) and on β(1) ≠ 0. The inventory
  confirms both before the first flight. An earlier or later first failure stops the PR.
- **The wrap control's k\*** depends on the nominal turn passing 184.5° after the crossing. The derivation in §10
  states why it does.
- **The yardstick's F.** A per-frame injection larger than §9 estimates, in some channel, makes a nominal rotated
  run miss. F is never raised; that goes to Joe.
- **libm.** A probe failure stops the PR before any flight.
- **The trim's p and r.** A miss of C-01's bound stops the PR before any flight.
- **Gate time** (§1). About 3.2 s of release test time is estimated. The derivative-call cost is an assumption,
  measured at the first flight. A large increase goes to Joe (Q3).
- **Rollback:** revert the PR. The hooks revert with their tests and variants, and nothing else ships.

## 18. The Codex plan critique (§16, step 4)

**The run.** One fresh, read-only critique, on PR 3's route.
- Companion 1.0.6 and Codex CLI 0.160.0: `task --fresh --effort high --cwd <this worktree>`, with no `--write` and
  no `--model`, in the background. The model is the companion's default (`~/.codex/config.toml`: `gpt-6.1-sol`
  at reasoning effort high).
- Its focus was §16 step 4's, told that R12's gate is the evaluation-level decomposition.
- Candidate: `0ee2c70` against `origin/main` = `fe922b0`, a clean tree. Job `task-mvhltdcf-g68uuk`, thread
  `01a15cb0-73a5-77a0-bfee-576f49749c51`, 5 min 33 s.
- Exit 0, **verdict rework**: 4 findings, 1 high and 3 medium. Codex reported that it edited no file, ran no build
  and executed no ASTRA code.

**Found sound** (Codex):
- At fixed state and controls, the four tables reach only ṗ and ṙ.
- The masks cover every ICD, state-record and recorder-header field; the counts are P-02.
- Part (c)'s choice by the sign of v is valid in still air, and ±0 does not break its by-value equality.
- The Ω-only reply-0 registration, the prefix exclusions, the tables-only and sign-drop window, and the doubled
  control's state-1 mechanism are consistent with the code. The doubled control's margin is still a pre-flight
  estimate.
- The projected IC, the quaternion route, and C-01's bound under the accepted constant assumption.
- The header-only hooks, the refused-mask rule, the wrap variant's accessors, the aero boundary copy and its
  recompose pin, and R15's C1 plugins under C2's unchanged core.
- P, the running maximum and the base run's route; F remains an estimate.
- The labels against the stop hook, the Release-only registration, and §1's estimate of about 1.08 M frames and
  3.2 s.

Claude Code re-read every cited line, and **all four findings are accepted**:
- P-02 was confirmed at `lockstep_exchange.cpp:164` and `:198`.
- P-01's stage evaluations were confirmed at `vehicle.cpp:504-515` and `integrator.hpp:109-121`.
- P-03's saturations were confirmed at `control_law.hpp:268-270`.

Each proposed resolution changes a registered object, a control or a guard, so all four come to Joe before the
first flight. None is folded into the sections above until he decides.

| # | Sev. | Finding (verified) | Proposed resolution | Touches |
|---|---|---|---|---|
| P-01 | high | §8 claims the parts account for the whole mirror defect, but §3's two disclosed gaps leave evaluations unchecked. (d) mirrors an already packed request, so an asymmetry in `icd_pack` (Euler angles, air data, vertical speed, the previous frame's specific force; `icd_pack.hpp:83`, `:133`) would pass it. The frame-start probes leave RK4's other three evaluations per frame unchecked (`vehicle.cpp:510`) | **Cover both gaps; keep the criterion.** (e) At every exchange of both runs, `icd_pack::pack_sim_to_sw` at (M·x, M·F) equals M·`pack_sim_to_sw` at (x, F) by value, under §7's request mask. Here x is the recorded state, F = m·(ax, ay, az) from the recorded request, and the environment is rebuilt at the state (still air; a second ISA-1976 instance at the Earth model's altitude). **Parts (a) to (c) at all four RK4 stage operands of every frame.** The stage operands are rebuilt from the recorded state, its latched controls and the evaluator's f in `integrator.hpp:109-121`'s order, and the rebuilt next state, renormalized as `frames.hpp:123-130` does, must equal the recorded next state bit for bit, so the operands are the flight's own. A miss of that rebuild stops the PR. The attribution then covers every evaluation of f, the packer and the law the flight makes. The exchange's copies, limits and latch, and RK4's combination and renormalization, are linear or even by construction, and the variant pairs exercise them | §8, §3's blind spots, §1's estimate (about 16 derivative calls per recorded state, 2.9 M in all, about 2.9 s at 1 µs) |
| P-02 | medium | §7 registers 30,001 and 60,001 states. The recorder stores the state at `open` and after every step (`lockstep_exchange.cpp:164`, `:198`), so the runs record 30,002 and 60,002 | The header counts become 3,001 and 30,002 (13.3), and 6,001 and 60,002 (13.4). §8's parts include the terminal state, with the last exchange's reply latched, over 180,008 states in the two shipped pairs | §7, §8, §1 |
| P-03 | medium | §10's wrap control registers its first difference at k\*, where the wrapped φ_cmd first unsaturates. But φ_cmd then passes through the lateral stick's and the pedal's own saturations (`control_law.hpp:268-270`), which can hide the difference at k\* | **Register the exchange where the outputs actually differ.** The first differing record from the nominal 172.5° run is the reply at k†: the first exchange at or after k\* at which an exact replay of the two laws on the nominal run's recorded request gives different replies. The shipped law runs through `inputs_from(·, kNone)`, `evaluate` and `to_icd`. The unwrapped law runs through the variant's own substitution, written again in the test. The differing fields are exactly those the replay finds, a nonempty subset of {aileron, rudder}, and every record before is bitwise equal. The replay is exact because the control run equals the nominal bitwise until its first differing reply. If no such exchange occurs before the run ends, the control has not failed as registered, and the PR stops. The orders-of-magnitude failure after it is unchanged | §10, §9's one test |
| P-04 | medium | §7's envelope guard (\|ψ\| < 90°) applies to "every pair and control", which includes the wrap control at 172.5° | Scope the signed-zero guard (\|ψ\| < 90°, \|φ\| < 90°, u > 0) to the meridian-mirror runs: the variant and shipped pairs, and the Ω-only, tables-only, sign-drop and doubled controls. The heading runs (the base, P, the two rotated runs, and the wrap control) get their own guards: no halt, u > 0, and \|θ\| < 80°, away from the 3-2-1 singularity. They need no signed-zero argument, because the yardstick is not by value | §7, §9 |

**Status: awaiting Joe's decision on P-01 to P-04, before the first flight.**
- §16 continues meanwhile with C1, the pre-flight facts, which none of the four touches.
- C2's tests are written to the resolutions only after Joe decides.
