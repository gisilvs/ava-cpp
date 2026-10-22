#pragma once

namespace ava {

// IDs match Synth.oscillator_names in the Python implementation.
enum class Waveform { sine = 0, square = 1, sawtooth = 2, triangle = 3 };

// Physical DSP values: seconds for ADSR, Hz for LFO rate, octaves for cutoff/depth.
// Gain is linear amplitude; detuning is measured in cents.
struct Preset {
    Waveform oscillator = Waveform::sine;
    Waveform oscillatorB = Waveform::sine;
    bool secondOscillator = true;
    double mix = 0.5;
    double detuneCents = 0.0;
    double noiseMix = 0.0;
    bool filterEnabled = false;
    double cutoffOctaves = 1.5;
    double q = 0.707;
    bool lfoEnabled = false;
    double lfoRate = 0.5;
    double lfoDepth = 0.0;
    double drive = 0.0;
    double attack = 0.01;
    double decay = 0.2;
    double sustain = 1.0;
    double release = 1.5;
    double gain = 0.5;
};

// Named 0–1 network controls. Physical conversions belong to the DSP.
// This is the synth_v1 mapping, not the old dataset's narrower sampling ranges.
struct Controls {
    float gain = 0.5f;
    float mix = 0.5f;
    float detuneCents = 0.0f;
    float noiseMix = 0.0f;
    float cutoff = 0.5f;
    float q = 0.0f;
    float lfoRate = 0.0f;
    float lfoDepth = 0.0f;
    float drive = 0.0f;
    float attack = 0.5f;
    float decay = 0.5f;
    float sustain = 1.0f;
    float release = 0.5f;
};

inline constexpr const char* synthVersion = "noise-distortion-1";
inline constexpr const char* controlMapping = "synth_v1";

double midiFrequency(double note);
double maxCutoff(double sampleRate);
double maxLfoDepth(double cutoffOctaves, double frequency, double sampleRate);
Preset decodeControls(const Controls& controls, double frequency, double sampleRate,
                      Waveform oscillator, Waveform oscillatorB);
void validatePreset(const Preset& preset, double frequency, double sampleRate);

} // namespace ava
