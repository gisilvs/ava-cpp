#include "../dsp/Engine.h"
#include <algorithm>
#include <array>

namespace {
ava::Engine engine(24000.0, 200.0);
ava::Preset preset;
std::array<float, 13> controls{};
std::array<float, 512> output{};
int previewRemaining = 0;
constexpr int previewId = 1000;
}

// Small C interface for the AudioWorklet. All synthesis stays in dsp/.
extern "C" {
float* ava_controls() { return controls.data(); }

void ava_set_controls(int oscillatorA, int oscillatorB, double referenceHz) {
    ava::Controls c;
    c.gain = controls[0];
    c.mix = controls[1];
    c.detuneCents = controls[2];
    c.noiseMix = controls[3];
    c.cutoff = controls[4];
    c.q = controls[5];
    c.lfoRate = controls[6];
    c.lfoDepth = controls[7];
    c.drive = controls[8];
    c.attack = controls[9];
    c.decay = controls[10];
    c.sustain = controls[11];
    c.release = controls[12];
    // As in the plugin, decode at the preset's reference pitch, then key-track.
    preset = ava::decodeControls(c, referenceHz, 24000.0,
                                ava::Waveform(oscillatorA), ava::Waveform(oscillatorB));
    engine.setPreset(preset);
}

void ava_note_on(int note, double velocity) { engine.noteOn(note, velocity); }
void ava_note_off(int note) { engine.noteOff(note); }
void ava_reset() { engine.reset(); previewRemaining = 0; }

void ava_preview(double frequency) {
    ava_reset();
    engine.noteOnFrequency(previewId, frequency);
    previewRemaining = 4 * 24000;
}

int ava_preview_active() { return previewRemaining > 0; }

float* ava_render(int count) {
    count = std::clamp(count, 0, int(output.size()));
    if (previewRemaining > 0) {
        const int held = std::min(count, previewRemaining);
        engine.render(output.data(), held);
        previewRemaining -= held;
        if (previewRemaining == 0) engine.noteOff(previewId);
        engine.render(output.data() + held, count - held);
    } else {
        engine.render(output.data(), count);
    }
    return output.data();
}

// Physical values for the UI; it doesn't need to duplicate the control mapping.
double ava_value(int index) {
    switch (index) {
        case 0: return preset.gain;
        case 1: return preset.mix;
        case 2: return preset.detuneCents;
        case 3: return preset.noiseMix;
        case 4: return preset.cutoffOctaves;
        case 5: return preset.q;
        case 6: return preset.lfoRate;
        case 7: return preset.lfoDepth;
        case 8: return preset.drive;
        case 9: return preset.attack;
        case 10: return preset.decay;
        case 11: return preset.sustain;
        case 12: return preset.release;
        default: return 0.0;
    }
}
}
