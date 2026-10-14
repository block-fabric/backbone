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
    };
    struct Callbacks {
        //! The user asks to close the window (the close button, Alt+F4). The window stays until Quit.
        std::function<void()> close_requested;
        //! The page calls the bridge: the id of the call, the JSON array of its arguments.
        std::function<void(const std::string& call_id, const std::string& args_json)> call;
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
    virtual void Show(bool minimized) = 0;
    /** Bring the window to the front (a second start of the app asks for it). */
    virtual void Present() = 0;
    virtual void Minimize() = 0;

    virtual void SetClipboard(const std::string& text) = 0;
    virtual std::optional<std::string> GetClipboard() = 0;

    /** Save a picture of what the page shows now as PNG. For the tests. */
    virtual void Snapshot(const std::string& png_path, std::function<void(bool ok)> done) = 0;
};

} // namespace webui

#endif // BITCOIN_WEBUI_WINDOW_H
