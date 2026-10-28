// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_BIP39_H
#define BITCOIN_WALLET_BIP39_H

#include <key.h>
#include <outputtype.h>
#include <support/allocators/secure.h>
#include <util/result.h>

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

/**
 * BIP39 recovery phrases: the words that write down the seed of a wallet, and the seed they give.
 *
 * Only the English word list. BIP39 normalizes the phrase and the passphrase in Unicode NFKD
 * before they are hashed; for the English words and an ASCII passphrase NFKD changes nothing,
 * so a passphrase that is not ASCII is refused instead (IsAsciiPassphrase), rather than hashed
 * unnormalized into a seed no other wallet would find.
 *
 * Nothing here is logged: the phrase, the passphrase and the seed are secrets, and the errors
 * name a word by its place in the phrase, never the word.
 */
namespace wallet::bip39 {

/** The sizes of a phrase: 12 to 24 words, in steps of three (128 to 256 bits of entropy). */
inline constexpr std::array<size_t, 5> WORD_COUNTS{12, 15, 18, 21, 24};
/** The size of a seed, in bytes (PBKDF2-HMAC-SHA512, of 2048 iterations). */
inline constexpr size_t SEED_SIZE{64};
inline constexpr int PBKDF2_ITERATIONS{2048};

bool IsWordCount(size_t words);

/** The phrase of this entropy (16, 20, 24, 28 or 32 bytes), its words separated by one space; empty for another size. */
SecureString MnemonicFromEntropy(std::span<const unsigned char> entropy);

/** A new phrase of so many words (one of WORD_COUNTS), of GetStrongRandBytes entropy. */
SecureString GenerateMnemonic(size_t words);

/**
 * A phrase as it is typed: its words separated by any white space, in any case. Its words, its
 * size and its checksum are checked; the result is the phrase in its canonical form (lower case,
 * one space between words), which is what the seed is made of.
 */
util::Result<SecureString> ParseMnemonic(std::string_view text);

/** Whether a BIP39 passphrase is ASCII, the one form of it whose NFKD is itself. */
bool IsAsciiPassphrase(std::string_view passphrase);

/** The seed of a canonical phrase and a passphrase (BIP39: PBKDF2-HMAC-SHA512, salt "mnemonic" + passphrase). */
void MnemonicToSeed(std::string_view phrase, std::string_view passphrase, std::span<unsigned char, SEED_SIZE> seed);

/**
 * The BIP32 master key of a phrase as typed and a passphrase (empty if there is none): the
 * phrase is checked (ParseMnemonic), the passphrase must be ASCII. The seed does not outlive it.
 */
util::Result<CExtKey> MasterKeyFromMnemonic(std::string_view text, std::string_view passphrase);

/** The BIP purpose of the standard single-key descriptors of an output type: 44, 49, 84 or 86 (0 for none). */
int Purpose(OutputType type);

/**
 * The descriptor of account 0 of an output type, of this master key, with its checksum: pkh(), sh(wpkh()),
 * wpkh() or tr() of xprv/<purpose>h/<coin type>h/0h/<0 for receiving, 1 for change>, ranged.
 */
SecureString AccountDescriptor(const CExtKey& master, OutputType type, int coin_type, bool internal);

} // namespace wallet::bip39

#endif // BITCOIN_WALLET_BIP39_H
