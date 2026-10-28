// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/bip39.h>

#include <crypto/hmac_sha512.h>
#include <crypto/sha256.h>
#include <key_io.h>
#include <random.h>
#include <script/descriptor.h>
#include <support/cleanse.h>
#include <tinyformat.h>
#include <util/strencodings.h>
#include <util/translation.h>
#include <wallet/bip39_english.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace wallet::bip39 {
namespace {
using SecureBytes = std::vector<unsigned char, secure_allocator<unsigned char>>;

//! The index of a word in the list, or -1.
int WordIndex(std::string_view word)
{
    const auto it{std::ranges::lower_bound(ENGLISH_WORDS, word)};
    return it != ENGLISH_WORDS.end() && *it == word ? static_cast<int>(it - ENGLISH_WORDS.begin()) : -1;
}

//! Bit `i` of the bytes, from the most significant bit of the first one.
bool Bit(std::span<const unsigned char> bytes, size_t i)
{
    return (bytes[i / 8] >> (7 - i % 8)) & 1;
}
} // namespace

bool IsWordCount(size_t words)
{
    return std::ranges::find(WORD_COUNTS, words) != WORD_COUNTS.end();
}

SecureString MnemonicFromEntropy(std::span<const unsigned char> entropy)
{
    if (entropy.size() < 16 || entropy.size() > 32 || entropy.size() % 4 != 0) return {};
    // The entropy, then the first ENT/32 bits of its SHA256: 11 bits a word.
    SecureBytes bytes(entropy.begin(), entropy.end());
    bytes.resize(entropy.size() + CSHA256::OUTPUT_SIZE);
    CSHA256().Write(entropy.data(), entropy.size()).Finalize(bytes.data() + entropy.size());
    const size_t words{(entropy.size() * 8 + entropy.size() / 4) / 11};
    SecureString phrase;
    phrase.reserve(words * 9);
    for (size_t w{0}; w < words; ++w) {
        unsigned index{0};
        for (size_t b{0}; b < 11; ++b) index = (index << 1) | Bit(bytes, w * 11 + b);
        if (w > 0) phrase += ' ';
        phrase += ENGLISH_WORDS[index];
    }
    return phrase;
}

SecureString GenerateMnemonic(size_t words)
{
    assert(IsWordCount(words));
    SecureBytes entropy(words * 4 / 3);
    GetStrongRandBytes(entropy);
    return MnemonicFromEntropy(entropy);
}

util::Result<SecureString> ParseMnemonic(std::string_view text)
{
    std::vector<int> indexes;
    SecureString phrase;
    SecureString word;
    const auto take_word{[&] {
        if (word.empty()) return;
        indexes.push_back(WordIndex(word));
        if (!phrase.empty()) phrase += ' ';
        phrase += word;
        word.clear();
    }};
    for (const char c : text) {
        if (IsSpace(c)) {
            take_word();
        } else {
            word += ToLower(c);
        }
    }
    take_word();
    const auto wipe{[&] { memory_cleanse(indexes.data(), indexes.size() * sizeof(int)); }};
    if (!IsWordCount(indexes.size())) {
        wipe();
        return util::Error{Untranslated(strprintf("A recovery phrase has 12, 15, 18, 21 or 24 words; this one has %u.", indexes.size()))};
    }
    for (size_t i{0}; i < indexes.size(); ++i) {
        if (indexes[i] < 0) {
            wipe();
            return util::Error{Untranslated(strprintf("Word %u of the recovery phrase is not one of the BIP39 English words: check it for typing errors.", i + 1))};
        }
    }
    // The bits of the words: the entropy, then its checksum.
    const size_t bits{indexes.size() * 11};
    const size_t checksum_bits{bits / 33};
    SecureBytes bytes((bits + 7) / 8);
    for (size_t i{0}; i < bits; ++i) {
        if ((indexes[i / 11] >> (10 - i % 11)) & 1) bytes[i / 8] |= 1 << (7 - i % 8);
    }
    wipe();
    const size_t entropy_size{(bits - checksum_bits) / 8};
    unsigned char hash[CSHA256::OUTPUT_SIZE];
    CSHA256().Write(bytes.data(), entropy_size).Finalize(hash);
    bool match{true};
    for (size_t i{0}; i < checksum_bits; ++i) {
        if (Bit(bytes, entropy_size * 8 + i) != Bit(hash, i)) match = false;
    }
    memory_cleanse(hash, sizeof(hash));
    if (!match) return util::Error{Untranslated("The checksum of the recovery phrase does not match: a word is mistyped, missing or out of place.")};
    return phrase;
}

bool IsAsciiPassphrase(std::string_view passphrase)
{
    return std::ranges::all_of(passphrase, [](char c) { return static_cast<unsigned char>(c) < 0x80; });
}

void MnemonicToSeed(std::string_view phrase, std::string_view passphrase, std::span<unsigned char, SEED_SIZE> seed)
{
    // PBKDF2 (RFC 8018) of HMAC-SHA512, with a key of the phrase: one block of 64 bytes, so the one
    // salt is "mnemonic" + passphrase + INT(1). The keyed HMAC is made once, and copied for each round.
    CHMAC_SHA512 keyed{reinterpret_cast<const unsigned char*>(phrase.data()), phrase.size()};
    SecureString salt{"mnemonic"};
    salt += passphrase;
    const unsigned char block[4]{0, 0, 0, 1};
    unsigned char u[CHMAC_SHA512::OUTPUT_SIZE];
    CHMAC_SHA512 hmac{keyed};
    hmac.Write(reinterpret_cast<const unsigned char*>(salt.data()), salt.size()).Write(block, sizeof(block)).Finalize(u);
    std::copy(u, u + SEED_SIZE, seed.begin());
    for (int i{1}; i < PBKDF2_ITERATIONS; ++i) {
        hmac = keyed;
        hmac.Write(u, sizeof(u)).Finalize(u);
        for (size_t j{0}; j < SEED_SIZE; ++j) seed[j] ^= u[j];
    }
    memory_cleanse(u, sizeof(u));
    memory_cleanse(&hmac, sizeof(hmac));
    memory_cleanse(&keyed, sizeof(keyed));
}

util::Result<CExtKey> MasterKeyFromMnemonic(std::string_view text, std::string_view passphrase)
{
    auto phrase{ParseMnemonic(text)};
    if (!phrase) return util::Error{util::ErrorString(phrase)};
    if (!IsAsciiPassphrase(passphrase)) {
        return util::Error{Untranslated("The BIP39 passphrase has characters that are not ASCII. Backbone takes ASCII passphrases only: BIP39 normalizes a passphrase in "
                                        "Unicode NFKD, which Backbone does not do, and a passphrase of other characters could give a seed other wallets do not.")};
    }
    SecureBytes seed(SEED_SIZE);
    MnemonicToSeed(*phrase, passphrase, std::span<unsigned char, SEED_SIZE>{seed.data(), SEED_SIZE});
    CExtKey master;
    master.SetSeed(MakeByteSpan(seed));
    return master;
}

int Purpose(OutputType type)
{
    switch (type) {
    case OutputType::LEGACY: return 44;
    case OutputType::P2SH_SEGWIT: return 49;
    case OutputType::BECH32: return 84;
    case OutputType::BECH32M: return 86;
    case OutputType::UNKNOWN: return 0;
    } // no default case, so the compiler can warn about missing cases
    return 0;
}

SecureString AccountDescriptor(const CExtKey& master, OutputType type, int coin_type, bool internal)
{
    // Made in locked memory: the descriptor has the master private key.
    std::string xprv{EncodeExtKey(master)};
    SecureString desc;
    switch (type) {
    case OutputType::LEGACY: desc = "pkh("; break;
    case OutputType::P2SH_SEGWIT: desc = "sh(wpkh("; break;
    case OutputType::BECH32: desc = "wpkh("; break;
    case OutputType::BECH32M: desc = "tr("; break;
    case OutputType::UNKNOWN: assert(false);
    } // no default case, so the compiler can warn about missing cases
    desc += xprv;
    memory_cleanse(xprv.data(), xprv.size());
    desc += strprintf("/%dh/%dh/0h/%d/*)", Purpose(type), coin_type, internal ? 1 : 0);
    if (type == OutputType::P2SH_SEGWIT) desc += ')';
    std::string text{desc.begin(), desc.end()};
    const std::string checksum{GetDescriptorChecksum(text)};
    memory_cleanse(text.data(), text.size());
    desc += '#';
    desc += checksum;
    return desc;
}

} // namespace wallet::bip39
