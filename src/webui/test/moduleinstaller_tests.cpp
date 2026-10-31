// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The downloads of the sidechains' programs (webui/moduleinstaller.h, doc/unified-app.md "Downloads and updates"):
// releases served by a fetcher of the tests (no network), with real archives made by tar; and, when python3,
// openssl and curl are there, the fetcher of the app against an https server of this computer.

#include <hash.h>
#include <key.h>
#include <pubkey.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/fs.h>
#include <util/strencodings.h>
#include <webui/moduleinstaller.h>
#include <webui/sidechainnodes.h>
#include <webui/supervisor.h>

#include <boost/test/unit_test.hpp>

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <map>
#include <thread>

#ifndef WIN32
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using webui::ModuleInstaller;
using webui::SidechainNodes;
using namespace std::chrono_literals;

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

/** What the fixture server serves: urls to bytes; how it misbehaves. */
struct Server {
    std::map<std::string, std::string> files;
    //! A download of this url stops half way, with an error.
    std::string fail_half;
    //! A download of this url is slow (the cancel test).
    std::string slow;
    int fetches{0};

    ModuleInstaller::Fetcher Fetcher()
    {
        return [this](const ModuleInstaller::FetchRequest& req) {
            ++fetches;
            ModuleInstaller::FetchResult r;
            const auto it{files.find(req.url)};
            if (it == files.end()) {
                r.error = "The requested URL returned error: 404";
                return r;
            }
            const std::string& data{it->second};
            if (req.url == slow) {
                std::ofstream f{req.out.std_path(), std::ios::binary | std::ios::trunc};
                for (size_t i{0}; i < data.size(); i += 8) {
                    if (req.cancelled && req.cancelled()) {
                        f.close();
                        std::filesystem::remove(req.out);
                        r.cancelled = true;
                        return r;
                    }
                    f.write(data.data() + i, std::min<size_t>(8, data.size() - i));
                    f.flush();
                    if (req.progress) req.progress(i, req.expected);
                    std::this_thread::sleep_for(20ms);
                }
            }
            if (data.size() > req.max_bytes) {
                std::filesystem::remove(req.out);
                r.too_large = true;
                r.error = "Maximum file size exceeded";
                return r;
            }
            if (req.url == fail_half) {
                // A partial file, then the connection lost: as curl, the file of the request is removed.
                WriteAll(req.out, data.substr(0, data.size() / 2));
                std::filesystem::remove(req.out);
                r.error = "Connection reset by peer";
                return r;
            }
            WriteAll(req.out, data);
            if (req.progress) req.progress(data.size(), req.expected);
            r.ok = true;
            return r;
        };
    }
};

struct ReleaseSpec {
    std::string tag;
    bool manifest{true};
    bool sums{true};
    int host_api{ModuleInstaller::HOST_API};
    std::string platform{PLATFORM};
    //! The manifest says another hash than SHA256SUMS.
    bool sums_disagree{false};
    //! The archive served differs from what manifest and sums say.
    bool tampered{false};
    //! "": none; "good": signed with the test key; "bad": a signature of other bytes.
    std::string sig;
    //! Archive member names: "unsafe" (../layersd), "link" (layersd a symlink).
    std::string archive{"plain"};
    int64_t declared_size{-1};
};

struct Fixture {
    fs::path root;
    Server server;
    CKey key;
    std::string key_hex;
    UniValue releases{UniValue::VARR};

    explicit Fixture(const fs::path& r) : root{r}
    {
        key.MakeNewKey(true);
        key_hex = HexStr(XOnlyPubKey{key.GetPubKey()});
    }

    std::string Url(const std::string& tag, const std::string& name) const { return BASE + "/block-fabric/layers/releases/download/" + tag + "/" + name; }

    std::string MakeArchive(const ReleaseSpec& s)
    {
        const fs::path work{root / fs::PathFromString("src-" + s.tag)};
        const std::string top{"layers-" + s.tag + "-" + s.platform};
        const fs::path pkg{work / fs::PathFromString(top)};
        std::filesystem::create_directories(pkg);
        WriteAll(pkg / "layersd", "#!/bin/sh\necho layersd " + s.tag + "\n");
        WriteAll(pkg / "layers-cli", "#!/bin/sh\necho cli\n");
        WriteAll(pkg / "layers-app", "not installed\n");
        WriteAll(pkg / "README.txt", "readme\n");
        const fs::path out{work / "out.tar.gz"};
        std::vector<std::string> argv;
        if (s.archive == "unsafe") {
            // A member named ../layersd: written as given (-P), from inside the package's folder.
            argv = {"tar", "-czPf", fs::PathToString(out), "-C", fs::PathToString(pkg), "../" + top + "/layersd", "layers-cli"};
        } else {
            if (s.archive == "link") {
                std::filesystem::remove(pkg / "layersd");
                std::filesystem::create_symlink("/bin/sh", pkg / "layersd");
            }
            argv = {"tar", "-czf", fs::PathToString(out), "-C", fs::PathToString(work), top};
        }
        const SidechainNodes::Output made{SidechainNodes::Run(argv, 60)};
        BOOST_REQUIRE_MESSAGE(made.code == 0, made.err);
        return ReadAll(out);
    }

    void Add(const ReleaseSpec& s)
    {
        const std::string name{ModuleInstaller::ArchiveName("layers", s.tag, s.platform)};
        const std::string archive{MakeArchive(s)};
        const std::string sha{Sha(archive)};
        UniValue assets{UniValue::VARR};
        const auto asset{[&](const std::string& n, const std::string& data, int64_t size = -1) {
            server.files[Url(s.tag, n)] = data;
            UniValue a{UniValue::VOBJ};
            a.pushKV("name", n);
            a.pushKV("size", size >= 0 ? size : static_cast<int64_t>(data.size()));
            a.pushKV("browser_download_url", "https://evil.example/ignored");
            assets.push_back(std::move(a));
        }};
        const int64_t size{s.declared_size >= 0 ? s.declared_size : static_cast<int64_t>(archive.size())};
        asset(name, s.tampered ? archive + "x" : archive, size);
        if (s.tampered) server.files[Url(s.tag, name)] = archive.substr(0, archive.size() - 1) + "\x01";
        asset("layers-" + s.tag + "-src.tar.gz", "source");
        if (s.sums) asset("SHA256SUMS-layers.txt", (s.sums_disagree ? std::string(64, '0') : sha) + "  " + name + "\n" + Sha("source") + "  layers-" + s.tag + "-src.tar.gz\n");
        std::string manifest_text;
        if (s.manifest) {
            UniValue m{UniValue::VOBJ};
            m.pushKV("format", 1);
            m.pushKV("stem", "layers");
            m.pushKV("version", s.tag);
            m.pushKV("hostApi", s.host_api);
            UniValue entry{UniValue::VOBJ};
            entry.pushKV("file", name);
            entry.pushKV("sha256", sha);
            entry.pushKV("size", size);
            UniValue bins{UniValue::VARR};
            bins.push_back("layersd");
            bins.push_back("layers-cli");
            entry.pushKV("binaries", std::move(bins));
            UniValue platforms{UniValue::VOBJ};
            platforms.pushKV(s.platform, std::move(entry));
            m.pushKV("platforms", std::move(platforms));
            manifest_text = m.write(2) + "\n";
            asset("manifest-layers.json", manifest_text);
        }
        if (!s.sig.empty()) {
            const std::string signed_text{s.sig == "good" ? manifest_text : manifest_text + " "};
            HashWriter hasher{TaggedHash(std::string{ModuleInstaller::SIGNATURE_TAG})};
            hasher.write(std::as_bytes(std::span{signed_text.data(), signed_text.size()}));
            std::vector<unsigned char> sig(64);
            BOOST_REQUIRE(key.SignSchnorr(hasher.GetSHA256(), sig, nullptr, uint256{}));
            asset("manifest-layers.json.sig", HexStr(sig) + "\n");
        }
        UniValue rel{UniValue::VOBJ};
        rel.pushKV("tag_name", s.tag);
        rel.pushKV("name", "Layers " + s.tag);
        rel.pushKV("draft", false);
        rel.pushKV("prerelease", true);
        rel.pushKV("published_at", "2026-10-20T01:59:13Z");
        rel.pushKV("assets", std::move(assets));
        // Newest first, as GitHub lists them.
        UniValue next{UniValue::VARR};
        next.push_back(std::move(rel));
        for (size_t i{0}; i < releases.size(); ++i) next.push_back(releases[i]);
        releases = std::move(next);
        server.files[BASE + "/repos/block-fabric/layers/releases?per_page=10"] = releases.write();
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
        o.fetcher = server.Fetcher();
        return o;
    }
};

const ModuleInstaller::Release* Find(const std::vector<ModuleInstaller::Release>& list, const std::string& tag)
{
    for (const auto& r : list) {
        if (r.tag == tag) return &r;
    }
    return nullptr;
}

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

/** Nothing half done is left in the folder of a sidechain. */
bool Clean(const fs::path& dir)
{
    std::error_code ec;
    for (auto it{std::filesystem::directory_iterator(dir, ec)}; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (fs::PathToString(fs::path{it->path().filename()}).starts_with(".")) return false;
    }
    return true;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(webui_moduleinstaller_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(downloads_of_the_network)
{
    // Backbone's sidechains on eCash (doc/unified-app.md §26): their own repositories (ecash_sidechains_tests.cpp).
    for (const ChainType chain : {ChainType::ECASH, ChainType::ECASH_BETANET, ChainType::ECASH_TESTNET}) {
        BOOST_CHECK_EQUAL(ModuleInstaller::Repos(chain).size(), 2U);
    }
    BOOST_CHECK_EQUAL(ModuleInstaller::Repos(ChainType::REGTEST).size(), 7U);
    BOOST_CHECK_EQUAL(ModuleInstaller::Repos(ChainType::REGTEST, /*ecash_regtest=*/true).size(), 2U);
    ModuleInstaller::UseNetwork(ChainType::ECASH);
    BOOST_CHECK(!ModuleInstaller::IsAllowed("layers"));
    BOOST_CHECK(ModuleInstaller::IsAllowed("flush"));
    BOOST_CHECK_EQUAL(ModuleInstaller::FindRepo("thunder")->repo, "backbone-thunder");
    ModuleInstaller::UseNetwork(ChainType::REGTEST);
    BOOST_CHECK(ModuleInstaller::IsAllowed("layers"));
    BOOST_CHECK(!ModuleInstaller::IsAllowed("flush"));
}

BOOST_AUTO_TEST_CASE(names_tags_platforms)
{
    BOOST_CHECK(ModuleInstaller::IsAllowed("layers"));
    BOOST_CHECK(ModuleInstaller::IsAllowed("hivemind"));
    BOOST_CHECK(!ModuleInstaller::IsAllowed("evil"));
    BOOST_CHECK(!ModuleInstaller::IsAllowed("../layers"));
    BOOST_CHECK_EQUAL(ModuleInstaller::ArchiveName("layers", "20261020.1", "x86_64-linux-gnu"), "layers-20261020.1-x86_64-linux-gnu.tar.gz");
    BOOST_CHECK_EQUAL(ModuleInstaller::ArchiveName("layers", "20261020.1", "win64"), "layers-20261020.1-win64.zip");
    BOOST_CHECK(ModuleInstaller::IsValidTag("20261020.1"));
    BOOST_CHECK(!ModuleInstaller::IsValidTag("../x"));
    BOOST_CHECK(!ModuleInstaller::IsValidTag(".hidden"));
    BOOST_CHECK(!ModuleInstaller::IsValidTag(""));
    BOOST_CHECK(ModuleInstaller::CompareTags("20261020.10", "20261020.9") > 0);
    BOOST_CHECK(ModuleInstaller::CompareTags("20261021.1", "20261020.9") > 0);
    BOOST_CHECK_EQUAL(ModuleInstaller::CompareTags("20261020.1", "20261020.1"), 0);
    BOOST_CHECK(ModuleInstaller::CompareTags("20261019.4", "20261020.1") < 0);
#if defined(__linux__) && defined(__x86_64__)
    BOOST_CHECK_EQUAL(ModuleInstaller::Platform(), "x86_64-linux-gnu");
#endif
    // No release key is built in yet (the maintainer makes it offline).
    BOOST_CHECK(ModuleInstaller::ReleaseKeys().empty());
}

BOOST_AUTO_TEST_CASE(install_update_rollback)
{
    Fixture fx{m_args.GetDataDirBase() / "mi_install"};
    fx.Add({.tag = "20261021.1", .sig = "good"});
    fx.Add({.tag = "20261022.1", .sig = "good"});
    ModuleInstaller mi{fx.Options("auto", /*trust_key=*/true)};
    const auto list{mi.Releases("layers")};
    BOOST_REQUIRE_EQUAL(list.size(), 2U);
    BOOST_CHECK_EQUAL(list[0].tag, "20261022.1");
    BOOST_CHECK(list[0].compatible);
    BOOST_CHECK(list[0].signed_ok);
    BOOST_CHECK(!mi.Current("layers"));

    mi.InstallNow("layers", "20261021.1");
    BOOST_CHECK_EQUAL(*mi.Current("layers"), "20261021.1");
    BOOST_CHECK(!mi.Previous("layers"));
    const fs::path dir{fx.root / "bin" / "layers" / "20261021.1"};
    BOOST_CHECK(*mi.CurrentDir("layers") == dir);
    // Only the programs named in the manifest, as plain executable files.
    BOOST_CHECK(std::filesystem::is_regular_file(dir / "layersd"));
    BOOST_CHECK(std::filesystem::is_regular_file(dir / "layers-cli"));
    BOOST_CHECK(!std::filesystem::exists(dir / "layers-app"));
    BOOST_CHECK(!std::filesystem::exists(dir / "README.txt"));
    BOOST_CHECK((std::filesystem::status(dir / "layersd").permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none);
    BOOST_CHECK(Contains(ReadAll(dir / "release.json"), "\"signed\": true"));
    BOOST_CHECK(Clean(fx.root / "bin" / "layers"));
    BOOST_CHECK_EQUAL(*mi.UpdateFor("layers"), "20261022.1");
    BOOST_CHECK(mi.ReleasesJson("layers", "download").find_value("updateAvailable").isTrue());

    // An update installed without being made current (the daemon is stopped first), then made current.
    mi.InstallNow("layers", "20261022.1", /*activate=*/false);
    BOOST_CHECK_EQUAL(*mi.Current("layers"), "20261021.1");
    mi.Activate("layers", "20261022.1");
    BOOST_CHECK_EQUAL(*mi.Current("layers"), "20261022.1");
    BOOST_CHECK_EQUAL(*mi.Previous("layers"), "20261021.1");
    BOOST_CHECK(!mi.UpdateFor("layers"));

    mi.Rollback("layers");
    BOOST_CHECK_EQUAL(*mi.Current("layers"), "20261021.1");
    BOOST_CHECK_EQUAL(*mi.Previous("layers"), "20261022.1");
    mi.Rollback("layers");
    BOOST_CHECK_EQUAL(*mi.Current("layers"), "20261022.1");

    // A failed install of a newer release (its package changed on the way) keeps what is there.
    fx.Add({.tag = "20261023.1", .tampered = true, .sig = "good"});
    const std::string err{ErrorOf([&] { mi.InstallNow("layers", "20261023.1"); })};
    BOOST_CHECK_MESSAGE(Contains(err, "does not match its SHA-256"), err);
    BOOST_CHECK_EQUAL(*mi.Current("layers"), "20261022.1");
    BOOST_CHECK_EQUAL(*mi.Previous("layers"), "20261021.1");
    BOOST_CHECK(!std::filesystem::exists(fx.root / "bin" / "layers" / "20261023.1"));
    BOOST_CHECK(Clean(fx.root / "bin" / "layers"));

    // A third version: the oldest goes, the one before the current stays for rollback.
    fx.Add({.tag = "20261024.1", .sig = "good"});
    mi.InstallNow("layers", "20261024.1");
    const auto installed{mi.Installed("layers")};
    BOOST_CHECK_EQUAL(installed.size(), 2U);
    BOOST_CHECK_EQUAL(*mi.Current("layers"), "20261024.1");
    BOOST_CHECK_EQUAL(*mi.Previous("layers"), "20261022.1");
    BOOST_CHECK(!std::filesystem::exists(fx.root / "bin" / "layers" / "20261021.1"));

    // An older release only when the user chose an older version.
    const std::string older{ErrorOf([&] { mi.InstallNow("layers", "20261021.1"); })};
    BOOST_CHECK_MESSAGE(Contains(older, "older than the version installed"), older);
    BOOST_CHECK_EQUAL(*mi.Current("layers"), "20261024.1");
    mi.InstallNow("layers", "20261021.1", /*activate=*/true, /*allow_older=*/true);
    BOOST_CHECK_EQUAL(*mi.Current("layers"), "20261021.1");
    BOOST_CHECK_EQUAL(*mi.Previous("layers"), "20261024.1");
    mi.Rollback("layers");
    BOOST_CHECK_EQUAL(*mi.Current("layers"), "20261024.1");
    BOOST_CHECK_EQUAL(*mi.Previous("layers"), "20261021.1");
    std::filesystem::remove_all(fx.root / "bin" / "layers" / "20261021.1");

    // Rolling back to a version gone: refused.
    std::filesystem::remove_all(fx.root / "bin" / "layers" / "20261022.1");
    BOOST_CHECK(Contains(ErrorOf([&] { mi.Rollback("layers"); }), "no earlier version"));
}

BOOST_AUTO_TEST_CASE(checksum_mismatch)
{
    Fixture fx{m_args.GetDataDirBase() / "mi_sums"};
    fx.Add({.tag = "20261021.1", .sums_disagree = true});
    fx.Add({.tag = "20261022.1", .tampered = true});
    ModuleInstaller mi{fx.Options()};
    const auto list{mi.Releases("layers")};
    const auto* disagree{Find(list, "20261021.1")};
    BOOST_REQUIRE(disagree);
    BOOST_CHECK(!disagree->compatible);
    BOOST_CHECK_MESSAGE(Contains(disagree->reason, "other hashes"), disagree->reason);
    BOOST_CHECK(Contains(ErrorOf([&] { mi.InstallNow("layers", "20261021.1"); }), "cannot be used"));
    const std::string err{ErrorOf([&] { mi.InstallNow("layers", "20261022.1"); })};
    BOOST_CHECK_MESSAGE(Contains(err, "does not match its SHA-256") && Contains(err, "Nothing was installed"), err);
    BOOST_CHECK(!mi.Current("layers"));
    BOOST_CHECK(mi.Installed("layers").empty());
    BOOST_CHECK(Clean(fx.root / "bin" / "layers"));
}

BOOST_AUTO_TEST_CASE(wrong_os_only)
{
    Fixture fx{m_args.GetDataDirBase() / "mi_os"};
    fx.Add({.tag = "20261021.1", .platform = "win64"});
    ModuleInstaller mi{fx.Options()};
    const auto list{mi.Releases("layers")};
    BOOST_REQUIRE_EQUAL(list.size(), 1U);
    BOOST_CHECK(!list[0].compatible);
    BOOST_CHECK(list[0].asset_name.empty());
    BOOST_CHECK_MESSAGE(Contains(list[0].reason, "No package for this computer (x86_64-linux-gnu)"), list[0].reason);
    const UniValue json{mi.ReleasesJson("layers", "none")};
    BOOST_CHECK(json.find_value("newestCompatible").isNull());
    BOOST_CHECK(json.find_value("releases")[0].find_value("asset").isNull());
}

BOOST_AUTO_TEST_CASE(oversize)
{
    Fixture fx{m_args.GetDataDirBase() / "mi_big"};
    // A package larger than its release says: stopped at the size said.
    fx.Add({.tag = "20261021.1"});
    const std::string name{ModuleInstaller::ArchiveName("layers", "20261021.1", PLATFORM)};
    fx.server.files[fx.Url("20261021.1", name)] += std::string(4096, 'x');
    // A release whose package is said to be over the limit: never downloaded.
    fx.Add({.tag = "20261022.1", .declared_size = static_cast<int64_t>(ModuleInstaller::MAX_ARCHIVE_BYTES) + 1});
    ModuleInstaller mi{fx.Options()};
    const auto list{mi.Releases("layers")};
    const auto* huge{Find(list, "20261022.1")};
    BOOST_REQUIRE(huge);
    BOOST_CHECK(!huge->compatible);
    BOOST_CHECK_MESSAGE(Contains(huge->reason, "too large"), huge->reason);
    const std::string err{ErrorOf([&] { mi.InstallNow("layers", "20261021.1"); })};
    BOOST_CHECK_MESSAGE(Contains(err, "larger than its release says"), err);
    BOOST_CHECK(Clean(fx.root / "bin" / "layers"));

    // A list of releases over 2 MB: refused.
    Fixture fx2{m_args.GetDataDirBase() / "mi_bigapi"};
    fx2.server.files[BASE + "/repos/block-fabric/layers/releases?per_page=10"] = "[" + std::string(ModuleInstaller::MAX_API_BYTES, ' ') + "]";
    ModuleInstaller mi2{fx2.Options()};
    const std::string err2{ErrorOf([&] { mi2.Releases("layers"); })};
    BOOST_CHECK_MESSAGE(Contains(err2, "larger than allowed"), err2);
}

BOOST_AUTO_TEST_CASE(partial_download)
{
    Fixture fx{m_args.GetDataDirBase() / "mi_partial"};
    fx.Add({.tag = "20261021.1"});
    fx.Add({.tag = "20261022.1"});
    ModuleInstaller mi{fx.Options()};
    mi.InstallNow("layers", "20261021.1");
    fx.server.fail_half = fx.Url("20261022.1", ModuleInstaller::ArchiveName("layers", "20261022.1", PLATFORM));
    const std::string err{ErrorOf([&] { mi.InstallNow("layers", "20261022.1"); })};
    BOOST_CHECK_MESSAGE(Contains(err, "download failed") && Contains(err, "Connection reset"), err);
    BOOST_CHECK_EQUAL(*mi.Current("layers"), "20261021.1");
    BOOST_CHECK(!std::filesystem::exists(fx.root / "bin" / "layers" / "20261022.1"));
    BOOST_CHECK(Clean(fx.root / "bin" / "layers"));
    // Retried once the connection is back.
    fx.server.fail_half.clear();
    mi.InstallNow("layers", "20261022.1");
    BOOST_CHECK_EQUAL(*mi.Current("layers"), "20261022.1");
}

BOOST_AUTO_TEST_CASE(incompatible_versions)
{
    Fixture fx{m_args.GetDataDirBase() / "mi_compat"};
    // As the public release 20261020.1: a package and SHA256SUMS, no manifest (its daemon has no host over RPC).
    fx.Add({.tag = "20261020.1", .manifest = false});
    fx.Add({.tag = "20261021.1", .host_api = ModuleInstaller::HOST_API + 1});
    fx.Add({.tag = "20261021.2", .host_api = 0});
    fx.Add({.tag = "20261019.1", .sums = false});
    ModuleInstaller mi{fx.Options()};
    const auto list{mi.Releases("layers")};
    BOOST_CHECK(Contains(Find(list, "20261020.1")->reason, "Made before this app could run it"));
    BOOST_CHECK(Contains(Find(list, "20261021.1")->reason, "newer app"));
    BOOST_CHECK(Contains(Find(list, "20261021.2")->reason, "older app"));
    BOOST_CHECK(Contains(Find(list, "20261019.1")->reason, "SHA256SUMS"));
    for (const auto& r : list) BOOST_CHECK(!r.compatible);
    BOOST_CHECK(Contains(ErrorOf([&] { mi.InstallNow("layers", "20261020.1"); }), "Made before"));
    BOOST_CHECK(!mi.Current("layers"));
    // A compatible older one can still be chosen, though not the newest.
    fx.Add({.tag = "20261018.1"});
    const auto again{mi.Releases("layers", /*force=*/true)};
    BOOST_CHECK(Find(again, "20261018.1")->compatible);
    BOOST_CHECK_EQUAL(mi.ReleasesJson("layers", "none").find_value("newestCompatible").get_str(), "20261018.1");
    mi.InstallNow("layers", "20261018.1");
    BOOST_CHECK_EQUAL(*mi.Current("layers"), "20261018.1");
}

BOOST_AUTO_TEST_CASE(signatures)
{
    Fixture fx{m_args.GetDataDirBase() / "mi_sig"};
    fx.Add({.tag = "20261021.1", .sig = "good"});
    fx.Add({.tag = "20261022.1", .sig = "bad"});
    fx.Add({.tag = "20261023.1"});
    {
        // A key is configured: a signature is needed in auto too (leaving it out is no way around it).
        ModuleInstaller mi{fx.Options("auto", /*trust_key=*/true)};
        const auto list{mi.Releases("layers")};
        BOOST_CHECK(Find(list, "20261021.1")->compatible && Find(list, "20261021.1")->signed_ok);
        BOOST_CHECK(!Find(list, "20261021.1")->unsigned_only);
        BOOST_CHECK(!Find(list, "20261022.1")->compatible);
        BOOST_CHECK(Contains(Find(list, "20261022.1")->reason, "signature does not verify"));
        BOOST_CHECK(!Find(list, "20261023.1")->compatible);
        BOOST_CHECK(Contains(Find(list, "20261023.1")->reason, "Not signed by the release key"));
        BOOST_CHECK(Contains(ErrorOf([&] { mi.InstallNow("layers", "20261023.1"); }), "Not signed"));
        const UniValue json{mi.ReleasesJson("layers", "none")};
        BOOST_CHECK_EQUAL(json.find_value("keys").getInt<int>(), 1);
        BOOST_CHECK(json.find_value("releases")[0].find_value("unsigned").isFalse());
    }
    {
        ModuleInstaller mi{fx.Options("require", /*trust_key=*/true)};
        const auto list{mi.Releases("layers")};
        BOOST_CHECK(Find(list, "20261021.1")->compatible);
        BOOST_CHECK(!Find(list, "20261022.1")->compatible);
        BOOST_CHECK(!Find(list, "20261023.1")->compatible);
    }
    {
        // Another key: the good signature is not by a key of this app.
        ModuleInstaller::Options o{fx.Options("require", /*trust_key=*/false)};
        CKey other;
        other.MakeNewKey(true);
        o.extra_keys = {HexStr(XOnlyPubKey{other.GetPubKey()})};
        ModuleInstaller mi{o};
        BOOST_CHECK(!Find(mi.Releases("layers"), "20261021.1")->compatible);
    }
    {
        // No key at all (a release build today): checksums only, each release said unsigned; required, nothing is taken.
        ModuleInstaller auto_mi{fx.Options("auto", /*trust_key=*/false)};
        const auto releases{auto_mi.Releases("layers")};
        for (const char* tag : {"20261021.1", "20261022.1", "20261023.1"}) {
            const auto* r{Find(releases, tag)};
            BOOST_CHECK(r->compatible && !r->signed_ok && r->unsigned_only);
        }
        const UniValue json{auto_mi.ReleasesJson("layers", "none")};
        BOOST_CHECK_EQUAL(json.find_value("keys").getInt<int>(), 0);
        BOOST_CHECK(json.find_value("releases")[0].find_value("unsigned").isTrue());
        BOOST_CHECK(json.find_value("releases")[0].find_value("signed").isFalse());
        ModuleInstaller req_mi{fx.Options("require", /*trust_key=*/false)};
        BOOST_CHECK(Contains(Find(req_mi.Releases("layers"), "20261021.1")->reason, "no release key"));
    }
    BOOST_CHECK(ModuleInstaller::VerifyManifestSignature("x", std::string(128, '0'), {fx.key_hex}) == false);
    BOOST_CHECK(ModuleInstaller::VerifyManifestSignature("x", "zz", {fx.key_hex}) == false);
}

BOOST_AUTO_TEST_CASE(unsafe_archives)
{
    Fixture fx{m_args.GetDataDirBase() / "mi_unsafe"};
    fx.Add({.tag = "20261021.1", .archive = "unsafe"});
    fx.Add({.tag = "20261022.1", .archive = "link"});
    ModuleInstaller mi{fx.Options()};
    const std::string e1{ErrorOf([&] { mi.InstallNow("layers", "20261021.1"); })};
    BOOST_CHECK_MESSAGE(Contains(e1, "outside its folder"), e1);
    const std::string e2{ErrorOf([&] { mi.InstallNow("layers", "20261022.1"); })};
    BOOST_CHECK_MESSAGE(Contains(e2, "not a plain file"), e2);
    BOOST_CHECK(!mi.Current("layers"));
    BOOST_CHECK(mi.Installed("layers").empty());
    BOOST_CHECK(Clean(fx.root / "bin" / "layers"));
    BOOST_CHECK(!std::filesystem::exists(fx.root / "bin" / "layersd"));
}

BOOST_AUTO_TEST_CASE(job_progress_and_cancel)
{
    Fixture fx{m_args.GetDataDirBase() / "mi_cancel"};
    fx.Add({.tag = "20261021.1"});
    int notified{0};
    ModuleInstaller mi{fx.Options(), [&](const std::string&) { ++notified; }};
    BOOST_CHECK_EQUAL(mi.Progress("layers").find_value("state").get_str(), "idle");
    mi.Start("layers", "20261021.1", true);
    BOOST_REQUIRE(mi.Wait("layers", 30s));
    BOOST_CHECK_EQUAL(mi.Progress("layers").find_value("state").get_str(), "done");
    BOOST_CHECK(notified > 0);
    BOOST_CHECK_EQUAL(*mi.Current("layers"), "20261021.1");

    fx.Add({.tag = "20261022.1"});
    fx.server.slow = fx.Url("20261022.1", ModuleInstaller::ArchiveName("layers", "20261022.1", PLATFORM));
    std::atomic<int> done_calls{0};
    std::atomic<bool> done_ok{true};
    mi.Start("layers", "20261022.1", true, [&](bool ok) {
        done_ok = ok;
        ++done_calls;
    });
    BOOST_CHECK(Contains(ErrorOf([&] { mi.Start("layers", "20261022.1", true); }), "already running"));
    for (int i{0}; i < 200 && mi.Progress("layers").find_value("state").get_str() != "downloading"; ++i) std::this_thread::sleep_for(10ms);
    mi.Cancel("layers");
    BOOST_REQUIRE(mi.Wait("layers", 30s));
    BOOST_CHECK_EQUAL(mi.Progress("layers").find_value("state").get_str(), "cancelled");
    BOOST_CHECK_EQUAL(done_calls.load(), 1);
    BOOST_CHECK(!done_ok.load());
    BOOST_CHECK_EQUAL(*mi.Current("layers"), "20261021.1");
    BOOST_CHECK(Clean(fx.root / "bin" / "layers"));
}

BOOST_AUTO_TEST_CASE(stop_waits_for_the_jobs)
{
    // The app stops (doc/unified-app.md §17, the crash at exit): Stop() cancels a download, waits for its thread and
    // its `done` (which may still be running), and no job starts after.
    Fixture fx{m_args.GetDataDirBase() / "mi_stop"};
    fx.Add({.tag = "20261021.1"});
    fx.server.slow = fx.Url("20261021.1", ModuleInstaller::ArchiveName("layers", "20261021.1", PLATFORM));
    ModuleInstaller mi{fx.Options()};
    std::atomic<bool> done_started{false};
    std::atomic<bool> done_finished{false};
    mi.Start("layers", "20261021.1", true, [&](bool) {
        done_started = true;
        // An end of an update that still waits for its daemon.
        std::this_thread::sleep_for(300ms);
        done_finished = true;
    });
    for (int i{0}; i < 200 && mi.Progress("layers").find_value("state").get_str() != "downloading"; ++i) std::this_thread::sleep_for(10ms);
    mi.Stop();
    BOOST_CHECK(done_started.load());
    BOOST_CHECK(done_finished.load());
    BOOST_CHECK(!mi.Busy("layers"));
    BOOST_CHECK_EQUAL(mi.Progress("layers").find_value("state").get_str(), "cancelled");
    BOOST_CHECK(!mi.Current("layers"));
    BOOST_CHECK(Contains(ErrorOf([&] { mi.Start("layers", "20261021.1", true); }), "stopping"));
    mi.Stop();
}

BOOST_AUTO_TEST_CASE(supervisor_finds_the_download)
{
    const fs::path base{m_args.GetDataDirBase() / "mi_super"};
    const fs::path bundled{base / "bundled"};
    const fs::path downloaded{base / "downloaded"};
    WriteAll(bundled / "layersd", "b");
    webui::SidechainSupervisor::Options o;
    o.chains_datadir = base / "chains";
    o.chain = ChainType::REGTEST;
    o.bin_dirs = {bundled};
    bool have_download{false};
    o.downloaded = [&](const std::string& stem) -> std::optional<fs::path> {
        if (stem == "layers" && have_download) return downloaded;
        return std::nullopt;
    };
    UniValue settings{UniValue::VOBJ};
    webui::SidechainSupervisor s{o, [&] { return settings; }, [&](const UniValue& v) { settings = v; }};
    BOOST_CHECK(*s.FindBinary("layers") == bundled / "layersd");
    BOOST_CHECK_EQUAL(s.BinarySource("layers"), "bundled");
    BOOST_CHECK_EQUAL(s.BinarySource("thunder"), "none");
    WriteAll(downloaded / "layersd", "d");
    have_download = true;
    // A version downloaded comes before the bundled one (the bundled one stays the offline path).
    BOOST_CHECK(*s.FindBinary("layers") == downloaded / "layersd");
    BOOST_CHECK_EQUAL(s.BinarySource("layers"), "download");
}

#ifndef WIN32
BOOST_AUTO_TEST_CASE(curl_fetcher_local_https)
{
    // The fetcher of the app against an https server of this computer (skipped without python3, openssl or curl).
    const fs::path dir{m_args.GetDataDirBase() / "mi_https"};
    const fs::path www{dir / "www"};
    WriteAll(www / "small.txt", "hello modules\n");
    WriteAll(www / "big.bin", std::string(100000, 'b'));
    const fs::path cert{dir / "cert.pem"}, key{dir / "key.pem"};
    const auto ssl{SidechainNodes::Run({"openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-subj", "/CN=127.0.0.1", "-addext", "subjectAltName=IP:127.0.0.1",
                                        "-keyout", fs::PathToString(key), "-out", fs::PathToString(cert)}, 60)};
    if (ssl.code != 0 || SidechainNodes::Run({"curl", "--version"}, 10).code != 0 || SidechainNodes::Run({"python3", "--version"}, 10).code != 0) {
        BOOST_TEST_MESSAGE("openssl, curl or python3 missing: the https test is skipped");
        return;
    }
    const int port{49231};
    const std::string script{strprintf(
        "import http.server,ssl,functools,sys\n"
        "h=functools.partial(http.server.SimpleHTTPRequestHandler,directory=sys.argv[1])\n"
        "s=http.server.ThreadingHTTPServer(('127.0.0.1',%d),h)\n"
        "c=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);c.load_cert_chain(sys.argv[2],sys.argv[3])\n"
        "s.socket=c.wrap_socket(s.socket,server_side=True)\n"
        "s.serve_forever()\n", port)};
    WriteAll(dir / "serve.py", script);
    const pid_t pid{fork()};
    if (pid == 0) {
        const int devnull{open("/dev/null", O_RDWR)};
        dup2(devnull, 0);
        dup2(devnull, 1);
        dup2(devnull, 2);
        execlp("python3", "python3", "-I", fs::PathToString(dir / "serve.py").c_str(), fs::PathToString(www).c_str(), fs::PathToString(cert).c_str(), fs::PathToString(key).c_str(), nullptr);
        _exit(127);
    }
    BOOST_REQUIRE(pid > 0);
    const auto fetcher{ModuleInstaller::CurlFetcher(cert)};
    const std::string url{strprintf("https://127.0.0.1:%d", port)};
    ModuleInstaller::FetchRequest req;
    req.max_bytes = 1024;
    req.out = dir / "out";
    ModuleInstaller::FetchResult got;
    for (int i{0}; i < 50; ++i) {
        req.url = url + "/small.txt";
        got = fetcher(req);
        if (got.ok) break;
        std::this_thread::sleep_for(100ms);
    }
    BOOST_CHECK_MESSAGE(got.ok, got.error);
    BOOST_CHECK_EQUAL(ReadAll(dir / "out"), "hello modules\n");
    // Over the limit: stopped, nothing left.
    req.url = url + "/big.bin";
    got = fetcher(req);
    BOOST_CHECK(!got.ok && got.too_large);
    BOOST_CHECK(!std::filesystem::exists(dir / "out"));
    // Not found.
    req.url = url + "/none";
    got = fetcher(req);
    BOOST_CHECK(!got.ok && Contains(got.error, "404"));
    // http: never.
    req.url = strprintf("http://127.0.0.1:%d/small.txt", port);
    got = fetcher(req);
    BOOST_CHECK(!got.ok && Contains(got.error, "https"));
    // A certificate not trusted: refused (no --cacert).
    req.url = url + "/small.txt";
    got = ModuleInstaller::CurlFetcher()(req);
    BOOST_CHECK(!got.ok);
    kill(pid, SIGTERM);
    int status;
    waitpid(pid, &status, 0);
}

BOOST_AUTO_TEST_CASE(curl_installer_against_mirror)
{
    // The whole path of the app (curl, tar, the checks) against the mirror of the e2e tests (release_mirror.py), with
    // real programs: CHAINS_TEST_SIDECHAIN_BINDIR=~/skydoge/layers/build_rel/bin. Skipped without them.
    const char* bindir{std::getenv("CHAINS_TEST_SIDECHAIN_BINDIR")};
    if (!bindir || !std::filesystem::exists(fs::PathFromString(bindir) / "layersd")) {
        BOOST_TEST_MESSAGE("CHAINS_TEST_SIDECHAIN_BINDIR not set: the mirror test is skipped");
        return;
    }
    const fs::path dir{m_args.GetDataDirBase() / "mi_mirror"};
    // __FILE__ is relative to src/ in this build: the tests run from the top of the repository (or say where it is).
    fs::path script{fs::PathFromString(__FILE__).parent_path() / "e2e" / "release_mirror.py"};
    if (const char* given{std::getenv("CHAINS_TEST_RELEASE_MIRROR")}) script = fs::PathFromString(given);
    else if (!script.is_absolute() && !std::filesystem::exists(script)) script = fs::current_path() / "src" / script;
    if (!std::filesystem::exists(script)) {
        BOOST_TEST_MESSAGE("release_mirror.py not found (CHAINS_TEST_RELEASE_MIRROR): the mirror test is skipped");
        return;
    }
    const int port{49235};
    const std::string base{strprintf("https://127.0.0.1:%d", port)};
    const auto made{SidechainNodes::Run({"python3", fs::PathToString(script), "make", "--stem", "layers", "--bindir", bindir, "--tag", "20261021.1", "--tag", "20261022.1",
                                         "--out", fs::PathToString(dir / "www"), "--base-url", base}, 300)};
    BOOST_REQUIRE_MESSAGE(made.code == 0, made.err);
    const fs::path cert{dir / "cert.pem"}, key{dir / "key.pem"};
    const pid_t pid{fork()};
    if (pid == 0) {
        const int devnull{open("/dev/null", O_RDWR)};
        dup2(devnull, 0);
        dup2(devnull, 1);
        dup2(devnull, 2);
        execlp("python3", "python3", fs::PathToString(script).c_str(), "serve", "--dir", fs::PathToString(dir / "www").c_str(), "--port", strprintf("%d", port).c_str(),
               "--cert", fs::PathToString(cert).c_str(), "--key", fs::PathToString(key).c_str(), nullptr);
        _exit(127);
    }
    BOOST_REQUIRE(pid > 0);
    for (int i{0}; i < 100 && !std::filesystem::exists(key); ++i) std::this_thread::sleep_for(100ms);
    std::this_thread::sleep_for(500ms);
    // The key of release_mirror.py (TEST_PUBKEY_HEX), required.
    ModuleInstaller::Options o;
    o.bin_root = dir / "bin";
    o.api_base = base;
    o.download_base = base;
    o.platform = PLATFORM;
    o.signatures = "require";
    o.extra_keys = {"269e50f918604bbbb0c70239d05212bb3dd9de866e2704d48f44c47e6222373b"};
    o.fetcher = ModuleInstaller::CurlFetcher(cert);
    ModuleInstaller mi{o};
    std::vector<ModuleInstaller::Release> list;
    for (int i{0}; i < 30; ++i) {
        try {
            list = mi.Releases("layers", true);
            break;
        } catch (const std::exception&) {
            std::this_thread::sleep_for(200ms);
        }
    }
    BOOST_REQUIRE_EQUAL(list.size(), 2U);
    BOOST_CHECK(list[0].compatible && list[0].signed_ok);
    mi.Start("layers", "20261021.1", true);
    BOOST_REQUIRE(mi.Wait("layers", 120s));
    BOOST_CHECK_MESSAGE(mi.Progress("layers").find_value("state").get_str() == "done", mi.Progress("layers").write());
    BOOST_CHECK_EQUAL(*mi.UpdateFor("layers"), "20261022.1");
    mi.InstallNow("layers", "20261022.1");
    const auto version{SidechainNodes::Run({fs::PathToString(*mi.CurrentDir("layers") / "layersd"), "-version"}, 30)};
    BOOST_CHECK_MESSAGE(Contains(version.out, "Layers"), version.out + version.err);
    kill(pid, SIGTERM);
    int status;
    waitpid(pid, &status, 0);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
