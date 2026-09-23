# M3 PR 8 pre-arm capture dry run (candidate b4c5f44 working tree, 2026-09-22)

Not a capture: nothing was written under the golden directory. The capture script's own
build/run/cross-check steps (`tests/capture/capture_cruise_regression.sh` steps 4-5), run by hand into
the session scratchpad, so a cross-preset disagreement would surface before Joe arms the capture.
Built from the working tree that became `b4c5f44` (clang-format whitespace changes applied mid-run;
no semantic difference).

For each preset: `cmake --preset <p>`, full build, `--target capture_cruise_regression`, then
`capture_cruise_regression trajectory <repo>/aircraft build/<p>/plugins`.

| Preset | Harness wall time | Halt | SHA-256 of trajectory output |
|---|---|---|---|
| debug | 38 s | none | `bd9b9fbe2ce68aa569129d2be2565491daea42ee18e547f3c85475a973142649` |
| release | < 1 s | none | same |
| asan | 172 s | none | same |
| static-plugins | 79 s | none | same |

`cmp`: debug == release == asan == static-plugins, byte for byte (301 samples x 13 states).

Excursions over the 300 s run (decoded from the debug output; relative to the t = 0 trim sample):

- u (body x velocity): max |du| = 7.97 m/s (range -2.55 .. +7.97)
- altitude: -141.3 .. +120.1 m (max |dh| = 141.3 m)
- pitch: theta0 = 1.351 deg (trim alpha), max |dtheta| = 3.05 deg (range -2.59 .. +3.05)
- also: max |phi| = 5.1 deg; heading at 300 s = -5.9 deg; max |omega_b| component = 0.151 rad/s
