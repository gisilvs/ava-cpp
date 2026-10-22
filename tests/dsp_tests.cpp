#include "Engine.h"
#include "Effects.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void close(double actual, double expected, double tolerance, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}

template <typename Function>
void rejects(Function function) {
    bool failed = false;
    try { function(); } catch (const std::invalid_argument&) { failed = true; }
    require(failed, "Invalid inputs must be rejected");
}

void testComponents() {
    ava::Oscillator sine;
    sine.start(ava::Waveform::sine, 1000.0, 24000.0);
    constexpr double pi = 3.14159265358979323846;
    for (int i = 0; i < 2400; ++i)
        close(sine.nextSample(), std::sin(2 * pi * 1000 * i / 24000), 1e-11, "Sine phase");
    // At this pitch every overtone is above Nyquist. Each waveform is a scaled sine.
    for (auto shape : {ava::Waveform::square, ava::Waveform::sawtooth, ava::Waveform::triangle}) {
        ava::Oscillator oscillator;
        oscillator.start(shape, 7000, 24000);
        const double scale = shape == ava::Waveform::square ? 4 / pi
            : shape == ava::Waveform::sawtooth ? 2 / pi : 8 / (pi * pi);
        for (int i = 0; i < 100; ++i)
            close(oscillator.nextSample(), scale * std::sin(2 * pi * 7000 * i / 24000),
                  1e-12, "Harmonics must stop below Nyquist");
    }
    ava::Envelope env;
    env.start(.1, .2, .6, .3, 1000);
    close(env.nextValue(), 0, 0, "ADSR starts at zero");
    for (int i = 1; i < 100; ++i) env.nextValue();
    close(env.nextValue(), 1, 1e-12, "Attack reaches one");
    env.start(.1, .2, .6, .3, 1000);
    for (int i = 0; i < 40; ++i) env.nextValue();
    const double held = -std::expm1(-4.0 * .4) / -std::expm1(-4.0);
    env.noteOff();
    close(env.nextValue(), held, 1e-12, "Early note-off uses exact held level");
    env.noteOff(); // Repeated note-off must not restart the tail.
    for (int i = 1; i <= 300; ++i) env.nextValue();
    require(!env.isActive(), "Release finishes");
    close(ava::distort(.37, 0), .37, 0, "Zero drive is identity");
    close(ava::distort(1, 10), 1, 1e-12, "Normalized distortion");
    ava::Noise noise;
    noise.reset(0);
    // First CPU torch.rand values, transformed to [-1,1].
    for (double expected : {-.007486820220947266, .5364435911178589, -.8230451345443726})
        close(noise.nextSample(), expected, 0, "Seeded noise reference");
}

void testControls() {
    ava::Controls c;
    auto p = ava::decodeControls(c, 440, 24000, ava::Waveform::sine, ava::Waveform::triangle);
    close(p.gain, 1, 0, "Gain coordinate");
    close(p.attack, .1, 1e-15, "Duration coordinate");
    close(p.q, .5, 1e-15, "Log Q coordinate");
    c.cutoff = 0;
    c.lfoDepth = 1;
    c.mix = 0;
    c.drive = 1;
    c.detuneCents = 1;
    p = ava::decodeControls(c, 440, 24000, ava::Waveform::sine, ava::Waveform::square);
    close(440 * std::exp2(p.cutoffOctaves), 20, 1e-12, "Full cutoff lower endpoint");
    close(p.lfoDepth, 0, 1e-12, "No room for LFO at cutoff boundary");
    close(p.mix, 0, 0, "Mix is not restricted to dataset range");
    close(p.drive, 10, 0, "Drive coordinate");
    close(p.detuneCents, 30, 0, "B detune coordinate");
    c.gain = c.attack = c.q = c.lfoRate = c.cutoff = 1;
    p = ava::decodeControls(c, 440, 24000, ava::Waveform::sine, ava::Waveform::square);
    require(std::isfinite(p.gain) && std::isfinite(p.attack), "Finite endpoint guards");
    close(p.q, 10, 1e-12, "Q upper endpoint");
    close(p.lfoRate, 10, 1e-12, "LFO rate upper endpoint");
    c.gain = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { ava::decodeControls(c, 440, 24000, ava::Waveform::sine, ava::Waveform::sine); });
    ava::Voice voice;
    p = ava::Preset{};
    p.attack = 0;
    rejects([&] { voice.start(p, 440); });
    p.attack = .01;
    p.filterEnabled = true;
    p.cutoffOctaves = -20;
    rejects([&] { voice.start(p, 440); });
}

void testStreaming() {
    ava::Preset p;
    p.oscillator = ava::Waveform::sawtooth;
    p.oscillatorB = ava::Waveform::square;
    p.mix = .37;
    p.detuneCents = 13;
    p.noiseMix = .08;
    p.filterEnabled = p.lfoEnabled = true;
    p.cutoffOctaves = 1.5;
    p.lfoRate = 3.7;
    p.lfoDepth = .4;
    p.q = 2.4;
    p.drive = 1.7;
    p.attack = .04;
    p.decay = .2;
    p.sustain = .5;
    p.release = .1;
    const int held = 1007, total = 5000;
    for (int controlBlock : {1, 120}) {
        ava::Voice one, many;
        one.start(p, 440, .8, 24000, controlBlock);
        many.start(p, 440, .8, 24000, controlBlock);
        std::vector<float> a(total), b(total);
        one.render(a.data(), held);
        one.noteOff();
        one.render(a.data() + held, total - held);
        int offset = 0;
        while (offset < total) {
            if (offset == held) many.noteOff();
            const int untilEvent = offset < held ? held - offset : total - offset;
            const int count = std::min(73, untilEvent);
            many.render(b.data() + offset, count);
            offset += count;
        }
        require(a == b, "Render block boundaries must not change audio");
        for (float value : a) require(std::isfinite(value), "Finite full-chain output");
        close(a.back(), 0, 0, "No signal after release");
        require(!one.isActive(), "Finished voice becomes inactive");
    }
    ava::Engine engine;
    engine.setPreset(ava::Preset{});
    std::vector<float> output(128);
    engine.noteOn(69, 1);
    engine.render(output.data(), 128);
    require(output[100] != 0, "MIDI note produces audio");
    engine.reset();
    engine.render(output.data(), 128);
    require(std::all_of(output.begin(), output.end(), [](float v) { return v == 0; }), "Reset silences voices");
    // More notes than voices: deterministic oldest-note stealing, no allocation.
    for (int i = 0; i < 12; ++i) engine.noteOn(60 + i, .7);
    engine.render(output.data(), 128);
    for (float value : output) require(std::isfinite(value), "Finite polyphony");
}
} // namespace

int main() {
    try {
        testComponents();
        testControls();
        testStreaming();
        std::cout << "DSP component, control and streaming checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
