#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Activate emsdk first. Pass an output path, or use build/web/ava.wasm.
output="${1:-build/web/ava.wasm}"
mkdir -p "$(dirname "$output")"
em++ web/bridge.cpp dsp/Engine.cpp dsp/Envelope.cpp dsp/LowPass.cpp \
  dsp/Oscillator.cpp dsp/Preset.cpp -std=c++17 -O3 --no-entry \
  -sSTANDALONE_WASM=1 -sFILESYSTEM=0 -sINITIAL_MEMORY=2097152 \
  -sSTACK_SIZE=65536 -sALLOW_MEMORY_GROWTH=0 \
  '-sEXPORTED_FUNCTIONS=["_ava_controls","_ava_set_controls","_ava_note_on","_ava_note_off","_ava_preview","_ava_preview_active","_ava_reset","_ava_render","_ava_value"]' \
  -o "$output"
