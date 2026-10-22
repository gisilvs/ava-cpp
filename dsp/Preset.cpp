#include "Preset.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ava {
namespace {
void checkRange(double value, double low, double high, const char* name) {
    if (!std::isfinite(value) || value < low || value > high)
        throw std::invalid_argument(name);
}

void checkPositive(double value, const char* name) {
    if (!std::isfinite(value) || value <= 0.0)
        throw std::invalid_argument(name);
}

// Stretch a normalized control with u / (1 - u), then apply its physical scale.
// Float epsilon keeps the endpoints finite, matching Python's float32 guards.
double positiveControl(float value, double scale, bool allowZero = false) {
    const double epsilon = std::numeric_limits<float>::epsilon();
    const double numerator = allowZero ? value : std::max(double(value), epsilon);
    return scale * numerator / std::max(1.0 - value, epsilon);
}

// Q and LFO rate span 0.5 to 10; equal control steps give equal ratios.
double logarithmicControl(float value) {
    return std::exp(std::log(0.5) + value * std::log(20.0));
}
} // namespace

double midiFrequency(double note) {
    checkRange(note, 0.0, 127.0, "MIDI note must be between 0 and 127");
    return 440.0 * std::exp2((note - 69.0) / 12.0);
}

double maxCutoff(double sampleRate) {
    return std::min(10000.0, 0.45 * sampleRate);
}

double maxLfoDepth(double center, double frequency, double sampleRate) {
    const double lower = std::log2(20.0 / frequency);
    const double upper = std::log2(maxCutoff(sampleRate) / frequency);
    return std::clamp(std::min(center - lower, upper - center), 0.0, 2.0);
}

Preset decodeControls(const Controls& controls, double frequency, double sampleRate,
                      Waveform oscillator, Waveform oscillatorB) {
    for (float value : {controls.gain, controls.mix, controls.detuneCents, controls.noiseMix, controls.cutoff, controls.q,
                        controls.lfoRate, controls.lfoDepth, controls.drive, controls.attack, controls.decay, controls.sustain, controls.release})
        checkRange(value, 0.0, 1.0, "Controls must be finite and between 0 and 1");
    checkPositive(sampleRate, "Sample rate must be positive");
    checkPositive(frequency, "Frequency must be positive");
    if (maxCutoff(sampleRate) <= 20.0)
        throw std::invalid_argument("Sample rate is too low for the filter");

    Preset preset;
    preset.oscillator = oscillator;
    preset.oscillatorB = oscillatorB;
    // Gain is linear amplitude; envelope durations are seconds.
    preset.gain = positiveControl(controls.gain, 1.0, true);
    preset.attack = positiveControl(controls.attack, 0.1);
    preset.decay = positiveControl(controls.decay, 0.1);
    preset.release = positiveControl(controls.release, 0.1);
    // These controls keep their linear scales.
    preset.sustain = controls.sustain;
    preset.mix = controls.mix;
    preset.detuneCents = 30.0 * controls.detuneCents;
    preset.noiseMix = controls.noiseMix;
    preset.drive = 10.0 * controls.drive;
    // Resonance and modulation rate use logarithmic scales.
    preset.q = logarithmicControl(controls.q);
    preset.lfoRate = logarithmicControl(controls.lfoRate);
    preset.filterEnabled = true;
    preset.lfoEnabled = true;
    // Express cutoff in octaves relative to the note, within 20 Hz to 10 kHz
    // (or the sample-rate limit). Depth shrinks near either cutoff boundary.
    const double lower = std::log2(20.0 / frequency);
    const double upper = std::log2(maxCutoff(sampleRate) / frequency);
    preset.cutoffOctaves = lower + controls.cutoff * (upper - lower);
    preset.lfoDepth = controls.lfoDepth * maxLfoDepth(preset.cutoffOctaves, frequency, sampleRate);
    validatePreset(preset, frequency, sampleRate);
    return preset;
}

void validatePreset(const Preset& preset, double frequency, double sampleRate) {
    checkPositive(sampleRate, "Sample rate must be positive");
    checkPositive(frequency, "Frequency must be positive");
    if (frequency >= sampleRate / 2.0)
        throw std::invalid_argument("Frequency must be below Nyquist");
    checkRange(int(preset.oscillator), 0, 3, "Unknown oscillator");
    checkRange(int(preset.oscillatorB), 0, 3, "Unknown oscillator B");
    checkRange(preset.mix, 0, 1, "Mix must be between 0 and 1");
    checkRange(preset.detuneCents, 0, 30, "Detune must be between 0 and 30 cents");
    if (!preset.secondOscillator && preset.detuneCents != 0.0)
        throw std::invalid_argument("Detuning requires oscillator B");
    if (preset.secondOscillator && frequency * std::exp2(preset.detuneCents / 1200.0) >= sampleRate / 2.0)
        throw std::invalid_argument("Oscillator B must be below Nyquist");
    checkRange(preset.noiseMix, 0, 1, "Noise mix must be between 0 and 1");
    checkRange(preset.drive, 0, 10, "Drive must be between 0 and 10");
    checkRange(preset.sustain, 0, 1, "Sustain must be between 0 and 1");
    checkPositive(preset.attack, "Attack must be positive seconds");
    checkPositive(preset.decay, "Decay must be positive seconds");
    checkPositive(preset.release, "Release must be positive seconds");
    if (!std::isfinite(preset.gain) || preset.gain < 0.0)
        throw std::invalid_argument("Gain must be finite and nonnegative");
    if (preset.lfoEnabled && !preset.filterEnabled)
        throw std::invalid_argument("Cutoff LFO requires an enabled filter");
    if (preset.filterEnabled) {
        const double cutoff = frequency * std::exp2(preset.cutoffOctaves);
        checkRange(cutoff, 20.0 - 1e-6, maxCutoff(sampleRate) + 1e-6, "Cutoff outside filter range");
        checkRange(preset.q, 0.5, 10, "Q must be between 0.5 and 10");
        if (preset.lfoEnabled) {
            checkRange(preset.lfoRate, 0.5, 10, "LFO rate must be between 0.5 and 10 Hz");
            checkRange(preset.lfoDepth, 0, maxLfoDepth(preset.cutoffOctaves, frequency, sampleRate) + 1e-6,
                       "LFO depth exceeds available cutoff range");
        }
    }
}
} // namespace ava
