// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The supervisor of the sidechains of the Chains app (webui/supervisor.h, doc/unified-app.md). The tests
// that run real daemons need the programs of a sidechain (Layers):
//   CHAINS_TEST_SIDECHAIN_BINDIR=~/skydoge/layers/build_rel/bin test_chains-app -t webui_supervisor_tests
// and chainsd next to test_chains-app (or CHAINS_TEST_CHAINSD); without them, those tests say so and pass.

#include <test/util/setup_common.h>
#include <util/fs.h>
#include <util/fs_helpers.h>
#include <webui/supervisor.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <thread>

#ifndef WIN32
#include <csignal>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using webui::SidechainSupervisor;
using namespace std::chrono_literals;

namespace {
struct Memory {
    UniValue value{UniValue::VOBJ};
    SidechainSupervisor::LoadFn Load() { return [this] { return value; }; }
    SidechainSupervisor::StoreFn Store() { return [this](const UniValue& v) { value = v; }; }
};

bool Has(const std::vector<std::string>& args, const std::string& arg) { return std::find(args.begin(), args.end(), arg) != args.end(); }

SidechainSupervisor::Options Regtest(const fs::path& base)
{
    SidechainSupervisor::Options o;
    o.chains_datadir = base / "chains";
    o.chain = ChainType::REGTEST;
    o.mainchain_rpc_port = 49311;
    o.mainchain_rpc_user = "chains-app-sidechains";
    o.mainchain_rpc_password = "0123456789abcdef";
    o.port_base = 49320;
    o.extra_args = {"-listen=0"};
    o.stop_timeout = 60s;
    o.poll = 50ms;
    o.backoff = {300ms, 300ms};
    return o;
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(webui_supervisor_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(catalog_of_the_network)
{
    // Backbone (doc/unified-app.md §26, P1): Flush and Thunder on eCash's main network, Flush on its test network, none on
    // the retired betanet; Chains' elsewhere (ecash_tests.cpp has the rows in full).
    BOOST_CHECK_EQUAL(SidechainSupervisor::Catalog(ChainType::ECASH).size(), 2U);
    BOOST_CHECK_EQUAL(SidechainSupervisor::Catalog(ChainType::ECASH_TESTNET).size(), 1U);
    BOOST_CHECK(SidechainSupervisor::Catalog(ChainType::ECASH_BETANET).empty());
    for (const ChainType chain : {ChainType::MAIN, ChainType::TESTNET, ChainType::SIGNET, ChainType::REGTEST}) {
        BOOST_CHECK_EQUAL(SidechainSupervisor::Catalog(chain).size(), 7U);
    }
    // The network the app runs decides what FindSpec finds.
    SidechainSupervisor::UseNetwork(ChainType::ECASH);
    BOOST_CHECK(!SidechainSupervisor::FindSpec("layers"));
    BOOST_CHECK(SidechainSupervisor::FindSpec("flush"));
    BOOST_CHECK(SidechainSupervisor::FindSpec("thunder")->kind == SidechainSupervisor::Kind::DROPIN);
    SidechainSupervisor::UseNetwork(ChainType::REGTEST);
    BOOST_CHECK(SidechainSupervisor::FindSpec("layers"));
    BOOST_CHECK(SidechainSupervisor::FindSpec("thunder")->kind == SidechainSupervisor::Kind::TEMPLATE);
}

BOOST_AUTO_TEST_CASE(catalog_and_ports)
{
    BOOST_CHECK(SidechainSupervisor::FindSpec("layers"));
    BOOST_CHECK(!SidechainSupervisor::FindSpec("nope"));
    Memory memory;
    SidechainSupervisor::Options o{Regtest(m_args.GetDataDirBase())};
    o.port_base = 0;
    {
        SidechainSupervisor s{o, memory.Load(), memory.Store()};
        // The default ports of each sidechain: <net>9<code>54/55.
        BOOST_CHECK_EQUAL(s.RpcPort("layers"), 29254);
        BOOST_CHECK_EQUAL(s.P2pPort("layers"), 29255);
        BOOST_CHECK_EQUAL(s.RpcPort("thunder"), 29754);
        BOOST_CHECK_EQUAL(s.RpcPort("fastblocks"), 29154);
    }
    o.chain = ChainType::TESTNET;
    {
        SidechainSupervisor s{o, memory.Load(), memory.Store()};
        BOOST_CHECK_EQUAL(s.RpcPort("layers"), 19254);
        BOOST_CHECK_EQUAL(s.RpcPort("hivemind"), 19854);
        BOOST_CHECK_EQUAL(s.RpcPort("bitnames"), 19954);
    }
    o.port_base = 49320;
    SidechainSupervisor s{o, memory.Load(), memory.Store()};
    BOOST_CHECK_EQUAL(s.RpcPort("layers"), 49320);
    BOOST_CHECK_EQUAL(s.P2pPort("layers"), 49321);
    BOOST_CHECK_EQUAL(s.RpcPort("fastblocks"), 49322);
}

BOOST_AUTO_TEST_CASE(auto_wiring_args)
{
    Memory memory;
    const fs::path base{m_args.GetDataDirBase()};
    SidechainSupervisor s{Regtest(base), memory.Load(), memory.Store()};
    const auto args{s.DaemonArgs("layers")};
    const std::string data{fs::PathToString(base / "chains" / "sidechains" / "layers" / "data")};
    BOOST_CHECK(Has(args, "-datadir=" + data));
    BOOST_CHECK(Has(args, "-regtest"));
    BOOST_CHECK(Has(args, "-sidechainslot=7"));
    BOOST_CHECK(Has(args, "-server=1"));
    BOOST_CHECK(Has(args, "-rpcport=49320"));
    BOOST_CHECK(Has(args, "-port=49321"));
    BOOST_CHECK(Has(args, "-mainchainrpcconnect=127.0.0.1"));
    BOOST_CHECK(Has(args, "-mainchainrpcport=49311"));
    BOOST_CHECK(Has(args, "-mainchainrpcuser=chains-app-sidechains"));
    BOOST_CHECK(Has(args, "-mainchainrpcpasswordfile=" + data + "/mainchain.rpcpass"));
    BOOST_CHECK(Has(args, "-listen=0"));
    // This app is a user of its own at the daemon; the user's cookie keeps everything (-rpcwhitelistdefault=0). The daemon
    // runs what the pages ask (webuihost) only when this user may call what it exercises (remotehost.cpp HostPrivileges,
    // CheckRpcCaller): the whitelist names what the pages use, no more (doc §13.5 #59). Files: only from this user.
    BOOST_CHECK(std::any_of(args.begin(), args.end(), [](const std::string& a) { return a.starts_with("-rpcauth=chains-app:"); }));
    BOOST_CHECK(Has(args, "-rpcwhitelistdefault=0"));
    BOOST_CHECK(Has(args, "-webuichooser=chains-app"));
    const auto wl{std::find_if(args.begin(), args.end(), [](const std::string& a) { return a.starts_with("-rpcwhitelist=chains-app:"); })};
    BOOST_REQUIRE(wl != args.end());
    const auto listed{[&](const std::string& whitelist, const std::string& m) {
        const std::string names{"," + whitelist.substr(whitelist.find(':') + 1) + ","};
        return names.find("," + m + ",") != std::string::npos;
    }};
    const std::vector<std::string>& kit{SidechainSupervisor::AppRpcMethods()};
    BOOST_CHECK(std::is_sorted(kit.begin(), kit.end()));
    // The host of the pages, the reads of the kit's pages, the privileges of the kit's host methods, its confirmed methods,
    // the Peg page's mainchain view (by name).
    for (const char* m : {"webuihost", "webuievents", "describeconfirmation", "webuichosenfile", "getblockcount", "getmainchaininfo", "getpeginfo", "getrawmempool",
                          "send", "walletcreatefundedpsbt", "bumpfee", "psbtbumpfee", "gettransaction", "getnewaddress", "setlabel", "listunspent", "lockunspent",
                          "estimatesmartfee", "getaddressinfo", "listreceivedbyaddress", "abandontransaction", "walletprocesspsbt", "decodepsbt", "sendrawtransaction",
                          "backupwallet", "restorewallet", "exportwatchonlywallet", "encryptwallet", "unloadwallet", "migratewallet", "enumeratesigners",
                          "walletdisplayaddress", "setbmm", "requestbmmblock", "disconnectnode", "setban", "signmessage", "createwithdrawal", "refundwithdrawal",
                          "peg.call", "peg.connection", "getdrivechaininfo", "listwithdrawalbundles"}) {
        BOOST_CHECK_MESSAGE(listed(*wl, m), m);
    }
    // Never: a key read out, a wallet's passphrase set or changed, an import (the phrase's setup is the daemon's own user's),
    // sends that are not the pages' (sendtoaddress, sendmany), a deposit (the app's own), a chain's own methods for another.
    for (const char* m : {"sendtoaddress", "sendmany", "dumpwallet", "dumpprivkey", "listdescriptors", "walletpassphrase", "walletpassphrasechange", "importdescriptors",
                          "createsidechaindeposit", "sendasset", "swapasset", "createbond", "bitassets.logo", "zside.viewingkey.export"}) {
        BOOST_CHECK_MESSAGE(std::find(kit.begin(), kit.end(), m) == kit.end(), m);
        BOOST_CHECK_MESSAGE(!listed(*wl, m), m);
    }
    // A chain's own (its pack's manifest, the app's pages' use of it): in its daemon's whitelist only, plain names only.
    {
        SidechainSupervisor::Options o{Regtest(base / "own")};
        o.chain_methods = [](const std::string& stem) {
            return stem == "layers" ? std::vector<std::string>{"sendasset", "swapasset", "bitassets.logo", "bad,name", "a:b"} : std::vector<std::string>{};
        };
        SidechainSupervisor own{o, memory.Load(), memory.Store()};
        const auto find_wl{[](const std::vector<std::string>& a) { return *std::find_if(a.begin(), a.end(), [](const std::string& x) { return x.starts_with("-rpcwhitelist=chains-app:"); }); }};
        const std::string layers{find_wl(own.DaemonArgs("layers"))};
        const std::string thunder{find_wl(own.DaemonArgs("thunder"))};
        for (const char* m : {"sendasset", "swapasset", "bitassets.logo"}) {
            BOOST_CHECK_MESSAGE(listed(layers, m), m);
            BOOST_CHECK_MESSAGE(!listed(thunder, m), m);
        }
        BOOST_CHECK(layers.find("bad") == std::string::npos);
        BOOST_CHECK(layers.find("a:b") == std::string::npos);
    }
    // Merged mining's bids: from the wallet the app names (the mining wallet, else the phrase's), none named without one.
    BOOST_CHECK(std::none_of(args.begin(), args.end(), [](const std::string& a) { return a.starts_with("-mainchainrpcwallet"); }));
    {
        SidechainSupervisor::Options o{Regtest(base / "bmm")};
        std::string paying{"mining"};
        o.bmm_wallet = [&](const std::string&) { return paying; };
        SidechainSupervisor bmm{o, memory.Load(), memory.Store()};
        BOOST_CHECK(Has(bmm.DaemonArgs("layers"), "-mainchainrpcwallet=mining"));
        paying = "bad/name";
        const auto refused{bmm.DaemonArgs("layers")};
        BOOST_CHECK(std::none_of(refused.begin(), refused.end(), [](const std::string& a) { return a.starts_with("-mainchainrpcwallet"); }));
        BOOST_CHECK_EQUAL(bmm.BmmWallet("layers"), "");
    }
    // What the daemons may call on Chains: never a deposit, a send, a dump.
    for (const char* m : {"createsidechaindeposit", "sendtoaddress", "dumpwallet", "walletpassphrase", "signrawtransactionwithwallet", "setwithdrawalvote"}) {
        const std::vector<std::string>& allowed{SidechainSupervisor::MainchainRpcMethods()};
        BOOST_CHECK_MESSAGE(std::find(allowed.begin(), allowed.end(), m) == allowed.end(), m);
    }
    // Never the password on a command line.
    for (const auto& a : args) BOOST_CHECK(a.find("0123456789abcdef") == std::string::npos);
    BOOST_CHECK_EQUAL(fs::PathToString(s.CookieFile("layers")), data + "/regtest/.cookie");
    // Thunder on testnet: no slot argument (the network has it).
    SidechainSupervisor::Options o{Regtest(base)};
    o.chain = ChainType::TESTNET;
    SidechainSupervisor t{o, memory.Load(), memory.Store()};
    const auto targs{t.DaemonArgs("thunder")};
    BOOST_CHECK(Has(targs, "-testnet"));
    BOOST_CHECK(std::none_of(targs.begin(), targs.end(), [](const std::string& a) { return a.starts_with("-sidechainslot"); }));
    BOOST_CHECK(s.DaemonArgs("nope").empty());
}

BOOST_AUTO_TEST_CASE(zside_keeps_its_own_wallet)
{
    // zSide's shielded keys are not made from the backup phrase: its wallet is always its own (doc §23.13).
    Memory memory;
    SidechainSupervisor s{Regtest(m_args.GetDataDirBase()), memory.Load(), memory.Store()};
    BOOST_CHECK(!SidechainSupervisor::FindSpec("zside")->phrase_keys);
    for (const char* stem : {"layers", "fastblocks", "thunder", "bitnames", "bitassets", "hivemind"}) BOOST_CHECK(SidechainSupervisor::FindSpec(stem)->phrase_keys);
    BOOST_CHECK_EQUAL(s.WalletMode("zside"), "separate");
    BOOST_CHECK_EQUAL(s.WalletMode("layers"), "shared");
    // Even when a setting says otherwise (an older version, a hand edit).
    UniValue chains{UniValue::VOBJ};
    UniValue z{UniValue::VOBJ};
    z.pushKV("wallet", "shared");
    chains.pushKV("zside", z);
    memory.value.pushKV("chains", chains);
    BOOST_CHECK_EQUAL(s.WalletMode("zside"), "separate");
}

BOOST_AUTO_TEST_CASE(validator_key_made_on_this_computer)
{
    // A chain that stakes gets a validator key made by the app on this computer (doc §25.2, decision 4 of §12): random, never
    // from the phrase, owner-only in its network's data folder; its daemon started with it, and its payout once known.
    Memory memory;
    const fs::path base{m_args.GetDataDirBase()};
    SidechainSupervisor s{Regtest(base), memory.Load(), memory.Store()};
    const auto key_arg{[](const std::vector<std::string>& args) { return std::any_of(args.begin(), args.end(), [](const std::string& a) { return a.starts_with("-validatorkeyfile="); }); }};
    BOOST_CHECK(!s.HasValidatorKey("fastblocks"));
    BOOST_CHECK(!key_arg(s.DaemonArgs("fastblocks")));
    // Not for a chain that does not stake.
    BOOST_CHECK_THROW(s.MakeValidatorKey("thunder"), std::runtime_error);
    BOOST_CHECK_THROW(s.MakeValidatorKey("nope"), std::runtime_error);
    const std::string pubkey{s.MakeValidatorKey("fastblocks")};
    BOOST_CHECK_EQUAL(pubkey.size(), 64U);
    const fs::path file{s.ValidatorKeyFile("fastblocks")};
    BOOST_CHECK(file == base / "chains" / "sidechains" / "fastblocks" / "data" / "regtest" / "validator.key");
    BOOST_CHECK(s.HasValidatorKey("fastblocks"));
    std::string text;
    {
        std::ifstream in{file.std_path()};
        std::getline(in, text);
    }
    BOOST_CHECK_EQUAL(text.size(), 64U);
    BOOST_CHECK(text.find_first_not_of("0123456789abcdef") == std::string::npos);
#ifndef WIN32
    struct stat st;
    BOOST_REQUIRE(stat(fs::PathToString(file).c_str(), &st) == 0);
    BOOST_CHECK_EQUAL(st.st_mode & 0777, 0600U);
#endif
    const auto args{s.DaemonArgs("fastblocks")};
    BOOST_CHECK(Has(args, "-validatorkeyfile=" + fs::PathToString(file)));
    // No payout yet (its wallet was not asked: its node does not run): none passed.
    BOOST_CHECK(!std::any_of(args.begin(), args.end(), [](const std::string& a) { return a.starts_with("-validatorpayout="); }));
    // Made again: the same key, never a new one (its bonds and its protection file go with it).
    BOOST_CHECK_EQUAL(s.MakeValidatorKey("fastblocks"), pubkey);
    {
        std::ifstream in{file.std_path()};
        std::string again;
        std::getline(in, again);
        BOOST_CHECK_EQUAL(again, text);
    }
    // Each machine its own: another data folder, another key.
    Memory other_memory;
    SidechainSupervisor other{Regtest(base / "other"), other_memory.Load(), other_memory.Store()};
    BOOST_CHECK(other.MakeValidatorKey("fastblocks") != pubkey);
    // A payout address set (by its node's wallet): passed; one that is not plain text is not.
    UniValue chains{memory.value.find_value("chains")};
    UniValue fb{chains.find_value("fastblocks")};
    fb.pushKV("validatorPayout", "rsc1qpayout");
    UniValue next{UniValue::VOBJ};
    next.pushKV("fastblocks", fb);
    memory.value = UniValue{UniValue::VOBJ};
    memory.value.pushKV("chains", next);
    BOOST_CHECK(Has(s.DaemonArgs("fastblocks"), "-validatorpayout=rsc1qpayout"));
    memory.value = UniValue{UniValue::VOBJ};
    UniValue bad{UniValue::VOBJ};
    bad.pushKV("validator", true);
    bad.pushKV("validatorPayout", "rsc1q -walletnotify=x");
    UniValue next2{UniValue::VOBJ};
    next2.pushKV("fastblocks", bad);
    memory.value.pushKV("chains", next2);
    for (const auto& a : s.DaemonArgs("fastblocks")) BOOST_CHECK(!a.starts_with("-validatorpayout="));
    // The list says so (Settings, the enable wizard).
    bool listed{false};
    const UniValue list{s.List()};
    for (const UniValue& e : list.getValues()) {
        if (e.find_value("stem").get_str() == "fastblocks") listed = e.find_value("validatorKey").isTrue();
    }
    BOOST_CHECK(listed);
    // The key file removed by hand: no longer passed.
    fs::remove(file);
    BOOST_CHECK(!key_arg(s.DaemonArgs("fastblocks")));
}

BOOST_AUTO_TEST_CASE(rpcauth_line)
{
    // As share/rpcauth/rpcauth.py makes it: HMAC-SHA256 of the password keyed with the salt (hex text).
    BOOST_CHECK_EQUAL(SidechainSupervisor::RpcAuthLine("u", "secret", "00112233445566778899aabbccddeeff"),
                      "u:00112233445566778899aabbccddeeff$74ef20277296f0f1f1a01f8915163d687de88b01b139720c60e410c0540a0937");
}

BOOST_AUTO_TEST_CASE(read_only_methods)
{
    BOOST_CHECK(SidechainSupervisor::IsReadOnlyMethod("getblockchaininfo"));
    BOOST_CHECK(SidechainSupervisor::IsReadOnlyMethod("getbalances"));
    for (const char* m : {"sendtoaddress", "stop", "createwallet", "dumpprivkey", "setbmm", "importdescriptors", "walletpassphrase", "createwithdrawal"}) {
        BOOST_CHECK_MESSAGE(!SidechainSupervisor::IsReadOnlyMethod(m), m);
    }
}

BOOST_AUTO_TEST_CASE(data_dirs)
{
    Memory memory;
    const fs::path base{m_args.GetDataDirBase()};
    SidechainSupervisor s{Regtest(base), memory.Load(), memory.Store()};
    const fs::path def{base / "chains" / "sidechains" / "layers" / "data"};
    BOOST_CHECK(s.DataDir("layers") == def);
    // Refused: a relative path, the data of Chains, the data of another sidechain, a missing parent.
    BOOST_CHECK(!s.CheckDataDir("layers", fs::PathFromString("relative/dir")).empty());
    BOOST_CHECK(!s.CheckDataDir("layers", base / "chains").empty());
    BOOST_CHECK(!s.CheckDataDir("layers", base / "chains" / "regtest" / "x").empty());
    BOOST_CHECK(!s.CheckDataDir("layers", base / "chains" / "sidechains" / "thunder" / "data").empty());
    // A folder not made yet is made when used, in the nearest one that is there.
    BOOST_CHECK_EQUAL(s.CheckDataDir("layers", base / "missing" / "deeper"), "");
    fs::create_directories(base / "disk2");
    BOOST_CHECK_EQUAL(s.CheckDataDir("layers", base / "disk2" / "layers"), "");
    // In the sidechains folder of Chains is fine (the default is there).
    BOOST_CHECK_EQUAL(s.CheckDataDir("layers", base / "chains" / "sidechains" / "layers" / "other"), "");

    // Moved: what was there goes along.
    fs::create_directories(def / "regtest" / "wallets" / "main");
    std::ofstream{(def / "regtest" / "blocks.dat").std_path()} << "x";
    s.SetDataDir("layers", base / "disk2" / "layers", /*move=*/true);
    BOOST_CHECK(s.DataDir("layers") == base / "disk2" / "layers");
    BOOST_CHECK(fs::exists(base / "disk2" / "layers" / "regtest" / "blocks.dat"));
    BOOST_CHECK(!fs::exists(def));
    BOOST_CHECK(memory.value.find_value("chains").find_value("layers").find_value("dataDir").get_str() == fs::PathToString(base / "disk2" / "layers"));
    // Back to the default, without moving: the data stays where it is; nothing is lost.
    s.SetDataDir("layers", std::nullopt, /*move=*/false);
    BOOST_CHECK(s.DataDir("layers") == def);
    BOOST_CHECK(fs::exists(base / "disk2" / "layers" / "regtest" / "blocks.dat"));
    // Into a folder that is not empty: refused.
    s.SetDataDir("layers", base / "disk2" / "layers", /*move=*/false);
    fs::create_directories(def / "regtest");
    std::ofstream{(def / "regtest" / "x").std_path()} << "x";
    BOOST_CHECK_THROW(s.SetDataDir("layers", std::nullopt, /*move=*/true), std::runtime_error);

    // Delete the chain data, the wallets kept.
    s.SetDataDir("layers", std::nullopt, /*move=*/false);
    fs::create_directories(def / "regtest" / "wallets" / "main");
    fs::create_directories(def / "regtest" / "blocks");
    s.DeleteData("layers", /*with_wallets=*/false);
    BOOST_CHECK(fs::exists(def / "regtest" / "wallets" / "main"));
    BOOST_CHECK(!fs::exists(def / "regtest" / "blocks"));
    BOOST_CHECK(!fs::exists(def / "regtest" / "x"));
    s.DeleteData("layers", /*with_wallets=*/true);
    BOOST_CHECK(!fs::exists(def / "regtest" / "wallets"));
}

BOOST_AUTO_TEST_CASE(standalone_runs_nothing)
{
    Memory memory;
    SidechainSupervisor s{Regtest(m_args.GetDataDirBase()), memory.Load(), memory.Store()};
    BOOST_CHECK(!s.AnyEnabled());
    s.StartEnabled();
    BOOST_CHECK(!s.ThreadRunning());
    BOOST_CHECK_EQUAL(SidechainSupervisor::StateName(s.GetStatus("layers").state), "disabled");
    // A program that is not there: the enable is refused and stays off.
    BOOST_CHECK_THROW(s.Enable("layers"), std::runtime_error);
    BOOST_CHECK(!s.Enabled("layers"));
    BOOST_CHECK(!s.ThreadRunning());
    const UniValue list{s.List()};
    BOOST_CHECK_EQUAL(list.size(), SidechainSupervisor::Catalog().size());
    BOOST_CHECK_EQUAL(list[0].find_value("stem").get_str(), "layers");
    BOOST_CHECK_EQUAL(list[0].find_value("program").get_str(), "");
}

BOOST_AUTO_TEST_CASE(wait_gives_up_when_stopping)
{
    // A waiter (the end of an update, doc/unified-app.md §15.3) does not outlive the app's stop by minutes.
    Memory memory;
    SidechainSupervisor s{Regtest(m_args.GetDataDirBase()), memory.Load(), memory.Store()};
    BOOST_CHECK(!s.Stopping());
    std::atomic<bool> waited{false};
    std::atomic<bool> reached{true};
    std::thread waiter{[&] {
        reached = s.WaitFor("layers", [](const auto& st) { return st.rpc_ready; }, 10min);
        waited = true;
    }};
    std::this_thread::sleep_for(200ms);
    BOOST_CHECK(!waited.load());
    const auto start{std::chrono::steady_clock::now()};
    s.StopAll();
    waiter.join();
    BOOST_CHECK(s.Stopping());
    BOOST_CHECK(!reached.load());
    BOOST_CHECK(std::chrono::steady_clock::now() - start < 5s);
    // A wait that starts after the stop returns at once.
    BOOST_CHECK(!s.WaitFor("layers", [](const auto&) { return false; }, 10min));
}

#ifndef WIN32
namespace {
std::optional<fs::path> TestSidechainDir()
{
    const char* dir{std::getenv("CHAINS_TEST_SIDECHAIN_BINDIR")};
    if (!dir || !fs::exists(fs::PathFromString(dir) / "layersd")) return std::nullopt;
    return fs::PathFromString(dir);
}

std::optional<fs::path> TestChainsd()
{
    if (const char* p{std::getenv("CHAINS_TEST_CHAINSD")}) return fs::PathFromString(p);
    std::error_code ec;
    const auto self{std::filesystem::read_symlink("/proc/self/exe", ec)};
    if (ec) return std::nullopt;
    const fs::path chainsd{fs::path{self.parent_path()} / "chainsd"};
    return fs::exists(chainsd) ? std::optional{chainsd} : std::nullopt;
}

/** chainsd on regtest, the mainchain the sidechain follows (stopped, by its pid, at the end). */
struct Mainchain {
    pid_t pid{0};
    Mainchain(const fs::path& chainsd, const fs::path& datadir, const std::string& rpcauth)
    {
        fs::create_directories(datadir);
        std::vector<std::string> argv{fs::PathToString(chainsd), "-regtest", "-datadir=" + fs::PathToString(datadir), "-rpcport=49311", "-port=49312",
                                      "-listen=0", "-server=1", "-printtoconsole=0", "-rpcauth=" + rpcauth};
        std::vector<char*> args;
        for (auto& a : argv) args.push_back(a.data());
        args.push_back(nullptr);
        // Ends with the test, even one that crashes; its output is not the test's.
        const pid_t parent{getpid()};
        pid = fork();
        if (pid == 0) {
            prctl(PR_SET_PDEATHSIG, SIGTERM);
            if (getppid() != parent) _exit(1);
            const int null{open("/dev/null", O_RDWR)};
            dup2(null, 0);
            dup2(null, 1);
            dup2(null, 2);
            execv(args[0], args.data());
            _exit(127);
        }
    }
    ~Mainchain()
    {
        if (pid <= 0) return;
        kill(pid, SIGTERM);
        int status;
        waitpid(pid, &status, 0);
    }
};

bool Gone(int pid) { return pid > 0 && kill(pid, 0) != 0 && errno == ESRCH; }
} // namespace

BOOST_AUTO_TEST_CASE(real_daemon_start_stop_restart)
{
    const auto bindir{TestSidechainDir()};
    const auto chainsd{TestChainsd()};
    if (!bindir || !chainsd) {
        BOOST_TEST_MESSAGE("skipped: set CHAINS_TEST_SIDECHAIN_BINDIR to a folder with layersd (and have chainsd next to the test)");
        return;
    }
    const fs::path base{m_args.GetDataDirBase()};
    SidechainSupervisor::Options o{Regtest(base)};
    o.bin_dirs = {*bindir};
    Mainchain main{*chainsd, base / "chains", SidechainSupervisor::RpcAuthLine(o.mainchain_rpc_user, o.mainchain_rpc_password, "a1b2c3d4e5f60718293a4b5c6d7e8f90")};
    BOOST_REQUIRE(main.pid > 0);
    Memory memory;
    std::vector<std::string> notified;
    Mutex notified_mutex;
    {
        SidechainSupervisor s{o, memory.Load(), memory.Store(), [&](const std::string& stem) { LOCK(notified_mutex); notified.push_back(stem); }};
        s.Enable("layers", "separate");
        BOOST_CHECK(s.ThreadRunning());
        BOOST_CHECK(s.Enabled("layers"));
        BOOST_CHECK_EQUAL(s.WalletMode("layers"), "separate");
        BOOST_REQUIRE(s.WaitFor("layers", [](const auto& st) { return st.rpc_ready; }, 90s));
        const int pid1{s.GetStatus("layers").pid};
        BOOST_CHECK(pid1 > 0);
        // The password file: the owner's only.
        struct stat st;
        BOOST_REQUIRE(stat(fs::PathToString(s.PasswordFile("layers")).c_str(), &st) == 0);
        BOOST_CHECK_EQUAL(st.st_mode & 0777, 0600);
        // Wired to the mainchain: it reaches chainsd with the user of the app.
        bool connected{false};
        for (int i{0}; i < 120 && !connected; ++i) {
            try {
                connected = s.Rpc("layers", "getmainchaininfo").find_value("connected").isTrue();
            } catch (const std::exception&) {
            }
            if (!connected) std::this_thread::sleep_for(500ms);
        }
        BOOST_CHECK(connected);
        // The overview the page shows: a height, in step with Chains (at 0), the wallet the app had it make.
        BOOST_CHECK(s.WaitFor("layers", [&](const auto&) {
            try {
                return s.Rpc("layers", "listwallets").size() == 1;
            } catch (const std::exception&) {
                return false;
            }
        }, 60s));
        const UniValue overview{s.Overview("layers", 0)};
        BOOST_CHECK_EQUAL(overview.find_value("height").getInt<int>(), 0);
        BOOST_CHECK(overview.find_value("synced").isTrue());
        BOOST_CHECK_EQUAL(overview.find_value("wallet").find_value("name").get_str(), "main");
        BOOST_CHECK_EQUAL(overview.find_value("blocks").size(), 1U);
        {
            LOCK(notified_mutex);
            BOOST_CHECK(std::count(notified.begin(), notified.end(), "layers") > 0);
        }
        // The app's user at the daemon: the host of the pages answers; a spend straight away is refused by the daemon
        // itself (its whitelist), whatever the app asks.
        {
            UniValue request{UniValue::VOBJ};
            request.pushKV("kind", "host");
            request.pushKV("method", "app.info");
            UniValue args{UniValue::VARR};
            args.push_back(request);
            BOOST_CHECK_EQUAL(s.Rpc("layers", "webuihost", args).find_value("result").find_value("hostApi").getInt<int>(), 1);
            UniValue send{UniValue::VARR};
            send.push_back("rladdr");
            send.push_back(1);
            BOOST_CHECK_EXCEPTION(s.Rpc("layers", "sendtoaddress", send, "main"), SidechainSupervisor::RpcError,
                                  [](const SidechainSupervisor::RpcError& e) { return e.code == -32601 && std::string{e.what()}.find("not one of the methods") != std::string::npos; });
            BOOST_CHECK_EXCEPTION(s.Rpc("layers", "dumpwallet", UniValue{UniValue::VARR}, "main"), SidechainSupervisor::RpcError, [](const auto& e) { return e.code == -32601; });
        }

        // It falls over: restarted (after the first wait), as another process.
        kill(pid1, SIGKILL);
        BOOST_REQUIRE(s.WaitFor("layers", [&](const auto& st) { return st.rpc_ready && st.pid != pid1 && st.pid > 0; }, 120s));
        BOOST_CHECK_EQUAL(s.GetStatus("layers").restarts, 1);
        BOOST_CHECK(Gone(pid1));
        const int pid2{s.GetStatus("layers").pid};

        // Disabled: it stops cleanly (SIGTERM, its own shutdown), its data stays.
        s.Disable("layers");
        BOOST_REQUIRE(s.WaitFor("layers", [](const auto& st) { return st.pid == 0 && st.state == SidechainSupervisor::State::DISABLED; }, 90s));
        BOOST_CHECK(Gone(pid2));
        BOOST_CHECK(!s.Enabled("layers"));
        BOOST_CHECK(fs::exists(s.NetDataDir("layers") / "wallets" / "main"));
        {
            std::ifstream log{(s.NetDataDir("layers") / "debug.log").std_path()};
            const std::string text((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
            BOOST_CHECK(text.find("Shutdown done") != std::string::npos);
        }

        // Its data moved to a folder of the user's; started from there.
        fs::create_directories(base / "disk2");
        s.SetDataDir("layers", base / "disk2" / "layers", /*move=*/true);
        s.Enable("layers");
        BOOST_REQUIRE(s.WaitFor("layers", [](const auto& st) { return st.rpc_ready; }, 90s));
        BOOST_CHECK(fs::exists(base / "disk2" / "layers" / "regtest" / "wallets" / "main"));
        BOOST_CHECK(!fs::exists(base / "chains" / "sidechains" / "layers" / "data" / "regtest"));
        const int pid3{s.GetStatus("layers").pid};
        // The app stops: every daemon stops with it, and stays enabled for the next start.
        s.StopAll();
        BOOST_CHECK(Gone(pid3));
        BOOST_CHECK(!s.ThreadRunning());
        BOOST_CHECK(s.Enabled("layers"));
    }
}
BOOST_AUTO_TEST_CASE(real_daemon_adopts_existing_data_and_sees_its_own_app)
{
    // The folder of a sidechain that ran in its own app before (~/.chains/sidechains/<stem>/data of a user): its wallet
    // is the one used, none is made; while its own app runs (the lock of its data held), it is not started, and the
    // user is told why.
    const auto bindir{TestSidechainDir()};
    const auto chainsd{TestChainsd()};
    if (!bindir || !chainsd) {
        BOOST_TEST_MESSAGE("skipped: set CHAINS_TEST_SIDECHAIN_BINDIR to a folder with layersd (and have chainsd next to the test)");
        return;
    }
    const fs::path base{m_args.GetDataDirBase()};
    SidechainSupervisor::Options o{Regtest(base)};
    o.bin_dirs = {*bindir};
    Mainchain main{*chainsd, base / "chains", SidechainSupervisor::RpcAuthLine(o.mainchain_rpc_user, o.mainchain_rpc_password, "a1b2c3d4e5f60718293a4b5c6d7e8f90")};
    BOOST_REQUIRE(main.pid > 0);
    Memory memory;
    SidechainSupervisor s{o, memory.Load(), memory.Store()};
    const auto wallets{[&] {
        try {
            return s.Rpc("layers", "listwallets");
        } catch (const std::exception&) {
            return UniValue{UniValue::VARR};
        }
    }};
    // A folder as an app of its own leaves it: a wallet "mine", none called "main", none loaded at the start.
    s.Enable("layers");
    BOOST_REQUIRE(s.WaitFor("layers", [&](const auto& st) { return st.rpc_ready && wallets().size() == 1; }, 120s));
    UniValue mine{UniValue::VARR};
    mine.push_back("mine");
    s.Rpc("layers", "createwallet", mine);
    s.Disable("layers");
    BOOST_REQUIRE(s.WaitFor("layers", [](const auto& st) { return st.pid == 0; }, 90s));
    fs::remove_all(s.NetDataDir("layers") / "wallets" / "main");
    BOOST_REQUIRE(fs::exists(s.NetDataDir("layers") / "wallets" / "mine"));

    // Its own app runs (another process holds the lock of its data): not started, said plainly.
    int ready[2];
    BOOST_REQUIRE(pipe(ready) == 0);
    const pid_t holder{fork()};
    BOOST_REQUIRE(holder >= 0);
    if (holder == 0) {
        close(ready[0]);
        const bool locked{util::LockDirectory(s.NetDataDir("layers"), ".lock") == util::LockResult::Success};
        const char c{locked ? '1' : '0'};
        if (write(ready[1], &c, 1) != 1) _exit(1);
        pause();
        _exit(0);
    }
    close(ready[1]);
    char c{0};
    BOOST_REQUIRE(read(ready[0], &c, 1) == 1);
    close(ready[0]);
    BOOST_REQUIRE_EQUAL(c, '1');
    BOOST_CHECK_EXCEPTION(s.Enable("layers"), std::runtime_error, [](const std::runtime_error& e) { return std::string{e.what()}.find("already running in its own app") != std::string::npos; });
    BOOST_CHECK(!s.Enabled("layers"));
    BOOST_CHECK_EQUAL(s.GetStatus("layers").pid, 0);
    kill(holder, SIGTERM);
    int status;
    waitpid(holder, &status, 0);

    // Quit: adopted as it is, its wallet "mine" loaded, no "main" made.
    s.Enable("layers");
    BOOST_REQUIRE(s.WaitFor("layers", [&](const auto& st) { return st.rpc_ready && wallets().size() == 1; }, 120s));
    BOOST_CHECK_EQUAL(wallets()[0].get_str(), "mine");
    BOOST_CHECK(!fs::exists(s.NetDataDir("layers") / "wallets" / "main"));
    s.StopAll();
}
#endif

BOOST_AUTO_TEST_SUITE_END()
