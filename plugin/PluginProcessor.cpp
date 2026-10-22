#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <limits>

namespace {
// MIDI keys use 0–127, so 128 identifies the separate four-second preview note.
constexpr int previewNoteId = 128;
constexpr double previewHoldSeconds = 4.0;

juce::String formatDuration(double seconds) {
    if (seconds < 1)
        return juce::String(seconds * 1000, 1) + " ms";
    return juce::String(seconds, 2) + " s";
}

bool validHostParameters(const juce::ValueTree& restored) {
    for (size_t i = 0; i < parameters::controls.size() + 2; ++i) {
        const char* id = parameters::id(i);
        const auto node = restored.getChildWithProperty("id", id);
        if (!node.isValid())
            return false;
        const auto value = node.getProperty("value");
        const double limit = i < 13 ? 1.0 : 3.0;
        const auto parsed = juce::JSON::fromString(value.toString());
        if (!(parsed.isDouble() || parsed.isInt() || parsed.isInt64())
            || !std::isfinite(double(parsed)) || double(parsed) < 0 || double(parsed) > limit
            || (i >= 13 && double(parsed) != std::floor(double(parsed))))
            return false;
    }
    return true;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout AvaAudioProcessor::parameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& control : parameters::controls)
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(control.id, 1),
            control.name, juce::NormalisableRange<float>(control.minimum, control.maximum), control.initial));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("oscillator", 1),
        "Oscillator A", parameters::waveforms, 2));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("oscillator_b", 1),
        "Oscillator B", parameters::waveforms, 3));
    return layout;
}

AvaAudioProcessor::AvaAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "AvaParametersV1", parameterLayout()) {
    for (size_t i = 0; i < rawParameters.size(); ++i)
        rawParameters[i] = state.getRawParameterValue(parameters::id(i));
    previousValues.fill(std::numeric_limits<float>::quiet_NaN());
    state.state.setProperty("preset_name", "Init", nullptr);
    state.state.setProperty("preview_midi", 60.0, nullptr);
}

std::array<float, 15> AvaAudioProcessor::parameterValues() const {
    std::array<float, 15> values;
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = rawParameters[i]->load(std::memory_order_relaxed);
    for (size_t i = 0; i < parameters::controls.size(); ++i) {
        const auto& control = parameters::controls[i];
        values[i] = std::clamp(values[i], control.minimum, control.maximum);
    }
    return values;
}

void AvaAudioProcessor::applyParameters() {
    const auto values = parameterValues();
    const double reference = previewMidi.load();
    // Compare exact host values so even small automation changes reach the DSP.
    if (values == previousValues && reference == previousPreviewMidi)
        return;
    previousPreviewMidi = reference;
    // Decode once at the preset's reference note. Keep octave offsets when playing
    // other notes, so cutoff and its LFO follow the keyboard together.
    renderer.synth.setPreset(ava::decodeControls(parameters::synthControls(values),
        ava::midiFrequency(previewMidi.load()), HostRenderer::synthRate,
        ava::Waveform(int(values[13])), ava::Waveform(int(values[14]))));
    previousValues = values;
}

void AvaAudioProcessor::prepareToPlay(double rate, int maxBlock) {
    canRender = rate >= HostRenderer::synthRate;
    hostSampleRate = rate;
    if (!canRender) {
        setLatencySamples(0);
        return;
    }
    renderer.prepare(rate, std::max(1, maxBlock));
    setLatencySamples(renderer.latencySamples());
    previousValues.fill(std::numeric_limits<float>::quiet_NaN());
    applyParameters();
    previewSamplesRemaining = 0;
    previewRequest.store(0);
    previewActive.store(false);
}

void AvaAudioProcessor::reset() {
    renderer.reset();
    keyboard.reset();
    previewSamplesRemaining = 0;
    previewActive.store(false);
    outputPeak.store(0);
}

bool AvaAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AvaAudioProcessor::handleMidi(const juce::MidiMessage& message) {
    if (message.isNoteOn()) {
        renderer.synth.noteOn(message.getNoteNumber(), message.getFloatVelocity());
    } else if (message.isNoteOff()) {
        renderer.synth.noteOff(message.getNoteNumber());
    } else if (message.isSustainPedalOn()) {
        renderer.synth.setSustainPedal(true);
    } else if (message.isSustainPedalOff()) {
        renderer.synth.setSustainPedal(false);
    } else if (message.isAllSoundOff()) {
        reset();
    } else if (message.isAllNotesOff()) {
        renderer.synth.allNotesOff();
        previewSamplesRemaining = 0;
        previewActive.store(false);
    } else if (message.isPitchWheel()) {
        renderer.synth.setPitchBend(2.0 * (message.getPitchWheelValue() - 8192) / 8192.0);
    }
}

void AvaAudioProcessor::render(juce::AudioBuffer<float>& buffer, int offset, int count) {
    while (count > 0) {
        const int length = previewSamplesRemaining > 0 ? std::min(count, previewSamplesRemaining) : count;
        renderer.render(buffer.getWritePointer(0, offset), length);
        if (previewSamplesRemaining > 0) {
            previewSamplesRemaining -= length;
            if (previewSamplesRemaining == 0) {
                renderer.synth.noteOff(previewNoteId);
                previewActive.store(false);
            }
        }
        offset += length;
        count -= length;
    }
}

void AvaAudioProcessor::handlePreviewRequest() {
    const int request = previewRequest.exchange(0);
    if (request == 1) {
        renderer.reset();
        renderer.synth.noteOnFrequency(previewNoteId, ava::midiFrequency(previewMidi.load()), 1.0);
        previewSamplesRemaining = juce::roundToInt(previewHoldSeconds * hostSampleRate);
        previewActive.store(true);
    } else if (request == -1) {
        renderer.synth.noteOff(previewNoteId);
        previewSamplesRemaining = 0;
        previewActive.store(false);
    }
}

void AvaAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (!canRender || buffer.getNumSamples() == 0)
        return;
    // Apply UI requests at a block boundary, before processing this block's MIDI.
    if (resetRequested.exchange(false))
        reset();
    applyParameters();
    handlePreviewRequest();

    keyboard.processNextMidiBuffer(midi, 0, buffer.getNumSamples(), true);
    // Render up to each event, then apply it at its sample position.
    int offset = 0;
    for (const auto event : midi) {
        const int position = std::clamp(event.samplePosition, offset, buffer.getNumSamples());
        render(buffer, offset, position - offset);
        handleMidi(event.getMessage());
        offset = position;
    }
    render(buffer, offset, buffer.getNumSamples() - offset);
    for (int channel = 1; channel < buffer.getNumChannels(); ++channel)
        buffer.copyFrom(channel, 0, buffer, 0, 0, buffer.getNumSamples());
    outputPeak.store(buffer.getMagnitude(0, 0, buffer.getNumSamples()), std::memory_order_relaxed);
    midi.clear();
}

juce::String AvaAudioProcessor::presetName() const {
    return state.state.getProperty("preset_name", "Init").toString();
}

juce::Result AvaAudioProcessor::loadPreset(const juce::File& file) {
    PresetFile preset;
    const auto result = PresetFile::read(file, preset);
    if (result.failed())
        return result;
    applyPreset(preset);
    return juce::Result::ok();
}

void AvaAudioProcessor::applyPreset(const PresetFile& preset) {
    for (size_t i = 0; i < preset.values.size(); ++i) {
        auto* parameter = state.getParameter(parameters::id(i));
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(preset.values[i]));
        parameter->endChangeGesture();
    }
    state.state.setProperty("preset_name", preset.name, nullptr);
    state.state.setProperty("description", preset.description, nullptr);
    state.state.setProperty("preview_midi", preset.previewMidi, nullptr);
    previewMidi.store(preset.previewMidi);
    resetRequested.store(true);
}

juce::Result AvaAudioProcessor::savePreset(const juce::File& file) const {
    PresetFile preset;
    preset.name = presetName();
    preset.description = state.state.getProperty("description").toString();
    preset.previewMidi = previewMidi.load();
    preset.values = parameterValues();
    return preset.write(file);
}

void AvaAudioProcessor::getStateInformation(juce::MemoryBlock& data) {
    auto snapshot = state.copyState();
    snapshot.setProperty("preview_midi", previewMidi.load(), nullptr);
    copyXmlToBinary(*snapshot.createXml(), data);
}

void AvaAudioProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr || !xml->hasTagName("AvaParametersV1"))
        return;
    auto restored = juce::ValueTree::fromXml(*xml);
    // Validate every value before replacing the host's current state.
    if (!validHostParameters(restored))
        return;
    const auto parsedNote = juce::JSON::fromString(restored.getProperty("preview_midi", 60.0).toString());
    if (!(parsedNote.isDouble() || parsedNote.isInt() || parsedNote.isInt64()))
        return;
    const double note = parsedNote;
    if (!std::isfinite(note) || note < 0 || note > 127)
        return;
    const double detune = double(restored.getChildWithProperty("id", "detune_cents").getProperty("value")) * 30;
    if (ava::midiFrequency(note) * std::exp2(detune / 1200) >= HostRenderer::synthRate / 2)
        return;
    state.replaceState(restored);
    previewMidi.store(note);
    resetRequested.store(true);
}

double AvaAudioProcessor::getTailLengthSeconds() const {
    const auto releaseControl = double(parameterValues()[12]);
    return .1 * std::max(releaseControl, double(std::numeric_limits<float>::epsilon()))
        / std::max(1 - releaseControl, double(std::numeric_limits<float>::epsilon())) + .005;
}

juce::String AvaAudioProcessor::valueText(int index) const {
    const auto preset = ava::decodeControls(parameters::synthControls(parameterValues()), ava::midiFrequency(previewMidi.load()),
        HostRenderer::synthRate, ava::Waveform::sine, ava::Waveform::sine);
    switch (index) {
        case 0: return preset.gain <= 0 ? "-inf dB" : juce::String(20 * std::log10(preset.gain), 1) + " dB";
        case 1: return juce::String(preset.mix * 100, 1) + "% B";
        case 2: return juce::String(preset.detuneCents, 1) + " ct";
        case 3: return juce::String(preset.noiseMix * 100, 1) + "%";
        case 4: return (preset.cutoffOctaves >= 0 ? "+" : "") + juce::String(preset.cutoffOctaves, 2) + " oct";
        case 5: return "Q " + juce::String(preset.q, 2);
        case 6: return juce::String(preset.lfoRate, 2) + " Hz";
        case 7: return juce::String(preset.lfoDepth, 2) + " oct";
        case 8: return juce::String(preset.drive, 2);
        case 9: return formatDuration(preset.attack);
        case 10: return formatDuration(preset.decay);
        case 11: return juce::String(preset.sustain * 100, 1) + "%";
        case 12: return formatDuration(preset.release);
        default: return {};
    }
}

juce::AudioProcessorEditor* AvaAudioProcessor::createEditor() { return new AvaAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AvaAudioProcessor(); }
