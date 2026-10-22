#pragma once
#include "Preset.h"
#include <array>

namespace ava {
// Build each waveform from a finite sum of sine harmonics.
class Oscillator {
public:
    void start(Waveform waveform, double frequency, double sampleRate);
    void configure(Waveform waveform, double frequency, double sampleRate);
    double nextSample();

private:
    // Match Python: harmonics 1 through 31, excluding frequencies at Nyquist.
    static constexpr int maxHarmonic = 31;
    std::array<double, maxHarmonic + 1> amplitudes{};
    double phase = 0.0;
    double phaseIncrement = 0.0;
};
} // namespace ava
