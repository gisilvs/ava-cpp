#pragma once
#include "Envelope.h"
#include "LowPass.h"
#include "Noise.h"
#include "Oscillator.h"
#include "Preset.h"
#include <array>
#include <cstdint>

namespace ava {
// One sounding note, including its oscillator phases, filter history and envelope.
class Voice {
public:
    void start(const Preset& preset, double frequency, double velocity = 1.0,
               double sampleRate = 24000.0, int lfoBlockSize = 1, std::uint32_t noiseSeed = 0);
    void update(const Preset& preset, double frequency);
    void noteOff();
    void reset();
    bool isActive() const;
    double nextSample();
    void render(float* output, int sampleCount);

private:
    void updateFilter();
    Preset preset;
    Oscillator oscillator;
    Oscillator oscillatorB;
    Envelope envelope;
    LowPass filter;
    Noise noise;
    double frequency = 440.0;
    double velocity = 1.0;
    double sampleRate = 24000.0;
    double lfoPhase = 0.0;
    unsigned long long ageSamples = 0;
    int lfoBlockSize = 1;
};

// A fixed pool of voices. MIDI events update it; render() sums the active notes.
class Engine {
public:
    static constexpr int maxVoices = 8;
    explicit Engine(double sampleRate = 24000.0, double lfoControlRate = 200.0,
                    std::uint32_t noiseSeed = 0);
    void setPreset(const Preset& preset);
    void setControls(const Controls& controls, Waveform oscillator, Waveform oscillatorB);
    void setPitchBend(double semitones);
    void reset();
    void noteOn(int note, double velocity = 1.0);
    void noteOnFrequency(int noteId, double frequency, double velocity = 1.0);
    void noteOff(int noteId);
    void setSustainPedal(bool down);
    void allNotesOff();
    void render(float* output, int sampleCount);

private:
    struct Note {
        Voice voice;
        int id = -1;
        bool held = false;
        double baseFrequency = 440.0; // Before pitch bend.
        unsigned long long startOrder = 0;
    };
    Preset presetFor(double frequency) const;
    void updateVoices();
    std::array<Note, maxVoices> notes;
    Preset preset;
    Controls controls;
    // The plugin uses physical presets for keyboard tracking. Normalized controls
    // are also supported for direct DSP use.
    bool usesNormalizedControls = false;
    bool sustainPedalDown = false;
    double pitchBend = 0.0;
    double sampleRate;
    int lfoBlockSize;
    std::uint32_t noiseSeed;
    unsigned long long noteCounter = 0;
};
} // namespace ava
