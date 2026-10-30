// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The pages of a sidechain in a frame of the app (webui/sidechainpages.h): where each request goes, the question of the
// daemon shown in the app's trusted dialog with the chain named, the very same request run with the token on a yes and
// never on a no, a wrong passphrase asked again, files only those chosen in the app's chooser, the look the app's.
// The daemon is a stand-in here; the real one is in run_unified.py and the tests of each sidechain.

#include <webui/sidechainpages.h>
#include <webui/supervisor.h>

#include <test/util/setup_common.h>

#include <tinyformat.h>
#include <util/fs.h>

#include <boost/test/unit_test.hpp>

#include <fstream>

#include <string>
#include <thread>
#include <variant>
#include <vector>

using namespace webui;
using Route = SidechainPages::Route;

namespace {
const std::string TOKEN(64, 'a');

UniValue Json(const std::string& text)
{
    UniValue v;
    BOOST_REQUIRE(v.read(text));
    return v;
}

struct Daemon {
    struct CallSeen {
        std::string method;
        UniValue params;
    };
    std::vector<CallSeen> calls;
    //! What webuihost answers, in turn: {result} or {confirm}, or an error (code) to throw.
    std::vector<std::variant<UniValue, int>> answers;
    bool running{true};
    std::vector<Window::ConfirmDialog> asked;
    std::vector<dialog::Answer> said;
    UniValue pack_prefs{UniValue::VOBJ};
    std::vector<std::string> chooser;

    SidechainPages::Hooks Hooks()
    {
        SidechainPages::Hooks h;
        h.rpc = [this](const std::string&, const std::string& method, const UniValue& params, std::chrono::milliseconds) -> UniValue {
            if (method == "webuievents") {
                // The thread that follows the daemon's events: nothing happens on this one.
                std::this_thread::sleep_for(std::chrono::milliseconds{20});
                return Json(R"({"seq":0,"reset":false,"batches":[]})");
            }
            calls.push_back({method, params});
            if (method == "webuichosenfile") return UniValue{std::string(64, 'f')};
            if (method != "webuihost") return UniValue{};
            BOOST_REQUIRE(!answers.empty());
            auto next{answers.front()};
            answers.erase(answers.begin());
            if (std::holds_alternative<int>(next)) throw SidechainSupervisor::RpcError(std::get<int>(next), "refused by the stand-in");
            return std::get<UniValue>(next);
        };
        h.running = [this](const std::string&) { return running; };
        h.name = [](const std::string&) { return std::string{"Layers"}; };
        h.ask = [this](const Window::ConfirmDialog& d) {
            asked.push_back(d);
            BOOST_REQUIRE(!said.empty());
            auto a{said.front()};
            said.erase(said.begin());
            return a;
        };
        h.hub = [this](const std::string& method, const UniValue&) -> UniValue {
            if (method == "prefs.get") {
                UniValue p{UniValue::VOBJ};
                p.pushKV("theme", "midnight");
                p.pushKV("layout", "dashboard");
                return p;
            }
            if (method == "file.choose") {
                if (chooser.empty()) return UniValue{};
                std::string path{chooser.front()};
                chooser.erase(chooser.begin());
                return UniValue{path};
            }
            return UniValue{"hub:" + method};
        };
        h.events = [](const std::string&, const std::string&, bool) {};
        h.deposit = [](const std::string&, const UniValue&) { return UniValue{"deposited"}; };
        h.pack_prefs = [this](const std::string&) { return pack_prefs; };
        h.set_pack_prefs = [this](const std::string&, const UniValue& p) { pack_prefs = p; };
        h.language = [] { return std::string{}; };
        h.network = [] { return std::string{"Regtest"}; };
        return h;
    }
    int HostCalls() const { return std::count_if(calls.begin(), calls.end(), [](const auto& c) { return c.method == "webuihost"; }); }
};


UniValue Confirm(bool passphrase = false, const std::string& token = TOKEN)
{
    UniValue c{Json(R"({"title":"Send coins","message":"Check each address.","accept":"Send","level":"warning","kind":"send",
        "rows":[{"label":"Wallet","value":"main","kind":"text"},{"label":"Pay to","value":"rladdr1","kind":"address"},{"label":"Amount","value":"1.00000000 LYR","kind":"amount"}]})")};
    c.pushKV("passphrase", passphrase);
    c.pushKV("token", token);
    UniValue out{UniValue::VOBJ};
    out.pushKV("confirm", c);
    return out;
}

UniValue Result(const std::string& txid)
{
    UniValue out{UniValue::VOBJ};
    UniValue r{UniValue::VOBJ};
    r.pushKV("txid", txid);
    out.pushKV("result", r);
    return out;
}

const UniValue SEND{Json(R"({"kind":"host","method":"wallet.send.commit","params":{"wallet":"main","token":"7"}})")};

dialog::Answer Yes(const std::string& passphrase = "")
{
    dialog::Answer a;
    a.accepted = true;
    a.passphrase.assign(passphrase.begin(), passphrase.end());
    return a;
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(webui_sidechainpages_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(routes)
{
    BOOST_CHECK(SidechainPages::RouteOf("wallet.send.commit") == Route::DAEMON);
    BOOST_CHECK(SidechainPages::RouteOf("rpc.confirmed") == Route::DAEMON);
    BOOST_CHECK(SidechainPages::RouteOf("bitassets.store.load") == Route::DAEMON);
    BOOST_CHECK(SidechainPages::RouteOf("fastblocks.bond") == Route::DAEMON);
    BOOST_CHECK(SidechainPages::RouteOf("clipboard.write") == Route::HUB);
    BOOST_CHECK(SidechainPages::RouteOf("file.read") == Route::LOCAL);
    BOOST_CHECK(SidechainPages::RouteOf("app.open") == Route::LOCAL);
    BOOST_CHECK(SidechainPages::RouteOf("prefs.set") == Route::LOCAL);
    BOOST_CHECK(SidechainPages::RouteOf("window.minimize") == Route::LOCAL);
    BOOST_CHECK(SidechainPages::RouteOf("peg.deposit") == Route::LOCAL);
    // The backup phrase and the mining wallet are the app's own, never a pack's to ask for.
    for (const char* m : {"seed.status", "seed.addPassword", "seed.removePassword", "mining.status", "mining.create", "mining.stop"}) BOOST_CHECK(SidechainPages::RouteOf(m) == Route::REFUSE);
    for (const char* m : {"app.quit", "app.restart", "app.quitGuard", "tray.state", "app.help", "app.about", "options.set", "intro.choose", "test.exit", "sidechains.enable", "pack.call", "sidechainNodes.start", "node.setNetworkActive", "chain.send", "chain.withdraw", "chain.deposit", "chain.summary"}) {
        BOOST_CHECK_MESSAGE(SidechainPages::RouteOf(m) == Route::REFUSE, m);
    }
}

BOOST_AUTO_TEST_CASE(question_shown_then_exactly_that_call_runs)
{
    Daemon d;
    d.answers = {Confirm(), Result("t1")};
    d.said = {Yes()};
    SidechainPages pages{d.Hooks()};
    const UniValue result{pages.Call("layers", SEND)};
    pages.Stop();
    BOOST_CHECK_EQUAL(result.find_value("txid").get_str(), "t1");
    // The question of the daemon, the chain named first and above the title, its mark.
    BOOST_REQUIRE_EQUAL(d.asked.size(), 1U);
    const auto& q{d.asked[0]};
    BOOST_CHECK_EQUAL(q.title, "Send coins");
    BOOST_CHECK_EQUAL(q.chain, "Layers");
    BOOST_CHECK_EQUAL(q.chain_network, "Regtest");
    BOOST_CHECK_EQUAL(q.rows[0].label, "Chain");
    BOOST_CHECK_EQUAL(q.rows[0].value, "Layers");
    BOOST_CHECK_EQUAL(q.rows[2].value, "rladdr1");
    BOOST_CHECK(q.rows[2].kind == dialog::Row::Kind::ADDRESS);
    BOOST_CHECK(q.kind.starts_with("pack:Layers:"));
    // Twice the same request: first without, then with the token.
    std::vector<UniValue> host;
    for (const auto& c : d.calls) {
        if (c.method == "webuihost") host.push_back(c.params);
    }
    BOOST_REQUIRE_EQUAL(host.size(), 2U);
    BOOST_CHECK_EQUAL(host[0][0].write(), host[1][0].write());
    BOOST_CHECK_EQUAL(host[0][1].get_str(), "");
    BOOST_CHECK_EQUAL(host[1][1].get_str(), TOKEN);
}

BOOST_AUTO_TEST_CASE(the_apps_own_rows_after_the_chain_before_the_daemons)
{
    // What the app checked (a swap's route quoted again, doc/unified-app.md §22): its rows after the chain's name, before
    // the daemon's own; at most 16; the same request runs with its token.
    Daemon d;
    d.answers = {Confirm(), Result("t2")};
    d.said = {Yes()};
    SidechainPages pages{d.Hooks()};
    UniValue rows{UniValue::VARR};
    rows.push_back(Json(R"({"label":"Checked by Chains","value":"","kind":"heading"})"));
    rows.push_back(Json(R"j({"label":"Route","value":"GOLD → CHN → SILV (2 pools)","kind":"text"})j"));
    for (int i{0}; i < 20; ++i) rows.push_back(Json(R"({"label":"More","value":"x"})"));
    const UniValue result{pages.Call("layers", SEND, rows)};
    pages.Stop();
    BOOST_CHECK_EQUAL(result.find_value("txid").get_str(), "t2");
    BOOST_REQUIRE_EQUAL(d.asked.size(), 1U);
    const auto& q{d.asked[0]};
    BOOST_CHECK_EQUAL(q.rows[0].label, "Chain");
    BOOST_CHECK_EQUAL(q.rows[1].label, "Checked by Chains");
    BOOST_CHECK(q.rows[1].kind == dialog::Row::Kind::HEADING);
    BOOST_CHECK_EQUAL(q.rows[2].value, "GOLD → CHN → SILV (2 pools)");
    BOOST_CHECK_EQUAL(q.rows[17].label, "Wallet");
    BOOST_CHECK_EQUAL(q.rows.size(), 1U + 16U + 3U);
    // A pack's request has no rows of the app's: the daemon's only.
    Daemon e;
    e.answers = {Confirm(), Result("t3")};
    e.said = {Yes()};
    SidechainPages other{e.Hooks()};
    other.Call("layers", SEND);
    other.Stop();
    BOOST_CHECK_EQUAL(e.asked[0].rows[1].label, "Wallet");
}

BOOST_AUTO_TEST_CASE(declined_runs_nothing)
{
    Daemon d;
    d.answers = {Confirm()};
    d.said = {dialog::Answer{}};
    SidechainPages pages{d.Hooks()};
    BOOST_CHECK_EXCEPTION(pages.Call("layers", SEND), HostError, [](const HostError& e) { return e.code == BRIDGE_CANCELLED; });
    pages.Stop();
    BOOST_CHECK_EQUAL(d.HostCalls(), 1);
}

BOOST_AUTO_TEST_CASE(wrong_passphrase_asked_again)
{
    Daemon d;
    d.answers = {Confirm(true), -14, Confirm(true), Result("t2")};
    d.said = {Yes("wrong"), Yes("right")};
    SidechainPages pages{d.Hooks()};
    BOOST_CHECK_EQUAL(pages.Call("layers", SEND).find_value("txid").get_str(), "t2");
    pages.Stop();
    BOOST_REQUIRE_EQUAL(d.asked.size(), 2U);
    BOOST_CHECK(d.asked[0].passphrase);
    BOOST_CHECK(d.asked[0].error.empty());
    BOOST_CHECK(!d.asked[1].error.empty());
    // The passphrase goes with the token, to the daemon only.
    BOOST_CHECK_EQUAL(d.calls.back().params[2].get_str(), "right");
}

BOOST_AUTO_TEST_CASE(refused_token_and_second_question)
{
    // The daemon refuses the token (the call changed, came late, another wallet): nothing more, the error said.
    Daemon d;
    d.answers = {Confirm(), -32802};
    d.said = {Yes()};
    SidechainPages pages{d.Hooks()};
    BOOST_CHECK_EXCEPTION(pages.Call("layers", SEND), HostError, [](const HostError& e) { return e.code == -32802; });
    // A second question for one request: never answered.
    Daemon d2;
    d2.answers = {Confirm(), Confirm()};
    d2.said = {Yes()};
    SidechainPages pages2{d2.Hooks()};
    BOOST_CHECK_EXCEPTION(pages2.Call("layers", SEND), HostError, [](const HostError& e) { return std::string{e.what()}.find("second question") != std::string::npos; });
    BOOST_CHECK_EQUAL(d2.asked.size(), 1U);
    // A question without a proper token: not shown.
    Daemon d3;
    d3.answers = {Confirm(false, "nothex")};
    SidechainPages pages3{d3.Hooks()};
    BOOST_CHECK_THROW(pages3.Call("layers", SEND), HostError);
    BOOST_CHECK(d3.asked.empty());
    pages.Stop();
    pages2.Stop();
    pages3.Stop();
}

BOOST_AUTO_TEST_CASE(files_only_chosen_in_the_app)
{
    Daemon d;
    const UniValue backup{Json(R"({"kind":"host","method":"wallet.backup","params":{"wallet":"main","file":"/home/u/backup.dat"}})")};
    d.answers = {Result("b1"), Result("b2")};
    SidechainPages pages{d.Hooks()};
    // A path the page names that was never chosen: no token, the daemon (which refuses a file without one) gets none.
    pages.Call("layers", backup);
    BOOST_CHECK(std::none_of(d.calls.begin(), d.calls.end(), [](const auto& c) { return c.method == "webuichosenfile"; }));
    BOOST_CHECK(d.calls.back().params[3].empty());
    // Chosen in the app's chooser for these pages: a token for it goes with the request, never a path the page gives.
    d.chooser = {"/home/u/backup.dat"};
    BOOST_CHECK_EQUAL(pages.Call("layers", Json(R"({"kind":"host","method":"file.choose","params":{"save":true}})")).get_str(), "/home/u/backup.dat");
    pages.Call("layers", backup);
    const auto chosen{std::find_if(d.calls.begin(), d.calls.end(), [](const auto& c) { return c.method == "webuichosenfile"; })};
    BOOST_REQUIRE(chosen != d.calls.end());
    BOOST_CHECK_EQUAL(chosen->params[0].get_str(), "/home/u/backup.dat");
    BOOST_CHECK(chosen->params[1].get_bool());
    // A new file: nothing to write over, so overwrite is not given.
    BOOST_CHECK(!chosen->params[2].get_bool());
    BOOST_CHECK_EQUAL(d.calls.back().params[3][0].get_str(), std::string(64, 'f'));
    pages.Stop();
}

BOOST_AUTO_TEST_CASE(each_packs_own_methods_from_its_manifest)
{
    // The packs built in (WEBUI_SIDECHAIN_PACKS): what each one's pages use of its daemon, read from its local.cpp at the
    // pinned commit (scripts/packs.mjs), for that daemon's whitelist of this app's user (doc §13.5 #59).
    if (SidechainPages::Packs().empty()) {
        BOOST_TEST_MESSAGE("no packs built in (the package Chains alone): nothing to check");
        return;
    }
    const auto has{[](const std::vector<std::string>& v, const char* m) { return std::ranges::find(v, m) != v.end(); }};
    const auto layers{SidechainPages::PackMethods("layers")};
    for (const char* m : {"listmyassets", "sendasset", "swapasset", "bitassets.logo", "fastblocks.bond", "getstakinginfo"}) BOOST_CHECK_MESSAGE(has(layers, m), m);
    const auto hivemind{SidechainPages::PackMethods("hivemind")};
    for (const char* m : {"listmarkets", "getwalletpositions", "buyshares", "hivemind.store.load"}) BOOST_CHECK_MESSAGE(has(hivemind, m), m);
    const auto zside{SidechainPages::PackMethods("zside")};
    BOOST_CHECK(has(zside, "sendshielded"));
    BOOST_CHECK(has(zside, "zside.viewingkey.export"));
    // A chain's own only: not another's, and nothing for a chain without a pack.
    BOOST_CHECK(!has(SidechainPages::PackMethods("thunder"), "sendasset"));
    BOOST_CHECK(SidechainPages::PackMethods("nope").empty());
}

BOOST_AUTO_TEST_CASE(overwrite_only_when_the_chooser_asked_to_replace)
{
    // A save onto a file there already: the chooser of the desktop asked to replace it (the user said yes), so the token
    // carries overwrite; a file to read, or a new one, never does.
    const fs::path there{m_args.GetDataDirBase() / "backup-there.dat"};
    {
        std::ofstream f{there.std_path()};
        f << "old";
    }
    const std::string path{fs::PathToString(there)};
    Daemon d;
    d.answers = {Result("b1"), Result("b2")};
    SidechainPages pages{d.Hooks()};
    d.chooser = {path, path};
    pages.Call("layers", Json(strprintf(R"({"kind":"host","method":"file.choose","params":{"save":true}})")));
    pages.Call("layers", Json(strprintf(R"({"kind":"host","method":"wallet.backup","params":{"wallet":"main","file":"%s"}})", path)));
    auto token{std::find_if(d.calls.begin(), d.calls.end(), [](const auto& c) { return c.method == "webuichosenfile"; })};
    BOOST_REQUIRE(token != d.calls.end());
    BOOST_CHECK(token->params[1].get_bool());
    BOOST_CHECK(token->params[2].get_bool());
    // The same file chosen to be read (a restore): no overwrite.
    d.calls.clear();
    pages.Call("layers", Json(R"({"kind":"host","method":"file.choose","params":{}})"));
    pages.Call("layers", Json(strprintf(R"({"kind":"host","method":"wallets.restore","params":{"name":"r","file":"%s"}})", path)));
    token = std::find_if(d.calls.begin(), d.calls.end(), [](const auto& c) { return c.method == "webuichosenfile"; });
    BOOST_REQUIRE(token != d.calls.end());
    BOOST_CHECK(!token->params[1].get_bool());
    BOOST_CHECK(!token->params[2].get_bool());
    pages.Stop();
}

BOOST_AUTO_TEST_CASE(a_phrase_without_a_password_asks_none)
{
    // The wallet of the backup phrase without a password (doc §23.11): the question is asked without a passphrase field, and
    // its passphrase made from the seed goes with the token. Another wallet of that chain is asked as before.
    Daemon d;
    d.answers = {Confirm(true), Result("t1"), Confirm(true), Result("t2")};
    d.said = {Yes(), Yes("its own")};
    auto hooks{d.Hooks()};
    hooks.phrase_wallet = [](const std::string&) { return std::string{"main"}; };
    hooks.passphrase_without = [](const std::string&, const std::string& wallet) { return wallet == "main" ? SecureString{"made-from-the-seed"} : SecureString{}; };
    SidechainPages pages{hooks};
    BOOST_CHECK_EQUAL(pages.Call("layers", SEND).find_value("txid").get_str(), "t1");
    BOOST_REQUIRE_EQUAL(d.asked.size(), 1U);
    BOOST_CHECK(!d.asked[0].passphrase);
    BOOST_CHECK_EQUAL(d.calls.back().params[2].get_str(), "made-from-the-seed");
    // A wallet of its own on that chain ("other"): its passphrase asked and sent as typed.
    pages.Call("layers", Json(R"({"kind":"host","method":"wallet.send.commit","params":{"wallet":"other","token":"7"},"wallet":"other"})"));
    BOOST_REQUIRE_EQUAL(d.asked.size(), 2U);
    BOOST_CHECK(d.asked[1].passphrase);
    BOOST_CHECK_EQUAL(d.calls.back().params[2].get_str(), "its own");
    pages.Stop();

    // A password added while the question was shown: nothing is sent with the passphrase made from the seed.
    Daemon e;
    e.answers = {Confirm(true)};
    e.said = {Yes()};
    auto hooks2{e.Hooks()};
    int asked_without{0};
    hooks2.phrase_wallet = [](const std::string&) { return std::string{"main"}; };
    hooks2.passphrase_without = [&](const std::string&, const std::string&) { return ++asked_without == 1 ? SecureString{"made-from-the-seed"} : SecureString{}; };
    SidechainPages later{hooks2};
    BOOST_CHECK_EXCEPTION(later.Call("layers", SEND), HostError, [](const HostError& err) { return err.code == BRIDGE_CANCELLED; });
    later.Stop();
    BOOST_CHECK_EQUAL(e.HostCalls(), 1);
}

BOOST_AUTO_TEST_CASE(app_answers_for_the_frame)
{
    Daemon d;
    SidechainPages pages{d.Hooks()};
    // The look is the app's; what the pack set of its own stays its own.
    pages.Call("layers", Json(R"({"kind":"host","method":"prefs.set","params":{"theme":"light","layout":"workspace"}})"));
    BOOST_CHECK(!d.pack_prefs.exists("theme"));
    BOOST_CHECK_EQUAL(d.pack_prefs.find_value("layout").get_str(), "workspace");
    const UniValue prefs{pages.Call("layers", Json(R"({"kind":"host","method":"prefs.get","params":{}})"))};
    BOOST_CHECK_EQUAL(prefs.find_value("theme").get_str(), "midnight");
    BOOST_CHECK_EQUAL(prefs.find_value("layout").get_str(), "workspace");
    // No window of its own.
    BOOST_CHECK(!pages.Call("layers", Json(R"({"kind":"host","method":"window.state","params":{}})")).find_value("frameless").get_bool());
    // What only a whole app does: refused.
    BOOST_CHECK_EXCEPTION(pages.Call("layers", Json(R"({"kind":"host","method":"app.quit","params":{}})")), HostError, [](const HostError& e) { return e.code == BRIDGE_METHOD_NOT_FOUND; });
    // The clipboard: the app's.
    BOOST_CHECK_EQUAL(pages.Call("layers", Json(R"({"kind":"host","method":"clipboard.write","params":{"text":"x"}})")).get_str(), "hub:clipboard.write");
    // Its node not running: the pages wait (phase init); calls to it are not ready.
    d.running = false;
    BOOST_CHECK_EQUAL(pages.Call("layers", Json(R"({"kind":"host","method":"app.hello","params":{}})")).find_value("phase").get_str(), "init");
    BOOST_CHECK(pages.Call("layers", Json(R"({"kind":"host","method":"app.hello","params":{}})")).find_value("info").find_value("embedded").get_bool());
    BOOST_CHECK_EXCEPTION(pages.Call("layers", SEND), HostError, [](const HostError& e) { return e.code == BRIDGE_NOT_READY; });
    BOOST_CHECK_EQUAL(d.HostCalls(), 0);
    // Least privilege: links only, never the app's log or configuration; no quit guard, no tray; a notice named
    // after its chain; a file only if chosen for these pages.
    BOOST_CHECK_EXCEPTION(pages.Call("layers", Json(R"({"kind":"host","method":"app.open","params":{"what":"debuglog"}})")), HostError, [](const HostError& e) { return e.code == BRIDGE_METHOD_NOT_FOUND; });
    BOOST_CHECK_EXCEPTION(pages.Call("layers", Json(R"({"kind":"host","method":"app.open","params":{"what":"config"}})")), HostError, [](const HostError& e) { return e.code == BRIDGE_METHOD_NOT_FOUND; });
    BOOST_CHECK_EQUAL(pages.Call("layers", Json(R"({"kind":"host","method":"app.open","params":{"what":"url","url":"https://example.org"}})")).get_str(), "hub:app.open");
    for (const char* m : {"app.quitGuard", "tray.state"}) {
        UniValue r{UniValue::VOBJ};
        r.pushKV("kind", "host");
        r.pushKV("method", m);
        r.pushKV("params", UniValue{UniValue::VOBJ});
        BOOST_CHECK_EXCEPTION(pages.Call("layers", r), HostError, [](const HostError& e) { return e.code == BRIDGE_METHOD_NOT_FOUND; });
    }
    BOOST_CHECK_EXCEPTION(pages.Call("layers", Json(R"({"kind":"host","method":"file.read","params":{"path":"/etc/passwd"}})")), HostError, [](const HostError& e) { return e.code == BRIDGE_INVALID_REQUEST; });
    // A deposit: the app's own (Chains' confirmation).
    BOOST_CHECK_EQUAL(pages.Call("layers", Json(R"({"kind":"host","method":"peg.deposit","params":{"destination":"x","amount":"1"}})")).get_str(), "deposited");
    pages.Stop();
}

BOOST_AUTO_TEST_SUITE_END()
