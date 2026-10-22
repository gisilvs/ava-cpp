#pragma once
#include "plugin/PresetFile.h"

// File access and ONNX execution belong on a worker, never in processBlock.
class LatentMapper {
public:
    static PresetFile predict(const juce::File& modelDirectory,
                              const juce::File& latent, double previewMidi);
};
