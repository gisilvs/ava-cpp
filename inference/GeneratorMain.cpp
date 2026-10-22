#include "TextGenerator.h"
#include <stdexcept>

// One helper process per request releases every model/allocator page on exit.
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const auto assets = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    const auto requestFile = juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);
    const auto root = requestFile.getParentDirectory();
    auto save = [&root](const char* name, juce::DynamicObject* value) {
        if (!root.getChildFile(name).replaceWithText(juce::JSON::toString(juce::var(value))))
            throw std::runtime_error("Could not write native generator status.");
    };
    try {
        const auto request = juce::JSON::parse(requestFile);
        if (!request["prompt"].isString() || !request["seed"].isInt())
            throw std::runtime_error("Invalid native generation request.");
        // The plugin polls progress.json while this helper owns the large models.
        const auto result = TextGenerator::generate(assets, request["prompt"].toString(), int(request["seed"]),
            [] { return false; }, [&save](int stage) {
                auto* value = new juce::DynamicObject();
                value->setProperty("stage", stage);
                save("progress.json", value);
            });
        TextGenerator::saveLatent(root.getChildFile("latents.npy"), result.latent);
        auto* timing = new juce::DynamicObject();
        timing->setProperty("backend", "onnx-cpp-helper-v1");
        const auto manifest = juce::JSON::parse(assets.getChildFile("manifest.json"));
        timing->setProperty("precision", manifest["precision"]);
        timing->setProperty("setup_seconds", result.setupSeconds);
        timing->setProperty("text_seconds", result.textSeconds);
        timing->setProperty("denoise_seconds", result.denoiseSeconds);
        save("timing.json", timing);
        // Report success only after the latent and timing files are written.
        auto* status = new juce::DynamicObject();
        status->setProperty("ok", true);
        save("status.json", status);
        return 0;
    } catch (const std::exception& error) {
        auto* status = new juce::DynamicObject();
        status->setProperty("ok", false);
        status->setProperty("error", error.what());
        try { save("status.json", status); } catch (...) {}
        return 1;
    }
}
