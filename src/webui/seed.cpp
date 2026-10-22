// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <webui/seed.h>

#include <base58.h>
#include <crypto/common.h>
#include <crypto/chacha20poly1305.h>
#include <crypto/hmac_sha256.h>
#include <crypto/sha256.h>
#include <key.h>
#include <pubkey.h>
#include <random.h>
#include <script/descriptor.h>
#include <support/cleanse.h>
#include <tinyformat.h>
#include <univalue.h>
#include <util/strencodings.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstring>
#include <regex>

namespace webui::seed {
namespace {

Bytes Pbkdf2Sha256(std::string_view password, std::span<const unsigned char> salt, size_t length)
{
    // One round (as scrypt uses it).
    Bytes out;
    out.reserve(length);
    const auto* key{reinterpret_cast<const unsigned char*>(password.data())};
    for (uint32_t block{1}; out.size() < length; ++block) {
        unsigned char index[4]{static_cast<unsigned char>(block >> 24), static_cast<unsigned char>(block >> 16), static_cast<unsigned char>(block >> 8), static_cast<unsigned char>(block)};
        unsigned char t[CHMAC_SHA256::OUTPUT_SIZE];
        CHMAC_SHA256(key, password.size()).Write(salt.data(), salt.size()).Write(index, sizeof(index)).Finalize(t);
        out.insert(out.end(), t, t + std::min(sizeof(t), length - out.size()));
        memory_cleanse(t, sizeof(t));
    }
    return out;
}

inline uint32_t Rotl(uint32_t a, int b) { return (a << b) | (a >> (32 - b)); }

void Salsa20_8(uint32_t b[16])
{
    uint32_t x[16];
    std::memcpy(x, b, sizeof(x));
    for (int i{0}; i < 8; i += 2) {
        x[4] ^= Rotl(x[0] + x[12], 7);
        x[8] ^= Rotl(x[4] + x[0], 9);
        x[12] ^= Rotl(x[8] + x[4], 13);
        x[0] ^= Rotl(x[12] + x[8], 18);
        x[9] ^= Rotl(x[5] + x[1], 7);
        x[13] ^= Rotl(x[9] + x[5], 9);
        x[1] ^= Rotl(x[13] + x[9], 13);
        x[5] ^= Rotl(x[1] + x[13], 18);
        x[14] ^= Rotl(x[10] + x[6], 7);
        x[2] ^= Rotl(x[14] + x[10], 9);
        x[6] ^= Rotl(x[2] + x[14], 13);
        x[10] ^= Rotl(x[6] + x[2], 18);
        x[3] ^= Rotl(x[15] + x[11], 7);
        x[7] ^= Rotl(x[3] + x[15], 9);
        x[11] ^= Rotl(x[7] + x[3], 13);
        x[15] ^= Rotl(x[11] + x[7], 18);
        x[1] ^= Rotl(x[0] + x[3], 7);
        x[2] ^= Rotl(x[1] + x[0], 9);
        x[3] ^= Rotl(x[2] + x[1], 13);
        x[0] ^= Rotl(x[3] + x[2], 18);
        x[6] ^= Rotl(x[5] + x[4], 7);
        x[7] ^= Rotl(x[6] + x[5], 9);
        x[4] ^= Rotl(x[7] + x[6], 13);
        x[5] ^= Rotl(x[4] + x[7], 18);
        x[11] ^= Rotl(x[10] + x[9], 7);
        x[8] ^= Rotl(x[11] + x[10], 9);
        x[9] ^= Rotl(x[8] + x[11], 13);
        x[10] ^= Rotl(x[9] + x[8], 18);
        x[12] ^= Rotl(x[15] + x[14], 7);
        x[13] ^= Rotl(x[12] + x[15], 9);
        x[14] ^= Rotl(x[13] + x[12], 13);
        x[15] ^= Rotl(x[14] + x[13], 18);
    }
    for (int i{0}; i < 16; ++i) b[i] += x[i];
    memory_cleanse(x, sizeof(x));
}

//! scryptBlockMix: b of 2r blocks of 16 words; y as large, for the work.
void BlockMix(uint32_t* b, uint32_t* y, uint32_t r)
{
    uint32_t x[16];
    std::memcpy(x, b + (2 * r - 1) * 16, sizeof(x));
    for (uint32_t i{0}; i < 2 * r; ++i) {
        for (int k{0}; k < 16; ++k) x[k] ^= b[i * 16 + k];
        Salsa20_8(x);
        std::memcpy(y + i * 16, x, sizeof(x));
    }
    // The even blocks first, then the odd ones.
    for (uint32_t i{0}; i < r; ++i) std::memcpy(b + i * 16, y + (2 * i) * 16, 64);
    for (uint32_t i{0}; i < r; ++i) std::memcpy(b + (r + i) * 16, y + (2 * i + 1) * 16, 64);
    memory_cleanse(x, sizeof(x));
}

void RoMix(unsigned char* block, uint64_t n, uint32_t r)
{
    const size_t words{32 * size_t{r}};
    std::vector<uint32_t> x(words), y(words), v(words * n);
    for (size_t k{0}; k < words; ++k) x[k] = ReadLE32(block + 4 * k);
    for (uint64_t i{0}; i < n; ++i) {
        std::memcpy(v.data() + i * words, x.data(), words * 4);
        BlockMix(x.data(), y.data(), r);
    }
    for (uint64_t i{0}; i < n; ++i) {
        const uint64_t j{(uint64_t{x[(2 * r - 1) * 16]} | (uint64_t{x[(2 * r - 1) * 16 + 1]} << 32)) & (n - 1)};
        for (size_t k{0}; k < words; ++k) x[k] ^= v[j * words + k];
        BlockMix(x.data(), y.data(), r);
    }
    for (size_t k{0}; k < words; ++k) WriteLE32(block + 4 * k, x[k]);
    memory_cleanse(x.data(), x.size() * 4);
    memory_cleanse(y.data(), y.size() * 4);
    memory_cleanse(v.data(), v.size() * 4);
}

std::string Aad(const Header& h)
{
    return strprintf("chains-seed/%d\n%s\n%d\n%d\n%d\n%d\n%s\n%u\n%u\n%u\n%s\n%s", h.format, h.network, h.created, h.birthday, h.words, h.extra ? 1 : 0, h.fingerprint, h.n, h.r, h.p, h.salt, h.nonce);
}

AEADChaCha20Poly1305::Nonce96 NonceOf(std::span<const unsigned char> bytes)
{
    return {ReadLE32(bytes.data()), ReadLE64(bytes.data() + 4)};
}

std::optional<Bytes> KeyOf(std::string_view passphrase, const Header& h)
{
    const auto salt{TryParseHex<unsigned char>(h.salt)};
    if (!salt || salt->size() != 32) return std::nullopt;
    return Scrypt(passphrase, *salt, h.n, h.r, h.p, AEADChaCha20Poly1305::KEYLEN);
}

const UniValue& Field(const UniValue& o, const std::string& key)
{
    return o.isObject() ? o.find_value(key) : NullUniValue;
}

} // namespace

std::optional<Bytes> Scrypt(std::string_view password, std::span<const unsigned char> salt, uint64_t n, uint32_t r, uint32_t p, size_t length)
{
    if (n < 2 || n > (uint64_t{1} << 22) || !std::has_single_bit(n) || r < 1 || r > 32 || p < 1 || p > 16 || length < 1 || length > 1024) return std::nullopt;
    const size_t block{128 * size_t{r}};
    Bytes b{Pbkdf2Sha256(password, salt, block * p)};
    for (uint32_t i{0}; i < p; ++i) RoMix(b.data() + i * block, n, r);
    return Pbkdf2Sha256(password, b, length);
}

uint32_t CoinType(bool test_chain)
{
    return test_chain ? 1 : 0;
}

uint32_t Account(std::string_view stem, std::string_view genesis_hex)
{
    if (stem.empty()) return 0;
    std::string genesis{genesis_hex};
    std::ranges::transform(genesis, genesis.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const unsigned char zero{0};
    unsigned char hash[CSHA256::OUTPUT_SIZE];
    CSHA256()
        .Write(reinterpret_cast<const unsigned char*>(ACCOUNT_TAG.data()), ACCOUNT_TAG.size())
        .Write(&zero, 1)
        .Write(reinterpret_cast<const unsigned char*>(stem.data()), stem.size())
        .Write(&zero, 1)
        .Write(reinterpret_cast<const unsigned char*>(genesis.data()), genesis.size())
        .Finalize(hash);
    const uint32_t a{ReadBE32(hash) & 0x7fffffffU};
    return a == 0 ? 1 : a;
}

SecureString WalletPassphrase(const Bytes& seed, std::string_view stem, std::string_view genesis_hex)
{
    static constexpr std::string_view LABEL{"unified-wallet/passphrase/1"};
    std::string genesis{genesis_hex};
    std::ranges::transform(genesis, genesis.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const unsigned char zero{0};
    unsigned char mac[CHMAC_SHA256::OUTPUT_SIZE];
    CHMAC_SHA256(seed.data(), seed.size())
        .Write(reinterpret_cast<const unsigned char*>(LABEL.data()), LABEL.size())
        .Write(&zero, 1)
        .Write(reinterpret_cast<const unsigned char*>(stem.data()), stem.size())
        .Write(&zero, 1)
        .Write(reinterpret_cast<const unsigned char*>(genesis.data()), genesis.size())
        .Finalize(mac);
    std::string hex{HexStr(mac)};
    SecureString out{hex.begin(), hex.end()};
    memory_cleanse(hex.data(), hex.size());
    memory_cleanse(mac, sizeof(mac));
    return out;
}

CExtKey Master(const Bytes& seed)
{
    CExtKey master;
    master.SetSeed(std::as_bytes(std::span{seed}));
    return master;
}

std::string Fingerprint(const Bytes& seed)
{
    const KeyFingerprint fp{Master(seed).id_key_fingerprint()};
    return HexStr(fp);
}

SecureString EncodeXprv(const CExtKey& key, bool test_network)
{
    std::vector<unsigned char, secure_allocator<unsigned char>> data{test_network ? std::initializer_list<unsigned char>{0x04, 0x35, 0x83, 0x94} : std::initializer_list<unsigned char>{0x04, 0x88, 0xAD, 0xE4}};
    data.resize(4 + BIP32_EXTKEY_SIZE);
    key.Encode(data.data() + 4);
    std::string text{EncodeBase58Check(std::span<const unsigned char>{data.data(), data.size()})};
    SecureString out{text.begin(), text.end()};
    memory_cleanse(text.data(), text.size());
    return out;
}

std::string EncodeXpub(const CExtKey& key, bool test_network)
{
    std::vector<unsigned char> data{test_network ? std::vector<unsigned char>{0x04, 0x35, 0x87, 0xCF} : std::vector<unsigned char>{0x04, 0x88, 0xB2, 0x1E}};
    data.resize(4 + BIP32_EXTKEY_SIZE);
    key.Neuter().Encode(data.data() + 4);
    return EncodeBase58Check(data);
}

namespace {
//! The account key m/purpose'/coin'/account' and the origin of its descriptors ("[fp/86h/1h/0h]").
std::optional<std::pair<CExtKey, std::string>> AccountKey(const Bytes& seed, uint32_t purpose, uint32_t coin, uint32_t account)
{
    const CExtKey master{Master(seed)};
    CExtKey a, b, c;
    if (!master.Derive(a, purpose | 0x80000000U) || !a.Derive(b, coin | 0x80000000U) || !b.Derive(c, account | 0x80000000U)) return std::nullopt;
    const std::string origin{strprintf("[%s/%uh/%uh/%uh]", HexStr(master.id_key_fingerprint()), purpose, coin, account)};
    return std::make_pair(c, origin);
}
} // namespace

std::vector<Descriptor> AccountDescriptors(const Bytes& seed, uint32_t coin, uint32_t account, bool test_network)
{
    std::vector<Descriptor> out;
    for (const auto& [purpose, script] : {std::pair<uint32_t, std::string_view>{86, "tr"}, {84, "wpkh"}}) {
        const auto key{AccountKey(seed, purpose, coin, account)};
        if (!key) return {};
        const SecureString xprv{EncodeXprv(key->first, test_network)};
        for (const int branch : {0, 1}) {
            std::string text{strprintf("%s(%s%s/%d/*)", script, key->second, std::string{xprv.begin(), xprv.end()}, branch)};
            const std::string checksum{GetDescriptorChecksum(text)};
            text += "#" + checksum;
            out.push_back({SecureString{text.begin(), text.end()}, branch == 1});
            memory_cleanse(text.data(), text.size());
        }
    }
    return out;
}

std::vector<std::string> AccountPublic(const Bytes& seed, uint32_t coin, uint32_t account, bool test_network)
{
    std::vector<std::string> out;
    for (const auto& [purpose, script] : {std::pair<uint32_t, std::string_view>{86, "tr"}, {84, "wpkh"}}) {
        const auto key{AccountKey(seed, purpose, coin, account)};
        if (!key) return {};
        const std::string xpub{EncodeXpub(key->first, test_network)};
        for (const int branch : {0, 1}) {
            std::string text{strprintf("%s(%s%s/%d/*)", script, key->second, xpub, branch)};
            out.push_back(text + "#" + GetDescriptorChecksum(text));
        }
    }
    return out;
}

std::string Encrypt(const Secret& secret, std::string_view passphrase, Header header)
{
    header.format = 1;
    unsigned char salt[32];
    unsigned char nonce[12];
    GetStrongRandBytes(salt);
    GetStrongRandBytes(nonce);
    header.salt = HexStr(salt);
    header.nonce = HexStr(nonce);
    const auto key{KeyOf(passphrase, header)};
    if (!key) return {};
    Bytes plain;
    plain.push_back(static_cast<unsigned char>(secret.entropy.size()));
    plain.insert(plain.end(), secret.entropy.begin(), secret.entropy.end());
    plain.insert(plain.end(), secret.seed.begin(), secret.seed.end());
    std::vector<unsigned char> cipher(plain.size() + AEADChaCha20Poly1305::EXPANSION);
    const std::string aad{Aad(header)};
    AEADChaCha20Poly1305 aead{std::as_bytes(std::span{*key})};
    aead.Encrypt(std::as_bytes(std::span{plain}), std::as_bytes(std::span{aad}), NonceOf(nonce), std::as_writable_bytes(std::span{cipher}));
    UniValue kdf{UniValue::VOBJ};
    kdf.pushKV("name", "scrypt");
    kdf.pushKV("n", header.n);
    kdf.pushKV("r", static_cast<uint64_t>(header.r));
    kdf.pushKV("p", static_cast<uint64_t>(header.p));
    kdf.pushKV("salt", header.salt);
    UniValue out{UniValue::VOBJ};
    out.pushKV("format", header.format);
    out.pushKV("about", "The backup phrase of the Chains app, encrypted with the wallet passphrase (doc/unified-app.md §23). Never share this file; keep your 12 or 24 words on paper.");
    out.pushKV("network", header.network);
    out.pushKV("created", header.created);
    out.pushKV("birthday", header.birthday);
    out.pushKV("words", header.words);
    out.pushKV("extra", header.extra);
    out.pushKV("fingerprint", header.fingerprint);
    out.pushKV("kdf", std::move(kdf));
    out.pushKV("cipher", "chacha20-poly1305");
    out.pushKV("nonce", header.nonce);
    out.pushKV("secret", HexStr(cipher));
    memory_cleanse(salt, sizeof(salt));
    return out.write(1) + "\n";
}

std::optional<Header> ReadHeader(const std::string& text, std::string& error)
{
    UniValue o;
    if (!o.read(text) || !o.isObject()) {
        error = "The file of the backup phrase cannot be read.";
        return std::nullopt;
    }
    Header h;
    const UniValue kdf{Field(o, "kdf")};
    if (!Field(o, "format").isNum() || Field(o, "format").getInt<int>() != 1) {
        error = "The file of the backup phrase is of a version this app does not know.";
        return std::nullopt;
    }
    if (!Field(o, "network").isStr() || !Field(o, "created").isNum() || !Field(o, "words").isNum() || !Field(o, "fingerprint").isStr() || !Field(kdf, "name").isStr() ||
        Field(kdf, "name").get_str() != "scrypt" || !Field(kdf, "n").isNum() || !Field(kdf, "r").isNum() || !Field(kdf, "p").isNum() || !Field(kdf, "salt").isStr() ||
        !Field(o, "cipher").isStr() || Field(o, "cipher").get_str() != "chacha20-poly1305" || !Field(o, "nonce").isStr() || !Field(o, "secret").isStr()) {
        error = "The file of the backup phrase is damaged.";
        return std::nullopt;
    }
    h.network = Field(o, "network").get_str();
    h.created = Field(o, "created").getInt<int64_t>();
    h.birthday = Field(o, "birthday").isNum() ? Field(o, "birthday").getInt<int64_t>() : 0;
    h.words = Field(o, "words").getInt<int>();
    h.extra = Field(o, "extra").isTrue();
    h.fingerprint = Field(o, "fingerprint").get_str();
    // The work of the key within bounds: a damaged or tampered file never makes the app take gigabytes or hours.
    const int64_t n{Field(kdf, "n").getInt<int64_t>()};
    const int64_t r{Field(kdf, "r").getInt<int64_t>()};
    const int64_t p{Field(kdf, "p").getInt<int64_t>()};
    if (n < 1024 || n > (int64_t{1} << 20) || !std::has_single_bit(static_cast<uint64_t>(n)) || r < 1 || r > 16 || p < 1 || p > 4) {
        error = "The file of the backup phrase asks for a key of a work this app does not take.";
        return std::nullopt;
    }
    h.n = static_cast<uint64_t>(n);
    h.r = static_cast<uint32_t>(r);
    h.p = static_cast<uint32_t>(p);
    h.salt = Field(kdf, "salt").get_str();
    h.nonce = Field(o, "nonce").get_str();
    if (!bip39::ValidCount(h.words) || h.fingerprint.size() != 8 || !IsHex(h.fingerprint) || h.salt.size() != 64 || !IsHex(h.salt) || h.nonce.size() != 24 || !IsHex(h.nonce)) {
        error = "The file of the backup phrase is damaged.";
        return std::nullopt;
    }
    return h;
}

Opened Decrypt(const std::string& text, std::string_view passphrase)
{
    Opened out;
    std::string error;
    const auto header{ReadHeader(text, error)};
    if (!header) {
        out.error = error;
        return out;
    }
    out.header = *header;
    UniValue o;
    if (!o.read(text) || !o.find_value("secret").isStr()) {
        out.error = "The file of the backup phrase is damaged.";
        return out;
    }
    const auto cipher{TryParseHex<unsigned char>(o.find_value("secret").get_str())};
    const auto nonce{TryParseHex<unsigned char>(header->nonce)};
    const size_t entropy_size{static_cast<size_t>(header->words) / 3 * 4};
    if (!cipher || !nonce || cipher->size() != 1 + entropy_size + 64 + AEADChaCha20Poly1305::EXPANSION) {
        out.error = "The file of the backup phrase is damaged.";
        return out;
    }
    const auto key{KeyOf(passphrase, *header)};
    if (!key) {
        out.error = "The file of the backup phrase is damaged.";
        return out;
    }
    Bytes plain(cipher->size() - AEADChaCha20Poly1305::EXPANSION);
    const std::string aad{Aad(*header)};
    AEADChaCha20Poly1305 aead{std::as_bytes(std::span{*key})};
    if (!aead.Decrypt(std::as_bytes(std::span{*cipher}), std::as_bytes(std::span{aad}), NonceOf(*nonce), std::as_writable_bytes(std::span{plain}))) {
        // A wrong passphrase, or a file changed since it was written: the two look the same, on purpose.
        out.error = "The passphrase is not right.";
        out.wrong_passphrase = true;
        return out;
    }
    if (plain[0] != entropy_size) {
        out.error = "The file of the backup phrase is damaged.";
        return out;
    }
    Secret secret;
    secret.entropy.assign(plain.begin() + 1, plain.begin() + 1 + entropy_size);
    secret.seed.assign(plain.begin() + 1 + entropy_size, plain.end());
    if (Fingerprint(secret.seed) != header->fingerprint) {
        out.error = "The file of the backup phrase is damaged.";
        return out;
    }
    out.secret = std::move(secret);
    return out;
}

std::string Scrubbed(std::string_view text)
{
    static const std::regex KEY{"[xt]prv[1-9A-HJ-NP-Za-km-z]{20,}|[1-9A-HJ-NP-Za-km-z]{90,}"};
    return std::regex_replace(std::string{text}, KEY, "[a private key]");
}

} // namespace webui::seed
