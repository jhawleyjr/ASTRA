"""scripts/wallclock_scan.py seen to fire, permanently (M5 PR 10; architecture §15, ADR-0010/0023).

Each case builds a throwaway repository root in tmp_path, compiles tiny translation units with
the gate's compiler, writes the compile database CMake would write, and runs the scanner as a
subprocess.
Nothing here reads the real build tree: `just wallclock-scan` is what scans that.

The parametrized symbol cases double as the toolchain check: each one compiles a real call and
expects the symbol nm shows for it on this toolchain (clang++-18, libstdc++), so a name that
stops appearing fails here instead of silently leaving a hole in the scan.
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

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
SYSTEM = "std::chrono::_V2::system_clock::now()"
NOW = ".time_since_epoch().count();"
MS = "std::chrono::milliseconds(1)"

# (id, source, a symbol nm shows for it)
SYMBOL_CASES = [
    ("steady_clock_now", _fn("chrono", STEADY_NOW), STEADY),
    (
        "system_clock_now",
        _fn("chrono", "return std::chrono::system_clock::now()" + NOW),
        SYSTEM,
    ),
    (
        "high_resolution_clock_now",
        _fn("chrono", "return std::chrono::high_resolution_clock::now()" + NOW),
        SYSTEM,  # an alias of system_clock in libstdc++
    ),
    ("clock_gettime", CLOCK_GETTIME, "clock_gettime"),
    (
        "gettimeofday",
        _fn("sys/time.h", "timeval v; gettimeofday(&v, nullptr); return v.tv_sec;"),
        "gettimeofday",
    ),
    ("time", _fn("ctime", "return time(nullptr);"), "time"),
    ("clock", _fn("ctime", "return clock();"), "clock"),
    (
        "timespec_get",
        _fn("ctime", "timespec t; timespec_get(&t, TIME_UTC); return t.tv_sec;"),
        "timespec_get",
    ),
    ("ftime", _fn("sys/timeb.h", "timeb t; ftime(&t); return t.time;"), "ftime"),
    (
        "this_thread_sleep_for",
        _fn("chrono thread", f"std::this_thread::sleep_for({MS}); return 0;"),
        "nanosleep",
    ),
    (
        "condition_variable_wait_for",
        _fn(
            "chrono condition_variable mutex",
            f"c.wait_for(l, {MS}); return 0;",
            "std::condition_variable& c, std::unique_lock<std::mutex>& l",
        ),
        "pthread_cond_clockwait",
    ),
    (
        "future_wait_for",
        _fn("chrono future", f"x.wait_for({MS}); return 0;", "std::future<int>& x"),
        "std::__atomic_futex_unsigned_base::_M_futex_wait_until_steady(",
    ),
    (
        "nanosleep",
        _fn("ctime", "timespec t{}; nanosleep(&t, nullptr); return 0;"),
        "nanosleep",
    ),
    (
        "clock_nanosleep",
        _fn("ctime", "timespec t{}; clock_nanosleep(CLOCK_MONOTONIC, 0, &t, nullptr); return 0;"),
        "clock_nanosleep",
    ),
    ("usleep", _fn("unistd.h", "usleep(1); return 0;"), "usleep"),
    ("sleep", _fn("unistd.h", "sleep(1); return 0;"), "sleep"),
    (
        "pthread_cond_timedwait",
        _fn(
            "pthread.h",
            "return pthread_cond_timedwait(c, m, t);",
            "pthread_cond_t* c, pthread_mutex_t* m, const timespec* t",
        ),
        "pthread_cond_timedwait",
    ),
    (
        "sem_timedwait",
        _fn("semaphore.h", "return sem_timedwait(s, t);", "sem_t* s, const timespec* t"),
        "sem_timedwait",
    ),
    (
        "sem_clockwait",
        _fn(
            "ctime semaphore.h",
            "return sem_clockwait(s, CLOCK_MONOTONIC, t);",
            "sem_t* s, const timespec* t",
        ),
        "sem_clockwait",
    ),
    (
        "pthread_mutex_timedlock",
        _fn(
            "pthread.h",
            "return pthread_mutex_timedlock(m, t);",
            "pthread_mutex_t* m, const timespec* t",
        ),
        "pthread_mutex_timedlock",
    ),
    (
        "thrd_sleep",
        _fn("threads.h ctime", "return thrd_sleep(t, nullptr);", "const timespec* t"),
        "thrd_sleep",
    ),
    (
        "cnd_timedwait",
        _fn(
            "threads.h ctime",
            "return cnd_timedwait(c, m, t);",
            "cnd_t* c, mtx_t* m, const timespec* t",
        ),
        "cnd_timedwait",
    ),
    (
        "mtx_timedlock",
        _fn("threads.h ctime", "return mtx_timedlock(m, t);", "mtx_t* m, const timespec* t"),
        "mtx_timedlock",
    ),
    (
        "sigtimedwait",
        _fn(
            "csignal ctime",
            "return sigtimedwait(s, nullptr, t);",
            "const sigset_t* s, const timespec* t",
        ),
        "sigtimedwait",
    ),
    (
        "semtimedop",
        _fn(
            "sys/sem.h ctime",
            "return semtimedop(id, ops, 1, t);",
            "int id, sembuf* ops, const timespec* t",
        ),
        "semtimedop",
    ),
    (
        "mq_timedsend",
        _fn(
            "mqueue.h ctime",
            "return mq_timedsend(q, m, 1, 0, t);",
            "mqd_t q, const char* m, const timespec* t",
        ),
        "mq_timedsend",
    ),
    (
        "mq_timedreceive",
        _fn(
            "mqueue.h ctime",
            "return mq_timedreceive(q, m, 1, nullptr, t);",
            "mqd_t q, char* m, const timespec* t",
        ),
        "mq_timedreceive",
    ),
    ("alarm", _fn("unistd.h", "return alarm(1);"), "alarm"),
    (
        "setitimer",
        _fn("sys/time.h", "itimerval v{}; return setitimer(ITIMER_REAL, &v, nullptr);"),
        "setitimer",
    ),
    (
        "timer_settime",
        _fn(
            "ctime",
            "return timer_settime(id, 0, spec, nullptr);",
            "timer_t id, const itimerspec* spec",
        ),
        "timer_settime",
    ),
    (
        "timerfd_settime",
        _fn(
            "sys/timerfd.h",
            "return timerfd_settime(fd, 0, spec, nullptr);",
            "int fd, const itimerspec* spec",
        ),
        "timerfd_settime",
    ),
    (
        "pthread_timedjoin_np",
        _fn(
            "pthread.h",
            "return pthread_timedjoin_np(p, nullptr, t);",
            "pthread_t p, const timespec* t",
        ),
        "pthread_timedjoin_np",
    ),
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
    ("source", "symbol"), [c[1:] for c in SYMBOL_CASES], ids=[c[0] for c in SYMBOL_CASES]
)
def test_a_wall_clock_or_sleep_reference_fails_naming_the_file_and_symbol(
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
