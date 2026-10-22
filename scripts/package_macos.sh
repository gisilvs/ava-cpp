#!/usr/bin/env bash
# Collect the verified build and models. No compilation or model export here.
set -euo pipefail
cd "$(dirname "$0")/.."
output="$PWD/build/release/Ava-0.1.0-macos-arm64"
assets="${AVA_GENERATOR_ASSETS:-$PWD/../ava-torch/outputs/generator_int8}"
if [[ -e "$output" ]]; then
  echo "Package already exists: $output (move it aside before rebuilding)." >&2
  exit 1
fi
for bundle in Standalone/Ava.app AU/Ava.component VST3/Ava.vst3; do
  codesign --verify --deep --strict "build/plugin/Ava_artefacts/Release/$bundle"
done
(cd "$assets" && shasum -a 256 -c checksums.txt)
mkdir -p "$output"
for format in Standalone AU VST3; do
  ditto "build/plugin/Ava_artefacts/Release/$format" "$output/$format"
done
mkdir "$output/models"
for name in text_encoder.onnx text_encoder.weights denoiser.onnx denoiser.weights tokenizer.bin sampler.json manifest.json checksums.txt; do
  cp -c "$assets/$name" "$output/models/$name" 2>/dev/null || cp "$assets/$name" "$output/models/$name"
done
cp scripts/install_macos.sh "$output/install.sh"
cp docs/macos_package.md "$output/README.md"
cp -R examples "$output/presets"
git rev-parse HEAD > "$output/source-commit.txt"
# Include local edits: the checkout need not be committed to package a test build.
git status --short > "$output/local-changes.txt"
echo "Prepared $output"
echo "Install with: bash \"$output/install.sh\""
