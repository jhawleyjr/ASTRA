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
