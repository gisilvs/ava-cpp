#include "Oscillator.h"
#include <cmath>
#include <stdexcept>

namespace ava {
namespace {
constexpr double pi = 3.14159265358979323846;
} // namespace

void Oscillator::start(Waveform waveform, double frequency, double sampleRate) {
    // A new note starts at phase zero; configure() alone preserves phase.
    phase = 0.0;
    configure(waveform, frequency, sampleRate);
}

void Oscillator::configure(Waveform waveform, double frequency, double sampleRate) {
    if (!std::isfinite(sampleRate) || !std::isfinite(frequency) || frequency <= 0.0
        || frequency >= sampleRate / 2.0)
        throw std::invalid_argument("Oscillator frequency must be positive and below Nyquist");
    if (int(waveform) < 0 || int(waveform) > 3)
        throw std::invalid_argument("Unknown oscillator");
    phaseIncrement = 2.0 * pi * frequency / sampleRate;
    // Cache amplitudes when the pitch or waveform changes, not every sample.
    amplitudes.fill(0.0);
    for (int harmonic = 1; harmonic <= maxHarmonic; ++harmonic) {
        if (frequency * harmonic >= sampleRate / 2.0)
            break;
        switch (waveform) {
        case Waveform::sine:
            if (harmonic == 1) amplitudes[harmonic] = 1.0;
            break;
        case Waveform::square:
            // Odd harmonics, with amplitude proportional to 1 / harmonic.
            if (harmonic % 2 == 1) amplitudes[harmonic] = 4.0 / (pi * harmonic);
            break;
        case Waveform::sawtooth:
            // All harmonics, alternating sign and falling as 1 / harmonic.
            amplitudes[harmonic] = (harmonic % 2 == 1 ? 2.0 : -2.0) / (pi * harmonic);
            break;
        case Waveform::triangle:
            // Odd harmonics with alternating signs, falling as 1 / harmonic squared.
            if (harmonic % 2 == 1) {
                const double sign = (harmonic % 4 == 1) ? 1.0 : -1.0;
                amplitudes[harmonic] = 8.0 * sign / (pi * pi * harmonic * harmonic);
            }
            break;
        }
    }
}

double Oscillator::nextSample() {
    double sample = 0.0;
    for (int harmonic = 1; harmonic <= maxHarmonic; ++harmonic) {
        if (amplitudes[harmonic] != 0.0)
            sample += amplitudes[harmonic] * std::sin(harmonic * phase);
    }
    phase += phaseIncrement;
    if (phase >= 2.0 * pi)
        phase -= 2.0 * pi;
    return sample;
}
} // namespace ava
