// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WEBUI_BIP39_H
#define BITCOIN_WEBUI_BIP39_H

#include <support/allocators/secure.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * BIP39 backup phrases (doc/unified-app.md §23): the English word list, a phrase from entropy and back (its checksum
 * checked), and the 64-byte seed of a phrase (PBKDF2-HMAC-SHA512, 2048 rounds, salt "mnemonic" and the extra word).
 * Everything secret is kept in memory that is locked and wiped (SecureString, secure_allocator).
 */
namespace webui::bip39 {

inline constexpr size_t WORDS{2048};
const std::array<std::string_view, WORDS>& Wordlist();

using Bytes = std::vector<unsigned char, secure_allocator<unsigned char>>;
using Words = std::vector<SecureString, secure_allocator<SecureString>>;

/** The index of a word in the list, or nothing. */
std::optional<int> IndexOf(std::string_view word);

/** Whether a count of words is one of BIP39's: 12, 15, 18, 21 or 24. */
bool ValidCount(size_t words);

/** The words of 16, 20, 24, 28 or 32 bytes of entropy (12 to 24 words). */
Words FromEntropy(const Bytes& entropy);

/** The words joined by single spaces (as BIP39 hashes them). */
SecureString Join(const Words& words);

/** A phrase read back: its entropy when it is one, else why not (`bad_word`: the position, from 1, of a word not of the list). */
struct Parsed {
    Words words;
    Bytes entropy;
    bool ok{false};
    std::string error;
    int bad_word{0};
};
/**
 * The words of a text (any spaces between them, upper case taken as lower case, nothing else changed): each one of the
 * list, 12 to 24 of them, the checksum right.
 */
Parsed Parse(std::string_view text);

/** PBKDF2 with HMAC-SHA512 (RFC 8018). */
Bytes Pbkdf2Sha512(std::string_view password, std::string_view salt, uint32_t rounds, size_t length);

/**
 * The seed of a phrase: PBKDF2-HMAC-SHA512 of the words joined by spaces, salt "mnemonic" + the extra word, 2048 rounds,
 * 64 bytes. The extra word is taken as it is: printable ASCII only (Extra), so that no Unicode normalisation is needed and
 * every wallet reads it the same.
 */
Bytes Seed(const Words& words, std::string_view extra);

/** Whether an extra word can be used: printable ASCII (space to tilde), at most 256 characters. */
bool ExtraOk(std::string_view extra);

} // namespace webui::bip39

#endif // BITCOIN_WEBUI_BIP39_H
