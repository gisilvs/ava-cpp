#include "plugin/PluginProcessor.h"
#include "inference/LatentMapper.h"
#include "inference/LatentJob.h"
#include "inference/TextTokenizer.h"
#include "inference/TextGenerator.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

std::vector<float> render(AvaAudioProcessor& processor, double rate, int blockSize,
                          int total = 16000, bool midiNotes = true) {
    processor.prepareToPlay(rate, 128); // Also exercise blocks larger than the prepared size.
    std::vector<float> result(size_t(total), 0);
    for (int offset = 0; offset < total; offset += blockSize) {
        const int count = std::min(blockSize, total - offset);
        juce::AudioBuffer<float> buffer(2, count);
        juce::MidiBuffer midi;
        if (midiNotes) {
            if (offset <= 37 && 37 < offset + count)
                midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 37 - offset);
            if (offset <= 1007 && 1007 < offset + count)
                midi.addEvent(juce::MidiMessage::noteOff(1, 69), 1007 - offset);
        }
        processor.processBlock(buffer, midi);
        for (int i = 0; i < count; ++i) {
            const auto value = buffer.getSample(0, i);
            require(std::isfinite(value), "Non-finite plugin output");
            require(value == buffer.getSample(1, i), "Mono must be duplicated without panning/gain changes");
            result[size_t(offset + i)] = value;
        }
    }
    return result;
}

void testRendering() {
    for (double rate : {24000., 44100., 48000., 96000.}) {
        AvaAudioProcessor a, b;
        auto one = render(a, rate, 73), other = render(b, rate, 257);
        require(one == other, "Host block size changed the render");
        require(a.getLatencySamples() == (rate == 24000 ? 0 : juce::roundToInt(100 * rate / 24000)), "Reported latency");
        require(*std::max_element(one.begin(), one.end()) > .001f, "Plugin is silent");
        if (rate == 24000) {
            ava::Engine engine(24000, 24000);
            const auto values = a.parameterValues();
            engine.setPreset(ava::decodeControls(parameters::synthControls(values), ava::midiFrequency(a.previewNote()), 24000, ava::Waveform(int(values[13])), ava::Waveform(int(values[14]))));
            std::vector<float> expected(one.size());
            engine.render(expected.data(), 37);
            engine.noteOn(69, 1);
            engine.render(expected.data() + 37, 970);
            engine.noteOff(69);
            engine.render(expected.data() + 1007, int(expected.size()) - 1007);
            require(one == expected, "24 kHz plugin differs from portable engine");
        }
    }
    AvaAudioProcessor processor;
    processor.prepareToPlay(24000, 256);
    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
    processor.processBlock(buffer, midi);
    processor.state.getParameter("gain")->setValueNotifyingHost(0);
    processor.processBlock(buffer, midi);
    require(buffer.getMagnitude(0, 0, 256) == 0, "Gain automation did not reach a playing voice");
    // Out-of-range high notes and pitch bend must not throw in the audio callback.
    midi.addEvent(juce::MidiMessage::noteOn(1, 127, 1.0f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 125, 1.0f), 1);
    midi.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 2);
    processor.processBlock(buffer, midi);
}

void testControlLimits() {
    AvaAudioProcessor processor;
    for (const char* id : {"gain", "attack", "decay", "release"})
        processor.state.getParameter(id)->setValueNotifyingHost(1.f);
    auto p = ava::decodeControls(parameters::synthControls(processor.parameterValues()),
        440, 24000, ava::Waveform::sine, ava::Waveform::sine);
    require(std::abs(p.gain - 1) < 1e-5, "Gain ceiling must be unity");
    require(std::abs(p.attack - 5) < 1e-4, "Attack ceiling must be 5 seconds");
    require(std::abs(p.decay - 10) < 1e-4 && std::abs(p.release - 10) < 1e-4,
            "Decay/release ceiling must be 10 seconds");
    require(processor.getTailLengthSeconds() < 10.01, "Reported tail exceeded release ceiling");
    for (const char* id : {"attack", "decay", "release"})
        processor.state.getParameter(id)->setValueNotifyingHost(0.f);
    p = ava::decodeControls(parameters::synthControls(processor.parameterValues()),
        440, 24000, ava::Waveform::sine, ava::Waveform::sine);
    require(std::abs(p.attack - .001) < 1e-8 && std::abs(p.decay - .001) < 1e-8
        && std::abs(p.release - .001) < 1e-8, "Minimum envelope times must be 1 ms");
}

void testKeyTracking() {
    ava::Preset preset;
    preset.filterEnabled = preset.lfoEnabled = true;
    preset.cutoffOctaves = 2;
    preset.lfoDepth = 1;
    preset.lfoRate = 3;
    for (int note : {21, 60, 72, 108}) {
        const double frequency = ava::midiFrequency(note);
        ava::Engine engine(24000, 24000);
        engine.setPreset(preset);
        engine.noteOn(note);
        auto expectedPreset = preset;
        expectedPreset.cutoffOctaves = std::clamp(preset.cutoffOctaves,
            std::log2(20 / frequency), std::log2(10000 / frequency));
        expectedPreset.lfoDepth = std::min(preset.lfoDepth,
            ava::maxLfoDepth(expectedPreset.cutoffOctaves, frequency, 24000));
        ava::Voice voice;
        voice.start(expectedPreset, frequency, 1, 24000, 1);
        std::vector<float> actual(2400), expected(2400);
        engine.render(actual.data(), int(actual.size()));
        voice.render(expected.data(), int(expected.size()));
        require(actual == expected, "Cutoff/LFO did not retain pitch-relative octave offsets");
    }
}

void testPresetsAndState() {
    auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("ava-plugin-test", {}, true);
    require(directory.createDirectory().wasOk(), "Could not create test directory");
    const auto file = directory.getChildFile("test.ava.json");
    AvaAudioProcessor a;
    a.state.getParameter("drive")->setValueNotifyingHost(.23f);
    a.state.getParameter("oscillator")->setValueNotifyingHost(1.f);
    a.state.getParameter("oscillator_b")->setValueNotifyingHost(1.f / 3);
    auto original = PresetFile{};
    original.values = a.parameterValues();
    original.name = "Guitar / round trip";
    original.description = "Warm, plucked; a player's preset.";
    original.previewMidi = 55;
    a.applyPreset(original);
    require(a.savePreset(file).wasOk(), "Preset write failed");
    PresetFile saved;
    require(PresetFile::read(file, saved).wasOk(), "Saved preset is invalid");
    require(saved.name == original.name && saved.description == original.description
        && saved.previewMidi == original.previewMidi, "Preset metadata did not round-trip");
    AvaAudioProcessor b;
    require(b.loadPreset(file).wasOk(), "Preset read failed");
    require(a.parameterValues() == b.parameterValues(), "Preset round-trip changed controls");
    juce::MemoryBlock state;
    a.getStateInformation(state);
    AvaAudioProcessor c;
    c.setStateInformation(state.getData(), int(state.getSize()));
    require(a.parameterValues() == c.parameterValues(), "Host state did not restore controls");
    require(c.presetName() == original.name && c.previewNote() == original.previewMidi
        && c.state.state.getProperty("description").toString() == original.description,
        "Host state did not restore preset name, prompt or preview pitch");
    require(render(a, 24000, 73) == render(b, 24000, 73), "Preset reload changed the sound");
    require(render(b, 24000, 73) == render(c, 24000, 73), "Host-state recall changed the sound");
    auto json = juce::JSON::parse(file);
    json.getDynamicObject()->setProperty("version", 99);
    file.replaceWithText(juce::JSON::toString(json));
    const auto before = b.parameterValues();
    require(b.loadPreset(file).failed(), "Unknown preset version accepted");
    require(b.parameterValues() == before, "Bad preset changed controls");
    json.getDynamicObject()->setProperty("version", 1);
    json["controls"].getDynamicObject()->setProperty("gain", 1.1);
    file.replaceWithText(juce::JSON::toString(json));
    require(b.loadPreset(file).failed(), "Out-of-range control accepted");
    directory.deleteRecursively();
}

void testSustain() {
    ava::Engine engine(24000, 24000);
    ava::Controls controls;
    controls.release = .01f;
    engine.setControls(controls, ava::Waveform::sine, ava::Waveform::sine);
    engine.noteOn(69);
    engine.setSustainPedal(true);
    engine.noteOff(69);
    std::vector<float> buffer(2400);
    engine.render(buffer.data(), int(buffer.size()));
    require(*std::max_element(buffer.begin(), buffer.end()) > .1f, "Sustain pedal failed");
    engine.setSustainPedal(false);
    engine.render(buffer.data(), int(buffer.size()));
    require(buffer.back() == 0, "Sustain pedal release failed");
}

juce::File argumentFile(const char* argument) { return juce::File::getCurrentWorkingDirectory().getChildFile(argument); }

void inferPreset(char** argv) {
    const auto directory = argumentFile(argv[2]), latent = argumentFile(argv[3]);
    PresetFile expected;
    require(PresetFile::read(argumentFile(argv[5]), expected).wasOk(), "Cannot read expected Python preset");
    LatentJob job(directory, latent, juce::String(argv[4]).getDoubleValue());
    require(job.startThread(), "Could not start native inference worker");
    // Audio keeps rendering the current preset; results are applied explicitly later.
    AvaAudioProcessor processor, baseline;
    const auto before = processor.parameterValues();
    require(render(processor, 24000, 73) == render(baseline, 24000, 73),
            "Background inference changed audio before preset handoff");
    require(processor.parameterValues() == before, "Worker changed parameters directly");
    require(job.waitForThreadToExit(10000), "Native inference did not finish");
    PresetFile predicted;
    juce::String error;
    require(job.takeResult(predicted, error), "Worker lost its result");
    require(error.isEmpty(), error.toRawUTF8());
    double maximum = 0;
    for (size_t i = 0; i < 13; ++i)
        maximum = std::max(maximum, std::abs(double(predicted.values[i] - expected.values[i])));
    require(maximum < 1e-5, "Native controls differ from Python ONNX Runtime");
    require(predicted.values[13] == expected.values[13] && predicted.values[14] == expected.values[14],
            "Native oscillator selection differs from Python");
    processor.applyPreset(predicted);
    AvaAudioProcessor reference;
    reference.applyPreset(expected);
    const auto actualAudio = render(processor, 24000, 73), expectedAudio = render(reference, 24000, 73);
    float audioError = 0;
    for (size_t i = 0; i < actualAudio.size(); ++i)
        audioError = std::max(audioError, std::abs(actualAudio[i] - expectedAudio[i]));
    require(audioError < 1e-4f, "Preset handoff differs from Python preset audio");
    require(predicted.write(argumentFile(argv[6])).wasOk(), "Could not save native preset");
    std::cout << "Native worker controls max error " << maximum << "; audio max error " << audioError << "\n";
}

void textPreset(char** argv) {
    const auto saved = juce::JSON::parse(argumentFile(argv[4]));
    TextRequest request;
    request.generatorDirectory = argumentFile(argv[2]);
    request.outputDirectory = argumentFile(argv[4]).getParentDirectory();
    request.instrument = saved["instrument"].toString();
    request.description = saved["description"].toString();
    request.midi = int(saved["midi"]);
    request.seed = int(saved["seed"]);
    LatentJob job(argumentFile(argv[3]), request);
    require(job.startThread(), "Could not start text worker");
    AvaAudioProcessor processor;
    const auto before = processor.parameterValues();
    render(processor, 24000, 73);
    require(processor.parameterValues() == before, "Text worker changed current parameters");
    require(job.waitForThreadToExit(60000), "Text worker did not finish within test timeout");
    PresetFile predicted;
    juce::String error;
    require(job.takeResult(predicted, error), "Text worker lost result");
    require(error.isEmpty(), error.toRawUTF8());
    require(std::abs(predicted.previewMidi - request.midi) < 1e-9, "Deployment must use requested pitch");
    processor.applyPreset(predicted);
    require(predicted.write(argumentFile(argv[5])).wasOk(), "Could not save text preset");
    std::cout << "Text worker finished; requested MIDI " << predicted.previewMidi << "\n";
}

void renderPreset(int argc, char** argv) {
    AvaAudioProcessor processor;
    const auto preset = argumentFile(argv[2]);
    auto status = processor.loadPreset(preset);
    require(status.wasOk(), status.getErrorMessage().toRawUTF8());
    const double rate = argc > 4 ? juce::String(argv[4]).getDoubleValue() : 24000.0;
    processor.prepareToPlay(rate, 257);
    processor.preview();
    const auto output = argumentFile(argv[3]);
    output.getParentDirectory().createDirectory();
    output.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = output.createOutputStream();
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(1)
        .withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = juce::WavAudioFormat{}.createWriterFor(stream, options);
    require(writer != nullptr, "Could not open WAV output");
    for (int offset = 0; offset < int(rate * 6); offset += 257) {
        const int count = std::min(257, int(rate * 6) - offset);
        juce::AudioBuffer<float> buffer(2, count);
        juce::MidiBuffer midi;
        processor.processBlock(buffer, midi);
        require(writer->writeFromAudioSampleBuffer(buffer, 0, count), "Could not write WAV");
    }
    std::cout << "Rendered " << preset.getFileName() << " at " << rate << " Hz\n";
}
} // namespace

int runPluginChecks(int argc, char** argv);
int runPluginChecks(int argc, char** argv) {
    try {
        juce::ScopedJuceInitialiser_GUI initialise;
        if (argc == 5 && juce::String(argv[1]) == "--native-cancel") {
            TextRequest request;
            request.generatorDirectory = argumentFile(argv[2]);
            request.outputDirectory = argumentFile(argv[4]);
            request.instrument = "Trumpet";
            request.description = "native cancellation check";
            LatentJob job(argumentFile(argv[3]), request);
            AvaAudioProcessor processor, reference;
            const auto before = processor.parameterValues();
            require(job.startThread(), "Native cancellation worker did not start");
            bool encoding = false;
            for (int i = 0; i < 1500; ++i) {
                if (job.progress().contains("Encoding")) { encoding = true; break; }
                if (!job.isThreadRunning()) break;
                juce::Thread::sleep(10);
            }
            job.signalThreadShouldExit();
            require(job.waitForThreadToExit(10000), "Native cancellation did not finish");
            require(encoding, "Native cancellation test never reached encoding");
            PresetFile preset;
            juce::String error;
            require(job.takeResult(preset, error) && error.containsIgnoreCase("cancelled"), "Cancelled native request returned success");
            require(processor.parameterValues() == before, "Cancellation replaced current controls");
            require(render(processor, 24000, 73) == render(reference, 24000, 73), "Cancellation changed current sound");
            std::cout << "Native cancellation retained the current preset and audio\n";
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--native-repeat") {
            const auto root = argumentFile(argv[2]);
            const auto folder = argumentFile(argv[3]);
            require(folder.createDirectory().wasOk(), "Could not create repeat-test directory");
            std::vector<float> previous;
            for (int i = 0; i < 2; ++i) {
                const auto output = TextGenerator::generate(root,
                    TextGenerator::prompt("Trumpet", "dark, mellow, smooth", 60), 123+i,
                    [] { return false; }, [](int) {});
                require(i == 0 || output.latent != previous, "New seed reused previous generated latent");
                TextGenerator::saveLatent(folder.getChildFile(juce::String(i) + ".npy"), output.latent);
                previous = output.latent;
                std::cout << "Repeated native generation " << i << " completed\n" << std::flush;
                juce::Thread::sleep(500); // Allow the RSS monitor to sample after model teardown.
            }
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--native-generate") {
            const auto root = argumentFile(argv[2]);
            juce::MemoryBlock bytes;
            require(root.getChildFile("noises.raw").loadFileAsData(bytes), "Noise fixture missing");
            require(bytes.getSize() == 9 * 256 * 66 * sizeof(float), "Noise fixture has wrong size");
            std::vector<float> noise(9 * 256 * 66);
            std::memcpy(noise.data(), bytes.getData(), bytes.getSize());
            const auto fixtures = juce::JSON::parse(root.getChildFile("text_fixtures.json"));
            const auto text = fixtures[0]["prompt"].toString();
            require(TextGenerator::prompt("Trumpet", "dark, mellow, smooth", 60) == text,
                    "Native prompt differs from training wrapper");
            const auto output = TextGenerator::generate(root, text, 123, [] { return false; }, [](int) {}, &noise);
            TextGenerator::saveLatent(argumentFile(argv[3]), output.latent);
            std::cout << "Native generation setup " << output.setupSeconds << "s; text " << output.textSeconds << "s; denoise " << output.denoiseSeconds << "s\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--prompts") {
            const auto fixtures = juce::JSON::parse(argumentFile(argv[2]));
            require(fixtures.isArray() && fixtures.size() > 0, "Prompt fixtures missing");
            for (const auto& fixture : *fixtures.getArray()) {
                const auto actual = TextGenerator::prompt(fixture["instrument"].toString(),
                                                         fixture["description"].toString(),
                                                         int(fixture["midi"]));
                require(actual == fixture["prompt"].toString(), "Python/C++ prompt mismatch");
            }
            std::cout << "Native prompts match all " << fixtures.size() << " Python fixtures\n";
            return 0;
        }
        if (argc == 4 && juce::String(argv[1]) == "--tokenize") {
            TextTokenizer tokenizer(argumentFile(argv[2]));
            const auto fixtures = juce::JSON::parse(argumentFile(argv[3]));
            require(fixtures.isArray(), "Tokenizer fixtures missing");
            for (const auto& fixture : *fixtures.getArray()) {
                const auto actual = tokenizer.encode(fixture["text"].toString());
                for (size_t i = 0; i < actual.ids.size(); ++i) {
                    require(actual.ids[i] == int64_t(fixture["ids"][int(i)]), "Tokenizer ID mismatch");
                    require(actual.mask[i] == int64_t(fixture["mask"][int(i)]), "Tokenizer mask mismatch");
                }
            }
            std::cout << "Native tokenizer matches all " << fixtures.size() << " fixtures\n";
            return 0;
        }
        if (argc == 6 && juce::String(argv[1]) == "--text") { textPreset(argv); return 0; }
        if (argc == 7 && juce::String(argv[1]) == "--infer") { inferPreset(argv); return 0; }
        if (argc >= 4 && juce::String(argv[1]) == "--render") { renderPreset(argc, argv); return 0; }
        if (argc == 4 && juce::String(argv[1]) == "--snapshot") {
            AvaAudioProcessor processor;
            require(processor.loadPreset(argumentFile(argv[2])).wasOk(), "Snapshot preset could not load");
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            auto image = editor->createComponentSnapshot(editor->getLocalBounds());
            auto file = argumentFile(argv[3]);
            file.getParentDirectory().createDirectory();
            file.deleteFile(); // JUCE output streams otherwise append to an existing PNG.
            auto stream = file.createOutputStream();
            require(stream != nullptr && juce::PNGImageFormat{}.writeImageToStream(image, *stream), "Snapshot failed");
            return 0;
        }
        testControlLimits();
        testKeyTracking();
        testRendering();
        testPresetsAndState();
        testSustain();
        std::cout << "Plugin rendering, rate conversion, MIDI, presets and state checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
