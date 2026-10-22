# Ava in the browser

The blog demo uses the existing `dsp/` engine in an AudioWorklet. `bridge.cpp`
only exposes controls, note events, rendering and a four-second preview through
small C functions. It doesn't use JUCE, ONNX or the text generator.

Install and activate [Emscripten](https://emscripten.org/docs/getting_started/downloads.html),
then build from this repository:

```bash
bash web/build.sh /path/to/gisilvs.github.io/assets/ava-gallery/ava.wasm
```

The current build uses Emscripten6.0.13. The standalone WASM module has no imports
and2MiB of fixed memory. Call `_initialize()` once after instantiation. Controls
are13float32 values in the same order as `plugin/Parameters.h`; decode at the
preset's reference pitch, then keep key tracking when playing other notes.

The website's `blog/ava/ava-worklet.js` adapts the24kHz output to the browser's
sample rate. `listening.js` loads the precomputed presets and supplies the UI.
A separate browser output limiter protects playback; it isn't part of the DSP
or the saved audio. Text generation is intentionally disabled in this demo.
