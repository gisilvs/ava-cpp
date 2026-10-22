#pragma once
#include <juce_core/juce_core.h>
#include "dsp/Preset.h"
#include <array>
#include <cmath>

namespace parameters {
// Host values stay in synth_v1 coordinates; only the playable range is restricted.
struct Control {
    const char* id;
    const char* name;
    float initial;
    float minimum = 0.f;
    float maximum = 1.f;
};

// Inverse of the DSP's duration mapping: seconds = 0.1 * u / (1 - u).
inline constexpr float timeControl(float seconds) { return seconds / (seconds + .1f); }

// IDs and order are shared by automation, preset files and the mapper handoff.
// Gain's upper control value .5 gives unity amplitude. Time knobs cover 1 ms
// through 5 s (attack) or 10 s (decay/release); the DSP itself has wider ranges.
inline const std::array<Control, 13> controls {{
    {"gain", "Gain", .2f, 0.f, .5f},
    {"mix", "Mix", .3f},
    {"detune_cents", "Detune B", 0.f},
    {"noise_mix", "Noise", 0.f},
    {"cutoff_octaves", "Cutoff (keytrack)", float(std::log(100.0) / std::log(500.0))},
    {"q", "Resonance", float(std::log(.707 / .5) / std::log(20.0))},
    {"lfo_rate", "LFO rate", float(std::log(4.0) / std::log(20.0))},
    {"lfo_depth", "LFO depth", 0.f},
    {"drive", "Drive", 0.f},
    {"attack", "Attack", 1.f / 11.f, timeControl(.001f), timeControl(5.f)},
    {"decay", "Decay", 2.f / 3.f, timeControl(.001f), timeControl(10.f)},
    {"sustain", "Sustain", .7f},
    {"release", "Release", .75f, timeControl(.001f), timeControl(10.f)}
}};
inline const juce::StringArray waveforms {"Sine", "Square", "Sawtooth", "Triangle"};

// The host array ends with oscillator A and B, after the continuous controls.
inline const char* id(size_t index) {
    if (index < controls.size())
        return controls[index].id;
    return index == controls.size() ? "oscillator" : "oscillator_b";
}

inline ava::Controls synthControls(const std::array<float, 15>& values) {
    // The final two host values are oscillator IDs, not continuous controls.
    ava::Controls result;
    result.gain = values[0];
    result.mix = values[1];
    result.detuneCents = values[2];
    result.noiseMix = values[3];
    result.cutoff = values[4];
    result.q = values[5];
    result.lfoRate = values[6];
    result.lfoDepth = values[7];
    result.drive = values[8];
    result.attack = values[9];
    result.decay = values[10];
    result.sustain = values[11];
    result.release = values[12];
    return result;
}
} // namespace parameters
