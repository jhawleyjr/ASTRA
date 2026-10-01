# M5 PR 8 (Tier 2): `astra.fgbridge`, the FlightGear FGNetFDM v24 bridge

Recovered from `~/.claude/plans/pasted-content-id-31cb-m5-pr-velvety-summit.md` on 2026-09-30. Approval: the file records none; the work merged as #70. Never committed: neither the file nor git history says why.

## Context

This is the PR 8 row of `docs/plans/m5-tooling.md` (Q21 to Q23, R18 to R20), bound by ADR-0002, ADR-0010,
ADR-0025 and ADR-0026. It covers architecture §16's M5 clause "FlightGear receives valid packets". A decoder test
checks this over a real localhost UDP socket; FlightGear is never launched. The oracle is FlightGear 2024.1.7's
`net_fdm.hxx` and `native_structs.cxx`, which Joe placed in the main checkout's `docs/refs/`. Base: `origin/main`
`3b5ca4a`, worktree branch `worktree-m5-pr8-fgbridge`. There is no `core/` change and no new dependency (`struct`
and `socket` only). The golden guard is armed for this session; I touch only
`tests/golden/flightgear/fgnetfdm_v24.py` under `tests/golden/`.

## Oracle findings (read in plan mode, both files at their main-checkout paths)

- The layout from the header, lines 44 to 123, is 58 members. The sizes add up to 408 with no implicit padding:
  the explicit `padding` member is at line 45, the doubles are at offsets 8, 16 and 24, and every later member is
  4 bytes. The struct's 8-byte alignment divides 408, so there is no tail padding either. The cross-check script
  confirms this with g++.
- Byte order: `htond` (cxx:56) swaps the two 32-bit halves and `htonl`s each, which reverses all 8 bytes, so each
  double is big-endian. `htonf` (cxx:73) is `htonl` of the float's bits. Integers use `htonl` (cxx:199 onward), and
  so does `warp`, which is `int32`. The whole packet is `>`.
- Version check: cxx:354. FlightGear reads the packet only when `version == 24`.
- Checking the header comments against how FGFDM2Props uses each field turns up **no numeric unit disagreement**.
  Joe decides the gaps and oddities below; I don't resolve them myself:
  1. `vcas` (h:62, "calibrated airspeed"): the header gives no unit. FGFDM2Props stores it straight into
     `velocities/airspeed-kt` (cxx:391), and FGProps2FDM fills it from the same property (cxx:118), so it is knots.
  2. `v_body_u/v/w` (h:67 to 69, "ECEF velocity in body frame"): the header gives no unit. They go to
     `uBody/vBody/wBody-fps` (cxx:396 to 398), so feet per second.
  3. `v_north/east/down` (h:64 to 66) are described as "in local/body frame, fps". FGFDM2Props writes them to
     `speed-north/east/down-fps` (cxx:393 to 395), which is local NED.
  4. `beta` (h:56, rad): the wire unit agrees. However, FGFDM2Props writes `beta * RAD_TO_DEG` into a property
     named `orientation/side-slip-rad` (cxx:383), while FGProps2FDM reads `orientation/beta-deg` (cxx:109). This is
     an inconsistency inside FlightGear and does not affect the wire format.
  5. `A_X/Y/Z_pilot` (h:72 to 74, ft/s², body) agree with `pilot/x-accel-fps_sec` (cxx:399 to 404). There is a
     semantic gap: FlightGear means the acceleration at the pilot station, while ASTRA's `ax ay az` are the specific
     force at the CG from the previous frame (ICD, ADR-0026). This is disclosed, not changed. Joe asked for a read-only
     lookup in `/home/joe/repos/flightgear-2024.1.7/src/FDM` of what produces `accelerations/pilot/*`: whether it
     includes gravity, and whether it is taken at the pilot station, with file and line. The finding goes to Joe and
     the mapping stays as it is.
  6. `agl` (h:51): FGFDM2Props uses the value when it is above -9000. Otherwise it computes AGL from its own terrain
     (cxx:366 to 374). Q22 fixed `agl = altitude`.
  7. The engine, tank and wheel loops take their counts straight from the packet, with no bound against
     `FG_MAX_*` (cxx:311 to 336 and 411 to 452). The bridge sends counts of 0. `cur_time` and `warp` are never
     applied (commented out at cxx:454 to 462), and `visibility` is only byte-swapped (cxx:340), never stored.

## Files

1. **`tests/golden/flightgear/fgnetfdm_v24.py`**: the transcribed table, data only.
   - `RELEASE`, and the SHA-256 of both files (net_fdm.hxx `7b25b342…5460`, native_structs.cxx `ea8fda12…e1440`).
   - `FG_NET_FDM_VERSION = 24` (h:28), `SIZE = 408`, `ALIGNMENT = 8`, and `BYTE_ORDER = "big"` with its cxx citations.
   - `FIELDS`: one record per member, in header order. Each record holds the name, the C type (`uint32_t`,
     `int32_t`, `float`, `double`), the count (1 for a scalar, or 4/4/3 per `FG_MAX_*` at h:39 to 41), the byte
     offset, the header line, the verbatim header comment, the unit the table states, and the FGFDM2Props line(s)
     with the property it sets. Any gap from the findings above is noted on its field.
   - The stated units are `rad`, `m`, `kt`, `ft/s`, `ft/s^2`, `rad/s`, or `None` where the field is unused or
     normalized.
   - **Joe's decision (plan mode):** the table uses the reader's units: `vcas` kt (cxx:391), `v_body_*` ft/s
     (cxx:396 to 398), and `v_north/east/down` as NED ft/s (cxx:393 to 395). Each cites its FGFDM2Props line and
     notes that the header is silent or ambiguous. `v_body_u/v/w` (h:67 to 69, "ECEF velocity in body frame") come
     from the Earth-relative body velocity, which is the state's `v_b`, not an air-relative velocity. The table
     entry says so.
2. **`scripts/fgnetfdm_layout_check.py`**: the by-hand cross-check, never run by `just check` (the ADR-0013
   pattern). It lives outside `tests/golden/` because of the one-path authorization.
   - It resolves `docs/refs` in the main checkout through `git rev-parse --git-common-dir` (lessons 2026-09-17 and
     2026-09-20), or from `--refs`.
   - It checks both SHA-256s against the table.
   - It writes a one-line stub `simgear/misc/stdint.hxx` (`#include <stdint.h>`) into a temp directory.
   - It generates a TU that prints `sizeof`, `alignof`, and each table member's `offsetof` and `sizeof`, then builds
     it with `g++ -std=c++17` and runs it.
   - It compares every offset and size with the table, exits nonzero on any mismatch, and prints a report. That
     report goes into the PR body.
3. **`python/astra/fgbridge.py`**: pyright strict. Its docstring lists all 58 fields with their source and
   conversion.
   - `pack(values) -> bytes`: a hand-written `>`-format `struct` string plus unit constants (ft = 0.3048 m exactly,
     kt = 1852/3600 m/s). It is written independently of the table; `astra` never imports `tests/`.
   - The mapping (Q22), with each value from `v<i>.derived.*` in SI:
     - `longitude`/`latitude` ← `lon`/`lat` (rad, doubles, exact)
     - `altitude` ← `altitude` (m, double)
     - `agl` ← `altitude`
     - `phi theta psi alpha beta` (rad)
     - `vcas` ← `cas` in kt
     - `climb_rate` ← `vertical_speed` in ft/s
     - `v_north/east/down` ← `v_n_0..2` in ft/s
     - `v_body_u/v/w` ← `u v w` in ft/s. Per the ICD these are the ground-relative (Earth-relative) body velocity
       `v_b`, not air-relative; the docstring states this source (Joe).
     - `A_X/Y/Z_pilot` ← `ax ay az` in ft/s²
     - `version = 24`
     - Everything else is 0: padding, Euler rates, stall, slip, the three counts, every engine/tank/gear array,
       `cur_time`, `warp`, `visibility` and the surfaces.
   - **"Frame k's values"** have one definition: row k of the run log, which is also what the registry's derived
     signals hold after `step()` completes frame k (ADR-0025 frame start at t_k).
   - **ACCEL_INVALID:** while the bit is set (always at frame 0), the packet carries exactly `0.0` in
     `A_X/Y/Z_pilot`, which is the ICD's defined placeholder with the conversion applied. Every other field is packed
     normally. Other invalid bits pass through the ICD's 0 in the same way; this is stated in the docstring.
   - **Refusal (R20):** any status_flags value with `ASTRA_ICD_STATUS_LATLON_NOT_MODELED` (0x2) raises
     `BridgeError` naming that bit, before any packet is sent. So does a source without the 25 `v<i>.derived.*`
     signals (no physics).
   - **`LiveBridge(sim, *, host="127.0.0.1", port=5500, divisor=16, vehicle=0, pace=False)`** is a context manager
     that owns the UDP socket.
     - `step()` calls `sim.step()`. When the step does not halt, frame k = `clock.frame - 1`: the bridge reads the
       registry with `registry.find`, checks status_flags, and sends if `k % divisor == 0`. It returns the
       `HaltReason`. A halted attempt sends nothing, the same as the log, which has no row for it.
     - `run(frames)` loops `step()`.
     - If the sim's clock moved outside the bridge, `step()` raises `BridgeError` naming the missed frames. Every
       due frame must be seen.
   - **`send_log(path, *, host, port, divisor=16, vehicle=0, pace=False) -> int`**:
     - It calls `astra.log.load(path)` strictly and never uses `allow_incomplete`.
     - It finds the rate group that holds the derived columns and requires `divisor` to be a multiple of that
       group's divisor.
     - It checks status_flags over every row before sending anything.
     - It sends the rows whose `frame % divisor == 0`, in order, and returns the packet count.
   - **`divisor`:** an `int` of at least 1; a `bool` is refused. Default 16 (Q23).
   - **Pacing:** Python-only and off by default. The packet for frame k is not sent before
     `start + (k - k0) * dt` on `time.monotonic()`. It never feeds sim time.
4. **`tests/python/test_fgbridge.py`**: a real localhost UDP receiver bound to `127.0.0.1:0` with a timeout. After
   each batch it drains the socket and asserts no extra datagram. Packet counts stay small, well under the receive
   buffer.
   - The decoder is built from the table only. `importlib` loads the golden module; the format comes from each
     field's ctype, count and offset, with `x` padding wherever an offset jumps. The test asserts
     `calcsize == SIZE` and that the offsets are contiguous with the sizes.
   - The fixture is `cruise.yaml` loaded as a dict, with `p_n` changed to a nonzero lat/lon (so lon/lat are
     discriminating) and `sim.frames` kept short, run with a `log` block in `tmp_path`.
   - **R18:**
     - Every datagram is 408 bytes, `version == 24` and `padding == 0`.
     - Every mapped field equals log row k converted with the unit the table states. Doubles must match exactly;
       floats must fall within ½ float32 ulp of the float64 expectation (test-side constants keyed by the table's
       unit strings).
     - Every unmapped field is exactly 0.
     - Cross-field checks use only decoded packet values (Joe), so the test does not share the packer's sign
       conventions: `climb_rate == -v_down` within float32 rounding, and (v_north, v_east, v_down) equal
       C_nb^T (v_body_u, v_body_v, v_body_w) rotated with the packet's own phi, theta and psi, within float32-level
       tolerance.
     - Float tolerance is up to 1 float32 ulp (Joe), not ½, because the test's float64 conversion and the packer's
       can round to different sides of a float32 boundary.
   - **R19:** the packets from `divisor` 1, 7 and 16 are exactly the log rows with `frame % d == 0`. Packet i is
     compared against row `i*d`.
   - **Carry-over:** the live source and the log source of the same run send byte-identical packet lists. A spy on
     `astra.log.load` asserts that no `allow_incomplete` is passed, and a log with `complete` tampered to 0 is
     refused (`IncompleteLogError`).
   - **v_body source (Joe):** Dryden wind is on (cruise.yaml, seed 42) and wind_n is nonzero in the rows used.
     `v_body_*` must equal the logged frame-start state `v_b` (the `v0.state.*` velocity columns at row k), converted.
     They must also differ, by more than float32 rounding, from the air-relative `v_b − C_bn·wind_n` computed in numpy
     from the same row. So a mutation that packs the air-relative velocity fails.
   - **ACCEL:** row 0 has bit 0x1 set and its packet has `A_* == 0.0` exactly; frame 16's packet is nonzero with the
     bit clear.
   - **R20:** a flat-Earth variant (`earth/flat` plus flat-compatible gravity, keys checked against the plugins when
     implementing) is refused. The live source raises at its first frame and the log source raises at load, each
     naming `LATLON_NOT_MODELED`, and the socket receives 0 datagrams.
   - **Other refusals:** a bad `divisor` (0, -1, 1.5, True), a frame gap in the live source, a `divisor` that is not
     a multiple of the log group's divisor, and a source with no derived signals.
   - **Pacing:** tested with monkeypatched `time.monotonic` and `time.sleep`, with no real wait.
5. **Docs:**
   - The PR 8 line in `PLAN.md`.
   - A `docs/process/validation.md` row, if PR 7's pattern applies.
   - `docs/reviews/m5-pr8-fgbridge-review.md`.
   - `docs/reviews/evidence/<head>-gate-summary.log`.

## Sequence

1. Write the table and the cross-check script, then run the script once and keep its output. Commit, push and
   open the draft PR (`docs/templates/pr-description.md`).
2. Write `fgbridge.py` and the tests. Use `.venv/bin/pytest tests/python -k fgbridge` to iterate, then commit and
   push.
3. Scratch mutations, applied with Edit, grepped to confirm, and run in a separate call (lesson 2026-09-26). Each
   must be killed:
   - drop the live flat-Earth check
   - drop the log flat-Earth check
   - `allow_incomplete=True`
   - live reads frame k+1
   - log uses row k+1
   - remove the divisor check
   - pack a nonzero A_* while ACCEL is invalid
   - little-endian packing
   - swap v_north/v_east
   - pack the air-relative velocity into v_body
   - remove the frame-gap check
   - kt→m/s on vcas
   - flip the climb_rate sign
   - swap v_body_u/v_body_w
4. Run `./scripts/check-full.sh` in the background, block on the real pid, and read `check: OK` and `EXIT=` from the
   log. Commit the trimmed gate summary.
5. Codex Tier 2 review via `/review-cycle` (general plus adversarial, fresh context, commit first). Allow at most
   two correction rounds.
6. Write the PR body with `/human-review`. Commands, exits, counts, the mutation table, the cross-check output,
   and the Codex verdicts and thread IDs go there. "Needs Joe's decision" lists the golden table's provenance, the
   oracle findings 1 to 7, and the ACCEL_INVALID definition. Then run `/wrap`.

## Verification

- `.venv/bin/pytest tests/python -k fgbridge` exits 0 (R18 to R20 and the carry-overs), and every mutation above
  is killed.
- `python3 scripts/fgnetfdm_layout_check.py` reports sizeof 408 and every offset equal to the table (by hand; its
  output goes in the PR body).
- `./scripts/check-full.sh` exits 0 with `check: OK`, and the trimmed summary is committed.
- `git diff origin/main --stat -- tests/golden` shows exactly one file.
