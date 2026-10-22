#pragma once
#include <JuceHeader.h>
#include "dsp/Engine.h"
#include <vector>

// The learned instrument always runs at 24 kHz. Only this JUCE-side adapter
// converts to the host rate; the portable DSP has no resampling dependency.
class HostRenderer {
public:
    static constexpr double synthRate = 24000.0;
    void prepare(double hostRate, int maxBlockSize);
    void reset();
    void render(float* output, int sampleCount);
    int latencySamples() const;
    ava::Engine synth {synthRate, synthRate};

private:
    juce::WindowedSincInterpolator interpolator;
    std::vector<float> input;
    double ratio = 1.0, inputPosition = 1.0;
    int blockSize = 512;
};
