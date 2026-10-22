#pragma once
#include <juce_core/juce_core.h>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <array>
#include <unordered_map>

// Stable Audio's fixed BPE tokenizer. No Python, network or audio-thread work.
class TextTokenizer {
public:
    explicit TextTokenizer(const juce::File&);
    struct Tokens {
        std::array<int64_t, 256> ids {};
        std::array<int64_t, 256> mask {};
    };
    Tokens encode(const juce::String&) const;
private:
    struct Merge {
        int rank;
        int mergedToken;
    };
    void encodePiece(const juce::String&, std::vector<int>&) const;
    std::unordered_map<std::string, int> vocabulary;
    std::unordered_map<uint64_t, Merge> merges;
    std::vector<std::pair<juce::String, int>> addedTokens;
    int pad = 0;
};
