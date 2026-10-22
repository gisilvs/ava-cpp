#include "LatentJob.h"
#include "LatentMapper.h"
#include "TextGenerator.h"
#include <juce_cryptography/juce_cryptography.h>
#include <dlfcn.h>
#include <mutex>
#include <stdexcept>

namespace {
juce::File generatorExecutable() {
    Dl_info module {};
    if (dladdr(reinterpret_cast<const void*>(&generatorExecutable), &module) == 0) return {};
    const juce::File binary(module.dli_fname);
    const auto bundled = binary.getParentDirectory().getParentDirectory().getChildFile("Helpers/AvaGenerator");
    return bundled.existsAsFile() ? bundled : binary.getSiblingFile("AvaGenerator");
}

void check(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(message);
}
} // namespace

LatentJob::LatentJob(juce::File modelDirectory, juce::File latent, double midi)
    : Thread("Ava latent inference"), mapperDirectory(std::move(modelDirectory)), latentFile(std::move(latent)), previewMidi(midi) {}
LatentJob::~LatentJob() { stopThread(-1); }
void LatentJob::run() {
    PresetFile result;
    juce::String failure;
    try {
        const auto input = text ? generateText() : latentFile;
        if (threadShouldExit()) throw std::runtime_error("Request cancelled.");
        result = LatentMapper::predict(mapperDirectory, input, previewMidi);
        if (text) {
            result.name = text->instrument + " / generated";
            result.description = text->description;
            check(result.write(text->outputDirectory.getChildFile("preset.ava.json")).wasOk(), "Could not save generated preset.");
        }
        if (threadShouldExit()) throw std::runtime_error("Request cancelled.");
    }
    catch (const std::exception& exception) { failure = exception.what(); }
    // Hand the completed result back to the editor; the worker never changes audio controls.
    const juce::ScopedLock guard(lock);
    preset = std::move(result);
    error = failure;
    finished = true;
}
bool LatentJob::takeResult(PresetFile& result, juce::String& failure) {
    const juce::ScopedLock guard(lock);
    if (!finished) return false;
    result = preset;
    failure = error;
    finished = false;
    return true;
}

LatentJob::LatentJob(juce::File modelDirectory, TextRequest request)
    : Thread("Ava text generation"), mapperDirectory(std::move(modelDirectory)), previewMidi(request.midi), text(std::move(request)) {}

juce::String LatentJob::progress() const {
    if (threadShouldExit()) return "Cancelling request...";
    const int current = stage.load();
    if (current >= 103 && current <= 110)
        return "Generating preset... step " + juce::String(current - 102) + "/8";
    switch (current) {
        case 99: return "Waiting for another generation to finish...";
        case 100: return "Checking native generator...";
        case 101: return "Encoding your description...";
        case 102: return "Loading diffusion model...";
        case 111: return "Mapping latent to synth controls...";
        default: return "Mapping latent...";
    }
}

juce::File LatentJob::generateText() {
    // Only one request loads the large generator at a time across plugin instances.
    static std::mutex generationMutex;
    std::unique_lock<std::mutex> generationLock(generationMutex, std::defer_lock);
    while (!generationLock.try_lock()) {
        if (threadShouldExit()) throw std::runtime_error("Request cancelled.");
        stage.store(99);
        wait(50);
    }
    if (threadShouldExit()) throw std::runtime_error("Request cancelled.");
    const auto executable = generatorExecutable();
    check(executable.existsAsFile(), "Native generator helper is missing. Reinstall Ava.");
    const auto assets = text->generatorDirectory;
    const auto sampler = juce::JSON::parse(assets.getChildFile("sampler.json"));
    const auto encoder = juce::JSON::parse(mapperDirectory.getChildFile("mapper.json"))["encoder"];
    check(sampler["checkpoint_sha256"].toString() == encoder["checkpoint_sha256"].toString()
          && sampler["config_sha256"].toString() == encoder["config_sha256"].toString(),
          "Native generator and mapper use different latent spaces.");
    if (text->outputDirectory == juce::File{})
        text->outputDirectory = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Ava/requests").getChildFile(juce::Uuid().toString());
    const auto root = text->outputDirectory;
    check(root.createDirectory().wasOk(), "Could not create generation output directory.");
    const auto prompt = TextGenerator::prompt(text->instrument, text->description, text->midi);
    auto* request = new juce::DynamicObject();
    request->setProperty("backend", "onnx-cpp-helper-v1");
    request->setProperty("generator_manifest_sha256", juce::SHA256(assets.getChildFile("manifest.json")).toHexString());
    request->setProperty("mapper_sha256", juce::JSON::parse(mapperDirectory.getChildFile("mapper.json"))["onnx_sha256"]);
    request->setProperty("instrument", text->instrument);
    request->setProperty("description", text->description);
    request->setProperty("midi", text->midi);
    request->setProperty("seed", text->seed);
    request->setProperty("prompt", prompt);
    check(root.getChildFile("request.json").replaceWithText(juce::JSON::toString(juce::var(request))), "Could not save native request.");
    check(root.getChildFile("prompt.txt").replaceWithText(prompt), "Could not save prompt.");
    for (const auto* name : {"status.json", "progress.json", "latents.npy"}) root.getChildFile(name).deleteFile();
    stage.store(100);
    juce::ChildProcess child;
    check(child.start(juce::StringArray {executable.getFullPathName(), assets.getFullPathName(),
        root.getChildFile("request.json").getFullPathName()}, 0), "Could not start native generator helper.");
    while (child.isRunning()) {
        if (threadShouldExit()) {
            child.kill();
            child.waitForProcessToFinish(1000);
            throw std::runtime_error("Request cancelled. Current preset retained.");
        }
        const auto progress = juce::JSON::parse(root.getChildFile("progress.json"))["stage"];
        if (progress.isInt()) stage.store(100 + int(progress));
        wait(50);
    }
    const auto status = juce::JSON::parse(root.getChildFile("status.json"));
    if (child.getExitCode() != 0 || !bool(status["ok"])) {
        const auto message = status["error"].toString();
        throw std::runtime_error(message.isEmpty()
            ? "Native generation failed. See request folder." : message.toStdString());
    }
    const auto latent = root.getChildFile("latents.npy");
    check(latent.existsAsFile(), "Native generator did not save its latent.");
    previewMidi = text->midi;
    stage.store(111);
    return latent;
}
