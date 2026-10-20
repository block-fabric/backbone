// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The app's own pages over every chain (webui/chainadapter.h): the table of what each chain does, amounts parsed
// exactly, the reads mapped to each node's methods, and the spends built from checked parameters: an address of
// another chain refused before anything is prepared or asked, an asset only one the wallet holds, the chain's own
// question asked (the trusted dialog stands behind `call`, tested in sidechainpages_tests). The nodes are stand-ins.

#include <webui/bridge.h>
#include <webui/chainadapter.h>
#include <webui/supervisor.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace webui;
using Kind = ChainAdapter::AddressKind;

namespace {
UniValue Json(const std::string& text)
{
    UniValue v;
    BOOST_REQUIRE_MESSAGE(v.read(text), text);
    return v;
}

const std::string TXID(64, 'b');
const std::string ASSET(64, 'c');

/** Nodes of a few chains: what each answers, and every call seen. */
struct Nodes {
    struct Seen {
        std::string stem;
        std::string method;
        UniValue params;
        std::string wallet;
    };
    std::vector<Seen> rpcs;
    std::vector<std::pair<std::string, UniValue>> calls;
    std::vector<std::pair<std::string, UniValue>> deposits;
    //! The addresses valid on each chain ("chains": the app's own), and those of a wallet of each.
    std::map<std::string, std::vector<std::string>> valid{{"chains", {"rchn1qmainaddress0"}}, {"layers", {"rsc1qlayersaddr0", "rsc1qthunderown0", "rsc1qlayersown00"}}, {"thunder", {"rsc1qlayersaddr0", "rsc1qthunderown0", "rsc1qlayersown00"}}};
    std::map<std::string, std::vector<std::string>> mine{{"chains", {}}, {"layers", {"rsc1qlayersown00"}}, {"thunder", {"rsc1qthunderown0"}}};
    std::vector<std::string> up{"layers", "thunder"};
    //! What a read through the host answers instead (a question: never asked).
    bool read_asks{false};
    //! The commit of a send is declined in the dialog.
    bool decline{false};

    UniValue Read(const std::string& stem, const UniValue& request)
    {
        const std::string method{request.find_value("method").get_str()};
        const UniValue& p{request.find_value("params")};
        UniValue out{UniValue::VOBJ};
        if (read_asks) {
            out.pushKV("confirm", Json(R"({"title":"?","token":"aa"})"));
            return out;
        }
        UniValue result;
        if (method == "validateaddress") {
            const auto& list{valid[stem]};
            result = Json(std::ranges::find(list, p[0].get_str()) != list.end() ? R"({"isvalid":true})" : R"({"isvalid":false})");
        } else if (method == "getaddressinfo") {
            const auto& list{mine[stem]};
            result = Json(std::ranges::find(list, p[0].get_str()) != list.end() ? R"({"ismine":true})" : R"({"ismine":false})");
        } else if (method == "listmyassets") {
            result = Json(R"({"assets":[{"asset":")" + ASSET + R"(","label":"GOLD","decimals":2,"balance":250.5,"pending":0,"control":true,"supply":100000}]})");
        } else if (method == "listwithdrawals") {
            result = Json(R"([{"txid":")" + TXID + R"(","vout":0,"amount":1.25000000,"refundaddress":"rsc1qlayersown00","status":"waiting"},{"txid":")" + TXID + R"(","vout":1,"amount":3,"refundaddress":"rsc1qsomeoneelse","status":"bundled"}])");
        } else if (method == "getstakinginfo") {
            result = Json(R"({"validator":true,"enabled":true,"keys":[{"pubkey":"ab","inset":true,"stake":5}]})");
        } else {
            result = UniValue{UniValue::VARR};
        }
        out.pushKV("result", result);
        return out;
    }

    ChainAdapter::Hooks Hooks()
    {
        ChainAdapter::Hooks h;
        h.rpc = [this](const std::string& stem, const std::string& method, const UniValue& params, const std::string& wallet) -> UniValue {
            rpcs.push_back({stem, method, params, wallet});
            if (method == "webuihost") return Read(stem, params[0]);
            if (method == "listwallets") return Json(stem == "layers" ? R"(["other","main"])" : R"(["main"])");
            if (method == "getbalances") return Json(R"({"mine":{"trusted":12.34567891,"untrusted_pending":0.1,"immature":0}})");
            if (method == "getblockcount") return UniValue{42};
            if (method == "getmainchaininfo") return Json(R"({"slot":7,"connected":true,"state":"ok","height":300})");
            if (method == "getdepositaddress") return Json(R"({"depositaddress":"s7_rsc1qlayersown00_abcdef","address":"rsc1qlayersown00","slot":7})");
            if (method == "listtransactions") {
                return Json(R"([{"txid":")" + TXID + R"(","category":"receive","amount":20.00000000,"time":100,"confirmations":3},{"txid":"nothex","amount":1},{"txid":")" + std::string(64, 'd') + R"(","category":"send","amount":-1.50000001,"fee":-0.00000141,"time":200,"confirmations":0}])");
            }
            return UniValue{};
        };
        h.call = [this](const std::string& stem, const UniValue& request) -> UniValue {
            calls.emplace_back(stem, request);
            const std::string method{request.find_value("method").get_str()};
            if (method == "wallet.send.prepare") return Json(R"({"token":"prepared-1","fee":141})");
            if (method == "wallet.send.commit") {
                if (decline) throw HostError(BRIDGE_CANCELLED, "Send: declined.");
                return Json(R"({"txid":")" + TXID + R"(","complete":true})");
            }
            if (method == "rpc.confirmed") return Json(R"({"txid":")" + TXID + R"("})");
            if (method == "wallet.newAddress") return UniValue{"rsc1qnewaddress0"};
            return UniValue{};
        };
        h.running = [this](const std::string& stem) { return std::ranges::find(up, stem) != up.end(); };
        h.name = [](const std::string& stem) { return stem == "layers" ? std::string{"Layers"} : stem == "thunder" ? std::string{"Thunder"} : stem; };
        h.product = [] { return std::string{"Backbone"}; };
        h.running_chains = [this] { return up; };
        h.mainchain_address = [this](const std::string& address) {
            UniValue out{UniValue::VOBJ};
            const auto& list{valid["chains"]};
            out.pushKV("isvalid", std::ranges::find(list, address) != list.end());
            out.pushKV("ismine", std::ranges::find(mine["chains"], address) != mine["chains"].end());
            return out;
        };
        h.deposit = [this](const std::string& stem, const UniValue& params) {
            deposits.emplace_back(stem, params);
            return Json(R"({"txid":")" + TXID + R"("})");
        };
        h.wallet_mode = [](const std::string& stem) { return stem == "thunder" ? std::string{"separate"} : std::string{"shared"}; };
        return h;
    }
};

UniValue P(const std::string& text) { return Json(text); }

int Code(const std::function<void()>& fn)
{
    try {
        fn();
    } catch (const HostError& e) {
        return e.code;
    }
    return 0;
}

std::string Message(const std::function<void()>& fn)
{
    try {
        fn();
    } catch (const HostError& e) {
        return e.what();
    }
    return {};
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(chainadapter_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(a_row_of_capabilities_for_every_chain_of_the_catalog)
{
    for (const SidechainSupervisor::Spec& s : SidechainSupervisor::Catalog()) BOOST_CHECK_MESSAGE(ChainAdapter::Find(s.stem), s.stem);
    BOOST_CHECK_EQUAL(ChainAdapter::Table().size(), SidechainSupervisor::Catalog().size());
    BOOST_CHECK(ChainAdapter::Find("layers")->assets && ChainAdapter::Find("layers")->staking && ChainAdapter::Find("layers")->dex);
    BOOST_CHECK(ChainAdapter::Find("bitassets")->assets && !ChainAdapter::Find("bitassets")->staking);
    BOOST_CHECK(ChainAdapter::Find("fastblocks")->staking && !ChainAdapter::Find("fastblocks")->assets);
    BOOST_CHECK(ChainAdapter::Find("zside")->shielded && ChainAdapter::Find("bitnames")->names && ChainAdapter::Find("hivemind")->markets);
    const ChainAdapter::Capabilities& thunder{*ChainAdapter::Find("thunder")};
    BOOST_CHECK(!thunder.assets && !thunder.staking && !thunder.names && !thunder.markets && !thunder.shielded);
    BOOST_CHECK(!ChainAdapter::Find("chains"));
    const UniValue json{ChainAdapter::CapabilitiesJson(*ChainAdapter::Find("layers"))};
    BOOST_CHECK(json.find_value("send").isTrue() && json.find_value("deposit").isTrue() && json.find_value("decimals").getInt<int>() == 8);
}

BOOST_AUTO_TEST_CASE(amounts_parsed_exactly)
{
    BOOST_CHECK_EQUAL(*ChainAdapter::ParseUnits("1.5", 8), 150000000);
    BOOST_CHECK_EQUAL(*ChainAdapter::ParseUnits("0.00000001", 8), 1);
    BOOST_CHECK_EQUAL(*ChainAdapter::ParseUnits("21000000", 8), 2100000000000000);
    BOOST_CHECK_EQUAL(*ChainAdapter::ParseUnits(".5", 2), 50);
    BOOST_CHECK_EQUAL(*ChainAdapter::ParseUnits("12", 0), 12);
    BOOST_CHECK_EQUAL(*ChainAdapter::ParseUnits("0", 8), 0);
    for (const char* bad : {"", ".", "1.", "-1", "+1", "1e3", "1,5", " 1", "0x10", "1.000000001", "99999999999999999999", "1..2", "١"}) {
        BOOST_CHECK_MESSAGE(!ChainAdapter::ParseUnits(bad, 8), bad);
    }
    BOOST_CHECK(!ChainAdapter::ParseUnits("1.234", 2));
    BOOST_CHECK(!ChainAdapter::ParseUnits("1.5", 0));
    BOOST_CHECK(!ChainAdapter::ParseUnits("1", 19));
    BOOST_CHECK_EQUAL(ChainAdapter::FormatUnits(150000000, 8), "1.50000000");
    BOOST_CHECK_EQUAL(ChainAdapter::FormatUnits(1, 8), "0.00000001");
    BOOST_CHECK_EQUAL(ChainAdapter::FormatUnits(-141, 8), "-0.00000141");
    BOOST_CHECK_EQUAL(ChainAdapter::FormatUnits(2505, 2), "25.05");
    BOOST_CHECK_EQUAL(ChainAdapter::FormatUnits(7, 0), "7");
    // A node's number, as it wrote it (never through a double).
    BOOST_CHECK_EQUAL(ChainAdapter::AmountText(Json("[20999999.97690000]")[0]), "20999999.97690000");
}

BOOST_AUTO_TEST_CASE(what_an_address_looks_like)
{
    std::string hrp;
    BOOST_CHECK(ChainAdapter::KindOf("") == Kind::EMPTY);
    BOOST_CHECK(ChainAdapter::KindOf("s7_rsc1qxyz_abcdef") == Kind::DEPOSIT);
    BOOST_CHECK(ChainAdapter::KindOf("s12_tly1qqqqqqqqqqq_012345") == Kind::DEPOSIT);
    BOOST_CHECK(ChainAdapter::KindOf("rsc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4", &hrp) == Kind::BECH32);
    BOOST_CHECK_EQUAL(hrp, "rsc");
    BOOST_CHECK(ChainAdapter::KindOf("TLY1QW508D6QEJXTDG4Y5R3ZARVARY0C5XW7KV8F3T4", &hrp) == Kind::BECH32);
    BOOST_CHECK_EQUAL(hrp, "tly");
    BOOST_CHECK(ChainAdapter::KindOf("tly1QW508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4") != Kind::BECH32);
    BOOST_CHECK(ChainAdapter::KindOf("mipcBbFg9gMiCh81Kj8tqqdgoZub1ZJRfn") == Kind::BASE58);
    BOOST_CHECK(ChainAdapter::KindOf("not an address") == Kind::OTHER);
}

BOOST_AUTO_TEST_CASE(the_wallet_of_a_chain)
{
    BOOST_CHECK_EQUAL(ChainAdapter::PickWallet(Json(R"(["a","main","b"])"), "b"), "b");
    BOOST_CHECK_EQUAL(ChainAdapter::PickWallet(Json(R"(["a","main","b"])"), "gone"), "main");
    BOOST_CHECK_EQUAL(ChainAdapter::PickWallet(Json(R"(["a","b"])"), ""), "a");
    BOOST_CHECK_EQUAL(ChainAdapter::PickWallet(Json("[]"), "a"), "");
    BOOST_CHECK_EQUAL(ChainAdapter::PickWallet(UniValue{}, "a"), "");
}

BOOST_AUTO_TEST_CASE(reads_mapped_to_each_node)
{
    Nodes nodes;
    ChainAdapter adapter{nodes.Hooks()};
    const UniValue s{adapter.Summary(P(R"({"stem":"layers"})"))};
    BOOST_CHECK_EQUAL(s.find_value("wallet").get_str(), "main");
    BOOST_CHECK_EQUAL(s.find_value("balances").find_value("trusted").get_str(), "12.34567891");
    BOOST_CHECK_EQUAL(s.find_value("height").getInt<int>(), 42);
    BOOST_CHECK_EQUAL(s.find_value("assets")[0].find_value("label").get_str(), "GOLD");
    BOOST_CHECK_EQUAL(s.find_value("assets")[0].find_value("balance").get_str(), "250.5");
    // Its own withdrawals only (the refund address is of the wallet).
    BOOST_CHECK_EQUAL(s.find_value("withdrawals").find_value("count").getInt<int>(), 1);
    BOOST_CHECK_EQUAL(s.find_value("withdrawals").find_value("amount").get_str(), "1.25000000");
    BOOST_CHECK_EQUAL(s.find_value("walletMode").get_str(), "shared");
    // Thunder: no assets asked for (its table says it has none).
    nodes.rpcs.clear();
    const UniValue t{adapter.Summary(P(R"({"stem":"thunder","wallet":"main"})"))};
    BOOST_CHECK(t.find_value("assets").isNull());
    BOOST_CHECK(std::ranges::none_of(nodes.rpcs, [](const auto& r) { return r.method == "webuihost" && r.params[0].find_value("method").get_str() == "listmyassets"; }));
    BOOST_CHECK_EQUAL(t.find_value("walletMode").get_str(), "separate");
    // A chain not running: said, nothing asked.
    nodes.up = {"layers"};
    nodes.rpcs.clear();
    BOOST_CHECK(!adapter.Summary(P(R"({"stem":"thunder"})")).find_value("running").isTrue());
    BOOST_CHECK(nodes.rpcs.empty());
    BOOST_CHECK_EQUAL(Code([&] { adapter.Summary(P(R"({"stem":"chains"})")); }), BRIDGE_INVALID_REQUEST);

    const UniValue txs{adapter.Transactions(P(R"({"stem":"layers","wallet":"other"})"))};
    const UniValue& rows{txs.find_value("rows")};
    BOOST_REQUIRE_EQUAL(rows.size(), 2U);
    BOOST_CHECK_EQUAL(rows[0].find_value("amount").get_str(), "-1.50000001");
    BOOST_CHECK_EQUAL(rows[0].find_value("fee").get_str(), "-0.00000141");
    BOOST_CHECK_EQUAL(rows[1].find_value("amount").get_str(), "20.00000000");
    BOOST_CHECK_EQUAL(nodes.rpcs.back().wallet, "other");

    const UniValue staking{adapter.Staking(P(R"({"stem":"layers"})"))};
    BOOST_CHECK(staking.find_value("node").find_value("validator").isTrue());
    BOOST_CHECK_EQUAL(Code([&] { adapter.Staking(P(R"({"stem":"thunder"})")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(Code([&] { adapter.Assets(P(R"({"stem":"thunder"})")); }), BRIDGE_INVALID_REQUEST);
}

BOOST_AUTO_TEST_CASE(a_read_that_asks_is_refused_not_asked)
{
    Nodes nodes;
    nodes.read_asks = true;
    ChainAdapter adapter{nodes.Hooks()};
    BOOST_CHECK_EQUAL(Code([&] { adapter.Assets(P(R"({"stem":"layers"})")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK(nodes.calls.empty());
    // The summary says it, section by section.
    const UniValue s{adapter.Summary(P(R"({"stem":"layers"})"))};
    BOOST_CHECK(s.find_value("errors").find_value("assets").isStr());
    BOOST_CHECK(nodes.calls.empty());
}

BOOST_AUTO_TEST_CASE(a_coin_send_prepared_then_asked)
{
    Nodes nodes;
    ChainAdapter adapter{nodes.Hooks()};
    const UniValue out{adapter.Send(P(R"({"stem":"layers","address":" rsc1qlayersaddr0 ","amount":"1.5"})"))};
    BOOST_CHECK_EQUAL(out.find_value("txid").get_str(), TXID);
    BOOST_REQUIRE_EQUAL(nodes.calls.size(), 2U);
    const UniValue& prepare{nodes.calls[0].second};
    BOOST_CHECK_EQUAL(prepare.find_value("method").get_str(), "wallet.send.prepare");
    const UniValue& r{prepare.find_value("params").find_value("recipients")[0]};
    BOOST_CHECK_EQUAL(r.find_value("address").get_str(), "rsc1qlayersaddr0");
    BOOST_CHECK_EQUAL(r.find_value("amount").getInt<int64_t>(), 150000000);
    BOOST_CHECK_EQUAL(prepare.find_value("params").find_value("wallet").get_str(), "main");
    BOOST_CHECK_EQUAL(nodes.calls[1].second.find_value("method").get_str(), "wallet.send.commit");
    BOOST_CHECK_EQUAL(nodes.calls[1].second.find_value("params").find_value("token").get_str(), "prepared-1");
    BOOST_CHECK(std::ranges::all_of(nodes.calls, [](const auto& c) { return c.first == "layers"; }));

    // Declined: nothing sent, the prepared send discarded.
    nodes.calls.clear();
    nodes.decline = true;
    BOOST_CHECK_EQUAL(Code([&] { adapter.Send(P(R"({"stem":"layers","address":"rsc1qlayersaddr0","amount":"1"})")); }), BRIDGE_CANCELLED);
    BOOST_REQUIRE_EQUAL(nodes.calls.size(), 3U);
    BOOST_CHECK_EQUAL(nodes.calls[2].second.find_value("method").get_str(), "wallet.send.discard");

    // Amounts: exactly, positive, in range, as text.
    nodes.calls.clear();
    for (const char* bad : {R"("0")", R"("1.000000001")", R"("-1")", R"("21000001")", R"(1.5)"}) {
        BOOST_CHECK_MESSAGE(Code([&] { adapter.Send(P(std::string{R"({"stem":"layers","address":"rsc1qlayersaddr0","amount":)"} + bad + "}")); }) == BRIDGE_INVALID_REQUEST, bad);
    }
    BOOST_CHECK(nodes.calls.empty());
}

BOOST_AUTO_TEST_CASE(wrong_chain_addresses_refused_before_anything_is_asked)
{
    Nodes nodes;
    ChainAdapter adapter{nodes.Hooks()};
    const auto refused{[&](const std::string& address, const std::string& words) {
        nodes.calls.clear();
        const std::string said{Message([&] { adapter.Send(P(R"({"stem":"layers","amount":"1","address":")" + address + R"("})")); })};
        BOOST_CHECK_MESSAGE(said.find(words) != std::string::npos, address + ": " + said);
        BOOST_CHECK_EQUAL(Code([&] { adapter.Send(P(R"({"stem":"layers","amount":"1","address":")" + address + R"("})")); }), ChainAdapter::WRONG_ADDRESS);
        BOOST_CHECK_MESSAGE(nodes.calls.empty(), address + ": something was prepared or asked");
    }};
    refused("rchn1qmainaddress0", "This is an address of Backbone, not of Layers");
    refused("s7_rsc1qlayersown00_abcdef", "deposit address");
    refused("rsc1qthunderown0", "your Thunder wallet");
    refused("rsc1qnowhere00000", "not an address of Layers");
    refused("", "Enter an address of Layers");
    // Valid only on Thunder (another prefix there).
    nodes.valid["layers"] = {"rsc1qlayersaddr0"};
    nodes.valid["thunder"] = {"tth1qthunderonly"};
    refused("tth1qthunderonly", "This is an address of Thunder, not of Layers");
    // Legacy addresses read the same on every chain.
    nodes.valid["layers"].push_back("mipcBbFg9gMiCh81Kj8tqqdgoZub1ZJRfn");
    refused("mipcBbFg9gMiCh81Kj8tqqdgoZub1ZJRfn", "legacy");
    // One of the app's own wallet.
    nodes.valid["layers"].push_back("rchn1qmainaddress0");
    nodes.mine["chains"].push_back("rchn1qmainaddress0");
    refused("rchn1qmainaddress0", "your Backbone wallet");

    // Validate says the same, and that an address is of the paying wallet (a send to oneself: allowed).
    nodes.valid["layers"].push_back("rsc1qlayersown00");
    const UniValue own{adapter.Validate(P(R"({"stem":"layers","address":"rsc1qlayersown00"})"))};
    BOOST_CHECK(own.find_value("own").isTrue());
    BOOST_CHECK_EQUAL(Code([&] { adapter.Validate(P(R"({"stem":"layers","address":"tth1qthunderonly"})")); }), ChainAdapter::WRONG_ADDRESS);
}

BOOST_AUTO_TEST_CASE(an_asset_send_only_of_an_asset_held)
{
    Nodes nodes;
    ChainAdapter adapter{nodes.Hooks()};
    const auto send{[&](const std::string& asset, const std::string& amount) {
        return adapter.Send(P(R"({"stem":"layers","address":"rsc1qlayersaddr0","asset":")" + asset + R"(","amount":")" + amount + R"("})"));
    }};
    send(ASSET, "25.5");
    BOOST_REQUIRE_EQUAL(nodes.calls.size(), 1U);
    const UniValue& c{nodes.calls[0].second};
    BOOST_CHECK_EQUAL(c.find_value("method").get_str(), "rpc.confirmed");
    BOOST_CHECK_EQUAL(c.find_value("params").find_value("method").get_str(), "sendasset");
    BOOST_CHECK_EQUAL(c.find_value("params").find_value("params").write(), R"(["rsc1qlayersaddr0","0x)" + ASSET + R"(","25.50"])");
    BOOST_CHECK_EQUAL(c.find_value("params").find_value("wallet").get_str(), "main");
    nodes.calls.clear();
    BOOST_CHECK_EQUAL(Code([&] { send(std::string(64, 'e'), "1"); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK(Message([&] { send(ASSET, "250.51"); }).find("More than the wallet holds") != std::string::npos);
    BOOST_CHECK_EQUAL(Code([&] { send(ASSET, "1.234"); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(Code([&] { send("GOLD", "1"); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK(nodes.calls.empty());
    // A chain without assets.
    BOOST_CHECK_EQUAL(Code([&] { adapter.Send(P(R"({"stem":"thunder","address":"rsc1qlayersaddr0","asset":")" + ASSET + R"(","amount":"1"})")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(Code([&] { adapter.Send(P(R"({"stem":"layers","address":"rsc1qlayersaddr0","shielded":true,"amount":"1"})")); }), BRIDGE_INVALID_REQUEST);
}

BOOST_AUTO_TEST_CASE(withdrawals_deposits_receive)
{
    Nodes nodes;
    ChainAdapter adapter{nodes.Hooks()};
    adapter.Withdraw(P(R"({"stem":"layers","address":"rchn1qmainaddress0","amount":"2"})"));
    BOOST_REQUIRE_EQUAL(nodes.calls.size(), 1U);
    BOOST_CHECK_EQUAL(nodes.calls[0].second.find_value("params").find_value("method").get_str(), "createwithdrawal");
    BOOST_CHECK_EQUAL(nodes.calls[0].second.find_value("params").find_value("params").write(), R"(["rchn1qmainaddress0","2.00000000","0.00010000"])");
    nodes.calls.clear();
    BOOST_CHECK_EQUAL(Code([&] { adapter.Withdraw(P(R"({"stem":"layers","address":"rsc1qlayersaddr0","amount":"2"})")); }), ChainAdapter::WRONG_ADDRESS);
    BOOST_CHECK_EQUAL(Code([&] { adapter.Withdraw(P(R"({"stem":"layers","address":"s7_rsc1qlayersown00_abcdef","amount":"2"})")); }), ChainAdapter::WRONG_ADDRESS);
    BOOST_CHECK(nodes.calls.empty());

    const UniValue dep{adapter.Deposit(P(R"({"stem":"layers","amount":"20"})"))};
    BOOST_REQUIRE_EQUAL(nodes.deposits.size(), 1U);
    BOOST_CHECK_EQUAL(nodes.deposits[0].first, "layers");
    BOOST_CHECK_EQUAL(nodes.deposits[0].second.find_value("destination").get_str(), "s7_rsc1qlayersown00_abcdef");
    BOOST_CHECK_EQUAL(nodes.deposits[0].second.find_value("amount").get_str(), "20.00000000");
    BOOST_CHECK_EQUAL(dep.find_value("txid").get_str(), TXID);
    BOOST_CHECK_EQUAL(Code([&] { adapter.Deposit(P(R"({"stem":"layers","amount":"0"})")); }), BRIDGE_INVALID_REQUEST);

    const UniValue rcv{adapter.Receive(P(R"({"stem":"layers","label":"from Bob"})"))};
    BOOST_CHECK_EQUAL(rcv.find_value("address").get_str(), "rsc1qnewaddress0");
    BOOST_CHECK_EQUAL(nodes.calls.back().second.find_value("method").get_str(), "wallet.newAddress");
    BOOST_CHECK_EQUAL(nodes.calls.back().second.find_value("params").find_value("label").get_str(), "from Bob");
    BOOST_CHECK_EQUAL(adapter.DepositAddress(P(R"({"stem":"layers"})")).find_value("depositaddress").get_str(), "s7_rsc1qlayersown00_abcdef");

    const UniValue peg{adapter.Peg(P(R"({"stem":"layers"})"))};
    BOOST_CHECK(peg.find_value("withdrawals")[0].find_value("mine").isTrue());
    // Amounts as their exact text, counts as numbers.
    BOOST_CHECK_EQUAL(peg.find_value("withdrawals")[0].find_value("amount").get_str(), "1.25000000");
    BOOST_CHECK(peg.find_value("withdrawals")[0].find_value("vout").isNum());
    BOOST_CHECK(!peg.find_value("withdrawals")[1].find_value("mine").isTrue());

    // Wallet names that go in a path: only plain ones.
    BOOST_CHECK_EQUAL(Code([&] { adapter.WalletCreate(P(R"({"stem":"layers","name":"../x"})")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(Code([&] { adapter.WalletLoad(P(R"({"stem":"layers","name":""})")); }), BRIDGE_INVALID_REQUEST);
    adapter.WalletCreate(P(R"({"stem":"layers","name":"savings"})"));
    BOOST_CHECK_EQUAL(nodes.rpcs.back().method, "createwallet");
    BOOST_CHECK_EQUAL(nodes.rpcs.back().params[0].get_str(), "savings");
}

BOOST_AUTO_TEST_SUITE_END()
