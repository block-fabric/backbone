// Copyright (c) 2026 The Backbone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_GRPC_SERVER_H
#define BITCOIN_GRPC_SERVER_H

#include <grpc/protobuf.h>
#include <netaddress.h>
#include <sync.h>
#include <util/threadinterrupt.h>
#include <util/time.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class Sock;
class ThreadPool;

/**
 * A server of RPCs described by protocol buffers (pb::Pool), as the bip300301_enforcer serves them
 * (connectrpc): gRPC over HTTP/2 without TLS (h2c, prior knowledge), which its clients use, and on
 * the same port gRPC-Web and the Connect protocol, over HTTP/2 or HTTP/1.1. HTTP/2 is nghttp2's;
 * there is no TLS, no authentication and no compression (identity only), as the enforcer.
 */
namespace grpcsrv {

/** gRPC status codes. */
enum class Code : int {
    OK = 0, CANCELLED = 1, UNKNOWN = 2, INVALID_ARGUMENT = 3, DEADLINE_EXCEEDED = 4, NOT_FOUND = 5,
    ALREADY_EXISTS = 6, PERMISSION_DENIED = 7, RESOURCE_EXHAUSTED = 8, FAILED_PRECONDITION = 9, ABORTED = 10,
    OUT_OF_RANGE = 11, UNIMPLEMENTED = 12, INTERNAL = 13, UNAVAILABLE = 14, DATA_LOSS = 15, UNAUTHENTICATED = 16,
};

struct Status {
    Code code{Code::OK};
    std::string message;
    bool ok() const { return code == Code::OK; }
};

/** The Connect name of a code ("invalid_argument") and the HTTP status the Connect protocol gives it. */
std::string ConnectCodeName(Code code);
int ConnectHttpStatus(Code code);
/** grpc-message is percent-encoded. */
std::string PercentEncode(std::string_view message);

/** Where a call puts its replies. Thread safe; handlers may keep it and write from any thread. */
class Writer
{
public:
    virtual ~Writer() = default;
    /** Queue a reply. @return false once the call is over (finished, or the client went away). */
    virtual bool Write(const pb::Message& reply) = 0;
    /** End the call (a unary call: after its one reply, or with an error instead). Later calls do nothing. */
    virtual void Finish(const Status& status) = 0;
    /** Whether the call is over. */
    virtual bool Closed() const = 0;
    /** Bytes written and not sent yet (a client that does not read lets them pile up). */
    virtual size_t Pending() const = 0;
    /** Called once when the call ends (the client cancelled, the connection closed, or Finish). */
    virtual void OnClose(std::function<void()> callback) = 0;
};

/**
 * A method: the handler gets the request and the writer, and finishes the call when it is done
 * (now or later: a server stream stays open for as long as it does not finish it). It runs on a
 * worker thread. A method with client streaming (server reflection) gets each request as it comes
 * instead, on the connection's thread, and the call ends with OK when the client is done sending.
 */
using Handler = std::function<void(const pb::Message& request, const std::shared_ptr<Writer>& writer)>;

/** A check of each call (its path and headers, names in lower case) before it runs; a status refuses it. */
using Guard = std::function<std::optional<Status>(const std::string& path, const std::map<std::string, std::string>& headers)>;

struct Options {
    //! Largest request message, in bytes.
    size_t max_request_bytes{16 << 20};
    //! Request bytes a connection may have buffered at once (all its streams), and all connections together.
    size_t max_connection_buffer{64 << 20};
    size_t max_total_buffer{256 << 20};
    //! Origin header values let in. A request with another Origin (from a web page) is refused (CSRF).
    std::vector<std::string> allowed_origins;
    //! Host (or :authority) names let in besides IP literals and localhost; others are refused (DNS rebinding).
    std::vector<std::string> allowed_hosts;
    //! Largest number of connections at a time.
    int max_connections{128};
    //! Connections one client address may hold at once (0: no bound), but for the addresses of per_ip_exempt (the
    //! loopback addresses for the node: the machine's own sidechain nodes and apps share them; the total bounds them).
    int max_connections_per_ip{0};
    std::vector<CSubNet> per_ip_exempt;
    //! Close a connection whose request (its headers and body) is not all in this long after its first byte, however
    //! slowly it keeps coming (slow clients holding connections); 0: never. Calls running on a worker are not counted.
    int request_timeout_seconds{0};
    //! Close a connection that has no call running and sent nothing for this long (0: never); over HTTP/2 with a
    //! GOAWAY first, so that a client knows to open another.
    int idle_timeout_seconds{0};
    //! Worker threads for the handlers.
    int threads{4};
};

class Connection;

class Server
{
public:
    explicit Server(const pb::Pool& pool, Options options = {});
    ~Server();

    /** Register the method `/service/method` of the pool. @return false if the pool has no such method. */
    bool Register(const std::string& service, const std::string& method, Handler handler);
    void SetGuard(Guard guard) { m_guard = std::move(guard); }
    const Guard& GetGuard() const { return m_guard; }
    /** Account for request bytes buffered (all connections); false if over the budget. */
    bool Reserve(size_t bytes);
    void Release(size_t bytes);
    /** The full names of the services with a method registered. */
    std::vector<std::string> Services() const;

    /** Listen on these addresses; a client must be in `allow` (or on the loopback interface). */
    bool Start(const std::vector<CService>& binds, const std::vector<CSubNet>& allow, std::string& error);
    /** Stop accepting connections, end every call (with UNAVAILABLE) and close the connections. */
    void Stop();
    /** The ports bound, for -grpcport=0 in tests. */
    std::vector<CService> Bound() const;

    struct MethodEntry {
        const pb::MethodDesc* desc;
        Handler handler;
    };
    const MethodEntry* Find(std::string_view path) const;
    const pb::Pool& Pool() const { return m_pool; }
    const Options& GetOptions() const { return m_options; }
    void Run(std::function<void()> task);

private:
    void AcceptLoop();
    void ServeConnection(std::shared_ptr<Sock> sock, CService peer);

    const pb::Pool& m_pool;
    const Options m_options;
    std::map<std::string, MethodEntry, std::less<>> m_methods;
    std::vector<std::shared_ptr<Sock>> m_listen;
    std::vector<CService> m_bound;
    std::vector<CSubNet> m_allow;
    CThreadInterrupt m_interrupt;
    std::thread m_accept_thread;
    std::unique_ptr<ThreadPool> m_workers;
    Mutex m_mutex;
    std::vector<std::pair<std::thread, std::shared_ptr<std::atomic<bool>>>> m_connection_threads GUARDED_BY(m_mutex);
    std::atomic<int> m_connections{0};
    std::atomic<size_t> m_buffered{0};
    Guard m_guard;
    //! Connections by client address (Options::max_connections_per_ip).
    std::map<std::string, int> m_per_ip GUARDED_BY(m_mutex);
};

/**
 * One client connection, without its socket (so that tests can drive it): bytes in (Receive),
 * bytes out (TakeOutput), and Poll() to send what calls have written meanwhile. HTTP/2 if the
 * client starts with its connection preface, HTTP/1.1 otherwise. Not thread safe: one thread runs it.
 */
class Connection
{
public:
    explicit Connection(Server& server);
    ~Connection();
    /** @return false if the connection is to be closed (a protocol error, or HTTP/1.1 without keep-alive). */
    bool Receive(std::span<const uint8_t> data);
    bool Poll();
    std::string TakeOutput();
    /** Whether there is something to send, or to wait for. */
    bool WantsWrite() const;
    /** Whether a call is running (its request all in, the call not over). */
    bool Busy() const;
    /** Whether a request is being read: begun and not all in (its headers, or its body). */
    bool Receiving() const;
    /** Before closing an idle connection: HTTP/2 says GOAWAY (no error), to be sent from TakeOutput. */
    void Goodbye();
    /** HTTP/2: when the oldest request still being read began (each stream timed on its own); else nothing. */
    std::optional<NodeClock::time_point> OldestRequest() const;
    /** End every call of the connection (the client went away, or the server stops). */
    void CloseAll();

    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace grpcsrv

#endif // BITCOIN_GRPC_SERVER_H
