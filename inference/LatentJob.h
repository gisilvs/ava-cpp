#pragma once
#include "plugin/PresetFile.h"
#include <optional>

// A background request keeps generation and model loading off the audio thread.
struct TextRequest {
    juce::File generatorDirectory, outputDirectory;
    juce::String instrument, description;
    int midi = 60, seed = 123;
};

class LatentJob : public juce::Thread {
public:
    LatentJob(juce::File modelDirectory, juce::File latent, double midi);
    LatentJob(juce::File modelDirectory, TextRequest request);
    ~LatentJob() override;
    juce::String progress() const;
    bool takeResult(PresetFile&, juce::String& error);
private:
    void run() override;
    juce::File generateText();
    juce::File mapperDirectory, latentFile;
    double previewMidi;
    std::optional<TextRequest> text;
    std::atomic<int> stage {0};
    juce::CriticalSection lock;
    PresetFile preset;
    juce::String error;
    bool finished = false;
};
