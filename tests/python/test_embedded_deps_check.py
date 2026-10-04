"""`scripts/embedded_deps_check.py`, `just embedded-standalone`'s dependency check (ADR-0008).

The check failed on a checkout whose path contained "yaml" (M6 PR 11's worktree
`m6-pr11-yaml-route`), because it grepped the whole CMakeCache, the build and source paths
included. It now reads dependency entries and compile/link lines only, with the checkout root
removed. Each case runs the script as the recipe does.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "scripts" / "embedded_deps_check.py"
CXX = "clang++-18"


def _check(build_dir: Path, root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), str(build_dir), "--root", str(root)],
        capture_output=True,
        text=True,
        check=False,
    )


def _cache(build_dir: Path, root: Path, *entries: str) -> None:
    build_dir.mkdir(parents=True)
    lines = [
        f"# For build in directory: {build_dir}",
        "//Path to a program.",
        "CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/clang++-18",
        f"astra_embedded_BINARY_DIR:STATIC={build_dir}",
        f"astra_embedded_SOURCE_DIR:STATIC={root}/embedded",
        f"CMAKE_FIND_PACKAGE_REDIRECTS_DIR:STATIC={build_dir}/CMakeFiles/pkgRedirects",
        f"CMAKE_HOME_DIRECTORY:INTERNAL={root}/embedded",
        f"CMAKE_INSTALL_PREFIX:PATH={root}/install",
        *entries,
    ]
    (build_dir / "CMakeCache.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def _link(build_dir: Path, line: str, name: str = "link.txt") -> None:
    directory = build_dir / "autopilot" / "CMakeFiles" / "astra_autopilot.dir"
    directory.mkdir(parents=True, exist_ok=True)
    (directory / name).write_text(line + "\n", encoding="utf-8")


def test_a_checkout_path_naming_every_word_passes(tmp_path: Path) -> None:
    root = tmp_path / "m6-pr11-yaml-route-python-gtest-eigen"
    build = root / "build" / "embedded-standalone" / "clang++-18"
    _cache(build, root)
    _link(build, f"/usr/bin/clang++-18 -O3 {build}/autopilot/main.o -o astra_autopilot")
    _link(build, f"CXX_INCLUDES = -I{root}/embedded/hal", "flags.make")
    result = _check(build, root)
    assert result.returncode == 0, result.stderr


@pytest.mark.parametrize(
    ("entry", "named"),
    [
        ("yaml-cpp_DIR:PATH=/usr/lib/x86_64-linux-gnu/cmake/yaml-cpp", "entry yaml-cpp_DIR"),
        ("Eigen3_DIR:PATH=/usr/share/eigen3/cmake", "entry Eigen3_DIR"),
        ("GTest_DIR:PATH=/usr/lib/x86_64-linux-gnu/cmake/GTest", "entry GTest_DIR"),
        ("Python3_EXECUTABLE:FILEPATH=/usr/bin/python3", "entry Python3_EXECUTABLE"),
        ("SOME_LIB:FILEPATH=/usr/lib/x86_64-linux-gnu/libyaml-cpp.so", "SOME_LIB = "),
        ("SOME_INCLUDE:PATH=/usr/include/eigen3", "SOME_INCLUDE = "),
    ],
)
def test_a_dependency_entry_fails_and_is_named(tmp_path: Path, entry: str, named: str) -> None:
    root = tmp_path / "checkout"
    build = root / "build"
    _cache(build, root, entry)
    result = _check(build, root)
    assert result.returncode == 1
    assert named in result.stderr


def test_a_dependency_vendored_inside_the_checkout_still_fails(tmp_path: Path) -> None:
    root = tmp_path / "checkout"
    build = root / "build"
    _cache(build, root, f"VENDORED:PATH={root}/third_party/yaml-cpp/include")
    assert _check(build, root).returncode == 1


@pytest.mark.parametrize(
    ("line", "name"),
    [
        ("/usr/bin/clang++-18 main.o -o astra_autopilot -lyaml-cpp", "link.txt"),
        ("CXX_INCLUDES = -isystem /usr/include/eigen3", "flags.make"),
    ],
)
def test_a_dependency_on_a_compile_or_link_line_fails(tmp_path: Path, line: str, name: str) -> None:
    root = tmp_path / "yaml-checkout"
    build = root / "build"
    _cache(build, root)
    _link(build, line, name)
    result = _check(build, root)
    assert result.returncode == 1
    assert name in result.stderr


def test_a_missing_cache_fails(tmp_path: Path) -> None:
    result = _check(tmp_path, tmp_path)
    assert result.returncode == 1
    assert "no CMakeCache.txt" in result.stderr


def _configure(root: Path, build: Path, extra: str = "") -> None:
    """A copy of embedded/ and what its standalone configure reads (cmake/, icd/) under ``root``,
    with ``extra`` appended to embedded/CMakeLists.txt, configured into ``build`` as the recipe
    does."""
    for part in ("embedded", "cmake", "icd"):
        shutil.copytree(REPO_ROOT / part, root / part)
    lists = root / "embedded" / "CMakeLists.txt"
    lists.write_text(lists.read_text(encoding="utf-8") + extra, encoding="utf-8")
    subprocess.run(
        ["cmake", "-S", str(root / "embedded"), "-B", str(build), f"-DCMAKE_CXX_COMPILER={CXX}"],
        check=True,
        capture_output=True,
    )


def test_a_real_configure_under_a_yaml_python_path_passes(tmp_path: Path) -> None:
    """The regression: embedded/ configured from a source and build dir whose path holds every
    word, as M6 PR 11's worktree did."""
    root = tmp_path / "m6-pr11-yaml-route-python-gtest-eigen"
    build = root / "build" / "embedded-standalone" / "clang++-18"
    _configure(root, build)
    result = _check(build, root)
    assert result.returncode == 0, result.stderr


def test_a_real_configure_that_finds_yaml_cpp_fails(tmp_path: Path) -> None:
    """The check still does its job: the same copy with a find_package(yaml-cpp) is refused."""
    root = tmp_path / "checkout"
    build = root / "build"
    _configure(root, build, "find_package(yaml-cpp REQUIRED)\n")
    result = _check(build, root)
    assert result.returncode == 1
    assert "yaml-cpp_DIR" in result.stderr


# --- follow-ups from #98's approval (Joe, 2026-10-04) ---------------------------------------------


def test_a_symlinked_checkout_whose_real_path_holds_a_word_passes(tmp_path: Path) -> None:
    """--root "$(pwd)" is the logical path; CMake records the physical one."""
    real = tmp_path / "real-yaml-python-checkout"
    real.mkdir()
    link = tmp_path / "checkout"
    link.symlink_to(real)
    build = real / "build"
    _cache(build, real)
    _link(build, f"/usr/bin/clang++-18 {build}/autopilot/main.o -o astra_autopilot")
    result = _check(link / "build", link)
    assert result.returncode == 0, result.stderr


def _configure_from(checkout: Path, extra: str = "") -> None:
    """As the recipe runs it: from inside ``checkout``, with relative paths, so CMake resolves them
    through getcwd() and records the physical path, while ``$(pwd)`` gives the logical one."""
    for part in ("embedded", "cmake", "icd"):
        shutil.copytree(REPO_ROOT / part, checkout / part)
    lists = checkout / "embedded" / "CMakeLists.txt"
    lists.write_text(lists.read_text(encoding="utf-8") + extra, encoding="utf-8")
    subprocess.run(
        ["cmake", "-S", "embedded", "-B", "build", f"-DCMAKE_CXX_COMPILER={CXX}"],
        cwd=checkout,
        check=True,
        capture_output=True,
    )


def test_a_real_configure_through_a_symlinked_checkout_passes(tmp_path: Path) -> None:
    real = tmp_path / "real-yaml-python-gtest-eigen"
    link = tmp_path / "checkout"
    real.mkdir()
    link.symlink_to(real)
    _configure_from(link)
    cache = (link / "build" / "CMakeCache.txt").read_text(encoding="utf-8")
    assert str(real) in cache  # CMake recorded the physical path
    result = _check(link / "build", link)
    assert result.returncode == 0, result.stderr


def test_a_dependency_through_a_symlinked_checkout_still_fails(tmp_path: Path) -> None:
    real = tmp_path / "real"
    link = tmp_path / "checkout"
    real.mkdir()
    link.symlink_to(real)
    _configure_from(link, "find_package(yaml-cpp REQUIRED)\n")
    result = _check(link / "build", link)
    assert result.returncode == 1
    assert "yaml-cpp_DIR" in result.stderr


def test_an_undecodable_byte_in_a_generated_file_does_not_stop_the_check(tmp_path: Path) -> None:
    root = tmp_path / "checkout"
    build = root / "build"
    _cache(build, root)
    with (build / "CMakeCache.txt").open("ab") as f:
        f.write(b"ODD:STRING=caf\xe9\n")
    directory = build / "autopilot" / "CMakeFiles" / "astra_autopilot.dir"
    directory.mkdir(parents=True)
    (directory / "link.txt").write_bytes(b"/usr/bin/clang++-18 \xff main.o -o a -lyaml-cpp\n")
    result = _check(build, root)
    assert result.returncode == 1
    assert "link.txt" in result.stderr
    assert "Traceback" not in result.stderr
