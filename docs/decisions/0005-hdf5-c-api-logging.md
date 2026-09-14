# ADR-0005 — Logging to HDF5 through the C API with an internal RAII wrapper

Status: **accepted**, 2026-09-14. Elaborates ADR-0000 row "Tooling" (structured logging); `docs/architecture.md` §9 Logger.

## Context

The logger samples every registered signal into preallocated ring buffers (one per rate group) each frame
and a writer drains them to disk — inline in `AsFastAsPossible`, from a separate non-RT thread in
`RealTime`. The file format must hold typed columns with unit attributes, the config, and the git describe
so any log is reproducible, be readable from pandas, and be writable from an apt-only toolchain.

## Decision

Logs are HDF5 files written through the C API from `libhdf5-dev` (1.10.10 in Ubuntu 24.04) behind a small
internal RAII wrapper in `core/signals/` that owns handles and turns error codes into typed init errors.
One dataset per rate group, columns named by signal, units in attributes, config and git describe stored as
file attributes; the loaded plugin set with versions and build strings is recorded too. `astra.log.load`
returns pandas frames per rate group.

## Rejected alternatives

- **HighFive (C++ header-only wrapper).** Not packaged in the 24.04 repos; the apt-only toolchain rule
  wins. The needed surface (create file, dataset, attribute, append chunk) is small enough to wrap by hand.
- **Parquet.** Good columnar analytics format, but there is no C++ writer in apt (Arrow is not packaged),
  and it has no natural place for per-file attributes and multiple rate groups.
- **CSV.** Human-readable, but no types, no attributes, no rate groups, and slow to write at 1 kHz.
  A CSV export from Python is a one-liner on top of the pandas frames.

## Consequences

- The per-frame path only writes into preallocated ring buffers; the HDF5 calls happen in the drain, never
  in `step`. Ring-buffer overflow is a health halt (§15): a silently truncated log is not a log.
- The RAII wrapper is the only place `H5*` calls appear; it has its own unit tests with a temp file.
- M5 acceptance depends on this: replay from a log, Monte Carlo one file per run plus summary parquet
  (written from Python, where Arrow is available via pip).
