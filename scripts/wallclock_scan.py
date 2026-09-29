#!/usr/bin/env python3
"""Wall-clock symbol scan (M5 PR 10; docs/architecture.md §15, ADR-0010, ADR-0023).

The rule: in core/, plugins/ and aircraft/ (and embedded/, which ADR-0023's second site lives in) no
translation unit reads the wall clock, sleeps, or waits on a timeout, except the two ADR-0023
deadline sites. This makes the rule a check on compiled code instead of prose.

For every compile-database entry whose source is under core/, plugins/, aircraft/ or embedded/, it
runs `nm -u -C` on the entry's object and flags any undefined reference that belongs to one of these
families (the patterns, each commented with what it covers, are C_FAMILIES and CXX_FAMILIES below;
every one is anchored, so a lookalike such as strtod, H5Pset_obj_track_times or a time-named
function in our own namespaces does not match):

- C and POSIX, matched against the unmangled symbol: clock reads (clock_*, time, clock,
  timespec_get, *timeofday, ftime), sleeps (*sleep), timer arming (alarm, ualarm, *itimer,
  timer_*, timerfd_*), timed waits and locks (*timedwait, *timedlock, *clockwait, *clocklock, and
  the timed forms of join, semaphore operations and message queues), and I/O multiplexing with a
  timeout (select, pselect, poll, ppoll, epoll_wait, epoll_pwait);
- libstdc++, matched against the demangled name: anything in std::chrono:: ending in ::now(),
  anything in std::this_thread::, and wait_for / wait_until / try_lock_for / try_lock_until (and
  their _M_futex_ form) on any std:: type.

Which symbol a std:: call shows depends on the toolchain (clang-18 with libstdc++ here):
sleep_for is header code that ends in nanosleep, condition_variable::wait_for ends in
pthread_cond_clockwait and steady_clock::now(), future::wait_for calls
std::__atomic_futex_unsigned_base::_M_futex_wait_until_steady. The C families catch those, and the
libstdc++ families catch the out-of-line members. tests/python/test_wallclock_scan.py compiles one
call per family, so a family that stops matching on this toolchain fails there.

The allowlist is by source file and by symbol: each ADR-0023 site may reference only what it
references today. An allowlisted symbol that its file no longer references is a stale entry and
fails, and so does a scan that finds no in-scope translation unit, an object that is missing (the
recipe builds first), or an unreadable compile database; none of them may pass silently.

This is a tripwire for the ordinary ways of reading a clock or waiting on real time, not a proof.
What it cannot see (it reads the undefined references of compiled objects):
- a raw syscall(), e.g. syscall(SYS_clock_gettime, ...) or syscall(SYS_futex, ...) with a timeout
  (syscall itself is not flagged: it is too broad, and both ADR-0023 sites use it for the futex);
- inline assembly and intrinsics that read a counter, such as rdtsc / __rdtsc;
- vDSO or function-pointer indirection: a symbol resolved at run time (dlsym) or called through a
  pointer, so no undefined reference names it;
- calls made from third-party libraries outside the scanned objects (a clock or wait reached through
  a library another object links);
- a source no target builds, and any translation unit outside core/, plugins/, aircraft/ and
  embedded/ (tests/ and the Python bindings are out of ADR-0010's scope).
The ADR-0023 sites' use of the clock stays a review matter (one function each).

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

# C and POSIX families, matched with fullmatch against the unmangled symbol. A demangled C++ name
# contains "::" or "(" and can never fullmatch, so a lookalike in our own namespaces is safe.
C_FAMILIES: dict[str, re.Pattern[str]] = {
    # clock_gettime, clock_settime, clock_getres, clock_nanosleep, and clock_getcpuclockid.
    "clock_*": re.compile(r"clock_\w+"),
    # Reads of the calendar or process time: time(), clock(), timespec_get(), ftime().
    "time reads": re.compile(r"time|clock|timespec_get|ftime"),
    # gettimeofday and settimeofday.
    "*timeofday": re.compile(r"\w*timeofday"),
    # sleep, usleep, nanosleep, clock_nanosleep, thrd_sleep.
    "*sleep": re.compile(r"\w*sleep"),
    # Timer arming: alarm, ualarm, setitimer, getitimer, timer_create/settime/gettime/delete, and
    # timerfd_create/settime/gettime.
    "timers": re.compile(r"alarm|ualarm|\w*itimer|timer_\w+|timerfd_\w+"),
    # Waits and locks with a deadline: pthread_cond_timedwait, sem_timedwait, cnd_timedwait,
    # sigtimedwait, pthread_mutex_timedlock, mtx_timedlock, pthread_rwlock_timed{rd,wr}lock, the
    # glibc clock variants (pthread_cond_clockwait, sem_clockwait, pthread_mutex_clocklock,
    # pthread_rwlock_clock{rd,wr}lock), pthread_timedjoin_np/pthread_clockjoin_np, semtimedop and
    # mq_timedsend/mq_timedreceive.
    "timed waits": re.compile(
        r"\w*(?:timed|clock)(?:wait|lock|rdlock|wrlock|join_np|op|send|receive)"
    ),
    # I/O multiplexing with a timeout.
    "multiplexing": re.compile(r"select|pselect|poll|ppoll|epoll_wait|epoll_pwait"),
}

# libstdc++ families, matched against the demangled name. libstdc++ puts its ABI-stable clocks in an
# inline namespace (_V2) and libc++ in std::__1, so the optional inline namespaces are accepted. A
# templated member is demangled with its return type in front, as in
# `bool std::timed_mutex::try_lock_for<...>`, so the name may follow one; it must still be std::.
_STD = r"(?:[^()]*? )?std::(?:__\d+::)?"
CXX_FAMILIES: dict[str, re.Pattern[str]] = {
    # steady_clock::now(), system_clock::now() (also high_resolution_clock, an alias in libstdc++),
    # and any other std::chrono clock's now().
    "std::chrono now": re.compile(_STD + r"chrono::.*::now\(\)$"),
    # std::this_thread::sleep_for and sleep_until, and anything else declared there.
    "std::this_thread": re.compile(_STD + r"this_thread::.*"),
    # wait_for, wait_until, try_lock_for, try_lock_until (condition_variable, future, shared_future,
    # the timed mutexes) and libstdc++'s out-of-line futex form _M_futex_wait_until[_steady].
    "std timed member": re.compile(
        _STD + r"[^()]*::\w*(?:wait_for|wait_until|try_lock_for|try_lock_until)\w*(?:<.*>)?\("
    ),
}

# ADR-0023's two sites, by source file and by the symbols each one references. The HAL's
# clock_nanosleep is its bounded attach poll inside the same deadline function
# (embedded/hal/shm/hal_shm_wait.cpp), capped at the absolute deadline.
ALLOWLIST: dict[str, frozenset[str]] = {
    "plugins/sim/transport/shm/shm_wait.cpp": frozenset({"clock_gettime"}),
    "embedded/hal/shm/hal_shm_wait.cpp": frozenset({"clock_gettime", "clock_nanosleep"}),
}

_NM_LINE = re.compile(r"^\s*[Uw] (.+)$")


def flagged(symbol: str) -> str | None:
    """The name of the family `symbol` belongs to, or None."""
    for name, pattern in C_FAMILIES.items():
        if pattern.fullmatch(symbol):
            return name
    for name, pattern in CXX_FAMILIES.items():
        if pattern.match(symbol):
            return name
    return None


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
        found = {s for s in undefined_symbols(obj) if flagged(s) is not None}
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
