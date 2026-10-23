#!/usr/bin/env bash
# Install exported ONNX models once, shared by all plugin instances. No Python.
set -euo pipefail
cd "$(dirname "$0")/.."
source_dir="${AVA_GENERATOR_ASSETS:-$PWD/../ava-torch/outputs/generator_int8}"
parent="$HOME/Library/Application Support/Ava"
destination="$parent/generator-native"
files=(text_encoder.onnx text_encoder.weights denoiser.onnx denoiser.weights tokenizer.bin sampler.json manifest.json checksums.txt)
for name in "${files[@]}"; do
  if [[ ! -f "$source_dir/$name" ]]; then
    echo "Missing native model export: $source_dir/$name" >&2
    exit 1
  fi
done
mkdir -p "$parent"
staging=$(mktemp -d "$parent/.native-generator-install.XXXXXX")
trap 'rm -rf "$staging"' EXIT
for name in "${files[@]}"; do
  # APFS clones share disk blocks with the local export. Copy normally elsewhere.
  cp -c "$source_dir/$name" "$staging/$name" 2>/dev/null || cp "$source_dir/$name" "$staging/$name"
done
cp licenses/STABILITY_AI_COMMUNITY_LICENSE.md "$staging/LICENSE_STABILITY.md"
cp licenses/GEMMA_TERMS.md "$staging/LICENSE_GEMMA.md"
cp models/generator/NOTICE "$staging/NOTICE"
(cd "$staging" && shasum -a 256 -c checksums.txt)
if [[ -d "$destination" ]]; then
  backup="build/native-generator-backups/$(date +%Y%m%d_%H%M%S)_$$"
  mkdir -p "$backup"
  mv "$destination" "$backup/generator-native"
  echo "Previous native generator saved in $backup"
fi
mv "$staging" "$destination"
echo "Installed native ONNX generator: $destination"
