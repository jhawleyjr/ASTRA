"""scripts/wallclock_scan.py seen to fire, permanently (M5 PR 10; architecture §15, ADR-0010/0023).

Three layers. (1) One compiled case per pattern family: a tiny translation unit is compiled with the
gate's compiler into a throwaway repository root in tmp_path, next to the compile database CMake
would write, and the scanner runs on it as a subprocess, so a family that stops matching on this
toolchain (clang++-18, libstdc++) fails here. (2) The scan's own semantics on such trees: scope,
allowlist by file and symbol, stale entries, missing objects. (3) `flagged()` on strings: every name
the reviews found, and the lookalikes that must not match. Nothing here reads the real build tree:
`just wallclock-scan` is what scans that.
"""

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
from pathlib import Path
from types import ModuleType

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
SCANNER = REPO_ROOT / "scripts" / "wallclock_scan.py"

SHM_SITE = "plugins/sim/transport/shm/shm_wait.cpp"
HAL_SITE = "embedded/hal/shm/hal_shm_wait.cpp"
BAD_PATH = "plugins/physical/thing/thing.cpp"


def _fn(includes: str, body: str, params: str = "") -> str:
    """One translation unit: the headers, then `auto f(params) { body }`."""
    headers = "".join(f"#include <{name}>\n" for name in includes.split())
    return f"{headers}auto f({params}) {{ {body} }}\n"


CLOCK_GETTIME = _fn("ctime", "timespec t{}; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec;")
HAL_BODY = _fn(
    "ctime",
    "timespec t{}; clock_gettime(CLOCK_MONOTONIC, &t);"
    " clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, nullptr); return t.tv_sec;",
)
CLEAN = _fn("cstdlib", "return std::strtod(s, nullptr);", "const char* s")  # strtod is no clock
STEADY_NOW = "return std::chrono::steady_clock::now().time_since_epoch().count();"

STEADY = "std::chrono::_V2::steady_clock::now()"
MS = "std::chrono::milliseconds(1)"


def _load_scanner() -> ModuleType:
    spec = importlib.util.spec_from_file_location("wallclock_scan", SCANNER)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


scanner = _load_scanner()

# One compiled case per family: (family, source, a symbol nm shows for it).
FAMILY_CASES = [
    ("clock_*", CLOCK_GETTIME, "clock_gettime"),
    ("time reads", _fn("ctime", "return time(nullptr);"), "time"),
    (
        "*timeofday",
        _fn("sys/time.h", "timeval v; gettimeofday(&v, nullptr); return v.tv_sec;"),
        "gettimeofday",
    ),
    (
        # sleep_for has no out-of-line symbol of its own here: it ends in nanosleep
        "*sleep",
        _fn("chrono thread", f"std::this_thread::sleep_for({MS}); return 0;"),
        "nanosleep",
    ),
    ("timers", _fn("unistd.h", "return ualarm(1000, 1000);"), "ualarm"),
    (
        # condition_variable::wait_for ends in pthread_cond_clockwait
        "timed waits",
        _fn(
            "chrono condition_variable mutex",
            f"c.wait_for(l, {MS}); return 0;",
            "std::condition_variable& c, std::unique_lock<std::mutex>& l",
        ),
        "pthread_cond_clockwait",
    ),
    (
        "multiplexing",
        _fn("poll.h", "return poll(fds, 1, 10);", "pollfd* fds"),
        "poll",
    ),
    ("std::chrono now", _fn("chrono", STEADY_NOW), STEADY),
    (
        "std timed member",
        _fn("chrono future", f"x.wait_for({MS}); return 0;", "std::future<int>& x"),
        "std::__atomic_futex_unsigned_base::_M_futex_wait_until_steady(",
    ),
]

# Every name the reviews raised, plus the family members Joe listed, as the scanner sees them.
MUST_MATCH = [
    "clock",
    "clock_gettime",
    "clock_settime",
    "clock_nanosleep",
    "time",
    "timespec_get",
    "gettimeofday",
    "settimeofday",
    "ftime",
    "sleep",
    "usleep",
    "nanosleep",
    "thrd_sleep",
    "alarm",
    "ualarm",
    "setitimer",
    "getitimer",
    "timer_create",
    "timer_settime",
    "timerfd_create",
    "timerfd_settime",
    "pthread_cond_timedwait",
    "pthread_cond_clockwait",
    "sem_timedwait",
    "sem_clockwait",
    "cnd_timedwait",
    "sigtimedwait",
    "pthread_mutex_timedlock",
    "pthread_mutex_clocklock",
    "mtx_timedlock",
    "pthread_rwlock_timedrdlock",
    "pthread_rwlock_clockwrlock",
    "pthread_timedjoin_np",
    "pthread_clockjoin_np",
    "semtimedop",
    "mq_timedsend",
    "mq_timedreceive",
    "select",
    "pselect",
    "poll",
    "ppoll",
    "epoll_wait",
    "epoll_pwait",
    "std::chrono::_V2::steady_clock::now()",
    "std::chrono::_V2::system_clock::now()",
    "std::chrono::__1::steady_clock::now()",
    "std::this_thread::sleep_for<long, std::ratio<1l, 1000l> >(std::chrono::duration<long, "
    "std::ratio<1l, 1000l> > const&)",
    "std::this_thread::__sleep_for(std::chrono::duration<long, std::ratio<1l, 1l> >, "
    "std::chrono::duration<long, std::ratio<1l, 1000000000l> >)",
    "std::cv_status std::condition_variable::wait_for<long, std::ratio<1l, 1000l> >"
    "(std::unique_lock<std::mutex>&, std::chrono::duration<long, std::ratio<1l, 1000l> > const&)",
    "std::future<int>::wait_for<long, std::ratio<1l, 1000l> >(std::chrono::duration<long, "
    "std::ratio<1l, 1000l> > const&) const",
    "bool std::timed_mutex::try_lock_for<long, std::ratio<1l, 1000l> >(std::chrono::duration<long, "
    "std::ratio<1l, 1000l> > const&)",
    "std::__atomic_futex_unsigned_base::_M_futex_wait_until_steady(unsigned int*, unsigned int, "
    "bool, std::chrono::duration<long, std::ratio<1l, 1l> >, std::chrono::duration<long, "
    "std::ratio<1l, 1000000000l> >)",
]

# Lookalikes that must not match: our own namespaces, other libraries, and near-miss C names.
MUST_NOT_MATCH = [
    "strtod",
    "H5Pset_obj_track_times",
    "H5Pget_obj_track_times",
    "memset",
    "mktime",
    "localtime_r",
    "strftime",
    "difftime",
    "timeout",
    "timeline",
    "runtime",
    "poller",
    "pollution",
    "selection",
    "select_all",
    "sleeper",
    "astra_hal_shm_wait_until",
    "astra_hal_wait_frame",
    "astra_plugin",
    "__cxa_begin_catch",
    "astra::time_step(double)",
    "astra::clock_skew() const",
    "astra::VirtualClock::now() const",
    "astra::sleep_ms(int)",
    "astra::poll(int)",
    "astra::Timer::timer_settime()",
    "std::runtime_error::what() const",
    "std::chrono::duration<long, std::ratio<1l, 1l> >::count() const",
    "std::vector<astra::wait_for_frame, std::allocator<astra::wait_for_frame> >::size() const",
    "std::function<void (astra::wait_until_done)>::operator()() const",
    "std::__throw_system_error(int)",
    "typeinfo for std::runtime_error",
]


class Tree:
    """A throwaway root with a build directory and a compile database, filled file by file."""

    def __init__(self, tmp_path: Path) -> None:
        self.root = tmp_path / "root"
        self.build = self.root / "build"
        self.build.mkdir(parents=True)
        self.entries: list[dict[str, str]] = []

    def add(self, relative: str, source: str, *, compile_object: bool = True) -> None:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        output = f"obj{len(self.entries)}.o"
        if compile_object:
            subprocess.run(
                ["clang++-18", "-std=c++23", "-w", "-c", str(path), "-o", str(self.build / output)],
                check=True,
                capture_output=True,
                text=True,
            )
        self.entries.append(
            {
                "directory": str(self.build),
                "file": str(path),
                "output": output,
                "command": f"clang++-18 -c {path} -o {output}",
            }
        )

    def add_sites(self) -> None:
        self.add(SHM_SITE, CLOCK_GETTIME)
        self.add(HAL_SITE, HAL_BODY)

    def scan(self) -> subprocess.CompletedProcess[str]:
        (self.build / "compile_commands.json").write_text(
            json.dumps(self.entries), encoding="utf-8"
        )
        return subprocess.run(
            [sys.executable, str(SCANNER), "--root", str(self.root), str(self.build)],
            capture_output=True,
            text=True,
            check=False,
        )


def _failure_lines(result: subprocess.CompletedProcess[str]) -> list[str]:
    assert result.returncode == 1, (result.returncode, result.stdout, result.stderr)
    return result.stderr.splitlines()


@pytest.mark.parametrize(
    ("source", "symbol"), [c[1:] for c in FAMILY_CASES], ids=[c[0] for c in FAMILY_CASES]
)
def test_each_family_fails_naming_the_file_and_symbol(
    tmp_path: Path, source: str, symbol: str
) -> None:
    tree = Tree(tmp_path)
    tree.add_sites()
    tree.add(BAD_PATH, source)
    lines = _failure_lines(tree.scan())
    assert any(
        line.startswith(f"wallclock-scan: {BAD_PATH}: ") and symbol in line for line in lines
    ), lines
    assert all(BAD_PATH in line for line in lines)  # only the planted file is reported


@pytest.mark.parametrize("scope_root", ["core/src", "aircraft/x/physical/y", "embedded/z"])
def test_every_scope_root_is_scanned(tmp_path: Path, scope_root: str) -> None:
    tree = Tree(tmp_path)
    tree.add_sites()
    tree.add(f"{scope_root}/bad.cpp", CLOCK_GETTIME)
    lines = _failure_lines(tree.scan())
    assert lines == [f"wallclock-scan: {scope_root}/bad.cpp: clock_gettime"]


def test_the_tree_with_only_the_two_adr_0023_sites_passes(tmp_path: Path) -> None:
    tree = Tree(tmp_path)
    tree.add_sites()
    tree.add(BAD_PATH, CLEAN)  # strtod and friends are not clocks
    result = tree.scan()
    assert result.returncode == 0, result.stderr
    assert result.stdout.startswith("wallclock-scan: OK (3 TUs scanned")


def test_the_allowlist_is_by_source_file(tmp_path: Path) -> None:
    tree = Tree(tmp_path)
    tree.add_sites()  # the same clock_gettime code passes at the allowlisted path ...
    assert tree.scan().returncode == 0
    tree.add(BAD_PATH, CLOCK_GETTIME)  # ... and fails at any other
    assert _failure_lines(tree.scan()) == [f"wallclock-scan: {BAD_PATH}: clock_gettime"]


def test_an_allowlisted_file_may_use_only_its_own_symbols(tmp_path: Path) -> None:
    tree = Tree(tmp_path)
    tree.add(
        SHM_SITE,
        CLOCK_GETTIME + _fn("chrono", STEADY_NOW).replace("f(", "g("),
    )
    tree.add(HAL_SITE, HAL_BODY)
    assert _failure_lines(tree.scan()) == [f"wallclock-scan: {SHM_SITE}: {STEADY}"]


def test_a_stale_allowlist_entry_fails(tmp_path: Path) -> None:
    tree = Tree(tmp_path)
    tree.add(SHM_SITE, CLEAN)
    tree.add(HAL_SITE, HAL_BODY)
    assert _failure_lines(tree.scan()) == [
        f"wallclock-scan: {SHM_SITE}: stale allowlist entry, no longer references clock_gettime"
    ]


def test_an_allowlisted_file_that_was_not_scanned_fails(tmp_path: Path) -> None:
    tree = Tree(tmp_path)
    tree.add(SHM_SITE, CLOCK_GETTIME)
    tree.add(BAD_PATH, CLEAN)
    assert _failure_lines(tree.scan()) == [
        f"wallclock-scan: {HAL_SITE}: allowlisted, but no object of it was scanned"
    ]


def test_a_database_with_no_in_scope_translation_unit_fails(tmp_path: Path) -> None:
    tree = Tree(tmp_path)
    tree.add("tests/unit/timing_test.cpp", CLOCK_GETTIME)  # out of scope: never scanned
    lines = _failure_lines(tree.scan())
    assert any("no in-scope translation unit" in line for line in lines), lines


def test_a_missing_object_fails(tmp_path: Path) -> None:
    tree = Tree(tmp_path)
    tree.add_sites()
    tree.add(BAD_PATH, CLEAN, compile_object=False)
    lines = _failure_lines(tree.scan())
    assert len(lines) == 1
    assert lines[0].startswith(f"wallclock-scan: {BAD_PATH}: object ")
    assert "does not exist" in lines[0]


def test_an_unreadable_database_fails(tmp_path: Path) -> None:
    tree = Tree(tmp_path)
    result = subprocess.run(
        [sys.executable, str(SCANNER), "--root", str(tree.root), str(tree.build)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert "cannot read the compile database" in "".join(_failure_lines(result))


def test_a_source_outside_the_scope_roots_is_not_scanned(tmp_path: Path) -> None:
    tree = Tree(tmp_path)
    tree.add_sites()
    tree.add("tests/unit/timing_test.cpp", CLOCK_GETTIME)
    tree.add("python/bindings/clocks.cpp", CLOCK_GETTIME)
    result = tree.scan()
    assert result.returncode == 0, result.stderr
    assert "(2 TUs scanned" in result.stdout


@pytest.mark.parametrize("symbol", MUST_MATCH)
def test_flagged_matches_every_name_the_reviews_raised(symbol: str) -> None:
    assert scanner.flagged(symbol) is not None, symbol


@pytest.mark.parametrize("symbol", MUST_NOT_MATCH)
def test_flagged_ignores_lookalikes(symbol: str) -> None:
    assert scanner.flagged(symbol) is None, (symbol, scanner.flagged(symbol))


def test_every_family_has_a_compiled_case_or_a_reason() -> None:
    compiled = {family for family, _, _ in FAMILY_CASES}
    # std::this_thread has no out-of-line symbol on this toolchain: sleep_for compiles to nanosleep
    # (the "*sleep" case), and MUST_MATCH holds the demangled forms other toolchains may show.
    assert compiled | {"std::this_thread"} == set(scanner.C_FAMILIES) | set(scanner.CXX_FAMILIES)
