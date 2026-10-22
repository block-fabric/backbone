// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WEBUI_SEED_H
#define BITCOIN_WEBUI_SEED_H

#include <support/allocators/secure.h>
#include <webui/bip39.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct CExtKey;

/**
 * The single seed of the unified app (doc/unified-app.md §4.1, §23): one backup phrase for the app's own chain and every
 * sidechain it runs. This file holds the parts without a node: the file that keeps the seed (scrypt, then
 * ChaCha20-Poly1305), the account of each chain and the descriptors its wallet gets. The seed lives only in this
 * process (the host): never in a page, a pack or a daemon; a daemon gets the private key of its own account only.
 */
namespace webui::seed {

using bip39::Bytes;

//! What the account of a sidechain is made from (Account): fixed once a release has shipped with it, as every key of
//! every sidechain account depends on it. It names no product (Chains and Backbone share it: a phrase restored into either
//! gets the same sidechain accounts) and is versioned instead. Phrases made by the test builds of 2026-10-21 (whose tag was
//! "chains/account") derive other sidechain accounts (doc/unified-app.md §23.2).
inline constexpr std::string_view ACCOUNT_TAG{"sidechain/account/v1"};
//! What the passphrase of a wallet made from the seed is made from (WalletPassphrase): product-neutral and versioned too
//! (it was "unified-wallet/passphrase/1" in those test builds).
inline constexpr std::string_view WALLET_PASSPHRASE_LABEL{"wallet/passphrase/v1"};
//! The accounts of the app's own chain are below this: 0 its wallet, MINING_ACCOUNT the wallet that pays merged mining's
//! bids (doc §23.12). A sidechain's account is never one of them (Account).
inline constexpr uint32_t APP_ACCOUNTS{1024};
inline constexpr uint32_t MINING_ACCOUNT{1};
//! The work of the key of seed.dat: scrypt N = 2^15, r = 8, p = 1 (32 MiB, a tenth of a second or so).
inline constexpr uint64_t SCRYPT_N{uint64_t{1} << 15};
inline constexpr uint32_t SCRYPT_R{8};
inline constexpr uint32_t SCRYPT_P{1};
//! The least passphrase that encrypts the seed (and every wallet made from it).
inline constexpr size_t MIN_PASSPHRASE{8};
//! seed.dat without a password (the user's choice, doc §23.11): the same file, its key from an empty passphrase with the
//! least work the file takes. It protects nothing (anyone who can read the file has the seed) and says so.
inline constexpr uint64_t NO_PASSWORD_N{1024};
inline constexpr size_t MAX_PASSPHRASE{1024};

/** scrypt (RFC 7914). N a power of two from 2 to 2^22, r from 1 to 32, p from 1 to 16; else nothing. */
std::optional<Bytes> Scrypt(std::string_view password, std::span<const unsigned char> salt, uint64_t n, uint32_t r, uint32_t p, size_t length);

/**
 * Bitcoin Core's coin type (the user's decision, §12.1), as Core's own wallets take it (walletutil.cpp): 1 on a test
 * chain (CChainParams::IsTestChain: testnet, signet, regtest), else 0. Taken from the chain's parameters, not from a list
 * of networks: on the ecash branch eCash's main network and betanet are not test chains (0), its testnet is (1).
 */
uint32_t CoinType(bool test_chain);

/**
 * The account of a chain: 0 for the app's own chain (an empty stem); for a sidechain the first four bytes (big-endian) of
 * SHA-256("sidechain/account/v1" 0x00 stem 0x00 genesis), the genesis as RPC writes its hash (lower-case hex), the top bit
 * cleared (hardened); below APP_ACCOUNTS (the app's own chain's: its wallet 0, its mining wallet 1) it is moved up by
 * APP_ACCOUNTS. With the stem: two sidechains of one genesis (the template's regtest) get two accounts, and a daemon that
 * names another chain's genesis still gets an account of its own chain only.
 */
uint32_t Account(std::string_view stem, std::string_view genesis_hex);

/** The master key of a seed (BIP32). */
CExtKey Master(const Bytes& seed);
/** The fingerprint of the master key (hex, 8 characters): public, shown to tell phrases apart. */
std::string Fingerprint(const Bytes& seed);
/** An extended key in base58 (xprv on the main network, tprv on the test ones: every chain of the family uses those). */
SecureString EncodeXprv(const CExtKey& key, bool test_network);
std::string EncodeXpub(const CExtKey& key, bool test_network);

/** One descriptor of a chain's wallet: with its checksum; `internal` for the change. */
struct Descriptor {
    SecureString text;
    bool internal{false};
};
/**
 * The four descriptors of a chain's account: tr([fp/86h/Ch/Ah]xprv/0/<i>) and /1/<i> (taproot, the default), wpkh of
 * 84h/Ch/Ah, receive and change (ranged: <i> is "*" in the descriptor). The xprv is the account's (hardened): it derives no other account, nor the seed.
 */
std::vector<Descriptor> AccountDescriptors(const Bytes& seed, uint32_t coin, uint32_t account, bool test_network);
/** The same, public (xpubs): what a page may be told. */
std::vector<std::string> AccountPublic(const Bytes& seed, uint32_t coin, uint32_t account, bool test_network);

/**
 * The passphrase of a wallet made from the seed: hex(HMAC-SHA256(seed, "wallet/passphrase/v1" 0x00 stem 0x00 genesis)).
 * A sidechain's wallet always has its own (its stem): the user's own passphrase (the one of seed.dat) is never sent to a
 * sidechain's node, so a node, its logs or its RPC history never learn what protects the seed of every chain. Without a
 * password (doc §23.11) the app's own wallet has one too (an empty stem, its chain's genesis): no protection, as the seed
 * is read without a password, but a password can then be added and removed by changing that wallet's passphrase (Core
 * cannot take a wallet's encryption away, and encrypting a wallet once its keys are in makes it generate keys of its own
 * that the phrase would not bring back). Made only from the seed, in this process; wiped after use.
 */
SecureString WalletPassphrase(const Bytes& seed, std::string_view stem, std::string_view genesis_hex);

/** What seed.dat keeps: the entropy (the words come back from it) and the 64-byte seed (the extra word in it). */
struct Secret {
    Bytes entropy;
    Bytes seed;
};

/** The part of seed.dat that is not encrypted (bound to the secret: changing it fails the decryption). */
struct Header {
    int format{2};
    std::string network;
    int64_t created{0};
    //! A restored phrase: from about when the chains are read again (0: from their start).
    int64_t birthday{0};
    int words{12};
    bool extra{false};
    //! Encrypted with the user's password; false: with an empty one (NO_PASSWORD_N), which protects nothing.
    bool password{true};
    std::string fingerprint;
    uint64_t n{SCRYPT_N};
    uint32_t r{SCRYPT_R};
    uint32_t p{SCRYPT_P};
    std::string salt;
    std::string nonce;
};

/**
 * The text of seed.dat (JSON) for a secret under a passphrase. An empty passphrase is the file without a password:
 * header.password is set from it, and its key takes the least work (NO_PASSWORD_N). Nothing on a bad header.
 */
std::string Encrypt(const Secret& secret, std::string_view passphrase, Header header);

struct Opened {
    std::optional<Secret> secret;
    Header header;
    //! Why not: the file cannot be read, or the passphrase is wrong (wrong_passphrase).
    std::string error;
    bool wrong_passphrase{false};
};
/** The header of seed.dat, without the passphrase. */
std::optional<Header> ReadHeader(const std::string& text, std::string& error);
/** The secret of seed.dat. */
Opened Decrypt(const std::string& text, std::string_view passphrase);

/**
 * An error of a node about keys, shown to the user or logged: anything that looks like an extended private key (an xprv or
 * tprv, or a long run of base58) written out as "[a private key]".
 */
std::string Scrubbed(std::string_view text);

} // namespace webui::seed

#endif // BITCOIN_WEBUI_SEED_H
