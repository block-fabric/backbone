// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <webui/bip39.h>

#include <crypto/hmac_sha512.h>
#include <crypto/sha256.h>
#include <support/cleanse.h>
#include <tinyformat.h>

#include <algorithm>
#include <cstring>

namespace webui::bip39 {

std::optional<int> IndexOf(std::string_view word)
{
    const auto& list{Wordlist()};
    const auto it{std::lower_bound(list.begin(), list.end(), word)};
    if (it == list.end() || *it != word) return std::nullopt;
    return static_cast<int>(it - list.begin());
}

bool ValidCount(size_t words)
{
    return words >= 12 && words <= 24 && words % 3 == 0;
}

Words FromEntropy(const Bytes& entropy)
{
    const size_t ent_bits{entropy.size() * 8};
    if (entropy.size() < 16 || entropy.size() > 32 || entropy.size() % 4 != 0) return {};
    const size_t cs_bits{ent_bits / 32};
    unsigned char hash[CSHA256::OUTPUT_SIZE];
    CSHA256().Write(entropy.data(), entropy.size()).Finalize(hash);
    // The entropy then the first bits of its hash, read 11 bits at a time.
    Bytes bits(entropy.begin(), entropy.end());
    bits.push_back(hash[0]);
    memory_cleanse(hash, sizeof(hash));
    const auto bit{[&](size_t i) { return (bits[i / 8] >> (7 - i % 8)) & 1; }};
    Words out;
    const size_t total{ent_bits + cs_bits};
    for (size_t i{0}; i < total; i += 11) {
        int index{0};
        for (size_t j{0}; j < 11; ++j) index = (index << 1) | bit(i + j);
        const std::string_view word{Wordlist()[index]};
        out.emplace_back(word.begin(), word.end());
    }
    return out;
}

SecureString Join(const Words& words)
{
    SecureString out;
    for (size_t i{0}; i < words.size(); ++i) {
        if (i) out += ' ';
        out += words[i];
    }
    return out;
}

Parsed Parse(std::string_view text)
{
    Parsed out;
    SecureString word;
    const auto flush{[&] {
        if (!word.empty()) out.words.push_back(word);
        if (!word.empty()) memory_cleanse(word.data(), word.size());
        word.clear();
    }};
    for (const char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            flush();
        } else {
            word += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
    }
    flush();
    std::vector<int> indexes;
    for (size_t i{0}; i < out.words.size(); ++i) {
        const auto index{IndexOf(std::string_view{out.words[i].data(), out.words[i].size()})};
        if (!index) {
            out.bad_word = static_cast<int>(i) + 1;
            out.error = strprintf("Word %d is not a word of the list: check its spelling.", i + 1);
            return out;
        }
        indexes.push_back(*index);
    }
    if (!ValidCount(out.words.size())) {
        out.error = strprintf("%d words: a backup phrase has 12 or 24 (sometimes 15, 18 or 21).", out.words.size());
        return out;
    }
    // Back to bits: the entropy, then the checksum, which must be the first bits of the entropy's hash.
    const size_t total{out.words.size() * 11};
    const size_t cs_bits{total / 33};
    const size_t ent_bits{total - cs_bits};
    Bytes bits((total + 7) / 8, 0);
    for (size_t w{0}; w < indexes.size(); ++w) {
        for (size_t j{0}; j < 11; ++j) {
            if ((indexes[w] >> (10 - j)) & 1) {
                const size_t i{w * 11 + j};
                bits[i / 8] |= static_cast<unsigned char>(1 << (7 - i % 8));
            }
        }
    }
    std::fill(indexes.begin(), indexes.end(), 0);
    Bytes entropy(bits.begin(), bits.begin() + ent_bits / 8);
    unsigned char hash[CSHA256::OUTPUT_SIZE];
    CSHA256().Write(entropy.data(), entropy.size()).Finalize(hash);
    const unsigned char mask{static_cast<unsigned char>(0xff << (8 - cs_bits))};
    const bool good{(bits[ent_bits / 8] & mask) == (hash[0] & mask)};
    memory_cleanse(hash, sizeof(hash));
    if (!good) {
        out.error = "These words do not check out: one of them is wrong, or two are swapped. Check each one against your paper.";
        return out;
    }
    out.entropy = std::move(entropy);
    out.ok = true;
    return out;
}

Bytes Pbkdf2Sha512(std::string_view password, std::string_view salt, uint32_t rounds, size_t length)
{
    Bytes out;
    out.reserve(length);
    const auto* key{reinterpret_cast<const unsigned char*>(password.data())};
    for (uint32_t block{1}; out.size() < length; ++block) {
        unsigned char index[4]{static_cast<unsigned char>(block >> 24), static_cast<unsigned char>(block >> 16), static_cast<unsigned char>(block >> 8), static_cast<unsigned char>(block)};
        unsigned char u[CHMAC_SHA512::OUTPUT_SIZE];
        unsigned char t[CHMAC_SHA512::OUTPUT_SIZE];
        CHMAC_SHA512(key, password.size()).Write(reinterpret_cast<const unsigned char*>(salt.data()), salt.size()).Write(index, sizeof(index)).Finalize(u);
        std::memcpy(t, u, sizeof(t));
        for (uint32_t r{1}; r < rounds; ++r) {
            CHMAC_SHA512(key, password.size()).Write(u, sizeof(u)).Finalize(u);
            for (size_t i{0}; i < sizeof(t); ++i) t[i] ^= u[i];
        }
        out.insert(out.end(), t, t + std::min(sizeof(t), length - out.size()));
        memory_cleanse(u, sizeof(u));
        memory_cleanse(t, sizeof(t));
    }
    return out;
}

bool ExtraOk(std::string_view extra)
{
    return extra.size() <= 256 && std::all_of(extra.begin(), extra.end(), [](char c) { return c >= ' ' && c <= '~'; });
}

Bytes Seed(const Words& words, std::string_view extra)
{
    const SecureString phrase{Join(words)};
    SecureString salt{"mnemonic"};
    salt.append(extra.begin(), extra.end());
    return Pbkdf2Sha512(std::string_view{phrase.data(), phrase.size()}, std::string_view{salt.data(), salt.size()}, 2048, 64);
}

} // namespace webui::bip39
