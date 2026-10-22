// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WEBUI_SUPERVISOR_H
#define BITCOIN_WEBUI_SUPERVISOR_H

#include <sync.h>
#include <univalue.h>
#include <util/chaintype.h>
#include <util/fs.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace webui {

/**
 * The sidechains the Chains app runs for the user (doc/unified-app.md): each one a daemon of its own
 * (<stem>d), started in the background by this app, wired to the node that runs in it, watched, restarted
 * when it falls over, and stopped before that node stops. The app is Chains alone until the user enables
 * a sidechain (Settings → Sidechains): nothing here runs, no thread, no process, until then.
 *
 * What it keeps (the app settings, AppConfig, one value per network): for each sidechain whether it is
 * enabled, a data directory of the user's choosing (else <Chains datadir>/sidechains/<stem>/data), a
 * folder of its programs (else found next to the app), and which wallet it uses.
 *
 * Phase 1: the sidechains are shown read-only (Rpc takes the methods that read, nothing else); every
 * process is started from one thread of its own (so that a daemon is told to stop if this app dies:
 * PR_SET_PDEATHSIG), with no shell, no password on its command line, its own process group.
 */
class SidechainSupervisor
{
public:
    /** A sidechain this app knows (the catalog built in; a sidechain added by hand comes in phase 2). */
    struct Spec {
        std::string stem;
        std::string name;
        //! What it is, in a few plain words.
        std::string description;
        int slot{0};
        //! Its default ports are <net>9<code>54 (RPC) and <net>9<code>55 (peers): 9254/9255 for Layers.
        int port_code{0};
        //! About how much it takes, for the wizards: disk in GB (a year of testnet), memory in MB.
        int disk_gb{1};
        int ram_mb{300};
        //! Whether it can stake (FastBlocks, Layers): an option of its wizard.
        bool staking{false};
    };
    static const std::vector<Spec>& Catalog();
    static const Spec* FindSpec(std::string_view stem);

    enum class State { DISABLED, STOPPED, STARTING, RUNNING, STOPPING, CRASHED, FAILED };
    static std::string StateName(State state);

    struct Options {
        //! The data directory of Chains (GetDataDirBase): the sidechains live in its "sidechains" folder.
        fs::path chains_datadir;
        ChainType chain{ChainType::MAIN};
        //! Where the daemons reach the node of this app.
        int mainchain_rpc_port{0};
        std::string mainchain_rpc_user;
        std::string mainchain_rpc_password;
        //! Folders searched for <stem>d after the folder set for that sidechain and its download (in order): the
        //! programs bundled next to the app, -sidechainbindir (the offline path).
        std::vector<fs::path> bin_dirs;
        //! The folder of the version of a sidechain downloaded and made current (ModuleInstaller), if any.
        std::function<std::optional<fs::path>(const std::string& stem)> downloaded;
        //! 0: each sidechain on its own default ports; else RPC port_base + 2 i, peers port_base + 2 i + 1 (i: catalog order).
        int port_base{0};
        //! Given to every daemon after the arguments of the app (tests: -listen=0).
        std::vector<std::string> extra_args;
        std::chrono::milliseconds stop_timeout{std::chrono::seconds{120}};
        std::chrono::milliseconds poll{std::chrono::milliseconds{250}};
        //! Waits before each restart of a daemon that stopped by itself; past the last one, FAILED.
        std::vector<std::chrono::milliseconds> backoff{std::chrono::seconds{2}, std::chrono::seconds{5}, std::chrono::seconds{15}, std::chrono::seconds{30}, std::chrono::seconds{60}};
        //! A daemon that ran this long is well again: its count of restarts starts over.
        std::chrono::seconds healthy_after{std::chrono::minutes{10}};
        //! The wallet a sidechain's node loads when its wallet is the backup phrase's (SeedKeeper::WalletOf, doc §23): its
        //! name, or "" while its keys are not made yet (then no wallet of its own is made); nothing: as before (a wallet
        //! of its own, "main", made when it has none).
        std::function<std::optional<std::string>(const std::string& stem)> seed_wallet;
    };
    using LoadFn = std::function<UniValue()>;
    using StoreFn = std::function<void(const UniValue&)>;
    //! Something changed for a sidechain (its state, its settings): the page is told.
    using NotifyFn = std::function<void(const std::string& stem)>;

    SidechainSupervisor(Options options, LoadFn load, StoreFn store, NotifyFn notify = {});
    ~SidechainSupervisor();
    SidechainSupervisor(const SidechainSupervisor&) = delete;
    SidechainSupervisor& operator=(const SidechainSupervisor&) = delete;

    const Options& GetOptions() const { return m_options; }
    //! Whether its thread runs (only once a sidechain was started: Chains alone runs nothing of this).
    bool ThreadRunning() const { return m_thread.joinable(); }

    /** <Chains datadir>/sidechains/<stem>: its home (the default data, downloads, the log of this supervisor). */
    fs::path HomeDir(const std::string& stem) const;
    fs::path DefaultDataDir(const std::string& stem) const;
    /** Where its daemon keeps its data: the folder the user chose, else DefaultDataDir. */
    fs::path DataDir(const std::string& stem) const;
    /** The data of the network of this app in it (the daemon makes a folder per network but main). */
    fs::path NetDataDir(const std::string& stem) const;
    std::optional<fs::path> FindBinary(const std::string& stem) const;
    /**
     * Where the program of a sidechain comes from: "folder" (a folder the user set for it), "download" (a release
     * downloaded and checked by ModuleInstaller), "bundled" (next to the app, or -sidechainbindir), "none".
     */
    std::string BinarySource(const std::string& stem) const;
    int RpcPort(const std::string& stem) const;
    int P2pPort(const std::string& stem) const;
    /** The arguments its daemon is started with (after the program). Never a password: PasswordFile holds it. */
    std::vector<std::string> DaemonArgs(const std::string& stem) const;
    fs::path PasswordFile(const std::string& stem) const;
    fs::path CookieFile(const std::string& stem) const;
    fs::path LogFile(const std::string& stem) const;

    bool Enabled(const std::string& stem) const;
    bool AnyEnabled() const;
    std::string WalletMode(const std::string& stem) const;

    /** Enable a sidechain and start its daemon (waits for the start, a few seconds at most). Throws std::runtime_error. */
    void Enable(const std::string& stem, const std::optional<std::string>& wallet_mode = std::nullopt);
    /** Disable it: its daemon is asked to stop (SIGTERM, then SIGKILL after stop_timeout); its data and keys stay. */
    void Disable(const std::string& stem);
    /** Start the daemons of the sidechains enabled (when the node of the app is ready). */
    void StartEnabled();
    /** Stop every daemon, all at once, and wait for them (at most stop_timeout); then the thread ends. */
    void StopAll();
    /** StopAll was called: the app is stopping, nothing starts again (a waiter gives up). Thread-safe. */
    bool Stopping() const { return m_stopping.load(); }

    /**
     * Another data directory (nullopt: the default). Only while its daemon does not run. move: the data
     * there now goes along (renamed, or copied then removed across disks); else the daemon will use what is
     * in the new folder (data kept elsewhere before). Throws std::runtime_error.
     */
    void SetDataDir(const std::string& stem, const std::optional<fs::path>& dir, bool move);
    /** Remove the chain data of a disabled sidechain (blocks, state, logs); its wallets stay unless with_wallets. */
    void DeleteData(const std::string& stem, bool with_wallets);
    /**
     * Whether another program has the data of a sidechain (its own app run by itself, a node by hand: the lock of its
     * data folder is held): "" when not, else what the user is to do ("Layers is already running in its own app...").
     */
    std::string RunningElsewhere(const std::string& stem) const;
    /** Whether a folder can be the data directory of a sidechain: "" when it can, else why not. */
    std::string CheckDataDir(const std::string& stem, const fs::path& dir) const;

    struct Status {
        State state{State::DISABLED};
        int pid{0};
        int restarts{0};
        bool rpc_ready{false};
        std::string error;
    };
    Status GetStatus(const std::string& stem) const;
    /** Every sidechain of the catalog, its settings and state (sidechains.list of the page). */
    UniValue List() const;
    /** Wait until a sidechain is in a state; gives up when the app stops (StopAll). */
    bool WaitFor(const std::string& stem, const std::function<bool(const Status&)>& pred, std::chrono::milliseconds timeout) const;

    /** Methods of a sidechain node the pages may call in phase 1: they read, never spend or change. */
    static bool IsReadOnlyMethod(std::string_view method);
    /**
     * The RPC user this app is at each daemon (an -rpcauth of this run, its password in memory only), and the only
     * methods it may call there (-rpcwhitelist): reads of the supervisor and the overview, wallet setup, and the host of
     * the pages over RPC (webuihost, which keeps the pages' own policy and asks for every spend, describeconfirmation,
     * webuievents, webuichosenfile). Never a spend directly (doc/unified-app.md §13).
     */
    static constexpr std::string_view APP_RPC_USER{"chains-app"};
    static const std::vector<std::string>& AppRpcMethods();
    /** The methods the daemons may call on the node of this app (its -rpcwhitelist for the sidechains' user). */
    static const std::vector<std::string>& MainchainRpcMethods();

    /** An error of a daemon's RPC: its code too (the confirmations tell a refused token, a wrong passphrase). */
    struct RpcError : std::runtime_error {
        int code;
        RpcError(int code_in, const std::string& message) : std::runtime_error(message), code(code_in) {}
    };
    /** Call a method of its daemon over JSON-RPC (127.0.0.1, as APP_RPC_USER). Throws RpcError with its error. */
    UniValue Rpc(const std::string& stem, const std::string& method, const UniValue& params = UniValue{UniValue::VARR}, const std::string& wallet = {}, std::chrono::milliseconds timeout = std::chrono::seconds{30}) const;
    /**
     * The wallet setup of the backup phrase (SeedKeeper, doc/unified-app.md §23): a blank wallet made, the keys of its
     * account put in, its passphrase. As the daemon's own user (its cookie, read for the call), never as APP_RPC_USER, whose
     * whitelist stays without them: only the methods of SetupRpcMethods, refused here for any other. Throws RpcError.
     */
    static const std::vector<std::string>& SetupRpcMethods();
    UniValue SetupRpc(const std::string& stem, const std::string& method, const UniValue& params, const std::string& wallet, std::chrono::milliseconds timeout) const;
    /** What the overview of a sidechain shows: height, sync, the balance of its wallet, recent blocks. */
    UniValue Overview(const std::string& stem, int mainchain_height) const;

    /** "user:salt$hmac" for -rpcauth (as share/rpcauth makes it). */
    static std::string RpcAuthLine(const std::string& user, const std::string& password, const std::string& salt_hex);

private:
    struct Proc {
        State state{State::STOPPED};
        int pid{0};
        bool want_start{false};
        bool rpc_ready{false};
        bool wallet_checked{false};
        int restarts{0};
        std::chrono::steady_clock::time_point started{};
        std::chrono::steady_clock::time_point next_try{};
        std::chrono::steady_clock::time_point stop_deadline{};
        std::chrono::steady_clock::time_point next_ping{};
        std::string error;
    };

    UniValue Settings(const std::string& stem) const;
    UniValue Setting(const std::string& stem, const std::string& key) const;
    void SetSetting(const std::string& stem, const std::string& key, const UniValue& value);
    void EnsureThread() EXCLUSIVE_LOCKS_REQUIRED(m_mutex);
    void Loop();
    //! Start a daemon (the thread of the supervisor only). An error is put in proc.
    void Spawn(const std::string& stem, Proc& proc) EXCLUSIVE_LOCKS_REQUIRED(m_mutex);
    void Signal(Proc& proc, int sig) EXCLUSIVE_LOCKS_REQUIRED(m_mutex);
    void Changed(const std::string& stem);

    const Options m_options;
    LoadFn m_load;
    StoreFn m_store;
    NotifyFn m_notify;

    mutable Mutex m_mutex;
    mutable std::condition_variable m_cv;
    std::map<std::string, Proc> m_procs GUARDED_BY(m_mutex);
    bool m_quit GUARDED_BY(m_mutex){false};
    //! Set first thing by StopAll, before the daemons are asked to stop (m_quit is set once they are gone).
    std::atomic<bool> m_stopping{false};
    std::thread m_thread;
    mutable Mutex m_settings_mutex;
    //! The password and the -rpcauth line of APP_RPC_USER at the daemons, made for this run.
    std::string m_app_password;
    std::string m_app_rpcauth;
};

} // namespace webui

#endif // BITCOIN_WEBUI_SUPERVISOR_H
