// Copyright (c) 2026 The Backbone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Backbone's sidechains (doc/unified-app.md §26, P1): the eCash family's catalog (Flush, our template's daemon; Thunder,
// a drop-in), their arguments, the drop-in's proof of its server, their downloads from the latest release of their
// repositories (served by a fetcher of the tests: no network) checked by the release's SHA256SUMS, their pages from the
// release with the copy built in as the fallback, and what the app's own pages make of them.

#include <hash.h>
#include <key.h>
#include <pubkey.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/fs.h>
#include <util/strencodings.h>
#include <webui/assets.h>
#include <webui/chainadapter.h>
#include <webui/moduleinstaller.h>
#include <webui/sidechainnodes.h>
#include <webui/sidechainpages.h>
#include <webui/supervisor.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <fstream>
#include <map>

using webui::ChainAdapter;
using webui::ModuleInstaller;
using webui::SidechainNodes;
using webui::SidechainSupervisor;

namespace {

const std::string BASE{"https://mirror.test"};
const std::string PLATFORM{"x86_64-linux-gnu"};

std::string ReadAll(const fs::path& p)
{
    std::ifstream f{p.std_path(), std::ios::binary};
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

void WriteAll(const fs::path& p, const std::string& text)
{
    std::filesystem::create_directories(p.parent_path());
    std::ofstream f{p.std_path(), std::ios::binary | std::ios::trunc};
    f << text;
}

std::string Sha(const std::string& data)
{
    uint256 out;
    CSHA256().Write(reinterpret_cast<const unsigned char*>(data.data()), data.size()).Finalize(out.begin());
    return HexStr(out);
}

bool Has(const std::vector<std::string>& v, const std::string& x) { return std::ranges::find(v, x) != v.end(); }
bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::string ErrorOf(const std::function<void()>& fn)
{
    try {
        fn();
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

//! The network of the app for a test, put back as the other tests have it (Chains' regtest) after.
struct UseNet {
    UseNet(ChainType chain, bool ecash_regtest = false)
    {
        SidechainSupervisor::UseNetwork(chain, ecash_regtest);
        ModuleInstaller::UseNetwork(chain, ecash_regtest);
    }
    ~UseNet()
    {
        SidechainSupervisor::UseNetwork(ChainType::REGTEST);
        ModuleInstaller::UseNetwork(ChainType::REGTEST);
    }
};

std::string PackJson(const std::string& stem, int host_api = 1)
{
    return strprintf(R"({"stem":"%s","name":"Flush","hostApi":%d,"commit":"c","rpc":{"reads":["listpools"],"confirmed":["swapasset"],"host":["fastblocks.bond"]}})", stem, host_api);
}

/** A latest release of block-fabric/backbone-flush, as GitHub serves it: its files and its releases/latest. */
struct Release {
    std::string tag{"flush-v8.3.8"};
    std::string version{"8.3.8"};
    //! "archive": its pages in the package (as Thunder's); "asset": a pack of their own (Flush from 8.3.8); "": none.
    std::string pages{"archive"};
    int pages_api{1};
    bool prerelease{false};
    bool sums{true};
    //! The package served is not the one SHA256SUMS says.
    bool tampered{false};
    //! "": no SHA256SUMS.sig; "good": signed with the key of the test; "bad": another text signed.
    std::string sig;
};

struct Fixture {
    fs::path root;
    std::map<std::string, std::string> files;
    CKey key;
    std::string key_hex;

    explicit Fixture(const fs::path& r) : root{r}
    {
        key.MakeNewKey(true);
        key_hex = HexStr(XOnlyPubKey{key.GetPubKey()});
    }

    ModuleInstaller::Fetcher Fetcher()
    {
        return [this](const ModuleInstaller::FetchRequest& req) {
            ModuleInstaller::FetchResult r;
            const auto it{files.find(req.url)};
            if (it == files.end()) {
                r.error = "The requested URL returned error: 404";
                return r;
            }
            if (it->second.size() > req.max_bytes) {
                r.too_large = true;
                return r;
            }
            WriteAll(req.out, it->second);
            r.ok = true;
            return r;
        };
    }

    std::string Url(const std::string& tag, const std::string& name) const { return BASE + "/block-fabric/backbone-flush/releases/download/" + tag + "/" + name; }

    std::string Tar(const fs::path& dir, const std::string& member)
    {
        const fs::path out{dir / "out.tar.gz"};
        const SidechainNodes::Output made{SidechainNodes::Run({"tar", "-czf", fs::PathToString(out), "-C", fs::PathToString(dir), member}, 60)};
        BOOST_REQUIRE_MESSAGE(made.code == 0, made.err);
        return ReadAll(out);
    }

    void PagesIn(const fs::path& at, const Release& s)
    {
        WriteAll(at / "pack" / "flush" / "pack.json", PackJson("flush", s.pages_api));
        WriteAll(at / "pack" / "flush" / "index.html", "<!doctype html><html><head><script type=\"module\" src=\"/packs/flush/assets/a.js\"></script></head></html>");
        WriteAll(at / "pack" / "flush" / "assets" / "a.js", "console.log('" + s.version + "')");
    }

    void Serve(const Release& s)
    {
        // The package: its programs in bin/ (and one more program, not installed), its pages beside them (Thunder's way).
        const fs::path work{root / fs::PathFromString("src-" + s.tag + "-" + s.pages)};
        std::filesystem::remove_all(work);
        const std::string top{"flush-" + s.version + "-" + PLATFORM};
        WriteAll(work / fs::PathFromString(top) / "bin" / "flushd", "#!/bin/sh\necho flushd " + s.version + "\n");
        WriteAll(work / fs::PathFromString(top) / "bin" / "flush-cli", "#!/bin/sh\necho cli\n");
        WriteAll(work / fs::PathFromString(top) / "bin" / "flush-app", "not installed\n");
        WriteAll(work / fs::PathFromString(top) / "README.md", "readme\n");
        if (s.pages == "archive") PagesIn(work / fs::PathFromString(top), s);
        const std::string archive{Tar(work, top)};
        const std::string name{top + ".tar.gz"};
        UniValue assets{UniValue::VARR};
        const auto asset{[&](const std::string& n, const std::string& data) {
            files[Url(s.tag, n)] = data;
            UniValue a{UniValue::VOBJ};
            a.pushKV("name", n);
            a.pushKV("size", static_cast<int64_t>(data.size()));
            assets.push_back(std::move(a));
        }};
        std::string sums{Sha(archive) + "  " + name + "\n"};
        asset(name, archive);
        if (s.tampered) files[Url(s.tag, name)] = archive.substr(0, archive.size() - 1) + "\x01";
        asset("flush-" + s.version + "-win64.zip", "zip");
        sums += Sha("zip") + "  flush-" + s.version + "-win64.zip\n";
        if (s.pages == "asset") {
            const fs::path pw{work / "pages"};
            PagesIn(pw, s);
            const std::string pack{Tar(pw, "pack")};
            asset("flush-" + s.version + "-pack.tar.gz", pack);
            sums += Sha(pack) + "  flush-" + s.version + "-pack.tar.gz\n";
        }
        if (s.sums) asset("SHA256SUMS", sums);
        if (!s.sig.empty()) {
            const std::string text{s.sig == "good" ? sums : sums + " "};
            HashWriter hasher{TaggedHash(std::string{ModuleInstaller::SUMS_SIGNATURE_TAG})};
            hasher.write(std::as_bytes(std::span{text.data(), text.size()}));
            std::vector<unsigned char> sig(64);
            BOOST_REQUIRE(key.SignSchnorr(hasher.GetSHA256(), sig, nullptr, uint256{}));
            asset("SHA256SUMS.sig", HexStr(sig) + "\n");
        }
        UniValue rel{UniValue::VOBJ};
        rel.pushKV("tag_name", s.tag);
        rel.pushKV("name", "Flush " + s.version);
        rel.pushKV("draft", false);
        rel.pushKV("prerelease", s.prerelease);
        rel.pushKV("published_at", "2026-10-31T01:00:00Z");
        rel.pushKV("assets", std::move(assets));
        files[BASE + "/repos/block-fabric/backbone-flush/releases/latest"] = rel.write();
    }

    ModuleInstaller::Options Options(const std::string& signatures = "auto", bool trust_key = false)
    {
        ModuleInstaller::Options o;
        o.bin_root = root / "bin";
        o.api_base = BASE;
        o.download_base = BASE;
        o.platform = PLATFORM;
        o.signatures = signatures;
        if (trust_key) o.extra_keys = {key_hex};
        o.fetcher = Fetcher();
        return o;
    }
};

SidechainSupervisor::Options Supervised(const fs::path& datadir, ChainType chain)
{
    SidechainSupervisor::Options o;
    o.chains_datadir = datadir;
    o.chain = chain;
    o.ecash_family = true;
    o.mainchain_rpc_port = 8532;
    o.mainchain_rpc_user = "chains-app-sidechains";
    o.mainchain_rpc_password = "0123456789abcdef";
    o.mainchain_grpc_port = 50071;
    return o;
}

struct Memory {
    UniValue value{UniValue::VOBJ};
    SidechainSupervisor::LoadFn Load()
    {
        return [this] { return value; };
    }
    SidechainSupervisor::StoreFn Store()
    {
        return [this](const UniValue& v) { value = v; };
    }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(ecash_sidechains_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(the_ecash_catalog)
{
    using Kind = SidechainSupervisor::Kind;
    const auto& main{SidechainSupervisor::Catalog(ChainType::ECASH)};
    BOOST_REQUIRE_EQUAL(main.size(), 2U);
    const SidechainSupervisor::Spec& flush{main[0]};
    BOOST_CHECK_EQUAL(flush.stem, "flush");
    BOOST_CHECK_EQUAL(flush.slot, 46);
    BOOST_CHECK(flush.kind == Kind::TEMPLATE && flush.staking && flush.phrase_keys);
    BOOST_CHECK_EQUAL(SidechainSupervisor::ProgramName(flush), "flushd");
    BOOST_CHECK_EQUAL(flush.rpc_port, 8254);
    BOOST_CHECK_EQUAL(flush.p2p_port, 8255);
    const SidechainSupervisor::Spec& thunder{main[1]};
    BOOST_CHECK_EQUAL(thunder.stem, "thunder");
    BOOST_CHECK_EQUAL(thunder.slot, 9);
    // A drop-in: its own wallet until P2 (never the template's descriptors of the phrase).
    BOOST_CHECK(thunder.kind == Kind::DROPIN && !thunder.staking && !thunder.phrase_keys);
    BOOST_CHECK_EQUAL(SidechainSupervisor::ProgramName(thunder), "thunder");
    BOOST_CHECK_EQUAL(thunder.rpc_port, 6109);
    BOOST_CHECK_EQUAL(thunder.p2p_port, 4009);
    BOOST_CHECK_EQUAL(thunder.l2l_rpc_port, 6009);
    // Its test network: Flush alone (Thunder has none of eCash's); betanet is retired.
    const auto& test{SidechainSupervisor::Catalog(ChainType::ECASH_TESTNET)};
    BOOST_REQUIRE_EQUAL(test.size(), 1U);
    BOOST_CHECK_EQUAL(test[0].rpc_port, 48254);
    BOOST_CHECK(SidechainSupervisor::Catalog(ChainType::ECASH_BETANET).empty());
    // Regtest: Chains' catalog, unless the node follows eCash's rules (-testecash).
    BOOST_CHECK_EQUAL(SidechainSupervisor::Catalog(ChainType::REGTEST).size(), 7U);
    const auto& regtest{SidechainSupervisor::Catalog(ChainType::REGTEST, /*ecash_regtest=*/true)};
    BOOST_REQUIRE_EQUAL(regtest.size(), 2U);
    BOOST_CHECK(regtest[1].kind == Kind::DROPIN);
    BOOST_CHECK(SidechainSupervisor::EcashFamily(ChainType::ECASH, false) && SidechainSupervisor::EcashFamily(ChainType::REGTEST, true));
    BOOST_CHECK(!SidechainSupervisor::EcashFamily(ChainType::REGTEST, false) && !SidechainSupervisor::EcashFamily(ChainType::MAIN, true));
    BOOST_CHECK_EQUAL(SidechainSupervisor::DropinNetwork(ChainType::ECASH), "mainnet");
    BOOST_CHECK_EQUAL(SidechainSupervisor::DropinNetwork(ChainType::REGTEST), "regtest");
    BOOST_CHECK_EQUAL(SidechainSupervisor::DropinNetwork(ChainType::ECASH_TESTNET), "");
    // Every row has its repository, the latest release of it.
    for (const ChainType chain : {ChainType::ECASH, ChainType::ECASH_TESTNET}) {
        for (const auto& s : SidechainSupervisor::Catalog(chain)) {
            const auto& repos{ModuleInstaller::Repos(chain)};
            const auto it{std::ranges::find_if(repos, [&](const auto& r) { return r.stem == s.stem; })};
            BOOST_REQUIRE(it != repos.end());
            BOOST_CHECK(it->latest);
            BOOST_CHECK_EQUAL(it->repo, "backbone-" + s.stem);
            BOOST_CHECK_EQUAL(it->programs.front(), s.program);
        }
    }
}

BOOST_AUTO_TEST_CASE(arguments_of_flush_and_thunder)
{
    UseNet net{ChainType::ECASH};
    Memory memory;
    SidechainSupervisor s{Supervised(m_args.GetDataDirBase(), ChainType::ECASH), memory.Load(), memory.Store()};
    // Flush: the template's arguments, its network by name, its own default ports, the mainchain over JSON-RPC.
    const std::vector<std::string> flush{s.DaemonArgs("flush")};
    for (const char* a : {"-chain=ecash", "-rpcport=8254", "-port=8255", "-mainchainrpcport=8532", "-webuichooser=chains-app", "-rpcwhitelistdefault=0"}) {
        BOOST_CHECK_MESSAGE(Has(flush, a), a);
    }
    BOOST_CHECK(!Has(flush, "-regtest") && !Has(flush, "-testmainenforcer"));
    BOOST_CHECK(s.NetDataDir("flush") == s.DataDir("flush"));
    // Thunder: its own flags, a folder of its network, the mainchain by this node's gRPC, the app layer.
    const std::vector<std::string> thunder{s.DaemonArgs("thunder")};
    const auto after{[&](const std::string& flag) {
        const auto it{std::ranges::find(thunder, flag)};
        return it != thunder.end() && it + 1 != thunder.end() ? *(it + 1) : std::string{};
    }};
    BOOST_CHECK_EQUAL(after("--network"), "mainnet");
    BOOST_CHECK_EQUAL(after("--datadir"), fs::PathToString(s.DataDir("thunder") / "mainnet"));
    BOOST_CHECK_EQUAL(after("--mainchain-grpc-url"), "http://127.0.0.1:50071");
    BOOST_CHECK_EQUAL(after("--rpc-addr"), "127.0.0.1:6009");
    BOOST_CHECK_EQUAL(after("--net-addr"), "0.0.0.0:4009");
    BOOST_CHECK_EQUAL(after("--bt-app-rpc-port"), "6109");
    BOOST_CHECK_EQUAL(after("--bt-app-chooser"), "chains-app");
    BOOST_CHECK(after("--bt-app-rpcauth").starts_with("chains-app:"));
    BOOST_CHECK(Has(thunder, "--bt-app-exit-with-stdin") && Has(thunder, "--bt-app-rpcwhitelistdefault=0") && Has(thunder, "--headless"));
    // No template flag, no password anywhere.
    for (const auto& a : thunder) BOOST_CHECK_MESSAGE(!a.starts_with("-mainchainrpc") && !a.starts_with("-rpc") && !Contains(a, "0123456789abcdef"), a);
    // The pages' user (this app's) may call the host protocol and the overview's reads, never the phrase or stop.
    const std::string whitelist{after("--bt-app-rpcwhitelist")};
    BOOST_CHECK(whitelist.starts_with("chains-app:"));
    for (const char* m : {"webuihost", "appserverproof", "getblockcount", "getbalances", "describeconfirmation"}) BOOST_CHECK_MESSAGE(Contains(whitelist, m), m);
    for (const char* m : {"setwalletphrase", "stop", "create_transfer", "set_seed_from_mnemonic"}) BOOST_CHECK_MESSAGE(!Contains(whitelist, std::string{":"} + m) && !Contains(whitelist, std::string{","} + m + ","), m);
    BOOST_CHECK(s.CookieFile("thunder") == s.DataDir("thunder") / "mainnet" / "backbone-app.cookie");
    BOOST_CHECK(s.CookieFile("flush") == s.NetDataDir("flush") / ".cookie");
    BOOST_CHECK_EQUAL(s.L2lRpcPort("thunder"), 6009);
    BOOST_CHECK_EQUAL(s.L2lRpcPort("flush"), 0);
    // The list says which kind each is.
    const UniValue list{s.List()};
    BOOST_CHECK_EQUAL(list[0].find_value("kind").get_str(), "template");
    BOOST_CHECK_EQUAL(list[1].find_value("kind").get_str(), "dropin");
}

BOOST_AUTO_TEST_CASE(arguments_on_regtest_with_ecash_rules)
{
    UseNet net{ChainType::REGTEST, /*ecash_regtest=*/true};
    Memory memory;
    SidechainSupervisor::Options o{Supervised(m_args.GetDataDirBase(), ChainType::REGTEST)};
    o.port_base = 49500;
    o.dropin_args = {"--bt-no-seeds"};
    SidechainSupervisor s{o, memory.Load(), memory.Store()};
    const std::vector<std::string> flush{s.DaemonArgs("flush")};
    for (const char* a : {"-regtest", "-sidechainslot=46", "-testmainenforcer", "-rpcport=49500", "-port=49501"}) BOOST_CHECK_MESSAGE(Has(flush, a), a);
    const std::vector<std::string> thunder{s.DaemonArgs("thunder")};
    BOOST_CHECK(Has(thunder, "regtest") && Has(thunder, "49502") && Has(thunder, "127.0.0.1:49602") && Has(thunder, "0.0.0.0:49503") && thunder.back() == "--bt-no-seeds");
}

BOOST_AUTO_TEST_CASE(the_server_proof_of_a_dropin)
{
    // backbone-core libs/appui auth.cpp server_proof: HMAC-SHA256(key = the cookie's secret as written, "bt-app-server-proof/v1" 0x00 challenge).
    const std::string secret{"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"};
    BOOST_CHECK_EQUAL(SidechainSupervisor::ServerProof(secret, std::string(32, 'c')), "91dd5382c87730d894759409ebff8a6bdb260609336b5e3a93a5dcd49a375aed");
    BOOST_CHECK(SidechainSupervisor::ServerProof(secret, std::string(32, 'd')) != SidechainSupervisor::ServerProof(secret, std::string(32, 'c')));
    // Nothing answers on its port, no cookie: it proves nothing.
    UseNet net{ChainType::REGTEST, true};
    Memory memory;
    SidechainSupervisor::Options o{Supervised(m_args.GetDataDirBase(), ChainType::REGTEST)};
    o.port_base = 49540;
    SidechainSupervisor s{o, memory.Load(), memory.Store()};
    BOOST_CHECK(!s.ProvesServer("thunder"));
}

BOOST_AUTO_TEST_CASE(latest_release_names_and_sums)
{
    ModuleInstaller::Repo repo{"flush", "block-fabric", "backbone-flush", "flush", true, {"flushd", "flush-cli"}};
    const std::vector<std::string> names{"flush-8.3.7-x86_64-linux-gnu.tar.gz", "flush-8.3.7-win64.zip", "flush-8.3.7-win64-setup.exe", "SHA256SUMS", "flush-8.3.7-pack.tar.gz"};
    BOOST_CHECK_EQUAL(ModuleInstaller::LatestArchive(repo, names, "x86_64-linux-gnu"), "flush-8.3.7-x86_64-linux-gnu.tar.gz");
    BOOST_CHECK_EQUAL(ModuleInstaller::LatestArchive(repo, names, "win64"), "flush-8.3.7-win64.zip");
    BOOST_CHECK_EQUAL(ModuleInstaller::LatestPack(repo, names), "flush-8.3.7-pack.tar.gz");
    // Thunder's names: its tag in them.
    ModuleInstaller::Repo thunder{"thunder", "block-fabric", "backbone-thunder", "thunder", true, {"thunder", "thunder-cli"}};
    BOOST_CHECK_EQUAL(ModuleInstaller::LatestArchive(thunder, {"thunder-mainnet-v1-x86_64-linux-gnu.tar.gz", "thunder-mainnet-v1-win64.zip", "SHA256SUMS"}, "x86_64-linux-gnu"), "thunder-mainnet-v1-x86_64-linux-gnu.tar.gz");
    // Two packages for this computer: none is taken; another sidechain's, or an odd version, neither.
    BOOST_CHECK_EQUAL(ModuleInstaller::LatestArchive(repo, {"flush-1-x86_64-linux-gnu.tar.gz", "flush-2-x86_64-linux-gnu.tar.gz"}, "x86_64-linux-gnu"), "");
    BOOST_CHECK_EQUAL(ModuleInstaller::LatestArchive(repo, {"flushx-1-x86_64-linux-gnu.tar.gz", "flush--x86_64-linux-gnu.tar.gz", "flush-a/b-x86_64-linux-gnu.tar.gz"}, "x86_64-linux-gnu"), "");
    BOOST_CHECK_EQUAL(ModuleInstaller::LatestArchive(repo, names, ""), "");
    const std::string a(64, 'a');
    const std::string sums{a + "  flush-8.3.7-x86_64-linux-gnu.tar.gz\n" + std::string(64, 'B') + " *flush-8.3.7-win64.zip\nnoise\n"};
    BOOST_CHECK_EQUAL(ModuleInstaller::SumOf(sums, "flush-8.3.7-x86_64-linux-gnu.tar.gz"), a);
    BOOST_CHECK_EQUAL(ModuleInstaller::SumOf(sums, "flush-8.3.7-win64.zip"), std::string(64, 'b'));
    BOOST_CHECK_EQUAL(ModuleInstaller::SumOf(sums, "SHA256SUMS"), "");
    // A file named twice with two hashes: none.
    BOOST_CHECK_EQUAL(ModuleInstaller::SumOf(sums + std::string(64, 'c') + "  flush-8.3.7-win64.zip\n", "flush-8.3.7-win64.zip"), "");
}

BOOST_AUTO_TEST_CASE(install_the_latest_release_with_its_pages)
{
    UseNet net{ChainType::ECASH};
    Fixture f{m_args.GetDataDirBase() / "latest"};
    f.Serve(Release{});
    ModuleInstaller installer{f.Options()};
    const auto releases{installer.Releases("flush")};
    BOOST_REQUIRE_EQUAL(releases.size(), 1U);
    const auto& r{releases[0]};
    BOOST_CHECK_MESSAGE(r.compatible, r.reason);
    BOOST_CHECK(r.unsigned_only && !r.signed_ok && !r.manifest);
    BOOST_CHECK_EQUAL(r.asset_name, "flush-8.3.8-x86_64-linux-gnu.tar.gz");
    installer.InstallNow("flush", "flush-v8.3.8");
    BOOST_CHECK_EQUAL(installer.Current("flush").value_or(""), "flush-v8.3.8");
    const auto dir{installer.CurrentDir("flush")};
    BOOST_REQUIRE(dir);
    // The programs named, from bin/, and nothing else of the package.
    BOOST_CHECK(Contains(ReadAll(*dir / "flushd"), "flushd 8.3.8"));
    BOOST_CHECK(std::filesystem::exists(*dir / "flush-cli"));
    BOOST_CHECK(!std::filesystem::exists(*dir / "flush-app") && !std::filesystem::exists(*dir / "README.md"));
    const auto pages{installer.CurrentPackDir("flush")};
    BOOST_REQUIRE(pages);
    BOOST_CHECK(Contains(ReadAll(*pages / "assets" / "a.js"), "8.3.8"));
    BOOST_CHECK(Contains(ReadAll(*dir / "release.json"), "\"pages\": true"));
    // The supervisor runs it: the program of the catalog's row, from the download.
    Memory memory;
    SidechainSupervisor::Options o{Supervised(m_args.GetDataDirBase(), ChainType::ECASH)};
    o.downloaded = [&](const std::string& stem) { return installer.CurrentDir(stem); };
    SidechainSupervisor s{o, memory.Load(), memory.Store()};
    BOOST_CHECK(s.FindBinary("flush") == *dir / "flushd");
    BOOST_CHECK_EQUAL(s.BinarySource("flush"), "download");
}

BOOST_AUTO_TEST_CASE(pages_as_an_asset_of_their_own_and_none)
{
    UseNet net{ChainType::ECASH};
    {
        Fixture f{m_args.GetDataDirBase() / "asset"};
        Release rel;
        rel.pages = "asset";
        f.Serve(rel);
        ModuleInstaller installer{f.Options()};
        const auto r{installer.Releases("flush")[0]};
        BOOST_CHECK_EQUAL(r.pack_asset, "flush-8.3.8-pack.tar.gz");
        installer.InstallNow("flush", rel.tag);
        const auto pages{installer.CurrentPackDir("flush")};
        BOOST_REQUIRE(pages);
        BOOST_CHECK(Contains(ReadAll(*pages / "pack.json"), "\"stem\":\"flush\""));
    }
    {
        // 8.3.7's way: no pages in the release. Installed; the copy built into the app shows it.
        Fixture f{m_args.GetDataDirBase() / "none"};
        Release rel;
        rel.pages = "";
        f.Serve(rel);
        ModuleInstaller installer{f.Options()};
        installer.InstallNow("flush", rel.tag);
        BOOST_CHECK(installer.CurrentDir("flush"));
        BOOST_CHECK(!installer.CurrentPackDir("flush"));
    }
    {
        // Pages of another host version: refused, nothing installed.
        Fixture f{m_args.GetDataDirBase() / "api"};
        Release rel;
        rel.pages_api = 2;
        f.Serve(rel);
        ModuleInstaller installer{f.Options()};
        BOOST_CHECK(Contains(ErrorOf([&] { installer.InstallNow("flush", rel.tag); }), "another version of the host"));
        BOOST_CHECK(!installer.Current("flush"));
    }
}

BOOST_AUTO_TEST_CASE(checked_by_the_release_sums)
{
    UseNet net{ChainType::ECASH};
    {
        // The package is not the one SHA256SUMS says: refused at the download, nothing installed.
        Fixture f{m_args.GetDataDirBase() / "tampered"};
        Release rel;
        rel.tampered = true;
        f.Serve(rel);
        ModuleInstaller installer{f.Options()};
        BOOST_CHECK(Contains(ErrorOf([&] { installer.InstallNow("flush", rel.tag); }), "does not match its SHA-256"));
        BOOST_CHECK(!installer.Current("flush"));
    }
    {
        Fixture f{m_args.GetDataDirBase() / "nosums"};
        Release rel;
        rel.sums = false;
        f.Serve(rel);
        ModuleInstaller installer{f.Options()};
        const auto r{installer.Releases("flush")[0]};
        BOOST_CHECK(!r.compatible && Contains(r.reason, "no SHA256SUMS"));
    }
    {
        // A pre-release as "latest" (GitHub never says so; a mirror might): not taken.
        Fixture f{m_args.GetDataDirBase() / "pre"};
        Release rel;
        rel.prerelease = true;
        f.Serve(rel);
        ModuleInstaller installer{f.Options()};
        BOOST_CHECK(installer.Releases("flush").empty());
    }
}

BOOST_AUTO_TEST_CASE(signatures_of_the_sums_p3_hook)
{
    UseNet net{ChainType::ECASH};
    BOOST_CHECK(ModuleInstaller::SumsKeys().empty());
    for (const std::string sig : {"", "bad", "good"}) {
        Fixture f{m_args.GetDataDirBase() / fs::PathFromString("sig-" + (sig.empty() ? std::string{"none"} : sig))};
        Release rel;
        rel.sig = sig;
        f.Serve(rel);
        // A key trusted: the sums must be signed by it.
        ModuleInstaller trusted{f.Options("auto", /*trust_key=*/true)};
        const auto r{trusted.Releases("flush")[0]};
        BOOST_CHECK_EQUAL(r.compatible, sig == "good");
        BOOST_CHECK_EQUAL(r.signed_ok, sig == "good");
        // No key (today): checked by the sums only; "require" refuses them all.
        ModuleInstaller plain{f.Options("auto")};
        BOOST_CHECK(plain.Releases("flush")[0].compatible);
        ModuleInstaller required{f.Options("require")};
        BOOST_CHECK(!required.Releases("flush")[0].compatible);
    }
}

BOOST_AUTO_TEST_CASE(a_newer_latest_release_and_rollback)
{
    UseNet net{ChainType::ECASH};
    Fixture f{m_args.GetDataDirBase() / "update"};
    Release one;
    one.tag = "flush-v8.3.7";
    one.version = "8.3.7";
    f.Serve(one);
    ModuleInstaller::Options o{f.Options()};
    o.cache_for = std::chrono::seconds{0};
    ModuleInstaller installer{o};
    installer.InstallNow("flush", one.tag);
    BOOST_CHECK(!installer.UpdateFor("flush", true));
    Release two;
    f.Serve(two);
    BOOST_CHECK_EQUAL(installer.UpdateFor("flush", true).value_or(""), "flush-v8.3.8");
    installer.InstallNow("flush", two.tag);
    BOOST_CHECK_EQUAL(installer.Current("flush").value_or(""), "flush-v8.3.8");
    BOOST_CHECK_EQUAL(installer.Previous("flush").value_or(""), "flush-v8.3.7");
    installer.Rollback("flush");
    BOOST_CHECK_EQUAL(installer.Current("flush").value_or(""), "flush-v8.3.7");
    BOOST_CHECK(Contains(ReadAll(*installer.CurrentPackDir("flush") / "assets" / "a.js"), "8.3.7"));
}

BOOST_AUTO_TEST_CASE(pages_from_the_release_else_built_in)
{
    UseNet net{ChainType::ECASH};
    const fs::path dir{m_args.GetDataDirBase() / "pack-flush"};
    WriteAll(dir / "pack.json", PackJson("flush"));
    WriteAll(dir / "index.html", "<html><head><script type=\"module\" src=\"/packs/flush/a.js\"></script></head></html>");
    WriteAll(dir / "a.js", "release");
    const auto built_in{[](const std::string& path) -> std::optional<std::string> {
        const webui::Asset* a{webui::FindAsset(webui::BuiltinAssets(), path)};
        if (!a) return std::nullopt;
        return std::string{reinterpret_cast<const char*>(a->data.data()), a->data.size()};
    }};
    webui::SetPackFolder("flush", dir);
    BOOST_CHECK(webui::PackFolder("flush") == dir);
    BOOST_CHECK_EQUAL(webui::PackFile("/packs/flush/a.js").value_or(""), "release");
    // Its page loads the shim first.
    const std::string page{webui::PackFile("/packs/flush/index.html").value_or("")};
    BOOST_CHECK(page.find("/packs/shim.js") != std::string::npos && page.find("/packs/shim.js") < page.find("/packs/flush/a.js"));
    // Only its own files then (never a mix of two versions), nothing outside it, no link.
    BOOST_CHECK(!webui::PackFile("/packs/flush/missing.js"));
    BOOST_CHECK(!webui::PackFile("/packs/flush/../pack-flush/a.js"));
    std::filesystem::create_symlink(dir / "a.js", dir / "link.js");
    BOOST_CHECK(!webui::PackFile("/packs/flush/link.js"));
    // The methods its pages use come from its pack.json, and the list of packs says it is the release's.
    const std::vector<std::string> methods{webui::SidechainPages::PackMethods("flush")};
    BOOST_CHECK(Has(methods, "listpools") && Has(methods, "swapasset") && Has(methods, "fastblocks.bond"));
    // The copy built in again.
    webui::SetPackFolder("flush", std::nullopt);
    BOOST_CHECK(!webui::PackFolder("flush"));
    BOOST_CHECK(webui::PackFile("/packs/flush/a.js") == built_in("/packs/flush/a.js"));
    // A folder of another sidechain's pages, or another host version, is not taken.
    webui::SetPackFolder("thunder", dir);
    BOOST_CHECK(!webui::PackFolder("thunder"));
    WriteAll(dir / "pack.json", PackJson("flush", 2));
    webui::SetPackFolder("flush", dir);
    BOOST_CHECK(!webui::PackFolder("flush"));
}

BOOST_AUTO_TEST_CASE(what_the_apps_pages_make_of_them)
{
    const ChainAdapter::Capabilities& flush{*ChainAdapter::Find("flush")};
    BOOST_CHECK(flush.assets && flush.dex && flush.staking && flush.dex_features.route && flush.dex_features.curves);
    BOOST_CHECK(!flush.names && !flush.markets && !flush.shielded);
    BOOST_CHECK_EQUAL(ChainAdapter::CoinWord("flush"), "ECX");
    BOOST_CHECK_EQUAL(ChainAdapter::CoinWord("layers"), "CHN");
    const std::vector<std::string> path{std::string(64, '0'), std::string(64, 'a')};
    const UniValue swap{ChainAdapter::SwapRequest("w", path, "1", 50, "0.9", ChainAdapter::CoinWord("flush"))};
    BOOST_CHECK(Contains(swap.write(), "\"ECX\"") && !Contains(swap.write(), "CHN"));
    // Flush's methods named in its daemon's whitelist: Layers' DEX, assets and stake.
    const std::vector<std::string> used{ChainAdapter::MethodsUsed("flush")};
    BOOST_CHECK(Has(used, "listpools") && Has(used, "swappools") && Has(used, "listbonds") && Has(used, "sendasset"));
    // Thunder: a drop-in on eCash (its wallet in its own pages), the template's on Chains' networks.
    {
        UseNet net{ChainType::ECASH};
        const UniValue caps{ChainAdapter::CapabilitiesJson(*ChainAdapter::Find("thunder"))};
        BOOST_CHECK(caps.find_value("dropin").isTrue() && caps.find_value("send").isFalse() && caps.find_value("withdraw").isFalse());
        BOOST_CHECK(ChainAdapter::CapabilitiesJson(flush).find_value("dropin").isFalse());
        BOOST_CHECK_EQUAL(ChainAdapter::CapabilitiesJson(flush).find_value("coin").get_str(), "ECX");
        BOOST_CHECK(ChainAdapter::Dropin("thunder") && !ChainAdapter::Dropin("flush"));
    }
    BOOST_CHECK(!ChainAdapter::Dropin("thunder"));
    BOOST_CHECK(ChainAdapter::CapabilitiesJson(*ChainAdapter::Find("thunder")).find_value("send").isTrue());
}

BOOST_AUTO_TEST_SUITE_END()
