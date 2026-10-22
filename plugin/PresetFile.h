#pragma once
#include "Parameters.h"

// JSON is a wrapper concern. The DSP receives ordinary values, not JSON objects.
struct PresetFile {
    juce::String name = "Init";
    juce::String description;
    double previewMidi = 60.0;
    // Thirteen normalized controls, followed by oscillator A and B IDs.
    std::array<float, 15> values{};

    static juce::Result read(const juce::File& file, PresetFile& preset);
    juce::Result write(const juce::File& file) const;
};
