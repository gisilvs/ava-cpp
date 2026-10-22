#include "Envelope.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ava {
namespace {
double curve(double progress) {
    // Normalize an exponential rise to reach exactly 0 and 1 at its endpoints.
    // Curvature 4 matches the fixed envelope shape used by the Python synth.
    constexpr double curvature = 4.0;
    progress = std::clamp(progress, 0.0, 1.0);
    return -std::expm1(-curvature * progress) / -std::expm1(-curvature);
}
} // namespace

void Envelope::start(double attackSeconds, double decaySeconds, double sustainLevel,
                     double releaseSeconds, double newSampleRate) {
    if (!std::isfinite(newSampleRate) || newSampleRate <= 0.0)
        throw std::invalid_argument("Sample rate must be positive");
    setParameters(attackSeconds, decaySeconds, sustainLevel, releaseSeconds);
    sampleRate = newSampleRate;
    ageSamples = 0;
    releaseSamples = 0;
    releaseLevel = 0.0;
    released = false;
    active = true;
}

void Envelope::setParameters(double attackSeconds, double decaySeconds,
                             double sustainLevel, double releaseSeconds) {
    if (!std::isfinite(attackSeconds) || !std::isfinite(decaySeconds)
        || !std::isfinite(sustainLevel) || !std::isfinite(releaseSeconds)
        || attackSeconds <= 0 || decaySeconds <= 0 || releaseSeconds <= 0
        || sustainLevel < 0 || sustainLevel > 1)
        throw std::invalid_argument("Invalid ADSR parameters");
    attack = attackSeconds;
    decay = decaySeconds;
    sustain = sustainLevel;
    release = releaseSeconds;
}

double Envelope::heldLevel(double time) const {
    if (time < attack)
        return curve(time / attack);
    // After attack, decay from one to sustain. Clamping in curve() holds sustain.
    return 1.0 - (1.0 - sustain) * curve((time - attack) / decay);
}

void Envelope::noteOff() {
    if (!active || released)
        return;
    // Release starts from the current level, even if the note ends during attack.
    releaseLevel = heldLevel(double(ageSamples) / sampleRate);
    releaseSamples = 0;
    released = true;
}

double Envelope::nextValue() {
    if (!active)
        return 0.0;
    double value;
    if (released) {
        const double progress = double(releaseSamples++) / (sampleRate * release);
        value = releaseLevel * (1.0 - curve(progress));
        if (progress >= 1.0)
            active = false;
    } else {
        value = heldLevel(double(ageSamples) / sampleRate);
    }
    ++ageSamples;
    return value;
}

bool Envelope::isActive() const { return active; }
} // namespace ava
