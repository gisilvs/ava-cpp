#include "TextTokenizer.h"
#include <limits>
#include <cstring>
#include <stdexcept>

namespace {
uint64_t pairKey(int a, int b) { return (uint64_t(a) << 32) | uint32_t(b); }
}

TextTokenizer::TextTokenizer(const juce::File& file) {
    juce::FileInputStream source(file);
    if (!source.openedOk()) throw std::runtime_error("Generator tokenizer is missing.");
    juce::BufferedInputStream input(&source, 65536, false);
    char magic[8];
    if (input.read(magic, 8) != 8 || std::memcmp(magic, "AVATOK1\0", 8) != 0)
        throw std::runtime_error("Unsupported tokenizer version.");
    const auto integer = [&input](int maximum) {
        const int value = input.readInt();
        if (value < 0 || value > maximum || input.getPosition() > input.getTotalLength())
            throw std::runtime_error("Invalid tokenizer table.");
        return value;
    };
    const auto token = [&input, &integer]() {
        const int index = integer(256000);
        const int bytes = integer(65536);
        std::string text(size_t(bytes), '\0');
        if (input.read(text.data(), bytes) != bytes) throw std::runtime_error("Truncated tokenizer token.");
        return std::make_pair(std::move(text), index);
    };
    pad = integer(256000);
    const int count = integer(256000);
    vocabulary.reserve(size_t(count));
    for (int i = 0; i < count; ++i) {
        auto value = token();
        vocabulary.emplace(std::move(value.first), value.second);
    }
    const int rules = integer(1000000);
    merges.reserve(size_t(rules));
    for (int rank = 0; rank < rules; ++rank) {
        const int a = integer(256000), b = integer(256000), result = integer(256000);
        merges.emplace(pairKey(a, b), Merge {rank, result});
    }
    const int extras = integer(1000);
    for (int i = 0; i < extras; ++i) {
        const auto value = token();
        addedTokens.emplace_back(juce::String::fromUTF8(value.first.data(), int(value.first.size())), value.second);
    }
    if (input.getPosition() != input.getTotalLength()) throw std::runtime_error("Tokenizer file size differs.");
}

void TextTokenizer::encodePiece(const juce::String& original, std::vector<int>& output) const {
    // This tokenizer represents spaces with the visible U+2581 marker.
    const auto text = original.replaceCharacter(' ', juce::juce_wchar(0x2581));
    std::vector<int> symbols;
    auto cursor = text.getCharPointer();
    while (!cursor.isEmpty()) {
        const auto character = juce::String::charToString(cursor.getAndAdvance()).toStdString();
        const auto found = vocabulary.find(character);
        if (found != vocabulary.end()) symbols.push_back(found->second);
        else {
            // Unknown Unicode characters fall back to their UTF-8 byte tokens.
            for (const char characterByte : character) {
                const auto byte = static_cast<unsigned char>(characterByte);
                const auto key = juce::String::formatted("<0x%02X>", unsigned(byte)).toStdString();
                const auto fallback = vocabulary.find(key);
                if (fallback == vocabulary.end()) throw std::runtime_error("Tokenizer byte fallback missing.");
                symbols.push_back(fallback->second);
            }
        }
    }
    // Repeatedly merge the adjacent pair with the lowest rank (highest priority).
    while (symbols.size() > 1) {
        size_t position = symbols.size();
        Merge best {std::numeric_limits<int>::max(), 0};
        for (size_t i = 0; i + 1 < symbols.size(); ++i) {
            const auto found = merges.find(pairKey(symbols[i], symbols[i+1]));
            if (found != merges.end() && found->second.rank < best.rank) {
                position = i;
                best = found->second;
            }
        }
        if (position == symbols.size()) break;
        symbols[position] = best.mergedToken;
        symbols.erase(symbols.begin() + std::ptrdiff_t(position + 1));
    }
    output.insert(output.end(), symbols.begin(), symbols.end());
}

TextTokenizer::Tokens TextTokenizer::encode(const juce::String& text) const {
    std::vector<int> result;
    int start = 0;
    while (start < text.length()) {
        int position = text.length();
        const std::pair<juce::String, int>* next = nullptr;
        // Added tokens bypass BPE; prefer the longest one at the same position.
        for (const auto& token : addedTokens) {
            const int found = text.indexOf(start, token.first);
            if (found >= 0 && (found < position || (found == position && next != nullptr && token.first.length() > next->first.length()))) {
                position = found;
                next = &token;
            }
        }
        encodePiece(text.substring(start, position), result);
        if (next == nullptr) break;
        result.push_back(next->second);
        start = position + next->first.length();
    }
    Tokens encoded;
    // The model always takes 256 positions. Only real tokens receive mask = 1.
    encoded.ids.fill(pad);
    for (size_t i = 0; i < std::min(result.size(), encoded.ids.size()); ++i) {
        encoded.ids[i] = result[i];
        encoded.mask[i] = 1;
    }
    return encoded;
}
