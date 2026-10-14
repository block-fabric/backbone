// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <webui/singleinstance.h>

#include <crypto/sha256.h>
#include <logging.h>
#include <util/strencodings.h>
#include <util/threadnames.h>

#include <array>
#include <cstdlib>
#include <cstring>

#ifndef WIN32
#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace webui {

fs::path SingleInstance::SocketPath(const fs::path& datadir_net)
{
    std::array<unsigned char, CSHA256::OUTPUT_SIZE> hash;
    const std::string dir{fs::PathToString(fs::absolute(datadir_net))};
    CSHA256().Write(reinterpret_cast<const unsigned char*>(dir.data()), dir.size()).Finalize(hash.data());
    const std::string name{"chains-app-" + HexStr(std::span{hash}.first(10)) + ".sock"};
    // A socket path is short (108 bytes on Linux): in the runtime directory of the user, or the temporary one.
    fs::path base;
    if (const char* runtime{std::getenv("XDG_RUNTIME_DIR")}; runtime && *runtime) base = fs::PathFromString(runtime);
    else base = fs::temp_directory_path();
    return base / fs::PathFromString(name);
}

#ifdef WIN32
// TODO(windows port): a named pipe. Until then every start runs on its own (and a second one for the
// same data directory stops at the lock of the data directory, with its message).
bool SingleInstance::Send(const fs::path&, const std::vector<std::string>&) { return false; }
bool SingleInstance::Listen(const fs::path&, MessageFn) { return true; }
void SingleInstance::Stop() {}
#else
namespace {
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

int Accept(int fd)
{
#ifdef __linux__
    return ::accept4(fd, nullptr, nullptr, SOCK_CLOEXEC);
#else
    return ::accept(fd, nullptr, nullptr);
#endif
}

bool Address(const fs::path& socket, sockaddr_un& addr)
{
    const std::string path{fs::PathToString(socket)};
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) return false;
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    return true;
}

#ifdef MSG_NOSIGNAL
constexpr int SEND_FLAGS{MSG_NOSIGNAL};
#else
constexpr int SEND_FLAGS{0};
#endif

bool WriteAll(int fd, std::string_view data)
{
    while (!data.empty()) {
        const ssize_t n{::send(fd, data.data(), data.size(), SEND_FLAGS)};
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        data.remove_prefix(n);
    }
    return true;
}
} // namespace

bool SingleInstance::Send(const fs::path& socket, const std::vector<std::string>& lines)
{
    sockaddr_un addr;
    if (!Address(socket, addr)) return false;
    const int fd{::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    if (fd < 0) return false;
    bool sent{::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0};
    for (const std::string& line : lines) {
        if (!sent) break;
        // One message per line: a line break inside one would split it.
        if (line.find_first_of("\r\n") != std::string::npos || line.size() > MAX_MESSAGE) continue;
        sent = WriteAll(fd, line + "\n");
    }
    ::close(fd);
    return sent;
}

bool SingleInstance::Listen(const fs::path& socket, MessageFn on_message)
{
    sockaddr_un addr;
    if (!Address(socket, addr)) {
        LogWarning("webui: the path of the instance socket is too long: %s", fs::PathToString(socket));
        return false;
    }
    // A socket left by an app that crashed: nobody answers on it.
    if (fs::exists(socket)) {
        if (Send(socket, {})) return false;
        std::error_code ec;
        fs::remove(socket, ec);
    }
    m_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (m_fd < 0) return false;
    if (::bind(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(m_fd, 8) != 0) {
        LogWarning("webui: cannot listen on %s: %s", fs::PathToString(socket), std::strerror(errno));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    m_socket = socket;
    m_stop = false;
    m_thread = std::thread([this, on_message = std::move(on_message)] {
        util::ThreadRename("webui-instance");
        while (!m_stop) {
            pollfd p{m_fd, POLLIN, 0};
            if (::poll(&p, 1, 200) <= 0) continue;
            const int client{Accept(m_fd)};
            if (client < 0) continue;
            // A client has a second to say what it wants.
            timeval timeout{1, 0};
            ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            std::string data;
            char buffer[4096];
            for (;;) {
                const ssize_t n{::recv(client, buffer, sizeof(buffer), 0)};
                if (n <= 0) break;
                data.append(buffer, n);
                if (data.size() > MAX_MESSAGE * 4) break;
            }
            ::close(client);
            size_t start{0};
            for (size_t end{data.find('\n')}; end != std::string::npos; start = end + 1, end = data.find('\n', start)) {
                const std::string line{data.substr(start, end - start)};
                if (!line.empty() && line.size() <= MAX_MESSAGE) on_message(line);
            }
        }
    });
    return true;
}

void SingleInstance::Stop()
{
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
        std::error_code ec;
        fs::remove(m_socket, ec);
    }
}
#endif

bool SingleInstance::SendToRunning(const fs::path& socket, const std::vector<std::string>& uris)
{
    std::vector<std::string> lines;
    for (const std::string& uri : uris) lines.push_back("uri " + uri);
    if (lines.empty()) lines.emplace_back("activate");
    return Send(socket, lines);
}

SingleInstance::~SingleInstance()
{
    Stop();
}

} // namespace webui
