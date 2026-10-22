#pragma once
#include <cmath>

namespace ava {
inline double cutoffLfo(double time, double rate, double depth) {
    constexpr double pi = 3.14159265358979323846;
    return depth * std::sin(2.0 * pi * rate * time);
}

inline double distort(double sample, double drive) {
    if (drive < 1e-4)
        return sample + drive * drive * (sample - sample * sample * sample) / 3.0;
    return std::tanh(drive * sample) / std::tanh(drive);
}
} // namespace ava
