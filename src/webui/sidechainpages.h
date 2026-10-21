// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WEBUI_SIDECHAINPAGES_H
#define BITCOIN_WEBUI_SIDECHAINPAGES_H

#include <sync.h>
#include <univalue.h>
#include <webui/bridge.h>
#include <webui/dialog.h>
#include <webui/window.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace webui {

/**
 * The pages of the sidechains the app runs, in its own window (doc/unified-app.md §13): each sidechain's own pages, a
 * pack built into the app at a pinned commit (/packs/<stem>/, src/webui/sidechains.json), shown in a frame of the page.
 * Their requests come here (pack.call {stem, request}); each goes
 *  - to the daemon of that sidechain, over RPC (webuihost of its remotehost.h): its RPC methods under its own pages'
 *    policy, its host methods (wallet.*, rpc.confirmed, its own). What needs the user's word comes back as a
 *    description of that chain's own describers with a one-time token: shown here, in the app's trusted dialog with
 *    the chain named and its mark, and only on a yes sent again, exactly, with the token (and the passphrase typed in
 *    that dialog), which the daemon checks (the same request, the same question, the same wallet, once, in time);
 *  - or to the app itself: what the window does (the look, the clipboard, links, the file chooser), answered as for a
 *    frame without a window of its own; a deposit into the sidechain, made by this app's wallet with Chains' own
 *    confirmation;
 *  - or nowhere: what only a whole app does (quit, its options, its first start).
 * The events of each daemon (webuievents) come back to its frame.
 */
class SidechainPages
{
public:
    //! Where a host method of the pages goes.
    enum class Route { DAEMON, HUB, LOCAL, REFUSE };
    static Route RouteOf(const std::string& method);

    //! The version of the host the packs and the daemons must speak (remotehost.h HOST_API, pack.mjs HOST_API).
    static constexpr int HOST_API{1};
    //! Of the preferences, those of the look: the app's always (a pack does not set them).
    static bool IsLookPref(const std::string& key);

    struct Hooks {
        //! A method of a daemon, as the app's user there. Throws SidechainSupervisor::RpcError (a std::runtime_error with a code).
        std::function<UniValue(const std::string& stem, const std::string& method, const UniValue& params, std::chrono::milliseconds timeout)> rpc;
        //! Its daemon answers.
        std::function<bool(const std::string& stem)> running;
        //! The name of the sidechain (the catalog).
        std::function<std::string(const std::string& stem)> name;
        //! The trusted dialog of the app (App::AskHost).
        std::function<dialog::Answer(const Window::ConfirmDialog&)> ask;
        //! A host method of the app itself, on the thread it must run on.
        std::function<UniValue(const std::string& method, const UniValue& params)> hub;
        //! Events of a daemon for its frame: a batch (JSON text of events), or a reset (batches were missed).
        std::function<void(const std::string& stem, const std::string& batch, bool reset)> events;
        //! A deposit into the sidechain, by this app's wallet, with Chains' own confirmation: {destination, amount}.
        std::function<UniValue(const std::string& stem, const UniValue& params)> deposit;
        //! The preferences of the pack of a sidechain (what it set itself), and storing them.
        std::function<UniValue(const std::string& stem)> pack_prefs;
        std::function<void(const std::string& stem, const UniValue& prefs)> set_pack_prefs;
        //! The language of the app ("" English), and the network of the node, as the dialogs name it.
        std::function<std::string()> language;
        std::function<std::string()> network;
    };

    explicit SidechainPages(Hooks hooks);
    ~SidechainPages();
    SidechainPages(const SidechainPages&) = delete;
    SidechainPages& operator=(const SidechainPages&) = delete;

    /** pack.call: one request of the pages of a sidechain {kind: "rpc"|"host", method, params, wallet?}. Throws HostError. */
    UniValue Call(const std::string& stem, const UniValue& request) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    /**
     * The same for the app's own pages (chain.*: never a pack's), with rows of the app's own shown in the question after
     * the chain's name ({label, value, kind}: what the app checked, as a swap's route quoted again).
     */
    UniValue Call(const std::string& stem, const UniValue& request, const UniValue& app_rows) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    /** pack.info: whether its pack is built in, and whether its daemon speaks the same (else: shown read-only, "update needed"). */
    UniValue Info(const std::string& stem) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    /** The packs built in (/packs/index.json): [{stem, name, hostApi, commit, brand}]. */
    static UniValue Packs();
    /** The mark of a sidechain for the dialogs: its pack's icon as a data: URL ("" without one). */
    static std::string Logo(const std::string& stem);
    /** A question of a daemon (remotehost.h DialogJson) as the app's dialog asks it; the chain and its mark added. */
    static Window::ConfirmDialog Question(const UniValue& description, const std::string& chain, const std::string& network, const std::string& logo, const UniValue& app_rows = NullUniValue);
    /** Stop following the daemons (the app stops). */
    void Stop() EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /** At most so many questions in a row for one request (a wrong passphrase asks again). */
    static constexpr int MAX_TRIES{3};

private:
    UniValue Daemon(const std::string& stem, const UniValue& request, const UniValue& app_rows = NullUniValue) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    UniValue Local(const std::string& stem, const std::string& method, const UniValue& params) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    UniValue Overlay(UniValue info) const;
    //! Tokens of the files the user chose for this sidechain that the request names (fresh ones: each is used once).
    UniValue FileTokens(const std::string& stem, const UniValue& request) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    void Follow(const std::string& stem) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    const Hooks m_hooks;
    Mutex m_mutex;
    //! The files chosen in the app's chooser for each sidechain's pages: path → (to write, when).
    std::map<std::string, std::map<std::string, std::pair<bool, std::chrono::steady_clock::time_point>>> m_chosen GUARDED_BY(m_mutex);
    std::map<std::string, std::thread> m_pumps GUARDED_BY(m_mutex);
    std::atomic<bool> m_stop{false};
};

} // namespace webui

#endif // BITCOIN_WEBUI_SIDECHAINPAGES_H
