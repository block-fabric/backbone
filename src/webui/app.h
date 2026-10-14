// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WEBUI_APP_H
#define BITCOIN_WEBUI_APP_H

#include <sync.h>
#include <univalue.h>
#include <util/fs.h>

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class ArgsManager;
namespace interfaces {
class Handler;
class Init;
class Node;
} // namespace interfaces

namespace webui {

class AppConfig;
class Bridge;
class EventHub;
class NodeEvents;
class SingleInstance;
class Window;

/** The options of the app on top of the ones of the node (the GUI options of chains-qt, and the debug ones of chains-app). */
void SetupUIArgs(ArgsManager& args);

/**
 * chains-app: the node, in this process, with its interface in a web view.
 *
 * The UI thread runs the window. The node starts and stops on a thread of its
 * own (as InitExecutor does for the Qt GUI): the intro, the configuration,
 * init, then, once a shutdown is asked for, the shutdown. The page follows
 * along through events, and asks the node through the bridge.
 *
 * Phases, as the page sees them: "loading" (before the window shows), "intro"
 * (choose the data directory), "init" (the node starts), "ready",
 * "shutdown" (the node stops), "fatal" (an error the app cannot go on after).
 */
class App
{
public:
    App(interfaces::Init& init, ArgsManager& args, std::vector<std::string> uris);
    ~App();

    /** Run the app; returns the exit status of the process. */
    int Run();

    /** Ask for the app to stop (from any thread). */
    void RequestShutdown() EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

private:
    void RegisterHostMethods();
    void NodeThread() EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    bool IntroIfNeeded() EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    bool StartNode() EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    void SetPhase(const std::string& phase, UniValue data = UniValue{UniValue::VOBJ}) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    UniValue Info() EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    void UpdateTitle() EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    void PollShutdown() EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    void DeliverEvents(const std::string& batch_json);
    void OnInstanceMessage(const std::string& line) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * Show a dialog in the page and wait for its answer (on a thread that is not the UI thread).
     * Without an answer (the app stops), `fallback`.
     */
    UniValue Ask(UniValue dialog, UniValue fallback) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    void Fatal(const std::string& title, const std::string& message, const std::vector<std::string>& details = {}) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    interfaces::Init& m_init;
    ArgsManager& m_args;
    std::unique_ptr<AppConfig> m_config;
    std::unique_ptr<interfaces::Node> m_node;
    std::unique_ptr<Window> m_window;
    std::unique_ptr<EventHub> m_hub;
    std::unique_ptr<Bridge> m_bridge;
    std::unique_ptr<NodeEvents> m_node_events;
    std::unique_ptr<SingleInstance> m_instance;
    std::thread m_node_thread;
    //! Messages and questions of the node.
    std::vector<std::unique_ptr<interfaces::Handler>> m_node_handlers;

    Mutex m_mutex;
    std::condition_variable m_cv;
    std::string m_phase GUARDED_BY(m_mutex){"loading"};
    UniValue m_phase_data GUARDED_BY(m_mutex){UniValue::VOBJ};
    std::string m_network GUARDED_BY(m_mutex);
    std::string m_wallet_title GUARDED_BY(m_mutex);
    bool m_shutdown_requested GUARDED_BY(m_mutex){false};
    bool m_page_ready GUARDED_BY(m_mutex){false};
    std::vector<std::string> m_pending_uris GUARDED_BY(m_mutex);
    //! The intro's choice: the data directory, and the prune setting in MiB (0: off).
    std::optional<std::pair<std::string, int64_t>> m_intro_choice GUARDED_BY(m_mutex);
    bool m_intro_cancelled GUARDED_BY(m_mutex){false};
    //! Dialogs waiting for an answer of the page, by id, and their answers.
    std::map<int64_t, UniValue> m_dialogs GUARDED_BY(m_mutex);
    std::map<int64_t, UniValue> m_answers GUARDED_BY(m_mutex);
    int64_t m_next_dialog GUARDED_BY(m_mutex){1};
    bool m_node_started GUARDED_BY(m_mutex){false};
    std::optional<int> m_exit_code GUARDED_BY(m_mutex);
};

} // namespace webui

#endif // BITCOIN_WEBUI_APP_H
