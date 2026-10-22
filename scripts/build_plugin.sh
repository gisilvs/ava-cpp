#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash scripts/setup_build.sh
bash scripts/setup_onnx.sh
build/tools/bin/cmake -S . -B build/plugin -DCMAKE_BUILD_TYPE=Release \
  -DAVA_BUILD_PLUGIN=ON \
  -DAVA_MAPPER_DIR="${AVA_MAPPER_DIR:-$PWD/models/mapper}"
build/tools/bin/cmake --build build/plugin --parallel "${BUILD_JOBS:-1}"
# Seal the finished bundles after JUCE has generated the VST3 metadata.
# Ad-hoc signing is sufficient for testing locally; no developer certificate.
if [[ "$(uname)" == Darwin ]]; then
  for bundle in Standalone/Ava.app VST3/Ava.vst3 AU/Ava.component; do
    path="build/plugin/Ava_artefacts/Release/$bundle"
    # Refresh weights even when no C++ source needed recompilation.
    mapper="${AVA_MAPPER_DIR:-$PWD/models/mapper}"
    cp "$mapper/mapper.onnx" "$mapper/mapper.json" "$path/Contents/Resources/mapper/"
    mkdir -p "$path/Contents/Helpers"
    cp build/plugin/AvaGenerator "$path/Contents/Helpers/AvaGenerator"
    codesign --force --sign - "$path/Contents/Frameworks/libonnxruntime.1.dylib"
    codesign --force --sign - "$path/Contents/Helpers/AvaGenerator"
    codesign --force --sign - "$path"
    codesign --verify --deep --strict "$path"
  done
fi
build/tools/bin/ctest --test-dir build/plugin --output-on-failure
