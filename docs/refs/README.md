# docs/refs/

Human-placed reference documents (specs, standards) that inform this codebase's constants and
formulas but are deliberately **not committed** to git (`.gitignore`: `docs/refs/*` with
`!docs/refs/README.md`, whatever the document's format) — they are large files nobody decided to
carry in the repository's history. Joe places them here locally; an agent session cites them by
page number but does not add, remove, or re-fetch them.

One line per document: what it is, where it came from, what in the tree cites it.

- **MIL-F-8785C** (`MIL-F-8785C.pdf`) — everyspec.com. Cited by
  `tests/golden/wind/mil_f_8785c_dryden.hpp` (scale-length/intensity constants and the Dryden
  spectral forms, section 3.7) and `plugins/physical/wind/dryden/*` (the same constants,
  independently transcribed).
- **NASA/TM-2015-218675 Vol I** (`NASA-TM-2015-218675-Vol-I.pdf`) — NTRS. The assessment report;
  Table 6.3-1 (doc p.15) is the ten sphere-and-brick atmospheric check cases. Cited by ADR-0013 and
  `tests/golden/nesc/generate_nesc_band.py`.
- **NASA/TM-2015-218675 Vol II** (`NASA-TM-2015-218675-Vol-II.pdf`) — NTRS. Appendices. Cited by
  page: B.1.1/Tables 1-2 (sphere mass and aero, doc p.12), B.1.3/Tables 4-5 (brick, doc p.13),
  C.1.1-C.1.10/Tables 25-34 (per-case initial conditions, doc p.56-61), Table 73 (geodetic
  constants and unit conversions, doc p.93), Tables 77-78 (the participants' own largest
  differences and the exclusions the generator honors, doc p.591-592). Cited by
  `tests/golden/nesc/generate_nesc_band.py` and every `tests/physics/nesc/case*.yaml`.
- **NESC atmospheric check-case trajectories** (`Atmospheric_checkcases/`) — NTRS, the report's
  companion data: 17 scenarios, 4-6 CSV trajectories each, one per participating simulation tool,
  ~82 MB. Read only by `tests/golden/nesc/generate_nesc_band.py`, by hand, never by `just check`
  (ADR-0013). Note: gitignored files do not exist inside a git worktree -- pass `--refs` pointing at
  the main checkout.
- **NESC initial conditions workbook** (`Initial_Conditions.xlsx`) — NTRS. A working spreadsheet;
  its constants are rounded back-conversions (Re = 20925646.99508 ft, omega = 7.292113023868e-5
  rad/s) and disagree with Vol II Table 73 in the 8th digit. **Table 73 is the authority**; the
  workbook is cited nowhere in the tree.
