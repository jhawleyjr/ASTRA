#!/usr/bin/env python3
"""Wall-clock symbol scan (M5 PR 10; docs/architecture.md §15, ADR-0010, ADR-0023).

The rule: in core/, plugins/ and aircraft/ (and embedded/, which ADR-0023's second site lives in) no
translation unit reads the wall clock, sleeps, or waits on a timeout, except the two ADR-0023
deadline sites. This makes the rule a check on compiled code instead of prose.

For every compile-database entry whose source is under core/, plugins/, aircraft/ or embedded/, it
runs `nm -u -C` on the entry's object and flags any undefined reference to:

- a wall-clock read: clock, clock_gettime, gettimeofday, time, timespec_get, ftime, and
  std::chrono::<any>_clock::now() (steady_clock, system_clock; high_resolution_clock is an alias of
  system_clock in libstdc++, so it shows as that symbol);
- a sleep or timed wait, which ties behavior to real time without reading a clock: nanosleep,
  clock_nanosleep, usleep, sleep, pthread_cond_timedwait, pthread_cond_clockwait, sem_timedwait,
  sem_clockwait, the timed and clock variants of pthread mutex/rwlock/join locking, and libstdc++'s
  std::__atomic_futex_unsigned_base::_M_futex_wait_until (what std::future::wait_for and
  wait_until call).

std::this_thread::sleep_for/sleep_until and std::condition_variable::wait_for/wait_until have no
symbols of their own on this toolchain (clang-18 with libstdc++): they are header code that ends in
nanosleep / clock_nanosleep / pthread_cond_clockwait, so those are what a translation unit shows.
The names were checked with nm, not assumed (tests/python/test_wallclock_scan.py compiles one case
per family and fails if a name stops appearing).

The allowlist is by source file and by symbol: each ADR-0023 site may reference only what it
references today. An allowlisted symbol that its file no longer references is a stale entry and
fails, and so does a scan that finds no in-scope translation unit, an object that is missing (the
recipe builds first), or an unreadable compile database; none of them may pass silently.

What the scan cannot see (it reads undefined references of compiled objects):
- inline assembly and intrinsics that read a counter, such as rdtsc / __rdtsc;
- a raw syscall(), e.g. syscall(SYS_clock_gettime, ...) or syscall(SYS_futex, ...) with a timeout
  (syscall itself is not flagged: it is too broad, and both ADR-0023 sites use it for the futex);
- a symbol resolved at run time (dlsym), or a clock reached through a library another object links;
- timeouts on poll/select/epoll_wait and similar, which are legitimate I/O calls;
- a source no target builds, and any translation unit outside core/, plugins/, aircraft/ and
  embedded/ (tests/ and the Python bindings are out of ADR-0010's scope).
So it is a tripwire for the ordinary ways of reading a clock, not a proof; the ADR-0023 sites'
use of the clock stays a review matter (one function each).

Usage: python3 scripts/wallclock_scan.py [--root DIR] BUILD_DIR
BUILD_DIR holds compile_commands.json (a configured, built CMake preset directory); DIR is the
repository root the source paths are made relative to (default: this script's repository).
Exit 0 with a summary line, or exit 1 printing `wallclock-scan: <source>: <symbol>` for each hit.
Stdlib only.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

SCOPE_ROOTS = ("core", "plugins", "aircraft", "embedded")

WALL_CLOCK_SYMBOLS = frozenset(
    {"clock", "clock_gettime", "gettimeofday", "time", "timespec_get", "ftime"}
)
SLEEP_AND_WAIT_SYMBOLS = frozenset(
    {
        "nanosleep",
        "clock_nanosleep",
        "usleep",
        "sleep",
        "pthread_cond_timedwait",
        "pthread_cond_clockwait",
        "sem_timedwait",
        "sem_clockwait",
        "pthread_mutex_timedlock",
        "pthread_mutex_clocklock",
        "pthread_rwlock_timedrdlock",
        "pthread_rwlock_clockrdlock",
        "pthread_rwlock_timedwrlock",
        "pthread_rwlock_clockwrlock",
        "pthread_timedjoin_np",
        "pthread_clockjoin_np",
    }
)
FLAGGED_SYMBOLS = WALL_CLOCK_SYMBOLS | SLEEP_AND_WAIT_SYMBOLS

# Demangled forms (nm -C). libstdc++ puts its ABI-stable clocks in an inline namespace (_V2), and
# libc++ in std::__1, so both are accepted.
FLAGGED_PATTERNS = (
    re.compile(r"^std::(?:__\d+::)?chrono::(?:_V2::)?\w+_clock::now\(\)$"),
    re.compile(r"^std::__atomic_futex_unsigned_base::_M_futex_wait_until\w*\("),
)

# ADR-0023's two sites, by source file and by the symbols each one references. The HAL's
# clock_nanosleep is its bounded attach poll inside the same deadline function
# (embedded/hal/shm/hal_shm_wait.cpp), capped at the absolute deadline.
ALLOWLIST: dict[str, frozenset[str]] = {
    "plugins/sim/transport/shm/shm_wait.cpp": frozenset({"clock_gettime"}),
    "embedded/hal/shm/hal_shm_wait.cpp": frozenset({"clock_gettime", "clock_nanosleep"}),
}

_NM_LINE = re.compile(r"^\s*[Uw] (.+)$")


def flagged(symbol: str) -> bool:
    return symbol in FLAGGED_SYMBOLS or any(p.match(symbol) for p in FLAGGED_PATTERNS)


def undefined_symbols(obj: Path) -> set[str]:
    """Every undefined (or weak undefined) symbol of `obj`, demangled."""
    result = subprocess.run(
        ["nm", "-u", "-C", str(obj)], capture_output=True, text=True, check=False
    )
    if result.returncode != 0:
        raise RuntimeError(f"nm -u -C {obj} exited {result.returncode}: {result.stderr.strip()}")
    symbols: set[str] = set()
    for line in result.stdout.splitlines():
        match = _NM_LINE.match(line)
        if match:
            symbols.add(match.group(1).strip())
    return symbols


def _resolve(path: str, directory: str) -> Path:
    p = Path(path)
    return p if p.is_absolute() else Path(directory) / p


def scan(root: Path, build_dir: Path) -> tuple[list[str], int]:
    """Returns (problems, number of in-scope translation units scanned)."""
    database = build_dir / "compile_commands.json"
    try:
        entries = json.loads(database.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        return [f"{database}: cannot read the compile database: {error}"], 0

    root = root.resolve()
    problems: list[str] = []
    hits: dict[str, set[str]] = {}
    scanned = 0
    for entry in entries:
        source = _resolve(entry["file"], entry["directory"]).resolve()
        try:
            relative = source.relative_to(root).as_posix()
        except ValueError:
            continue
        if relative.split("/", 1)[0] not in SCOPE_ROOTS:
            continue
        output = entry.get("output")
        if output is None:
            problems.append(f"{relative}: the compile database entry has no output object")
            continue
        obj = _resolve(output, entry["directory"])
        if not obj.is_file():
            problems.append(
                f"{relative}: object {obj} does not exist (build the preset before scanning)"
            )
            continue
        scanned += 1
        found = {s for s in undefined_symbols(obj) if flagged(s)}
        hits.setdefault(relative, set()).update(found)

    for relative in sorted(hits):
        allowed = ALLOWLIST.get(relative, frozenset())
        problems.extend(f"{relative}: {s}" for s in sorted(hits[relative] - allowed))
    for relative, allowed in sorted(ALLOWLIST.items()):
        if relative not in hits:
            problems.append(f"{relative}: allowlisted, but no object of it was scanned")
            continue
        problems.extend(
            f"{relative}: stale allowlist entry, no longer references {s}"
            for s in sorted(allowed - hits[relative])
        )
    if scanned == 0:
        problems.append(f"{database}: no in-scope translation unit ({', '.join(SCOPE_ROOTS)})")
    return problems, scanned


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args(argv)
    problems, scanned = scan(args.root, args.build_dir)
    if problems:
        for problem in problems:
            print(f"wallclock-scan: {problem}", file=sys.stderr)
        return 1
    allowed = ", ".join(sorted(ALLOWLIST))
    print(f"wallclock-scan: OK ({scanned} TUs scanned; allowlisted: {allowed})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
