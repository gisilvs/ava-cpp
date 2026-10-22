# The portable synth

This is the C++ version of the components in [ava-torch/synth](https://github.com/gisilvs/ava-torch/tree/main/synth). It uses the C++17
standard library only. JUCE still supplies the plugin interface, but nothing in
this folder includes JUCE or LibTorch. The plugin now calls this engine through `HostRenderer`.

The audio path is:

```
oscillator A + oscillator B → noise mix → low-pass + cutoff LFO
                           → distortion → ADSR × gain × velocity
```

## Where to start reading

- `Preset.h/.cpp`: physical parameters and the Python `synth_v1` control mapping.
- `Oscillator.h/.cpp`: sine and the same harmonic sums for square, saw and triangle.
- `Envelope.h/.cpp`: finite-duration exponential ADSR, including early note-off.
- `LowPass.h/.cpp`: the biquad coefficients and its input/output history.
- `Noise.h`: repeatable uniform noise; `Effects.h`: cutoff sine LFO and tanh drive.
- `Engine.h/.cpp`: one voice, then a small eight-voice layer for note events.

There are no factories, inheritance hierarchies or external DSP dependencies.
The calculations use double precision and the output buffers use float32.
The oscillator IDs stay `sine=0, square=1, sawtooth=2, triangle=3`.

## Render a note

```cpp
#include "Engine.h"

ava::Preset preset;
preset.oscillator = ava::Waveform::sawtooth;
preset.oscillatorB = ava::Waveform::triangle;
preset.mix = 0.3;
preset.detuneCents = 8.0;
preset.filterEnabled = true;
preset.cutoffOctaves = 1.5;  // cutoff Hz = note Hz * 2^1.5
preset.q = 1.2;

ava::Engine synth(24000.0, 24000.0); // sample rate, filter-coefficient update rate
synth.setPreset(preset);
synth.noteOn(60, 1.0);             // MIDI note, normalized velocity
synth.render(output, sampleCount); // writes mono samples into an existing buffer
synth.noteOff(60);                 // call between blocks at the exact event position
synth.render(output, sampleCount); // continues through release
```

For exact sample timing, split a host block at MIDI event positions, as the
existing JUCE wrapper already does. Each voice resets oscillator/LFO phase,
filter history and seeded noise at note-on. Render boundaries do not reset them.
Call `reset()` to silence all voices. Velocity zero is note-off. If all eight
voices are active, the oldest is replaced. Sustain pedal and ±2-semitone pitch bend are supported by the engine.
Legato/glide and MPE are not implemented.

`setPreset()` and `setControls()` also update playing voices. Oscillator/LFO
phase,filter history and envelope age are preserved. There is no hidden parameter
smoothing or loudness normalization. The core
neither pans nor limits the output; eight summed voices can exceed ±1.

For a single note at an estimated/fractional pitch, use `ava::Voice` directly:
`voice.start(preset, frequencyHz, velocity, sampleRate, lfoBlockSize, seed)`.
This is also the voice used by `Engine`; there is no second offline DSP path.

## Parameters and network controls

A `Preset` stores physical values: ADSR in seconds, detune in cents, LFO rate in
Hz, cutoff offset and depth in octaves, linear amplitude gain. Optional second
oscillator/filter/LFO flags correspond to Python's optional components; they
are not additional learned parameters. The filter is off in the default Preset.

`Controls` stores the 13 continuous 0–1 network values by name. There is no
hard-coded network output-column order in the DSP. Use `decodeControls()` with
the two selected oscillator IDs, external note frequency and sample rate:

```cpp
ava::Controls controls;
controls.mix = 0.0f;  // full synth range, not the old dataset's 0.2–0.8 interval
const auto physical = ava::decodeControls(
    controls, ava::midiFrequency(60), 24000.0,
    ava::Waveform::sine, ava::Waveform::square);
```

This enables the two oscillators, filter and LFO, as in the trained model.
`Controls.cutoff` maps Python's normalized `cutoff_octaves` value to the full
20 Hz–min(10 kHz,0.45×sample rate) interval. LFO depth uses the remaining cutoff
margin and is limited to two octaves. Q/rate use logarithmic 0.5–10 ranges,
drive is 0–10 and B detune is 0–30 cents. Gain uses u/(1-u), and ADSR durations
use 0.1u/(1-u), with the same float32 endpoint guards as Python. There are no
new training-data range restrictions or output scalings.

A decoded physical preset contains a pitch-relative cutoff. Decode normalized
controls again for a different pitch if you want to preserve the same absolute
cutoff. Invalid pitch/filter/LFO combinations are rejected at note-on instead
of being silently remapped. The JUCE-side `PresetFile` implements versioned normalized JSON import/export;
the old 26-float presets are not interchangeable.

## Filter updates and sample rates

Use `Engine(24000,24000)` for the sample-wise updates in the current training
config, or `Engine(24000,200)` for held200 Hz updates. The Engine constructor
uses 24 kHz/200 Hz by default, matching Python's constructor defaults. A standalone
Voice takes its update interval in samples (default 1). Filter state persists
when coefficients change, including between render blocks.

Begin comparisons at 24 kHz. The core accepts other valid sample rates, but
changing the rate changes which oscillator harmonics fit below Nyquist. The core itself has no resampling dependency. The plugin uses the JUCE-side
`HostRenderer` to keep synthesis at 24kHz and convert output to the host rate.

Noise resets to seed0 unless specified. Its144000 samples match the current CPU
PyTorch float32 generator for seed0 exactly. Separate voices start the same
sequence, as separate reference notes do. A continuous streaming note is not
limited to the six-second reference buffer.

## Build and check

With only a C++ compiler:

```bash
bash scripts/test_dsp.sh
```

With CMake, without installing JUCE or Torch:

```bash
cmake -S . -B build/dsp -DAVA_BUILD_PLUGIN=OFF
cmake --build build/dsp
ctest --test-dir build/dsp --output-on-failure
```

You can also configure `dsp/` directly. Normal plugin builds still use JUCE,but no Torch dependency;
`AVA_BUILD_PLUGIN` defaults to ON.
