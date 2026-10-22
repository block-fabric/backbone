// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The single seed of the unified app (doc/unified-app.md §23): BIP39 and BIP32 against their published vectors, scrypt
// against RFC 7914, our paths (the account of each chain) against a reference apart from this code
// (src/webui/test/e2e/seedref.py), seed.dat (round trip, wrong passphrase, tampering, bounds), the dialog's fields, and the
// SeedKeeper with nodes played by the test: what each chain's node is asked (a blank encrypted wallet, the descriptors of
// its own account, locked again), restores with their rescans, a passphrase changed on every wallet or none, the session.

#include <chainparams.h>
#include <crypto/sha256.h>
#include <key.h>
#include <key_io.h>
#include <script/descriptor.h>
#include <script/signingprovider.h>
#include <test/util/setup_common.h>
#include <util/fs.h>
#include <util/fs_helpers.h>
#include <util/strencodings.h>
#include <webui/bip39.h>
#include <webui/bridge.h>
#include <webui/dialog.h>
#include <webui/seed.h>
#include <webui/seedkeeper.h>
#include <webui/supervisor.h>

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#ifndef WIN32
#include <sys/stat.h>
#endif

using namespace webui;
using namespace std::chrono_literals;

namespace {

struct Bip39Vector {
    const char* entropy;
    const char* words;
    const char* seed;
};
// BIP39's English vectors (passphrase "TREZOR"), as trezor/python-mnemonic and rust-bip39 have them.
const Bip39Vector VECTORS[]{
    {"00000000000000000000000000000000", "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about", "c55257c360c07c72029aebc1b53c05ed0362ada38ead3e3e9efa3708e53495531f09a6987599d18264c1e1c92f2cf141630c7a3c4ab7c81b2f001698e7463b04"},
    {"7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f", "legal winner thank year wave sausage worth useful legal winner thank yellow", "2e8905819b8723fe2c1d161860e5ee1830318dbf49a83bd451cfb8440c28bd6fa457fe1296106559a3c80937a1c1069be3a3a5bd381ee6260e8d9739fce1f607"},
    {"80808080808080808080808080808080", "letter advice cage absurd amount doctor acoustic avoid letter advice cage above", "d71de856f81a8acc65e6fc851a38d4d7ec216fd0796d0a6827a3ad6ed5511a30fa280f12eb2e47ed2ac03b5c462a0358d18d69fe4f985ec81778c1b370b652a8"},
    {"ffffffffffffffffffffffffffffffff", "zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo wrong", "ac27495480225222079d7be181583751e86f571027b0497b5b5d11218e0a8a13332572917f0f8e5a589620c6f15b11c61dee327651a14c34e18231052e48c069"},
    {"000000000000000000000000000000000000000000000000", "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon agent", "035895f2f481b1b0f01fcf8c289c794660b289981a78f8106447707fdd9666ca06da5a9a565181599b79f53b844d8a71dd9f439c52a3d7b3e8a79c906ac845fa"},
    {"7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f", "legal winner thank year wave sausage worth useful legal winner thank year wave sausage worth useful legal will", "f2b94508732bcbacbcc020faefecfc89feafa6649a5491b8c952cede496c214a0c7b3c392d168748f2d4a612bada0753b52a1c7ac53c1e93abd5c6320b9e95dd"},
    {"808080808080808080808080808080808080808080808080", "letter advice cage absurd amount doctor acoustic avoid letter advice cage absurd amount doctor acoustic avoid letter always", "107d7c02a5aa6f38c58083ff74f04c607c2d2c0ecc55501dadd72d025b751bc27fe913ffb796f841c49b1d33b610cf0e91d3aa239027f5e99fe4ce9e5088cd65"},
    {"ffffffffffffffffffffffffffffffffffffffffffffffff", "zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo when", "0cd6e5d827bb62eb8fc1e262254223817fd068a74b5b449cc2f667c3f1f985a76379b43348d952e2265b4cd129090758b3e3c2c49103b5051aac2eaeb890a528"},
    {"0000000000000000000000000000000000000000000000000000000000000000", "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon art", "bda85446c68413707090a52022edd26a1c9462295029f2e60cd7c4f2bbd3097170af7a4d73245cafa9c3cca8d561a7c3de6f5d4a10be8ed2a5e608d68f92fcc8"},
    {"7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f", "legal winner thank year wave sausage worth useful legal winner thank year wave sausage worth useful legal winner thank year wave sausage worth title", "bc09fca1804f7e69da93c2f2028eb238c227f2e9dda30cd63699232578480a4021b146ad717fbb7e451ce9eb835f43620bf5c514db0f8add49f5d121449d3e87"},
    {"8080808080808080808080808080808080808080808080808080808080808080", "letter advice cage absurd amount doctor acoustic avoid letter advice cage absurd amount doctor acoustic avoid letter advice cage absurd amount doctor acoustic bless", "c0c519bd0e91a2ed54357d9d1ebef6f5af218a153624cf4f2da911a0ed8f7a09e2ef61af0aca007096df430022f7a2b6fb91661a9589097069720d015e4e982f"},
    {"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff", "zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo vote", "dd48c104698c30cfe2b6142103248622fb7bb0ff692eebb00089b32d22484e1613912f0a5b694407be899ffd31ed3992c456cdf60f5d4564b8ba3f05a69890ad"},
    {"9e885d952ad362caeb4efe34a8e91bd2", "ozone drill grab fiber curtain grace pudding thank cruise elder eight picnic", "274ddc525802f7c828d8ef7ddbcdc5304e87ac3535913611fbbfa986d0c9e5476c91689f9c8a54fd55bd38606aa6a8595ad213d4c9c9f9aca3fb217069a41028"},
    {"6610b25967cdcca9d59875f5cb50b0ea75433311869e930b", "gravity machine north sort system female filter attitude volume fold club stay feature office ecology stable narrow fog", "628c3827a8823298ee685db84f55caa34b5cc195a778e52d45f59bcf75aba68e4d7590e101dc414bc1bbd5737666fbbef35d1f1903953b66624f910feef245ac"},
    {"68a79eaca2324873eacc50cb9c6eca8cc68ea5d936f98787c60c7ebc74e6ce7c", "hamster diagram private dutch cause delay private meat slide toddler razor book happy fancy gospel tennis maple dilemma loan word shrug inflict delay length", "64c87cde7e12ecf6704ab95bb1408bef047c22db4cc7491c4271d170a1b213d20b385bc1588d9c7b38f1b39d415665b8a9030c9ec653d75e65f847d8fc1fc440"},
    {"c0ba5a8e914111210f2bd131f3d5e08d", "scheme spot photo card baby mountain device kick cradle pact join borrow", "ea725895aaae8d4c1cf682c1bfd2d358d52ed9f0f0591131b559e2724bb234fca05aa9c02c57407e04ee9dc3b454aa63fbff483a8b11de949624b9f1831a9612"},
    {"6d9be1ee6ebd27a258115aad99b7317b9c8d28b6d76431c3", "horn tenant knee talent sponsor spell gate clip pulse soap slush warm silver nephew swap uncle crack brave", "fd579828af3da1d32544ce4db5c73d53fc8acc4ddb1e3b251a31179cdb71e853c56d2fcb11aed39898ce6c34b10b5382772db8796e52837b54468aeb312cfc3d"},
    {"9f6a2878b2520799a44ef18bc7df394e7061a224d2c33cd015b157d746869863", "panda eyebrow bullet gorilla call smoke muffin taste mesh discover soft ostrich alcohol speed nation flash devote level hobby quick inner drive ghost inside", "72be8e052fc4919d2adf28d5306b5474b0069df35b02303de8c1729c9538dbb6fc2d731d5f832193cd9fb6aeecbc469594a70e3dd50811b5067f3b88b28c3e8d"},
    {"23db8160a31d3e0dca3688ed941adbf3", "cat swing flag economy stadium alone churn speed unique patch report train", "deb5f45449e615feff5640f2e49f933ff51895de3b4381832b3139941c57b59205a42480c52175b6efcffaa58a2503887c1e8b363a707256bdd2b587b46541f5"},
    {"8197a4a47f0425faeaa69deebc05ca29c0a5b5cc76ceacc0", "light rule cinnamon wrap drastic word pride squirrel upgrade then income fatal apart sustain crack supply proud access", "4cbdff1ca2db800fd61cae72a57475fdc6bab03e441fd63f96dabd1f183ef5b782925f00105f318309a7e9c3ea6967c7801e46c8a58082674c860a37b93eda02"},
    {"066dca1a2bb7e8a1db2832148ce9933eea0f3ac9548d793112d9a95c9407efad", "all hour make first leader extend hole alien behind guard gospel lava path output census museum junior mass reopen famous sing advance salt reform", "26e975ec644423f4a4c4f4215ef09b4bd7ef924e85d1d17c4cf3f136c2863cf6df0a475045652c57eb5fb41513ca2a2d67722b77e954b4b3fc11f7590449191d"},
    {"f30f8c1da665478f49b001d94c5fc452", "vessel ladder alter error federal sibling chat ability sun glass valve picture", "2aaa9242daafcee6aa9d7269f17d4efe271e1b9a529178d7dc139cd18747090bf9d60295d0ce74309a78852a9caadf0af48aae1c6253839624076224374bc63f"},
    {"c10ec20dc3cd9f652c7fac2f1230f7a3c828389a14392f05", "scissors invite lock maple supreme raw rapid void congress muscle digital elegant little brisk hair mango congress clump", "7b4a10be9d98e6cba265566db7f136718e1398c71cb581e1b2f464cac1ceedf4f3e274dc270003c670ad8d02c4558b2f8e39edea2775c9e232c7cb798b069e88"},
    {"f585c11aec520db57dd353c69554b21a89b20fb0650966fa0a9d6f74fd989d8f", "void come effort suffer camp survey warrior heavy shoot primary clutch crush open amazing screen patrol group space point ten exist slush involve unfold", "01f5bced59dec48e362f2c45b5de68b9fd6c92c6634f44d6d40aab69056506f0e35524a518034ddc1192e1dacd32c1ed3eaa3c3b131c88ed8e7e54c49a5d0998"},
};

constexpr const char* ABOUT{"abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"};
//! The genesis of the template's sidechains on regtest (Bitcoin's regtest genesis): every one of them has it.
constexpr const char* REGTEST_GENESIS{"0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"};

bip39::Bytes Hex(const std::string& hex)
{
    const auto bytes{ParseHex(hex)};
    return bip39::Bytes(bytes.begin(), bytes.end());
}

std::string Text(const SecureString& s)
{
    return std::string{s.begin(), s.end()};
}

std::string Joined(const bip39::Words& words)
{
    return Text(bip39::Join(words));
}

bip39::Bytes SeedOf(const std::string& words, const std::string& extra = "")
{
    const auto parsed{bip39::Parse(words)};
    BOOST_REQUIRE(parsed.ok);
    return bip39::Seed(parsed.words, extra);
}

/** The x-only output key of the first address of a descriptor (tr) or its key hash (wpkh), by Core's own descriptors. */
std::string FirstProgram(const std::string& descriptor)
{
    FlatSigningProvider keys;
    std::string error;
    auto parsed{Parse(descriptor, keys, error, /*require_checksum=*/true)};
    BOOST_REQUIRE_MESSAGE(!parsed.empty(), error);
    std::vector<CScript> scripts;
    FlatSigningProvider out;
    BOOST_REQUIRE(parsed[0]->Expand(0, keys, scripts, out));
    BOOST_REQUIRE_EQUAL(scripts.size(), 1U);
    std::vector<unsigned char> program;
    int version;
    BOOST_REQUIRE(scripts[0].IsWitnessProgram(version, program));
    return HexStr(program);
}

struct RegtestSetup : BasicTestingSetup {
    RegtestSetup() : BasicTestingSetup{ChainType::REGTEST} {}
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(webui_seed_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(bip39_wordlist_and_vectors)
{
    // The list of bitcoin/bips, byte for byte: its lines each with a line feed hash to the published SHA-256.
    std::string list;
    for (const auto w : bip39::Wordlist()) list += std::string{w} + "\n";
    unsigned char hash[CSHA256::OUTPUT_SIZE];
    CSHA256().Write(reinterpret_cast<const unsigned char*>(list.data()), list.size()).Finalize(hash);
    BOOST_CHECK_EQUAL(HexStr(hash), "2f5eed53a4727b4bf8880d8f3f199efc90e58503646d9ff8eff3a2ed3b24dbda");
    BOOST_CHECK(std::is_sorted(bip39::Wordlist().begin(), bip39::Wordlist().end()));
    BOOST_CHECK_EQUAL(*bip39::IndexOf("abandon"), 0);
    BOOST_CHECK_EQUAL(*bip39::IndexOf("zoo"), 2047);
    BOOST_CHECK(!bip39::IndexOf("bitcoin"));

    for (const Bip39Vector& v : VECTORS) {
        const bip39::Words words{bip39::FromEntropy(Hex(v.entropy))};
        BOOST_CHECK_EQUAL(Joined(words), v.words);
        const bip39::Parsed parsed{bip39::Parse(v.words)};
        BOOST_REQUIRE_MESSAGE(parsed.ok, parsed.error);
        BOOST_CHECK_EQUAL(HexStr(parsed.entropy), v.entropy);
        BOOST_CHECK_EQUAL(HexStr(bip39::Seed(parsed.words, "TREZOR")), v.seed);
    }
    // The root key of the first vector, as BIP39's vectors give it.
    BOOST_CHECK_EQUAL(Text(seed::EncodeXprv(seed::Master(SeedOf(ABOUT, "TREZOR")), false)),
                      "xprv9s21ZrQH143K3h3fDYiay8mocZ3afhfULfb5GX8kCBdno77K4HiA15Tg23wpbeF1pLfs1c5SPmYHrEpTuuRhxMwvKDwqdKiGJS9XFKzUsAF");
    // PBKDF2-HMAC-SHA512 apart (RFC 6070's style; the value from Python's hashlib).
    BOOST_CHECK_EQUAL(HexStr(bip39::Pbkdf2Sha512("password", "salt", 1, 64)),
                      "867f70cf1ade02cff3752599a3a53dc4af34c7a669815ae5d513554e1c8cf252c02d470a285a0501bad999bfe943c08f050235d7d68b1da55e63f73b60a57fce");
}

BOOST_AUTO_TEST_CASE(bip39_reading_typed_phrases)
{
    // Spaces of any kind, upper case: the same phrase.
    const bip39::Parsed loose{bip39::Parse("  Legal winner\tthank year wave\nsausage worth useful legal winner thank YELLOW  ")};
    BOOST_REQUIRE(loose.ok);
    BOOST_CHECK_EQUAL(HexStr(loose.entropy), "7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f");
    // A word not of the list: which one.
    const bip39::Parsed typo{bip39::Parse("legal winner thank year wave sausage worth usefull legal winner thank yellow")};
    BOOST_CHECK(!typo.ok);
    BOOST_CHECK_EQUAL(typo.bad_word, 8);
    BOOST_CHECK(typo.error.find("Word 8") != std::string::npos);
    // Two swapped: the checksum says so.
    const bip39::Parsed swapped{bip39::Parse("winner legal thank year wave sausage worth useful legal winner thank yellow")};
    BOOST_CHECK(!swapped.ok);
    BOOST_CHECK_EQUAL(swapped.bad_word, 0);
    BOOST_CHECK(swapped.error.find("do not check out") != std::string::npos);
    BOOST_CHECK(!bip39::Parse("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon").ok);
    // Counts: 12 to 24 by threes.
    BOOST_CHECK(!bip39::Parse("abandon abandon abandon").ok);
    BOOST_CHECK(!bip39::Parse("").ok);
    for (const size_t n : {12, 15, 18, 21, 24}) BOOST_CHECK(bip39::ValidCount(n));
    for (const size_t n : {0, 11, 13, 25, 27}) BOOST_CHECK(!bip39::ValidCount(n));
    // The entropy of every length makes a phrase that reads back.
    for (const size_t bytes : {16, 20, 24, 28, 32}) {
        bip39::Bytes entropy(bytes, 0x5a);
        const bip39::Words words{bip39::FromEntropy(entropy)};
        BOOST_CHECK_EQUAL(words.size(), bytes * 3 / 4);
        BOOST_CHECK(bip39::Parse(Joined(words)).entropy == entropy);
    }
    BOOST_CHECK(bip39::FromEntropy(bip39::Bytes(15, 0)).empty());
    // The extra word: printable ASCII only.
    BOOST_CHECK(bip39::ExtraOk(""));
    BOOST_CHECK(bip39::ExtraOk("TREZOR horse ~!"));
    BOOST_CHECK(!bip39::ExtraOk("caf\xc3\xa9"));
    BOOST_CHECK(!bip39::ExtraOk(std::string(257, 'a')));
}

BOOST_AUTO_TEST_CASE(scrypt_rfc7914)
{
    const auto run{[](std::string_view p, std::string_view s, uint64_t n, uint32_t r, uint32_t q) {
        const auto out{seed::Scrypt(p, std::span{reinterpret_cast<const unsigned char*>(s.data()), s.size()}, n, r, q, 64)};
        BOOST_REQUIRE(out);
        return HexStr(*out);
    }};
    BOOST_CHECK_EQUAL(run("", "", 16, 1, 1), "77d6576238657b203b19ca42c18a0497f16b4844e3074ae8dfdffa3fede21442fcd0069ded0948f8326a753a0fc81f17e8d3e0fb2e0d3628cf35e20c38d18906");
    BOOST_CHECK_EQUAL(run("password", "NaCl", 1024, 8, 16), "fdbabe1c9d3472007856e7190d01e9fe7c6ad7cbc8237830e77376634b3731622eaf30d92e22a3886ff109279d9830dac727afb94a83ee6d8360cbdfa2cc0640");
    BOOST_CHECK_EQUAL(run("pleaseletmein", "SodiumChloride", 16384, 8, 1), "7023bdcb3afd7348461c06cd81fd38ebfda8fbba904f8e3ea9b543f6545da1f2d5432955613f0fcf62d49705242a9af9e61e85dc0d651e40dfcf017b45575887");
    // Out of bounds: nothing.
    const unsigned char salt[1]{0};
    BOOST_CHECK(!seed::Scrypt("x", salt, 1000, 8, 1, 32));
    BOOST_CHECK(!seed::Scrypt("x", salt, uint64_t{1} << 23, 8, 1, 32));
    BOOST_CHECK(!seed::Scrypt("x", salt, 1024, 0, 1, 32));
    BOOST_CHECK(!seed::Scrypt("x", salt, 1024, 8, 17, 32));
}

BOOST_AUTO_TEST_CASE(bip84_bip86_vectors_on_the_main_network)
{
    // The vectors of BIP84 and BIP86 (the phrase "abandon ... about", no extra word): our accounts are these paths.
    const bip39::Bytes s{SeedOf(ABOUT)};
    BOOST_CHECK_EQUAL(seed::Fingerprint(s), "73c5da0a");
    const auto pub{seed::AccountPublic(s, 0, 0, false)};
    BOOST_REQUIRE_EQUAL(pub.size(), 4U);
    BOOST_CHECK(pub[0].starts_with("tr([73c5da0a/86h/0h/0h]xpub6BgBgsespWvERF3LHQu6CnqdvfEvtMcQjYrcRzx53QJjSxarj2afYWcLteoGVky7D3UKDP9QyrLprQ3VCECoY49yfdDEHGCtMMj92pReUsQ/0/*)#"));
    BOOST_CHECK(pub[2].starts_with("wpkh([73c5da0a/84h/0h/0h]xpub6CatWdiZiodmUeTDp8LT5or8nmbKNcuyvz7WyksVFkKB4RHwCD3XyuvPEbvqAQY3rAPshWcMLoP2fMFMKHPJ4ZeZXYVUhLv1VMrjPC7PW6V/0/*)#"));
    // m/86'/0'/0'/0/0: bc1p5cyxnuxmeuwuvkwfem96lqzszd02n6xdcjrs20cac6yqjjwudpxqkedrcr; m/84'/0'/0'/0/0: bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu.
    const auto priv{seed::AccountDescriptors(s, 0, 0, false)};
    BOOST_REQUIRE_EQUAL(priv.size(), 4U);
    BOOST_CHECK(Text(priv[0].text).find("xprv9xgqHN7yz9MwCkxsBPN5qetuNdQSUttZNKw1dcYTV4mkaAFiBVGQziHs3NRSWMkCzvgjEe3n9xV8oYywvM8at9yRqyaZVz6TYYhX98VjsUk") != std::string::npos);
    BOOST_CHECK_EQUAL(FirstProgram(Text(priv[0].text)), "a60869f0dbcf1dc659c9cecbaf8050135ea9e8cdc487053f1dc6880949dc684c");
    BOOST_CHECK_EQUAL(FirstProgram(Text(priv[2].text)), "c0cebcd6c3d3ca8c75dc5ec62ebe55330ef910e2");
    // Receive then change, each kind.
    BOOST_CHECK(!priv[0].internal);
    BOOST_CHECK(priv[1].internal);
    BOOST_CHECK(Text(priv[1].text).starts_with("tr([73c5da0a/86h/0h/0h]xprv"));
    BOOST_CHECK(Text(priv[1].text).find("/1/*)#") != std::string::npos);
    BOOST_CHECK(!priv[2].internal);
    BOOST_CHECK(priv[3].internal);
}

BOOST_FIXTURE_TEST_CASE(our_paths_per_chain, RegtestSetup)
{
    // The coin type of Bitcoin Core (the user's decision): 0 on main, 1 on every test network.
    BOOST_CHECK_EQUAL(seed::CoinType(false), 0U);
    BOOST_CHECK_EQUAL(seed::CoinType(true), 1U);
    BOOST_CHECK_EQUAL(seed::CoinType(Params().IsTestChain()), 1U);
    BOOST_CHECK_EQUAL(seed::CoinType(CreateChainParams(m_args, ChainType::MAIN)->IsTestChain()), 0U);
    for (const ChainType c : {ChainType::TESTNET, ChainType::SIGNET}) BOOST_CHECK_EQUAL(seed::CoinType(CreateChainParams(m_args, c)->IsTestChain()), 1U);
    // The app's own chain is account 0; a sidechain's from its id and genesis (vectors of seedref.py).
    BOOST_CHECK_EQUAL(seed::Account("", REGTEST_GENESIS), 0U);
    BOOST_CHECK_EQUAL(seed::Account("layers", REGTEST_GENESIS), 756096351U);
    BOOST_CHECK_EQUAL(seed::Account("thunder", REGTEST_GENESIS), 954058073U);
    // The genesis as RPC writes it, in any case.
    BOOST_CHECK_EQUAL(seed::Account("layers", "0F9188F13CB7B2C71F2A335E3A4FC328BF5BEB436012AFCA590B1A11466E2206"), 756096351U);
    // Two sidechains of one genesis (every template chain on regtest) get two accounts; another genesis another account.
    BOOST_CHECK(seed::Account("layers", REGTEST_GENESIS) != seed::Account("thunder", REGTEST_GENESIS));
    BOOST_CHECK(seed::Account("layers", REGTEST_GENESIS) != seed::Account("layers", std::string(64, '1')));
    // Never an account of the app's own chain (0 its wallet, 1 its mining wallet: below 1024); the stems of seedref.py.
    for (const auto& [stem, account] : std::vector<std::pair<const char*, uint32_t>>{{"layers", 756096351}, {"thunder", 954058073}, {"bitassets", 1219916544}, {"bitnames", 1270392304},
                                                                                    {"zside", 563665917}, {"hivemind", 330684049}, {"fastblocks", 368603874}, {"flush", 31868005}}) {
        const uint32_t a{seed::Account(stem, REGTEST_GENESIS)};
        BOOST_CHECK_EQUAL(a, account);
        BOOST_CHECK(a >= seed::APP_ACCOUNTS && a < 0x80000000U);
    }
    // A hash that falls below 1024 is moved above the app's accounts (this genesis gives 804 for Layers: seedref.py).
    BOOST_CHECK_EQUAL(seed::Account("layers", "0000000000000000000000000000000000000000000000000000000000070ee8"), 804U + seed::APP_ACCOUNTS);
    const bip39::Bytes s{SeedOf(ABOUT)};
    const auto chains{seed::AccountPublic(s, 1, 0, true)};
    BOOST_CHECK(chains[0].starts_with("tr([73c5da0a/86h/1h/0h]tpubDDfvzhdVV4unsoKt5aE6dcsNsfeWbTgmLZPi8LQDYU2xixrYemMfWJ3BaVneH3u7DBQePdTwhpybaKRU95pi6PMUtLPBJLVQRpzEnjfjZzX/0/*)#"));
    const auto layers{seed::AccountPublic(s, 1, 756096351, true)};
    BOOST_CHECK(layers[0].starts_with("tr([73c5da0a/86h/1h/756096351h]tpubDDfvzhdYRMnJH9p4K9hmRXkAJb2mRxRGDatU4LN2TZKehj3JVbgz1hRxbVYFdmEe69WiU4DW5zweu4qZhg4rDPGvuV2HgXy1mvu3L6mfsNB/0/*)#"));
    const auto thunder{seed::AccountPublic(s, 1, 954058073, true)};
    BOOST_CHECK(thunder[0].starts_with("tr([73c5da0a/86h/1h/954058073h]tpubDDfvzhdZBwkYn6UJhmC2F4wVREVoNdgfCwT4wqiw7FG43n3Z1K4hiQjBAL9o7TVQrGg5LZdEc2bjkKvqFMg3qLHfsA9CLzAxRqhuWPVRRof/0/*)#"));
    // The first address of each, by Core's descriptors from the private ones a node gets.
    BOOST_CHECK_EQUAL(FirstProgram(Text(seed::AccountDescriptors(s, 1, 0, true)[0].text)), "3b82b2b2a9185315da6f80da5f06d0440d8a5e1457fa93387c2d919c86ec8786");
    BOOST_CHECK_EQUAL(FirstProgram(Text(seed::AccountDescriptors(s, 1, 756096351, true)[0].text)), "4e3c94ecfaa1d22ad7fa37b61e01a5b64eb2f630ca5166296319b6ea544c8666");
    BOOST_CHECK_EQUAL(FirstProgram(Text(seed::AccountDescriptors(s, 1, 954058073, true)[0].text)), "c03e43c44389415edc7bdb102b4e920e42e68375b7e84a6eda2b0f0173cf4608");
    // What a node gets holds its account's key only (depth 3, hardened): not the master's.
    const std::string master_xprv{Text(seed::EncodeXprv(seed::Master(s), true))};
    for (const auto& d : seed::AccountDescriptors(s, 1, 756096351, true)) {
        BOOST_CHECK(Text(d.text).find(master_xprv) == std::string::npos);
        BOOST_CHECK(Text(d.text).find("tprv") != std::string::npos);
    }
    // The mining wallet's account (1) of the app's own chain (seedref.py).
    BOOST_CHECK(seed::AccountPublic(s, 1, seed::MINING_ACCOUNT, true)[0].starts_with("tr([73c5da0a/86h/1h/1h]tpubDDfvzhdVV4unxTr3yqNZkM2u5c8u9ecmsdWK6bFzZ4hE2tbjhSjhDa1dzsVymqrr19pZ43YK9c5CQwp7fcTNwHZg59Yt6Mdsu5myM4oUX42/0/*)#"));
    BOOST_CHECK_EQUAL(FirstProgram(Text(seed::AccountDescriptors(s, 1, seed::MINING_ACCOUNT, true)[0].text)), "f34fac8695b01c225b2ad605ae70d129d7b767a97b3bf801788f7960a94e60a0");
    // The passphrase of a wallet made from the seed (seedref.py wallet_passphrase): a sidechain's, and the app's own wallet's
    // without a password (an empty stem).
    BOOST_CHECK_EQUAL(Text(seed::WalletPassphrase(s, "", REGTEST_GENESIS)), "a6bf89c1da56d3a95b89e880778893509a4a685887311f159e503eb2a87b9e56");
    BOOST_CHECK_EQUAL(Text(seed::WalletPassphrase(s, "layers", REGTEST_GENESIS)), "7bb89fdf7fee2d711745f541a54b03794f2a531addccea7ea033639e39eaec5a");
    BOOST_CHECK(Text(seed::WalletPassphrase(s, "thunder", REGTEST_GENESIS)) != Text(seed::WalletPassphrase(s, "layers", REGTEST_GENESIS)));
    // The public descriptors are the private ones without their keys (what a page may be told).
    const auto priv{seed::AccountDescriptors(s, 1, 0, true)};
    for (size_t i{0}; i < priv.size(); ++i) {
        FlatSigningProvider keys;
        std::string error;
        auto parsed{Parse(Text(priv[i].text), keys, error, true)};
        BOOST_REQUIRE(!parsed.empty());
        BOOST_CHECK_EQUAL(parsed[0]->ToString(), chains[i]);
    }
}

BOOST_AUTO_TEST_CASE(seed_file_encrypted)
{
    seed::Secret secret;
    secret.entropy = Hex("7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f");
    secret.seed = SeedOf("legal winner thank year wave sausage worth useful legal winner thank yellow");
    seed::Header header;
    header.network = "regtest";
    header.created = 1792600000;
    header.birthday = 1792600000;
    header.words = 12;
    header.fingerprint = seed::Fingerprint(secret.seed);
    // A lighter key for the test (the app uses 2^15).
    header.n = 1024;
    const std::string text{seed::Encrypt(secret, "correct horse", header)};
    BOOST_REQUIRE(!text.empty());
    // No word, no key, no seed in the file.
    BOOST_CHECK(text.find("legal") == std::string::npos);
    BOOST_CHECK(text.find(HexStr(secret.seed)) == std::string::npos);
    BOOST_CHECK(text.find(HexStr(secret.entropy)) == std::string::npos);
    std::string error;
    const auto read{seed::ReadHeader(text, error)};
    BOOST_REQUIRE_MESSAGE(read, error);
    BOOST_CHECK_EQUAL(read->fingerprint, header.fingerprint);
    BOOST_CHECK_EQUAL(read->words, 12);
    BOOST_CHECK_EQUAL(read->n, 1024U);

    const seed::Opened opened{seed::Decrypt(text, "correct horse")};
    BOOST_REQUIRE_MESSAGE(opened.secret, opened.error);
    BOOST_CHECK(opened.secret->entropy == secret.entropy);
    BOOST_CHECK(opened.secret->seed == secret.seed);
    // Each time another salt and nonce: two files of one secret differ.
    BOOST_CHECK(seed::Encrypt(secret, "correct horse", header) != text);

    const seed::Opened wrong{seed::Decrypt(text, "correct horses")};
    BOOST_CHECK(!wrong.secret);
    BOOST_CHECK(wrong.wrong_passphrase);
    // The header is bound: another network, birthday or fingerprint fails as a wrong passphrase would.
    for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{{"\"regtest\"", "\"main\""}, {"\"birthday\": 1792600000", "\"birthday\": 0"}}) {
        std::string tampered{text};
        const size_t at{tampered.find(from)};
        BOOST_REQUIRE(at != std::string::npos);
        tampered.replace(at, from.size(), to);
        BOOST_CHECK(!seed::Decrypt(tampered, "correct horse").secret);
    }
    // The ciphertext changed: refused.
    {
        UniValue o;
        BOOST_REQUIRE(o.read(text));
        std::string cipher{o.find_value("secret").get_str()};
        cipher[10] = cipher[10] == '0' ? '1' : '0';
        std::string tampered{text};
        tampered.replace(tampered.find(o.find_value("secret").get_str()), cipher.size(), cipher);
        BOOST_CHECK(!seed::Decrypt(tampered, "correct horse").secret);
    }
    // A key of a work the app does not take (a tampered file asking for gigabytes): refused before any work.
    {
        std::string tampered{text};
        tampered.replace(tampered.find("\"n\": 1024"), 9, "\"n\": 1073741824");
        BOOST_CHECK(!seed::ReadHeader(tampered, error));
        BOOST_CHECK(!seed::Decrypt(tampered, "correct horse").secret);
    }
    BOOST_CHECK(!seed::ReadHeader("{}", error));
    BOOST_CHECK(!seed::ReadHeader("not json", error));
    // Keys in an error of a node: never shown or logged.
    BOOST_CHECK_EQUAL(seed::Scrubbed("key 'tprv8ZgxMBicQKsPd7Uf69XL1XwhmjHopUGep8GuEiJDZmbQz6o58LninorQAfcKZWARbtRtfnLcJ5MQ2AtHcQJCCRUcMRvmDUjyEmNUWwx8UbK' is not valid"), "key '[a private key]' is not valid");
    BOOST_CHECK_EQUAL(seed::Scrubbed("Wallet is locked"), "Wallet is locked");
}

BOOST_AUTO_TEST_CASE(wallet_setup_at_the_daemons)
{
    // The wallet setup of the phrase goes as the daemon's own user, its methods listed apart: no spend, no key read out.
    // The app's user keeps its whitelist of phase 2 (no wallet unlock, no import: supervisor_tests).
    const auto& setup{SidechainSupervisor::SetupRpcMethods()};
    for (const char* m : {"sendtoaddress", "sendmany", "dumpwallet", "listdescriptors", "dumpprivkey", "signmessage", "walletpassphrasechange", "createwithdrawal", "webuihost"}) {
        BOOST_CHECK_MESSAGE(std::ranges::find(setup, m) == setup.end(), m);
    }
    for (const char* m : {"createwallet", "importdescriptors", "walletpassphrase", "walletlock"}) BOOST_CHECK(std::ranges::find(setup, m) != setup.end());
    const auto& app{SidechainSupervisor::AppRpcMethods()};
    for (const char* m : {"importdescriptors", "walletpassphrase", "walletpassphrasechange"}) BOOST_CHECK(std::ranges::find(app, m) == app.end());
}

BOOST_AUTO_TEST_CASE(dialog_fields_words_and_box)
{
    dialog::Request request;
    request.title = "Check your backup";
    dialog::Field word;
    word.id = "word-3";
    word.kind = dialog::Field::Kind::WORD;
    word.label = "Word 3";
    dialog::Field day;
    day.id = "day";
    day.kind = dialog::Field::Kind::DATE;
    day.label = "When";
    day.optional = true;
    request.fields = {word, day};
    request.checkbox = "I wrote them down";
    request.words = std::make_shared<std::vector<SecureString>>(std::vector<SecureString>{"abandon", "about"});
    // Only the dialog's window can ask it: the dialog of the system has no fields, words or box.
    BOOST_CHECK(dialog::NeedsThemedWindow(request));
    BOOST_CHECK(!dialog::NeedsThemedWindow(dialog::Request{}));
    const UniValue page{dialog::PageData(request)};
    BOOST_CHECK_EQUAL(page.find_value("words").size(), 2U);
    BOOST_CHECK_EQUAL(page.find_value("fields")[0].find_value("kind").get_str(), "word");
    BOOST_CHECK_EQUAL(page.find_value("fields")[1].find_value("optional").get_bool(), true);
    BOOST_CHECK_EQUAL(page.find_value("checkbox").get_str(), "I wrote them down");
    const auto answer{[&](const std::string& json) {
        dialog::Session s{request};
        BOOST_REQUIRE_EQUAL(s.Call(R"(["hello"])").status, 0);
        BOOST_REQUIRE_EQUAL(s.Call(R"(["ready",{"width":400,"height":300}])").status, 0);
        const auto reply{s.Call(json)};
        return std::make_pair(reply.status, reply.status == 0 ? s.TakeAnswer() : dialog::Answer{});
    }};
    // Filled, ticked: taken.
    auto [ok, a] = answer(R"(["answer",{"accept":true,"fields":{"word-3":"thank"},"checked":true}])");
    BOOST_CHECK_EQUAL(ok, 0);
    BOOST_CHECK(a.accepted && a.checked);
    BOOST_CHECK_EQUAL(Text(a.fields["word-3"]), "thank");
    BOOST_CHECK(!a.fields.contains("day"));
    // Not ticked, a field missing, a field not asked for, too long, a field when declined: refused.
    BOOST_CHECK(answer(R"(["answer",{"accept":true,"fields":{"word-3":"thank"}}])").first != 0);
    BOOST_CHECK(answer(R"(["answer",{"accept":true,"fields":{},"checked":true}])").first != 0);
    BOOST_CHECK(answer(R"(["answer",{"accept":true,"fields":{"word-3":"a","other":"b"},"checked":true}])").first != 0);
    BOOST_CHECK(answer(R"(["answer",{"accept":true,"fields":{"word-3":")" + std::string(1100, 'a') + R"("},"checked":true}])").first != 0);
    BOOST_CHECK(answer(R"(["answer",{"accept":false,"fields":{"word-3":"a"}}])").first != 0);
    auto [declined, d] = answer(R"(["answer",{"accept":false}])");
    BOOST_CHECK_EQUAL(declined, 0);
    BOOST_CHECK(!d.accepted && d.fields.empty());
}

namespace {

/** Nodes played by the test: what each is asked, its wallets, and what it answers. */
struct FakeNodes {
    std::mutex mutex;
    std::map<std::string, std::vector<std::string>> dirs;                  // id -> wallets in its folder
    std::map<std::string, std::map<std::string, std::string>> passphrases;  // id -> wallet -> passphrase
    std::map<std::string, std::vector<std::pair<std::string, UniValue>>> calls;
    std::map<std::string, std::string> genesis;
    std::set<std::string> refuse_change;
    //! id -> wallet -> how long it was last opened for (walletpassphrase), 0 when locked.
    std::map<std::string, std::map<std::string, int64_t>> opened_for;
    bool running{true};

    UniValue Call(const std::string& id, const std::string& method, const UniValue& params, const std::string& wallet)
    {
        std::lock_guard lock{mutex};
        calls[id].emplace_back(method + (wallet.empty() ? "" : "@" + wallet), params);
        if (method == "getblockhash") return genesis.contains(id) ? genesis[id] : std::string{REGTEST_GENESIS};
        if (method == "listwalletdir") {
            UniValue list{UniValue::VARR};
            for (const auto& name : dirs[id]) {
                UniValue w{UniValue::VOBJ};
                w.pushKV("name", name);
                list.push_back(w);
            }
            UniValue out{UniValue::VOBJ};
            out.pushKV("wallets", list);
            return out;
        }
        if (method == "listwallets") {
            UniValue out{UniValue::VARR};
            for (const auto& name : dirs[id]) out.push_back(name);
            return out;
        }
        if (method == "createwallet") {
            dirs[id].push_back(params[0].get_str());
            passphrases[id][params[0].get_str()] = params.size() > 3 ? params[3].get_str() : std::string{};
            opened_for[id][params[0].get_str()] = 0;
            return UniValue{UniValue::VOBJ};
        }
        if (method == "walletpassphrase") {
            if (passphrases[id][wallet] != params[0].get_str()) throw SidechainSupervisor::RpcError(-14, "The wallet passphrase entered was incorrect.");
            opened_for[id][wallet] = params[1].getInt<int64_t>();
            return UniValue{};
        }
        if (method == "walletlock") {
            opened_for[id][wallet] = 0;
            return UniValue{};
        }
        if (method == "walletpassphrasechange") {
            if (refuse_change.contains(id) || passphrases[id][wallet] != params[0].get_str()) throw SidechainSupervisor::RpcError(-14, "The wallet passphrase entered was incorrect.");
            passphrases[id][wallet] = params[1].get_str();
            return UniValue{};
        }
        if (method == "importdescriptors") {
            UniValue out{UniValue::VARR};
            for (size_t i{0}; i < params[0].size(); ++i) {
                UniValue r{UniValue::VOBJ};
                r.pushKV("success", true);
                out.push_back(r);
            }
            return out;
        }
        if (method == "getwalletinfo") {
            UniValue info{UniValue::VOBJ};
            // As Core says it: unlocked_until for an encrypted wallet only (0: locked).
            if (!passphrases[id][wallet].empty()) info.pushKV("unlocked_until", opened_for[id][wallet] ? GetTime() + opened_for[id][wallet] : 0);
            return info;
        }
        return UniValue{};
    }
    int64_t OpenedFor(const std::string& id, const std::string& wallet)
    {
        std::lock_guard lock{mutex};
        return opened_for[id][wallet];
    }
    std::string PassphraseOf(const std::string& id, const std::string& wallet)
    {
        std::lock_guard lock{mutex};
        return passphrases[id][wallet];
    }
    /** Whether any request to a node carried a text (a passphrase) anywhere in its parameters. */
    bool Carried(const std::string& id, const std::string& text)
    {
        std::lock_guard lock{mutex};
        for (const auto& [m, p] : calls[id]) {
            if (p.write().find(text) != std::string::npos) return true;
        }
        return false;
    }
    std::vector<std::string> Methods(const std::string& id)
    {
        std::lock_guard lock{mutex};
        std::vector<std::string> out;
        for (const auto& [m, p] : calls[id]) out.push_back(m);
        return out;
    }
    UniValue Params(const std::string& id, const std::string& method)
    {
        std::lock_guard lock{mutex};
        for (const auto& [m, p] : calls[id]) {
            if (m == method) return p;
        }
        return {};
    }
};

struct KeeperFixture : RegtestSetup {
    fs::path dir{m_args.GetDataDirBase() / "seedkeeper"};
    FakeNodes nodes;
    UniValue records{UniValue::VOBJ};
    std::mutex records_mutex;
    std::vector<std::string> shared{"layers"};
    //! The questions asked (their kind), and how each is answered.
    std::vector<std::string> asked;
    std::function<dialog::Answer(const Window::ConfirmDialog&)> answer;

    SeedKeeper::Hooks Hooks(std::chrono::seconds session = 600s)
    {
        fs::create_directories(dir);
        SeedKeeper::Hooks h;
        h.file = dir / "seed.dat";
        h.chain = ChainType::REGTEST;
        h.test_chain = true;
        h.node_ready = [] { return true; };
        h.node_rpc = [this](const std::string& method, const UniValue& params, const std::string& wallet) { return nodes.Call("chains", method, params, wallet); };
        h.side_rpc = [this](const std::string& stem, const std::string& method, const UniValue& params, const std::string& wallet, std::chrono::milliseconds) { return nodes.Call(stem, method, params, wallet); };
        h.side_running = [this](const std::string&) { return nodes.running; };
        h.shared_chains = [this] { return shared; };
        h.separate_chains = [] { return std::vector<std::string>{}; };
        h.name = [](const std::string& stem) { return stem == "layers" ? std::string{"Layers"} : stem; };
        h.ask = [this](const Window::ConfirmDialog& d) {
            asked.push_back(d.kind);
            return answer(d);
        };
        h.load = [this] {
            std::lock_guard lock{records_mutex};
            return records;
        };
        h.store = [this](const UniValue& v) {
            std::lock_guard lock{records_mutex};
            records = v;
        };
        h.session = session;
        return h;
    }
    UniValue WaitReady(SeedKeeper& keeper, const std::string& id)
    {
        for (int i{0}; i < 200; ++i) {
            const UniValue status{keeper.Status()};
            for (const UniValue& c : status.find_value("chains").getValues()) {
                if (c.find_value("id").get_str() == id && (c.find_value("state").get_str() == "ready" || c.find_value("state").get_str() == "error")) return c;
            }
            std::this_thread::sleep_for(25ms);
        }
        BOOST_FAIL("not ready: " + id);
        return {};
    }
};

dialog::Answer Accept(std::map<std::string, std::string> fields = {}, const std::string& passphrase = {})
{
    dialog::Answer a;
    a.accepted = true;
    a.checked = true;
    for (const auto& [k, v] : fields) a.fields[k].assign(v.begin(), v.end());
    a.passphrase.assign(passphrase.begin(), passphrase.end());
    return a;
}

} // namespace

BOOST_FIXTURE_TEST_CASE(create_makes_every_chains_wallet_from_the_phrase, KeeperFixture)
{
    std::vector<std::string> words;
    int checks{0};
    int passes{0};
    nodes.dirs["layers"] = {"main"}; // the wallet it had before the phrase: kept, never touched
    answer = [&](const Window::ConfirmDialog& d) -> dialog::Answer {
        if (d.kind == "seed-words") {
            BOOST_REQUIRE(d.words);
            BOOST_CHECK_EQUAL(d.words->size(), 12U);
            BOOST_CHECK(!d.checkbox.empty());
            // The words are shown here only: never in the text, the rows or the details.
            for (const auto& w : *d.words) words.push_back(Text(w));
            BOOST_CHECK(d.rows.empty());
            return Accept();
        }
        if (d.kind == "seed-check") {
            BOOST_REQUIRE_EQUAL(d.fields.size(), 3U);
            std::map<std::string, std::string> typed;
            for (const auto& f : d.fields) {
                const int n{std::stoi(f.id.substr(5))};
                typed[f.id] = checks == 0 ? std::string{"zoo"} : " " + words[n - 1] + " ";
            }
            if (checks == 1) {
                // The wrong ones said at their field.
                BOOST_CHECK(!d.fields[0].error.empty());
            }
            ++checks;
            return Accept(typed);
        }
        if (d.kind == "seed-passphrase") {
            ++passes;
            if (passes == 1) return Accept({{"passphrase", "short"}, {"repeat", "short"}});
            if (passes == 2) {
                BOOST_CHECK(!d.fields[0].error.empty());
                return Accept({{"passphrase", "correct horse"}, {"repeat", "correct hors"}});
            }
            BOOST_CHECK(!d.fields[1].error.empty());
            return Accept({{"passphrase", "correct horse"}, {"repeat", "correct horse"}});
        }
        BOOST_FAIL("unexpected question " + d.kind);
        return {};
    };
    SeedKeeper keeper{Hooks()};
    BOOST_CHECK(!keeper.Exists());
    BOOST_CHECK(!keeper.WalletOf("layers"));
    UniValue params{UniValue::VOBJ};
    params.pushKV("words", 12);
    const UniValue made{keeper.Create(params)};
    BOOST_CHECK(made.find_value("created").isTrue());
    BOOST_CHECK_EQUAL(checks, 2);
    BOOST_CHECK(keeper.Exists());
#ifndef WIN32
    struct stat st;
    BOOST_REQUIRE_EQUAL(stat(fs::PathToString(dir / "seed.dat").c_str(), &st), 0);
    BOOST_CHECK_EQUAL(st.st_mode & 0777, 0600);
#endif
    std::string phrase;
    for (const auto& w : words) phrase += (phrase.empty() ? "" : " ") + w;
    const bip39::Bytes seed_bytes{SeedOf(phrase)};
    BOOST_CHECK_EQUAL(made.find_value("fingerprint").get_str(), seed::Fingerprint(seed_bytes));

    const UniValue chains{WaitReady(keeper, "chains")};
    BOOST_CHECK_EQUAL(chains.find_value("state").get_str(), "ready");
    BOOST_CHECK_EQUAL(chains.find_value("wallet").get_str(), "main");
    const UniValue layers{WaitReady(keeper, "layers")};
    BOOST_CHECK_EQUAL(layers.find_value("state").get_str(), "ready");
    // Layers had a "main": the phrase's is "seed", the old one untouched.
    BOOST_CHECK_EQUAL(layers.find_value("wallet").get_str(), "seed");
    BOOST_CHECK_EQUAL(*keeper.WalletOf("layers"), "seed");
    BOOST_CHECK_EQUAL(keeper.MadeWallet("layers"), "seed");

    // The app's node: its genesis, a blank wallet encrypted with the password, unlocked, the four descriptors of account 0,
    // locked.
    BOOST_CHECK((nodes.Methods("chains") == std::vector<std::string>{"getblockhash", "listwalletdir", "createwallet", "walletpassphrase@main", "importdescriptors@main", "walletlock@main"}));
    const UniValue create{nodes.Params("chains", "createwallet")};
    BOOST_CHECK_EQUAL(create[0].get_str(), "main");
    BOOST_CHECK_EQUAL(create[1].get_bool(), false);
    BOOST_CHECK_EQUAL(create[2].get_bool(), true);
    // Wiped once sent: the test's node kept a copy, the keeper's own is gone (its value here is the test's copy).
    const UniValue imported{nodes.Params("chains", "importdescriptors@main")};
    BOOST_REQUIRE_EQUAL(imported[0].size(), 4U);
    const auto expected{seed::AccountDescriptors(seed_bytes, 1, 0, true)};
    for (size_t i{0}; i < 4; ++i) {
        BOOST_CHECK_EQUAL(imported[0][i].find_value("desc").get_str(), Text(expected[i].text));
        BOOST_CHECK_EQUAL(imported[0][i].find_value("internal").get_bool(), expected[i].internal);
        BOOST_CHECK(imported[0][i].find_value("active").get_bool());
        // A new phrase: nothing to read again.
        BOOST_CHECK_EQUAL(imported[0][i].find_value("timestamp").get_str(), "now");
    }
    // Layers' node: its genesis asked, its own account (never 0, never another chain's), its own wallet.
    BOOST_CHECK((nodes.Methods("layers") == std::vector<std::string>{"getblockhash", "listwalletdir", "createwallet", "walletpassphrase@seed", "importdescriptors@seed", "walletlock@seed"}));
    const UniValue side{nodes.Params("layers", "importdescriptors@seed")};
    const auto expected_layers{seed::AccountDescriptors(seed_bytes, 1, seed::Account("layers", REGTEST_GENESIS), true)};
    for (size_t i{0}; i < 4; ++i) BOOST_CHECK_EQUAL(side[0][i].find_value("desc").get_str(), Text(expected_layers[i].text));
    // Layers' wallet has a passphrase of its own, made from the seed: the user's never reaches a sidechain's node (§13.5 #46).
    BOOST_CHECK_EQUAL(nodes.passphrases["layers"]["seed"], Text(seed::WalletPassphrase(seed_bytes, "layers", REGTEST_GENESIS)));
    BOOST_CHECK_EQUAL(nodes.passphrases["layers"]["seed"].size(), 64U);
    BOOST_CHECK(!nodes.Carried("layers", "correct horse"));
    BOOST_CHECK_EQUAL(nodes.passphrases["chains"]["main"], "correct horse");

    // The status says which wallet and how: never a word, a key or the seed.
    const std::string status{keeper.Status().write()};
    for (const auto& w : words) BOOST_CHECK(status.find("\"" + w + "\"") == std::string::npos);
    BOOST_CHECK(status.find("prv") == std::string::npos);
    BOOST_CHECK(status.find(HexStr(seed_bytes)) == std::string::npos);
    BOOST_CHECK(keeper.Status().find_value("unlocked").isTrue());
    BOOST_CHECK(keeper.Status().find_value("password").isTrue());
    BOOST_CHECK(keeper.HasPassword());
    keeper.Lock();
    BOOST_CHECK(keeper.Status().find_value("unlocked").isFalse());
    // With a password the wallets stay locked: nothing opens them by itself.
    BOOST_CHECK_EQUAL(nodes.OpenedFor("chains", "main"), 0);
    BOOST_CHECK_EQUAL(nodes.OpenedFor("layers", "seed"), 0);
    BOOST_CHECK(!keeper.OpenWithoutPassword("main"));
    BOOST_CHECK(keeper.PassphraseWithout("layers", "seed").empty());

    // A second phrase is refused: one per data folder.
    BOOST_CHECK_THROW(keeper.Create(params), HostError);
    keeper.Stop();
}

BOOST_FIXTURE_TEST_CASE(restore_reads_each_chain_again_and_a_later_chain_waits_for_the_passphrase, KeeperFixture)
{
    const std::string phrase{"legal winner thank year wave sausage worth useful legal winner thank yellow"};
    int restores{0};
    shared = {};
    answer = [&](const Window::ConfirmDialog& d) -> dialog::Answer {
        if (d.kind == "seed-restore") {
            ++restores;
            if (restores == 1) return Accept({{"words", "legal winner thank year wave sausage worth usefull legal winner thank yellow"}});
            if (restores == 2) {
                // The words typed come back to be corrected, with the word said.
                BOOST_CHECK(d.fields[0].error.find("Word 8") != std::string::npos);
                BOOST_CHECK_EQUAL(Text(d.fields[0].value), "legal winner thank year wave sausage worth usefull legal winner thank yellow");
                return Accept({{"words", phrase}, {"day", "next tuesday"}});
            }
            BOOST_CHECK(d.fields[0].error.empty());
            BOOST_CHECK(!d.fields[1].error.empty());
            return Accept({{"words", phrase}, {"day", "2026-10-02"}, {"extra", "TREZOR"}});
        }
        if (d.kind == "seed-passphrase") return Accept({{"passphrase", "correct horse"}, {"repeat", "correct horse"}});
        if (d.kind == "seed-unlock") return Accept({}, "correct horse");
        BOOST_FAIL("unexpected question " + d.kind);
        return {};
    };
    SeedKeeper keeper{Hooks()};
    const UniValue done{keeper.Restore(UniValue{UniValue::VOBJ})};
    BOOST_CHECK(done.find_value("restored").isTrue());
    BOOST_CHECK_EQUAL(restores, 3);
    BOOST_CHECK(keeper.Status().find_value("restored").isTrue());
    BOOST_CHECK_EQUAL(WaitReady(keeper, "chains").find_value("state").get_str(), "ready");
    // Read again from a day before the day given (2026-10-02 00:00 UTC is 1790899200).
    const UniValue imported{nodes.Params("chains", "importdescriptors@main")};
    BOOST_CHECK_EQUAL(imported[0][0].find_value("timestamp").getInt<int64_t>(), 1790899200 - 86400);
    // The extra word is in the seed: the keys are those of the phrase with "TREZOR".
    BOOST_CHECK_EQUAL(imported[0][0].find_value("desc").get_str(), Text(seed::AccountDescriptors(SeedOf(phrase, "TREZOR"), 1, 0, true)[0].text));

    // A sidechain turned on later, once the phrase is locked: it waits for its keys, its node makes none of its own.
    keeper.Lock();
    shared = {"layers"};
    BOOST_CHECK_EQUAL(*keeper.WalletOf("layers"), "");
    keeper.ChainReady("layers");
    bool waits{false};
    const UniValue listed{keeper.Status().find_value("chains")};
    for (const UniValue& c : listed.getValues()) {
        if (c.find_value("id").get_str() == "layers") waits = c.find_value("state").get_str() == "needs-keys";
    }
    BOOST_CHECK(waits);
    BOOST_CHECK(nodes.Methods("layers").empty());
    // Unlocked for it: made at once, read again from the same day; then the phrase is locked again by itself.
    UniValue params{UniValue::VOBJ};
    UniValue stems{UniValue::VARR};
    stems.push_back("layers");
    params.pushKV("stems", stems);
    BOOST_CHECK(keeper.Unlock(params).find_value("unlocked").isTrue());
    BOOST_CHECK_EQUAL(WaitReady(keeper, "layers").find_value("state").get_str(), "ready");
    BOOST_CHECK_EQUAL(nodes.Params("layers", "importdescriptors@main")[0][0].find_value("timestamp").getInt<int64_t>(), 1790899200 - 86400);
    for (int i{0}; i < 100 && keeper.Status().find_value("unlocked").isTrue(); ++i) std::this_thread::sleep_for(20ms);
    BOOST_CHECK(keeper.Status().find_value("unlocked").isFalse());
    keeper.Stop();
}

BOOST_FIXTURE_TEST_CASE(a_daemon_naming_another_genesis_gets_an_account_of_its_own_chain, KeeperFixture)
{
    // A compromised Thunder node says it has Layers' genesis: it gets an account of Thunder's for that genesis, never Layers'.
    shared = {"thunder"};
    nodes.genesis["thunder"] = REGTEST_GENESIS;
    answer = [&](const Window::ConfirmDialog& d) -> dialog::Answer {
        if (d.kind == "seed-restore") return Accept({{"words", ABOUT}});
        if (d.kind == "seed-passphrase") return Accept({{"passphrase", "correct horse"}, {"repeat", "correct horse"}});
        return {};
    };
    SeedKeeper keeper{Hooks()};
    keeper.Restore(UniValue{UniValue::VOBJ});
    WaitReady(keeper, "thunder");
    const std::string got{nodes.Params("thunder", "importdescriptors@main")[0][0].find_value("desc").get_str()};
    const bip39::Bytes s{SeedOf(ABOUT)};
    BOOST_CHECK(got != Text(seed::AccountDescriptors(s, 1, seed::Account("layers", REGTEST_GENESIS), true)[0].text));
    BOOST_CHECK(got != Text(seed::AccountDescriptors(s, 1, 0, true)[0].text));
    BOOST_CHECK_EQUAL(got, Text(seed::AccountDescriptors(s, 1, seed::Account("thunder", REGTEST_GENESIS), true)[0].text));
    keeper.Stop();
}

BOOST_FIXTURE_TEST_CASE(the_users_passphrase_never_reaches_a_sidechain, KeeperFixture)
{
    answer = [&](const Window::ConfirmDialog& d) -> dialog::Answer {
        if (d.kind == "seed-restore") return Accept({{"words", ABOUT}});
        if (d.kind == "seed-passphrase") return Accept({{"passphrase", "correct horse"}, {"repeat", "correct horse"}});
        if (d.kind == "seed-change") {
            if (!d.fields[0].error.empty()) return dialog::Answer{};
            return Accept({{"old", "correct horse"}, {"passphrase", "battery staple"}, {"repeat", "battery staple"}});
        }
        if (d.kind == "seed-unlock") return Accept({}, d.error.empty() ? "wrong one" : "battery staple");
        return {};
    };
    SeedKeeper keeper{Hooks()};
    keeper.Restore(UniValue{UniValue::VOBJ});
    WaitReady(keeper, "chains");
    WaitReady(keeper, "layers");
    keeper.Lock();
    const std::string layers_pass{Text(seed::WalletPassphrase(SeedOf(ABOUT), "layers", REGTEST_GENESIS))};
    BOOST_CHECK_EQUAL(nodes.passphrases["layers"]["main"], layers_pass);
    BOOST_CHECK(!nodes.Carried("layers", "correct horse"));

    // A spend on Layers: the user types their own passphrase in the trusted dialog; Layers' node gets its own instead.
    BOOST_CHECK_EQUAL(Text(keeper.PassphraseFor("layers", SecureString{"correct horse"})), layers_pass);
    // A passphrase of another wallet (one of its own) is sent as typed; a chain without a wallet of the phrase gets none.
    BOOST_CHECK_EQUAL(Text(keeper.PassphraseFor("layers", SecureString{"another wallet's"})), "another wallet's");
    BOOST_CHECK_EQUAL(Text(keeper.PassphraseFor("thunder", SecureString{"correct horse"})), "");

    // The app's own wallet refuses: seed.dat keeps the old one, nothing changed.
    nodes.refuse_change = {"chains"};
    BOOST_CHECK_THROW(keeper.ChangePassphrase(), HostError);
    BOOST_CHECK_EQUAL(nodes.passphrases["chains"]["main"], "correct horse");
    {
        std::ifstream f{static_cast<const std::filesystem::path&>(dir / "seed.dat")};
        std::stringstream text;
        text << f.rdbuf();
        BOOST_CHECK(seed::Decrypt(text.str(), "correct horse").secret);
    }
    // A sidechain turned off does not hold the change back: its wallet's passphrase does not change.
    nodes.refuse_change.clear();
    nodes.running = false;
    BOOST_CHECK(keeper.ChangePassphrase().find_value("changed").isTrue());
    nodes.running = true;
    BOOST_CHECK_EQUAL(nodes.passphrases["chains"]["main"], "battery staple");
    BOOST_CHECK_EQUAL(nodes.passphrases["layers"]["main"], layers_pass);
    BOOST_CHECK(!nodes.Carried("layers", "battery staple"));
    // The new one opens the phrase (the first try, a wrong one, is asked again), and is exchanged as the old one was.
    BOOST_CHECK(keeper.Unlock(UniValue{UniValue::VOBJ}).find_value("unlocked").isTrue());
    BOOST_CHECK_EQUAL(Text(keeper.PassphraseFor("layers", SecureString{"battery staple"})), layers_pass);
    keeper.Stop();

    // Restored elsewhere from the same words: the same passphrase for Layers' wallet (it comes from the seed, not from the user).
    {
        std::lock_guard lock{nodes.mutex};
        nodes.dirs.clear();
        nodes.passphrases.clear();
        nodes.calls.clear();
    }
    {
        std::lock_guard lock{records_mutex};
        records = UniValue{UniValue::VOBJ};
    }
    dir = dir.parent_path() / "seedkeeper-other";
    answer = [&](const Window::ConfirmDialog& d) -> dialog::Answer {
        if (d.kind == "seed-restore") return Accept({{"words", ABOUT}});
        if (d.kind == "seed-passphrase") return Accept({{"passphrase", "a different one"}, {"repeat", "a different one"}});
        return {};
    };
    SeedKeeper again{Hooks()};
    again.Restore(UniValue{UniValue::VOBJ});
    WaitReady(again, "layers");
    WaitReady(again, "chains");
    BOOST_CHECK_EQUAL(nodes.passphrases["layers"]["main"], layers_pass);
    BOOST_CHECK_EQUAL(nodes.passphrases["chains"]["main"], "a different one");
    again.Stop();
}

BOOST_FIXTURE_TEST_CASE(the_phrase_is_locked_again_when_its_time_is_up, KeeperFixture)
{
    shared = {};
    answer = [&](const Window::ConfirmDialog& d) -> dialog::Answer {
        if (d.kind == "seed-restore") return Accept({{"words", ABOUT}});
        if (d.kind == "seed-passphrase") return Accept({{"passphrase", "correct horse"}, {"repeat", "correct horse"}});
        return {};
    };
    SeedKeeper keeper{Hooks(1s)};
    keeper.Restore(UniValue{UniValue::VOBJ});
    BOOST_CHECK(keeper.Status().find_value("unlocked").isTrue());
    std::this_thread::sleep_for(1500ms);
    BOOST_CHECK(keeper.Status().find_value("unlocked").isFalse());
    // Declined: nothing made, nothing kept.
    answer = [](const Window::ConfirmDialog&) { return dialog::Answer{}; };
    BOOST_CHECK(keeper.Unlock(UniValue{UniValue::VOBJ}).find_value("cancelled").isTrue());
    BOOST_CHECK(keeper.Status().find_value("unlocked").isFalse());
    keeper.Stop();
}

BOOST_FIXTURE_TEST_CASE(without_a_password_then_one_added_then_removed, KeeperFixture)
{
    // The user's choice (doc §23.11): no password. Nothing asked after the words but nothing else; seed.dat says so; every
    // wallet of the phrase encrypted from its first moment with a passphrase made from the seed (the app's too), and kept
    // open as an unencrypted one would be.
    std::vector<std::string> words;
    std::string add_pass{"correct horse"};
    int removes{0};
    answer = [&](const Window::ConfirmDialog& d) -> dialog::Answer {
        if (d.kind == "seed-words") {
            for (const auto& w : *d.words) words.push_back(Text(w));
            return Accept();
        }
        if (d.kind == "seed-check") {
            std::map<std::string, std::string> typed;
            for (const auto& f : d.fields) typed[f.id] = words[std::stoi(f.id.substr(5)) - 1];
            return Accept(typed);
        }
        if (d.kind == "seed-show-confirm") return Accept();
        if (d.kind == "seed-show") return Accept();
        if (d.kind == "seed-add-password") return Accept({{"passphrase", add_pass}, {"repeat", add_pass}});
        if (d.kind == "seed-remove-password") {
            ++removes;
            // A wrong one said at its field; the box (anyone with this computer can spend) ticked.
            BOOST_CHECK(!d.checkbox.empty());
            BOOST_CHECK(d.level == dialog::Level::DANGER);
            if (removes == 1) return Accept({{"old", "not it"}});
            BOOST_CHECK(!d.fields[0].error.empty());
            return Accept({{"old", add_pass}});
        }
        BOOST_FAIL("unexpected question " + d.kind);
        return {};
    };
    SeedKeeper keeper{Hooks()};
    UniValue params{UniValue::VOBJ};
    params.pushKV("password", false);
    const UniValue made{keeper.Create(params)};
    BOOST_CHECK(made.find_value("created").isTrue());
    BOOST_CHECK(made.find_value("password").isFalse());
    BOOST_CHECK((asked == std::vector<std::string>{"seed-words", "seed-check"}));
    std::string phrase;
    for (const auto& w : words) phrase += (phrase.empty() ? "" : " ") + w;
    const bip39::Bytes seed_bytes{SeedOf(phrase)};
    const auto file_text{[&] {
        std::ifstream f{static_cast<const std::filesystem::path&>(dir / "seed.dat")};
        std::stringstream text;
        text << f.rdbuf();
        return text.str();
    }};
    // seed.dat: said to have no password, opened with none, the least work; it says plainly that it protects nothing.
    std::string error;
    BOOST_CHECK(!seed::ReadHeader(file_text(), error)->password);
    BOOST_CHECK_EQUAL(seed::ReadHeader(file_text(), error)->n, seed::NO_PASSWORD_N);
    BOOST_CHECK(seed::Decrypt(file_text(), "").secret);
    BOOST_CHECK(file_text().find("WITHOUT a password") != std::string::npos);
    BOOST_CHECK(!keeper.HasPassword());
    BOOST_CHECK(keeper.Status().find_value("password").isFalse());
    WaitReady(keeper, "chains");
    WaitReady(keeper, "layers");
    const std::string app_pass{Text(seed::WalletPassphrase(seed_bytes, "", REGTEST_GENESIS))};
    const std::string layers_pass{Text(seed::WalletPassphrase(seed_bytes, "layers", REGTEST_GENESIS))};
    BOOST_CHECK_EQUAL(nodes.PassphraseOf("chains", "main"), app_pass);
    BOOST_CHECK_EQUAL(nodes.PassphraseOf("layers", "main"), layers_pass);
    // Kept open for good (Core's longest), never locked after the import.
    for (int i{0}; i < 200 && (nodes.OpenedFor("chains", "main") != 100'000'000 || nodes.OpenedFor("layers", "main") != 100'000'000); ++i) std::this_thread::sleep_for(20ms);
    BOOST_CHECK_EQUAL(nodes.OpenedFor("chains", "main"), 100'000'000);
    BOOST_CHECK_EQUAL(nodes.OpenedFor("layers", "main"), 100'000'000);
    {
        const auto methods{nodes.Methods("chains")};
        BOOST_CHECK(std::ranges::find(methods, "walletlock@main") == methods.end());
    }
    // A spend on Layers: its passphrase made from the seed goes with the question, nothing typed; not for another wallet.
    BOOST_CHECK_EQUAL(Text(keeper.PassphraseWithout("layers", "main")), layers_pass);
    BOOST_CHECK_EQUAL(Text(keeper.PassphraseWithout("layers", "")), layers_pass);
    BOOST_CHECK(keeper.PassphraseWithout("layers", "other").empty());
    // The app's own wallet locked by someone (its command line): opened again by itself for a spend; not another wallet.
    {
        std::lock_guard lock{nodes.mutex};
        nodes.opened_for["chains"]["main"] = 0;
    }
    BOOST_CHECK(keeper.OpenWithoutPassword("main"));
    BOOST_CHECK_EQUAL(nodes.OpenedFor("chains", "main"), 100'000'000);
    BOOST_CHECK(!keeper.OpenWithoutPassword("other"));
    // Layers' node started again (locked): opened again when it is ready.
    {
        std::lock_guard lock{nodes.mutex};
        nodes.opened_for["layers"]["main"] = 0;
    }
    keeper.ChainReady("layers");
    for (int i{0}; i < 200 && nodes.OpenedFor("layers", "main") == 0; ++i) std::this_thread::sleep_for(20ms);
    BOOST_CHECK_EQUAL(nodes.OpenedFor("layers", "main"), 100'000'000);
    // Shown with a question first (nobody looking), no password asked; changing a password there is none of is refused.
    asked.clear();
    BOOST_CHECK(keeper.Show().find_value("shown").isTrue());
    BOOST_CHECK((asked == std::vector<std::string>{"seed-show-confirm", "seed-show"}));
    BOOST_CHECK_THROW(keeper.ChangePassphrase(), HostError);
    BOOST_CHECK_THROW(keeper.RemovePassword(), HostError);

    // A password added: the app's wallet refuses first, so nothing changes at all.
    nodes.refuse_change = {"chains"};
    BOOST_CHECK_THROW(keeper.AddPassword(), HostError);
    BOOST_CHECK(!seed::ReadHeader(file_text(), error)->password);
    BOOST_CHECK_EQUAL(nodes.PassphraseOf("chains", "main"), app_pass);
    nodes.refuse_change.clear();
    const UniValue added{keeper.AddPassword()};
    BOOST_CHECK(added.find_value("added").isTrue());
    // seed.dat with it; the app's wallet's passphrase is it; every wallet of the phrase locked; Layers' keeps its own.
    BOOST_CHECK(seed::ReadHeader(file_text(), error)->password);
    BOOST_CHECK(seed::Decrypt(file_text(), add_pass).secret);
    BOOST_CHECK(!seed::Decrypt(file_text(), "").secret);
    BOOST_CHECK_EQUAL(nodes.PassphraseOf("chains", "main"), add_pass);
    BOOST_CHECK_EQUAL(nodes.OpenedFor("chains", "main"), 0);
    BOOST_CHECK_EQUAL(nodes.OpenedFor("layers", "main"), 0);
    BOOST_CHECK_EQUAL(nodes.PassphraseOf("layers", "main"), layers_pass);
    BOOST_CHECK(!nodes.Carried("layers", add_pass));
    BOOST_CHECK(keeper.HasPassword());
    BOOST_CHECK_THROW(keeper.AddPassword(), HostError);
    // From now on, as with a password from the start: the user's exchanged for Layers' own, nothing opened by itself.
    BOOST_CHECK_EQUAL(Text(keeper.PassphraseFor("layers", SecureString{add_pass.begin(), add_pass.end()})), layers_pass);
    BOOST_CHECK(keeper.PassphraseWithout("layers", "main").empty());
    BOOST_CHECK(!keeper.OpenWithoutPassword("main"));

    // Removed again: asked with its consequences, a wrong one said; the wallets kept open again.
    const UniValue removed{keeper.RemovePassword()};
    BOOST_CHECK(removed.find_value("removed").isTrue());
    BOOST_CHECK_EQUAL(removes, 2);
    BOOST_CHECK(!seed::ReadHeader(file_text(), error)->password);
    BOOST_CHECK_EQUAL(nodes.PassphraseOf("chains", "main"), app_pass);
    for (int i{0}; i < 200 && (nodes.OpenedFor("chains", "main") != 100'000'000 || nodes.OpenedFor("layers", "main") != 100'000'000); ++i) std::this_thread::sleep_for(20ms);
    BOOST_CHECK_EQUAL(nodes.OpenedFor("chains", "main"), 100'000'000);
    BOOST_CHECK_EQUAL(nodes.OpenedFor("layers", "main"), 100'000'000);
    BOOST_CHECK(!nodes.Carried("layers", add_pass));
    keeper.Stop();

    // Restored elsewhere without a password: no password asked after the words; the same passphrases for the wallets.
    {
        std::lock_guard lock{nodes.mutex};
        nodes.dirs.clear();
        nodes.passphrases.clear();
        nodes.calls.clear();
        nodes.opened_for.clear();
    }
    {
        std::lock_guard lock{records_mutex};
        records = UniValue{UniValue::VOBJ};
    }
    dir = dir.parent_path() / "seedkeeper-nopass";
    asked.clear();
    answer = [&](const Window::ConfirmDialog& d) -> dialog::Answer {
        if (d.kind == "seed-restore") return Accept({{"words", phrase}});
        BOOST_FAIL("unexpected question " + d.kind);
        return {};
    };
    SeedKeeper again{Hooks()};
    UniValue restore{UniValue::VOBJ};
    restore.pushKV("password", false);
    BOOST_CHECK(again.Restore(restore).find_value("restored").isTrue());
    BOOST_CHECK((asked == std::vector<std::string>{"seed-restore"}));
    WaitReady(again, "chains");
    WaitReady(again, "layers");
    BOOST_CHECK_EQUAL(nodes.PassphraseOf("chains", "main"), app_pass);
    BOOST_CHECK_EQUAL(nodes.PassphraseOf("layers", "main"), layers_pass);
    again.Stop();
}

BOOST_FIXTURE_TEST_CASE(a_mining_wallet_from_the_phrase_pays_the_bids, KeeperFixture)
{
    // Merged mining's bids from a wallet of their own (doc §23.12): account 1 of the app's chain, from the phrase, without a
    // password (it pays by itself), so the main wallet can stay locked.
    shared = {};
    int paying{0};
    answer = [&](const Window::ConfirmDialog& d) -> dialog::Answer {
        if (d.kind == "seed-restore") return Accept({{"words", ABOUT}});
        if (d.kind == "seed-passphrase") return Accept({{"passphrase", "correct horse"}, {"repeat", "correct horse"}});
        if (d.kind == "seed-unlock") return Accept({}, "correct horse");
        BOOST_FAIL("unexpected question " + d.kind);
        return {};
    };
    auto hooks{Hooks()};
    hooks.paying_changed = [&] { ++paying; };
    SeedKeeper keeper{hooks};
    BOOST_CHECK_EQUAL(keeper.MiningWallet(), "");
    keeper.Restore(UniValue{UniValue::VOBJ});
    WaitReady(keeper, "chains");
    // The app's wallet of the phrase made: it pays the bids from now on (the daemons told).
    BOOST_CHECK_EQUAL(paying, 1);
    keeper.Lock();
    asked.clear();
    const UniValue made{keeper.MiningCreate()};
    // The phrase was locked: its password asked once.
    BOOST_CHECK((asked == std::vector<std::string>{"seed-unlock"}));
    BOOST_CHECK_EQUAL(made.find_value("wallet").get_str(), "mining");
    BOOST_CHECK_EQUAL(made.find_value("from").get_str(), "phrase");
    BOOST_CHECK_EQUAL(keeper.MiningWallet(), "mining");
    BOOST_CHECK_EQUAL(paying, 2);
    // A blank wallet without a passphrase, the four descriptors of account 1 (never the main wallet's account 0).
    for (const auto& [m, p] : [&] { std::lock_guard l{nodes.mutex}; return nodes.calls["chains"]; }()) {
        if (m == "createwallet" && p[0].get_str() == "mining") {
            BOOST_CHECK(p[2].get_bool());
            BOOST_CHECK_EQUAL(p[3].get_str(), "");
        }
    }
    const UniValue imported{nodes.Params("chains", "importdescriptors@mining")};
    BOOST_REQUIRE_EQUAL(imported[0].size(), 4U);
    const auto expected{seed::AccountDescriptors(SeedOf(ABOUT), 1, seed::MINING_ACCOUNT, true)};
    for (size_t i{0}; i < 4; ++i) BOOST_CHECK_EQUAL(imported[0][i].find_value("desc").get_str(), Text(expected[i].text));
    BOOST_CHECK(Text(expected[0].text).find("/86h/1h/1h]") != std::string::npos);
    BOOST_CHECK_EQUAL(nodes.PassphraseOf("chains", "mining"), "");
    // The status says it, never a key.
    const UniValue status{keeper.Status()};
    BOOST_CHECK_EQUAL(status.find_value("mining").find_value("wallet").get_str(), "mining");
    BOOST_CHECK(status.write().find("prv") == std::string::npos);
    // Stopped: the main wallet pays again; the mining wallet stays, with its coins. Again: the same wallet, nothing made.
    keeper.MiningStop();
    BOOST_CHECK_EQUAL(keeper.MiningWallet(), "");
    BOOST_CHECK_EQUAL(paying, 3);
    const size_t creates{[&] { std::lock_guard l{nodes.mutex}; return static_cast<size_t>(std::ranges::count_if(nodes.calls["chains"], [](const auto& c) { return c.first == "createwallet"; })); }()};
    asked.clear();
    BOOST_CHECK_EQUAL(keeper.MiningCreate().find_value("wallet").get_str(), "mining");
    BOOST_CHECK(asked.empty());
    BOOST_CHECK_EQUAL(creates, [&] { std::lock_guard l{nodes.mutex}; return static_cast<size_t>(std::ranges::count_if(nodes.calls["chains"], [](const auto& c) { return c.first == "createwallet"; })); }());
    BOOST_CHECK_EQUAL(keeper.MiningWallet(), "mining");
    keeper.Stop();

    // Without a phrase (wallets of before, "Not now"): a mining wallet with keys of its own.
    {
        std::lock_guard lock{records_mutex};
        records = UniValue{UniValue::VOBJ};
    }
    dir = dir.parent_path() / "seedkeeper-nophrase";
    SeedKeeper none{Hooks()};
    const UniValue own{none.MiningCreate()};
    BOOST_CHECK_EQUAL(own.find_value("from").get_str(), "own");
    BOOST_CHECK_EQUAL(own.find_value("wallet").get_str(), "mining-2");
    none.Stop();
}

BOOST_FIXTURE_TEST_CASE(zside_is_never_from_the_phrase, KeeperFixture)
{
    // zSide's shielded keys are not made from the phrase (doc §23.13): its wallet is its own, and the status says so for
    // the pages' notices.
    shared = {};
    answer = [](const Window::ConfirmDialog&) { return dialog::Answer{}; };
    auto hooks{Hooks()};
    hooks.separate_chains = [] { return std::vector<std::string>{"zside"}; };
    SeedKeeper keeper{hooks};
    bool seen{false};
    const UniValue status{keeper.Status()};
    for (const UniValue& c : status.find_value("chains").getValues()) {
        if (c.find_value("id").get_str() != "zside") continue;
        seen = true;
        BOOST_CHECK(c.find_value("phraseKeys").isFalse());
        BOOST_CHECK_EQUAL(c.find_value("mode").get_str(), "separate");
    }
    BOOST_CHECK(seen);
    BOOST_CHECK(!keeper.WalletOf("zside"));
    keeper.Stop();
}

BOOST_FIXTURE_TEST_CASE(first_run_and_offer, KeeperFixture)
{
    answer = [](const Window::ConfirmDialog&) { return dialog::Answer{}; };
    SeedKeeper keeper{Hooks()};
    // No wallet yet: the first run; declined at its first question, nothing is written.
    BOOST_CHECK(keeper.Status().find_value("firstRun").isTrue());
    BOOST_CHECK(keeper.Status().find_value("offer").isFalse());
    BOOST_CHECK(keeper.Create(UniValue{UniValue::VOBJ}).find_value("cancelled").isTrue());
    BOOST_CHECK(!keeper.Exists());
    keeper.Dismiss("firstRun");
    BOOST_CHECK(keeper.Status().find_value("firstRun").isFalse());
    // An existing wallet: the offer, once.
    nodes.dirs["chains"] = {"wallet.dat"};
    BOOST_CHECK(keeper.Status().find_value("offer").isTrue());
    keeper.Dismiss("offer");
    BOOST_CHECK(keeper.Status().find_value("offer").isFalse());
    BOOST_CHECK_THROW(keeper.Dismiss("other"), HostError);
    keeper.Stop();
}

BOOST_AUTO_TEST_SUITE_END()
