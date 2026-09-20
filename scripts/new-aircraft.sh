#!/usr/bin/env bash
# Copies aircraft/_template to aircraft/<name> (docs/architecture.md §18, PLAN.md M3 PR 1):
# cmake/AstraPlugin.cmake derives every plugin's identity from its own directory
# (cmake/AstraPlugin.cmake's astra_add_plugin), so the copy alone re-identifies each plugin with
# no source edits -- only the config files' literal "aircraft/_template" plugin-path strings need
# rewriting to "aircraft/<name>". Repo root is this script's own parent directory (matches
# scripts/check-fast.sh's convention), not `git rev-parse`, so this works unmodified from any
# worktree: it always writes into the checkout that contains it, never the main checkout.
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: ${0##*/} <name>  (lowercase letters, digits, underscore only)" >&2
  exit 2
fi
name="$1"

if [[ ! "$name" =~ ^[a-z0-9_]+$ ]]; then
  echo "new-aircraft: '$name' must match [a-z0-9_]+" >&2
  exit 2
fi
if [[ "$name" == "_template" ]]; then
  echo "new-aircraft: '_template' is the template itself, not a new aircraft name" >&2
  exit 2
fi

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd -- "$repo_root"

dest="aircraft/$name"
# mkdir is the atomic exclusive-create primitive (a single syscall): claims $dest before writing
# anything into it, so two concurrent invocations for the same name can't both pass a separate
# "does it exist" check and then have the second `cp -r` nest _template's own tree inside the
# first invocation's directory (a bare `cp -r template dest` copies AS a subdirectory when dest
# already exists, which astra_discover_plugins would then walk into) -- Codex adversarial review,
# PLAN.md M3 PR 1. It also rejects an existing file or symlink at $dest, not just a directory.
if ! mkdir -- "$dest" 2>/dev/null; then
  echo "new-aircraft: $dest already exists" >&2
  exit 1
fi
# A failure partway (disk full, permission error) must not leave a half-populated aircraft
# directory that blocks every future retry and that a build could still discover.
trap 'rm -rf -- "$dest"' ERR

cp -r aircraft/_template/. "$dest/"
find "$dest/configs" -type f -name '*.yaml' -exec \
  sed -i "s#aircraft/_template#$dest#g" {} +

trap - ERR
echo "Created $dest from aircraft/_template. Its physical/mass config is still zeroed/TODO --" \
     "edit $dest/configs/*.yaml before running it (it will refuse to run un-edited)."
