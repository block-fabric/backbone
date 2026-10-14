// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <webui/app.h>

#include <chainparams.h>
#include <clientversion.h>
#include <common/args.h>
#include <common/init.h>
#include <common/license_info.h>
#include <common/settings.h>
#include <init.h>
#include <interfaces/handler.h>
#include <interfaces/init.h>
#include <interfaces/node.h>
#include <logging.h>
#include <node/interface_ui.h>
#include <util/chaintype.h>
#include <util/exception.h>
#include <util/fs_helpers.h>
#include <util/strencodings.h>
#include <util/threadnames.h>
#include <util/translation.h>
#include <validation.h>
#include <webui/appconfig.h>
#include <webui/bridge.h>
#include <webui/events.h>
#include <webui/nodeevents.h>
#include <webui/singleinstance.h>
#include <webui/window.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <future>
#include <sstream>

using namespace std::chrono_literals;

namespace webui {
namespace {
constexpr int DEFAULT_PRUNE_TARGET_GB{2};
constexpr uint64_t GB_BYTES{1'000'000'000};
constexpr bool DEFAULT_CHOOSE_DATADIR{false};
constexpr bool DEFAULT_SPLASHSCREEN{true};
//! How often the app looks whether the node was asked to stop from elsewhere (RPC stop, a signal).
constexpr auto SHUTDOWN_POLL{200ms};

int64_t PruneGBtoMiB(int64_t gb) { return gb * GB_BYTES / (1024 * 1024); }
int PruneMiBtoGB(int64_t mib) { return (mib * 1024 * 1024 + GB_BYTES - 1) / GB_BYTES; }

std::thread::id g_ui_thread;

std::string NetworkLabel(const std::string& network)
{
    if (network == "main") return "";
    if (network == "test") return "testnet";
    return network;
}

const UniValue& Param(const UniValue& params, const std::string& key)
{
    return params.isObject() ? params.find_value(key) : NullUniValue;
}

std::string ParamStr(const UniValue& params, const std::string& key)
{
    const UniValue& value{Param(params, key)};
    if (!value.isStr()) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("%s: a string is needed", key));
    return value.get_str();
}

/** Free space of the directory, or of the closest folder above it that exists (as FreeSpaceChecker). */
UniValue SpaceOf(const std::string& dir)
{
    UniValue out{UniValue::VOBJ};
    fs::path path{fs::PathFromString(dir)};
    try {
        const bool exists{fs::exists(path)};
        out.pushKV("exists", exists);
        out.pushKV("isDirectory", exists && fs::is_directory(path));
        fs::path parent{path};
        while (!parent.empty() && !fs::exists(parent)) parent = parent.parent_path();
        if (parent.empty()) {
            out.pushKV("error", "No folder of this path exists");
            return out;
        }
        out.pushKV("free", static_cast<int64_t>(fs::space(parent).available));
    } catch (const fs::filesystem_error& e) {
        out.pushKV("error", e.what());
    }
    return out;
}
} // namespace

void SetupUIArgs(ArgsManager& argsman)
{
    argsman.AddArg("-choosedatadir", strprintf("Choose data directory on startup (default: %u)", DEFAULT_CHOOSE_DATADIR), ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
    argsman.AddArg("-lang=<lang>", "Set language, for example \"de_DE\" (default: system locale)", ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
    argsman.AddArg("-min", "Start minimized", ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
    argsman.AddArg("-resetguisettings", "Reset all settings changed in the GUI", ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
    argsman.AddArg("-splash", strprintf("Show splash screen on startup (default: %u)", DEFAULT_SPLASHSCREEN), ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
    argsman.AddArg("-webuidevtools", "Enable the web inspector and the context menu of the web view (default: 0)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::GUI);
    argsman.AddArg("-webuiconfig=<file>", "File of the app settings (default: chains-app.json in the configuration folder of the user)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::GUI);
    argsman.AddArg("-webuitestscript=<file>", "Run this script in the page once it is loaded, with the test methods of the bridge (for the tests of the app)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::GUI);
    argsman.AddArg("-webuitestshots=<dir>", "Folder for the pictures the test script takes", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::GUI);
}

App::App(interfaces::Init& init, ArgsManager& args, std::vector<std::string> uris)
    : m_init(init), m_args(args)
{
    LOCK(m_mutex);
    m_pending_uris = std::move(uris);
}

App::~App()
{
    if (m_node_thread.joinable()) m_node_thread.join();
}

void App::DeliverEvents(const std::string& batch_json)
{
    // The batch goes to the page as a string literal that it parses: data, never code.
    m_window->Eval("window.__chainsEvents && window.__chainsEvents(" + UniValue{batch_json}.write() + ")");
}

UniValue App::Info()
{
    UniValue info{UniValue::VOBJ};
    info.pushKV("name", CLIENT_NAME);
    info.pushKV("version", FormatFullVersion());
    info.pushKV("bugreport", CLIENT_BUGREPORT);
    {
        LOCK(m_mutex);
        info.pushKV("network", m_network);
        info.pushKV("networkLabel", NetworkLabel(m_network));
    }
    std::string datadir;
    bool configured;
    {
        LOCK(m_mutex);
        configured = !m_network.empty();
    }
    try {
        if (configured) datadir = fs::PathToString(m_args.GetDataDirNet());
    } catch (const std::exception&) {
    }
    info.pushKV("datadir", datadir);
    if (configured) info.pushKV("blockSpacing", Params().GetConsensus().nPowTargetSpacing);
#ifdef ENABLE_WALLET
    info.pushKV("walletEnabled", !m_args.GetBoolArg("-disablewallet", false));
#else
    info.pushKV("walletEnabled", false);
#endif
    info.pushKV("devtools", m_args.GetBoolArg("-webuidevtools", false));
    info.pushKV("test", !m_args.GetArg("-webuitestscript", "").empty());
#if defined(__linux__)
    info.pushKV("platform", "linux");
#elif defined(WIN32)
    info.pushKV("platform", "windows");
#elif defined(__APPLE__)
    info.pushKV("platform", "macos");
#else
    info.pushKV("platform", "other");
#endif
    return info;
}

void App::SetPhase(const std::string& phase, UniValue data)
{
    {
        LOCK(m_mutex);
        m_phase = phase;
        m_phase_data = data;
    }
    UniValue event{UniValue::VOBJ};
    event.pushKV("phase", phase);
    event.pushKV("data", std::move(data));
    m_hub->Emit("phase", std::move(event));
}

void App::UpdateTitle()
{
    std::string title{CLIENT_NAME};
    {
        LOCK(m_mutex);
        if (!m_wallet_title.empty()) title += " - " + m_wallet_title;
        if (const std::string label{NetworkLabel(m_network)}; !label.empty()) title += " [" + label + "]";
    }
    m_window->Post([this, title] { m_window->SetTitle(title); });
}

UniValue App::Ask(UniValue dialog, UniValue fallback)
{
    if (std::this_thread::get_id() == g_ui_thread) {
        // The UI thread cannot wait for the page: show it and go on.
        m_hub->Emit("dialog", std::move(dialog));
        return fallback;
    }
    int64_t id;
    {
        LOCK(m_mutex);
        if (m_shutdown_requested) return fallback;
        id = m_next_dialog++;
        dialog.pushKV("id", id);
        m_dialogs[id] = dialog;
    }
    m_hub->Emit("dialog", dialog);
    WAIT_LOCK(m_mutex, lock);
    m_cv.wait(lock, [&]() EXCLUSIVE_LOCKS_REQUIRED(m_mutex) { return m_answers.contains(id) || m_shutdown_requested; });
    m_dialogs.erase(id);
    const auto it{m_answers.find(id)};
    if (it == m_answers.end()) return fallback;
    UniValue answer{it->second};
    m_answers.erase(it);
    return answer;
}

void App::Fatal(const std::string& title, const std::string& message, const std::vector<std::string>& details)
{
    UniValue data{UniValue::VOBJ};
    data.pushKV("title", title);
    data.pushKV("message", message);
    UniValue list{UniValue::VARR};
    for (const auto& d : details) list.push_back(d);
    data.pushKV("details", std::move(list));
    {
        LOCK(m_mutex);
        if (!m_exit_code) m_exit_code = EXIT_FAILURE;
    }
    m_window->Post([this] { m_window->Show(false); });
    SetPhase("fatal", std::move(data));
}

void App::RequestShutdown()
{
    {
        LOCK(m_mutex);
        if (m_shutdown_requested) return;
        m_shutdown_requested = true;
    }
    LogInfo("webui: shutdown requested");
    // Calls that wait for the node (waitfornewblock, a BMM request) return when it is asked to stop.
    if (m_node) m_node->startShutdown();
    m_cv.notify_all();
}

void App::OnInstanceMessage(const std::string& line)
{
    if (line == "activate") {
        m_window->Post([this] { m_window->Present(); });
    } else if (line.starts_with("uri ")) {
        const std::string uri{line.substr(4)};
        m_window->Post([this] { m_window->Present(); });
        bool deliver;
        {
            LOCK(m_mutex);
            deliver = m_phase == "ready";
            if (!deliver) m_pending_uris.push_back(uri);
        }
        if (deliver) {
            UniValue data{UniValue::VOBJ};
            data.pushKV("uri", uri);
            m_hub->Emit("uri", std::move(data));
        }
    }
}

void App::PollShutdown()
{
    {
        LOCK(m_mutex);
        if (m_shutdown_requested) return;
    }
    if (m_node && m_node->shutdownRequested()) {
        RequestShutdown();
        return;
    }
    m_window->PostDelayed(SHUTDOWN_POLL, [this] { PollShutdown(); });
}

bool App::IntroIfNeeded()
{
    if (!m_args.GetArg("-datadir", "").empty()) return true;
    const std::string default_dir{fs::PathToString(GetDefaultDataDir())};
    std::string dir{m_config->DataDir().value_or(default_dir)};

    if (!fs::exists(fs::PathFromString(dir)) || m_args.GetBoolArg("-choosedatadir", DEFAULT_CHOOSE_DATADIR) ||
        m_config->ResetPending() || m_args.GetBoolArg("-resetguisettings", false)) {
        try {
            SelectParams(m_args.GetChainType());
        } catch (const std::exception& e) {
            InitError(Untranslated(e.what()));
            Fatal("Error", e.what());
            return false;
        }
        const int64_t prune_arg{m_args.GetIntArg("-prune", 0)};
        UniValue data{UniValue::VOBJ};
        data.pushKV("defaultDir", default_dir);
        data.pushKV("dir", dir);
        data.pushKV("blockchainGB", static_cast<int64_t>(Params().AssumedBlockchainSize()));
        data.pushKV("chainstateGB", static_cast<int64_t>(Params().AssumedChainStateSize()));
        data.pushKV("pruneGB", prune_arg > 1 ? PruneMiBtoGB(prune_arg) : DEFAULT_PRUNE_TARGET_GB);
        data.pushKV("minPruneGB", static_cast<int64_t>(std::ceil(MIN_DISK_SPACE_FOR_BLOCK_FILES / double(GB_BYTES))));
        // -prune on the command line decides, as in the Qt intro (the box is ticked or not, and disabled).
        if (m_args.IsArgSet("-prune")) data.pushKV("pruneForced", prune_arg >= 1);
        m_window->Post([this] { m_window->Show(false); });
        SetPhase("intro", std::move(data));

        std::pair<std::string, int64_t> choice;
        {
            WAIT_LOCK(m_mutex, lock);
            m_cv.wait(lock, [&]() EXCLUSIVE_LOCKS_REQUIRED(m_mutex) { return m_intro_choice || m_intro_cancelled || m_shutdown_requested; });
            if (!m_intro_choice) {
                if (!m_exit_code) m_exit_code = EXIT_SUCCESS;
                return false;
            }
            choice = *m_intro_choice;
        }
        dir = choice.first;
        m_config->SetDataDir(dir);
        m_config->SetResetPending(false);
    }
    // Only override -datadir if different from the default, to make it possible to override
    // -datadir in the chains.conf of the default data directory (as chainsd does).
    if (dir != default_dir) m_args.SoftSetArg("-datadir", dir);
    return true;
}

bool App::StartNode()
{
    // The configuration: chains.conf and settings.json.
    auto settings_abort{[this](const bilingual_str& message, const std::vector<std::string>& details) {
        UniValue dialog{UniValue::VOBJ};
        dialog.pushKV("kind", "settingsError");
        dialog.pushKV("title", CLIENT_NAME);
        dialog.pushKV("message", message.original + ".");
        dialog.pushKV("question", "Do you want to reset settings to default values, or to abort without making changes?");
        UniValue list{UniValue::VARR};
        for (const auto& d : details) list.push_back(d);
        dialog.pushKV("details", std::move(list));
        const UniValue answer{Ask(std::move(dialog), UniValue{"abort"})};
        return !(answer.isStr() && answer.get_str() == "reset");
    }};
    if (auto error{common::InitConfig(m_args, settings_abort)}) {
        InitError(error->message, error->details);
        if (error->status == common::ConfigStatus::FAILED_WRITE) {
            Fatal(error->message.original, "A fatal error occurred. Check that settings file is writable, or try running with -nosettings.", error->details);
        } else if (error->status != common::ConfigStatus::ABORTED) {
            Fatal("Error", "Error: " + error->message.original, error->details);
        } else {
            LOCK(m_mutex);
            m_exit_code = EXIT_FAILURE;
        }
        return false;
    }
    {
        LOCK(m_mutex);
        m_network = ChainTypeToString(m_args.GetChainType());
    }

    // One app per data directory: a second start hands its URIs to the first one.
    std::vector<std::string> uris;
    {
        LOCK(m_mutex);
        uris = m_pending_uris;
    }
    const fs::path socket{SingleInstance::SocketPath(m_args.GetDataDirNet())};
    if (SingleInstance::SendToRunning(socket, uris)) {
        tfm::format(std::cout, "%s runs already for this data directory: %s handed to it.\n", CLIENT_NAME, uris.empty() ? "asked it to show itself" : "the URIs were");
        LOCK(m_mutex);
        m_exit_code = EXIT_SUCCESS;
        return false;
    }
    m_instance = std::make_unique<SingleInstance>();
    if (!m_instance->Listen(socket, [this](const std::string& line) { OnInstanceMessage(line); })) {
        LogWarning("webui: cannot take URIs from other starts of the app");
    }

    UpdateTitle();
    const bool minimized{m_args.GetBoolArg("-min", false)};
    m_window->Post([this, minimized] { m_window->Show(minimized); });
    SetPhase("init");

    // As BitcoinApplication::parameterSetup.
    m_args.SoftSetBoolArg("-printtoconsole", false);
    InitLogging(m_args);
    InitParameterInteraction(m_args);

    m_node = m_init.makeNode();
    m_node_events = std::make_unique<NodeEvents>(*m_node, *m_hub);
    m_node_events->SubscribeInit();

    // Messages and questions of the node, in the page (as BitcoinGUI::subscribeToCoreSignals).
    m_node_handlers.push_back(m_node->handleMessageBox([this](const bilingual_str& message, unsigned int style) {
        const unsigned int icon{style & CClientUIInterface::ICON_MASK};
        const std::string level{icon == CClientUIInterface::ICON_ERROR ? "error" : icon == CClientUIInterface::ICON_WARNING ? "warning" : "info"};
        UniValue dialog{UniValue::VOBJ};
        dialog.pushKV("kind", "message");
        dialog.pushKV("level", level);
        dialog.pushKV("title", level == "error" ? "Error" : level == "warning" ? "Warning" : "Information");
        dialog.pushKV("message", message.translated);
        if (style & CClientUIInterface::MODAL) {
            Ask(std::move(dialog), NullUniValue);
        } else {
            m_hub->Emit("notice", std::move(dialog));
        }
    }));
    m_node_handlers.push_back(m_node->handleQuestion([this](const bilingual_str& message, const std::string&, unsigned int style) {
        UniValue dialog{UniValue::VOBJ};
        dialog.pushKV("kind", "question");
        dialog.pushKV("title", CLIENT_NAME);
        dialog.pushKV("message", message.translated);
        dialog.pushKV("style", static_cast<int64_t>(style));
        const UniValue answer{Ask(std::move(dialog), UniValue{false})};
        return answer.isBool() && answer.get_bool();
    }));

    if (m_args.GetBoolArg("-resetguisettings", false)) {
        // As OptionsModel::Reset: settings.json is backed up and reset, and the preferences of the interface too.
        m_node->resetSettings();
        m_config->ResetPrefs(m_network);
    }
    {
        std::optional<std::pair<std::string, int64_t>> choice;
        {
            LOCK(m_mutex);
            choice = m_intro_choice;
        }
        if (choice) {
            // As OptionsModel::SetPruneTargetGB, for the choice of the intro.
            const int64_t prune_mib{choice->second};
            const common::SettingsValue current{m_node->getPersistentSetting("prune")};
            const common::SettingsValue chosen{prune_mib};
            m_node->forceSetting("prune", chosen);
            const int64_t current_mib{current.isNum() ? current.getInt<int64_t>() : 0};
            if ((current_mib > 1) != (prune_mib > 1) || (prune_mib > 1 && PruneMiBtoGB(current_mib) != PruneMiBtoGB(prune_mib))) {
                m_node->updateRwSetting("prune", chosen);
            }
        }
    }

    if (!m_node->baseInitialize()) {
        // The error was shown, as a message of the node.
        LOCK(m_mutex);
        if (!m_exit_code) m_exit_code = EXIT_FAILURE;
        return false;
    }
    {
        LOCK(m_mutex);
        m_node_started = true;
    }

    interfaces::BlockAndHeaderTipInfo tip_info;
    const bool ok{m_node->appInitMain(&tip_info)};
    if (!ok || m_node->shutdownRequested()) {
        RequestShutdown();
        return false;
    }

    interfaces::Node* node{m_node.get()};
    m_bridge->SetRpc([node](const std::string& method, const UniValue& params, const std::string& uri) {
        return node->executeRpc(method, params, uri);
    });
    m_node_events->SubscribeNode();

    UniValue data{UniValue::VOBJ};
    data.pushKV("height", tip_info.block_height);
    data.pushKV("time", tip_info.block_time);
    data.pushKV("headerHeight", tip_info.header_height);
    data.pushKV("headerTime", tip_info.header_time);
    data.pushKV("progress", tip_info.verification_progress);
    SetPhase("ready", std::move(data));

    std::vector<std::string> waiting;
    {
        LOCK(m_mutex);
        waiting.swap(m_pending_uris);
    }
    for (const std::string& uri : waiting) {
        UniValue event{UniValue::VOBJ};
        event.pushKV("uri", uri);
        m_hub->Emit("uri", std::move(event));
    }
    m_window->PostDelayed(SHUTDOWN_POLL, [this] { PollShutdown(); });
    return true;
}

void App::NodeThread()
{
    util::ThreadRename("webui-node");
    bool clean{true};
    try {
        if (IntroIfNeeded()) StartNode();
    } catch (const std::exception& e) {
        PrintExceptionContinue(&e, "Runaway exception");
        Fatal("Runaway exception", strprintf("A fatal error occurred. %s can no longer continue safely and will quit.\n\n%s", CLIENT_NAME, e.what()));
        clean = false;
    } catch (...) {
        PrintExceptionContinue(nullptr, "Runaway exception");
        Fatal("Runaway exception", strprintf("A fatal error occurred. %s can no longer continue safely and will quit.", CLIENT_NAME));
        clean = false;
    }

    bool started;
    {
        WAIT_LOCK(m_mutex, lock);
        // Without a node that runs (the intro was cancelled, an error, another app runs) the app ends at
        // once, unless it shows an error: then when the user closes it.
        if (!m_node_started && m_phase != "fatal") m_shutdown_requested = true;
        m_cv.wait(lock, [&]() EXCLUSIVE_LOCKS_REQUIRED(m_mutex) { return m_shutdown_requested; });
        started = m_node_started && clean;
    }

    for (auto& handler : m_node_handlers) handler->disconnect();
    if (started) {
        SetPhase("shutdown");
        // Let the page show that the node stops before the work starts.
        m_hub->Flush();
        if (m_node_events) m_node_events->Stop();
        m_bridge->Stop();
        if (m_instance) m_instance->Stop();
        LogInfo("webui: shutting the node down");
        m_node->appShutdown();
        LogInfo("webui: node stopped");
    } else {
        if (m_node_events) m_node_events->Stop();
        m_bridge->Stop();
        if (m_instance) m_instance->Stop();
    }
    m_window->Quit();
}

void App::RegisterHostMethods()
{
    Bridge& b{*m_bridge};

    b.RegisterHost("app.hello", [this](const UniValue&) {
        UniValue out{UniValue::VOBJ};
        UniValue dialogs{UniValue::VARR};
        bool first_hello;
        {
            LOCK(m_mutex);
            out.pushKV("phase", m_phase);
            out.pushKV("phaseData", m_phase_data);
            for (const auto& [id, dialog] : m_dialogs) dialogs.push_back(dialog);
            first_hello = !m_page_ready;
            m_page_ready = true;
        }
        out.pushKV("dialogs", std::move(dialogs));
        out.pushKV("info", Info());
        m_hub->SetReady(true);
        const std::string script_path{m_args.GetArg("-webuitestscript", "")};
        if (first_hello && !script_path.empty()) {
            std::ifstream file{static_cast<const std::filesystem::path&>(fs::PathFromString(script_path))};
            std::stringstream script;
            script << file.rdbuf();
            const std::string code{script.str()};
            // A test hook: the script given on the command line (a debug option) runs in the page.
            m_window->PostDelayed(100ms, [this, code] { m_window->Eval(code); });
        }
        return out;
    });
    b.RegisterHost("app.info", [this](const UniValue&) { return Info(); });
    b.RegisterHost("app.quit", [this](const UniValue&) {
        RequestShutdown();
        return UniValue{true};
    });
    b.RegisterHost("app.help", [this](const UniValue&) {
        UniValue out{UniValue::VOBJ};
        out.pushKV("usage", "Usage: chains-app [options] [URI]");
        out.pushKV("options", m_args.GetHelpMessage());
        return out;
    });
    b.RegisterHost("app.about", [](const UniValue&) {
        UniValue out{UniValue::VOBJ};
        out.pushKV("name", CLIENT_NAME);
        out.pushKV("version", FormatFullVersion());
        out.pushKV("license", LicenseInfo());
        return out;
    });

    b.RegisterHost("intro.space", [](const UniValue& params) { return SpaceOf(ParamStr(params, "dir")); });
    b.RegisterHost("intro.choose", [this](const UniValue& params) {
        const std::string dir{ParamStr(params, "dir")};
        if (dir.empty()) throw HostError(BRIDGE_INVALID_REQUEST, "Choose a data directory");
        const UniValue prune{Param(params, "pruneGB")};
        const int64_t prune_gb{prune.isNum() ? prune.getInt<int64_t>() : 0};
        {
            LOCK(m_mutex);
            if (m_phase != "intro") throw HostError("The intro is over");
        }
        try {
            const fs::path path{fs::PathFromString(dir)};
            // If a new data directory has been created, make the wallets folder in it too.
            if (TryCreateDirectories(path)) TryCreateDirectories(path / "wallets");
        } catch (const fs::filesystem_error&) {
            throw HostError(strprintf("Error: Specified data directory \"%s\" cannot be created.", dir));
        }
        {
            LOCK(m_mutex);
            m_intro_choice = std::make_pair(dir, prune_gb > 0 ? PruneGBtoMiB(prune_gb) : int64_t{0});
        }
        m_cv.notify_all();
        return UniValue{true};
    });
    b.RegisterHost("intro.cancel", [this](const UniValue&) {
        {
            LOCK(m_mutex);
            m_intro_cancelled = true;
        }
        m_cv.notify_all();
        return UniValue{true};
    });

    b.RegisterHost("dialog.answer", [this](const UniValue& params) {
        const UniValue id{Param(params, "id")};
        if (!id.isNum()) throw HostError(BRIDGE_INVALID_REQUEST, "id: the id of the dialog is needed");
        {
            LOCK(m_mutex);
            if (!m_dialogs.contains(id.getInt<int64_t>())) return UniValue{false};
            m_answers[id.getInt<int64_t>()] = Param(params, "answer");
        }
        m_cv.notify_all();
        return UniValue{true};
    });

    b.RegisterHost("prefs.get", [this](const UniValue&) {
        std::string network;
        {
            LOCK(m_mutex);
            network = m_network.empty() ? ChainTypeToString(m_args.GetChainType()) : m_network;
        }
        return m_config->Prefs(network);
    });
    b.RegisterHost("prefs.set", [this](const UniValue& params) {
        if (!params.isObject()) throw HostError(BRIDGE_INVALID_REQUEST, "prefs.set takes an object of the preferences to change");
        std::string network;
        {
            LOCK(m_mutex);
            network = m_network.empty() ? ChainTypeToString(m_args.GetChainType()) : m_network;
        }
        try {
            return m_config->UpdatePrefs(network, params);
        } catch (const std::runtime_error& e) {
            throw HostError(e.what());
        }
    });

    const auto need_node{[this]() -> interfaces::Node& {
        LOCK(m_mutex);
        if (!m_node_started || !m_node || m_phase != "ready") throw HostError(BRIDGE_NOT_READY, "The node is not running");
        return *m_node;
    }};
    b.RegisterHost("node.state", [this, need_node](const UniValue&) {
        need_node();
        return m_node_events->Snapshot();
    });
    b.RegisterHost("node.setNetworkActive", [need_node](const UniValue& params) {
        const UniValue active{Param(params, "active")};
        if (!active.isBool()) throw HostError(BRIDGE_INVALID_REQUEST, "active: true or false");
        need_node().setNetworkActive(active.get_bool());
        return UniValue{true};
    });
    b.RegisterHost("node.getSetting", [need_node](const UniValue& params) {
        interfaces::Node& node{need_node()};
        const std::string name{ParamStr(params, "name")};
        UniValue out{UniValue::VOBJ};
        out.pushKV("value", node.getPersistentSetting(name));
        out.pushKV("ignored", node.isSettingIgnored(name));
        return out;
    });
    b.RegisterHost("node.setSetting", [need_node](const UniValue& params) {
        interfaces::Node& node{need_node()};
        const std::string name{ParamStr(params, "name")};
        node.updateRwSetting(name, Param(params, "value"));
        return UniValue{true};
    });

    b.RegisterHost("window.setWallet", [this](const UniValue& params) {
        const UniValue name{Param(params, "name")};
        {
            LOCK(m_mutex);
            m_wallet_title = name.isStr() ? name.get_str() : "";
        }
        UpdateTitle();
        return UniValue{true};
    });
    b.RegisterHost("window.minimize", [this](const UniValue&) {
        m_window->Minimize();
        return UniValue{true};
    }, Bridge::Thread::UI);
    b.RegisterHost("clipboard.write", [this](const UniValue& params) {
        m_window->SetClipboard(ParamStr(params, "text"));
        return UniValue{true};
    }, Bridge::Thread::UI);
    b.RegisterHost("clipboard.read", [this](const UniValue&) {
        const auto text{m_window->GetClipboard()};
        return text ? UniValue{*text} : UniValue{};
    }, Bridge::Thread::UI);

    if (!m_args.GetArg("-webuitestscript", "").empty()) {
        // The hooks of the tests of the app (only with the debug option -webuitestscript).
        b.RegisterHost("test.log", [](const UniValue& params) {
            const std::string message{params.isObject() && params.find_value("message").isStr() ? params.find_value("message").get_str() : params.write()};
            LogInfo("webui test: %s", message);
            tfm::format(std::cout, "webui test: %s\n", message);
            std::cout.flush();
            return UniValue{true};
        });
        b.RegisterHost("test.snapshot", [this](const UniValue& params) {
            const std::string name{ParamStr(params, "name")};
            if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") != std::string::npos) {
                throw HostError(BRIDGE_INVALID_REQUEST, "name: letters, digits, '.', '_' and '-'");
            }
            const fs::path dir{fs::PathFromString(m_args.GetArg("-webuitestshots", ""))};
            if (dir.empty()) throw HostError("No -webuitestshots folder");
            fs::create_directories(dir);
            const std::string path{fs::PathToString(dir / fs::PathFromString(name + ".png"))};
            auto done{std::make_shared<std::promise<bool>>()};
            m_window->Post([this, path, done] { m_window->Snapshot(path, [done](bool ok) { done->set_value(ok); }); });
            auto future{done->get_future()};
            if (future.wait_for(30s) != std::future_status::ready || !future.get()) throw HostError("The snapshot failed");
            return UniValue{path};
        });
        b.RegisterHost("test.exit", [this](const UniValue& params) {
            const UniValue code{Param(params, "code")};
            {
                LOCK(m_mutex);
                m_exit_code = code.isNum() ? code.getInt<int>() : EXIT_FAILURE;
            }
            RequestShutdown();
            return UniValue{true};
        });
    }
}

int App::Run()
{
    g_ui_thread = std::this_thread::get_id();
    const std::string config_path{m_args.GetArg("-webuiconfig", "")};
    m_config = std::make_unique<AppConfig>(config_path.empty() ? AppConfig::DefaultPath() : fs::PathFromString(config_path));
    m_config->Load();

    Window::Options options;
    options.title = CLIENT_NAME;
    options.devtools = m_args.GetBoolArg("-webuidevtools", false);
    Window::Callbacks callbacks;
    callbacks.close_requested = [this] {
        {
            LOCK(m_mutex);
            if (m_phase == "shutdown") return;
        }
        RequestShutdown();
    };
    callbacks.call = [this](const std::string& id, const std::string& args) { m_bridge->Handle(id, args); };
    m_window = Window::Create(options, std::move(callbacks));

    m_hub = std::make_unique<EventHub>(
        [this](const std::string& batch) { DeliverEvents(batch); },
        [this](std::chrono::milliseconds delay, std::function<void()> fn) {
            if (delay.count() == 0) m_window->Post(std::move(fn));
            else m_window->PostDelayed(delay, std::move(fn));
        });
    m_bridge = std::make_unique<Bridge>(
        [this](const std::string& id, int status, const std::string& json) { m_window->Reply(id, status, json); },
        [this](std::function<void()> fn) { m_window->Post(std::move(fn)); });
    RegisterHostMethods();

    m_node_thread = std::thread([this] { NodeThread(); });
    m_window->Run();

    // The loop ends after the node stopped (Quit), or when the window went away without asking.
    RequestShutdown();
    if (m_node_thread.joinable()) m_node_thread.join();

    m_bridge.reset();
    int status;
    bool started;
    {
        LOCK(m_mutex);
        status = m_exit_code.value_or(EXIT_SUCCESS);
        started = m_node_started;
    }
    if (m_node && started && status == EXIT_SUCCESS) status = m_node->getExitStatus();
    for (auto& handler : m_node_handlers) handler->disconnect();
    m_node_events.reset();
    m_window.reset();
    return status;
}

} // namespace webui
