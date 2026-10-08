# docs/refs/

Human-placed reference documents (specs, standards) that inform this codebase's constants and
formulas but are deliberately **not committed** to git (`.gitignore`: `docs/refs/*` with
`!docs/refs/README.md`, whatever the document's format) — they are large files nobody decided to
carry in the repository's history. Joe places them here locally; an agent session cites them by
page number but does not add, remove, or re-fetch them. Gitignored files do not exist inside a git
worktree: read them from the main checkout.

One line per document: what it is, where it came from, what in the tree cites it.

- **MIL-F-8785C** (`MIL-F-8785C.pdf`) — everyspec.com. Cited by
  `tests/golden/wind/mil_f_8785c_dryden.hpp` (scale-length/intensity constants and the Dryden
  spectral forms, section 3.7) and `plugins/physical/wind/dryden/*` (the same constants,
  independently transcribed), and by the turbulence configs
  (`aircraft/notional_transport/configs/cruise_{light,moderate}_turbulence.yaml`).
- **NASA/TM-2015-218675 Vol I** (`NASA-TM-2015-218675-Vol-I.pdf`) — NTRS. The assessment report;
  Table 6.3-1 (doc p.15) is the ten sphere-and-brick atmospheric check cases. Cited by ADR-0013.
- **NASA/TM-2015-218675 Vol II** (`NASA-TM-2015-218675-Vol-II.pdf`) — NTRS. Appendices. Cited by
  `tests/plugins/nesc_aero/nesc_aero.cpp`: B.1.1/Table 2 (sphere drag, doc p.12), B.1.3/Table 5
  (brick damping, doc p.13), and the check-case 7/case 3 numeric cross-checks in that file's own
  header. Also cited by `tests/golden/nesc/generate_nesc_band.py` (Table 73, geodetic constants and
  unit conversions, doc p.93; Tables 77-78, the participants' largest differences and the exclusions
  the generator honors, doc p.591-592; for cases 11 to 16, Tables 35-40, 42 and 43's durations, doc
  p.62-67, and Tables 79-80's footnotes, doc p.593-594, per ADR-0028), the `tests/physics/nesc/case*.yaml`
  configs (per-case initial conditions, C.1.1-C.1.10/Tables 25-34, doc p.56-61),
  `tests/physics/nesc/nesc_f16_bands_test.cpp` (cases 11 to 16's initial conditions, Tables 35-40, 42
  and 43) and `plugins/physical/wind/table`.
- **NESC atmospheric check-case trajectories** (`Atmospheric_checkcases/`) — NTRS, the report's
  companion data: 17 scenarios, 4-6 CSV trajectories each, one per participating simulation tool,
  ~82 MB. Read only by `tests/golden/nesc/generate_nesc_band.py`, by hand, never by `just check`
  (ADR-0013); its `--refs` defaults to the main checkout's copy. Cases 11 to 16 (the F-16) are M7's
  oracle (PLAN.md, ADR-0028), reduced to `tests/golden/nesc/case1*_band.hpp` in M7 PR 8; case 17 is
  out of scope.
- **NESC initial conditions workbook** (`Initial_Conditions.xlsx`) — NTRS. A working spreadsheet;
  its constants are rounded back-conversions (Re = 20925646.99508 ft, omega = 7.292113023868e-5
  rad/s) and disagree with Vol II Table 73 in the 8th digit. **Table 73 is the authority**; the
  workbook is cited nowhere in the tree.
- **JSBSim 737** (`aircrafts/737.xml`) — JSBSim's example aircraft ($Revision: 1.43 $, Dave Culp /
  Aeromatic, GPL). The source of the notional transport's numbers: cited by
  `aircraft/notional_transport/` (aero, turbofan, mass and every config),
  `embedded/autopilot/limits.hpp`, `tests/python/test_trim.py` and the notional transport's unit
  tests.
- **FlightGear sources** (`net_fdm.hxx`, `native_fdm.cxx`, `native_structs.cxx`) — FlightGear
  2024.1.7. The `FGNetFDM` v24 layout: cited by `python/astra/fgbridge.py`,
  `tests/golden/flightgear/fgnetfdm_v24.py` (which pins both reference files' SHA-256) and
  `scripts/fgnetfdm_layout_check.py`, which compiles `net_fdm.hxx` by hand, never in `just check`.
- **Framework intent audit** (`astra_framework_intent_and_state_audit.md`) — Joe's stated intent,
  captured from a Cowork review session 2026-09-19. Cited by ADR-0019.
- **Textbooks** (`textbooks/`):
  - Stevens, Lewis & Johnson, *Aircraft Control and Simulation*, 3rd ed. (2015). Cited by
    `core/include/astra/util/frames.hpp`, `rigid_body_6dof_dynamics.*`, `earth/round` and
    `gravity/inverse_square`, the aircraft mass plugins, architecture §6 and ADRs 0001, 0003, 0016 to 0018, 0020 and 0021.
  - Zipfel, *Modeling and Simulation of Aerospace Vehicle Dynamics*, 4th ed. Cited by ADRs 0016 to
    0018, 0020 and 0021.
  - Nutaro, *Building Software for Simulation*. Cited by `core/include/astra/kernel/integrator.hpp`,
    `core/include/astra/interfaces/dynamics_model.hpp`, architecture §6, ADR-0016 and ADR-0021.
  - Groves, *Principles of GNSS, Inertial, and Multisensor Integrated Navigation Systems*, 2nd ed.
    Cited by `core/include/astra/util/geodesy.hpp`, the `inverse_square` and `wgs84_j2` gravity
    plugins and `tests/unit/gravity/wgs84_j2_test.cpp`; future work (sensors, M9).
  - Titterton & Weston, *Strapdown Inertial Navigation Technology*, 2nd ed. Cited by
    `plugins/physical/earth/round/round.cpp` and `tests/unit/earth/earth_kinematics_test.cpp`;
    future work (sensors, M9).
  - NIMA TR8350.2, *Department of Defense World Geodetic System 1984*, 3rd ed., amendment 1
    (`wgs84fin.pdf`). Cited by `core/include/astra/util/geodesy.hpp` and
    `tests/unit/gravity/wgs84_j2_test.cpp`.
  - MIL-HDBK-1797, *Flying Qualities of Piloted Aircraft*. Cited by
    `tests/python/test_autopilot_design.py` (Appendix A bandwidth); future work (handling qualities,
    M8).
  - Moin, *Fundamentals of Engineering Numerical Analysis*, and Press et al., *Numerical Recipes*.
    Cited by `docs/lessons.md` only.
