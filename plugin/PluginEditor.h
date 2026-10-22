#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "inference/LatentJob.h"

class AvaLookAndFeel : public juce::LookAndFeel_V4 {
public:
    AvaLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                         float position, float start, float end, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
};

// A label, a dial and a physical-unit readout. Reused for all 13 controls.
class ControlDial : public juce::Component {
public:
    void connect(AvaAudioProcessor&, int parameterIndex, juce::Colour accent);
    void refresh();
    void resized() override;
    juce::Slider slider;
private:
    AvaAudioProcessor* audioProcessor = nullptr;
    int index = 0;
    juce::Label title, value;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class AvaAudioProcessorEditor : public juce::AudioProcessorEditor,
                                public juce::FileDragAndDropTarget,
                                private juce::Timer {
public:
    explicit AvaAudioProcessorEditor(AvaAudioProcessor&);
    ~AvaAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

private:
    void timerCallback() override;
    void choosePreset(bool save);
    void generateText();
    void loadPreset(const juce::File&);
    void paintWave(juce::Graphics&, juce::Rectangle<float>, int waveform, juce::Colour);
    void paintEnvelope(juce::Graphics&, juce::Rectangle<float>);
    AvaAudioProcessor& audioProcessor;
    AvaLookAndFeel look;
    std::array<ControlDial, 13> dials;
    juce::ComboBox oscillatorA, oscillatorB, instrument;
    juce::TextEditor description;
    juce::TextButton generateButton {"Generate"};
    juce::Label generationStatus;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachmentA, attachmentB;
    std::unique_ptr<LatentJob> latentJob;
    juce::TextButton loadButton {"Load"}, saveButton {"Save"}, previewButton {"Preview"};
    juce::MidiKeyboardComponent keyboard;
    juce::TooltipWindow tooltip {this, 600};
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status = "Load a preset, or start shaping a sound.", lastName;
    float meter = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AvaAudioProcessorEditor)
};
