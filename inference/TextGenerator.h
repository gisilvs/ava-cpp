#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

// All large models are scoped to this background call. No Python or audio decoding.
class TextGenerator {
public:
    struct Result {
        std::vector<float> latent;
        double setupSeconds = 0, textSeconds = 0, denoiseSeconds = 0;
    };
    static juce::String prompt(const juce::String& instrument, const juce::String& description, int midi);
    static Result generate(const juce::File& assets, const juce::String& prompt, int seed,
                           const std::function<bool()>& cancelled,
                           const std::function<void(int)>& progress,
                           const std::vector<float>* testNoise = nullptr);
    static void saveLatent(const juce::File&, const std::vector<float>&);
};
