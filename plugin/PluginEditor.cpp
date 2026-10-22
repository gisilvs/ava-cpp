#include "PluginEditor.h"
#include "dsp/Oscillator.h"
#include <dlfcn.h>

namespace {
// Locate our module, not Logic's main application bundle.
juce::File bundledMapper() {
    Dl_info module {};
    if (dladdr(reinterpret_cast<const void*>(&bundledMapper), &module) == 0) return {};
    return juce::File(module.dli_fname).getParentDirectory().getParentDirectory()
        .getChildFile("Resources/mapper");
}

juce::File installedGenerator() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support/Ava/generator-native");
}
const juce::Colour background {0xff10191d}, panel {0xff1a272d}, edge {0xff2b3b42};
const juce::Colour text {0xfff1eee5}, muted {0xff93a6ac}, mint {0xff9ce2c2}, amber {0xffefb681};

juce::Font font(float size, bool bold = false) {
    return juce::Font(juce::FontOptions(size).withStyle(bold ? "Bold" : "Regular"));
}

void card(juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& heading) {
    g.setColour(panel);
    g.fillRoundedRectangle(bounds, 14);
    g.setColour(edge);
    g.drawRoundedRectangle(bounds, 14, 1);
    g.setColour(muted);
    g.setFont(font(12, true));
    g.drawText(heading, bounds.reduced(20, 15).removeFromTop(18), juce::Justification::left);
}
} // namespace

AvaLookAndFeel::AvaLookAndFeel() {
    setColour(juce::Label::textColourId, text);
    setColour(juce::TextEditor::backgroundColourId, background);
    setColour(juce::TextEditor::textColourId, text);
    setColour(juce::TextEditor::outlineColourId, edge);
    setColour(juce::TextEditor::focusedOutlineColourId, mint);
    setColour(juce::ComboBox::backgroundColourId, background);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::outlineColourId, edge);
    setColour(juce::ComboBox::arrowColourId, muted);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, mint);
    setColour(juce::PopupMenu::highlightedTextColourId, background);
    setColour(juce::TextButton::buttonColourId, panel.brighter(.1f));
    setColour(juce::TextButton::textColourOffId, text);
}

void AvaLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                          const juce::Colour& colour, bool over, bool down) {
    g.setColour(colour.brighter(down ? -.1f : over ? .12f : 0));
    g.fillRoundedRectangle(button.getLocalBounds().toFloat().reduced(.5f), 7);
}

void AvaLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                     float position, float start, float end, juce::Slider& slider) {
    const float radius = float(std::min(w, h)) * .39f;
    const float cx = float(x) + float(w) * .5f, cy = float(y) + float(h) * .5f;
    const float angle = start + position * (end - start);
    const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId);
    juce::Path track, active;
    track.addCentredArc(cx, cy, radius + 4, radius + 4, 0, start, end, true);
    active.addCentredArc(cx, cy, radius + 4, radius + 4, 0, start, angle, true);
    g.setColour(edge.brighter(.15f));
    g.strokePath(track, juce::PathStrokeType(3, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(accent);
    g.strokePath(active, juce::PathStrokeType(3, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3b4a50), cx, cy - radius,
                                          juce::Colour(0xff202e34), cx, cy + radius, false));
    g.fillEllipse(cx - radius, cy - radius, 2 * radius, 2 * radius);
    g.setColour(juce::Colour(0xff10191d));
    g.drawEllipse(cx - radius, cy - radius, 2 * radius, 2 * radius, 1);
    g.setColour(text);
    g.drawLine(cx + std::sin(angle) * radius * .55f, cy - std::cos(angle) * radius * .55f,
               cx + std::sin(angle) * radius * .83f, cy - std::cos(angle) * radius * .83f, 2);
}

void ControlDial::connect(AvaAudioProcessor& owner, int parameterIndex, juce::Colour accent) {
    audioProcessor = &owner;
    index = parameterIndex;
    const auto& control = parameters::controls[size_t(index)];
    title.setText(control.name, juce::dontSendNotification);
    title.setFont(font(12));
    title.setColour(juce::Label::textColourId, muted);
    title.setJustificationType(juce::Justification::centred);
    value.setFont(juce::Font(juce::FontOptions(12).withName("Menlo")));
    value.setJustificationType(juce::Justification::centred);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                               juce::MathConstants<float>::pi * 2.75f, true);
    slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    slider.setDoubleClickReturnValue(true, control.initial);
    slider.setTitle(control.name);
    slider.setTooltip(juce::String(control.name) + " — double-click to reset");
    addAndMakeVisible(title);
    addAndMakeVisible(slider);
    addAndMakeVisible(value);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.state, control.id, slider);
    refresh();
}

void ControlDial::refresh() { value.setText(audioProcessor->valueText(index), juce::dontSendNotification); }
void ControlDial::resized() {
    auto bounds = getLocalBounds();
    title.setBounds(bounds.removeFromTop(18));
    value.setBounds(bounds.removeFromBottom(20));
    slider.setBounds(bounds);
}

AvaAudioProcessorEditor::AvaAudioProcessorEditor(AvaAudioProcessor& owner)
    : AudioProcessorEditor(owner), audioProcessor(owner),
      keyboard(owner.keyboard, juce::MidiKeyboardComponent::horizontalKeyboard) {
    setLookAndFeel(&look);
    for (int i = 0; i < int(dials.size()); ++i) {
        dials[size_t(i)].connect(owner, i, i >= 9 ? amber : mint);
        addAndMakeVisible(dials[size_t(i)]);
    }
    for (auto* box : {&oscillatorA, &oscillatorB}) {
        box->addItemList(parameters::waveforms, 1);
        box->setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(box);
    }
    attachmentA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(owner.state, "oscillator", oscillatorA);
    attachmentB = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(owner.state, "oscillator_b", oscillatorB);
    for (auto* button : {&loadButton, &saveButton, &previewButton}) addAndMakeVisible(button);
    loadButton.onClick = [this] { choosePreset(false); };
    saveButton.onClick = [this] { choosePreset(true); };
    previewButton.onClick = [this] { audioProcessor.preview(); };
    previewButton.setColour(juce::TextButton::buttonColourId, mint);
    previewButton.setColour(juce::TextButton::textColourOffId, background);
    keyboard.setAvailableRange(21, 108);
    keyboard.setOctaveForMiddleC(4);
    keyboard.setKeyWidth(25);
    keyboard.setVelocity(1.0f, false);
    keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, text);
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, background);
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, mint.withAlpha(.7f));
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, mint.withAlpha(.3f));
    keyboard.setColour(juce::MidiKeyboardComponent::textLabelColourId, panel);
    addAndMakeVisible(keyboard);
    instrument.addItemList({"Bass", "Piano", "Guitar", "Trumpet", "Flute", "Strings", "Synth Lead", "Pad"}, 1);
    instrument.setSelectedId(4, juce::dontSendNotification);
    description.setTextToShowWhenEmpty("Describe the sound (optional)", muted);
    description.onReturnKey = [this] { generateText(); };
    generateButton.onClick = [this] {
        if (latentJob != nullptr) latentJob->signalThreadShouldExit();
        else generateText();
    };
    generateButton.setColour(juce::TextButton::buttonColourId, mint);
    generateButton.setColour(juce::TextButton::textColourOffId, background);
    addAndMakeVisible(instrument);
    addAndMakeVisible(description);
    addAndMakeVisible(generateButton);
    generationStatus.setColour(juce::Label::textColourId, mint);
    generationStatus.setFont(font(12));
    generationStatus.setMinimumHorizontalScale(.8f);
    addAndMakeVisible(generationStatus);
    setSize(1080, 880);
    timerCallback();
    startTimerHz(20);
}

AvaAudioProcessorEditor::~AvaAudioProcessorEditor() { stopTimer(); setLookAndFeel(nullptr); }

void AvaAudioProcessorEditor::resized() {
    loadButton.setBounds(770, 34, 78, 36);
    saveButton.setBounds(858, 34, 78, 36);
    previewButton.setBounds(946, 34, 110, 36);
    oscillatorA.setBounds(52, 155, 265, 30);
    oscillatorB.setBounds(351, 155, 265, 30);
    // Mix, detune, noise and drive form the source panel.
    const int source[] {1, 2, 3, 8};
    for (int i = 0; i < 4; ++i) dials[size_t(source[i])].setBounds(51 + i * 147, 252, 120, 105);
    dials[4].setBounds(712, 148, 130, 100);
    dials[5].setBounds(888, 148, 130, 100);
    dials[6].setBounds(712, 260, 130, 100);
    dials[7].setBounds(888, 260, 130, 100);
    for (int i = 0; i < 4; ++i) dials[size_t(9 + i)].setBounds(43 + i * 119, 445, 108, 133);
    dials[0].setBounds(819, 436, 151, 145);
    keyboard.setBounds(24, 742, 1032, 88);
    for (auto& dial : dials) dial.setTopLeftPosition(dial.getX(), dial.getY() + 120);
    oscillatorA.setTopLeftPosition(oscillatorA.getX(), oscillatorA.getY() + 120);
    oscillatorB.setTopLeftPosition(oscillatorB.getX(), oscillatorB.getY() + 120);
    instrument.setBounds(44, 140, 145, 30);
    description.setBounds(205, 140, 625, 30);
    generateButton.setBounds(850, 140, 180, 30);
    generationStatus.setBounds(44, 175, 986, 30);
}

void AvaAudioProcessorEditor::paintWave(juce::Graphics& g, juce::Rectangle<float> bounds,
                                       int waveform, juce::Colour colour) {
    g.setColour(background.withAlpha(.65f));
    g.fillRoundedRectangle(bounds, 5);
    ava::Oscillator oscillator;
    oscillator.start(ava::Waveform(std::clamp(waveform, 0, 3)), 375, 24000);
    juce::Path path;
    for (int i = 0; i < 128; ++i) {
        const float x = bounds.getX() + 7 + float(i) * (bounds.getWidth() - 14) / 127;
        const float y = bounds.getCentreY() - float(oscillator.nextSample()) * bounds.getHeight() * .30f;
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.setColour(colour.withAlpha(.8f));
    g.strokePath(path, juce::PathStrokeType(1.5f));
}

void AvaAudioProcessorEditor::paintEnvelope(juce::Graphics& g, juce::Rectangle<float> bounds) {
    const auto values = audioProcessor.parameterValues();
    const auto p = ava::decodeControls(parameters::synthControls(values), 440, 24000, ava::Waveform::sine, ava::Waveform::sine);
    const double held = 4.0; // Match the Preview button, including early note-off.
    const double duration = 6.0; // Fixed time axis: changing release must not zoom the graph.
    ava::Envelope envelope;
    const double rate = 256;
    const int samples = juce::roundToInt(duration * rate);
    envelope.start(p.attack, p.decay, p.sustain, p.release, rate);
    const int noteOffSample = juce::roundToInt(held * rate);
    juce::Path path;
    for (int i = 0; i <= samples; ++i) {
        if (i == noteOffSample) envelope.noteOff();
        const float x = bounds.getX() + float(i) * bounds.getWidth() / float(samples);
        const float y = bounds.getBottom() - float(envelope.nextValue()) * bounds.getHeight();
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.setColour(amber.withAlpha(.8f));
    g.strokePath(path, juce::PathStrokeType(2));
    path.lineTo(bounds.getBottomRight());
    path.lineTo(bounds.getBottomLeft());
    path.closeSubPath();
    g.setColour(amber.withAlpha(.06f));
    g.fillPath(path);
    const float noteOffX = bounds.getX() + float(noteOffSample) * bounds.getWidth() / float(samples);
    g.setColour(mint.withAlpha(.45f));
    g.drawVerticalLine(juce::roundToInt(noteOffX), bounds.getY(), bounds.getBottom());
    g.setColour(muted);
    g.setFont(font(10));
    g.drawText("NOTE OFF / 4 s", juce::Rectangle<float>(
        std::clamp(noteOffX - 40, bounds.getX(), bounds.getRight() - 85),
        bounds.getY() - 18, 85, 14), juce::Justification::centred);
}

void AvaAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(background);
    g.setColour(text);
    g.setFont(font(38, true));
    g.drawText("Ava", 24, 23, 110, 43, juce::Justification::left);
    g.setColour(mint);
    g.fillEllipse(92, 51, 6, 6);
    g.setColour(muted);
    g.setFont(font(11));
    g.drawText("TEXT TO SYNTH", 25, 69, 170, 15, juce::Justification::left);
    g.drawText("CURRENT PRESET", 213, 28, 440, 15, juce::Justification::left);
    g.setColour(text);
    g.setFont(font(20));
    g.drawFittedText(audioProcessor.presetName(), {212, 46, 310, 29}, juce::Justification::left, 1);
    g.setColour(panel);
    g.fillRoundedRectangle(24, 100, 1032, 110, 14);
    g.setColour(muted);
    g.setFont(font(11));
    g.drawText("INSTRUMENT", 44, 115, 145, 18, juce::Justification::left);
    g.drawText("DESIRED SOUND", 205, 115, 625, 18, juce::Justification::left);
    g.saveState();
    g.addTransform(juce::AffineTransform::translation(0, 120));
    card(g, {24, 100, 640, 270}, "01   SOURCE");
    card(g, {680, 100, 376, 270}, "02   FILTER + MOTION");
    card(g, {24, 386, 760, 216}, "03   AMPLITUDE ENVELOPE");
    card(g, {800, 386, 256, 216}, "04   OUTPUT");
    g.setFont(font(10, true));
    g.setColour(muted);
    g.drawText("OSCILLATOR A", 53, 136, 260, 15, juce::Justification::left);
    g.drawText("OSCILLATOR B", 352, 136, 260, 15, juce::Justification::left);
    paintWave(g, {52, 197, 265, 43}, oscillatorA.getSelectedId() - 1, mint);
    paintWave(g, {351, 197, 265, 43}, oscillatorB.getSelectedId() - 1, amber);
    g.setColour(edge);
    g.drawLine(704, 253, 1032, 253, 1);
    paintEnvelope(g, {538, 448, 220, 115});
    g.setColour(muted);
    g.setFont(font(10));
    g.drawText("0-6 s  /  NOTE OFF AT 4 s", 538, 573, 220, 14, juce::Justification::left);
    const float level = juce::jlimit(0.f, 1.f, (juce::Decibels::gainToDecibels(std::max(meter, .00001f)) + 60) / 60);
    g.setColour(background);
    g.fillRoundedRectangle(994, 444, 15, 124, 4);
    g.setColour(meter > 1 ? amber : mint);
    g.fillRoundedRectangle(997, 447 + 118 * (1 - level), 9, std::max(2.f, 118 * level), 3);
    g.setColour(muted);
    g.setFont(font(10));
    g.drawText(meter > 1 ? "HOT" : "PEAK", 975, 575, 52, 15, juce::Justification::centred);
    g.restoreState();
    g.setFont(font(12));
    g.drawFittedText(status, {24, 845, 760, 20}, juce::Justification::left, 1);
    const auto note = juce::MidiMessage::getMidiNoteName(juce::roundToInt(audioProcessor.previewNote()), true, true, 4);
    g.drawText("PREVIEW " + note + "   /   8 VOICES", 800, 845, 256, 20, juce::Justification::right);
}

void AvaAudioProcessorEditor::timerCallback() {
    if (latentJob != nullptr) {
        status = latentJob->progress();
        PresetFile preset;
        juce::String error;
        if (latentJob->takeResult(preset, error)) {
            if (error.isEmpty()) {
                audioProcessor.applyPreset(preset);
                status = "Preset ready. Press Preview, or play a note.";
            } else status = error;
            latentJob.reset();
        }
    }
    generationStatus.setText(status, juce::dontSendNotification);
    generationStatus.setTooltip(status);
    for (auto* button : {&loadButton, &saveButton})
        button->setEnabled(latentJob == nullptr);
    generateButton.setButtonText(latentJob != nullptr ? "Cancel" : "Generate");
    for (auto& dial : dials) dial.refresh();
    meter = std::max(audioProcessor.outputPeak.load(), meter * .85f);
    previewButton.setButtonText(audioProcessor.isPreviewing() ? "Stop" : "Preview");
    if (lastName != audioProcessor.presetName()) {
        lastName = audioProcessor.presetName();
        description.setText(audioProcessor.state.state.getProperty("description").toString(), false);
        for (int i = 0; i < instrument.getNumItems(); ++i)
            if (lastName.startsWith(instrument.getItemText(i) + " /"))
                instrument.setSelectedItemIndex(i, juce::dontSendNotification);
        keyboard.setLowestVisibleKey(std::clamp(juce::roundToInt(audioProcessor.previewNote()) - 12, 21, 84));
    }
    repaint();
}

void AvaAudioProcessorEditor::choosePreset(bool save) {
    chooser = std::make_unique<juce::FileChooser>(save ? "Save preset" : "Load preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
    const int flags = juce::FileBrowserComponent::canSelectFiles
        | (save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                : juce::FileBrowserComponent::openMode);
    chooser->launchAsync(flags, [safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(this), save](const juce::FileChooser& dialog) {
        if (safe == nullptr || dialog.getResult() == juce::File{}) return;
        if (save) {
            const auto result = safe->audioProcessor.savePreset(dialog.getResult().withFileExtension("ava.json"));
            safe->status = result.wasOk() ? "Preset saved." : result.getErrorMessage();
        } else safe->loadPreset(dialog.getResult());
    });
}

void AvaAudioProcessorEditor::loadPreset(const juce::File& file) {
    const auto result = audioProcessor.loadPreset(file);
    status = result.wasOk() ? "Preset loaded. Press Preview, or play a note." : result.getErrorMessage();
}

bool AvaAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& file : files)
        if (file.endsWithIgnoreCase(".json")) return true;
    return false;
}
void AvaAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int) {
    if (latentJob != nullptr) { status = "Cancel generation before loading another preset."; return; }
    for (const auto& file : files) {
        if (file.endsWithIgnoreCase(".json")) { loadPreset(juce::File(file)); return; }
    }
}

void AvaAudioProcessorEditor::generateText() {
    if (latentJob != nullptr) return;
    // Prompt pitch is an instrument reference; played pitch comes from MIDI.
    const int defaultPitches[] {40, 60, 55, 60, 72, 60, 72, 60};
    const int midi = defaultPitches[instrument.getSelectedItemIndex()];
    const auto mapper = bundledMapper();
    const auto generator = installedGenerator();
    if (!mapper.getChildFile("mapper.onnx").existsAsFile() || !mapper.getChildFile("mapper.json").existsAsFile()) {
        status = "Mapper missing from this build. Rebuild and reinstall Ava.";
        timerCallback();
        return;
    }
    if (!generator.getChildFile("manifest.json").existsAsFile()
        || !generator.getChildFile("denoiser.onnx").existsAsFile()) {
        status = "Native generator not installed. Run Ava's scripts/install_au.sh, then reopen the plugin.";
        timerCallback();
        return;
    }
    TextRequest request;
    request.generatorDirectory = generator;
    request.instrument = instrument.getText();
    request.description = description.getText().trim();
    request.midi = midi;
    request.seed = juce::Random::getSystemRandom().nextInt(1000000000);
    latentJob = std::make_unique<LatentJob>(mapper, request);
    status = "Starting text generation...";
    if (!latentJob->startThread()) { latentJob.reset(); status = "Could not start generation worker."; }
    timerCallback();
}
