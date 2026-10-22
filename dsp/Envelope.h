#pragma once

namespace ava {
// Durations are seconds; sustain is a level between zero and one.
class Envelope {
public:
    void start(double attack, double decay, double sustain, double release, double sampleRate);
    void setParameters(double attack, double decay, double sustain, double release);
    void noteOff();
    double nextValue();
    bool isActive() const;

private:
    double heldLevel(double time) const;
    double attack = 0.01;
    double decay = 0.2;
    double sustain = 1.0;
    double release = 1.5;
    double sampleRate = 24000.0;
    unsigned long long ageSamples = 0;
    unsigned long long releaseSamples = 0;
    double releaseLevel = 0.0;
    bool released = false;
    bool active = false;
};
} // namespace ava
