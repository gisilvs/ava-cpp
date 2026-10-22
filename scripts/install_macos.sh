#!/usr/bin/env bash
# Run from the packaged folder. No repository, Python, downloads or login needed.
set -euo pipefail
package_dir=$(cd "$(dirname "$0")" && pwd)
install_root="${AVA_INSTALL_ROOT:-$HOME}"
if [[ "$install_root" == "$HOME" ]] && { pgrep -x 'Logic Pro' >/dev/null || pgrep -x Ava >/dev/null; }; then
  echo "Quit Logic Pro and Ava before installing." >&2
  exit 1
fi
for bundle in Standalone/Ava.app AU/Ava.component VST3/Ava.vst3; do
  codesign --verify --deep --strict "$package_dir/$bundle"
done
(cd "$package_dir/models" && shasum -a 256 -c checksums.txt)
backup="$install_root/Library/Application Support/Ava/backups/$(date +%Y%m%d_%H%M%S)_$$"
install_bundle() {
  local source="$1" parent="$2" name="$3" staging
  mkdir -p "$parent"
  staging=$(mktemp -d "$parent/.ava-install.XXXXXX")
  ditto "$source" "$staging/$name"
  codesign --verify --deep --strict "$staging/$name"
  if [[ -e "$parent/$name" ]]; then
    mkdir -p "$backup"
    mv "$parent/$name" "$backup/$name"
  fi
  mv "$staging/$name" "$parent/$name"
  rmdir "$staging"
}
model_parent="$install_root/Library/Application Support/Ava"
mkdir -p "$model_parent"
staging=$(mktemp -d "$model_parent/.ava-models.XXXXXX")
trap 'rm -rf "$staging"' EXIT
for file in "$package_dir"/models/*; do
  cp -c "$file" "$staging/" 2>/dev/null || cp "$file" "$staging/"
done
(cd "$staging" && shasum -a 256 -c checksums.txt)
if [[ -d "$model_parent/generator-native" ]]; then
  mkdir -p "$backup"
  mv "$model_parent/generator-native" "$backup/generator-native"
fi
mv "$staging" "$model_parent/generator-native"
install_bundle "$package_dir/AU/Ava.component" "$install_root/Library/Audio/Plug-Ins/Components" Ava.component
install_bundle "$package_dir/VST3/Ava.vst3" "$install_root/Library/Audio/Plug-Ins/VST3" Ava.vst3
install_bundle "$package_dir/Standalone/Ava.app" "$install_root/Applications" Ava.app
echo "Installed Ava (AU, VST3, standalone and INT8 models)."
echo "Reopen your host; rescan plugins if necessary. Existing versions are backed up in $backup when replaced."
