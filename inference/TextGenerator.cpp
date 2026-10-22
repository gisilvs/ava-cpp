#include "TextGenerator.h"
#include "TextTokenizer.h"
#include <onnxruntime_cxx_api.h>
#include <array>
#include <cmath>
#include <random>
#include <stdexcept>
#include <juce_cryptography/juce_cryptography.h>
#if JUCE_MAC
#include <CommonCrypto/CommonDigest.h>
#endif

namespace {
constexpr size_t sampleCount = 256 * 66;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void checkCancelled(const std::function<bool()>& cancelled) {
    if (cancelled()) throw std::runtime_error("Request cancelled. Current preset retained.");
}

juce::String fileHash(const juce::File& file, const std::function<bool()>& cancelled) {
    juce::FileInputStream input(file);
    check(input.openedOk(), "Could not read native model file.");
#if JUCE_MAC
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    std::vector<char> buffer(65536);
    for (;;) {
        const int bytes = input.read(buffer.data(), int(buffer.size()));
        if (bytes <= 0) break;
        checkCancelled(cancelled);
        CC_SHA256_Update(&context, buffer.data(), CC_LONG(bytes));
    }
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &context);
    return juce::String::toHexString(digest, int(sizeof(digest)), 0);
#else
    juce::BufferedInputStream buffered(&input, 65536, false);
    return juce::SHA256(buffered).toHexString();
#endif
}

Ort::SessionOptions makeSessionOptions() {
    Ort::SessionOptions result;
    result.SetIntraOpNumThreads(4);
    result.SetInterOpNumThreads(1);
    result.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    // Avoid caching large intermediate buffers.
    result.DisableCpuMemArena();
    return result;
}

std::vector<float> copyOutput(const Ort::Value& value, const std::vector<int64_t>& shape) {
    const auto info = value.GetTensorTypeAndShapeInfo();
    check(info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT && info.GetShape() == shape,
          "Generator output has an invalid shape or dtype.");
    const auto* data = value.GetTensorData<float>();
    std::vector<float> result(data, data + info.GetElementCount());
    for (const auto sample : result) check(std::isfinite(sample), "Generator produced non-finite values.");
    return result;
}
}

juce::String TextGenerator::prompt(const juce::String& instrument, const juce::String& description, int midi) {
    check(midi >= 21 && midi <= 78, "Prompt pitch is outside the trained range.");
    const char* names[] {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const auto note = juce::String(names[midi % 12]) + juce::String(midi / 12 - 1);
    const auto frequency = 440 * std::exp2((midi - 69) / 12.);
    auto text = "A single isolated " + instrument.toLowerCase() + "-inspired subtractive synthesizer note at "
        + note + " (MIDI " + juce::String(midi) + ", " + juce::String(frequency, 2) + " Hz).";
    if (description.trim().isNotEmpty()) text += " Desired timbre: " + description.trim() + ".";
    // Match preparation/text_prompts.py: one trigger allows either a pluck or a held note.
    return text + " Dry and monophonic, triggered once, within a 6-second clip.";
}

TextGenerator::Result TextGenerator::generate(const juce::File& assets, const juce::String& text, int seed,
                                             const std::function<bool()>& cancelled,
                                             const std::function<void(int)>& progress,
                                             const std::vector<float>* testNoise) {
    const auto setupStarted = juce::Time::getMillisecondCounterHiRes();
    progress(0);
    const auto manifest = juce::JSON::parse(assets.getChildFile("manifest.json"));
    check(manifest["format"].toString() == "ava-native-generator" && int(manifest["version"]) == 1,
          "Native generator installation is missing or incompatible.");
    const auto* files = manifest["files"].getDynamicObject();
    check(files != nullptr && files->getProperties().size() == 6, "Native model manifest is incomplete.");
    for (const auto& entry : files->getProperties()) {
        const auto name = entry.name.toString();
        check(!name.containsAnyOf("/\\") && name != "." && name != "..", "Invalid generator file path.");
        const auto file = assets.getChildFile(name);
        check(file.existsAsFile() && fileHash(file, cancelled) == entry.value.toString(),
              "Native generator file checksum differs. Reinstall Ava's generator assets.");
        checkCancelled(cancelled);
    }
    const auto config = juce::JSON::parse(assets.getChildFile("sampler.json"));
    check(int(config["effective_frames"]) == 65 && double(config["duration"]) == 6. && double(config["cfg_scale"]) == 1.
          && int(config["steps"]) == 8 && int(config["frames"]) == 66 && int(config["channels"]) == 256
          && config["sampler"].toString() == "rf_denoiser_pingpong", "Unsupported native generator settings.");
    const auto* schedule = config["schedule"].getArray();
    check(schedule != nullptr && schedule->size() == 9, "Generator schedule missing.");
    // Eight denoising steps use nine saved noise levels, ending at zero.
    std::array<float, 9> timesteps;
    for (int step = 0; step < 9; ++step) timesteps[size_t(step)] = float((*schedule)[step]);
    if (testNoise != nullptr) check(testNoise->size() == 9 * sampleCount, "Sampler fixture noise shape differs.");
    TextTokenizer tokenizer(assets.getChildFile("tokenizer.bin"));
    auto tokens = tokenizer.encode(text);
    checkCancelled(cancelled);
    Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "Ava generator");
    environment.DisableTelemetryEvents();
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const auto sessionOptions = makeSessionOptions();
    // The encoder produces token-wise context and one pooled conditioning vector.
    std::vector<float> textConditioning, globalConditioning;
    Result result;
    result.setupSeconds = (juce::Time::getMillisecondCounterHiRes() - setupStarted) / 1000;
    auto started = juce::Time::getMillisecondCounterHiRes();
    progress(1);
    {
        Ort::Session encoder(environment, assets.getChildFile("text_encoder.onnx").getFullPathName().toRawUTF8(), sessionOptions);
        const int64_t shape[] {1, 256};
        std::array<Ort::Value, 2> inputs {
            Ort::Value::CreateTensor<int64_t>(memory, tokens.ids.data(), 256, shape, 2),
            Ort::Value::CreateTensor<int64_t>(memory, tokens.mask.data(), 256, shape, 2)};
        const char* names[] {"input_ids", "attention_mask"};
        const char* outputs[] {"cross_attention", "global_cond"};
        auto encoderOutputs = encoder.Run(Ort::RunOptions{nullptr}, names, inputs.data(), 2, outputs, 2);
        textConditioning = copyOutput(encoderOutputs[0], {1,257,768});
        globalConditioning = copyOutput(encoderOutputs[1], {1,768});
    } // Release the text encoder before loading the denoiser.
    result.textSeconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000;
    checkCancelled(cancelled);
    progress(2);
    started = juce::Time::getMillisecondCounterHiRes();
    std::mt19937 random {uint32_t(seed)};
    std::normal_distribution<float> normal;
    result.latent.resize(sampleCount);
    for (size_t sample = 0; sample < sampleCount; ++sample)
        result.latent[sample] = testNoise != nullptr ? (*testNoise)[sample] : normal(random);
    {
        Ort::Session denoiser(environment, assets.getChildFile("denoiser.onnx").getFullPathName().toRawUTF8(), sessionOptions);
        const int64_t latentShape[] {1,256,66}, timeShape[] {1}, textShape[] {1,257,768}, globalShape[] {1,768};
        const char* inputNames[] {"latent", "timestep", "cross_attention", "global_cond"};
        const char* outputs[] {"velocity"};
        for (size_t step = 0; step < 8; ++step) {
            checkCancelled(cancelled);
            progress(int(step) + 3);
            std::array<Ort::Value, 4> inputs {
                Ort::Value::CreateTensor<float>(memory, result.latent.data(), sampleCount, latentShape, 3),
                Ort::Value::CreateTensor<float>(memory, &timesteps[step], 1, timeShape, 1),
                Ort::Value::CreateTensor<float>(memory, textConditioning.data(), textConditioning.size(), textShape, 3),
                Ort::Value::CreateTensor<float>(memory, globalConditioning.data(), globalConditioning.size(), globalShape, 2)};
            auto denoiserOutputs = denoiser.Run(Ort::RunOptions{nullptr}, inputNames, inputs.data(), 4, outputs, 1);
            const auto velocity = copyOutput(denoiserOutputs[0], {1,256,66});
            for (size_t sample = 0; sample < sampleCount; ++sample) {
                // Predict the clean latent, then mix in noise at the next level.
                const float denoised = result.latent[sample] - timesteps[step] * velocity[sample];
                const float noise = testNoise != nullptr ? (*testNoise)[(step + 1) * sampleCount + sample] : normal(random);
                result.latent[sample] = (1 - timesteps[step + 1]) * denoised + timesteps[step + 1] * noise;
                check(std::isfinite(result.latent[sample]), "Sampler produced non-finite values.");
            }
        }
    }
    result.denoiseSeconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000;
    checkCancelled(cancelled);
    return result;
}

void TextGenerator::saveLatent(const juce::File& file, const std::vector<float>& latent) {
    check(latent.size() == sampleCount, "Cannot save invalid latent shape.");
    file.deleteFile();
    juce::FileOutputStream stream(file);
    check(stream.openedOk(), "Could not save generated latent.");
    std::string header = "{'descr': '<f4', 'fortran_order': False, 'shape': (256, 66), }";
    while ((10 + header.size() + 1) % 64 != 0) header += ' ';
    header += '\n';
    stream.write("\x93NUMPY", 6);
    stream.writeByte(1);
    stream.writeByte(0);
    stream.writeShort(short(header.size()));
    stream.write(header.data(), header.size());
    stream.write(latent.data(), latent.size() * sizeof(float));
    check(stream.getStatus().wasOk(), "Could not write complete latent.");
}
