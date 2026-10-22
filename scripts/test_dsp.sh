#!/usr/bin/env bash
# A compiler is enough; this test does not need JUCE, LibTorch or CMake.
set -euo pipefail
cd "$(dirname "$0")/.."
output=$(mktemp -d)
trap 'rm -rf "$output"' EXIT
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -I dsp dsp/Engine.cpp dsp/Envelope.cpp dsp/LowPass.cpp \
  dsp/Oscillator.cpp dsp/Preset.cpp tests/dsp_tests.cpp -o "$output/dsp_tests"
"$output/dsp_tests"
