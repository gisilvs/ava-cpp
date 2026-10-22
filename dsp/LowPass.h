#pragma once

namespace ava {
// Resonant biquad. Changing coefficients preserves the filter's history.
class LowPass {
public:
    void reset();
    void setCutoff(double cutoffHz, double q, double sampleRate);
    double process(double sample);

private:
    // Feedforward (b) and feedback (a) coefficients, with a0 normalized to one.
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
    // The previous two input samples (x) and output samples (y).
    double x1 = 0.0, x2 = 0.0;
    double y1 = 0.0, y2 = 0.0;
};
} // namespace ava
