#include "HostRenderer.h"
#include <cmath>
#include <stdexcept>

void HostRenderer::prepare(double hostRate, int maxBlockSize) {
    if (!std::isfinite(hostRate) || hostRate < synthRate)
        throw std::invalid_argument("Ava needs a host sample rate of at least 24 kHz");
    ratio = synthRate / hostRate;
    blockSize = std::max(1, maxBlockSize);
    input.resize(size_t(std::ceil(blockSize * ratio)) + 2);
    reset();
}

void HostRenderer::reset() {
    synth.reset();
    interpolator.reset();
    inputPosition = 1.0;
}

int HostRenderer::latencySamples() const {
    return ratio == 1.0 ? 0 : juce::roundToInt(interpolator.getBaseLatency() / ratio);
}

void HostRenderer::render(float* output, int sampleCount) {
    if (ratio == 1.0) { synth.render(output, sampleCount); return; }
    while (sampleCount > 0) {
        const int count = std::min(sampleCount, blockSize);
        int needed = 0;
        // Mirror the interpolator's input clock, so we never synthesize ahead
        // of a MIDI event at the next segment boundary. No look-ahead buffer.
        for (int i = 0; i < count; ++i) {
            while (inputPosition >= 1.0) { ++needed; inputPosition -= 1.0; }
            inputPosition += ratio;
        }
        jassert(needed <= int(input.size()));
        synth.render(input.data(), needed);
        const int consumed = interpolator.process(ratio, input.data(), output, count);
        jassert(consumed == needed);
        juce::ignoreUnused(consumed);
        output += count;
        sampleCount -= count;
    }
}
