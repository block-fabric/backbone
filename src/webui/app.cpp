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
#include <chainparamsbase.h>
#include <random.h>
#include <rpc/server.h>
#include <node/interface_ui.h>
#include <util/chaintype.h>
#include <util/exception.h>
#include <util/fs_helpers.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <util/threadnames.h>
#include <util/translation.h>
#include <drivechain/sidechain.h>
#include <validation.h>
#include <webui/appconfig.h>
#include <webui/bridge.h>
#include <webui/confirm.h>
#include <webui/events.h>
#include <webui/guard.h>
#include <webui/i18n.h>
#include <webui/assets.h>
#include <webui/chainadapter.h>
#include <webui/nodeapi.h>
#include <webui/nodeevents.h>
#include <webui/options.h>
#include <webui/rpcpolicy.h>
#include <webui/seedkeeper.h>
#include <webui/sidechainpages.h>
#include <webui/moduleinstaller.h>
#include <webui/supervisor.h>
#include <webui/singleinstance.h>
#ifdef WIN32
#include <webui/urischeme.h>
#endif
#ifdef ENABLE_WALLET
#include <webui/walletapi.h>
#endif
#include <webui/window.h>
#include <webui/windowframe.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <future>
#include <sstream>

using namespace std::chrono_literals;

namespace webui {
namespace {
//! The RPC user of the sidechains this app runs.
const std::string SIDECHAIN_RPC_USER{"chains-app-sidechains"};
constexpr uint64_t GB_BYTES{1'000'000'000};
constexpr bool DEFAULT_CHOOSE_DATADIR{false};
constexpr bool DEFAULT_SPLASHSCREEN{true};
//! How often the app looks whether the node was asked to stop from elsewhere (RPC stop, a signal).
constexpr auto SHUTDOWN_POLL{200ms};

int64_t PruneGBtoMiB(int64_t gb) { return gb * GB_BYTES / (1024 * 1024); }
int PruneMiBtoGB(int64_t mib) { return (mib * 1024 * 1024 + GB_BYTES - 1) / GB_BYTES; }

std::thread::id g_ui_thread;

//! The questions of the host (App::AskHost, audit 7 #8): at most so many wait at once, and a kind declined so many times
//! within the window is refused for the rest of the session.
constexpr int MAX_QUESTIONS_WAITING{4};
constexpr size_t MAX_DECLINED{3};
constexpr int64_t DECLINED_WINDOW_S{60};
//! How long after it was chosen a file may be used (App::TakeChosenFile).
constexpr int64_t CHOSEN_FILE_S{10 * 60};
//! The clipboard (audit 7 #5): how soon after the user's key or click the page may write or read it (milliseconds; a
//! write may follow a call to the node, such as a PSBT made), the reads in a minute at most, the largest text written.
constexpr int64_t CLIPBOARD_WRITE_MS{5000};
constexpr int64_t CLIPBOARD_READ_MS{2000};
constexpr size_t MAX_CLIPBOARD_READS{10};
constexpr size_t MAX_CLIPBOARD_BYTES{1'000'000};

int64_t SteadySeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

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
    argsman.AddArg("-webuicompositing=<auto|on|off>", "How the window is drawn: on, WebKit composites its layers with OpenGL; off, it paints them with the processor; auto, off when OpenGL is emulated by the processor (LIBGL_ALWAYS_SOFTWARE, llvmpipe: WSLg, a virtual machine), where every frame of OpenGL costs every core (default: auto)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::GUI);
    argsman.AddArg("-webuiconfig=<file>", "File of the app settings (default: chains-app.json in the configuration folder of the user)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::GUI);
    argsman.AddArg("-sidechainbindir=<dir>", "Folder with the programs of sidechains this app runs (<name>d), searched before the folder of the app (can be given more than once)", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::GUI);
    argsman.AddArg("-sidechainportbase=<port>", "Ports of the sidechains this app runs: RPC <port>+2i, peers <port>+2i+1 (default: the default ports of each sidechain)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::GUI);
    argsman.AddArg("-sidechainarg=<arg>", "An argument given to every sidechain daemon this app starts (can be given more than once)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::GUI);
    argsman.AddArg("-sidechainsignatures=<auto|require>", "Releases of sidechains downloaded by this app: \"require\" takes only those whose manifest is signed with the release key built into the app; \"auto\" checks a signature when there is one (default: auto)", ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
    argsman.AddArg("-sidechainreleaseapi=<https url>", "Ask this mirror for the releases of the sidechains instead of GitHub (regtest only: the tests)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::GUI);
    argsman.AddArg("-sidechaindownloadcacert=<file>", "Trust this certificate for the downloads of the sidechains' releases (regtest only: the tests)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::GUI);
    argsman.AddArg("-sidechainreleasekey=<hex>", "Trust this release key (x-only public key) beside the keys built in (regtest only: the tests; can be given more than once)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::GUI);
#ifdef WEBUI_TEST_HOOKS
    // Only in a build for the tests (cmake -DWEBUI_TEST_HOOKS=ON): a release has neither the options nor the hooks.
    argsman.AddArg("-webuitestscript=<file>", "Run this script in the page once it is loaded, with the test methods of the bridge (for the tests of the app)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::GUI);
    argsman.AddArg("-webuitestshots=<dir>", "Folder for the pictures the test script takes", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::GUI);
#endif
#ifdef WIN32
    argsman.AddArg("-webuiregisteruri", "Open chains: payment URIs with this program, for this user, unless another one does (default: 1; never with -webuitestscript)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::GUI);
#endif
}

App::App(interfaces::Init& init, ArgsManager& args, std::vector<std::string> uris)
    : m_init(init), m_args(args),
      // The debug options of the app are taken from the command line alone (only it is read yet): a
      // chains.conf cannot run a script in the page, nor open the inspector.
      m_devtools(args.GetBoolArg("-webuidevtools", false)),
#ifdef WEBUI_TEST_HOOKS
      m_test_script(args.GetArg("-webuitestscript", "")),
      m_test_shots(args.GetArg("-webuitestshots", "")),
#endif
      m_config_path(args.GetArg("-webuiconfig", ""))
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
    // The weight a block of this chain and network may have (the explorer draws how full blocks are against it).
    if (configured) info.pushKV("blockWeight", Params().GetConsensus().max_block_weight);
#ifdef ENABLE_WALLET
    info.pushKV("walletEnabled", !m_args.GetBoolArg("-disablewallet", false));
#else
    info.pushKV("walletEnabled", false);
#endif
    info.pushKV("devtools", m_devtools);
    {
        LOCK(m_mutex);
        info.pushKV("language", m_language);
    }
    info.pushKV("test", !m_test_script.empty());
    // Drawn by the processor (software GL, as under WSLg with LIBGL_ALWAYS_SOFTWARE): every frame is costly there, on
    // every core, so the page leaves out its animations and its heavy shadows (doc/unified-app.md §20.6).
    {
        const auto env_on{[](const char* name) {
            const char* v{std::getenv(name)};
            return v && (std::string_view{v} == "1" || std::string_view{v} == "true");
        }};
        info.pushKV("softwareRendering", SoftwareGl() || env_on("WEBKIT_DISABLE_COMPOSITING_MODE"));
    }
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

void App::Inform(UniValue dialog)
{
    {
        LOCK(m_mutex);
        const int64_t id{m_next_dialog++};
        dialog.pushKV("id", id);
        m_dialogs[id] = dialog;
        m_informs.insert(id);
    }
    m_hub->Emit("dialog", dialog);
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

void App::UpdateLanguage()
{
    const std::string language{ChooseLanguage(BuiltinAssets(), m_args.GetArg("-lang", ""))};
    {
        LOCK(m_mutex);
        if (language == m_language) return;
        m_language = language;
    }
    SetCoreCatalog(LoadCatalog(BuiltinAssets(), language));
    UniValue data{UniValue::VOBJ};
    data.pushKV("code", language);
    if (m_hub) m_hub->Emit("locale", std::move(data));
}

void App::CloseRequested()
{
    {
        LOCK(m_mutex);
        if (m_phase == "shutdown") return;
    }
    // As BitcoinGUI::closeEvent: minimized, if the user wants that, else the app stops.
    bool ready;
    {
        LOCK(m_mutex);
        ready = m_phase == "ready";
    }
    if (ready && PrefFlag("minimizeOnClose", false)) {
        m_window->Minimize();
        return;
    }
    RequestShutdown();
}

UniValue App::WindowState()
{
    const TitleBarMode mode{ParseTitleBarMode(m_config->Value("titleBar"))};
    const bool supported{m_window->SupportsFrameless()};
    const bool frameless{m_window->IsFrameless()};
    UniValue state{UniValue::VOBJ};
    //! The title bar chosen in the settings, and whether the window has it now (else after a restart).
    state.pushKV("titleBar", std::string{TitleBarModeName(mode)});
    state.pushKV("supported", supported);
    state.pushKV("frameless", frameless);
    state.pushKV("restartNeeded", supported && frameless != (mode == TitleBarMode::INTEGRATED));
    state.pushKV("maximized", m_window->IsMaximized());
    return state;
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
        // The least of the chain (10000 MiB, so 11 GB, but on regtest), and no less by default.
        data.pushKV("pruneGB", prune_arg > 1 ? PruneMiBtoGB(prune_arg) : NodeOptions::DefaultPruneGB(m_args));
        data.pushKV("minPruneGB", NodeOptions::MinPruneGB(m_args));
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
        // Asked by the host, not the page (audit 7 #18): a reset wipes settings.json.
        m_window->Post([this] { m_window->Show(false); });
        Window::ConfirmDialog dialog;
        dialog.title = strprintf("%s: the settings cannot be read", CLIENT_NAME);
        dialog.message = message.original + ".\n\nDo you want to reset settings to default values, or to abort without making changes?";
        dialog.details = util::Join(details, "\n");
        dialog.accept = "Reset settings";
        dialog.cancel = "Abort";
        return !Confirm(dialog);
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
    // The language option is in settings.json: the page and the node follow it from now on.
    UpdateLanguage();
    // The icon of the window in the colours of the network (NetworkStyle).
    m_window->Post([this, network = ChainTypeToString(m_args.GetChainType())] { m_window->SetNetwork(network); });

    // The debug options come from the command line alone: what chains.conf says of them is ignored.
    const std::vector<std::pair<std::string, std::string>> command_line{{"-webuitestscript", m_test_script}, {"-webuitestshots", m_test_shots}, {"-webuiconfig", m_config_path}};
    for (const auto& [arg, value] : command_line) {
        if (m_args.GetArg(arg, "") != value) LogWarning("webui: %s is taken from the command line only; the configuration file sets it, and is ignored", arg);
    }
    if (m_args.GetBoolArg("-webuidevtools", false) != m_devtools) LogWarning("webui: -webuidevtools is taken from the command line only; the configuration file sets it, and is ignored");

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
        // A question of the node as it starts (rebuild the databases?): asked by the host, not the page (audit 7 #18).
        Window::ConfirmDialog dialog;
        dialog.title = strprintf("%s: a question of the node", CLIENT_NAME);
        dialog.message = message.translated;
        dialog.accept = "OK";
        dialog.cancel = (style & CClientUIInterface::BTN_ABORT) ? "Abort" : "Cancel";
        return Confirm(dialog);
    }));

    // Changes of the Settings page never applied (the app stopped first): taken back before the node
    // reads its settings, as the Cancel of the options dialog would.
    UndoOptions(*m_node, "never applied");

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

    // A prune size saved below the least of the chain (an older version offered 2 GB): raised to it, said
    // once, rather than a node that refuses to start.
    if (const auto low{NodeOptions::RaiseLowPrune(*m_node, m_args)}) {
        UniValue dialog{UniValue::VOBJ};
        dialog.pushKV("kind", "message");
        dialog.pushKV("level", "info");
        dialog.pushKV("title", "Prune size raised");
        dialog.pushKV("message", strprintf("The prune size was %d MiB, below the least this network allows (%d MiB). It is now %d GB: the node keeps that much of the most recent blocks.",
                                           *low, CreateChainParams(m_args, m_args.GetChainType())->MinPruneTarget() / (1024 * 1024), NodeOptions::MinPruneGB(m_args)));
        Inform(std::move(dialog));
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

    // The user the sidechains this app runs reach this node as (doc/unified-app.md): an -rpcauth of this run
    // only, its password in memory and in an owner-only file of each sidechain, never on a command line. Only
    // used if a sidechain is enabled: the RPC server is not started for it otherwise.
    {
        std::array<unsigned char, 32> secret;
        GetStrongRandBytes(secret);
        std::array<unsigned char, 16> salt;
        GetStrongRandBytes(salt);
        m_sidechain_rpc_password = HexStr(secret);
        const std::string line{SidechainSupervisor::RpcAuthLine(SIDECHAIN_RPC_USER, m_sidechain_rpc_password, HexStr(salt))};
        // What the daemons may call here: their follower, BMM, bundles, what their pages read of the mainchain
        // (SidechainSupervisor::MainchainRpcMethods); never a spend of this wallet but the BMM requests the user turned on
        // in a sidechain (doc/unified-app.md §13). The user's own users keep what they had: a whitelist of one user
        // empties the others' only with -rpcwhitelistdefault, set here to 0 unless the user set whitelists or it.
        std::string whitelist{SIDECHAIN_RPC_USER + ":"};
        for (size_t i{0}; i < SidechainSupervisor::MainchainRpcMethods().size(); ++i) whitelist += (i ? "," : "") + SidechainSupervisor::MainchainRpcMethods()[i];
        const bool user_whitelists{!m_args.GetArgs("-rpcwhitelist").empty() || m_args.IsArgSet("-rpcwhitelistdefault")};
        m_args.LockSettings([&](common::Settings& settings) {
            settings.command_line_options["rpcauth"].emplace_back(line);
            settings.command_line_options["rpcwhitelist"].emplace_back(whitelist);
            if (!user_whitelists) settings.command_line_options["rpcwhitelistdefault"] = {UniValue{"0"}};
        });
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
    // A restart was what the options waited for (OptionsModel::Init).
    {
        UniValue change{UniValue::VOBJ};
        change.pushKV("restartRequired", UniValue{});
        std::string network;
        {
            LOCK(m_mutex);
            network = m_network;
        }
        m_config->UpdatePrefs(network, change);
    }
    SetPhase("ready", std::move(data));
    m_window->Post([this] { UpdateTray(); });
    StartSidechains();

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
        // The sidechains first: they follow this node (doc/unified-app.md, shutdown order).
        std::shared_ptr<SidechainSupervisor> supervisor;
        std::shared_ptr<SidechainPages> pages;
        {
            LOCK(m_mutex);
            supervisor = m_supervisor;
            pages = m_pages;
        }
        if (pages) pages->Stop();
        if (supervisor) supervisor->StopAll();
        {
            // The backup phrase wiped from memory; the wallets being made from it stopped (after the sidechains, before
            // the node: a rescan of this node's wallet is aborted).
            std::shared_ptr<SeedKeeper> keeper;
            {
                LOCK(m_mutex);
                keeper = m_seed;
            }
            if (keeper) keeper->Stop();
        }
        {
            // A download still running stops (nothing half done stays installed).
            std::shared_ptr<ModuleInstaller> installer;
            {
                LOCK(m_mutex);
                installer = m_installer;
            }
            // After the supervisor: the end of an update waits for its daemon, which no longer starts.
            if (installer) installer->Stop();
        }
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

std::optional<std::string> App::Choose(const Window::FileDialog& dialog)
{
    std::optional<std::string> chosen;
    bool from_test{false};
#ifdef WEBUI_TEST_HOOKS
    {
        LOCK(m_mutex);
        if (!m_test_files.empty()) {
            const UniValue answer{m_test_files.front()};
            m_test_files.pop_front();
            if (answer.isStr()) chosen = answer.get_str();
            from_test = true;
        }
    }
#endif
    if (!from_test) {
        auto done{std::make_shared<std::promise<std::optional<std::string>>>()};
        // Dark or light as the app is (the chooser is the system's).
        Window::FileDialog shown{dialog};
        shown.scheme = dialog::SchemeOf(NetworkPrefs());
        m_window->Post([this, dialog = std::move(shown), done] { m_window->ChooseFile(dialog, [done](std::optional<std::string> path) { done->set_value(std::move(path)); }); });
        auto future{done->get_future()};
        // The user takes the time needed; the app may stop meanwhile.
        while (future.wait_for(200ms) != std::future_status::ready) {
            LOCK(m_mutex);
            if (m_shutdown_requested) return std::nullopt;
        }
        chosen = future.get();
    }
    if (!chosen) return std::nullopt;
    if (!dialog.folder) {
        LOCK(m_mutex);
        (dialog.save ? m_writable : m_readable)[*chosen] = SteadySeconds();
    }
    return chosen;
}

bool App::TakeChosenFile(const std::string& path, bool write)
{
    LOCK(m_mutex);
    auto& grants{write ? m_writable : m_readable};
    const auto it{grants.find(path)};
    if (it == grants.end()) return false;
    const bool fresh{SteadySeconds() - it->second <= CHOSEN_FILE_S};
    grants.erase(it);
    return fresh;
}

dialog::Request App::DialogRequest(const Window::ConfirmDialog& confirm)
{
    dialog::Request request;
    request.title = confirm.title;
    request.message = confirm.message;
    request.details = confirm.details;
    request.accept = confirm.accept;
    if (!confirm.cancel.empty()) request.cancel = confirm.cancel;
    request.level = confirm.level;
    request.rows = confirm.rows;
    request.passphrase = confirm.passphrase;
    request.passphrase_label = confirm.passphrase_label;
    request.error = confirm.error;
    request.chain = confirm.chain;
    request.chain_network = confirm.chain_network;
    request.chain_logo = confirm.chain_logo;
    request.words = confirm.words;
    request.fields = confirm.fields;
    request.checkbox = confirm.checkbox;
    std::string language;
    {
        LOCK(m_mutex);
        language = m_language;
    }
    bool rtl{false};
    if (auto catalog{LoadCatalog(BuiltinAssets(), language)}) rtl = catalog->find_value("rtl").isTrue();
    UniValue prefs{UniValue::VOBJ};
    try {
        prefs = NetworkPrefs();
    } catch (const std::exception&) {
        // Before the network is known (a question of the configuration): the default look.
    }
    request.look = dialog::Look(prefs, rtl, language);
    return request;
}


bool App::Confirm(const Window::ConfirmDialog& confirm)
{
    return AskHost(confirm).accepted;
}

dialog::Answer App::AskHost(const Window::ConfirmDialog& confirm)
{
    // Every fact of a confirmation shown, none cut: more than a dialog shows is refused instead.
    if (confirm.rows.size() > dialog::MAX_ROWS) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("Too much for one question (%u facts; at most %u): make it in parts", confirm.rows.size(), dialog::MAX_ROWS));
    const std::string kind{confirm.kind.empty() ? confirm.title : confirm.kind};
    Window::ConfirmDialog shown{confirm};
    {
        LOCK(m_mutex);
        if (m_shutdown_requested) return {};
        QuestionKind& k{m_question_kinds[kind]};
        const int64_t now{SteadySeconds()};
        while (!k.declined.empty() && k.declined.front() < now - DECLINED_WINDOW_S) k.declined.pop_front();
        if (k.refused) {
            LogInfo("webui: \"%s\" is not asked again: it was declined %u times in a row", guard::OneLine(kind, 80), MAX_DECLINED);
            return {};
        }
        // One question of a kind at a time, and a few at once: a page that asks again and again waits for none of them.
        if (k.waiting || m_questions_waiting >= MAX_QUESTIONS_WAITING) {
            LogInfo("webui: \"%s\" is not asked: %s", guard::OneLine(kind, 80), k.waiting ? "the same question waits for an answer" : "too many questions wait for an answer");
            return {};
        }
        k.waiting = true;
        ++m_questions_waiting;
        if (k.declined.size() + 1 >= MAX_DECLINED) {
            shown.message = strprintf("This was asked and declined %u times in the last minute. Decline it once more and the app stops asking it until it starts again.\n\n%s",
                                      k.declined.size(), shown.message);
        }
    }
    ++m_host_questions;
    // Answered: the kind may be asked again (or not, declined too often).
    const auto answered{[&](bool accepted, bool counted) {
        --m_host_questions;
        LOCK(m_mutex);
        QuestionKind& k{m_question_kinds[kind]};
        k.waiting = false;
        --m_questions_waiting;
        if (!counted) return;
        if (accepted) {
            k.declined.clear();
        } else {
            k.declined.push_back(SteadySeconds());
            if (k.declined.size() >= MAX_DECLINED) k.refused = true;
        }
    }};
    dialog::Request request{DialogRequest(shown)};
#ifdef WEBUI_TEST_HOOKS
    {
        std::optional<TestAnswer> answer;
        bool unexpected{false};
        {
            LOCK(m_mutex);
            if (!m_test_confirms.empty()) {
                answer = m_test_confirms.front();
                m_test_confirms.pop_front();
                m_test_confirmed.push_back(shown);
            } else if (m_test_confirm_always) {
                answer = m_test_confirm_always;
                m_test_confirmed.push_back(shown);
            } else if (!m_test_script.empty()) {
                // A test that did not expect the question: declined, and kept for test.confirmed (never a dialog
                // on the desktop of whoever runs the tests).
                m_test_confirmed.push_back(shown);
                unexpected = true;
            }
        }
        if (unexpected) {
            answered(false, false);
            return {};
        }
        if (answer) {
            // Answered here, or shown (in the dialog window, or in the dialog of the system as when that window cannot be made).
            if (answer->kind == TestConfirm::YES || answer->kind == TestConfirm::NO) {
                dialog::Answer out;
                out.accepted = answer->kind == TestConfirm::YES;
                if (out.accepted && shown.passphrase) out.passphrase.assign(answer->passphrase.begin(), answer->passphrase.end());
                if (out.accepted) {
                    for (const dialog::Field& f : shown.fields) {
                        if (const auto it{answer->fields.find(f.id)}; it != answer->fields.end()) out.fields[f.id].assign(it->second.begin(), it->second.end());
                    }
                    out.checked = !shown.checkbox.empty();
                }
                answered(out.accepted, false);
                return out;
            }
            request.test_fallback = answer->kind == TestConfirm::FALLBACK;
        }
    }
#endif
    request.withdrawn = std::make_shared<std::atomic<bool>>(false);
    auto done{std::make_shared<std::promise<dialog::Answer>>()};
    m_window->Post([this, request, done] {
        m_window->Ask(request, [done](dialog::Answer answer) { done->set_value(std::move(answer)); });
    });
    auto future{done->get_future()};
    while (future.wait_for(200ms) != std::future_status::ready) {
        // The page cancelled the request (or the app stops): the question is withdrawn if it still waits its turn.
        const bool gone{Bridge::CurrentRequestDone() || WITH_LOCK(m_mutex, return m_shutdown_requested)};
        if (gone) {
            request.withdrawn->store(true);
            answered(false, false);
            return {};
        }
    }
    dialog::Answer answer{future.get()};
    answered(answer.accepted, true);
    return answer;
}

UniValue App::NetworkPrefs()
{
    std::string network;
    {
        LOCK(m_mutex);
        network = m_network.empty() ? ChainTypeToString(m_args.GetChainType()) : m_network;
    }
    return m_config->Prefs(network);
}

bool App::PrefFlag(const std::string& key, bool fallback)
{
    const UniValue prefs{NetworkPrefs()};
    const UniValue& value{prefs.find_value(key)};
    return value.isBool() ? value.get_bool() : fallback;
}

void App::UpdateTray()
{
    std::string label;
    bool ready;
    {
        LOCK(m_mutex);
        label = NetworkLabel(m_network);
        ready = m_phase == "ready";
    }
    Window::Tray tray;
    // As BitcoinGUI: the icon shows unless the user turned it off (fHideTrayIcon).
    tray.visible = ready && PrefFlag("showTray", true);
    tray.tooltip = strprintf("%s client", CLIENT_NAME) + (label.empty() ? "" : " [" + label + "]");
    tray.menu = {
        {"toggle", "Show / Hide"},
        {"", "", true, true},
        {"send", "Send"},
        {"receive", "Receive"},
        {"", "", true, true},
        {"sign", "Sign message…"},
        {"verify", "Verify message…"},
        {"", "", true, true},
        {"options", "Options…"},
        {"node", "Node window"},
        {"", "", true, true},
        {"quit", "Quit"},
    };
    m_window->SetTray(tray);
}

void App::OnTrayMenu(const std::string& id)
{
    if (id == "quit") {
        RequestShutdown();
        return;
    }
    if (id == "toggle") {
        if (m_window->IsVisible()) m_window->Hide();
        else m_window->Present();
        return;
    }
    static const std::map<std::string, std::pair<std::string, std::string>> pages{
        {"send", {"send", ""}},
        {"receive", {"receive", ""}},
        {"sign", {"messages", "sign"}},
        {"verify", {"messages", "verify"}},
        {"options", {"settings", "main"}},
        {"node", {"node", "info"}},
    };
    const auto page{pages.find(id)};
    if (page == pages.end()) return;
    m_window->Present();
    UniValue data{UniValue::VOBJ};
    data.pushKV("page", page->second.first);
    data.pushKV("tab", page->second.second);
    m_hub->Emit("navigate", std::move(data));
}

void App::UndoOptions(interfaces::Node& node, const std::string& why)
{
    const size_t count{m_options_journal->Undo(node, [this](bool restart) {
        UniValue change{UniValue::VOBJ};
        change.pushKV("restartRequired", restart ? UniValue{true} : UniValue{});
        std::string network;
        {
            LOCK(m_mutex);
            network = m_network;
        }
        m_config->UpdatePrefs(network, change);
    })};
    if (count > 0) LogInfo("webui: %d changes of the options taken back (%s)", count, why);
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
#ifdef WEBUI_TEST_HOOKS
        const std::string script_path{m_test_script};
        if (first_hello && !script_path.empty()) {
            std::ifstream file{static_cast<const std::filesystem::path&>(fs::PathFromString(script_path))};
            std::stringstream script;
            script << file.rdbuf();
            // A test hook: the script given on the command line (a debug option) runs in the page.
            // It marks that it ran: a script that does not parse (or throws at its start) runs not at
            // all, and the page would say nothing; the check after it says so and ends the app.
            const std::string code{script.str() + "\n;window.__chainsTestScriptRan = true;\n"};
            const std::string check{"if (!window.__chainsTestScriptRan) {"
                                    " const say = (method, params) => window.__chains({ kind: 'host', method, params });"
                                    " say('test.log', { message: 'SCRIPT_ERROR the test script did not run: it does not parse, or throws at its start' })"
                                    ".finally(() => say('test.exit', { code: 1 })); }"};
            m_window->PostDelayed(100ms, [this, code] { m_window->Eval(code); });
            m_window->PostDelayed(3s, [this, check] { m_window->Eval(check); });
        }
#else
        (void)first_hello;
#endif
        return out;
    });
    b.RegisterHost("app.info", [this](const UniValue&) { return Info(); });
    // The language of the interface, with its catalog (the messages of the page), and the languages there are.
    b.RegisterHost("app.locale", [this](const UniValue&) {
        std::string language;
        {
            LOCK(m_mutex);
            language = m_language;
        }
        UniValue out{UniValue::VOBJ};
        out.pushKV("code", language);
        if (auto catalog{LoadCatalog(BuiltinAssets(), language)}) {
            out.pushKV("name", catalog->find_value("name"));
            out.pushKV("rtl", catalog->find_value("rtl"));
            out.pushKV("messages", catalog->find_value("gui"));
        } else {
            out.pushKV("name", "English");
            out.pushKV("rtl", false);
            out.pushKV("messages", UniValue{UniValue::VOBJ});
        }
        return out;
    });
    b.RegisterHost("app.languages", [](const UniValue&) { return Languages(BuiltinAssets()); });
    // The language option changed (Settings, applied): the page and the node follow it at once.
    b.RegisterHost("app.updateLanguage", [this](const UniValue&) {
        UpdateLanguage();
        LOCK(m_mutex);
        return UniValue{m_language};
    });
    b.RegisterHost("app.quit", [this](const UniValue&) {
        RequestShutdown();
        return UniValue{true};
    });
    // Stop, and start again with the same options (a change of the options needs it).
    b.RegisterHost("app.restart", [this](const UniValue&) {
        if (!m_test_script.empty()) throw HostError("No restart in the tests");
        m_restart_requested = true;
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
        int64_t prune_gb{prune.isNum() ? prune.getInt<int64_t>() : 0};
        // Bounded as options.set bounds pruneSize (audit 7 #17): never a size that overflows, nor one below the least of the chain.
        if (prune_gb > 0) prune_gb = std::clamp<int64_t>(prune_gb, NodeOptions::MinPruneGB(m_args), NodeOptions::MAX_PRUNE_GB);
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
            if (m_informs.erase(id.getInt<int64_t>())) {
                m_dialogs.erase(id.getInt<int64_t>());
                return UniValue{true};
            }
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
        UniValue prefs;
        try {
            prefs = m_config->UpdatePrefs(network, params);
        } catch (const std::runtime_error& e) {
            throw HostError(e.what());
        }
        if (params.exists("showTray")) m_window->Post([this] { UpdateTray(); });
        return prefs;
    });

    // While a question of the host shows, the page neither hides nor moves the window under it (audit 7 #7): a dialog
    // that comes back with the window would be under the user's pointer, armed.
    const auto RefuseWhileQuestion{[this] {
        if (HostQuestionOpen()) throw HostError(BRIDGE_CANCELLED, "A question of the app is open");
    }};
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
        // The settings of the Settings page only: never one that holds a secret, such as rpcpassword (audit 7 #15).
        static const std::set<std::string> READABLE{"dbcache", "par", "prune", "spendzeroconfchange", "signer", "natpmp", "listen",
                                                    "server", "proxy", "proxyrandomize", "onion", "onlynet", "lang", "maxuploadtarget"};
        if (!READABLE.contains(name)) throw HostError(BRIDGE_INVALID_REQUEST, "This setting is not one the pages read");
        UniValue out{UniValue::VOBJ};
        out.pushKV("value", node.getPersistentSetting(name));
        out.pushKV("ignored", node.isSettingIgnored(name));
        return out;
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
    b.RegisterHost("window.minimize", [this, RefuseWhileQuestion](const UniValue&) {
        RefuseWhileQuestion();
        m_window->Minimize();
        return UniValue{true};
    }, Bridge::Thread::UI);
    // The integrated title bar of the page (doc/webui-layouts.md): its buttons, its drags, its setting.
    b.RegisterHost("window.state", [this](const UniValue&) { return WindowState(); }, Bridge::Thread::UI);
    b.RegisterHost("window.isMaximized", [this](const UniValue&) { return UniValue{m_window->IsMaximized()}; }, Bridge::Thread::UI);
    b.RegisterHost("window.toggleMaximize", [this, RefuseWhileQuestion](const UniValue&) {
        RefuseWhileQuestion();
        m_window->ToggleMaximize();
        return UniValue{true};
    }, Bridge::Thread::UI);
    b.RegisterHost("window.close", [this, RefuseWhileQuestion](const UniValue&) {
        RefuseWhileQuestion();
        // As the close button of the system: minimized or to the tray if the user wants that, else the app stops.
        CloseRequested();
        return UniValue{true};
    }, Bridge::Thread::UI);
    b.RegisterHost("window.startMove", [this, RefuseWhileQuestion](const UniValue&) {
        RefuseWhileQuestion();
        m_window->StartMove();
        return UniValue{true};
    }, Bridge::Thread::UI);
    b.RegisterHost("window.startResize", [this, RefuseWhileQuestion](const UniValue& params) {
        RefuseWhileQuestion();
        const auto edge{ParseWindowEdge(ParamStr(params, "edge"))};
        if (!edge) throw HostError(BRIDGE_INVALID_REQUEST, "edge: top, bottom, left, right, top-left, top-right, bottom-left or bottom-right");
        m_window->StartResize(*edge);
        return UniValue{true};
    }, Bridge::Thread::UI);
    b.RegisterHost("window.setTitleBar", [this](const UniValue& params) {
        const UniValue mode{Param(params, "mode")};
        if (!mode.isStr() || (mode.get_str() != "integrated" && mode.get_str() != "system")) throw HostError(BRIDGE_INVALID_REQUEST, "mode: integrated or system");
        m_config->SetValue("titleBar", mode);
        // At once where the platform can; otherwise at the next start (window.state says so).
        if (m_window->SupportsFrameless()) m_window->SetFrameless(ParseTitleBarMode(mode) == TitleBarMode::INTEGRATED);
        return WindowState();
    }, Bridge::Thread::UI);
    // Files: the user chooses them in a file chooser of the desktop; the page reads and writes only those.
    b.RegisterHost("file.choose", [this](const UniValue& params) {
        Window::FileDialog dialog;
        dialog.save = Param(params, "save").isBool() && Param(params, "save").get_bool();
        dialog.folder = Param(params, "folder").isBool() && Param(params, "folder").get_bool();
        dialog.title = Param(params, "title").isStr() ? Param(params, "title").get_str() : (dialog.save ? "Save" : "Open");
        if (Param(params, "name").isStr()) dialog.suggested_name = Param(params, "name").get_str();
        if (const UniValue& filters{Param(params, "filters")}; filters.isArray()) {
            for (const UniValue& f : filters.getValues()) {
                std::vector<std::string> patterns;
                if (f.find_value("patterns").isArray()) {
                    for (const UniValue& pattern : f.find_value("patterns").getValues()) {
                        if (pattern.isStr()) patterns.push_back(pattern.get_str());
                    }
                }
                dialog.filters.emplace_back(f.find_value("name").isStr() ? f.find_value("name").get_str() : "Files", std::move(patterns));
            }
        }
        const auto chosen{Choose(dialog)};
        return chosen ? UniValue{*chosen} : UniValue{};
    }, Bridge::Thread::LONG);
    b.RegisterHost("file.read", [this](const UniValue& params) {
        const std::string path{ParamStr(params, "path")};
        if (!TakeChosenFile(path, /*write=*/false)) throw HostError(BRIDGE_INVALID_REQUEST, "Only a file chosen by the user can be read, once, soon after");
        std::ifstream file{static_cast<const std::filesystem::path&>(fs::PathFromString(path)), std::ios::binary};
        if (!file) throw HostError(strprintf("Cannot open %s", path));
        file.seekg(0, std::ios::end);
        const auto size{static_cast<int64_t>(file.tellg())};
        // The largest file the page reads is a PSBT (MAX_FILE_SIZE_PSBT).
        if (size < 0 || size >= 100'000'000) throw HostError("The file must be smaller than 100 MB");
        file.seekg(0);
        std::string data(static_cast<size_t>(size), '\0');
        file.read(data.data(), size);
        UniValue out{UniValue::VOBJ};
        out.pushKV("path", path);
        out.pushKV("size", size);
        out.pushKV("base64", EncodeBase64(data));
        return out;
    });
    b.RegisterHost("file.write", [this](const UniValue& params) {
        const std::string path{ParamStr(params, "path")};
        if (!TakeChosenFile(path, /*write=*/true)) throw HostError(BRIDGE_INVALID_REQUEST, "Only a file chosen by the user can be written, once, soon after");
        std::string data;
        if (Param(params, "base64").isStr()) {
            auto bytes{DecodeBase64(Param(params, "base64").get_str())};
            if (!bytes) throw HostError(BRIDGE_INVALID_REQUEST, "base64: not base64");
            data.assign(bytes->begin(), bytes->end());
        } else {
            data = ParamStr(params, "text");
        }
        std::ofstream file{static_cast<const std::filesystem::path&>(fs::PathFromString(path)), std::ios::binary | std::ios::trunc};
        if (!file || !file.write(data.data(), data.size()) || !(file.flush())) throw HostError(strprintf("There was an error trying to save to %s.", path));
        return UniValue{path};
    });

    // The clipboard, only right after the user asked for it (audit 7 #5): a key or a click of the user in the main window,
    // as the platform saw it (never an event the page makes), at most one read and one write for each, reads rate-limited.
    // A page cannot watch the clipboard from the tray, nor put an address of its own there while the user is elsewhere.
    b.RegisterHost("clipboard.write", [this](const UniValue& params) {
        const std::string text{ParamStr(params, "text")};
        if (text.size() > MAX_CLIPBOARD_BYTES) throw HostError(BRIDGE_INVALID_REQUEST, "The text is too long for the clipboard");
        const auto input{m_window->LastUserInput()};
        {
            LOCK(m_mutex);
            if (!input || input->age_ms > CLIPBOARD_WRITE_MS || input->serial == m_clipboard_write_gesture) {
                throw HostError(BRIDGE_CANCELLED, "The clipboard is written only right after you ask for it, with a click or a key");
            }
            m_clipboard_write_gesture = input->serial;
        }
        m_window->SetClipboard(text);
        return UniValue{true};
    }, Bridge::Thread::UI);
    b.RegisterHost("clipboard.read", [this](const UniValue&) {
        const auto input{m_window->LastUserInput()};
        {
            LOCK(m_mutex);
            const int64_t now{SteadySeconds()};
            while (!m_clipboard_reads.empty() && m_clipboard_reads.front() < now - 60) m_clipboard_reads.pop_front();
            if (!input || input->age_ms > CLIPBOARD_READ_MS || input->serial == m_clipboard_read_gesture || m_clipboard_reads.size() >= MAX_CLIPBOARD_READS) {
                throw HostError(BRIDGE_CANCELLED, "The clipboard is read only right after you ask for it, with a click or a key");
            }
            m_clipboard_read_gesture = input->serial;
            m_clipboard_reads.push_back(now);
        }
        const auto text{m_window->GetClipboard()};
        return text ? UniValue{*text} : UniValue{};
    }, Bridge::Thread::UI);

    // The node window and the tools of chains-qt.
    m_node_api = std::make_unique<NodeApi>(need_node, [this](const std::string& path) {
        return TakeChosenFile(path, /*write=*/false);
    }, [this](const Window::ConfirmDialog& dialog) { return Confirm(dialog); });
    m_node_api->Register(b);

    // The sidechains this app runs (Settings → Sidechains, the chain switcher, their pages: doc/unified-app.md). The old
    // Nodes tab (start, stop and download by hand: SidechainNodes) is gone; its download and check of a release stays
    // in sidechainnodes.cpp for download-on-enable (phase 4).
    RegisterSidechainMethods(b);
    RegisterSeedMethods(b);
    RegisterModuleMethods(b);

    // The options of the node (OptionsModel), in settings.json; the flag of a restart in the preferences.
    const auto options{[this, need_node]() {
        return NodeOptions{need_node(), m_args, [this](std::optional<bool> set) {
            if (set) {
                UniValue change{UniValue::VOBJ};
                change.pushKV("restartRequired", *set);
                std::string network;
                {
                    LOCK(m_mutex);
                    network = m_network;
                }
                m_config->UpdatePrefs(network, change);
                return *set;
            }
            return PrefFlag("restartRequired", false);
        }, [this](const std::string& setting) {
            interfaces::Node* node;
            {
                LOCK(m_mutex);
                node = m_node.get();
            }
            if (node) m_options_journal->Record(*node, setting, PrefFlag("restartRequired", false));
        }, [this](const std::string& title, const std::string& current) -> std::optional<std::string> {
            // A program the node runs: chosen by the user in the file chooser of the desktop, then confirmed
            // in a dialog of the desktop. Never a text of the page.
            Window::FileDialog dialog;
            dialog.title = title;
            if (!current.empty()) dialog.start_dir = fs::PathToString(fs::PathFromString(current).parent_path());
            const auto path{Choose(dialog)};
            if (!path) return std::nullopt;
            const std::string question{strprintf("%s\n\nProgram: %s\n\nThe node will run this program with your rights whenever it needs it. "
                                                 "Accept only if you know what it is.", title, *path)};
            if (!Confirm({.title = title, .message = question, .accept = "Use this program"})) return std::nullopt;
            return path;
        }, [this](const std::string& title, const std::string& message, const std::string& accept) {
            // A proxy, the Tor proxy or the RPC server turned on: asked in a dialog of the desktop.
            return Confirm({.title = title, .message = message, .accept = accept});
        }};
    }};
    const auto with_pending{[this](UniValue out) {
        // The changes waiting for Apply (the restart flag they may have set is not one).
        out.pushKV("pending", static_cast<int64_t>(m_options_journal->Pending()));
        return out;
    }};
    b.RegisterHost("options.get", [options, with_pending](const UniValue&) { return with_pending(options().Get()); });
    b.RegisterHost("options.apply", [this, need_node, options, with_pending](const UniValue&) {
        need_node();
        m_options_journal->Commit();
        return with_pending(options().Get());
    });
    b.RegisterHost("options.cancel", [this, need_node, options, with_pending](const UniValue&) {
        UndoOptions(need_node(), "cancelled");
        return with_pending(options().Get());
    });
    b.RegisterHost("options.set", [options, with_pending](const UniValue& params) {
        // A long call: some changes wait for the answer of a confirmation of the desktop.
        try {
            return with_pending(options().Set(ParamStr(params, "name"), Param(params, "value")));
        } catch (const std::runtime_error& e) {
            throw HostError(BRIDGE_INVALID_REQUEST, e.what());
        }
    }, Bridge::Thread::LONG);
    b.RegisterHost("options.chooseProgram", [options, with_pending](const UniValue& params) {
        // A setting that names a program (the external signer): through the file chooser and a confirmation
        // of the desktop. Null when the user cancelled.
        try {
            auto out{options().ChooseProgram(ParamStr(params, "name"))};
            return out.isNull() ? out : with_pending(std::move(out));
        } catch (const std::runtime_error& e) {
            throw HostError(BRIDGE_INVALID_REQUEST, e.what());
        }
    }, Bridge::Thread::LONG);
    b.RegisterHost("options.validProxy", [](const UniValue& params) { return UniValue{NodeOptions::ValidProxyAddress(ParamStr(params, "address"))}; });
    b.RegisterHost("options.reset", [this, need_node](const UniValue&) {
        // As OptionsModel::Reset: settings.json backed up and reset, the settings of the app backed up and
        // cleared (but for the data directory), the intro shown at the next start; then the app shuts down.
        interfaces::Node& node{need_node()};
        // Not while a change of the options waits for a confirmation of the desktop.
        std::unique_lock changes{NodeOptions::ChangesMutex()};
        node.resetSettings();
        std::string network;
        {
            LOCK(m_mutex);
            network = m_network;
        }
        try {
            const fs::path backup{m_args.GetDataDirNet() / "guisettings.json.bak"};
            std::ofstream file{backup.std_path(), std::ios::trunc};
            file << m_config->Prefs(network).write(2) << "\n";
        } catch (const std::exception& e) {
            LogWarning("webui: cannot back up the settings of the app: %s", e.what());
        }
        m_config->ResetPrefs(network);
        m_options_journal->Commit();
        m_config->SetResetPending(true);
        if (NodeOptions::StartAtLogin(m_args)) NodeOptions::SetStartAtLogin(m_args, false);
        changes.unlock();
        RequestShutdown();
        return UniValue{true};
    });

    b.RegisterHost("node.paths", [this, need_node](const UniValue&) {
        need_node();
        UniValue out{UniValue::VOBJ};
        out.pushKV("datadir", fs::PathToString(m_args.GetDataDirNet()));
        out.pushKV("blocksdir", fs::PathToString(m_args.GetBlocksDirPath()));
        out.pushKV("debuglog", fs::PathToString(LogInstance().m_file_path));
        out.pushKV("config", fs::PathToString(m_args.GetConfigFilePath()));
        return out;
    });
    // Open the debug log or the configuration file with the application of the desktop (GUIUtil::openDebugLogfile,
    // openBitcoinConf), or a web page of a third-party explorer (http and https only).
    b.RegisterHost("app.open", [this, need_node](const UniValue& params) {
        const std::string what{ParamStr(params, "what")};
        std::string uri;
        if (what == "debuglog" || what == "config") {
            need_node();
            fs::path path{what == "debuglog" ? LogInstance().m_file_path : m_args.GetConfigFilePath()};
            if (what == "config") {
                // The file is made if it is not there.
                std::ofstream file{path.std_path(), std::ios_base::app};
                if (!file.good()) throw HostError("The configuration file could not be opened.");
            } else if (!fs::exists(path)) {
                throw HostError("There is no debug log file yet.");
            }
            uri = "file://";
            for (const auto& part : util::SplitString(fs::PathToString(fs::absolute(path)), '/')) {
                if (!part.empty()) uri += "/" + PercentEncode(part);
            }
        } else if (what == "url") {
            // The page names the address: one that is not a link of the app's own is opened on the user's
            // word, given to the desktop, with its host first (a page could otherwise send what it read away).
            uri = ParamStr(params, "url");
            std::optional<Window::ConfirmDialog> dialog;
            try {
                dialog = guard::OpenUrlConfirmation(uri);
            } catch (const std::invalid_argument& e) {
                throw HostError(BRIDGE_INVALID_REQUEST, e.what());
            }
            if (dialog && !Confirm(*dialog)) return UniValue{false};
        } else {
            throw HostError(BRIDGE_INVALID_REQUEST, "what: debuglog, config or url");
        }
        auto done{std::make_shared<std::promise<bool>>()};
        m_window->Post([this, uri, done] { done->set_value(m_window->OpenExternal(uri)); });
        auto future{done->get_future()};
        if (future.wait_for(10s) != std::future_status::ready) return UniValue{false};
        const bool ok{future.get()};
        if (!ok && what == "config") throw HostError("The configuration file could not be opened.");
        return UniValue{ok};
    }, Bridge::Thread::LONG);

    // Notifications of the desktop (Notificator) and the tray.
    b.RegisterHost("app.notify", [this](const UniValue& params) {
        // One line of title and a short text, with what is not shown as itself written out (audit 7 #16; the desktop
        // also gets the text escaped, never as markup).
        const std::string title{guard::OneLine(ParamStr(params, "title"), 80)};
        const std::string body{Param(params, "body").isStr() ? dialog::Shown(Param(params, "body").get_str(), 300) : ""};
        const std::string level{Param(params, "level").isStr() ? Param(params, "level").get_str() : "info"};
        {
            LOCK(m_mutex);
            UniValue sent{UniValue::VOBJ};
            sent.pushKV("title", title);
            sent.pushKV("body", body);
            sent.pushKV("level", level);
            m_notifications.push_back(std::move(sent));
            if (m_notifications.size() > 100) m_notifications.erase(m_notifications.begin());
        }
        m_window->Post([this, title, body, level] { m_window->Notify(title, body, level); });
        return UniValue{true};
    });
    b.RegisterHost("tray.state", [this](const UniValue&) {
        UniValue out{UniValue::VOBJ};
        out.pushKV("available", m_window->TrayAvailable());
        out.pushKV("visible", PrefFlag("showTray", true));
        out.pushKV("windowVisible", m_window->IsVisible());
        return out;
    }, Bridge::Thread::UI);

#ifdef ENABLE_WALLET
    // The wallet functions (the wallet pages of chains-qt).
    m_wallet_api = std::make_unique<WalletApi>(need_node, [this](const std::string& path, bool write) {
        return TakeChosenFile(path, write);
    }, [this](const Window::ConfirmDialog& dialog) { return AskHost(dialog); });
    m_wallet_api->Register(b);
#endif

    // The RPC methods of the pages that pay, sign or change what the node votes for: decoded and confirmed on the desktop
    // first, the passphrase of a locked wallet typed there (confirm.h, audit 7 #1).
    confirm::KeysFn keys;
    confirm::FeeFn fee;
#ifdef ENABLE_WALLET
    keys = [this](const std::string& wallet, Window::ConfirmDialog dialog, bool always, bool fee_unknown, const std::function<UniValue()>& run) {
        return m_wallet_api->WithKeys(wallet, std::move(dialog), always, fee_unknown, run);
    };
    fee = [this](const std::string& wallet, int64_t vsize) { return m_wallet_api->FeeFor(wallet, vsize); };
#endif
    m_confirmed_rpc = std::make_unique<confirm::ConfirmedRpc>(need_node, [this](const Window::ConfirmDialog& dialog) { return AskHost(dialog); }, keys, fee);
    m_confirmed_rpc->Register(b);

#ifdef WEBUI_TEST_HOOKS
    if (!m_test_script.empty()) {
        // The hooks of the tests of the app: only in a build for the tests (WEBUI_TEST_HOOKS), and only
        // with the debug option -webuitestscript of the command line.
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
            const fs::path dir{fs::PathFromString(m_test_shots)};
            if (dir.empty()) throw HostError("No -webuitestshots folder");
            fs::create_directories(dir);
            const std::string path{fs::PathToString(dir / fs::PathFromString(name + ".png"))};
            auto done{std::make_shared<std::promise<bool>>()};
            m_window->Post([this, path, done] { m_window->Snapshot(path, [done](bool ok) { done->set_value(ok); }); });
            auto future{done->get_future()};
            if (future.wait_for(30s) != std::future_status::ready || !future.get()) throw HostError("The snapshot failed");
            return UniValue{path};
        }, Bridge::Thread::LONG);
        b.RegisterHost("test.fileAnswer", [this](const UniValue& params) {
            // The answer of the next file chooser: a path, or null for "cancelled".
            LOCK(m_mutex);
            m_test_files.push_back(Param(params, "path"));
            return UniValue{true};
        });
        b.RegisterHost("test.confirmAnswer", [this](const UniValue& params) {
            // The next confirmation of the host (App::Confirm): {yes} answers it here (true for accepted); {show: true}
            // shows it in the dialog window, {fallback: true} in the dialog of the system, as when that window cannot be
            // made; the test then answers it there (test.dialog).
            // {yes: true, passphrase} types the passphrase in the field of a question that has one; {clear: true} drops the
            // answers not used (a question that may or may not come).
            if (Param(params, "clear").isTrue()) {
                LOCK(m_mutex);
                m_test_confirms.clear();
                return UniValue{true};
            }
            // {always: true, passphrase?} answers yes to every question asked when none is queued (a page script that spends
            // often, and reads what was asked with test.confirmed); {always: false} stops it.
            if (Param(params, "always").isBool()) {
                LOCK(m_mutex);
                if (Param(params, "always").get_bool()) {
                    m_test_confirm_always = TestAnswer{TestConfirm::YES, Param(params, "passphrase").isStr() ? Param(params, "passphrase").get_str() : std::string{}};
                } else {
                    m_test_confirm_always.reset();
                }
                return UniValue{true};
            }
            const UniValue& yes{Param(params, "yes")};
            const bool show{Param(params, "show").isTrue()};
            const bool fallback{Param(params, "fallback").isTrue()};
            if (!yes.isBool() && !show && !fallback) throw HostError(BRIDGE_INVALID_REQUEST, "yes: true or false; or show: true; or fallback: true");
            const UniValue& passphrase{Param(params, "passphrase")};
            // {yes: true, fields: {id: text}} types in the fields of a question that has some (dialog::Field) and ticks its box.
            std::map<std::string, std::string> fields;
            if (Param(params, "fields").isObject()) {
                for (const std::string& key : Param(params, "fields").getKeys()) {
                    if (Param(params, "fields").find_value(key).isStr()) fields[key] = Param(params, "fields").find_value(key).get_str();
                }
            }
            LOCK(m_mutex);
            m_test_confirms.push_back({fallback ? TestConfirm::FALLBACK : show ? TestConfirm::SHOW : yes.get_bool() ? TestConfirm::YES : TestConfirm::NO,
                                       passphrase.isStr() ? passphrase.get_str() : std::string{}, std::move(fields)});
            return UniValue{true};
        });
        b.RegisterHost("test.questionLimits", [this](const UniValue&) {
            // The limits of the questions start again (AskHost): the tests of the dialog window decline a question
            // many times, on purpose; what they found is said first.
            LOCK(m_mutex);
            UniValue out{UniValue::VOBJ};
            for (const auto& [kind, k] : m_question_kinds) {
                if (k.refused) out.pushKV(kind, "refused");
            }
            m_question_kinds.clear();
            return out;
        });
        b.RegisterHost("test.gesture", [this](const UniValue&) {
            // As if the user pressed a key in the main window now (the clipboard takes calls right after one).
            m_window->NoteUserInput();
            return UniValue{true};
        }, Bridge::Thread::UI);
        b.RegisterHost("test.dialog", [this](const UniValue& params) {
            // The dialog shown now (a confirmation the test let show): {action: "kind"}, {action: "eval", script},
            // {action: "snapshot", name} (in -webuitestshots), {action: "native", arg: "state", "accept" or "cancel"}.
            const std::string action{ParamStr(params, "action")};
            std::string arg;
            if (action == "eval") {
                arg = ParamStr(params, "script");
            } else if (action == "snapshot") {
                const std::string name{ParamStr(params, "name")};
                if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") != std::string::npos) {
                    throw HostError(BRIDGE_INVALID_REQUEST, "name: letters, digits, '.', '_' and '-'");
                }
                const fs::path dir{fs::PathFromString(m_test_shots)};
                if (dir.empty()) throw HostError("No -webuitestshots folder");
                fs::create_directories(dir);
                arg = fs::PathToString(dir / fs::PathFromString(name + ".png"));
            } else if (action == "native") {
                arg = ParamStr(params, "arg");
            } else if (action != "kind") {
                throw HostError(BRIDGE_INVALID_REQUEST, "action: kind, eval, snapshot or native");
            }
            auto done{std::make_shared<std::promise<std::optional<std::string>>>()};
            m_window->Post([this, action, arg, done] { m_window->TestDialog(action, arg, [done](std::optional<std::string> out) { done->set_value(std::move(out)); }); });
            auto future{done->get_future()};
            if (future.wait_for(30s) != std::future_status::ready) throw HostError("The dialog did not answer");
            const auto out{future.get()};
            if (!out) return UniValue{};
            UniValue value;
            if (!value.read(*out)) return UniValue{*out};
            if (action == "snapshot" && value.isTrue()) return UniValue{arg};
            return value;
        }, Bridge::Thread::LONG);
        b.RegisterHost("test.refused", [this](const UniValue&) {
            // The RPC methods the policy refused to the pages (a page that calls one fails in a release).
            UniValue out{UniValue::VARR};
            for (const std::string& m : m_bridge->Refused()) out.push_back(m);
            return out;
        });
        b.RegisterHost("test.confirmed", [this](const UniValue&) {
            // What the confirmations answered by the tests asked, in order.
            LOCK(m_mutex);
            UniValue out{UniValue::VARR};
            for (const auto& d : m_test_confirmed) {
                UniValue entry{UniValue::VOBJ};
                entry.pushKV("title", d.title);
                entry.pushKV("message", d.message);
                entry.pushKV("details", d.details);
                entry.pushKV("accept", d.accept);
                entry.pushKV("level", std::string{dialog::LevelName(d.level)});
                entry.pushKV("passphrase", d.passphrase);
                if (!d.error.empty()) entry.pushKV("error", d.error);
                if (!d.chain.empty()) {
                    entry.pushKV("chain", d.chain);
                    entry.pushKV("chainLogo", !d.chain_logo.empty());
                }
                UniValue rows{UniValue::VARR};
                for (const auto& row : d.rows) {
                    UniValue r{UniValue::VOBJ};
                    r.pushKV("label", row.label);
                    r.pushKV("value", row.value);
                    r.pushKV("kind", std::string{dialog::RowKindName(row.kind)});
                    rows.push_back(std::move(r));
                }
                entry.pushKV("rows", std::move(rows));
                // A backup phrase is never written here: how many words were shown, the fields asked and the box.
                if (d.words) entry.pushKV("words", static_cast<uint64_t>(d.words->size()));
                if (!d.fields.empty()) {
                    UniValue fields{UniValue::VARR};
                    for (const auto& f : d.fields) {
                        UniValue field{UniValue::VOBJ};
                        field.pushKV("id", f.id);
                        field.pushKV("kind", std::string{dialog::FieldKindName(f.kind)});
                        field.pushKV("label", f.label);
                        if (!f.error.empty()) field.pushKV("error", f.error);
                        fields.push_back(std::move(field));
                    }
                    entry.pushKV("fields", std::move(fields));
                }
                if (!d.checkbox.empty()) entry.pushKV("checkbox", d.checkbox);
                out.push_back(std::move(entry));
            }
            return out;
        });
        b.RegisterHost("test.notifications", [this](const UniValue&) {
            LOCK(m_mutex);
            UniValue out{UniValue::VARR};
            for (const auto& n : m_notifications) out.push_back(n);
            return out;
        });
        // What the user does with the window and the tray, done by the tests.
        b.RegisterHost("test.window", [this](const UniValue& params) {
            const std::string action{ParamStr(params, "action")};
            if (action == "trayMenu") m_window->Post([this, id = ParamStr(params, "id")] { OnTrayMenu(id); });
            else if (action == "trayActivate") m_window->Post([this] { if (m_window->IsVisible()) m_window->Hide(); else m_window->Present(); });
            else if (action == "minimize") m_window->Post([this] { m_window->Minimize(); });
            else if (action == "close") m_window->Post([this] { CloseRequested(); });
            else if (action == "present") m_window->Post([this] { m_window->Present(); });
            return UniValue{true};
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
#endif
}

std::shared_ptr<SidechainSupervisor> App::Supervisor()
{
    LOCK(m_mutex);
    if (!m_supervisor) throw HostError(BRIDGE_NOT_READY, "The node is not running");
    return m_supervisor;
}

std::shared_ptr<ModuleInstaller> App::Installer()
{
    LOCK(m_mutex);
    if (!m_installer) throw HostError(BRIDGE_NOT_READY, "The node is not running");
    return m_installer;
}

void App::EnsureRpcServer()
{
    if (IsRPCRunning()) return;
    interfaces::Node* node;
    {
        LOCK(m_mutex);
        if (!m_node_started || !m_node || m_phase != "ready") throw HostError(BRIDGE_NOT_READY, "The node is not running");
        node = m_node.get();
    }
    // Sidechains follow this node over RPC: its server, on this computer only unless the user set otherwise.
    if (!node->startRpcServer()) throw HostError(strprintf("The private connection of the sidechains to Chains could not be opened (is port %d in use?). See the log of the node.", m_args.GetIntArg("-rpcport", BaseParams().RPCPort())));
}

void App::StartSidechains()
{
    // The single seed (doc/unified-app.md §23), made first: the supervisor asks it which wallet a sidechain's node loads.
    SeedKeeper::Hooks seed_hooks;
    seed_hooks.file = m_args.GetDataDirNet() / "seed.dat";
    seed_hooks.chain = m_args.GetChainType();
    seed_hooks.test_chain = Params().IsTestChain();
    seed_hooks.product = CLIENT_NAME;
    seed_hooks.node_ready = [this] {
        LOCK(m_mutex);
        return m_node_started && m_node && m_phase == "ready";
    };
    seed_hooks.node_rpc = [this](const std::string& method, const UniValue& params, const std::string& wallet) -> UniValue {
        interfaces::Node* node;
        {
            LOCK(m_mutex);
            if (!m_node_started || !m_node) throw SidechainSupervisor::RpcError(BRIDGE_NOT_READY, "The node is not running");
            node = m_node.get();
        }
        try {
            return node->executeRpc(method, params, wallet.empty() ? std::string{"/"} : WalletUri(wallet));
        } catch (const UniValue& error) {
            const UniValue& code{error.find_value("code")};
            const UniValue& message{error.find_value("message")};
            throw SidechainSupervisor::RpcError(code.isNum() ? code.getInt<int>() : -1, message.isStr() ? message.get_str() : std::string{"the node refused it"});
        }
    };
    const auto running_supervisor{[this] {
        LOCK(m_mutex);
        return m_supervisor;
    }};
    seed_hooks.side_rpc = [running_supervisor](const std::string& stem, const std::string& method, const UniValue& params, const std::string& wallet, std::chrono::milliseconds timeout) {
        const auto s{running_supervisor()};
        if (!s) throw SidechainSupervisor::RpcError(BRIDGE_NOT_READY, "The sidechains are not running");
        return s->SetupRpc(stem, method, params, wallet, timeout);
    };
    seed_hooks.side_running = [running_supervisor](const std::string& stem) {
        const auto s{running_supervisor()};
        if (!s) return false;
        const auto status{s->GetStatus(stem)};
        return status.state == SidechainSupervisor::State::RUNNING && status.rpc_ready;
    };
    const auto chains_of{[running_supervisor](bool separate) {
        std::vector<std::string> out;
        const auto s{running_supervisor()};
        if (!s) return out;
        for (const SidechainSupervisor::Spec& spec : SidechainSupervisor::Catalog()) {
            if (s->Enabled(spec.stem) && (s->WalletMode(spec.stem) == "separate") == separate) out.push_back(spec.stem);
        }
        return out;
    }};
    seed_hooks.shared_chains = [chains_of] { return chains_of(false); };
    seed_hooks.separate_chains = [chains_of] { return chains_of(true); };
    seed_hooks.name = [](const std::string& stem) {
        const SidechainSupervisor::Spec* spec{SidechainSupervisor::FindSpec(stem)};
        return spec ? spec->name : stem;
    };
    seed_hooks.ask = [this](const Window::ConfirmDialog& dialog) { return AskHost(dialog); };
    const std::string seed_key{"seed:" + ChainTypeToString(m_args.GetChainType())};
    seed_hooks.load = [this, seed_key] {
        UniValue value{m_config->Value(seed_key)};
        return value.isObject() ? value : UniValue{UniValue::VOBJ};
    };
    seed_hooks.store = [this, seed_key](const UniValue& value) { m_config->SetValue(seed_key, value); };
    seed_hooks.changed = [this] {
        if (m_hub) m_hub->Emit("seed", UniValue{UniValue::VOBJ}, "seed");
    };
    auto keeper{std::make_shared<SeedKeeper>(std::move(seed_hooks))};
    {
        LOCK(m_mutex);
        m_seed = keeper;
    }

    SidechainSupervisor::Options options;
    options.seed_wallet = [weak = std::weak_ptr<SeedKeeper>{keeper}](const std::string& stem) -> std::optional<std::string> {
        const auto k{weak.lock()};
        return k ? k->WalletOf(stem) : std::nullopt;
    };
    options.chains_datadir = m_args.GetDataDirBase();
    options.chain = m_args.GetChainType();
    options.mainchain_rpc_port = m_args.GetIntArg("-rpcport", BaseParams().RPCPort());
    options.mainchain_rpc_user = SIDECHAIN_RPC_USER;
    options.mainchain_rpc_password = m_sidechain_rpc_password;
    for (const std::string& dir : m_args.GetArgs("-sidechainbindir")) options.bin_dirs.push_back(fs::PathFromString(dir));
#ifdef __linux__
    // The programs of the sidechains come next to this app (phase 1: bundled).
    {
        std::error_code ec;
        const auto self{std::filesystem::read_symlink("/proc/self/exe", ec)};
        if (!ec) options.bin_dirs.push_back(fs::path{self.parent_path()});
    }
#endif
    options.port_base = m_args.GetIntArg("-sidechainportbase", 0);
    options.extra_args = m_args.GetArgs("-sidechainarg");
    // The programs downloaded from the sidechains' releases (doc/unified-app.md, downloads): before the bundled ones.
    ModuleInstaller::Options module_options;
    module_options.bin_root = m_args.GetDataDirBase() / "sidechains" / "bin";
    std::optional<fs::path> cacert;
    const bool test_overrides{m_args.IsArgSet("-sidechainreleaseapi") || m_args.IsArgSet("-sidechaindownloadcacert") || m_args.IsArgSet("-sidechainreleasekey")};
    if (test_overrides && m_args.GetChainType() != ChainType::REGTEST) {
        LogWarning("webui: -sidechainreleaseapi, -sidechaindownloadcacert and -sidechainreleasekey are for the tests, on regtest only: ignored");
    } else if (test_overrides) {
        std::string mirror{m_args.GetArg("-sidechainreleaseapi", "")};
        while (mirror.ends_with('/')) mirror.pop_back();
        if (!mirror.empty()) {
            if (!mirror.starts_with("https://")) {
                LogWarning("webui: -sidechainreleaseapi must be https://: ignored");
            } else {
                module_options.api_base = mirror;
                module_options.download_base = mirror;
            }
        }
        if (m_args.IsArgSet("-sidechaindownloadcacert")) cacert = fs::PathFromString(m_args.GetArg("-sidechaindownloadcacert", ""));
        module_options.extra_keys = m_args.GetArgs("-sidechainreleasekey");
    }
    module_options.signatures = m_args.GetArg("-sidechainsignatures", "auto") == "require" ? "require" : "auto";
    module_options.fetcher = ModuleInstaller::CurlFetcher(cacert);
    auto installer{std::make_shared<ModuleInstaller>(std::move(module_options), [this](const std::string& stem) {
        UniValue data{UniValue::VOBJ};
        data.pushKV("stem", stem);
        if (m_hub) m_hub->Emit("modules", std::move(data), "modules:" + stem);
    })};
    options.downloaded = [weak = std::weak_ptr<ModuleInstaller>{installer}](const std::string& stem) -> std::optional<fs::path> {
        const auto installer{weak.lock()};
        return installer ? installer->CurrentDir(stem) : std::nullopt;
    };
    const std::string key{"sidechains:" + ChainTypeToString(m_args.GetChainType())};
    auto supervisor{std::make_shared<SidechainSupervisor>(
        std::move(options),
        [this, key] {
            UniValue value{m_config->Value(key)};
            return value.isObject() ? value : UniValue{UniValue::VOBJ};
        },
        [this, key](const UniValue& value) { m_config->SetValue(key, value); },
        [this, weak_keeper = std::weak_ptr<SeedKeeper>{keeper}](const std::string& stem) {
            UniValue data{UniValue::VOBJ};
            data.pushKV("stem", stem);
            if (m_hub) m_hub->Emit("sidechains", std::move(data), "sidechains:" + stem);
            // A sidechain whose wallet is the backup phrase's gets its keys as soon as its node answers (when unlocked).
            if (const auto k{weak_keeper.lock()}) k->ChainReady(stem);
        })};
    SidechainPages::Hooks hooks;
    hooks.rpc = [supervisor](const std::string& stem, const std::string& method, const UniValue& params, std::chrono::milliseconds timeout) {
        return supervisor->Rpc(stem, method, params, {}, timeout);
    };
    hooks.running = [supervisor](const std::string& stem) {
        const auto status{supervisor->GetStatus(stem)};
        return status.state == SidechainSupervisor::State::RUNNING && status.rpc_ready;
    };
    hooks.name = [](const std::string& stem) {
        const SidechainSupervisor::Spec* spec{SidechainSupervisor::FindSpec(stem)};
        return spec ? spec->name : stem;
    };
    hooks.ask = [this](const Window::ConfirmDialog& dialog) { return AskHost(dialog); };
    hooks.hub = [this](const std::string& method, const UniValue& params) { return RunHost(method, params); };
    hooks.events = [this](const std::string& stem, const std::string& batch, bool reset) {
        UniValue data{UniValue::VOBJ};
        data.pushKV("stem", stem);
        data.pushKV("batch", batch);
        data.pushKV("reset", reset);
        if (m_hub) m_hub->Emit("pack.events", std::move(data));
    };
    hooks.deposit = [this](const std::string& stem, const UniValue& params) { return PackDeposit(stem, params); };
    hooks.pack_prefs = [this](const std::string& stem) { return m_config->Value("packPrefs:" + stem); };
    hooks.set_pack_prefs = [this](const std::string& stem, const UniValue& prefs) { m_config->SetValue("packPrefs:" + stem, prefs); };
    hooks.language = [this] {
        LOCK(m_mutex);
        return m_language;
    };
    hooks.network = [] { return confirm::NetworkName(); };
    hooks.passphrase = [weak = std::weak_ptr<SeedKeeper>{keeper}](const std::string& stem, const SecureString& typed) {
        const auto k{weak.lock()};
        return k ? k->PassphraseFor(stem, typed) : typed;
    };
    hooks.phrase_wallet = [weak = std::weak_ptr<SeedKeeper>{keeper}](const std::string& stem) {
        const auto k{weak.lock()};
        return k ? k->MadeWallet(stem) : std::string{};
    };
    auto pages{std::make_shared<SidechainPages>(std::move(hooks))};
    // The app's own pages over every chain (chain.*): reads by the supervisor, spends through the pages' host above.
    ChainAdapter::Hooks adapter_hooks;
    adapter_hooks.rpc = [supervisor](const std::string& stem, const std::string& method, const UniValue& params, const std::string& wallet) {
        return supervisor->Rpc(stem, method, params, wallet);
    };
    adapter_hooks.call = [weak = std::weak_ptr<SidechainPages>{pages}](const std::string& stem, const UniValue& request) {
        const auto p{weak.lock()};
        if (!p) throw HostError(BRIDGE_NOT_READY, "The app is stopping");
        return p->Call(stem, request);
    };
    adapter_hooks.call_rows = [weak = std::weak_ptr<SidechainPages>{pages}](const std::string& stem, const UniValue& request, const UniValue& rows) {
        const auto p{weak.lock()};
        if (!p) throw HostError(BRIDGE_NOT_READY, "The app is stopping");
        return p->Call(stem, request, rows);
    };
    adapter_hooks.running = [supervisor](const std::string& stem) {
        const auto status{supervisor->GetStatus(stem)};
        return status.state == SidechainSupervisor::State::RUNNING && status.rpc_ready;
    };
    adapter_hooks.name = [](const std::string& stem) {
        const SidechainSupervisor::Spec* spec{SidechainSupervisor::FindSpec(stem)};
        return spec ? spec->name : stem;
    };
    adapter_hooks.product = [] { return std::string{CLIENT_NAME}; };
    adapter_hooks.running_chains = [supervisor] {
        std::vector<std::string> out;
        for (const SidechainSupervisor::Spec& s : SidechainSupervisor::Catalog()) {
            const auto status{supervisor->GetStatus(s.stem)};
            if (status.state == SidechainSupervisor::State::RUNNING && status.rpc_ready) out.push_back(s.stem);
        }
        return out;
    };
    adapter_hooks.mainchain_address = [this](const std::string& address) { return MainchainAddress(address); };
    adapter_hooks.deposit = [this](const std::string& stem, const UniValue& params) { return PackDeposit(stem, params); };
    adapter_hooks.wallet_mode = [supervisor](const std::string& stem) { return supervisor->WalletMode(stem); };
    adapter_hooks.seed_wallet = [weak = std::weak_ptr<SeedKeeper>{keeper}](const std::string& stem) {
        const auto k{weak.lock()};
        return k ? k->MadeWallet(stem) : std::string{};
    };
    auto adapter{std::make_shared<ChainAdapter>(std::move(adapter_hooks))};
    {
        LOCK(m_mutex);
        m_supervisor = supervisor;
        m_pages = pages;
        m_adapter = adapter;
        m_installer = installer;
    }
    // Chains alone unless a sidechain is enabled: then (only then) the RPC server and the daemons.
    if (!supervisor->AnyEnabled()) return;
    try {
        EnsureRpcServer();
    } catch (const std::exception& e) {
        LogWarning("webui: sidechains not started: %s", e.what());
        return;
    }
    supervisor->StartEnabled();
}

std::shared_ptr<SeedKeeper> App::Seed()
{
    LOCK(m_mutex);
    if (!m_seed) throw HostError(BRIDGE_NOT_READY, "The node is not running");
    return m_seed;
}

void App::RegisterSeedMethods(Bridge& b)
{
    // The backup phrase (doc/unified-app.md §23). Its words and keys never come here: every one of these asks in the dialog's
    // window of the host, and answers what the page may know (made, cancelled, the state of each chain). Packs: refused
    // (SidechainPages::RouteOf, "seed.").
    const auto guarded{[](auto fn) {
        return [fn](const UniValue& params) -> UniValue {
            try {
                return fn(params);
            } catch (const HostError&) {
                throw;
            } catch (const std::runtime_error& e) {
                throw HostError(e.what());
            }
        };
    }};
    b.RegisterHost("seed.status", guarded([this](const UniValue&) { return Seed()->Status(); }));
    b.RegisterHost("seed.create", guarded([this](const UniValue& params) { return Seed()->Create(params); }), Bridge::Thread::LONG);
    b.RegisterHost("seed.restore", guarded([this](const UniValue& params) { return Seed()->Restore(params); }), Bridge::Thread::LONG);
    b.RegisterHost("seed.unlock", guarded([this](const UniValue& params) { return Seed()->Unlock(params); }), Bridge::Thread::LONG);
    b.RegisterHost("seed.lock", guarded([this](const UniValue&) {
        Seed()->Lock();
        return UniValue{true};
    }));
    b.RegisterHost("seed.addChain", guarded([this](const UniValue& params) { return Seed()->AddChain(ParamStr(params, "stem")); }), Bridge::Thread::LONG);
    b.RegisterHost("seed.show", guarded([this](const UniValue&) { return Seed()->Show(); }), Bridge::Thread::LONG);
    b.RegisterHost("seed.changePassphrase", guarded([this](const UniValue&) { return Seed()->ChangePassphrase(); }), Bridge::Thread::LONG);
    b.RegisterHost("seed.dismiss", guarded([this](const UniValue& params) {
        Seed()->Dismiss(ParamStr(params, "what"));
        return UniValue{true};
    }));
}

std::shared_ptr<SidechainPages> App::Pages()
{
    LOCK(m_mutex);
    if (!m_pages) throw HostError(BRIDGE_NOT_READY, "The node is not running");
    return m_pages;
}

std::shared_ptr<ChainAdapter> App::Adapter()
{
    LOCK(m_mutex);
    if (!m_adapter) throw HostError(BRIDGE_NOT_READY, "The node is not running");
    return m_adapter;
}

UniValue App::MainchainAddress(const std::string& address)
{
    interfaces::Node* node;
    std::string wallet;
    {
        LOCK(m_mutex);
        if (!m_node_started || !m_node || m_phase != "ready") throw HostError(BRIDGE_NOT_READY, "The node is not running");
        node = m_node.get();
        wallet = m_wallet_title;
    }
    UniValue args{UniValue::VARR};
    args.push_back(address);
    UniValue out{UniValue::VOBJ};
    const UniValue valid{node->executeRpc("validateaddress", args, "/")};
    out.pushKV("isvalid", valid.find_value("isvalid").isTrue());
    bool mine{false};
    if (valid.find_value("isvalid").isTrue()) {
        try {
            if (wallet.empty()) {
                const UniValue wallets{node->executeRpc("listwallets", UniValue{UniValue::VARR}, "/")};
                if (wallets.isArray() && !wallets.empty() && wallets[0].isStr()) wallet = wallets[0].get_str();
            }
            if (!wallet.empty()) mine = node->executeRpc("getaddressinfo", args, WalletUri(wallet)).find_value("ismine").isTrue();
        } catch (const std::exception&) {
        }
    }
    out.pushKV("ismine", mine);
    return out;
}

UniValue App::RunHost(const std::string& method, const UniValue& params)
{
    const Bridge::HostFn fn{m_bridge->FindHost(method)};
    if (!fn) throw HostError(BRIDGE_METHOD_NOT_FOUND, strprintf("No host method %s", method));
    // The clipboard is the window's: on its thread, as the bridge runs it for the app's own pages.
    if (!method.starts_with("clipboard.")) return fn(params);
    auto done{std::make_shared<std::promise<UniValue>>()};
    m_window->Post([fn, params, done] {
        try {
            done->set_value(fn(params));
        } catch (...) {
            done->set_exception(std::current_exception());
        }
    });
    return done->get_future().get();
}

UniValue App::PackDeposit(const std::string& stem, const UniValue& params)
{
    const SidechainSupervisor::Spec* spec{SidechainSupervisor::FindSpec(stem)};
    if (!spec) throw HostError(BRIDGE_INVALID_REQUEST, "No such sidechain");
    const std::string destination{util::TrimString(ParamStr(params, "destination"))};
    const std::string amount{util::TrimString(ParamStr(params, "amount"))};
    // A deposit address of this sidechain only: the pages of one sidechain do not pay into another.
    drivechain::DepositAddress parsed;
    if (drivechain::ParseDepositAddress(destination, parsed) != drivechain::DepositAddressKind::VALID) throw HostError(BRIDGE_INVALID_REQUEST, "This is not a deposit address: get one with \"New deposit address\".");
    if (parsed.slot != static_cast<uint32_t>(spec->slot)) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("This deposit address is for the sidechain in slot %u, not %s (slot %d).", parsed.slot, spec->name, spec->slot));
    std::string wallet;
    {
        LOCK(m_mutex);
        wallet = m_wallet_title;
    }
    if (wallet.empty()) {
        interfaces::Node* node;
        {
            LOCK(m_mutex);
            if (!m_node_started || !m_node || m_phase != "ready") throw HostError(BRIDGE_NOT_READY, "The node is not running");
            node = m_node.get();
        }
        const UniValue wallets{node->executeRpc("listwallets", UniValue{UniValue::VARR}, "/")};
        if (wallets.isArray() && !wallets.empty() && wallets[0].isStr()) wallet = wallets[0].get_str();
    }
    if (wallet.empty()) throw HostError(-18, strprintf("%s has no wallet loaded to pay the deposit from", CLIENT_NAME));
    // Coin control (doc/unified-app.md §20.7): only unspent coins of that wallet, refused here before anything is asked;
    // the node checks them again.
    const UniValue inputs{ChainAdapter::ParseInputs(Param(params, "inputs"))};
    if (!inputs.isNull()) {
        interfaces::Node* node;
        {
            LOCK(m_mutex);
            if (!m_node_started || !m_node || m_phase != "ready") throw HostError(BRIDGE_NOT_READY, "The node is not running");
            node = m_node.get();
        }
        UniValue range{UniValue::VARR};
        range.push_back(0);
        range.push_back(9999999);
        const UniValue coins{ChainAdapter::CoinRows(node->executeRpc("listunspent", range, WalletUri(wallet)))};
        const std::string missing{ChainAdapter::MissingCoin(inputs, coins)};
        if (!missing.empty()) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("%s is not an unspent coin of your %s wallet %s", missing, CLIENT_NAME, wallet));
    }
    // The slot of the address: Chains' own describer says the sidechain of that slot by its title, and a slot that is
    // not this sidechain's.
    UniValue args{UniValue::VARR};
    args.push_back(static_cast<int64_t>(parsed.slot));
    args.push_back(destination);
    args.push_back(amount);
    if (!inputs.isNull()) {
        args.push_back(UniValue{});
        args.push_back(inputs);
    }
    UniValue call{UniValue::VOBJ};
    call.pushKV("method", "createsidechaindeposit");
    call.pushKV("params", args);
    call.pushKV("wallet", wallet);
    return m_confirmed_rpc->Call(call);
}

void App::RegisterSidechainMethods(Bridge& b)
{
    const auto stem_of{[](const UniValue& params) {
        const std::string stem{ParamStr(params, "stem")};
        if (!SidechainSupervisor::FindSpec(stem)) throw HostError(BRIDGE_INVALID_REQUEST, "No such sidechain");
        return stem;
    }};
    // The errors of the supervisor are for the user, as they are.
    const auto guarded{[](auto fn) {
        return [fn](const UniValue& params) -> UniValue {
            try {
                return fn(params);
            } catch (const HostError&) {
                throw;
            } catch (const std::runtime_error& e) {
                throw HostError(e.what());
            }
        };
    }};
    b.RegisterHost("sidechains.list", guarded([this](const UniValue&) {
        const auto s{Supervisor()};
        UniValue out{UniValue::VOBJ};
        out.pushKV("network", ChainTypeToString(m_args.GetChainType()));
        out.pushKV("rpcServer", IsRPCRunning());
        out.pushKV("chainsDataDir", fs::PathToString(m_args.GetDataDirBase()));
        out.pushKV("chains", s->List());
        return out;
    }));
    b.RegisterHost("sidechains.enable", guarded([this, stem_of](const UniValue& params) {
        const std::string stem{stem_of(params)};
        const auto s{Supervisor()};
        std::optional<std::string> wallet;
        if (Param(params, "wallet").isStr()) wallet = Param(params, "wallet").get_str();
        EnsureRpcServer();
        s->Enable(stem, wallet);
        return UniValue{true};
    }), Bridge::Thread::LONG);
    b.RegisterHost("sidechains.disable", guarded([this, stem_of](const UniValue& params) {
        Supervisor()->Disable(stem_of(params));
        return UniValue{true};
    }), Bridge::Thread::LONG);
    b.RegisterHost("sidechains.chooseDataDir", guarded([this, stem_of](const UniValue& params) {
        const std::string stem{stem_of(params)};
        const auto s{Supervisor()};
        const SidechainSupervisor::Spec* spec{SidechainSupervisor::FindSpec(stem)};
        Window::FileDialog dialog;
        dialog.folder = true;
        dialog.title = strprintf("Where %s keeps its data", spec->name);
        dialog.start_dir = fs::PathToString(s->DataDir(stem).parent_path());
        const auto chosen{Choose(dialog)};
        if (!chosen) return UniValue{};
        {
            LOCK(m_mutex);
            m_chosen_sidechain_dirs[stem] = *chosen;
        }
        UniValue out{UniValue::VOBJ};
        out.pushKV("path", *chosen);
        out.pushKV("problem", s->CheckDataDir(stem, fs::PathFromString(*chosen)));
        std::error_code ec;
        const auto space{std::filesystem::space(fs::PathFromString(*chosen), ec)};
        if (!ec) out.pushKV("freeGB", static_cast<double>(space.available) / 1e9);
        return out;
    }), Bridge::Thread::LONG);
    b.RegisterHost("sidechains.setDataDir", guarded([this, stem_of](const UniValue& params) {
        const std::string stem{stem_of(params)};
        const auto s{Supervisor()};
        std::optional<fs::path> dir;
        if (Param(params, "dir").isStr()) {
            const std::string path{Param(params, "dir").get_str()};
            // Only a folder the user chose in the chooser of the desktop (or the default).
            LOCK(m_mutex);
            const auto it{m_chosen_sidechain_dirs.find(stem)};
            if (it == m_chosen_sidechain_dirs.end() || it->second != path) throw HostError(BRIDGE_INVALID_REQUEST, "Choose the folder first");
            dir = fs::PathFromString(path);
        }
        s->SetDataDir(stem, dir, Param(params, "move").isTrue());
        return UniValue{true};
    }), Bridge::Thread::LONG);
    b.RegisterHost("sidechains.deleteData", guarded([this, stem_of](const UniValue& params) {
        const std::string stem{stem_of(params)};
        const auto s{Supervisor()};
        const bool wallets{Param(params, "wallets").isTrue()};
        const SidechainSupervisor::Spec* spec{SidechainSupervisor::FindSpec(stem)};
        Window::ConfirmDialog dialog;
        dialog.title = strprintf("Delete the data of %s", spec->name);
        dialog.message = wallets ? strprintf("Everything %s keeps in %s goes, its wallets too: coins on %s whose keys are only there are lost for good.\n\nAccept only if you backed its wallet up, or it holds nothing.", spec->name, fs::PathToString(s->NetDataDir(stem)), spec->name)
                                 : strprintf("The blocks and the state of %s in %s go; its wallets stay. Turned on again, it downloads its chain anew.", spec->name, fs::PathToString(s->NetDataDir(stem)));
        dialog.accept = "Delete";
        if (!Confirm(dialog)) return UniValue{false};
        s->DeleteData(stem, wallets);
        return UniValue{true};
    }), Bridge::Thread::LONG);
    b.RegisterHost("sidechains.overview", guarded([this, stem_of](const UniValue& params) {
        const std::string stem{stem_of(params)};
        const auto s{Supervisor()};
        int height{-1};
        {
            LOCK(m_mutex);
            if (m_node) height = m_node->getNumBlocks();
        }
        return s->Overview(stem, height);
    }));
    b.RegisterHost("sidechains.rpc", guarded([this, stem_of](const UniValue& params) {
        const std::string stem{stem_of(params)};
        const std::string method{ParamStr(params, "method")};
        // Phase 1: the pages read a sidechain, they do not spend on it (doc/unified-app.md, security).
        if (!SidechainSupervisor::IsReadOnlyMethod(method)) throw HostError(BRIDGE_METHOD_NOT_FOUND, strprintf("%s is not offered for sidechains yet", method));
        const UniValue& args{Param(params, "params")};
        const std::string wallet{Param(params, "wallet").isStr() ? Param(params, "wallet").get_str() : std::string{}};
        return Supervisor()->Rpc(stem, method, args.isArray() ? args : UniValue{UniValue::VARR}, wallet);
    }));
    // The pages of a sidechain in a frame (doc/unified-app.md §13): their requests, which pack is built in, its fit.
    b.RegisterHost("pack.call", guarded([this, stem_of](const UniValue& params) {
        return Pages()->Call(stem_of(params), Param(params, "request"));
    }), Bridge::Thread::LONG);
    // The app's own pages over every chain (doc/unified-app.md §18): what each chain can do, its reads, and its spends
    // (built here from checked parameters, asked in the trusted dialog by the chain's own describers).
    b.RegisterHost("chain.capabilities", [](const UniValue&) {
        UniValue out{UniValue::VARR};
        for (const ChainAdapter::Capabilities& c : ChainAdapter::Table()) out.push_back(ChainAdapter::CapabilitiesJson(c));
        return out;
    });
    using AdapterFn = UniValue (ChainAdapter::*)(const UniValue&) const;
    for (const auto& [name, fn, thread] : std::initializer_list<std::tuple<const char*, AdapterFn, Bridge::Thread>>{
             {"chain.summary", &ChainAdapter::Summary, Bridge::Thread::WORKER},
             {"chain.transactions", &ChainAdapter::Transactions, Bridge::Thread::WORKER},
             {"chain.transaction", &ChainAdapter::Transaction, Bridge::Thread::WORKER},
             {"chain.wallets", &ChainAdapter::Wallets, Bridge::Thread::WORKER},
             {"chain.wallet.create", &ChainAdapter::WalletCreate, Bridge::Thread::LONG},
             {"chain.wallet.load", &ChainAdapter::WalletLoad, Bridge::Thread::LONG},
             {"chain.wallet.unload", &ChainAdapter::WalletUnload, Bridge::Thread::LONG},
             {"chain.wallet.backup", &ChainAdapter::WalletBackup, Bridge::Thread::LONG},
             {"chain.receive", &ChainAdapter::Receive, Bridge::Thread::LONG},
             {"chain.depositAddress", &ChainAdapter::DepositAddress, Bridge::Thread::WORKER},
             {"chain.validate", &ChainAdapter::Validate, Bridge::Thread::WORKER},
             {"chain.send", &ChainAdapter::Send, Bridge::Thread::LONG},
             {"chain.withdraw", &ChainAdapter::Withdraw, Bridge::Thread::LONG},
             {"chain.deposit", &ChainAdapter::Deposit, Bridge::Thread::LONG},
             {"chain.staking", &ChainAdapter::Staking, Bridge::Thread::WORKER},
             {"chain.peg", &ChainAdapter::Peg, Bridge::Thread::WORKER},
             {"chain.coins", &ChainAdapter::Coins, Bridge::Thread::WORKER},
             {"chain.search", &ChainAdapter::Search, Bridge::Thread::WORKER},
             {"chain.block", &ChainAdapter::Block, Bridge::Thread::WORKER},
             {"chain.assets", &ChainAdapter::Assets, Bridge::Thread::WORKER},
             {"chain.names", &ChainAdapter::Names, Bridge::Thread::WORKER},
             {"chain.markets", &ChainAdapter::Markets, Bridge::Thread::WORKER},
             {"chain.positions", &ChainAdapter::Positions, Bridge::Thread::WORKER},
             {"chain.activity", &ChainAdapter::Activity, Bridge::Thread::WORKER},
             {"chain.assetLogo", &ChainAdapter::AssetLogo, Bridge::Thread::WORKER},
             // The DEX of each chain that has one (doc/unified-app.md §22): reads, then its spends through the trusted dialog.
             {"chain.dex.info", &ChainAdapter::DexInfo, Bridge::Thread::WORKER},
             {"chain.dex.pools", &ChainAdapter::DexPools, Bridge::Thread::WORKER},
             {"chain.dex.quote", &ChainAdapter::DexQuote, Bridge::Thread::WORKER},
             {"chain.dex.chart", &ChainAdapter::DexChart, Bridge::Thread::WORKER},
             {"chain.dex.positions", &ChainAdapter::DexPositions, Bridge::Thread::WORKER},
             {"chain.dex.swap", &ChainAdapter::DexSwap, Bridge::Thread::LONG},
             {"chain.dex.addLiquidity", &ChainAdapter::DexAddLiquidity, Bridge::Thread::LONG},
             {"chain.dex.removeLiquidity", &ChainAdapter::DexRemoveLiquidity, Bridge::Thread::LONG},
             {"chain.dex.createPool", &ChainAdapter::DexCreatePool, Bridge::Thread::LONG},
         }) {
        b.RegisterHost(name, guarded([this, fn](const UniValue& params) { return ((*Adapter()).*fn)(params); }), thread);
    }
    b.RegisterHost("pack.info", guarded([this, stem_of](const UniValue& params) { return Pages()->Info(stem_of(params)); }));
    b.RegisterHost("pack.list", [](const UniValue&) { return SidechainPages::Packs(); });
    b.RegisterHost("sidechains.log", guarded([this, stem_of](const UniValue& params) {
        const std::string stem{stem_of(params)};
        const auto s{Supervisor()};
        UniValue out{UniValue::VOBJ};
        for (const auto& [key, path] : {std::pair{"node", s->NetDataDir(stem) / "debug.log"}, std::pair{"supervisor", s->LogFile(stem)}}) {
            std::ifstream file{path.std_path(), std::ios::binary};
            std::string text;
            if (file) {
                file.seekg(0, std::ios::end);
                const std::streamoff size{file.tellg()};
                file.seekg(std::max<std::streamoff>(0, size - 6000));
                text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            }
            out.pushKV(key, text);
        }
        return out;
    }));
}

void App::RegisterModuleMethods(Bridge& b)
{
    // The downloads of the sidechains' programs (doc/unified-app.md, "Downloads and updates"): only for the app's own
    // pages (the pages of a sidechain are refused every modules.* method, sidechainpages.cpp).
    const auto stem_of{[](const UniValue& params) {
        const std::string stem{ParamStr(params, "stem")};
        if (!SidechainSupervisor::FindSpec(stem) || !ModuleInstaller::IsAllowed(stem)) throw HostError(BRIDGE_INVALID_REQUEST, "No such sidechain");
        return stem;
    }};
    const auto tag_of{[](const UniValue& params) {
        const std::string tag{ParamStr(params, "tag")};
        if (!ModuleInstaller::IsValidTag(tag)) throw HostError(BRIDGE_INVALID_REQUEST, "No such release");
        return tag;
    }};
    const auto guarded{[](auto fn) {
        return [fn](const UniValue& params) -> UniValue {
            try {
                return fn(params);
            } catch (const HostError&) {
                throw;
            } catch (const std::runtime_error& e) {
                throw HostError(e.what());
            }
        };
    }};
    const auto changed{[this](const std::string& stem) {
        UniValue data{UniValue::VOBJ};
        data.pushKV("stem", stem);
        if (m_hub) m_hub->Emit("sidechains", std::move(data), "sidechains:" + stem);
    }};
    // Another version made current: a daemon that runs is stopped first, then started on the new programs; one that does
    // not start goes back to the version it ran.
    const auto swap{[this, changed](const std::shared_ptr<SidechainSupervisor>& s, ModuleInstaller& installer, const std::string& stem, const std::function<void()>& make_current) {
        const bool enabled{s->Enabled(stem)};
        const auto stopped{[](const SidechainSupervisor::Status& st) { return st.pid == 0 && st.state != SidechainSupervisor::State::STOPPING; }};
        if (enabled) {
            s->Disable(stem);
            if (!s->WaitFor(stem, stopped, s->GetOptions().stop_timeout + std::chrono::seconds{10})) throw std::runtime_error("Its node did not stop: the version was not changed.");
        }
        const auto before{installer.Current(stem)};
        make_current();
        changed(stem);
        if (!enabled) return;
        const auto ready{[](const SidechainSupervisor::Status& st) { return st.state == SidechainSupervisor::State::RUNNING && st.rpc_ready; }};
        bool started{false};
        try {
            s->Enable(stem);
            started = true;
            if (s->WaitFor(stem, ready, std::chrono::minutes{3})) return;
        } catch (const std::exception&) {
        }
        // Not a failure of the new version: the app stops (the version stays, tried at the next start), or the user
        // turned the sidechain off while it started (it stays off).
        if (s->Stopping() || (started && !s->Enabled(stem))) return;
        // The new version does not start: back to the one before.
        const std::string now{installer.Current(stem).value_or("")};
        s->Disable(stem);
        s->WaitFor(stem, stopped, s->GetOptions().stop_timeout + std::chrono::seconds{10});
        if (before && *before != now) installer.Activate(stem, *before);
        changed(stem);
        s->Enable(stem);
        throw std::runtime_error(strprintf("Version %s did not start: %s", now, before ? strprintf("back to %s", *before) : std::string{"its node is off"}));
    }};
    b.RegisterHost("modules.releases", guarded([this, stem_of](const UniValue& params) {
        const std::string stem{stem_of(params)};
        const auto s{Supervisor()};
        return Installer()->ReleasesJson(stem, s->BinarySource(stem), Param(params, "force").isTrue());
    }), Bridge::Thread::LONG);
    b.RegisterHost("modules.install", guarded([this, stem_of, tag_of, changed](const UniValue& params) {
        const std::string stem{stem_of(params)};
        const std::string tag{tag_of(params)};
        Installer()->Start(stem, tag, /*activate=*/true, [stem, changed](bool) { changed(stem); }, Param(params, "allowOlder").isTrue());
        return UniValue{true};
    }));
    b.RegisterHost("modules.update", guarded([this, stem_of, tag_of, swap](const UniValue& params) {
        const std::string stem{stem_of(params)};
        const std::string tag{tag_of(params)};
        const auto s{Supervisor()};
        const auto installer{Installer()};
        // Installed beside the version that runs, then made current once its node stopped.
        ModuleInstaller* raw{installer.get()};
        installer->Start(stem, tag, /*activate=*/false, [s, raw, stem, tag, swap](bool ok) {
            if (ok) swap(s, *raw, stem, [&] { raw->Activate(stem, tag); });
        }, Param(params, "allowOlder").isTrue());
        return UniValue{true};
    }));
    b.RegisterHost("modules.rollback", guarded([this, stem_of, swap](const UniValue& params) {
        const std::string stem{stem_of(params)};
        const auto s{Supervisor()};
        const auto installer{Installer()};
        if (installer->Busy(stem)) throw HostError("A download of this sidechain is running: wait for it, or cancel it.");
        if (!installer->Previous(stem)) throw HostError("There is no earlier version to go back to.");
        swap(s, *installer, stem, [&] { installer->Rollback(stem); });
        UniValue out{UniValue::VOBJ};
        const auto current{installer->Current(stem)};
        out.pushKV("current", current ? UniValue{*current} : UniValue{});
        return out;
    }), Bridge::Thread::LONG);
    b.RegisterHost("modules.progress", guarded([this, stem_of](const UniValue& params) { return Installer()->Progress(stem_of(params)); }));
    b.RegisterHost("modules.cancel", guarded([this, stem_of](const UniValue& params) {
        Installer()->Cancel(stem_of(params));
        return UniValue{true};
    }));
    b.RegisterHost("modules.checkUpdates", guarded([this](const UniValue& params) {
        const auto s{Supervisor()};
        const auto installer{Installer()};
        const bool force{Param(params, "force").isTrue()};
        UniValue updates{UniValue::VARR};
        UniValue errors{UniValue::VARR};
        for (const auto& spec : SidechainSupervisor::Catalog()) {
            if (!s->Enabled(spec.stem) || !ModuleInstaller::IsAllowed(spec.stem)) continue;
            try {
                const auto tag{installer->UpdateFor(spec.stem, force)};
                if (!tag) continue;
                UniValue u{UniValue::VOBJ};
                u.pushKV("stem", spec.stem);
                u.pushKV("name", spec.name);
                u.pushKV("current", installer->Current(spec.stem).value_or(""));
                u.pushKV("latest", *tag);
                updates.push_back(std::move(u));
            } catch (const std::exception& e) {
                UniValue err{UniValue::VOBJ};
                err.pushKV("stem", spec.stem);
                err.pushKV("error", e.what());
                errors.push_back(std::move(err));
            }
        }
        UniValue out{UniValue::VOBJ};
        out.pushKV("updates", std::move(updates));
        out.pushKV("errors", std::move(errors));
        return out;
    }), Bridge::Thread::LONG);
}

int App::Run()
{
    g_ui_thread = std::this_thread::get_id();
    const std::string config_path{m_config_path};
    m_config = std::make_unique<AppConfig>(config_path.empty() ? AppConfig::DefaultPath() : fs::PathFromString(config_path));
    m_config->Load();
    // The language from the command line or the system, until settings.json is read (-lang there).
    {
        const std::string language{ChooseLanguage(BuiltinAssets(), m_args.GetArg("-lang", ""))};
        {
            LOCK(m_mutex);
            m_language = language;
        }
        SetCoreCatalog(LoadCatalog(BuiltinAssets(), language));
    }
    m_options_journal = std::make_unique<OptionsJournal>(*m_config, m_args, [this] {
        LOCK(m_mutex);
        return "optionsUndo:" + (m_network.empty() ? ChainTypeToString(m_args.GetChainType()) : m_network);
    });

#ifdef WIN32
    // The payment URIs of the chain (the installer of chains-qt registers them for all users).
    if (m_args.GetBoolArg("-webuiregisteruri", true) && m_args.GetArg("-webuitestscript", "").empty()) RegisterUriScheme("chains", CLIENT_NAME);
#endif

    Window::Options options;
    options.title = CLIENT_NAME;
    options.devtools = m_devtools;
    // The size the window had last time (MainWindowGeometry).
    if (const UniValue geometry{m_config->Value("window")}; geometry.isObject()) {
        const UniValue& width{geometry.find_value("width")};
        const UniValue& height{geometry.find_value("height")};
        if (width.isNum() && height.isNum() && width.getInt<int>() >= 400 && height.getInt<int>() >= 300) {
            options.width = std::min(width.getInt<int>(), 10000);
            options.height = std::min(height.getInt<int>(), 10000);
        }
        options.maximized = geometry.find_value("maximized").isBool() && geometry.find_value("maximized").get_bool();
    }
    // The integrated title bar unless the user chose the one of the system (Settings → Appearance).
    options.frameless = ParseTitleBarMode(m_config->Value("titleBar")) == TitleBarMode::INTEGRATED;
    Window::Callbacks callbacks;
    callbacks.close_requested = [this] { CloseRequested(); };
    callbacks.minimized = [this] {
        // As BitcoinGUI::changeEvent: minimized to the tray, if the user wants that and there is a tray.
        if (PrefFlag("minimizeToTray", false) && PrefFlag("showTray", true) && m_window->TrayAvailable()) {
            m_window->Post([this] { m_window->Hide(); });
        }
    };
    callbacks.maximized_changed = [this](bool maximized) {
        UniValue data{UniValue::VOBJ};
        data.pushKV("maximized", maximized);
        if (m_hub) m_hub->Emit("window", std::move(data), "window");
    };
    callbacks.tray_activated = [this] {
        if (m_window->IsVisible()) m_window->Hide();
        else m_window->Present();
    };
    callbacks.tray_menu = [this](const std::string& id) { OnTrayMenu(id); };
    callbacks.session_ending = [this] { RequestShutdown(); };
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
    // An exception a host method did not expect: the internal error dialog of chains-qt (handleNonFatalException).
    m_bridge->SetInternalError([this](const std::string& method, const std::string& what) {
        LogWarning("webui: internal error in %s: %s", method, what);
        UniValue dialog{UniValue::VOBJ};
        dialog.pushKV("kind", "message");
        dialog.pushKV("level", "error");
        dialog.pushKV("title", "Internal error");
        dialog.pushKV("message", strprintf("An internal error occurred. %s will attempt to continue safely. This is an unexpected bug which can be reported as described below.", CLIENT_NAME));
        UniValue details{UniValue::VARR};
        details.push_back(strprintf("%s: %s", method, what));
        details.push_back(CLIENT_BUGREPORT);
        dialog.pushKV("details", std::move(details));
        m_hub->Emit("dialog", std::move(dialog));
    });
    // The pages call the RPC methods they need, and no other (the console runs any command, a command that
    // does more than read on the user's word given to the desktop). They keep to it in the tests too: the
    // policy that ships is the one tested (audit port-1 #5). A test script (-webuitestscript, in a build with
    // WEBUI_TEST_HOOKS) sets the node up (wallets, blocks) with requests of the kind "testrpc", which a
    // release does not take.
    m_bridge->SetRpcPolicy(PageRpcPolicy);
#ifdef WEBUI_TEST_HOOKS
    if (!m_test_script.empty()) m_bridge->OpenTestRpc();
#endif
    RegisterHostMethods();

    m_node_thread = std::thread([this] { NodeThread(); });
    m_window->Run();

    // The loop ends after the node stopped (Quit), or when the window went away without asking.
    RequestShutdown();
    if (const Window::Geometry geometry{m_window->GetGeometry()}; geometry.width > 0 && geometry.height > 0) {
        UniValue saved{UniValue::VOBJ};
        saved.pushKV("width", geometry.width);
        saved.pushKV("height", geometry.height);
        saved.pushKV("maximized", geometry.maximized);
        m_config->SetValue("window", saved);
    }
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
    // No thread may reach the window once it is gone: the downloads (and the end of an update, which emits) are
    // waited for, and an event emitted later is only kept (a page that is not ready schedules no delivery).
    std::shared_ptr<ModuleInstaller> installer;
    {
        LOCK(m_mutex);
        installer = m_installer;
    }
    if (installer) installer->Stop();
    m_hub->SetReady(false);
    m_window.reset();
    return status;
}

} // namespace webui
