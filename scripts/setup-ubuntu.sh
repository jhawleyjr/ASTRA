#!/usr/bin/env bash
# ASTRA developer setup for Ubuntu 24.04. Idempotent. Run from the repo root: ./scripts/setup-ubuntu.sh
# --ci: apt-get install only, then exit -- this is the one package list this script and
# .github/workflows/check.yml both use (docs/lessons.md: they used to be two copies that drifted).
set -euo pipefail

install_apt_packages() {
  echo "== apt: toolchain and libraries (all from the 24.04 repos) =="
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends \
    build-essential clang-18 clangd-18 clang-tidy-18 clang-format-18 lld-18 \
    cmake ninja-build just git gh \
    libeigen3-dev libhdf5-dev libgtest-dev libgmock-dev libyaml-cpp-dev libboost-dev \
    python3 python3-venv python3-dev python3-pip
}

if [ "${1:-}" = "--ci" ]; then
  install_apt_packages
  exit 0
fi

install_apt_packages

echo "== clang-18 as the default clang/clangd names (the LSP plugin looks for plain 'clangd') =="
sudo update-alternatives --install /usr/bin/clang        clang        /usr/bin/clang-18        100
sudo update-alternatives --install /usr/bin/clang++      clang++      /usr/bin/clang++-18      100
sudo update-alternatives --install /usr/bin/clangd       clangd       /usr/bin/clangd-18       100
sudo update-alternatives --install /usr/bin/clang-tidy   clang-tidy   /usr/bin/clang-tidy-18   100
sudo update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-18 100

echo "== python venv (just venv: creates .venv, installs the pinned dev group from pyproject.toml) =="
# A sourced ROS setup.bash puts /opt/ros/.../site-packages on PYTHONPATH, which leaks into the venv
# and makes pip warn about ROS packages. ASTRA never wants that path; the justfile clears it too.
unset PYTHONPATH
just venv
# Analysis libraries for later milestones (trim, linearize, log replay); unpinned until a gate uses them.
.venv/bin/pip install scipy h5py pandas pyarrow pyyaml

echo "== versions =="
clang --version | head -1; cmake --version | head -1; just --version; .venv/bin/python --version
echo "nanobind $(.venv/bin/python -c 'import nanobind; print(nanobind.__version__)')"

cat <<'MSG'

Done. Remaining one-time steps (need your login, not scriptable here):
  gh auth login
  (main is already protected by .github/rulesets/main.json; change it with
   gh api -X PUT repos/jhawleyjr/ASTRA/rulesets/<id> --input .github/rulesets/main.json)
  claude plugin install clangd-lsp
  claude plugin install pyright-lsp
Optional, for real-time mode later (free for personal use, up to 5 machines):
  sudo pro attach && sudo pro enable realtime-kernel
MSG
