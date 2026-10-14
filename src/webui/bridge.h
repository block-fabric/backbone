// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WEBUI_BRIDGE_H
#define BITCOIN_WEBUI_BRIDGE_H

#include <sync.h>
#include <univalue.h>
#include <util/threadpool.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

namespace webui {

//! Error codes the bridge answers with, besides the codes of the RPC methods themselves.
enum BridgeError : int {
    //! The request was cancelled by the page.
    BRIDGE_CANCELLED = -32800,
    //! The request is not a bridge request (bad JSON, missing fields, unknown kind).
    BRIDGE_INVALID_REQUEST = -32600,
    //! No host method of that name.
    BRIDGE_METHOD_NOT_FOUND = -32601,
    //! The node is not ready for RPC calls yet (still starting).
    BRIDGE_NOT_READY = -28,
    //! The app is shutting down: no more calls.
    BRIDGE_STOPPED = -9,
    //! A host method failed.
    BRIDGE_HOST_ERROR = -1,
};

//! What a host method throws to answer with an error of its own.
struct HostError : std::runtime_error {
    int code;
    HostError(int code_in, const std::string& message) : std::runtime_error(message), code(code_in) {}
    explicit HostError(const std::string& message) : HostError(BRIDGE_HOST_ERROR, message) {}
};

/** The URI path of a wallet for the RPC dispatcher: "/wallet/<percent-encoded name>". */
std::string WalletUri(const std::string& wallet_name);

/** Percent-encode everything but the unreserved characters of RFC 3986. */
std::string PercentEncode(const std::string& text);

/**
 * The bridge between the page and the node. The page calls one function,
 * with requests like
 *
 *   {"rid": "17", "kind": "rpc",  "method": "getbalances", "params": [], "wallet": "w1"}
 *   {"rid": "18", "kind": "host", "method": "prefs.set", "params": {"theme": "dark"}}
 *   {"kind": "cancel", "rid": "17"}
 *
 * RPC calls go to the in-process RPC dispatcher (as the RPC console of the Qt
 * GUI does: no HTTP, no credentials), host calls to the functions the app
 * registers. Both run on worker threads, never on the UI thread, except the
 * host methods registered to run on the UI thread (window, clipboard). The
 * answer comes back through the reply function: status 0 and the result as
 * JSON, or status 1 and {"code": ..., "message": ...}.
 *
 * A request can be cancelled while it waits or runs: its answer is then the
 * BRIDGE_CANCELLED error at once, and what it returns later is dropped.
 */
class Bridge
{
public:
    //! Sends an answer to the page. Called from any thread; must be thread-safe.
    using ReplyFn = std::function<void(const std::string& call_id, int status, const std::string& json)>;
    //! Runs an RPC method in-process. Throws a UniValue error object, or std::exception.
    using RpcFn = std::function<UniValue(const std::string& method, const UniValue& params, const std::string& uri)>;
    //! A host method: takes the params of the request, returns the result. Throws HostError.
    using HostFn = std::function<UniValue(const UniValue& params)>;
    //! Runs a function on the UI thread, later.
    using UiRunner = std::function<void(std::function<void()>)>;

    enum class Thread { WORKER, UI };

    Bridge(ReplyFn reply, UiRunner ui, int workers = 4);
    ~Bridge();

    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;

    /** Where RPC calls go. Until it is set, they are answered with BRIDGE_NOT_READY. */
    void SetRpc(RpcFn rpc) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /** Make a host method available to the page. */
    void RegisterHost(const std::string& name, HostFn fn, Thread thread = Thread::WORKER) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * Handle a call of the page. `call_id` identifies the call for the reply,
     * `args_json` is the JSON array of the arguments of the call: [request].
     */
    void Handle(const std::string& call_id, const std::string& args_json) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * Stop taking calls: the ones waiting are answered with BRIDGE_STOPPED, the
     * ones running are waited for. Called before the node shuts down, after
     * the node was asked to (so that calls that wait for the node return).
     */
    void Stop() EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /** Number of requests waiting or running. */
    size_t Pending() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

private:
    struct Request {
        std::string call_id;
        std::string rid;
        std::atomic<bool> answered{false};
    };

    void Reply(Request& request, int status, const std::string& json);
    void ReplyError(Request& request, int code, const std::string& message);
    void Run(const std::shared_ptr<Request>& request, const UniValue& body) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    void Finish(const std::shared_ptr<Request>& request) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    const ReplyFn m_reply;
    const UiRunner m_ui;
    ThreadPool m_pool{"webui"};

    mutable Mutex m_mutex;
    RpcFn m_rpc GUARDED_BY(m_mutex);
    struct Host {
        HostFn fn;
        Thread thread;
    };
    std::map<std::string, Host> m_host GUARDED_BY(m_mutex);
    //! Requests waiting or running, by the id the page gave them.
    std::map<std::string, std::shared_ptr<Request>> m_requests GUARDED_BY(m_mutex);
    bool m_stopped GUARDED_BY(m_mutex){false};
};

/** The error object of an exception thrown by the RPC dispatcher or a host method. */
UniValue ErrorObject(int code, const std::string& message);

} // namespace webui

#endif // BITCOIN_WEBUI_BRIDGE_H
