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
  Table 6.3-1 (doc p.15) is the ten sphere-and-brick atmospheric check cases. Cited by ADR-0013.
  PLAN.md's M2 PR 3b bullet is a further citer once it lands: `tests/golden/nesc/
  generate_nesc_band.py` will cite Table 6.3-1 by page; not yet in the tree.
- **NASA/TM-2015-218675 Vol II** (`NASA-TM-2015-218675-Vol-II.pdf`) — NTRS. Appendices. Cited by
  `tests/plugins/nesc_aero/nesc_aero.cpp`: B.1.1/Table 2 (sphere drag, doc p.12), B.1.3/Table 5
  (brick damping, doc p.13), and the check-case 7/case 3 numeric cross-checks in that file's own
  header. PLAN.md's M2 PR 3b bullet is a further citer once it lands: C.1.1-C.1.10/Tables 25-34
  (per-case initial conditions, doc p.56-61), Table 73 (geodetic constants and unit conversions,
  doc p.93), Tables 77-78 (the participants' own largest differences and the exclusions the
  generator honors, doc p.591-592) -- `tests/golden/nesc/generate_nesc_band.py` and every
  `tests/physics/nesc/case*.yaml`; none of those files are in the tree yet.
- **NESC atmospheric check-case trajectories** (`Atmospheric_checkcases/`) — NTRS, the report's
  companion data: 17 scenarios, 4-6 CSV trajectories each, one per participating simulation tool,
  ~82 MB. Not read by anything in the tree yet -- `tests/golden/nesc/generate_nesc_band.py`
  (PLAN.md's M2 PR 3b bullet) will be the first and only reader, by hand, never by `just check`
  (ADR-0013). Note for whenever that generator is written: gitignored files do not exist inside a
  git worktree -- pass `--refs` pointing at the main checkout.
- **NESC initial conditions workbook** (`Initial_Conditions.xlsx`) — NTRS. A working spreadsheet;
  its constants are rounded back-conversions (Re = 20925646.99508 ft, omega = 7.292113023868e-5
  rad/s) and disagree with Vol II Table 73 in the 8th digit. **Table 73 is the authority**; the
  workbook is cited nowhere in the tree.
