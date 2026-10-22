#include "LowPass.h"
#include <cmath>

namespace ava {
void LowPass::reset() {
    x1 = x2 = y1 = y2 = 0.0;
}

void LowPass::setCutoff(double cutoffHz, double q, double sampleRate) {
    constexpr double pi = 3.14159265358979323846;
    // Convert Hz to radians per sample; Q controls the resonance.
    const double omega = 2.0 * pi * cutoffHz / sampleRate;
    const double cosine = std::cos(omega);
    const double alpha = std::sin(omega) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    // Divide every coefficient by a0, matching the Python biquad.
    b0 = (1.0 - cosine) / (2.0 * a0);
    b1 = (1.0 - cosine) / a0;
    b2 = b0;
    a1 = -2.0 * cosine / a0;
    a2 = (1.0 - alpha) / a0;
    // Updating coefficients must not reset the input/output history.
}

double LowPass::process(double sample) {
    // Apply the biquad recurrence, then shift the history by one sample.
    const double output = b0 * sample + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = sample;
    y2 = y1;
    y1 = output;
    return output;
}
} // namespace ava
