# M7 PR 4: `mass/constant`'s product-of-inertia convention

Status: **approved by Joe, 2026-10-09, in plan mode**, with his amendments 1 to 7 folded in (below).
Owner: Joe. Builder: Claude (one builder). Tier: **3** (`plugins/physical/`).
Base: `origin/main` = `58b681f` (#110, PR 3b). I read it with `git ls-remote` in plan mode, and `git fetch`
confirmed it.
Branch and worktree: `worktree-m7-pr4`, from `origin/main`. This file started as the session's plan-mode
draft and was moved into the worktree (`docs/agents/claude-code.md`).
Sources: the [M7 plan](m7-f16-nesc-oracle.md)'s PR table row 4, R17 and Q19, and PLAN.md's (M7)
`mass/constant` item. There is no Codex plan critique: the design is Joe's decision, and the change is small.

## Context

`plugins/physical/mass/constant` puts `ixy`/`ixz`/`iyz` into the tensor unnegated, and nothing documents
what those keys mean. The template's and the notional transport's mass plugins read the same key names as
S&L products of inertia and negate them. PR 6b's F-16 takes Izx = 982 slug·ft² from the DML as a positive
product of inertia. Outcome: one meaning for these key names everywhere, stated in the plugin and pinned by
tests.

## Joe's amendments (2026-10-09)

1. D1 is decided: `0.0 - p`. Its comment goes in the header (§1).
2. The `ctest -N` delta is pre-registered as a number, with the new tests named (§3, §7).
3. The plan states what the R1 golden test compares (§2).
4. The refusal tests assert the path and the message text as they are today (§3).
5. `test_trim_kinematics.py:135-136` and `m7-pr5-trim.md:234` stay as written (§5).
6. The PR body's contents (§6).
7. The Status line follows PR 3b's and PR 5's shape.

## 1. The change (`plugins/physical/mass/constant/constant.cpp`)

- `ixy`, `ixz` and `iyz` are S&L products of inertia: Ixy = ∫xy dm, Ixz = ∫xz dm, Iyz = ∫yz dm, in body FRD
  axes about the CG, in kg·m². The tensor's off-diagonal entries are their negations. The diagonal, the
  finite checks, the defaults (0), the LLT positive-definite check on the tensor and every error message
  stay as they are.
- **D1 (Joe: decided).** The negation is written `0.0 - p`. That has the same bits as `-p` for every
  nonzero `p`, and gives +0.0 for an omitted key, where `-p` would give −0.0. ASTRA builds without
  `-ffast-math`, so the compiler cannot fold it into `-p`.
- The header comment states:
  - the convention: a published product of inertia is entered as printed, and a source that tabulates the
    tensor's own entries must have its sign flipped;
  - D1: an omitted key gives the tensor its pre-PR bits, +0.0 off the diagonal. That is what keeps M2's
    NESC cases 1 to 10 and the R1 golden unchanged by construction;
  - `aircraft/_template`'s and `aircraft/notional_transport`'s mass plugins use the same convention and
    keep `-p` on purpose. Their −0.0 bits are already frozen into M3's regression, the R1 end-to-end
    fixture's golden and PR 3b's template-identity evidence, and this PR does not touch their expressions.

## 2. Blast radius (pre-registered)

I grepped every file outside `build/`, `.git` and `docs/refs` for `\b(ixy|ixz|iyz)\b`, plus a
case-insensitive variant. 55 tracked files under `tests/` name `mass/constant`, and 54 of them load it:
`tests/configs/*.yaml` (17), the NESC cases 1 to 10, the Python fixtures and tests, the kernel, wind,
wrench and lockstep tests, and the R1 golden test and capture. The 55th, `test_trim_kinematics.py`, only
mentions it in a docstring.

- **Exactly one sets a product key:** `tests/unit/kernel/simulation_physical_config_test.cpp:66`, with
  `ixy: 2.0` and a unit diagonal. Today's tensor is [[1, 2, 0], [2, 1, 0], [0, 0, 1]]. The new one has −2
  where today's has 2, and its eigenvalues are still 3, 1 and −1. It is still refused, at the same path,
  with the same message.
- **Every other config omits the keys,** so under D1 its tensor has exactly today's bits.
- **The R1 golden** (`tests/physics/dynamics/rigid_body_r1_golden_test.cpp`) compares trajectory bits, not
  tensor bits:
  - it loads `mass/constant` with the diagonal config `ixx: 1000, iyy: 1500, izz: 2000`;
  - for each of `kRigidBody6DofR1Initial`'s five initial states, it takes one RK4 step and the post-step
    renormalization;
  - it compares all 13 state components with `kRigidBody6DofR1Result` through `std::bit_cast<std::uint64_t>`,
    65 values in all.
  The tensor reaches those values only through `inertia_b.inverse()` and `inertia_b * omega_b`
  (`core/src/kernel/rigid_body_6dof_dynamics.cpp:10, 51-52`). D1 makes the tensor bitwise identical, so
  every operand is identical and the test passes by construction. It still runs under release (§7).
- **M2's NESC cases 1 to 10** omit the keys too, so they are unchanged by the same construction.
- **Not affected (a different plugin):** M3's frozen regression (`notional_transport_cruise_regression`)
  reads `cruise.yaml` and loads `aircraft/notional_transport/physical/mass`. So does every config with
  `ixz: -10846.5`, including the R1 end-to-end fixture. `test_trim_kinematics.py` sets `ixz = 0` on that
  same plugin.
- Nothing changes under `tests/golden/`, in any tolerance file or under `docs/decisions/`. A value that
  moves is a finding for Joe, never a tolerance change.

## 3. Tests: `tests/unit/mass/constant_mass_test.cpp` (new)

The target is `astra_constant_mass_test`, with `TEST_PREFIX "constant_mass."` and the suite
`ConstantMass`. Its CMake block copies `notional_transport_mass`'s in `tests/unit/CMakeLists.txt`,
including `astra_add_plugin_test`. Like `notional_transport_mass_test.cpp`, it loads the plugin through
`PluginLoader`. Each config sits at `physical.mass` (`Config::at`), so the paths match the simulator's.

Five tests, all matching `-R Mass`:

1. `ConstantMass.PositiveProductsAreNegatedIntoTheTensorBitwise`. The diagonal is 10, 11 and 12; `ixy` is
   1.25, `ixz` 2.5 and `iyz` 0.75. Both entries of each off-diagonal pair have the bits of `-key`, and each
   diagonal entry has its key's bits.
2. `ConstantMass.OmittedProductsLeaveThePrePr4Tensor`. A diagonal-only config gives exactly
   `diag(ixx, iyy, izz)`, bitwise, with +0.0 off the diagonal (`std::signbit` false).
3. `ConstantMass.TwoPointMassesGiveAPositiveProductAndANegativeTensorEntry`, with two 2 kg point masses at
   (+1.5, 0, +0.5) m and (−1.5, 0, −0.5) m:
   - Σ m·x·z = 3 kg·m² > 0;
   - the pair lies on a line through the CG, so its own tensor is singular, and a centered body with
     1 kg·m² about each axis and no products makes it positive-definite;
   - the expected tensor comes from the definition Σ m(|r|²E − r rᵀ) + E, never from a negation: diagonal
     (2, 11, 10), xz entry −3;
   - the plugin gets that diagonal and `ixz` = Σ m·x·z, and its tensor must equal the definition's exactly,
     with a negative xz entry. Every value is exact in binary.
4. `ConstantMass.StillRefusesANonPositiveDefiniteTensorForEitherSign`. `ixy` = +2.0 and −2.0 on a unit
   diagonal are both refused. `config_path()` is `physical.mass`, and `what()` equals today's text exactly:
   `config '<origin>' at 'physical.mass': inertia tensor (ixx, iyy, izz, ixy, ixz, iyz) must be
   positive-definite`.
5. `ConstantMass.StillRefusesANonFiniteProductNamingTheKey`. `ixz: .nan` is refused, with `config_path()`
   `physical.mass.ixz` and `what()` exactly `config '<origin>' at 'physical.mass.ixz': ixz must be finite`.

The `SimulationPhysicalConfig` refusals stay unchanged and still pass.

**`ctest -N` delta (pre-registered).** At `58b681f`, `-R Mass` matches 12 tests, enumerated from source:
- `AircraftMass.ZeroedConfigThrowsConfigErrorNamingTheTodoField`;
- `IcdPack`, 2 tests;
- `DerivedSignals`, 2 tests;
- `SimulationPhysicalConfig.RejectsNonPositiveMassNamingTheKey` and `...RejectsNonFiniteMassNamingTheKey`;
- `WindMultiVehicle`, 2 tests;
- `NotionalTransportMass`, 3 tests.

The candidate must list **17**: those 12 plus the 5 above. Any other count is a finding.

## 4. R17's config-key reference

This PR documents the convention in the plugin. I add a row to the M7 plan's "Obligations carried to PR
plans" table: PR 14's key block for `mass/constant` states the S&L convention (R17's reference half). The
PR body says the same.

## 5. Comments and docs

Two comment-only fixes:
- `aircraft/_template/physical/mass/mass.cpp:17-19`;
- `aircraft/notional_transport/physical/mass/mass.cpp:14-16`.

In each, the NOTE becomes: "plugins/physical/mass/constant reads its ixy/ixz/iyz in this same convention
(M7 PR 4)". The code is unchanged.

The grep found these other statements of `mass/constant`'s convention. Each stays unchanged:
- `PLAN.md:503-509`: the open item, which `/wrap` closes.
- `docs/plans/m7-f16-nesc-oracle.md:226-228`: the "Repository findings (observed at `9c534c5`)" record,
  still true of that commit.
- `docs/plans/m7-pr5-trim.md:234` and `tests/python/test_trim_kinematics.py:135-136`: PR 5's recorded
  reason for `ixz = 0`, which reads as history (Joe, amendment 5).
- `docs/plans/m1-pr3-earth-gravity-mass.md:156-160`, `docs/plans/pr4-notional-aero-turbofan.md:144-145`,
  `docs/reviews/m3-pr1-scaffolding-human.md:27, 50-51, 129, 149` and
  `docs/reviews/m3-pr1-scaffolding-review.md:223`: historical records.
- `docs/architecture.md:558` concerns the notional transport's own plugin. §15 (line 591) says only
  "symmetric positive-definite", and the architecture states no `mass/constant` convention.

## 6. Compatibility and the PR body

`kCoreApiVersion` stays at 10. `MassProps`, `MassPropertiesModel` and the descriptor are unchanged, and no
struct or vtable changes. The PR body carries:
- D1's reasoning, under assumptions;
- the one case that sets a product key, `ixy: 2.0`, which is still refused;
- the sentence: "ASTRA has no release, so a config written for the old meaning has no deprecation path:
  a nonzero product key now enters the tensor with the opposite sign, and no tracked config sets one";
- `kCoreApiVersion` unchanged at 10.

## 7. Checks

- `ctest --preset asan -N -R Mass`: 12 on the base build and 17 on the candidate (§3).
- `ctest --preset asan -R Mass --no-tests=error`.
- `ctest --preset asan -R SimulationPhysicalConfig --no-tests=error`, for the existing refusals.
- `ctest --preset release -R 'Nesc|RigidBodyR1' --no-tests=error`.
- `./scripts/check-full.sh` on the final candidate: exit 0 and `check: OK`. The trimmed summary goes in
  `docs/reviews/evidence/<head>-gate-summary.log`.
- `git diff --check origin/main..HEAD`.

## 8. Order

1. Joe approves the plan, and the "approved" line goes in, in an edit he approves manually.
2. Commit 1: this plan. Push, and open the draft PR.
3. Commit 2: the plugin, the tests, the CMake block, the two comments and the obligations row.
4. The full gate, then commit 3: the gate summary.
5. The adversarial Codex review (`/review-cycle`, at most two correction rounds).
6. `/human-review`, then Joe reads the critical diff and tests.

## 9. `/wrap`

- PLAN.md: close the (M7) `mass/constant` item and add the PR 4 line under M7.
- STATE.md, as main reads it after the merge: PR 6b is eligible, with PR 3b and PR 4 both merged.

## Files

- `plugins/physical/mass/constant/constant.cpp`
- `tests/unit/mass/constant_mass_test.cpp` (new)
- `tests/unit/CMakeLists.txt`
- `aircraft/_template/physical/mass/mass.cpp` and `aircraft/notional_transport/physical/mass/mass.cpp`
  (comments only)
- `docs/plans/m7-f16-nesc-oracle.md` (the obligations row)
- `docs/plans/m7-pr4-mass-convention.md`
- the gate summary
- `PLAN.md` and `STATE.md` (at `/wrap`)
