// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>
#include <webui/singleinstance.h>

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

using namespace std::chrono_literals;

#ifndef WIN32
namespace {
struct Received {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::string> lines;
    webui::SingleInstance::MessageFn Fn()
    {
        return [this](const std::string& line) {
            {
                std::lock_guard lock{mutex};
                lines.push_back(line);
            }
            cv.notify_all();
        };
    }
    bool WaitFor(size_t n)
    {
        std::unique_lock lock{mutex};
        return cv.wait_for(lock, 10s, [&] { return lines.size() >= n; });
    }
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(webui_singleinstance_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(socket_path)
{
    const fs::path a{webui::SingleInstance::SocketPath(fs::PathFromString("/data/one"))};
    const fs::path b{webui::SingleInstance::SocketPath(fs::PathFromString("/data/two"))};
    BOOST_CHECK(a != b);
    BOOST_CHECK(a == webui::SingleInstance::SocketPath(fs::PathFromString("/data/one")));
    BOOST_CHECK(fs::PathToString(a.filename()).starts_with("chains-app-"));
    BOOST_CHECK(fs::PathToString(a).size() < 100);
}

BOOST_AUTO_TEST_CASE(hand_over)
{
    const fs::path socket{m_args.GetDataDirBase() / "app.sock"};
    // Nobody listens yet.
    BOOST_CHECK(!webui::SingleInstance::SendToRunning(socket, {"chains:abc"}));

    Received received;
    webui::SingleInstance first;
    BOOST_REQUIRE(first.Listen(socket, received.Fn()));
    // A second one cannot listen there.
    webui::SingleInstance second;
    BOOST_CHECK(!second.Listen(socket, [](const std::string&) {}));

    BOOST_CHECK(webui::SingleInstance::SendToRunning(socket, {"chains:abc?amount=1", "chains:def"}));
    BOOST_REQUIRE(received.WaitFor(2));
    BOOST_CHECK(webui::SingleInstance::SendToRunning(socket, {}));
    BOOST_REQUIRE(received.WaitFor(3));
    // A message with a line break would be two: it is not sent.
    BOOST_CHECK(webui::SingleInstance::Send(socket, {"uri chains:x\nactivate", "uri chains:ok"}));
    BOOST_REQUIRE(received.WaitFor(4));
    std::this_thread::sleep_for(100ms);
    {
        std::lock_guard lock{received.mutex};
        BOOST_REQUIRE_EQUAL(received.lines.size(), 4U);
        BOOST_CHECK_EQUAL(received.lines[0], "uri chains:abc?amount=1");
        BOOST_CHECK_EQUAL(received.lines[1], "uri chains:def");
        BOOST_CHECK_EQUAL(received.lines[2], "activate");
        BOOST_CHECK_EQUAL(received.lines[3], "uri chains:ok");
    }
    first.Stop();
    BOOST_CHECK(!fs::exists(socket));
    BOOST_CHECK(!webui::SingleInstance::SendToRunning(socket, {}));
}

BOOST_AUTO_TEST_CASE(stale_socket)
{
    // A socket file left by an app that crashed is taken over.
    const fs::path socket{m_args.GetDataDirBase() / "stale.sock"};
    {
        webui::SingleInstance crashed;
        BOOST_REQUIRE(crashed.Listen(socket, [](const std::string&) {}));
        // Stop removes the file; put one back as a crash would leave it.
    }
    std::ofstream{static_cast<const std::filesystem::path&>(socket)} << "";
    Received received;
    webui::SingleInstance app;
    BOOST_REQUIRE(app.Listen(socket, received.Fn()));
    BOOST_CHECK(webui::SingleInstance::SendToRunning(socket, {}));
    BOOST_CHECK(received.WaitFor(1));
}

BOOST_AUTO_TEST_SUITE_END()
#endif
