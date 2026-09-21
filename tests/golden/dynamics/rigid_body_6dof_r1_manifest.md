# R1 / R1-Sim capture manifest (ADR-0021)

Captured 2026-09-21T07:17:52Z by tests/golden/dynamics/capture_r1_golden.sh,
per docs/plans/pr4a-dynamics-model.md's "R1 golden capture" section.

- `baseline_revision`: `26ad594` -- the pre-change revision this
  golden's arithmetic actually is. Verified byte-identical to this capture in
  `core/`, `plugins/`, `cmake/`, the top-level build files, and
  `aircraft/notional_transport/` immediately before the harness commit below.
- `capture_revision`: `553f23e2f5a9a1b914605f4443976eb03c1281f6` -- the actual commit the harness ran
  from (the harness file itself, and this worktree's own docs bookkeeping ahead of
  it, postdate `baseline_revision`; the captured arithmetic does not).
- `cross_preset_agreement`:
  - r1: `true`
  - r1sim: `true`
- Capture harness: `tests/golden/dynamics/capture_r1_golden.cpp`, blob `96e180dd2be097b609718e71205ec97e40e67433`.
- Frozen fixture: `tests/golden/dynamics/cruise_end_to_end_r1_fixture.yaml`, blob `201b6a2f51f301058eef9f2aa7e47428d36f85b8` (copied verbatim from `aircraft/notional_transport/configs/cruise.yaml` at capture time).
- Sampled initial-state bit patterns (R1): see `kRigidBody6DofR1Initial` in
  `rigid_body_6dof_r1_golden.hpp` -- the actual values, not a description.
- Applied wrench (R1, an input, not a captured result): see
  `kRigidBody6DofR1Wrench` in the same file.
- R1's five sampled initial states are hardcoded `std::uint64_t` bit patterns in
  `capture_r1_golden.cpp`'s own `sample_states()`, not computed at capture time. An
  earlier version of that function built samples 1/2/4's quaternions at runtime via
  `std::sin`/`std::cos`/`Eigen::Vector4d::normalize()`; a first capture run found
  debug and release disagreeing by 1 ULP on sample 2's `w`/`x` components (diagnosed:
  the six underlying `std::sin`/`std::cos` results were bit-identical across presets;
  only the multiply-add combination (a `ca*cb*cc + sa*sb*sc`-shaped expression)
  differed -- `-ffp-contract` contracting that expression shape differently by
  optimization level, not a hardware FMA effect, since this build passes no
  `-march`/`-mfma`).
  The current hardcoded bit patterns were taken from a **release**-preset run of that
  prior, runtime-computed code -- computed once, frozen, never recomputed by this
  program again. See `docs/lessons.md` for the general rule this became.

## Per-preset build metadata

### debug
```
EIGEN_VERSION=3.4.0
GLIBCXX=20240908
Ubuntu clang version 18.1.3 (1ubuntu1)
Target: x86_64-pc-linux-gnu
Thread model: posix
InstalledDir: /usr/bin
target_triple=x86_64-pc-linux-gnu
ASTRA_SANITIZERS:STRING=
ASTRA_STATIC_PLUGINS:BOOL=OFF
CMAKE_BUILD_TYPE:STRING=Debug
```

### release
```
EIGEN_VERSION=3.4.0
GLIBCXX=20240908
Ubuntu clang version 18.1.3 (1ubuntu1)
Target: x86_64-pc-linux-gnu
Thread model: posix
InstalledDir: /usr/bin
target_triple=x86_64-pc-linux-gnu
ASTRA_SANITIZERS:STRING=
ASTRA_STATIC_PLUGINS:BOOL=OFF
CMAKE_BUILD_TYPE:STRING=Release
```

### asan
```
EIGEN_VERSION=3.4.0
GLIBCXX=20240908
Ubuntu clang version 18.1.3 (1ubuntu1)
Target: x86_64-pc-linux-gnu
Thread model: posix
InstalledDir: /usr/bin
target_triple=x86_64-pc-linux-gnu
ASTRA_SANITIZERS:STRING=address,undefined
ASTRA_STATIC_PLUGINS:BOOL=OFF
CMAKE_BUILD_TYPE:STRING=Debug
```

### static-plugins
```
EIGEN_VERSION=3.4.0
GLIBCXX=20240908
Ubuntu clang version 18.1.3 (1ubuntu1)
Target: x86_64-pc-linux-gnu
Thread model: posix
InstalledDir: /usr/bin
target_triple=x86_64-pc-linux-gnu
ASTRA_SANITIZERS:STRING=address,undefined
ASTRA_STATIC_PLUGINS:BOOL=ON
CMAKE_BUILD_TYPE:STRING=Debug
```

