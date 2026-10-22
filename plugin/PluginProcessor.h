#pragma once
#include <JuceHeader.h>
#include "HostRenderer.h"
#include "PresetFile.h"
#include <array>
#include <atomic>

class AvaAudioProcessor : public juce::AudioProcessor {
public:
    AvaAudioProcessor();
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ava"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Init"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& data) override;
    void setStateInformation(const void* data, int size) override;

    juce::Result loadPreset(const juce::File& file);
    void applyPreset(const PresetFile& preset);
    juce::Result savePreset(const juce::File& file) const;
    juce::String presetName() const;
    double previewNote() const { return previewMidi.load(); }
    void preview() { previewRequest.store(previewActive.load() ? -1 : 1); }
    bool isPreviewing() const { return previewActive.load(); }
    juce::String valueText(int index) const;
    std::array<float, 15> parameterValues() const;

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboard;
    std::atomic<float> outputPeak {0.0f};

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout();
    void render(juce::AudioBuffer<float>& buffer, int offset, int count);
    void handleMidi(const juce::MidiMessage& message);
    void applyParameters();
    void handlePreviewRequest();
    HostRenderer renderer;
    std::array<std::atomic<float>*, 15> rawParameters{};
    double previousPreviewMidi = -1;
    std::array<float, 15> previousValues{};
    std::atomic<bool> resetRequested {false};
    std::atomic<bool> previewActive {false};
    // The editor posts a start (1) or stop (-1); the audio thread consumes it.
    std::atomic<int> previewRequest {0};
    std::atomic<double> previewMidi {60.0};
    int previewSamplesRemaining = 0;
    double hostSampleRate = 24000.0;
    bool canRender = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AvaAudioProcessor)
};
