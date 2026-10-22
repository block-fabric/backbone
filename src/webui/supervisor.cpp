// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <webui/supervisor.h>

#include <webui/chainadapter.h>

#include <common/secretfile.h>
#include <crypto/hmac_sha256.h>
#include <logging.h>
#include <random.h>
#include <support/cleanse.h>
#include <tinyformat.h>
#include <util/fs_helpers.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <util/threadnames.h>
#include <util/time.h>
#include <span.h>

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <array>
#include <fstream>
#include <stdexcept>

#ifndef WIN32
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#endif

namespace webui {
namespace {

//! Every string of a value wiped (a request that carried keys or a passphrase, once written out).
void WipeStrings(UniValue& value)
{
    if (value.isStr()) {
        std::string& text{const_cast<std::string&>(value.get_str())};
        memory_cleanse(text.data(), text.size());
        return;
    }
    if (value.isArray() || value.isObject()) {
        for (size_t i{0}; i < value.size(); ++i) WipeStrings(const_cast<UniValue&>(value[i]));
    }
}
using namespace std::chrono_literals;

std::runtime_error Error(const std::string& message) { return std::runtime_error{message}; }

bool Exists(const fs::path& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool IsEmptyDir(const fs::path& path)
{
    std::error_code ec;
    return !std::filesystem::exists(path, ec) || std::filesystem::is_empty(path, ec);
}

/** The folder a daemon keeps the data of a network in, under its -datadir ("" for main). */
std::string NetDir(ChainType chain)
{
    switch (chain) {
    case ChainType::MAIN: return "";
    case ChainType::TESTNET: return "testnet";
    case ChainType::SIGNET: return "signet";
    case ChainType::REGTEST: return "regtest";
    }
    return "";
}

/** Whether `inner` is `outer` or in it. */
bool Within(const fs::path& inner, const fs::path& outer)
{
    const auto a{inner.lexically_normal()};
    const auto b{outer.lexically_normal()};
    auto i{a.begin()};
    for (auto j{b.begin()}; j != b.end(); ++j, ++i) {
        if (j->empty()) continue; // a trailing separator
        if (i == a.end() || *i != *j) return false;
    }
    return true;
}

#ifndef WIN32
bool PortFree(int port)
{
    const int fd{socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    if (fd < 0) return true;
    int one{1};
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool free{bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0};
    close(fd);
    return free;
}

/** POST a JSON-RPC request to 127.0.0.1:port (HTTP/1.1, the connection closed after). {status, body}; status 0: no answer. */
std::pair<int, std::string> HttpPost(int port, const std::string& path, const std::string& auth, const std::string& body, std::chrono::milliseconds timeout)
{
    const int fd{socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    if (fd < 0) return {0, "no socket"};
    timeval tv{};
    tv.tv_sec = timeout.count() / 1000;
    tv.tv_usec = (timeout.count() % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return {0, "the node does not answer (not started yet?)"};
    }
    std::string request{strprintf("POST %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nAuthorization: Basic %s\r\n"
                                  "Content-Type: application/json\r\nContent-Length: %u\r\n\r\n%s",
                                  path, EncodeBase64(auth), body.size(), body)};
    size_t sent{0};
    while (sent < request.size()) {
        const ssize_t n{send(fd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL)};
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            close(fd);
            return {0, "the request could not be sent"};
        }
        sent += n;
    }
    memory_cleanse(request.data(), request.size());
    std::string reply;
    char buffer[16384];
    while (true) {
        const ssize_t n{recv(fd, buffer, sizeof(buffer), 0)};
        if (n > 0) {
            reply.append(buffer, n);
            if (reply.size() > 32 * 1024 * 1024) break;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
    close(fd);
    const size_t head_end{reply.find("\r\n\r\n")};
    if (head_end == std::string::npos || !reply.starts_with("HTTP/1.")) return {0, "no answer"};
    const size_t sp{reply.find(' ')};
    const int status{sp == std::string::npos ? 0 : std::atoi(reply.c_str() + sp + 1)};
    std::string payload{reply.substr(head_end + 4)};
    // Only Content-Length bodies come from the HTTP server of a node; a chunked one is not expected.
    return {status, payload};
}
#endif

std::string DescribeExit(int status)
{
#ifndef WIN32
    if (WIFEXITED(status)) return strprintf("it stopped by itself (exit code %d)", WEXITSTATUS(status));
    if (WIFSIGNALED(status)) return strprintf("it was ended by signal %d", WTERMSIG(status));
#endif
    return "it stopped";
}

/** The last lines of a file (an error a daemon wrote before it stopped). */
std::string Tail(const fs::path& path, size_t bytes = 600)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return {};
    std::ifstream file{path.std_path(), std::ios::binary};
    if (!file) return {};
    file.seekg(0, std::ios::end);
    const std::streamoff size{file.tellg()};
    file.seekg(std::max<std::streamoff>(0, size - static_cast<std::streamoff>(bytes)));
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return std::string{util::TrimStringView(text)};
}

UniValue Without(const UniValue& object, const std::string& key)
{
    UniValue out{UniValue::VOBJ};
    if (!object.isObject()) return out;
    for (size_t i{0}; i < object.size(); ++i) {
        if (object.getKeys()[i] != key) out.pushKV(object.getKeys()[i], object.getValues()[i]);
    }
    return out;
}
} // namespace

const std::vector<SidechainSupervisor::Spec>& SidechainSupervisor::Catalog()
{
    // The order is that of the list in Settings → Sidechains (and of the ports with Options::port_base).
    static const std::vector<Spec> catalog{
        {"layers", "Layers", "Make and trade assets on a fast chain: a block every 15 seconds.", 7, 2, 4, 400, true},
        {"fastblocks", "FastBlocks", "Quick, cheap payments: a block every 15 seconds, kept honest by stake.", 5, 1, 3, 400, true},
        {"thunder", "Thunder", "Very large blocks, for many small payments at a low fee.", 2, 7, 10, 600, false},
        {"bitnames", "BitNames", "Names you own, like a web address for your keys and your data.", 3, 9, 2, 300, false},
        {"bitassets", "BitAssets", "Issue your own tokens and trade them.", 4, 3, 2, 300, false},
        {"zside", "zSide", "Private payments: amounts and addresses hidden.", 6, 4, 3, 500, false, /*phrase_keys=*/false},
        {"hivemind", "Hivemind", "Prediction markets: trade on what will happen.", 9, 8, 2, 400, false},
    };
    return catalog;
}

const SidechainSupervisor::Spec* SidechainSupervisor::FindSpec(std::string_view stem)
{
    for (const Spec& s : Catalog()) {
        if (s.stem == stem) return &s;
    }
    return nullptr;
}

std::string SidechainSupervisor::StateName(State state)
{
    switch (state) {
    case State::DISABLED: return "disabled";
    case State::STOPPED: return "stopped";
    case State::STARTING: return "starting";
    case State::RUNNING: return "running";
    case State::STOPPING: return "stopping";
    case State::CRASHED: return "crashed";
    case State::FAILED: return "failed";
    }
    return "unknown";
}

std::string SidechainSupervisor::RpcAuthLine(const std::string& user, const std::string& password, const std::string& salt_hex)
{
    std::array<unsigned char, CHMAC_SHA256::OUTPUT_SIZE> out;
    CHMAC_SHA256(UCharCast(salt_hex.data()), salt_hex.size()).Write(UCharCast(password.data()), password.size()).Finalize(out.data());
    return strprintf("%s:%s$%s", user, salt_hex, HexStr(out));
}

bool SidechainSupervisor::IsReadOnlyMethod(std::string_view method)
{
    static const std::vector<std::string_view> methods{
        "getblockchaininfo", "getblockcount", "getbestblockhash", "getblockhash", "getblockheader", "getblock",
        "getmainchaininfo", "getbmminfo", "getnetworkinfo", "getconnectioncount", "getmempoolinfo", "getpeerinfo",
        "listwallets", "getwalletinfo", "getbalances", "getbalance", "listtransactions", "uptime",
    };
    return std::find(methods.begin(), methods.end(), method) != methods.end();
}

const std::vector<std::string>& SidechainSupervisor::AppRpcMethods()
{
    // Every sidechain (the kit at hostApi 1, sidechain-template main 6fc20b4c9f): least privilege, what is used and no more.
    static const std::vector<std::string> methods{[] {
        std::vector<std::string> m{
            // The supervisor (is it up, its wallets) and the app's own reads of it (Overview, the adapter's summaries).
            "getblockcount", "listwallets", "listwalletdir", "loadwallet", "createwallet", "stop", "getconnectioncount", "getbalance", "listtransactions",
            // The host of the pages over RPC (remotehost.h of the sidechain).
            "webuihost", "webuievents", "describeconfirmation", "webuichosenfile",
            // The pages' reads: the kit's policy for a sidechain (its rpcpolicy.cpp), each checked by name by the daemon.
            "getbestblockhash", "getblock", "getblockchaininfo", "getblockhash", "getblockstats", "getblockheader", "getmempoolentry", "getmempoolinfo",
            "getmininginfo", "getnetworkhashps", "getnetworkinfo", "getpeerinfo", "getrawmempool", "getrawtransaction", "gettxout", "gettxspendingprevout",
            "help", "listbanned", "uptime", "getbmminfo", "getdepositaddress", "getmainchaininfo", "getpeginfo", "getwithdrawalbundle", "listwithdrawals",
            "syncmainchain", "createpsbt", "decodepsbt", "deriveaddresses", "finalizepsbt", "getdescriptorinfo", "scantxoutset", "utxoupdatepsbt",
            "validateaddress", "verifymessage", "getaddressinfo", "getbalances", "getnewaddress", "getwalletinfo", "listunspent", "walletlock",
            // What the kit's host methods exercise (remotehost.cpp HostPrivileges): payments and fee bumps (each asked in the
            // app's trusted dialog first), PSBTs, the address book, coins, wallets, merged mining, peers.
            "send", "walletcreatefundedpsbt", "bumpfee", "psbtbumpfee", "gettransaction", "setlabel", "lockunspent", "estimatesmartfee",
            "listreceivedbyaddress", "abandontransaction", "walletprocesspsbt", "sendrawtransaction", "backupwallet", "restorewallet",
            "exportwatchonlywallet", "encryptwallet", "unloadwallet", "migratewallet", "enumeratesigners", "walletdisplayaddress", "setbmm",
            "requestbmmblock", "disconnectnode", "setban",
            // The kit's confirmed methods (confirm.cpp: rpc.confirmed asks in the trusted dialog first).
            "signmessage", "createwithdrawal", "refundwithdrawal",
            // Named, having no privilege of their own: the Peg page's view of the mainchain connection and its reads there,
            // each forwarded method checked by name too (SidechainApi::MainchainMethods).
            "peg.call", "peg.connection", "getdrivechaininfo", "getsidechain", "getsidechainevents", "listactivesidechains", "listsidechaindeposits",
            "listwithdrawalbundles",
            // Not: walletpassphrase, walletpassphrasechange, importdescriptors (the phrase's wallet setup goes as the daemon's
            // own user, SetupRpc), any key read out (dumpprivkey, listdescriptors), createsidechaindeposit (a deposit is this
            // app's own payment, peg.deposit never reaches the daemon: SidechainPages::RouteOf).
        };
        std::ranges::sort(m);
        m.erase(std::unique(m.begin(), m.end()), m.end());
        return m;
    }()};
    return methods;
}

std::vector<std::string> SidechainSupervisor::AppRpcMethodsFor(const std::string& stem) const
{
    std::vector<std::string> m{AppRpcMethods()};
    if (m_options.chain_methods) {
        for (const std::string& name : m_options.chain_methods(stem)) {
            // A name of a method only (a whitelist is "user:a,b,c").
            if (name.empty() || name.size() > 64 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._") != std::string::npos) continue;
            m.push_back(name);
        }
    }
    std::ranges::sort(m);
    m.erase(std::unique(m.begin(), m.end()), m.end());
    return m;
}

std::string SidechainSupervisor::BmmWallet(const std::string& stem) const
{
    std::string wallet{m_options.bmm_wallet ? m_options.bmm_wallet(stem) : std::string{}};
    // A name for the path of the RPC request: the plain ones only.
    if (wallet.size() > 64 || wallet.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") != std::string::npos) return {};
    return wallet;
}

const std::vector<std::string>& SidechainSupervisor::MainchainRpcMethods()
{
    // What a sidechain daemon calls on its mainchain (src/sidechain of the template: the follower, BMM, bundles, the
    // check of a withdrawal address) and what its pages read there (SidechainApi::MainchainMethods). Not
    // createsidechaindeposit: a deposit from a sidechain's pages is made by this app, confirmed in its own dialog.
    static const std::vector<std::string> methods{
        "getblockchaininfo", "getblockcount", "getbestblockhash", "getblockhash", "getblockheader", "getsidechain", "getsidechainevents",
        "listsidechaindeposits", "listactivesidechains", "listwithdrawalbundles", "getdrivechaininfo", "receivewithdrawalbundle",
        "sendwithdrawalbundle", "vouchwithdrawalbundle", "createbmmrequest", "validateaddress", "getbalances", "getnewaddress", "getwalletinfo",
    };
    return methods;
}

SidechainSupervisor::SidechainSupervisor(Options options, LoadFn load, StoreFn store, NotifyFn notify)
    : m_options(std::move(options)), m_load(std::move(load)), m_store(std::move(store)), m_notify(std::move(notify))
{
    std::array<unsigned char, 32> secret;
    GetStrongRandBytes(secret);
    std::array<unsigned char, 16> salt;
    GetStrongRandBytes(salt);
    m_app_password = HexStr(secret);
    memory_cleanse(secret.data(), secret.size());
    m_app_rpcauth = RpcAuthLine(std::string{APP_RPC_USER}, m_app_password, HexStr(salt));
}

SidechainSupervisor::~SidechainSupervisor()
{
    StopAll();
}

UniValue SidechainSupervisor::Settings(const std::string& stem) const
{
    LOCK(m_settings_mutex);
    const UniValue all{m_load()};
    if (!all.isObject()) return UniValue{UniValue::VOBJ};
    const UniValue& chains{all.find_value("chains")};
    if (!chains.isObject()) return UniValue{UniValue::VOBJ};
    const UniValue& mine{chains.find_value(stem)};
    return mine.isObject() ? mine : UniValue{UniValue::VOBJ};
}

UniValue SidechainSupervisor::Setting(const std::string& stem, const std::string& key) const
{
    // A copy: the settings are read anew each time.
    return Settings(stem).find_value(key);
}

void SidechainSupervisor::SetSetting(const std::string& stem, const std::string& key, const UniValue& value)
{
    {
        LOCK(m_settings_mutex);
        UniValue all{m_load()};
        if (!all.isObject()) all = UniValue{UniValue::VOBJ};
        const UniValue& chains_old{all.find_value("chains")};
        UniValue mine{Without(chains_old.isObject() ? chains_old.find_value(stem) : UniValue{}, key)};
        if (!value.isNull()) mine.pushKV(key, value);
        UniValue chains{Without(chains_old, stem)};
        chains.pushKV(stem, std::move(mine));
        UniValue next{Without(all, "chains")};
        next.pushKV("chains", std::move(chains));
        m_store(next);
    }
    Changed(stem);
}

fs::path SidechainSupervisor::HomeDir(const std::string& stem) const
{
    return m_options.chains_datadir / "sidechains" / fs::PathFromString(stem);
}

fs::path SidechainSupervisor::DefaultDataDir(const std::string& stem) const
{
    // As the Nodes tab of the Sidechains page has it: one place for a sidechain, whichever way it was started.
    return HomeDir(stem) / "data";
}

fs::path SidechainSupervisor::DataDir(const std::string& stem) const
{
    const UniValue dir{Setting(stem, "dataDir")};
    if (dir.isStr() && !dir.get_str().empty()) return fs::PathFromString(dir.get_str());
    return DefaultDataDir(stem);
}

fs::path SidechainSupervisor::NetDataDir(const std::string& stem) const
{
    const std::string net{NetDir(m_options.chain)};
    return net.empty() ? DataDir(stem) : DataDir(stem) / fs::PathFromString(net);
}

namespace {
std::string DaemonName(const std::string& stem)
{
#ifdef WIN32
    return stem + "d.exe";
#else
    return stem + "d";
#endif
}
} // namespace

std::optional<fs::path> SidechainSupervisor::FindBinary(const std::string& stem) const
{
    const std::string name{DaemonName(stem)};
    // In order: the folder the user set, the version downloaded and made current, the programs bundled next to the
    // app (the offline path), where a download of phase 1 went.
    std::vector<fs::path> dirs;
    if (const UniValue dir{Setting(stem, "binDir")}; dir.isStr() && !dir.get_str().empty()) dirs.push_back(fs::PathFromString(dir.get_str()));
    if (m_options.downloaded) {
        if (const auto d{m_options.downloaded(stem)}) dirs.push_back(*d);
    }
    for (const auto& d : m_options.bin_dirs) dirs.push_back(d);
    dirs.push_back(HomeDir(stem) / "bin");
    for (const auto& d : dirs) {
        const fs::path candidate{d / fs::PathFromString(name)};
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec)) return candidate;
    }
    return std::nullopt;
}

std::string SidechainSupervisor::BinarySource(const std::string& stem) const
{
    const std::string name{DaemonName(stem)};
    const auto has{[&](const fs::path& d) {
        std::error_code ec;
        return std::filesystem::is_regular_file(d / fs::PathFromString(name), ec);
    }};
    if (const UniValue dir{Setting(stem, "binDir")}; dir.isStr() && !dir.get_str().empty() && has(fs::PathFromString(dir.get_str()))) return "folder";
    if (m_options.downloaded) {
        if (const auto d{m_options.downloaded(stem)}; d && has(*d)) return "download";
    }
    for (const auto& d : m_options.bin_dirs) {
        if (has(d)) return "bundled";
    }
    if (has(HomeDir(stem) / "bin")) return "folder";
    return "none";
}

int SidechainSupervisor::RpcPort(const std::string& stem) const
{
    if (const UniValue port{Setting(stem, "rpcPort")}; port.isNum()) return port.getInt<int>();
    const auto& catalog{Catalog()};
    for (size_t i{0}; i < catalog.size(); ++i) {
        if (catalog[i].stem != stem) continue;
        if (m_options.port_base > 0) return m_options.port_base + 2 * static_cast<int>(i);
        int net{0};
        switch (m_options.chain) {
        case ChainType::MAIN: net = 0; break;
        case ChainType::TESTNET: net = 10000; break;
        case ChainType::REGTEST: net = 20000; break;
        case ChainType::SIGNET: net = 30000; break;
        }
        return net + 9054 + 100 * catalog[i].port_code;
    }
    return 0;
}

int SidechainSupervisor::P2pPort(const std::string& stem) const
{
    if (const UniValue port{Setting(stem, "port")}; port.isNum()) return port.getInt<int>();
    return RpcPort(stem) + 1;
}

fs::path SidechainSupervisor::PasswordFile(const std::string& stem) const
{
    return DataDir(stem) / "mainchain.rpcpass";
}

fs::path SidechainSupervisor::CookieFile(const std::string& stem) const
{
    return NetDataDir(stem) / ".cookie";
}

fs::path SidechainSupervisor::LogFile(const std::string& stem) const
{
    return HomeDir(stem) / "supervisor.log";
}

std::vector<std::string> SidechainSupervisor::DaemonArgs(const std::string& stem) const
{
    const Spec* spec{FindSpec(stem)};
    if (!spec) return {};
    std::vector<std::string> args{"-datadir=" + fs::PathToString(DataDir(stem))};
    switch (m_options.chain) {
    case ChainType::MAIN: break;
    case ChainType::TESTNET: args.emplace_back("-testnet"); break;
    case ChainType::SIGNET: args.emplace_back("-signet"); break;
    case ChainType::REGTEST:
        args.emplace_back("-regtest");
        args.push_back(strprintf("-sidechainslot=%d", spec->slot));
        break;
    }
    // Its own RPC server, on 127.0.0.1 only (the default binding). This app is a user of its own there, which may call
    // only AppRpcMethods (no spend but through webuihost, which asks); the cookie stays the user's, unrestricted
    // (-rpcwhitelistdefault=0: a whitelist for one user does not empty the others').
    args.emplace_back("-server=1");
    args.push_back("-rpcauth=" + m_app_rpcauth);
    const std::vector<std::string> allowed{AppRpcMethodsFor(stem)};
    std::string whitelist{strprintf("-rpcwhitelist=%s:", APP_RPC_USER)};
    for (size_t i{0}; i < allowed.size(); ++i) whitelist += (i ? "," : "") + allowed[i];
    args.push_back(std::move(whitelist));
    args.emplace_back("-rpcwhitelistdefault=0");
    // Files the user chose are given (webuichosenfile) only to this app's user: its own chooser.
    args.push_back(strprintf("-webuichooser=%s", APP_RPC_USER));
    args.push_back(strprintf("-rpcport=%d", RpcPort(stem)));
    args.push_back(strprintf("-port=%d", P2pPort(stem)));
    // Its log is its debug.log; what it writes to the console (an error before the log opens) goes to LogFile.
    args.emplace_back("-printtoconsole=0");
    // Wired to the node of this app: its own user (an -rpcauth of this run of the app), the password in a file.
    args.emplace_back("-mainchainrpcconnect=127.0.0.1");
    args.push_back(strprintf("-mainchainrpcport=%d", m_options.mainchain_rpc_port));
    args.push_back("-mainchainrpcuser=" + m_options.mainchain_rpc_user);
    args.push_back("-mainchainrpcpasswordfile=" + fs::PathToString(PasswordFile(stem)));
    // The wallet of this app's node that pays its merged mining bids (the mining wallet, else the phrase's): named, so that
    // a second wallet of the node (one of before, a mining one) does not leave it without one.
    if (const std::string bmm{BmmWallet(stem)}; !bmm.empty()) args.push_back("-mainchainrpcwallet=" + bmm);
    // A background node: modest caches.
    args.emplace_back("-dbcache=100");
    args.emplace_back("-maxmempool=100");
    for (const auto& a : m_options.extra_args) args.push_back(a);
    return args;
}

bool SidechainSupervisor::Enabled(const std::string& stem) const
{
    const UniValue on{Setting(stem, "enabled")};
    return on.isBool() && on.get_bool();
}

bool SidechainSupervisor::AnyEnabled() const
{
    for (const Spec& s : Catalog()) {
        if (Enabled(s.stem)) return true;
    }
    return false;
}

std::string SidechainSupervisor::WalletMode(const std::string& stem) const
{
    // zSide: always its own wallet (its shielded keys are not made from the phrase).
    if (const Spec* spec{FindSpec(stem)}; spec && !spec->phrase_keys) return "separate";
    const UniValue mode{Setting(stem, "wallet")};
    return mode.isStr() && mode.get_str() == "separate" ? "separate" : "shared";
}

void SidechainSupervisor::Changed(const std::string& stem)
{
    m_cv.notify_all();
    if (m_notify) m_notify(stem);
}

void SidechainSupervisor::EnsureThread()
{
    if (m_thread.joinable() || m_quit) return;
    m_thread = std::thread([this] { Loop(); });
}

std::string SidechainSupervisor::RunningElsewhere(const std::string& stem) const
{
    const Spec* spec{FindSpec(stem)};
    if (!spec) return {};
    // Its data folder in use: the lock a node takes on it (bitcoind's .lock) is held by another program, such as its
    // own app run by itself. Its data is then left as it is: no second node on it.
    const fs::path dir{NetDataDir(stem)};
    if (!Exists(dir)) return {};
    switch (util::LockDirectory(dir, ".lock", /*probe_only=*/true)) {
    case util::LockResult::Success: return {};
    case util::LockResult::ErrorLock:
        return strprintf("%s is already running in its own app (or another program uses its data folder %s): quit it first, then turn it on here.", spec->name, fs::PathToString(dir));
    case util::LockResult::ErrorWrite:
        return strprintf("The data folder of %s (%s) cannot be written.", spec->name, fs::PathToString(dir));
    }
    return {};
}

void SidechainSupervisor::Enable(const std::string& stem, const std::optional<std::string>& wallet_mode)
{
    const Spec* spec{FindSpec(stem)};
    if (!spec) throw Error(strprintf("There is no sidechain %s.", stem));
    // Said at once, before anything changes: another node (its own app) has its data.
    if (GetStatus(stem).pid == 0) {
        if (const std::string running{RunningElsewhere(stem)}; !running.empty()) throw Error(running);
    }
    if (!FindBinary(stem)) {
        throw Error(strprintf("The program of %s (%sd) is not on this computer: it is looked for next to this app and in the folder set for it.", spec->name, stem));
    }
    if (wallet_mode) SetSetting(stem, "wallet", UniValue{*wallet_mode == "separate" || !spec->phrase_keys ? "separate" : "shared"});
    SetSetting(stem, "enabled", UniValue{true});
    WAIT_LOCK(m_mutex, lock);
    if (m_quit) throw Error("The app is stopping.");
    Proc& proc{m_procs[stem]};
    proc.want_start = true;
    if (proc.state == State::FAILED || proc.state == State::DISABLED) {
        proc.state = State::STOPPED;
        proc.restarts = 0;
        proc.error.clear();
    }
    EnsureThread();
    m_cv.notify_all();
    // Until it was started (or could not be): a few seconds at most, a start does not wait for the node to be ready.
    m_cv.wait_for(lock, 10s, [&]() EXCLUSIVE_LOCKS_REQUIRED(m_mutex) { return proc.pid != 0 || proc.state == State::FAILED || m_quit; });
    if (proc.state == State::FAILED) {
        const std::string error{proc.error};
        REVERSE_LOCK(lock, m_mutex);
        SetSetting(stem, "enabled", UniValue{false});
        throw Error(error);
    }
}

void SidechainSupervisor::Disable(const std::string& stem)
{
    if (!FindSpec(stem)) throw Error(strprintf("There is no sidechain %s.", stem));
    SetSetting(stem, "enabled", UniValue{false});
    LOCK(m_mutex);
    Proc& proc{m_procs[stem]};
    proc.want_start = false;
    if (proc.pid) {
        Signal(proc, SIGTERM);
        proc.state = State::STOPPING;
        proc.stop_deadline = std::chrono::steady_clock::now() + m_options.stop_timeout;
    } else {
        proc.state = State::DISABLED;
        proc.error.clear();
    }
    m_cv.notify_all();
}

void SidechainSupervisor::Restart(const std::string& stem)
{
    LOCK(m_mutex);
    const auto it{m_procs.find(stem)};
    if (it == m_procs.end() || !it->second.pid || it->second.state == State::STOPPING) return;
    // Asked to stop, still wanted: the loop starts it again once it is gone (StopAll still ends it for good).
    LogInfo("webui: starting the %s node again (pid %d)", stem, it->second.pid);
    Signal(it->second, SIGTERM);
    it->second.state = State::STOPPING;
    it->second.stop_deadline = std::chrono::steady_clock::now() + m_options.stop_timeout;
    m_cv.notify_all();
}

void SidechainSupervisor::RefreshBmmWallets()
{
    std::vector<std::pair<std::string, std::string>> running;
    {
        LOCK(m_mutex);
        for (const auto& [stem, proc] : m_procs) {
            if (proc.pid && proc.state != State::STOPPING && proc.want_start) running.emplace_back(stem, proc.bmm_wallet);
        }
    }
    for (const auto& [stem, started] : running) {
        if (BmmWallet(stem) != started) Restart(stem);
    }
}

void SidechainSupervisor::StartEnabled()
{
    LOCK(m_mutex);
    if (m_quit) return;
    bool any{false};
    for (const Spec& s : Catalog()) {
        if (!Enabled(s.stem)) continue;
        m_procs[s.stem].want_start = true;
        any = true;
    }
    if (any) EnsureThread();
    m_cv.notify_all();
}

void SidechainSupervisor::StopAll()
{
    m_stopping = true;
    WAIT_LOCK(m_mutex, lock);
    if (!m_thread.joinable()) {
        m_quit = true;
        return;
    }
    const auto deadline{std::chrono::steady_clock::now() + m_options.stop_timeout};
    for (auto& [stem, proc] : m_procs) {
        proc.want_start = false;
        if (proc.pid && proc.state != State::STOPPING) {
            LogInfo("webui: stopping the %s node (pid %d)", stem, proc.pid);
            Signal(proc, SIGTERM);
            proc.state = State::STOPPING;
            proc.stop_deadline = deadline;
        }
    }
    m_cv.notify_all();
    // The thread reaps them (and ends one that takes too long); then it ends.
    m_cv.wait_for(lock, m_options.stop_timeout + 10s, [&]() EXCLUSIVE_LOCKS_REQUIRED(m_mutex) {
        return std::all_of(m_procs.begin(), m_procs.end(), [](const auto& p) { return p.second.pid == 0; });
    });
    m_quit = true;
    m_cv.notify_all();
    REVERSE_LOCK(lock, m_mutex);
    m_thread.join();
}

void SidechainSupervisor::Signal(Proc& proc, int sig)
{
#ifndef WIN32
    if (proc.pid > 0) kill(proc.pid, sig);
#endif
}

void SidechainSupervisor::Spawn(const std::string& stem, Proc& proc)
{
    const Spec* spec{FindSpec(stem)};
    const auto fail{[&](const std::string& why) {
        proc.state = State::FAILED;
        proc.error = why;
        LogWarning("webui: %s not started: %s", stem, why);
    }};
#ifdef WIN32
    fail("Running sidechains from the app comes to Windows in a later version.");
    return;
#else
    if (!spec) return fail("unknown sidechain");
    const auto binary{FindBinary(stem)};
    if (!binary) return fail(strprintf("The program %sd is not on this computer.", stem));
    if (const std::string running{RunningElsewhere(stem)}; !running.empty()) return fail(running);
    for (const int port : {RpcPort(stem), P2pPort(stem)}) {
        if (!PortFree(port)) return fail(strprintf("Port %d is in use: another %s node may run on this computer. Stop it, or give this one other ports.", port, spec->name));
    }
    std::error_code ec;
    std::filesystem::create_directories(DataDir(stem), ec);
    if (ec) return fail(strprintf("Its data folder %s could not be made: %s", fs::PathToString(DataDir(stem)), ec.message()));
    std::filesystem::create_directories(HomeDir(stem), ec);
    std::string password{m_options.mainchain_rpc_password + "\n"};
    const auto written{common::WriteSecretFile(PasswordFile(stem), password)};
    memory_cleanse(password.data(), password.size());
    if (!written) return fail(strprintf("The key of its connection to Chains could not be written (%s).", util::ErrorString(written).original));

    std::vector<std::string> argv{fs::PathToString(*binary)};
    const std::string bmm_wallet{BmmWallet(stem)};
    for (auto& a : DaemonArgs(stem)) argv.push_back(std::move(a));
    const int log_fd{open(fs::PathToString(LogFile(stem)).c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600)};
    if (log_fd >= 0) {
        const std::string head{strprintf("%s starting %s\n", FormatISO8601DateTime(GetTime()), util::Join(argv, " "))};
        (void)!write(log_fd, head.data(), head.size());
    }
    std::vector<char*> args;
    for (auto& a : argv) args.push_back(a.data());
    args.push_back(nullptr);
    int status_pipe[2];
    if (pipe2(status_pipe, O_CLOEXEC) != 0) {
        if (log_fd >= 0) close(log_fd);
        return fail("no pipe");
    }
    const pid_t parent{getpid()};
    const long open_max{sysconf(_SC_OPEN_MAX)};
    const int fd_limit{static_cast<int>(open_max > 0 && open_max < 65536 ? open_max : 65536)};
    const pid_t pid{fork()};
    if (pid == 0) {
        // The child: only async-signal-safe calls until exec. A process group of its own (a Ctrl+C in a
        // terminal does not reach it); told to stop (SIGTERM, a clean shutdown) if this app dies.
        setpgid(0, 0);
#ifdef __linux__
        prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
        if (getppid() != parent) _exit(1);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        // Below the node of the app and the window: a sidechain catching up does not make the app lag.
        setpriority(PRIO_PROCESS, 0, 5);
        const int devnull{open("/dev/null", O_RDONLY)};
        if (devnull >= 0) dup2(devnull, 0);
        if (log_fd >= 0) {
            dup2(log_fd, 1);
            dup2(log_fd, 2);
        }
        for (int fd{3}; fd < fd_limit; ++fd) {
            if (fd != status_pipe[1]) close(fd);
        }
        execv(args[0], args.data());
        const char failed{1};
        (void)!write(status_pipe[1], &failed, 1);
        _exit(127);
    }
    close(status_pipe[1]);
    if (log_fd >= 0) close(log_fd);
    if (pid < 0) {
        close(status_pipe[0]);
        return fail("The process could not be made.");
    }
    char failed{0};
    ssize_t n;
    while ((n = read(status_pipe[0], &failed, 1)) < 0 && errno == EINTR) {
    }
    close(status_pipe[0]);
    if (n > 0) {
        int status;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        return fail(strprintf("%s could not be run.", fs::PathToString(*binary)));
    }
    proc.pid = pid;
    proc.bmm_wallet = bmm_wallet;
    proc.state = State::STARTING;
    proc.rpc_ready = false;
    proc.wallet_checked = false;
    proc.error.clear();
    proc.started = std::chrono::steady_clock::now();
    proc.next_ping = proc.started + 300ms;
    LogInfo("webui: started the %s node (pid %d) on ports %d/%d, wired to this node on %d", stem, pid, RpcPort(stem), P2pPort(stem), m_options.mainchain_rpc_port);
#endif
}

void SidechainSupervisor::Loop()
{
    util::ThreadRename("webui-sidechains");
    WAIT_LOCK(m_mutex, lock);
    while (true) {
        const auto now{std::chrono::steady_clock::now()};
        std::vector<std::string> changed;
        std::vector<std::string> ping;
        for (auto& [stem, proc] : m_procs) {
#ifndef WIN32
            if (proc.pid > 0) {
                int status{0};
                const pid_t r{waitpid(proc.pid, &status, WNOHANG)};
                if (r == proc.pid || (r < 0 && errno == ECHILD)) {
                    const bool asked{proc.state == State::STOPPING || !proc.want_start};
                    LogInfo("webui: the %s node (pid %d) ended: %s", stem, proc.pid, DescribeExit(status));
                    proc.pid = 0;
                    proc.rpc_ready = false;
                    if (asked) {
                        proc.state = proc.want_start ? State::STOPPED : (Enabled(stem) ? State::STOPPED : State::DISABLED);
                        proc.error.clear();
                    } else {
                        // It stopped by itself: restarted after a wait that grows, until it is given up.
                        if (now - proc.started > m_options.healthy_after) proc.restarts = 0;
                        ++proc.restarts;
                        const std::string tail{Tail(NetDataDir(stem) / "debug.log", 400)};
                        proc.error = DescribeExit(status) + (tail.empty() ? "" : ". The end of its log: " + tail);
                        if (proc.restarts > static_cast<int>(m_options.backoff.size())) {
                            proc.state = State::FAILED;
                        } else {
                            proc.state = State::CRASHED;
                            proc.next_try = now + m_options.backoff[proc.restarts - 1];
                        }
                    }
                    changed.push_back(stem);
                }
            }
            if (proc.pid > 0 && proc.state == State::STOPPING && now >= proc.stop_deadline) {
                LogWarning("webui: the %s node (pid %d) did not stop in time: ended", stem, proc.pid);
                Signal(proc, SIGKILL);
                proc.stop_deadline = now + 24h;
            }
#endif
            const bool may_start{proc.state == State::STOPPED || proc.state == State::DISABLED || (proc.state == State::CRASHED && now >= proc.next_try)};
            if (!m_quit && proc.want_start && proc.pid == 0 && may_start) {
                Spawn(stem, proc);
                changed.push_back(stem);
            }
            if (proc.pid > 0 && proc.state != State::STOPPING && now >= proc.next_ping && (!proc.rpc_ready || !proc.wallet_checked)) {
                ping.push_back(stem);
                proc.next_ping = now + 1s;
            }
        }
        if (m_quit && std::all_of(m_procs.begin(), m_procs.end(), [](const auto& p) { return p.second.pid == 0; })) break;
        if (!changed.empty() || !ping.empty()) {
            REVERSE_LOCK(lock, m_mutex);
            for (const auto& stem : changed) Changed(stem);
            for (const auto& stem : ping) {
                bool ready{false};
                try {
                    Rpc(stem, "getblockcount");
                    ready = true;
                } catch (const std::exception&) {
                    // Not yet (the cookie is written, the node warms up).
                }
                bool check_wallet{false};
                bool now_ready{false};
                {
                    LOCK(m_mutex);
                    auto it{m_procs.find(stem)};
                    if (it == m_procs.end() || it->second.pid == 0 || !ready) continue;
                    if (!it->second.rpc_ready) {
                        it->second.rpc_ready = true;
                        it->second.state = State::RUNNING;
                        now_ready = true;
                    }
                    check_wallet = !it->second.wallet_checked;
                }
                if (check_wallet) {
                    // A wallet of its own, made by the daemon; or, when its wallet is the backup phrase's (doc §23), that
                    // one (made by SeedKeeper with the keys of its account) and the wallet it had before, if any: never a
                    // new one of its own.
                    bool done{false};
                    const std::optional<std::string> seed{m_options.seed_wallet ? m_options.seed_wallet(stem) : std::nullopt};
                    try {
                        const UniValue wallets{Rpc(stem, "listwallets")};
                        if (seed) {
                            std::vector<std::string> found;
                            const UniValue dir{Rpc(stem, "listwalletdir")};
                            for (const UniValue& w : dir.find_value("wallets").getValues()) {
                                if (w.find_value("name").isStr()) found.push_back(w.find_value("name").get_str());
                            }
                            const auto loaded{[&](const std::string& name) {
                                return std::ranges::any_of(wallets.getValues(), [&](const UniValue& w) { return w.isStr() && w.get_str() == name; });
                            }};
                            std::vector<std::string> load;
                            if (!seed->empty() && std::ranges::find(found, *seed) != found.end() && !loaded(*seed)) load.push_back(*seed);
                            // The wallet it had before (made by this app before the phrase, or by its own app): kept in sight,
                            // to move its coins.
                            std::string before{std::ranges::find(found, "main") != found.end() && *seed != "main" ? std::string{"main"} : std::string{}};
                            if (before.empty()) {
                                for (const auto& f : found) {
                                    if (f != *seed && !f.starts_with("seed")) {
                                        before = f;
                                        break;
                                    }
                                }
                            }
                            if (!before.empty() && !loaded(before)) load.push_back(before);
                            for (const auto& name : load) {
                                UniValue p{UniValue::VARR};
                                p.push_back(name);
                                Rpc(stem, "loadwallet", p);
                            }
                            done = true;
                        } else if (wallets.isArray() && !wallets.empty()) {
                            done = true;
                        } else {
                            // A folder adopted from a standalone app keeps its wallet: "main" if it has one, else the
                            // first of its wallets; a new one only where there is none.
                            std::vector<std::string> found;
                            const UniValue dir{Rpc(stem, "listwalletdir")};
                            for (const UniValue& w : dir.find_value("wallets").getValues()) {
                                if (w.find_value("name").isStr()) found.push_back(w.find_value("name").get_str());
                            }
                            UniValue name{UniValue::VARR};
                            if (found.empty()) {
                                name.push_back("main");
                                Rpc(stem, "createwallet", name);
                            } else {
                                name.push_back(std::ranges::find(found, "main") != found.end() ? std::string{"main"} : found.front());
                                Rpc(stem, "loadwallet", name);
                            }
                            done = true;
                        }
                    } catch (const std::exception& e) {
                        LogInfo("webui: the wallet of %s is not ready yet: %s", stem, e.what());
                    }
                    LOCK(m_mutex);
                    auto it{m_procs.find(stem)};
                    if (it != m_procs.end() && done) it->second.wallet_checked = true;
                }
                if (now_ready || check_wallet) Changed(stem);
            }
            continue;
        }
        m_cv.wait_for(lock, m_options.poll);
    }
}

SidechainSupervisor::Status SidechainSupervisor::GetStatus(const std::string& stem) const
{
    Status status;
    {
        LOCK(m_mutex);
        const auto it{m_procs.find(stem)};
        if (it != m_procs.end()) {
            status.state = it->second.state;
            status.pid = it->second.pid;
            status.restarts = it->second.restarts;
            status.rpc_ready = it->second.rpc_ready;
            status.error = it->second.error;
        } else {
            status.state = State::DISABLED;
        }
    }
    if (status.state == State::DISABLED && Enabled(stem)) status.state = State::STOPPED;
    return status;
}

bool SidechainSupervisor::WaitFor(const std::string& stem, const std::function<bool(const Status&)>& pred, std::chrono::milliseconds timeout) const
{
    const auto until{std::chrono::steady_clock::now() + timeout};
    while (std::chrono::steady_clock::now() < until && !m_stopping) {
        if (pred(GetStatus(stem))) return true;
        WAIT_LOCK(m_mutex, lock);
        m_cv.wait_for(lock, 100ms);
    }
    return pred(GetStatus(stem));
}

std::string SidechainSupervisor::CheckDataDir(const std::string& stem, const fs::path& dir) const
{
    if (dir.empty() || !dir.is_absolute()) return "Choose a full path to a folder.";
    const fs::path chains_net{m_options.chains_datadir};
    // Inside the data of Chains only in its "sidechains" folder.
    if (Within(dir, chains_net) && !Within(dir, m_options.chains_datadir / "sidechains")) return "That folder holds the data of Chains: choose another one.";
    if (Within(m_options.chains_datadir, dir)) return "That folder holds the data of Chains: choose another one.";
    for (const Spec& s : Catalog()) {
        if (s.stem == stem) continue;
        const fs::path other{DataDir(s.stem)};
        if (Within(dir, other) || Within(other, dir)) return strprintf("That folder holds the data of %s: choose another one.", s.name);
    }
    std::error_code ec;
    if (std::filesystem::exists(dir, ec)) {
        if (!std::filesystem::is_directory(dir, ec)) return "That is a file, not a folder.";
#ifndef WIN32
        if (access(fs::PathToString(dir).c_str(), W_OK) != 0) return "This app cannot write in that folder.";
#endif
    } else {
        // Made when it is used: in the nearest folder that exists, which this app can write in.
        fs::path parent{dir.parent_path()};
        while (parent.has_parent_path() && parent.parent_path() != parent && !std::filesystem::exists(parent, ec)) parent = parent.parent_path();
        if (!std::filesystem::is_directory(parent, ec)) return "The folder that would hold it does not exist.";
#ifndef WIN32
        if (access(fs::PathToString(parent).c_str(), W_OK) != 0) return "This app cannot write in that folder.";
#endif
    }
    return "";
}

void SidechainSupervisor::SetDataDir(const std::string& stem, const std::optional<fs::path>& dir, bool move)
{
    const Spec* spec{FindSpec(stem)};
    if (!spec) throw Error(strprintf("There is no sidechain %s.", stem));
    {
        LOCK(m_mutex);
        const auto it{m_procs.find(stem)};
        if (it != m_procs.end() && it->second.pid != 0) throw Error(strprintf("Turn %s off first: its data cannot move while it runs.", spec->name));
    }
    const fs::path target{dir ? *dir : DefaultDataDir(stem)};
    if (dir) {
        if (const std::string why{CheckDataDir(stem, target)}; !why.empty()) throw Error(why);
    }
    const fs::path old{DataDir(stem)};
    if (old.lexically_normal() == target.lexically_normal()) return;
    if (move && !IsEmptyDir(old)) {
        if (!IsEmptyDir(target)) throw Error("The new folder is not empty: choose an empty one to move the data into.");
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        if (Exists(target)) std::filesystem::remove(target, ec);
        std::filesystem::rename(old, target, ec);
        if (ec) {
            // Another disk: copied, then removed where it was.
            std::error_code copy_ec;
            std::filesystem::copy(old, target, std::filesystem::copy_options::recursive | std::filesystem::copy_options::copy_symlinks, copy_ec);
            if (copy_ec) {
                std::filesystem::remove_all(target, ec);
                throw Error(strprintf("The data could not be copied there: %s. Nothing was changed.", copy_ec.message()));
            }
            std::filesystem::remove_all(old, ec);
        }
    }
    SetSetting(stem, "dataDir", dir ? UniValue{fs::PathToString(target)} : UniValue{});
}

void SidechainSupervisor::DeleteData(const std::string& stem, bool with_wallets)
{
    const Spec* spec{FindSpec(stem)};
    if (!spec) throw Error(strprintf("There is no sidechain %s.", stem));
    if (Enabled(stem) || GetStatus(stem).pid != 0) throw Error(strprintf("Turn %s off first.", spec->name));
    const fs::path net_dir{NetDataDir(stem)};
    std::error_code ec;
    if (!std::filesystem::is_directory(net_dir, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(net_dir, ec)) {
        const std::string name{fs::PathToString(entry.path().filename())};
        // The folders of other networks (in the data of main) and, unless asked, the wallets stay.
        if (m_options.chain == ChainType::MAIN && (name == "testnet" || name == "signet" || name == "regtest")) continue;
        if (!with_wallets && (name == "wallets" || name == "settings.json")) continue;
        std::error_code rm;
        std::filesystem::remove_all(entry.path(), rm);
    }
    Changed(stem);
}

UniValue SidechainSupervisor::Rpc(const std::string& stem, const std::string& method, const UniValue& params, const std::string& wallet, std::chrono::milliseconds timeout) const
{
#ifdef WIN32
    throw RpcError(-1, "not on Windows yet");
#else
    // As this app's own user of the daemon (APP_RPC_USER), whose methods the daemon limits (AppRpcMethods).
    std::string cookie{strprintf("%s:%s", APP_RPC_USER, m_app_password)};
    UniValue request{UniValue::VOBJ};
    request.pushKV("jsonrpc", "2.0");
    request.pushKV("id", 1);
    request.pushKV("method", method);
    request.pushKV("params", params.isNull() ? UniValue{UniValue::VARR} : params);
    // A wallet name goes in the path as it is: only the plain ones are taken.
    if (wallet.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") != std::string::npos) throw RpcError(-8, "That wallet name is not taken here.");
    const std::string path{wallet.empty() ? std::string{"/"} : "/wallet/" + wallet};
    std::string text{request.write()};
    // The copy of the parameters in the request (keys, a passphrase) not left in freed memory either.
    WipeStrings(request);
    auto [status, body] = HttpPost(RpcPort(stem), path, cookie, text, timeout);
    memory_cleanse(cookie.data(), cookie.size());
    // The request may carry a passphrase the user typed (webuihost): not left in freed memory.
    memory_cleanse(text.data(), text.size());
    if (status == 0) throw RpcError(-28, body);
    if (status == 401 || status == 403) throw RpcError(-32601, strprintf("%s is not one of the methods this app may call on the %s node", method, stem));
    UniValue reply;
    if (!reply.read(body) || !reply.isObject()) throw RpcError(-1, strprintf("HTTP %d from the node", status));
    const UniValue& error{reply.find_value("error")};
    if (!error.isNull()) {
        const UniValue& message{error.find_value("message")};
        const UniValue& code{error.find_value("code")};
        throw RpcError(code.isNum() ? code.getInt<int>() : -1, message.isStr() ? message.get_str() : error.write());
    }
    return reply.find_value("result");
#endif
}

const std::vector<std::string>& SidechainSupervisor::SetupRpcMethods()
{
    static const std::vector<std::string> methods{
        "getblockhash", "listwallets", "listwalletdir", "loadwallet", "createwallet", "getwalletinfo",
        "importdescriptors", "walletpassphrase", "walletlock", "abortrescan",
    };
    return methods;
}

UniValue SidechainSupervisor::SetupRpc(const std::string& stem, const std::string& method, const UniValue& params, const std::string& wallet, std::chrono::milliseconds timeout) const
{
#ifdef WIN32
    throw RpcError(-1, "not on Windows yet");
#else
    // Only the wallet setup of the backup phrase, refused here for anything else (the daemon's cookie can do all).
    if (std::ranges::find(SetupRpcMethods(), method) == SetupRpcMethods().end()) throw RpcError(-32601, strprintf("%s is not a method of the wallet setup", method));
    if (wallet.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") != std::string::npos) throw RpcError(-8, "That wallet name is not taken here.");
    std::string cookie;
    {
        std::ifstream file{static_cast<const std::filesystem::path&>(CookieFile(stem))};
        std::getline(file, cookie);
    }
    if (cookie.find(':') == std::string::npos) throw RpcError(-28, strprintf("The %s node is not ready yet (no cookie)", stem));
    UniValue request{UniValue::VOBJ};
    request.pushKV("jsonrpc", "2.0");
    request.pushKV("id", 1);
    request.pushKV("method", method);
    request.pushKV("params", params.isNull() ? UniValue{UniValue::VARR} : params);
    std::string text{request.write()};
    WipeStrings(request);
    auto [status, body] = HttpPost(RpcPort(stem), wallet.empty() ? std::string{"/"} : "/wallet/" + wallet, cookie, text, timeout);
    memory_cleanse(cookie.data(), cookie.size());
    memory_cleanse(text.data(), text.size());
    if (status == 0) throw RpcError(-28, body);
    if (status == 401 || status == 403) throw RpcError(-32601, strprintf("the %s node refused the wallet setup", stem));
    UniValue reply;
    if (!reply.read(body) || !reply.isObject()) throw RpcError(-1, strprintf("HTTP %d from the node", status));
    const UniValue& error{reply.find_value("error")};
    if (!error.isNull()) {
        const UniValue& message{error.find_value("message")};
        const UniValue& code{error.find_value("code")};
        throw RpcError(code.isNum() ? code.getInt<int>() : -1, message.isStr() ? message.get_str() : error.write());
    }
    return reply.find_value("result");
#endif
}

UniValue SidechainSupervisor::Overview(const std::string& stem, int mainchain_height) const
{
    const Spec* spec{FindSpec(stem)};
    if (!spec) throw Error(strprintf("There is no sidechain %s.", stem));
    const Status status{GetStatus(stem)};
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("name", spec->name);
    out.pushKV("state", StateName(status.state));
    out.pushKV("mainchainHeight", mainchain_height);
    if (!status.rpc_ready) return out;
    try {
        const UniValue chain{Rpc(stem, "getblockchaininfo")};
        out.pushKV("height", chain.find_value("blocks"));
        out.pushKV("headers", chain.find_value("headers"));
        out.pushKV("bestBlockHash", chain.find_value("bestblockhash"));
        out.pushKV("progress", chain.find_value("verificationprogress"));
        const UniValue main{Rpc(stem, "getmainchaininfo")};
        const bool connected{main.find_value("connected").isTrue()};
        const int main_height{main.find_value("height").isNum() ? main.find_value("height").getInt<int>() : -1};
        out.pushKV("mainchainConnected", connected);
        out.pushKV("mainchainSeen", main_height);
        if (!connected && main.find_value("error").isStr()) out.pushKV("mainchainError", main.find_value("error"));
        // In step with Chains: it has seen every block of the node of this app.
        out.pushKV("synced", connected && main_height >= mainchain_height);
        const int height{chain.find_value("blocks").getInt<int>()};
        UniValue blocks{UniValue::VARR};
        for (int h{height}; h >= 0 && h > height - 6; --h) {
            UniValue param{UniValue::VARR};
            param.push_back(h);
            const UniValue hash{Rpc(stem, "getblockhash", param)};
            UniValue hparam{UniValue::VARR};
            hparam.push_back(hash);
            const UniValue header{Rpc(stem, "getblockheader", hparam)};
            UniValue block{UniValue::VOBJ};
            block.pushKV("height", h);
            block.pushKV("hash", hash);
            block.pushKV("time", header.find_value("time"));
            block.pushKV("txs", header.find_value("nTx"));
            blocks.push_back(std::move(block));
        }
        out.pushKV("blocks", std::move(blocks));
    } catch (const std::exception& e) {
        out.pushKV("error", e.what());
        return out;
    }
    try {
        const UniValue wallets{Rpc(stem, "listwallets")};
        if (wallets.isArray() && !wallets.empty()) {
            const std::string name{wallets[0].get_str()};
            const UniValue balances{Rpc(stem, "getbalances", UniValue{UniValue::VARR}, name)};
            const UniValue& mine{balances.find_value("mine")};
            UniValue wallet{UniValue::VOBJ};
            wallet.pushKV("name", name);
            // As the chain's own getbalance counts them (coins spent by a transaction not in the mempool are not there).
            const UniValue counted{ChainAdapter::WalletBalances(mine)};
            wallet.pushKV("trusted", UniValue{UniValue::VNUM, counted.find_value("trusted").get_str()});
            wallet.pushKV("pending", UniValue{UniValue::VNUM, counted.find_value("pending").get_str()});
            wallet.pushKV("immature", UniValue{UniValue::VNUM, counted.find_value("immature").get_str()});
            out.pushKV("wallet", std::move(wallet));
        }
    } catch (const std::exception& e) {
        out.pushKV("walletError", e.what());
    }
    return out;
}

UniValue SidechainSupervisor::List() const
{
    UniValue list{UniValue::VARR};
    for (const Spec& s : Catalog()) {
        const Status status{GetStatus(s.stem)};
        UniValue entry{UniValue::VOBJ};
        entry.pushKV("stem", s.stem);
        entry.pushKV("name", s.name);
        entry.pushKV("description", s.description);
        entry.pushKV("slot", s.slot);
        entry.pushKV("diskGB", s.disk_gb);
        entry.pushKV("ramMB", s.ram_mb);
        entry.pushKV("staking", s.staking);
        // Whether its wallet may come from the backup phrase (zSide: never, its private funds need its own backup).
        entry.pushKV("phraseKeys", s.phrase_keys);
        entry.pushKV("enabled", Enabled(s.stem));
        entry.pushKV("state", StateName(status.state));
        entry.pushKV("pid", status.pid);
        entry.pushKV("restarts", status.restarts);
        entry.pushKV("ready", status.rpc_ready);
        if (!status.error.empty()) entry.pushKV("error", status.error);
        const fs::path data{DataDir(s.stem)};
        entry.pushKV("dataDir", fs::PathToString(data));
        entry.pushKV("defaultDataDir", fs::PathToString(DefaultDataDir(s.stem)));
        entry.pushKV("customDataDir", data.lexically_normal() != DefaultDataDir(s.stem).lexically_normal());
        entry.pushKV("hasData", !IsEmptyDir(NetDataDir(s.stem)));
        // The free space where its data goes (the nearest folder that exists).
        fs::path probe{data};
        std::error_code ec;
        while (!probe.empty() && !std::filesystem::exists(probe, ec) && probe.has_parent_path() && probe.parent_path() != probe) probe = probe.parent_path();
        const auto space{std::filesystem::space(probe, ec)};
        if (!ec) entry.pushKV("freeGB", static_cast<double>(space.available) / 1e9);
        const auto binary{FindBinary(s.stem)};
        entry.pushKV("program", binary ? fs::PathToString(*binary) : std::string{});
        entry.pushKV("programSource", BinarySource(s.stem));
        entry.pushKV("rpcPort", RpcPort(s.stem));
        entry.pushKV("port", P2pPort(s.stem));
        entry.pushKV("wallet", WalletMode(s.stem));
        list.push_back(std::move(entry));
    }
    return list;
}

} // namespace webui
