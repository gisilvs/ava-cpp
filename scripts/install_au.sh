#!/usr/bin/env bash
# Install the local build without overwriting the last working Audio Unit.
set -euo pipefail
cd "$(dirname "$0")/.."
if pgrep -x 'Logic Pro' >/dev/null; then
  echo "Quit Logic Pro before installing the new Audio Unit." >&2
  exit 1
fi
source_bundle="build/plugin/Ava_artefacts/Release/AU/Ava.component"
if [[ ! -d "$source_bundle" ]]; then
  echo "Build Ava first: bash scripts/build_plugin.sh" >&2
  exit 1
fi
codesign --verify --deep --strict "$source_bundle"
# Install the native text encoder and diffusion weights. No Python runtime.
bash scripts/install_native_generator.sh
destination="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$destination"
staging=$(mktemp -d "$destination/.ava-install.XXXXXX")
trap 'rm -rf "$staging"' EXIT
ditto "$source_bundle" "$staging/Ava.component"
if [[ -d "$destination/Ava.component" ]]; then
  backup="build/au-backups/$(date +%Y%m%d_%H%M%S)_$$"
  mkdir -p "$backup"
  mv "$destination/Ava.component" "$backup/Ava.component"
  echo "Previous version saved in $backup"
fi
mv "$staging/Ava.component" "$destination/Ava.component"
codesign --verify --deep --strict "$destination/Ava.component"
echo "Installed Ava. Reopen Logic and rescan Ava in Plug-in Manager if necessary."
