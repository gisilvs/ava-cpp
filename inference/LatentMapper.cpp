#include "LatentMapper.h"
#include <onnxruntime_cxx_api.h>
#include <juce_cryptography/juce_cryptography.h>
#include <regex>
#include <cstring>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(message);
}

struct Latent {
    std::vector<float> samples = std::vector<float>(256 * 66, 0.f);
    int64_t frames = 66;
};

Latent readLatent(const juce::File& file) {
    juce::FileInputStream input(file);
    check(input.openedOk(), "Could not open latent file.");
    char magic[6];
    check(input.read(magic, 6) == 6 && std::memcmp(magic, "\x93NUMPY", 6) == 0,
          "Choose a NumPy .npy latent file.");
    const auto major = input.readByte();
    const auto minor = input.readByte();
    check((major == 1 || major == 2) && minor == 0, "Unsupported NPY version.");
    const int length = major == 1 ? int(uint16_t(input.readShort())) : input.readInt();
    check(length > 0 && length <= 65536, "Invalid NPY header.");
    std::string header(size_t(length), ' ');
    check(input.read(header.data(), length) == length, "Truncated NPY header.");
    check(std::regex_search(header, std::regex(R"(['"]descr['"]\s*:\s*['"](<f4|=f4)['"])"))
          && std::regex_search(header, std::regex(R"(['"]fortran_order['"]\s*:\s*False)")),
          "Use little-endian float32, channel-major SAME latents.");
    std::smatch match;
    check(std::regex_search(header, match, std::regex(R"(['"]shape['"]\s*:\s*\(([^)]*)\))")),
          "NPY shape is missing.");
    auto dimensions = juce::StringArray::fromTokens(juce::String(match[1].str()), ",", "");
    dimensions.trim();
    dimensions.removeEmptyStrings();
    if (dimensions.size() == 3 && dimensions[0] == "1") dimensions.remove(0);
    check(dimensions.size() == 2, "Expected [256,T] or [1,256,T].");
    for (const auto& dimension : dimensions)
        check(dimension.containsOnly("0123456789"), "Invalid NPY dimensions.");
    const int channels = dimensions[0].getIntValue(), frames = dimensions[1].getIntValue();
    check(channels == 256 && frames >= 1 && frames <= 66, "Expected raw SAME latent [256,1..66].");
    const int bytes = 256 * frames * int(sizeof(float));
    check(input.getTotalLength() - input.getPosition() == bytes, "NPY data size does not match its shape.");
    std::vector<float> raw(size_t(256 * frames), 0.f);
    check(input.read(raw.data(), bytes) == bytes, "Truncated latent data.");
    Latent latent;
    latent.frames = frames;
    // Pad each channel separately to 66 frames, preserving channel-major order.
    for (int channel = 0; channel < 256; ++channel)
        for (int frame = 0; frame < frames; ++frame) {
            const auto value = raw[size_t(channel * frames + frame)];
            check(std::isfinite(value), "Latent contains non-finite values.");
            latent.samples[size_t(channel * 66 + frame)] = value;
        }
    // Generated datasets already store their valid length alongside padded arrays.
    const auto sidecar = file.getSiblingFile("generation.json");
    if (sidecar.existsAsFile()) {
        const auto value = juce::JSON::parse(sidecar)["valid_frames"];
        check(value.isInt() && int(value) >= 1 && int(value) <= frames,
              "generation.json has an invalid valid_frames value.");
        latent.frames = int(value);
    }
    return latent;
}
} // namespace

PresetFile LatentMapper::predict(const juce::File& modelDirectory, const juce::File& latentFile, double previewMidi) {
    const auto metadata = juce::JSON::parse(modelDirectory.getChildFile("mapper.json"));
    check(metadata["format"].toString() == "ava-latent-mapper" && int(metadata["version"]) == 1
          && metadata["control_mapping"].toString() == "synth_v1", "Choose a valid mapper bundle.");
    const auto* shape = metadata["feature_shape"].getArray();
    check(shape != nullptr && shape->size() == 2 && int((*shape)[0]) == 256 && int((*shape)[1]) == 66,
          "Mapper must accept SAME [256,66] features.");
    const auto* names = metadata["controls"].getArray();
    check(names != nullptr && names->size() == 13, "Mapper control order is missing.");
    // Exported columns may differ from the host control order. Match them by name.
    std::array<int, 13> controlOrder;
    std::array<bool, 13> usedControls{};
    for (int i = 0; i < 13; ++i) {
        int index = -1;
        for (int j = 0; j < 13; ++j)
            if ((*names)[i].toString() == parameters::controls[size_t(j)].id) index = j;
        check(index >= 0 && !usedControls[size_t(index)], "Invalid or duplicate mapper control name.");
        usedControls[size_t(index)] = true;
        controlOrder[size_t(i)] = index;
    }
    juce::MemoryBlock model;
    check(modelDirectory.getChildFile("mapper.onnx").loadFileAsData(model), "Could not read mapper.onnx.");
    check(juce::SHA256(model).toHexString() == metadata["onnx_sha256"].toString(), "Mapper checksum does not match.");
    // Run the small mapper on raw latents; no feature normalization is applied.
    auto latent = readLatent(latentFile);
    Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "Ava");
    Ort::SessionOptions options;
    options.SetIntraOpNumThreads(2);
    options.SetInterOpNumThreads(1);
    environment.DisableTelemetryEvents();
    Ort::Session session(environment, model.getData(), model.getSize(), options);
    check(session.GetInputCount() == 2 && session.GetOutputCount() == 2, "Unexpected mapper inputs/outputs.");
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const std::array<int64_t, 3> latentShape {1, 256, 66};
    const std::array<int64_t, 1> frameShape {1};
    std::array<Ort::Value, 2> inputs {
        Ort::Value::CreateTensor<float>(memory, latent.samples.data(), latent.samples.size(), latentShape.data(), 3),
        Ort::Value::CreateTensor<int64_t>(memory, &latent.frames, 1, frameShape.data(), 1)};
    const char* inputNames[] {"latents", "valid_frames"};
    const char* outputNames[] {"controls", "oscillator_logits"};
    auto outputs = session.Run(Ort::RunOptions{nullptr}, inputNames, inputs.data(), 2, outputNames, 2);
    check(outputs[0].GetTensorTypeAndShapeInfo().GetShape() == std::vector<int64_t>({1,13})
          && outputs[1].GetTensorTypeAndShapeInfo().GetShape() == std::vector<int64_t>({1,2,4})
          && outputs[0].GetTensorTypeAndShapeInfo().GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
          && outputs[1].GetTensorTypeAndShapeInfo().GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
          "Unexpected mapper output shape or type.");
    PresetFile preset;
    preset.name = latentFile.getParentDirectory().getFileName() + " / latent";
    preset.description = "Predicted from " + latentFile.getFileName();
    preset.previewMidi = previewMidi;
    // Preserve the plugin's existing playable limits when applying predictions.
    const auto* predictedControls = outputs[0].GetTensorData<float>();
    for (int i = 0; i < 13; ++i) {
        check(std::isfinite(predictedControls[i]) && predictedControls[i] >= 0 && predictedControls[i] <= 1, "Invalid predicted control.");
        const auto index = size_t(controlOrder[size_t(i)]);
        const auto& control = parameters::controls[index];
        preset.values[index] = std::clamp(predictedControls[i], control.minimum, control.maximum);
    }
    const auto* logits = outputs[1].GetTensorData<float>();
    for (int i = 0; i < 8; ++i) check(std::isfinite(logits[i]), "Invalid oscillator logits.");
    // Each oscillator has four logits. Deployment selects the largest one.
    for (int head = 0; head < 2; ++head) {
        const auto* begin = logits + head * 4;
        preset.values[size_t(13 + head)] = float(std::max_element(begin, begin + 4) - begin);
    }
    ava::decodeControls(parameters::synthControls(preset.values), ava::midiFrequency(previewMidi), 24000,
                        ava::Waveform(int(preset.values[13])), ava::Waveform(int(preset.values[14])));
    return preset;
}
