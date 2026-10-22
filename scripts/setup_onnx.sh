#!/usr/bin/env bash
# Official native SDK; keep it separate from Python's training environment.
set -euo pipefail
cd "$(dirname "$0")/.."
version=1.31.0
sdk="build/onnxruntime-osx-arm64-$version"
if [[ -f "$sdk/include/onnxruntime_cxx_api.h" && -f "$sdk/lib/libonnxruntime.$version.dylib" ]]; then exit 0; fi
if [[ "$(uname -s)" != Darwin || "$(uname -m)" != arm64 ]]; then
  echo "This plugin setup currently targets Apple Silicon macOS." >&2
  exit 1
fi
mkdir -p build/downloads
archive="build/downloads/onnxruntime-$version.tgz"
curl -fL "https://github.com/microsoft/onnxruntime/releases/download/v$version/onnxruntime-osx-arm64-$version.tgz" -o "$archive"
printf '%s  %s\n' 1031b36dd3bdaa8d976a6130d9cf2a05e9be320b93ae804f8427b91dc3432afc "$archive" | shasum -a 256 -c -
tar -xzf "$archive" -C build
