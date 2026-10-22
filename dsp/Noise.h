#pragma once
#include <cstdint>
#include <random>

namespace ava {
class Noise {
public:
    void reset(std::uint32_t seed) { generator.seed(seed); }
    double nextSample() {
        // PyTorch CPU float32 uniform sampling uses the low 24 random bits.
        const float uniform = float(generator() & 0xffffffu) / 16777216.0f;
        return 2.0f * uniform - 1.0f;
    }
private:
    std::mt19937 generator{0};
};
} // namespace ava
