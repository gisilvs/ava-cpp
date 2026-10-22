#include "Engine.h"
#include "Effects.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ava {
namespace {
constexpr double pi = 3.14159265358979323846;
} // namespace

void Voice::start(const Preset& newPreset, double newFrequency, double noteVelocity,
                  double newSampleRate, int blockSize, std::uint32_t seed) {
    if (!std::isfinite(noteVelocity) || noteVelocity < 0.0 || noteVelocity > 1.0 || blockSize < 1)
        throw std::invalid_argument("Velocity must be 0–1 and LFO block size must be positive");
    sampleRate = newSampleRate;
    velocity = noteVelocity;
    lfoBlockSize = blockSize;
    ageSamples = 0;
    lfoPhase = 0;
    filter.reset();
    // Starting a note resets oscillator phases; later updates preserve them.
    oscillator.start(newPreset.oscillator, newFrequency, newSampleRate);
    if (newPreset.secondOscillator)
        oscillatorB.start(newPreset.oscillatorB,
                          newFrequency * std::exp2(newPreset.detuneCents / 1200.0), newSampleRate);
    update(newPreset, newFrequency);
    envelope.start(newPreset.attack, newPreset.decay, newPreset.sustain, newPreset.release, newSampleRate);
    noise.reset(seed);
}

void Voice::update(const Preset& newPreset, double newFrequency) {
    validatePreset(newPreset, newFrequency, sampleRate);
    preset = newPreset;
    frequency = newFrequency;
    oscillator.configure(newPreset.oscillator, newFrequency, sampleRate);
    if (newPreset.secondOscillator)
        oscillatorB.configure(newPreset.oscillatorB,
                              newFrequency * std::exp2(newPreset.detuneCents / 1200.0), sampleRate);
    envelope.setParameters(newPreset.attack, newPreset.decay, newPreset.sustain, newPreset.release);
    if (newPreset.filterEnabled)
        updateFilter();
}

void Voice::updateFilter() {
    // Cutoff is an octave offset from this note; the LFO adds another octave offset.
    const double offset = preset.cutoffOctaves
        + (preset.lfoEnabled ? preset.lfoDepth * std::sin(lfoPhase) : 0.0);
    const double cutoff = std::clamp(frequency * std::exp2(offset), 20.0, maxCutoff(sampleRate));
    filter.setCutoff(cutoff, preset.q, sampleRate);
}

void Voice::noteOff() { envelope.noteOff(); }

void Voice::reset() {
    envelope = Envelope{};
    filter.reset();
    ageSamples = 0;
    lfoPhase = 0;
}

bool Voice::isActive() const { return envelope.isActive(); }

double Voice::nextSample() {
    if (!isActive())
        return 0.0;

    // Oscillator A -> optional oscillator B mix -> noise mix.
    double sample = oscillator.nextSample();
    if (preset.secondOscillator)
        sample = (1.0 - preset.mix) * sample + preset.mix * oscillatorB.nextSample();
    sample = (1.0 - preset.noiseMix) * sample + preset.noiseMix * noise.nextSample();

    // Resonant low-pass -> distortion -> amplitude envelope and note velocity.
    if (preset.filterEnabled) {
        if (preset.lfoEnabled && ageSamples % lfoBlockSize == 0)
            updateFilter();
        sample = filter.process(sample);
    }
    sample = distort(sample, preset.drive);
    sample *= envelope.nextValue() * preset.gain * velocity;

    // Advance modulation after rendering, so a new note begins at LFO phase zero.
    if (preset.lfoEnabled) {
        lfoPhase += 2 * pi * preset.lfoRate / sampleRate;
        if (lfoPhase >= 2 * pi)
            lfoPhase -= 2 * pi;
    }
    ++ageSamples;
    return sample;
}

void Voice::render(float* output, int sampleCount) {
    for (int i = 0; i < sampleCount; ++i)
        output[i] = float(nextSample());
}

Engine::Engine(double newSampleRate, double controlRate, std::uint32_t seed)
    : sampleRate(newSampleRate), noiseSeed(seed) {
    if (!std::isfinite(newSampleRate) || !std::isfinite(controlRate) || newSampleRate <= 0.0
        || controlRate <= 0.0 || controlRate > newSampleRate
        || newSampleRate / controlRate > std::numeric_limits<int>::max())
        throw std::invalid_argument("Invalid sample rate or LFO control rate");
    lfoBlockSize = std::max(1, int(std::nearbyint(newSampleRate / controlRate)));
}

Preset Engine::presetFor(double frequency) const {
    auto notePreset = preset;
    if (usesNormalizedControls)
        notePreset = decodeControls(controls, frequency, sampleRate, preset.oscillator, preset.oscillatorB);
    if (notePreset.filterEnabled) {
        // Physical presets track pitch. At the keyboard extremes, keep both
        // the center and the modulation sweep inside the filter's valid range.
        notePreset.cutoffOctaves = std::clamp(notePreset.cutoffOctaves, std::log2(20.0 / frequency),
                                             std::log2(maxCutoff(sampleRate) / frequency));
        notePreset.lfoDepth = std::min(notePreset.lfoDepth,
                                      maxLfoDepth(notePreset.cutoffOctaves, frequency, sampleRate));
    }
    return notePreset;
}

void Engine::setPreset(const Preset& newPreset) {
    preset = newPreset;
    usesNormalizedControls = false;
    updateVoices();
}

void Engine::setControls(const Controls& newControls, Waveform oscillatorA, Waveform oscillatorB) {
    // Validate before accepting a new control snapshot.
    decodeControls(newControls, 440.0, sampleRate, oscillatorA, oscillatorB);
    controls = newControls;
    preset.oscillator = oscillatorA;
    preset.oscillatorB = oscillatorB;
    usesNormalizedControls = true;
    updateVoices();
}

void Engine::setPitchBend(double semitones) {
    if (!std::isfinite(semitones) || std::abs(semitones) > 2.0)
        throw std::invalid_argument("Pitch bend range is ±2 semitones");
    pitchBend = semitones;
    updateVoices();
}

void Engine::updateVoices() {
    for (auto& note : notes) {
        if (!note.voice.isActive())
            continue;
        const double frequency = note.baseFrequency * std::exp2(pitchBend / 12.0);
        const double detune = usesNormalizedControls ? 30.0 * controls.detuneCents : preset.detuneCents;
        if (frequency * std::exp2(detune / 1200.0) >= sampleRate / 2.0) {
            note.voice.reset();
            continue;
        }
        note.voice.update(presetFor(frequency), frequency);
    }
}

void Engine::reset() {
    for (auto& note : notes) {
        note.voice.reset();
        note.id = -1;
        note.held = false;
        note.startOrder = 0;
    }
    noteCounter = 0;
    sustainPedalDown = false;
    pitchBend = 0.0;
}

void Engine::noteOn(int midi, double velocity) {
    noteOnFrequency(midi, midiFrequency(midi), velocity);
}

void Engine::noteOnFrequency(int id, double baseFrequency, double velocity) {
    if (!std::isfinite(velocity) || velocity < 0.0 || velocity > 1.0)
        throw std::invalid_argument("Velocity must be between 0 and 1");
    if (velocity == 0.0) {
        noteOff(id);
        return;
    }
    const double frequency = baseFrequency * std::exp2(pitchBend / 12.0);
    if (!std::isfinite(frequency) || frequency <= 0 || frequency >= sampleRate / 2.0)
        return;
    // The top few MIDI keys lie outside the 24 kHz oscillator range.
    // Ignore them instead of throwing an exception in the host's audio callback.
    const double detune = usesNormalizedControls ? 30.0 * controls.detuneCents : preset.detuneCents;
    if (frequency * std::exp2(detune / 1200.0) >= sampleRate / 2.0)
        return;

    // Use the first free voice. If all eight are sounding, replace the oldest.
    auto* selected = &notes[0];
    for (auto& note : notes) {
        if (!note.voice.isActive()) {
            selected = &note;
            break;
        }
        if (note.startOrder < selected->startOrder)
            selected = &note;
    }
    selected->voice.start(presetFor(frequency), frequency, velocity, sampleRate, lfoBlockSize, noiseSeed);
    selected->id = id;
    selected->baseFrequency = baseFrequency;
    selected->held = true;
    selected->startOrder = ++noteCounter;
}

void Engine::noteOff(int id) {
    for (auto& note : notes) {
        if (note.id != id)
            continue;
        note.held = false;
        if (!sustainPedalDown)
            note.voice.noteOff();
    }
}

void Engine::setSustainPedal(bool down) {
    sustainPedalDown = down;
    if (!down) {
        for (auto& note : notes) {
            if (!note.held)
                note.voice.noteOff();
        }
    }
}

void Engine::allNotesOff() {
    sustainPedalDown = false;
    for (auto& note : notes) {
        note.held = false;
        note.voice.noteOff();
    }
}

void Engine::render(float* output, int sampleCount) {
    for (int i = 0; i < sampleCount; ++i) {
        double sample = 0.0;
        for (auto& note : notes)
            sample += note.voice.nextSample();
        output[i] = float(sample);
    }
}
} // namespace ava
