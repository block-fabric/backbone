// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WEBUI_WINDOW_H
#define BITCOIN_WEBUI_WINDOW_H

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace webui {

/**
 * The native window of the app with the web view in it (webview/webview), and
 * the event loop of the UI thread.
 *
 * Every method is for the UI thread, except Post, PostDelayed, Reply and Quit,
 * which any thread may call.
 */
class Window
{
public:
    struct Options {
        std::string title;
        int width{1180};
        int height{760};
        //! Web inspector and the context menu of the web view (-webuidevtools).
        bool devtools{false};
        //! Start maximized (the geometry saved last time).
        bool maximized{false};
        //! The page draws the frame: the title bar and the border, in the colours of its theme (ThemedFrame).
        bool framed{false};
    };
    //! The size of the window, and whether it is maximized (MainWindowGeometry of chains-qt).
    struct Geometry {
        int width{0};
        int height{0};
        bool maximized{false};
    };
    //! An item of the menu of the tray icon.
    struct TrayItem {
        std::string id;
        std::string label;
        bool enabled{true};
        bool separator{false};
    };
    //! The icon in the system tray (notification area), with its menu.
    struct Tray {
        bool visible{false};
        std::string tooltip;
        std::vector<TrayItem> menu;
    };
    struct Callbacks {
        //! The user asks to close the window (the close button, Alt+F4). The window stays until Quit.
        std::function<void()> close_requested;
        //! The page calls the bridge: the id of the call, the JSON array of its arguments.
        std::function<void(const std::string& call_id, const std::string& args_json)> call;
        //! The icon in the tray was clicked (BitcoinGUI::trayIconActivated: show or hide the window).
        std::function<void()> tray_activated;
        //! An item of the menu of the tray was chosen: its id.
        std::function<void(const std::string& id)> tray_menu;
        //! The window was minimized by the user (for "minimize to the tray").
        std::function<void()> minimized;
        //! The window was maximized, or no longer is (the page shows its button as "Restore").
        std::function<void(bool maximized)> maximized_changed;
    };

    /** The window of this platform. It is not shown until Show. */
    static std::unique_ptr<Window> Create(const Options& options, Callbacks callbacks);

    virtual ~Window() = default;

    /** Run the event loop until Quit. */
    virtual void Run() = 0;
    /** End the event loop. Thread-safe. */
    virtual void Quit() = 0;
    /** Run a function on the UI thread. Thread-safe. */
    virtual void Post(std::function<void()> fn) = 0;
    /** Run a function on the UI thread after a delay. Thread-safe. */
    virtual void PostDelayed(std::chrono::milliseconds delay, std::function<void()> fn) = 0;
    /** Answer a call of the page (status 0: result, otherwise error), with JSON. Thread-safe. */
    virtual void Reply(const std::string& call_id, int status, const std::string& json) = 0;

    /** Run a script in the page (not subject to the policy of the page: for the app and its tests only). */
    virtual void Eval(const std::string& script) = 0;

    virtual void SetTitle(const std::string& title) = 0;
    /** The network of the node: the icon of the window and of the tray takes its colours (NetworkStyle). */
    virtual void SetNetwork(const std::string& network) = 0;
    virtual void Show(bool minimized) = 0;
    /** Bring the window to the front (a second start of the app asks for it). */
    virtual void Present() = 0;
    virtual void Minimize() = 0;
    /** Hide the window (to the tray), and whether it shows now. */
    virtual void Hide() = 0;
    virtual bool IsVisible() = 0;
    /** The size of the window now. */
    virtual Geometry GetGeometry() = 0;

    /**
     * The frame of the window: the one of the desktop, or none, the page drawing its own (ThemedFrame of
     * chains-qt: the menus in the title bar, the colours of the theme). @return whether the page frames the
     * window now (false where the platform cannot do without the frame of the desktop).
     */
    virtual bool SetFramed(bool framed) = 0;
    /** The user drags the title bar the page draws: the window moves with the pointer. */
    virtual void BeginMove() = 0;
    /** The user drags an edge ("n", "ne", "e", "se", "s", "sw", "w", "nw") of the border the page draws. */
    virtual void BeginResize(const std::string& edge) = 0;
    virtual void ToggleMaximized() = 0;
    virtual bool IsMaximized() = 0;

    /**
     * Show, change or take away the icon in the tray. Desktops with a tray of the
     * StatusNotifierItem kind (KDE, most others; GNOME with an extension) and the older
     * kind (XEmbed) both have it.
     */
    virtual void SetTray(const Tray& tray) = 0;
    /** Whether this desktop has a tray to show the icon in. */
    virtual bool TrayAvailable() = 0;

    /**
     * A notification of the desktop (Notificator of chains-qt): a title, a text, and its kind:
     * "info", "warning" or "critical". Shown by the notification service of the desktop.
     */
    virtual void Notify(const std::string& title, const std::string& body, const std::string& level) = 0;

    /** Open a file, a folder or a web address with the application of the desktop for it. */
    virtual bool OpenExternal(const std::string& uri) = 0;

    virtual void SetClipboard(const std::string& text) = 0;
    virtual std::optional<std::string> GetClipboard() = 0;

    /** A file chooser of the desktop, to open a file or to save one. */
    struct FileDialog {
        bool save{false};
        std::string title;
        //! The name proposed when saving.
        std::string suggested_name;
        //! Filters: a name and its patterns ("Wallet Data", {"*.dat"}). The first one is chosen.
        std::vector<std::pair<std::string, std::vector<std::string>>> filters;
        //! Choose a folder, not a file.
        bool folder{false};
        //! The folder the chooser opens in.
        std::string start_dir;
    };
    /**
     * Show a file chooser; `done` gets the path chosen, or nothing when it was cancelled. When
     * saving, the extension of the first filter is added to a name without one (as the Qt GUI
     * does), and the user confirms overwriting a file.
     */
    virtual void ChooseFile(const FileDialog& options, std::function<void(std::optional<std::string>)> done) = 0;

    /** Save a picture of what the page shows now as PNG. For the tests. */
    virtual void Snapshot(const std::string& png_path, std::function<void(bool ok)> done) = 0;
};

} // namespace webui

#endif // BITCOIN_WEBUI_WINDOW_H
