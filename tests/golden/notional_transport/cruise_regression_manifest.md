# Cruise regression capture manifest (M3 PR 8, D20)

Captured 2026-09-22T23:00:03Z by `tests/capture/capture_cruise_regression.sh`,
per `docs/plans/pr8-cruise-regression.md`.

- `baseline_revision`: `553700f` -- `origin/main` this PR branched from.
- `capture_revision`: `4ce6a9fb6280307e9d178131dcc45e0470bfbf06` -- the clean commit the harness ran from.
- `cross_preset_agreement`: `true` (debug release asan static-plugins, byte for byte); no halt.
- Run: `aircraft/notional_transport/configs/cruise.yaml` (blob `74f8922f965ac2539f69582005e64af53115a6e1`) through
  `Simulation::from_config`; the 13-double rigid-body state sampled at frame 0 and after
  each of 300 `run(1000)` calls (1 Hz, 300 s, dt = 1 ms), frozen as `std::bit_cast`
  bit patterns. Run definition: `tests/support/cruise_regression_run.hpp` (blob
  `a7902d481129423b18519bc5767430c98cadf17f`); harness: `tests/capture/capture_cruise_regression.cpp` (blob
  `377a0714fe0dd3a233d3fbdf5c3c8e7f64c5114f`).
- **What this trajectory is.** The initial state and controls are the still-air trim
  (`astra.trim`, D19; residual 8.3e-17 without wind). The config keeps its Dryden wind
  (seed 42), so the run starts in turbulence: the frame-0 gust is
  `wind_n = (-3.08, 0.91, 6.63)` m/s (stationary start), and the residual with that gust
  latched is 3.77 m/s^2. This is a turbulence response from trim, not a cruise hold.

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

