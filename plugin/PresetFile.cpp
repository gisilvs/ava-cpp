#include "PresetFile.h"
#include "HostRenderer.h"

namespace {
bool isFiniteNumber(const juce::var& value) {
    return (value.isInt() || value.isInt64() || value.isDouble()) && std::isfinite(double(value));
}
} // namespace

juce::Result PresetFile::read(const juce::File& file, PresetFile& destination) {
    juce::var json;
    const auto result = juce::JSON::parse(file.loadFileAsString(), json);
    if (result.failed())
        return result;
    // Read only the supported preset version and the Python-compatible mapping.
    if (!json.isObject() || json["format"].toString() != "ava-preset"
        || !json["version"].isInt() || int(json["version"]) != 1
        || json["synth_version"].toString() != ava::synthVersion
        || json["control_mapping"].toString() != ava::controlMapping
        || !isFiniteNumber(json["sample_rate"]) || !juce::exactlyEqual(double(json["sample_rate"]), HostRenderer::synthRate))
        return juce::Result::fail("This preset format is not supported.");
    auto* controls = json["controls"].getDynamicObject();
    if (controls == nullptr || controls->getProperties().size() != int(parameters::controls.size()))
        return juce::Result::fail("The preset must contain all 13 synth controls.");

    PresetFile preset;
    preset.name = json["name"].toString().trim();
    if (preset.name.isEmpty())
        preset.name = file.getFileNameWithoutExtension();
    preset.description = json["description"].toString();
    if (!isFiniteNumber(json["preview_midi"]))
        return juce::Result::fail("The preview note is missing.");
    preset.previewMidi = double(json["preview_midi"]);
    // Presets store the full normalized range. Playable knob limits are applied
    // by the plugin when loading, not by changing the file format here.
    for (size_t i = 0; i < parameters::controls.size(); ++i) {
        const auto value = controls->getProperty(parameters::controls[i].id);
        if (!isFiniteNumber(value) || double(value) < 0 || double(value) > 1)
            return juce::Result::fail("Invalid control: " + juce::String(parameters::controls[i].id));
        preset.values[i] = float(double(value));
    }
    // The last two values are integer oscillator IDs, in the same order as Python.
    for (int i = 0; i < 2; ++i) {
        const auto value = json[i == 0 ? "oscillator" : "oscillator_b"];
        if (!value.isInt() || int(value) < 0 || int(value) > 3)
            return juce::Result::fail("Unknown oscillator selection.");
        preset.values[size_t(13 + i)] = float(int(value));
    }
    try {
        ava::decodeControls(parameters::synthControls(preset.values), ava::midiFrequency(preset.previewMidi),
            HostRenderer::synthRate, ava::Waveform(int(preset.values[13])), ava::Waveform(int(preset.values[14])));
    } catch (const std::exception& error) {
        return juce::Result::fail(error.what());
    }
    destination = preset; // No partially applied presets after a validation failure.
    return juce::Result::ok();
}

juce::Result PresetFile::write(const juce::File& file) const {
    auto* json = new juce::DynamicObject();
    json->setProperty("format", "ava-preset");
    json->setProperty("version", 1);
    json->setProperty("synth_version", ava::synthVersion);
    json->setProperty("control_mapping", ava::controlMapping);
    json->setProperty("sample_rate", HostRenderer::synthRate);
    json->setProperty("name", name);
    json->setProperty("description", description);
    json->setProperty("preview_midi", previewMidi);
    json->setProperty("oscillator", int(values[13]));
    json->setProperty("oscillator_b", int(values[14]));
    auto* controls = new juce::DynamicObject();
    for (size_t i = 0; i < parameters::controls.size(); ++i)
        controls->setProperty(parameters::controls[i].id, values[i]);
    json->setProperty("controls", juce::var(controls));
    return file.replaceWithText(juce::JSON::toString(juce::var(json), false))
        ? juce::Result::ok() : juce::Result::fail("Could not save the preset.");
}
