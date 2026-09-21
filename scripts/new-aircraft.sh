#!/usr/bin/env bash
# Copies aircraft/_template to aircraft/<name> (docs/architecture.md §18, PLAN.md M3 PR 1):
# cmake/AstraPlugin.cmake derives every plugin's identity from its own directory
# (cmake/AstraPlugin.cmake's astra_add_plugin), so the copy alone re-identifies each plugin with
# no source edits -- only the config files' literal "aircraft/_template" plugin-path strings need
# rewriting to "aircraft/<name>". Repo root is this script's own parent directory (matches
# scripts/check-fast.sh's convention), not `git rev-parse`, so this works unmodified from any
# worktree: it always writes into the checkout that contains it, never the main checkout.
#
# docs/plans/pr4-notional-aero-turbofan.md (Codex adversarial-review finding, Joe's own re-review):
# _math.hpp's own Inputs/Coefficients/compute_wrench() live in an aircraft-specific C++ namespace
# (astra::aircraft_template_{aero,turbofan}_math), not a generic one -- ASTRA_STATIC_PLUGINS links
# every plugin's object into one binary, so two aircraft sharing one namespace (the generic-name
# mistake this fixes) would be a silent One Definition Rule violation the instant a copied
# aircraft's own math header is edited to differ from _template's. The namespace tokens below are
# therefore rewritten on copy exactly like the YAML plugin paths above, not left as a manual step a
# new-aircraft author could forget.
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
# _math.hpp's own aircraft-specific namespace (see this file's own header comment): rewritten in
# every .hpp/.cpp this aircraft owns, not just the YAML, so a static-plugins build never links two
# aircraft's math headers under the same namespace.
find "$dest" -type f \( -name '*.hpp' -o -name '*.cpp' \) -exec \
  sed -i "s#aircraft_template_aero_math#aircraft_${name}_aero_math#g; \
          s#aircraft_template_turbofan_math#aircraft_${name}_turbofan_math#g" {} +

trap - ERR
echo "Created $dest from aircraft/_template. Its physical/mass config is still zeroed/TODO --" \
     "edit $dest/configs/*.yaml before running it (it will refuse to run un-edited)."
