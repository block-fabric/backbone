// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WEBUI_SEEDKEEPER_H
#define BITCOIN_WEBUI_SEEDKEEPER_H

#include <sync.h>
#include <univalue.h>
#include <util/chaintype.h>
#include <util/fs.h>
#include <webui/dialog.h>
#include <webui/seed.h>
#include <webui/window.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace webui {

/**
 * The single seed of the unified app (doc/unified-app.md §4.1, §23): the backup phrase made or restored in the host's own
 * dialogs, kept in <network datadir>/seed.dat encrypted with the user's password (or, by their choice, without one: §23.11),
 * and each chain's wallet made from it:
 * the app's own chain (account 0, its in-process node) and each sidechain whose wallet is the phrase's (its daemon gets a
 * blank encrypted wallet and the descriptors of its own account, nothing else).
 *
 *  - The phrase and the seed never leave this process: the words are shown, checked and typed in the dialog's window of
 *    the host (dialog.h: its own web view and origin), never in a page or a pack; a daemon gets its account's private key
 *    only (hardened: it derives no other chain's keys, nor the seed). Validator keys are not made from the seed (§12.4).
 *  - The user's passphrase (seed.dat's, and the app's own wallet's) never reaches a sidechain's node either: a sidechain's
 *    wallet is encrypted with a passphrase made from the seed for it alone (seed::WalletPassphrase), and a passphrase the
 *    user types for a spend there is exchanged for it here (PassphraseFor).
 *  - Unlocked for a while only (a session, Hooks::session at most): the seed and the passphrase in locked, wiped memory,
 *    to make the keys of the chains that wait for them (a sidechain turned on, a first run); wiped when they are made or
 *    when the time is up, at Lock and at Stop.
 *  - Its records (which wallet of which chain is the phrase's, its account, the genesis it was made for) are in the app's
 *    settings (Hooks::load/store), apart from seed.dat.
 */
class SeedKeeper
{
public:
    //! The id of the app's own chain among the chains of the phrase (the pages' HOST, lib/chains.svelte.ts).
    static constexpr std::string_view APP_CHAIN{"chains"};

    using SideRpcFn = std::function<UniValue(const std::string& stem, const std::string& method, const UniValue& params, const std::string& wallet, std::chrono::milliseconds timeout)>;
    using NodeRpcFn = std::function<UniValue(const std::string& method, const UniValue& params, const std::string& wallet)>;

    struct Hooks {
        //! <network datadir>/seed.dat.
        fs::path file;
        ChainType chain{ChainType::MAIN};
        //! The chain's parameters say it is a test chain (CChainParams::IsTestChain): coin type 1 and tprv/tpub; else 0
        //! and xprv/xpub, as Core's own wallets (and every chain of the family: the sidechains share the prefixes).
        bool test_chain{false};
        //! The product's name ("Chains"; "Backbone" on the ecash branch): the words of the dialogs.
        std::string product{"Chains"};
        //! The app's own node: ready (its wallets can be made), and its RPC in-process. Errors are thrown as
        //! SidechainSupervisor::RpcError (code, message).
        std::function<bool()> node_ready;
        NodeRpcFn node_rpc;
        //! A sidechain's node (the supervisor): its RPC as this app's user, and whether it runs and answers.
        SideRpcFn side_rpc;
        std::function<bool(const std::string& stem)> side_running;
        //! The sidechains turned on whose wallet is the phrase's (not "separate").
        std::function<std::vector<std::string>()> shared_chains;
        //! The sidechains turned on whose wallet is their own ("separate").
        std::function<std::vector<std::string>()> separate_chains;
        std::function<std::string(const std::string& stem)> name;
        //! A question in the dialog's window (App::AskHost).
        std::function<dialog::Answer(const Window::ConfirmDialog&)> ask;
        //! The records (the app's settings, seed:<network>).
        std::function<UniValue()> load;
        std::function<void(const UniValue&)> store;
        //! Something changed (the page is told: event "seed").
        std::function<void()> changed;
        //! The wallet that pays merged mining's bids changed (the phrase's wallet of the app's chain made, the mining wallet
        //! turned on or off): the supervisor starts again the daemons that pay from another one (-mainchainrpcwallet).
        std::function<void()> paying_changed;
        //! How long the phrase stays unlocked for the keys of the chains that wait for them.
        std::chrono::seconds session{std::chrono::minutes{10}};
        //! The longest an import (its rescan) may take on a sidechain's node.
        std::chrono::milliseconds import_timeout{std::chrono::hours{12}};
    };

    explicit SeedKeeper(Hooks hooks);
    ~SeedKeeper();
    SeedKeeper(const SeedKeeper&) = delete;
    SeedKeeper& operator=(const SeedKeeper&) = delete;

    /** The phrase is wiped from memory, the imports under way stopped (a rescan aborted) and waited for. */
    void Stop();

    bool Exists() const;

    /**
     * seed.status: whether there is a phrase (its words count, when it was made, its fingerprint), whether it is unlocked
     * and for how long, the first run and the offer to existing users (decision 6), and each chain: its wallet from the
     * phrase or its own, made, waiting for its keys, being made, read again (with progress), or the error. Never a word.
     */
    UniValue Status();
    /** seed.create {words: 12|24, extraWord, password}: the phrase made, shown, checked and (password, the default)
     *  encrypted with a password asked in the host's dialogs; the app's wallet made from it, and each sidechain's that
     *  waits. {cancelled: true} when the user stopped. Without a password (password: false) nothing is asked after the
     *  check but the extra word (when chosen): seed.dat and every wallet are then open to anyone using this computer. */
    UniValue Create(const UniValue& params);
    /** seed.restore {password}: the phrase typed in the host's dialog (its words checked), with a password or not; every
     *  chain's wallet made from it and read again from about when it was made. */
    UniValue Restore(const UniValue& params);
    /** seed.unlock {stems?}: the passphrase asked; the phrase unlocked for a while for the keys of those chains (and of any
     *  that waits). */
    UniValue Unlock(const UniValue& params);
    void Lock();
    /** seed.addChain {stem}: that sidechain's wallet made from the phrase now (asks the passphrase when locked). */
    UniValue AddChain(const std::string& stem);
    /** seed.show: the passphrase asked, then the words shown again in the host's dialog. */
    UniValue Show();
    /**
     * seed.changePassphrase: old and new asked; changed on seed.dat and on the app's own wallet made from the phrase, or
     * on neither. The sidechains' wallets keep their passphrases (made from the seed, seed::WalletPassphrase).
     */
    UniValue ChangePassphrase();
    /**
     * seed.addPassword (a phrase without one): a password asked twice; the app's own wallet's passphrase changed from the one
     * made from the seed to it, then seed.dat written with it (or neither); every wallet of the phrase locked. The
     * sidechains' wallets keep their own passphrases (made from the seed): the app gives them when the password is typed.
     */
    UniValue AddPassword();
    /**
     * seed.removePassword: asked with its consequences said plainly (anyone with this computer can spend) and the password;
     * the app's wallet's passphrase changed to the one made from the seed, seed.dat written without one (or neither); every
     * wallet of the phrase then kept open by the app.
     */
    UniValue RemovePassword();
    /** Whether the phrase has a password (true when there is no phrase: nothing to open without one). */
    bool HasPassword() const;
    /**
     * The app's node is ready: without a password, the app's wallet of the phrase is kept open (as an unencrypted one would
     * be), in the background.
     */
    void NodeReady();
    /** For the app's own spends (WalletApi): without a password, its wallet of the phrase opened now; true when it was. */
    bool OpenWithoutPassword(const std::string& wallet);
    /**
     * For a spend on a sidechain (SidechainPages): without a password, that chain's wallet of the phrase's passphrase (made
     * from the seed), sent with the question so none is asked; nothing otherwise (or for another wallet of that chain).
     */
    SecureString PassphraseWithout(const std::string& stem, const std::string& wallet);

    /**
     * mining.create: a wallet of the app's chain that pays merged mining's bids, so the main wallet can stay locked
     * (doc §23.12). From the phrase (its account 1 of the app's chain, without a password: a hot wallet holding a little,
     * brought back by the phrase) or, without a phrase, with keys of its own. Reused when it was made before.
     */
    UniValue MiningCreate();
    /** mining.stop: bids paid from the main wallet again; the mining wallet stays, with its coins. */
    UniValue MiningStop();
    /** The mining wallet's name when it pays the bids ("" when the main wallet does), and its record. */
    std::string MiningWallet();
    UniValue MiningRecord();
    /**
     * What is sent to a sidechain's node for a passphrase the user typed in the trusted dialog (SidechainPages): the user's
     * own phrase passphrase (it opens seed.dat) is never sent; that chain's wallet passphrase made from the seed is sent
     * instead (nothing when the chain has no wallet from the phrase). Any other passphrase (a wallet of its own) as typed.
     */
    SecureString PassphraseFor(const std::string& stem, const SecureString& typed);
    /** seed.dismiss {what: "firstRun" | "offer"}: not offered again by itself (Settings keeps it). */
    void Dismiss(const std::string& what);

    /**
     * For the supervisor: the wallet a sidechain's node is to load when its wallet is the phrase's: its name once made, ""
     * while it waits for its keys (then the supervisor makes none of its own), nothing when the chain's wallet is its own
     * (no phrase, or "separate": the supervisor does as before).
     */
    std::optional<std::string> WalletOf(const std::string& stem);
    /** The wallet of the phrase on a chain, once made ("" when none): the one the app's pages use there first. */
    std::string MadeWallet(const std::string& id);
    /** A sidechain's node answers (the supervisor's notice): its keys are made now if the phrase is unlocked and it has none. */
    void ChainReady(const std::string& stem);

    //! The records of a chain (the app's settings): the wallet of the phrase on it, its account, the genesis it was made for.
    struct Record {
        std::string wallet;
        uint32_t account{0};
        std::string genesis;
        bool imported{false};
    };

private:
    /**
     * While a password is added or removed: refused (HostError) when an import runs, and no import starts until it ends
     * (StartJob): a wallet is never made in the mode being left.
     */
    class ChangingProtection
    {
    public:
        explicit ChangingProtection(SeedKeeper& keeper);
        ~ChangingProtection();
        ChangingProtection(const ChangingProtection&) = delete;
        ChangingProtection& operator=(const ChangingProtection&) = delete;

    private:
        SeedKeeper& m_keeper;
    };

    struct Session {
        seed::Secret secret;
        SecureString passphrase;
        seed::Header header;
        std::chrono::steady_clock::time_point until;
        //! The chains it was opened for: locked as soon as each has its keys.
        std::set<std::string> awaited;
    };
    struct Job {
        std::string state{"importing"};
        std::string error;
        std::string wallet;
        std::thread thread;
        bool done{false};
    };

    UniValue Records() const;
    std::optional<Record> RecordOf(const UniValue& records, const std::string& id) const;
    void SetRecord(const std::string& id, const Record& record);
    void SetFlag(const std::string& key);
    std::optional<std::string> ReadFile(std::string& error) const;
    std::optional<seed::Header> FileHeader() const;

    /** Ask a new wallet passphrase (twice) and, when asked for, the extra word. Nothing when declined. */
    std::optional<std::pair<SecureString, SecureString>> AskNewPassphrase(bool extra_word, const std::string& title);
    /** Ask the passphrase and open seed.dat. Nothing when declined (after three wrong ones, an error). */
    std::optional<std::pair<seed::Opened, SecureString>> AskOpen(const std::string& title, const std::string& message);
    /** Write seed.dat (never over one) and open the session. */
    void Keep(seed::Secret secret, SecureString passphrase, seed::Header header, std::set<std::string> awaited);
    /** Start the imports of the chains that wait for their keys, while the session is open. */
    void StartWaiting();
    void StartJob(const std::string& id) EXCLUSIVE_LOCKS_REQUIRED(m_mutex);
    void RunJob(const std::string& id, seed::Secret secret, SecureString passphrase, int64_t timestamp);
    void ExpireLoop();
    /**
     * Without a password: the wallet of the phrase on a chain kept open (walletpassphrase with its passphrase, for good).
     * The mode is checked and the wallet opened under m_protect_mutex, which AddPassword and RemovePassword hold while they
     * change it: a wallet is never opened once a password was added (it is locked by AddPassword, or not opened at all).
     */
    void KeepOpen(const std::string& id) EXCLUSIVE_LOCKS_REQUIRED(!m_protect_mutex);
    void KeepOpenLocked(const std::string& id) EXCLUSIVE_LOCKS_REQUIRED(m_protect_mutex);
    void KeepOpenLater(const std::string& id) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    /** The passphrase of a chain's wallet of the phrase, made from the seed (the app's own: an empty stem). */
    static SecureString ChainWalletPassphrase(const seed::Secret& secret, const std::string& id, const std::string& genesis);
    /** Ask the extra word alone (a phrase made without a password, with an extra word). Nothing when declined. */
    std::optional<SecureString> AskExtraWord();
    /** The genesis of the app's own chain (its block 0). */
    std::string AppGenesis() const;
    std::optional<UniValue> RecordValue(const std::string& key) const;
    void SetValue(const std::string& key, const UniValue& value);
    void Wipe() EXCLUSIVE_LOCKS_REQUIRED(m_mutex);
    void Changed();
    /** The time the chains are read again from: a new phrase from when it was made, a restored one from its birthday (0: all). */
    static int64_t ScanFrom(const seed::Header& header);
    bool Running(const std::string& id) const;
    UniValue ChainRpc(const std::string& id, const std::string& method, const UniValue& params, const std::string& wallet, std::chrono::milliseconds timeout) const;

    const Hooks m_hooks;
    mutable Mutex m_mutex;
    std::condition_variable m_cv;
    std::optional<Session> m_session GUARDED_BY(m_mutex);
    std::map<std::string, std::shared_ptr<Job>> m_jobs GUARDED_BY(m_mutex);
    std::vector<std::thread> m_finished GUARDED_BY(m_mutex);
    std::thread m_expiry;
    bool m_stopping GUARDED_BY(m_mutex){false};
    //! The chains whose wallet of the phrase is to be kept open (no password): done by the expiry thread, one at a time.
    std::set<std::string> m_keep_open GUARDED_BY(m_mutex);
    //! Held while the password is added or removed (the wallets' passphrases, seed.dat, their locks) and while a wallet is
    //! opened for want of one (KeepOpen, the end of an import): never both at once. Taken before m_mutex.
    Mutex m_protect_mutex ACQUIRED_BEFORE(m_mutex);
    //! A password is being added or removed: no import starts meanwhile (and neither starts while one runs).
    bool m_changing_protection GUARDED_BY(m_mutex){false};
    //! A dialog of the phrase is open (one flow at a time: create, restore, show, change).
    std::atomic<bool> m_busy{false};
};

} // namespace webui

#endif // BITCOIN_WEBUI_SEEDKEEPER_H
