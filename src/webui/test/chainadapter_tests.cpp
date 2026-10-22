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
#include <tinyformat.h>

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
const std::string MARKET(64, 'e');
//! The DEX's stand-ins: a second asset, the coin (all zeros), two pools.
const std::string SILV(64, '5');
const std::string COINID(64, '0');
const std::string POOL1(64, '7');
const std::string POOL2(64, '8');
const std::string POOL3(64, '9');

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
    //! The app's own rows of each question (call_rows).
    std::vector<UniValue> app_rows;
    //! The addresses valid on each chain ("chains": the app's own), and those of a wallet of each.
    std::map<std::string, std::vector<std::string>> valid{{"chains", {"rchn1qmainaddress0"}}, {"layers", {"rsc1qlayersaddr0", "rsc1qthunderown0", "rsc1qlayersown00"}}, {"thunder", {"rsc1qlayersaddr0", "rsc1qthunderown0", "rsc1qlayersown00"}}};
    std::map<std::string, std::vector<std::string>> mine{{"chains", {}}, {"layers", {"rsc1qlayersown00"}}, {"thunder", {"rsc1qthunderown0"}}, {"bitnames", {"rsc1qbnowner00"}}};
    std::vector<std::string> up{"layers", "thunder"};
    //! What a read through the host answers instead (a question: never asked).
    bool read_asks{false};
    //! The commit of a send is declined in the dialog.
    bool decline{false};
    //! getbalances as the hub's Layers answered it (coins spent by transactions not in the mempool).
    bool nonmempool{false};
    //! The node's pages' policy offers Layers' route reads (an older node: "does not call").
    bool routes_offered{true};
    //! A second pool of GOLD and CHN (stable, 0.01 %): the pair alone no longer says which.
    bool second_pool{false};
    //! The node says its pool kinds apply (dex-curves).
    bool curves_active{false};
    //! What the chain's host gives for a logo.
    std::string logo{R"({"asset":")" + ASSET + R"(","hash":")" + std::string(64, 'f') + R"(","mime":"image/png","size":4,"url":"data:image/png;base64,iVBORw0KGgo="})"};

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
            result = Json(R"({"assets":[{"asset":")" + ASSET + R"(","label":"GOLD","decimals":2,"balance":250.5,"pending":0,"control":true,"supply":100000}],)"
                          R"("liquidity":[{"pool":")" + POOL1 + R"(","asset_a":"GOLD","asset_b":"CHN","asset_a_id":")" + ASSET + R"(","asset_b_id":")" + COINID + R"(","shares":1000,"percent":2.5000,"value_a":250.00,"value_b":0.50000000},{"pool":"bad","shares":1}]})");
        } else if (method == "getasset") {
            const std::string id{p[0].get_str()};
            if (id == "0x" + ASSET) result = Json(R"({"asset":")" + ASSET + R"(","label":"GOLD","decimals":2})");
            else if (id == "0x" + SILV) result = Json(R"({"asset":")" + SILV + R"(","label":"SILV","decimals":4})");
            else throw std::runtime_error{"No such asset is registered"};
        } else if (method == "listpools") {
            const std::string gold_chn{R"({"pool":")" + POOL1 + R"(","asset_a":"GOLD","asset_b":"CHN","asset_a_id":")" + ASSET + R"(","asset_b_id":")" + COINID + R"(","reserve_a":10000.00,"reserve_b":20.00000000,"price":0.002,"shares":447213,"volume_a":500.00,"volume_b":1.00000000,"swaps":7,"created":12,"abandoned":false})"};
            const std::string chn_gold{R"({"pool":")" + POOL1 + R"(","asset_a":"CHN","asset_b":"GOLD","asset_a_id":")" + COINID + R"(","asset_b_id":")" + ASSET + R"(","reserve_a":20.00000000,"reserve_b":10000.00,"price":500,"shares":447213,"volume_a":1.00000000,"volume_b":500.00,"swaps":7,"created":12,"abandoned":false})"};
            const std::string chn_silv{R"({"pool":")" + POOL2 + R"(","asset_a":"CHN","asset_b":"SILV","asset_a_id":")" + COINID + R"(","asset_b_id":")" + SILV + R"(","reserve_a":20.00000000,"reserve_b":5000.0000,"price":250,"shares":10000,"volume_a":0,"volume_b":0,"swaps":0,"created":15,"abandoned":false,"curve":"stable","fee":0.05,"fee_millionths":500,"amp":100,"default_kind":false})"};
            const std::string silv_chn{R"({"pool":")" + POOL2 + R"(","asset_a":"SILV","asset_b":"CHN","asset_a_id":")" + SILV + R"(","asset_b_id":")" + COINID + R"(","reserve_a":5000.0000,"reserve_b":20.00000000,"shares":10000,"curve":"stable","fee":0.05,"fee_millionths":500,"amp":100,"default_kind":false})"};
            // A pool whose fee is no tier: never shown (as another fee, or at all).
            const std::string odd{R"({"pool":")" + std::string(64, '6') + R"(","asset_a":"GOLD","asset_b":"CHN","asset_a_id":")" + ASSET + R"(","asset_b_id":")" + COINID + R"(","reserve_a":1.00,"reserve_b":1,"curve":"constant_product","fee":0.25,"fee_millionths":2500})"};
            const std::string gold_chn2{R"({"pool":")" + POOL3 + R"(","asset_a":"GOLD","asset_b":"CHN","asset_a_id":")" + ASSET + R"(","asset_b_id":")" + COINID + R"(","reserve_a":50.00,"reserve_b":1,"curve":"stable","fee":0.01,"fee_millionths":100,"amp":50,"default_kind":false})"};
            const std::string more{second_pool ? "," + gold_chn2 : std::string{}};
            if (p.size() > 2 && p[2].isNum() && p[2].getInt<int>() > 0) result = UniValue{UniValue::VARR};
            else if (p[0].isNull()) result = Json("[" + gold_chn + "," + silv_chn + "," + odd + R"(,{"pool":"nothex"}])");
            else if (p[0].get_str() == "0x" + ASSET) result = Json("[" + gold_chn + "," + odd + more + "]");
            else if (p[0].get_str() == "CHN") result = Json("[" + chn_gold + "," + chn_silv + "]");
            else result = Json("[" + silv_chn + "]");
        } else if (method == "getpool") {
            // By id (a node with pool kinds); an older one takes two assets only.
            if (p.size() != 1 || !curves_active) throw std::runtime_error{"Missing required argument asset_b"};
            const std::string id{p[0].get_str()};
            if (id == POOL1) result = Json(R"({"pool":")" + POOL1 + R"(","asset_a":"GOLD","asset_b":"CHN","asset_a_id":")" + ASSET + R"(","asset_b_id":")" + COINID + R"(","reserve_a":10000.00,"reserve_b":20.00000000,"shares":447213,"curve":"constant_product","fee":0.3,"fee_millionths":3000,"default_kind":true})");
            else if (id == POOL2) result = Json(R"({"pool":")" + POOL2 + R"(","asset_a":"SILV","asset_b":"CHN","asset_a_id":")" + SILV + R"(","asset_b_id":")" + COINID + R"(","reserve_a":5000.0000,"reserve_b":20.00000000,"shares":10000,"curve":"stable","fee":0.05,"fee_millionths":500,"amp":100,"default_kind":false})");
            else if (id == POOL3) result = Json(R"({"pool":")" + POOL3 + R"(","asset_a":"GOLD","asset_b":"CHN","asset_a_id":")" + ASSET + R"(","asset_b_id":")" + COINID + R"(","reserve_a":50.00,"reserve_b":1,"curve":"stable","fee":0.01,"fee_millionths":100,"amp":50,"default_kind":false})");
            else throw std::runtime_error{"No such pool"};
        } else if (method == "previewpool") {
            result = Json(R"({"pool":")" + std::string(64, '4') + R"(","exists":false,"open":false,"shares":1000})");
        } else if (method == "getdexinfo") {
            if (!routes_offered) throw std::runtime_error{"The interface does not call getdexinfo (use the console)"};
            result = Json(curves_active ? R"({"active":true,"max_route_hops":4,"curves_active":true})" : R"({"active":true,"max_route_hops":4})");
        } else if (method == "quoteroute") {
            if (!routes_offered) throw std::runtime_error{"The interface does not call quoteroute (use the console)"};
            if (p[0].get_str() == "0x" + ASSET && p[2].get_str() == "0x" + SILV) {
                result = Json(R"({"amount_in":10.00,"amount_out":12.3456,"path":["GOLD","CHN","SILV"],"routes":[{"path":["GOLD","CHN","SILV"],"path_ids":[")" + ASSET + R"(",")" + COINID + R"(",")" + SILV +
                              R"("],"amount_out":12.3456,"hops":[{"pool":")" + POOL1 + R"(","amount_out":0.01990000},{"pool":")" + POOL2 + R"(","amount_out":12.3456}]}]})");
            } else if (p[0].get_str() == "0x" + ASSET && p[2].get_str() == "CHN") {
                result = Json(R"({"amount_out":0.01990000,"routes":[{"path":["GOLD","CHN"],"path_ids":[")" + ASSET + R"(",")" + COINID + R"("],"amount_out":0.01990000,"hops":[{"pool":")" + POOL1 + R"(","amount_out":0.01990000}]}]})");
            } else {
                result = Json(R"({"amount_out":0,"routes":[]})");
            }
        } else if (method == "quoteswap") {
            if (p[0].get_str() == "0x" + ASSET && p[2].get_str() == "CHN") result = Json(R"({"amount_in":10.00,"amount_out":0.01990000,"price_impact":0.3988,"fee":0.03})");
            else throw std::runtime_error{"There is no pool of these assets"};
        } else if (method == "getpooltwap") {
            if (p[0].get_str() != POOL1) throw std::runtime_error{"The pool has no observations yet: a block has to change it under the DEX's rules first"};
            // The price rose by one part in a thousand a second, from 0.002 at 800: the average over the last s seconds.
            const int64_t s{p[2].getInt<int64_t>()};
            const double avg{0.002 * (1 + 0.001 * (1200 - s / 2.0))};
            result = Json(R"({"to_time":2000,"oldest_time":800,"spot_a":0.0044,"asset_a":"GOLD","twap_a":)" + strprintf("%.12f", avg) + "}");
        } else if (method == "listwithdrawals") {
            result = Json(R"([{"txid":")" + TXID + R"(","vout":0,"amount":1.25000000,"refundaddress":"rsc1qlayersown00","status":"waiting"},{"txid":")" + TXID + R"(","vout":1,"amount":3,"refundaddress":"rsc1qsomeoneelse","status":"bundled"}])");
        } else if (method == "getwalletpositions") {
            result = Json(R"([{"market":")" + MARKET + R"(","account":"rsc1qhmown0","state":1,"shares":12.5},{"market":"zz","state":0,"shares":1}])");
        } else if (method == "listmarkets") {
            result = Json(R"([{"id":")" + MARKET + R"(","text":"Rain tomorrow?","status":"trading","states":[{"state":0,"answers":"N","price":0.25},{"state":1,"answers":"Y","price":0.75000000}]}])");
        } else if (method == "getrawmempool") {
            result = Json(R"({")" + std::string(64, '1') + R"(":{"time":5,"vsize":141,"fees":{"base":0.00000141}},")" + std::string(64, '2') + R"(":{"time":9,"vsize":200,"fees":{"base":0.0000025}},"bad":{"time":99}})");
        } else if (method == "listunspent") {
            // Two coins of the wallet, one of no value (an asset's: never offered), one not spendable.
            result = Json(R"([{"txid":")" + TXID + R"(","vout":0,"amount":1.5,"confirmations":4,"address":"rsc1qlayersown00","label":"","spendable":true,"safe":true},)"
                          R"({"txid":")" + std::string(64, 'd') + R"(","vout":2,"amount":20.00000000,"confirmations":9,"address":"rsc1qlayersown00","label":"Savings","spendable":true,"safe":true},)"
                          R"({"txid":")" + std::string(64, 'e') + R"(","vout":1,"amount":0,"confirmations":9,"spendable":true},)"
                          R"({"txid":")" + std::string(64, 'f') + R"(","vout":0,"amount":3,"confirmations":9,"spendable":false}])");
        } else if (method == "bitassets.logo") {
            result = Json(logo);
        } else if (method == "getname") {
            // A node that answers a name with a mark that turns text around, and markup for its number.
            if (stem == "bitnames" && p[0].get_str() == "evil") {
                result = Json(R"({"namehash":")" + std::string(64, 'e') + "\",\"name\":\"\u202eevil\",\"seq\":\"0001-0002\",\"owner\":{\"address\":\"rsc1q\"}}");
                out.pushKV("result", result);
                return out;
            }
            if (stem == "bitnames" && p[0].get_str() == "odd") {
                result = Json(R"({"namehash":")" + std::string(64, 'f') + R"(","name":"odd","seq":"<b>1</b>","owner":{"address":"rsc1q <script>"}})");
                out.pushKV("result", result);
                return out;
            }
            if (stem != "bitnames" || (p[0].get_str() != "alice" && p[0].get_str() != "#0001-0001")) throw std::runtime_error{"No such name is registered"};
            result = Json(R"({"namehash":")" + std::string(64, 'a') + R"(","name":"alice","seq":"0001-0001","registered":12,"owner":{"txid":")" + TXID + R"(","vout":0,"address":"rsc1qbnowner00"}})");
        } else if (method == "listmynames") {
            result = Json(R"([{"kind":"name","name":"alice","namehash":")" + std::string(64, 'a') + R"(","seq":"0001-0001","address":"rsc1qbnowner00","txid":")" + TXID + R"(","vout":0},{"kind":"reservation","address":"rsc1qbnowner00","txid":")" + TXID + R"(","vout":1},{"kind":"odd"}])");
        } else if (method == "estimateshieldedfee") {
            result = Json(R"({"fee":0.00012345,"notes":1,"total":1})");
        } else if (method == "getshieldedbalance") {
            result = Json(R"({"available":3.00000000,"spendable":2.50000000,"pending":0.5,"notes":2})");
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
            if (method == "getbalances") return Json(nonmempool ? R"({"mine":{"trusted":6161.54,"untrusted_pending":0,"immature":0,"nonmempool":-5171.23}})" : R"({"mine":{"trusted":12.34567891,"untrusted_pending":0.1,"immature":0}})");
            if (method == "getblockcount") return UniValue{42};
            if (method == "getblockhash") return UniValue{params[0].getInt<int>() == 39 ? std::string{"nothex"} : strprintf("%064x", params[0].getInt<int>())};
            if (method == "getblockheader") return Json(R"({"time":1000,"nTx":3})");
            if (method == "getblock") {
                if (params[0].get_str() != std::string(64, 'b')) throw std::runtime_error{"Block not found"};
                return Json(R"({"hash":")" + std::string(64, 'B') + R"(","height":41,"time":1000,"confirmations":2,"size":300,"weight":1200,"nTx":2,"difficulty":0.5,"merkleroot":"nothex","previousblockhash":")" + std::string(64, 'a') + R"(","tx":[")" + std::string(64, 'c') + R"(","nothex",")" + TXID + R"("],"extra":{"a":1}})");
            }
            if (method == "getmainchaininfo") return Json(R"({"slot":7,"connected":true,"state":"ok","height":300})");
            if (method == "getdepositaddress") return Json(R"({"depositaddress":"s7_rsc1qlayersown00_abcdef","address":"rsc1qlayersown00","slot":7})");
            if (method == "listtransactions") {
                // A block of stake's reward that paid nothing (its maker's output of no value): never a row; one that paid fees is.
                return Json(R"([{"txid":")" + TXID + R"(","category":"receive","amount":20.00000000,"time":100,"confirmations":3},{"txid":"nothex","amount":1},)"
                            R"({"txid":")" + std::string(64, '3') + R"(","category":"generate","amount":0.00000000,"vout":1,"label":"validator payout","time":150,"confirmations":5},)"
                            R"({"txid":")" + std::string(64, 'd') + R"(","category":"send","amount":-1.50000001,"fee":-0.00000141,"time":200,"confirmations":0}])");
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
        h.call_rows = [this](const std::string& stem, const UniValue& request, const UniValue& rows) -> UniValue {
            app_rows.push_back(rows);
            return calls.emplace_back(stem, request), Json(R"({"txid":")" + TXID + R"("})");
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
    BOOST_CHECK_EQUAL(nodes.rpcs.back().params[1].getInt<int>(), 200);
    // A whole history for the Transactions page: up to 5 000 rows asked of the node, no more.
    adapter.Transactions(P(R"({"stem":"layers","count":5000})"));
    BOOST_CHECK_EQUAL(nodes.rpcs.back().params[1].getInt<int>(), 5000);
    adapter.Transactions(P(R"({"stem":"layers","count":99999})"));
    BOOST_CHECK_EQUAL(nodes.rpcs.back().params[1].getInt<int>(), 5000);

    const UniValue staking{adapter.Staking(P(R"({"stem":"layers"})"))};
    BOOST_CHECK(staking.find_value("node").find_value("validator").isTrue());
    BOOST_CHECK_EQUAL(Code([&] { adapter.Staking(P(R"({"stem":"thunder"})")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(Code([&] { adapter.Assets(P(R"({"stem":"thunder"})")); }), BRIDGE_INVALID_REQUEST);
}

BOOST_AUTO_TEST_CASE(available_as_the_chains_own_getbalance_counts_it)
{
    // The hub's Layers: coins spent by transactions not in the mempool are still in "trusted", their sum negative in
    // "nonmempool"; getbalance says 990.31, never 6161.54.
    const UniValue b{ChainAdapter::WalletBalances(Json(R"({"trusted":6161.54000000,"untrusted_pending":0.5,"immature":2,"nonmempool":-5171.23000000})"))};
    BOOST_CHECK_EQUAL(b.find_value("trusted").get_str(), "990.31000000");
    BOOST_CHECK_EQUAL(b.find_value("pending").get_str(), "0.50000000");
    BOOST_CHECK_EQUAL(b.find_value("immature").get_str(), "2.00000000");
    // No such field (an older node): trusted as it is; never below zero.
    BOOST_CHECK_EQUAL(ChainAdapter::WalletBalances(Json(R"({"trusted":1.5,"untrusted_pending":0,"immature":0})")).find_value("trusted").get_str(), "1.50000000");
    BOOST_CHECK_EQUAL(ChainAdapter::WalletBalances(Json(R"({"trusted":1,"nonmempool":-3})")).find_value("trusted").get_str(), "0.00000000");
    // The summary and the wallets list use it.
    Nodes nodes;
    ChainAdapter adapter{nodes.Hooks()};
    nodes.nonmempool = true;
    BOOST_CHECK_EQUAL(adapter.Summary(P(R"({"stem":"layers"})")).find_value("balances").find_value("trusted").get_str(), "990.31000000");
    const UniValue w{adapter.Wallets(P(R"({"stem":"layers"})"))};
    BOOST_CHECK_EQUAL(w.find_value("wallets")[0].find_value("trusted").get_str(), "990.31000000");
}

BOOST_AUTO_TEST_CASE(positions_of_the_wallet_with_their_market)
{
    Nodes nodes;
    nodes.up = {"layers", "thunder", "hivemind"};
    ChainAdapter adapter{nodes.Hooks()};
    const UniValue out{adapter.Positions(P(R"({"stem":"hivemind"})"))};
    const UniValue& rows{out.find_value("positions")};
    // The row with a market that is not one is dropped.
    BOOST_REQUIRE_EQUAL(rows.size(), 1U);
    BOOST_CHECK_EQUAL(rows[0].find_value("text").get_str(), "Rain tomorrow?");
    BOOST_CHECK_EQUAL(rows[0].find_value("answers").get_str(), "Y");
    BOOST_CHECK_EQUAL(rows[0].find_value("price").get_str(), "0.75000000");
    BOOST_CHECK_EQUAL(rows[0].find_value("shares").get_str(), "12.5");
    BOOST_CHECK_EQUAL(rows[0].find_value("status").get_str(), "trading");
    // Reads of the chain's own pages' policy, with the wallet; never on a chain without markets.
    BOOST_CHECK(std::ranges::any_of(nodes.rpcs, [](const auto& r) { return r.method == "webuihost" && r.params[0].find_value("method").get_str() == "getwalletpositions" && r.params[0].find_value("wallet").get_str() == "main"; }));
    BOOST_CHECK_EQUAL(Code([&] { adapter.Positions(P(R"({"stem":"layers"})")); }), BRIDGE_INVALID_REQUEST);
    nodes.read_asks = true;
    BOOST_CHECK_EQUAL(Code([&] { adapter.Positions(P(R"({"stem":"hivemind"})")); }), BRIDGE_INVALID_REQUEST);
}

BOOST_AUTO_TEST_CASE(activity_of_a_chain_its_blocks_and_mempool)
{
    Nodes nodes;
    ChainAdapter adapter{nodes.Hooks()};
    const UniValue a{adapter.Activity(P(R"({"stem":"thunder"})"))};
    BOOST_CHECK_EQUAL(a.find_value("height").getInt<int>(), 42);
    // 42, 41, 40; 39's hash is not one: the list stops there.
    BOOST_REQUIRE_EQUAL(a.find_value("blocks").size(), 3U);
    BOOST_CHECK_EQUAL(a.find_value("blocks")[0].find_value("txs").getInt<int>(), 3);
    const UniValue& rows{a.find_value("mempool").find_value("rows")};
    BOOST_REQUIRE_EQUAL(rows.size(), 2U);
    BOOST_CHECK_EQUAL(rows[0].find_value("txid").get_str(), std::string(64, '2'));
    BOOST_CHECK_EQUAL(rows[0].find_value("fee").get_str(), "0.0000025");
    // As many as asked, 1 to 30 (the explorer's strips): 2 of them, and never past the list's end.
    BOOST_CHECK_EQUAL(adapter.Activity(P(R"({"stem":"thunder","count":2})")).find_value("blocks").size(), 2U);
    BOOST_CHECK_EQUAL(adapter.Activity(P(R"({"stem":"thunder","count":0})")).find_value("blocks").size(), 1U);
    BOOST_CHECK_EQUAL(adapter.Activity(P(R"({"stem":"thunder","count":1000})")).find_value("blocks").size(), 3U);
    nodes.up = {"layers"};
    BOOST_CHECK_EQUAL(Code([&] { adapter.Activity(P(R"({"stem":"thunder"})")); }), BRIDGE_NOT_READY);
}

BOOST_AUTO_TEST_CASE(a_block_of_a_sidechain_its_facts_and_transactions)
{
    Nodes nodes;
    ChainAdapter adapter{nodes.Hooks()};
    const UniValue b{adapter.Block(P(R"({"stem":"thunder","hash":")" + std::string(64, 'b') + R"("})"))};
    BOOST_CHECK_EQUAL(b.find_value("height").getInt<int>(), 41);
    BOOST_CHECK_EQUAL(b.find_value("hash").get_str(), std::string(64, 'b'));
    BOOST_CHECK_EQUAL(b.find_value("previousblockhash").get_str(), std::string(64, 'a'));
    // What is not a hash is left out; nothing but what a page shows.
    BOOST_CHECK(b.find_value("merkleroot").isNull());
    BOOST_CHECK(b.find_value("extra").isNull());
    BOOST_REQUIRE_EQUAL(b.find_value("tx").size(), 2U);
    BOOST_CHECK_EQUAL(b.find_value("tx")[0].get_str(), std::string(64, 'c'));
    BOOST_CHECK_EQUAL(Code([&] { adapter.Block(P(R"({"stem":"thunder","hash":"nothex"})")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(Code([&] { adapter.Block(P(R"({"stem":"thunder","hash":")" + std::string(64, 'e') + R"("})")); }), BRIDGE_INVALID_REQUEST);
    nodes.up = {"layers"};
    BOOST_CHECK_EQUAL(Code([&] { adapter.Block(P(R"({"stem":"thunder","hash":")" + std::string(64, 'b') + R"("})")); }), BRIDGE_NOT_READY);
}

BOOST_AUTO_TEST_CASE(asset_logos_only_as_png_or_jpeg_data_urls)
{
    BOOST_CHECK(ChainAdapter::IsLogoUrl("data:image/png;base64,iVBORw0KGgo="));
    BOOST_CHECK(ChainAdapter::IsLogoUrl("data:image/jpeg;base64,/9j/4AAQ"));
    BOOST_CHECK(!ChainAdapter::IsLogoUrl("data:image/svg+xml;base64,PHN2Zz4="));
    BOOST_CHECK(!ChainAdapter::IsLogoUrl("data:text/html;base64,PGI+"));
    BOOST_CHECK(!ChainAdapter::IsLogoUrl("data:image/png;base64,\"><script>"));
    BOOST_CHECK(!ChainAdapter::IsLogoUrl("data:image/png;base64,AAA==="));
    BOOST_CHECK(!ChainAdapter::IsLogoUrl("https://example.com/a.png"));
    BOOST_CHECK(!ChainAdapter::IsLogoUrl("data:image/png;base64," + std::string(45 * 1024, 'A')));
    Nodes nodes;
    ChainAdapter adapter{nodes.Hooks()};
    const UniValue got{adapter.AssetLogo(P(R"({"stem":"layers","asset":")" + ASSET + R"("})"))};
    BOOST_CHECK_EQUAL(got.find_value("url").get_str(), "data:image/png;base64,iVBORw0KGgo=");
    BOOST_CHECK_EQUAL(got.find_value("mime").get_str(), "image/png");
    // Asked of the chain's own host (the logo its pages show), by the asset's hash only.
    BOOST_CHECK_EQUAL(nodes.rpcs.back().params[0].find_value("method").get_str(), "bitassets.logo");
    BOOST_CHECK_EQUAL(nodes.rpcs.back().params[0].find_value("kind").get_str(), "host");
    // Something else than a picture, or of another asset: nothing.
    nodes.logo = R"({"asset":")" + ASSET + R"(","url":"data:image/svg+xml;base64,PHN2Zz4="})";
    BOOST_CHECK(adapter.AssetLogo(P(R"({"stem":"layers","asset":")" + ASSET + R"("})")).isNull());
    nodes.logo = R"({"asset":")" + std::string(64, 'a') + R"(","url":"data:image/png;base64,iVBORw0KGgo="})";
    BOOST_CHECK(adapter.AssetLogo(P(R"({"stem":"layers","asset":")" + ASSET + R"("})")).isNull());
    nodes.logo = "null";
    BOOST_CHECK(adapter.AssetLogo(P(R"({"stem":"layers","asset":")" + ASSET + R"("})")).isNull());
    BOOST_CHECK_EQUAL(Code([&] { adapter.AssetLogo(P(R"({"stem":"layers","asset":"GOLD"})")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(Code([&] { adapter.AssetLogo(P(R"({"stem":"thunder","asset":")" + ASSET + R"("})")); }), BRIDGE_INVALID_REQUEST);
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

BOOST_AUTO_TEST_CASE(names_found_and_held_and_private_coins_moved)
{
    // What a search text may be as a name (BitNames): its text, a subname, its domain, its number; never digits alone.
    for (const char* q : {"alice", "blog.core", "core.x", "#1739-0029", "a-b_c", "x1"}) BOOST_CHECK_MESSAGE(ChainAdapter::IsNameText(q), q);
    for (const char* q : {"12345", "Alice", "a..b", ".a", "a.", "a b", "#1739-002", "<img>", ""}) BOOST_CHECK_MESSAGE(!ChainAdapter::IsNameText(q), q);
    BOOST_CHECK(!ChainAdapter::IsNameText(std::string(64, 'a')));
    BOOST_CHECK(ChainAdapter::IsNameText(std::string(63, 'a')));
    BOOST_CHECK_EQUAL(ChainAdapter::NameOf("core.x"), "core");
    BOOST_CHECK_EQUAL(ChainAdapter::NameOf("alice"), "alice");

    Nodes nodes;
    nodes.up = {"layers", "thunder", "bitnames", "zside"};
    ChainAdapter a{nodes.Hooks()};
    // A name found on BitNames by the chain's own getname: its number, its owner, the wallet's.
    const UniValue found{a.Search(P(R"({"stem":"bitnames","q":"alice"})")).find_value("results")};
    BOOST_REQUIRE_EQUAL(found.size(), 1U);
    BOOST_CHECK_EQUAL(found[0].find_value("kind").get_str(), "name");
    BOOST_CHECK_EQUAL(found[0].find_value("name").get_str(), "alice");
    BOOST_CHECK_EQUAL(found[0].find_value("seq").get_str(), "0001-0001");
    BOOST_CHECK_EQUAL(found[0].find_value("owner").get_str(), "rsc1qbnowner00");
    BOOST_CHECK(found[0].find_value("mine").isTrue());
    BOOST_CHECK_EQUAL(a.Search(P(R"({"stem":"bitnames","q":"alice.x"})")).find_value("results").size(), 1U);
    // A node's text that is not a name's is not shown: a name turning text around is dropped; a number or an owner that are
    // not one are left out.
    BOOST_CHECK(a.Search(P(R"({"stem":"bitnames","q":"evil"})")).find_value("results").empty());
    const UniValue odd{a.Search(P(R"({"stem":"bitnames","q":"odd"})")).find_value("results")};
    BOOST_REQUIRE_EQUAL(odd.size(), 1U);
    BOOST_CHECK_EQUAL(odd[0].find_value("seq").get_str(), "");
    BOOST_CHECK_EQUAL(odd[0].find_value("owner").get_str(), "");
    // Not a name there: nothing; a chain without names is not asked.
    BOOST_CHECK(a.Search(P(R"({"stem":"bitnames","q":"bob"})")).find_value("results").empty());
    nodes.rpcs.clear();
    BOOST_CHECK(a.Search(P(R"({"stem":"layers","q":"alice"})")).find_value("results").empty());
    BOOST_CHECK(std::none_of(nodes.rpcs.begin(), nodes.rpcs.end(), [](const auto& r) { return r.method == "webuihost" && r.params[0].find_value("method").get_str() == "getname"; }));
    // Held: the names of the wallet and its reservations, on Home.
    const UniValue held{a.Summary(P(R"({"stem":"bitnames"})")).find_value("names")};
    BOOST_CHECK_EQUAL(held.find_value("count").getInt<int>(), 1);
    BOOST_CHECK_EQUAL(held.find_value("reservations").getInt<int>(), 1);
    BOOST_CHECK(a.Summary(P(R"({"stem":"layers"})")).find_value("names").isNull());
    // zSide: its private coins beside its coins, and coins moved between the two, its own question.
    const UniValue z{a.Summary(P(R"({"stem":"zside"})"))};
    BOOST_CHECK_EQUAL(z.find_value("shielded").find_value("spendable").get_str(), "2.50000000");
    nodes.calls.clear();
    a.Shield(P(R"({"stem":"zside","direction":"shield","amount":"1.5"})"));
    a.Shield(P(R"({"stem":"zside","direction":"unshield","amount":"0.25"})"));
    BOOST_REQUIRE_EQUAL(nodes.calls.size(), 3U);
    BOOST_CHECK_EQUAL(nodes.calls[0].second.write(), R"({"kind":"host","method":"rpc.confirmed","params":{"method":"shieldcoins","params":["1.50000000"],"wallet":"main"}})");
    // Unshielded to a new address of the wallet, its fee the chain's estimate for one (its question shows what is paid).
    BOOST_CHECK_EQUAL(nodes.calls[1].second.find_value("method").get_str(), "wallet.newAddress");
    BOOST_CHECK_EQUAL(nodes.calls[2].second.write(), R"({"kind":"host","method":"rpc.confirmed","params":{"method":"unshieldcoins","params":["0.25000000",null,false,"0.00012345"],"wallet":"main"}})");
    // A private payment: its fee given as the chain estimates it, the memo with it.
    nodes.calls.clear();
    a.Send(P(R"({"stem":"zside","address":"zs1qprivateaddress0000","amount":"0.5","shielded":true,"memo":"For the coffee"})"));
    BOOST_REQUIRE_EQUAL(nodes.calls.size(), 1U);
    BOOST_CHECK_EQUAL(nodes.calls[0].second.write(), R"({"kind":"host","method":"rpc.confirmed","params":{"method":"sendshielded","params":[{"zs1qprivateaddress0000":"0.50000000"},"For the coffee","0.00012345"],"wallet":"main"}})");
    // Refused before anything is asked: a chain without a shielded account, another direction, an amount not exact.
    for (const char* bad : {R"({"stem":"layers","direction":"shield","amount":"1"})", R"({"stem":"zside","direction":"sideways","amount":"1"})", R"({"stem":"zside","direction":"shield","amount":"1e3"})",
                            R"({"stem":"zside","direction":"shield","amount":"-1"})", R"({"stem":"zside","direction":"shield","amount":"0.000000001"})", R"({"stem":"zside","direction":"shield","amount":"0"})"}) {
        BOOST_CHECK_MESSAGE(Code([&] { a.Shield(P(bad)); }) == BRIDGE_INVALID_REQUEST, bad);
    }
    BOOST_CHECK_EQUAL(nodes.calls.size(), 1U);
    // What the app uses there is in each daemon's whitelist for this app's user.
    const auto used{[](const char* stem, const char* m) { const auto v{ChainAdapter::MethodsUsed(stem)}; return std::ranges::find(v, m) != v.end(); }};
    BOOST_CHECK(used("zside", "shieldcoins") && used("zside", "unshieldcoins") && used("bitnames", "getname") && !used("layers", "getname"));
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

BOOST_AUTO_TEST_CASE(coin_control_only_coins_of_the_wallet)
{
    // §20.7: the coins a withdrawal or a deposit is paid with are named by {txid, vout} only, checked here before anything
    // is asked (only unspent coins of that wallet, holding coins), and passed to the node, which checks them again.
    BOOST_CHECK(ChainAdapter::ParseInputs(UniValue{}).isNull());
    for (const char* bad : {R"([])", R"("x")", R"([{"txid":"zz","vout":0}])", R"([{"txid":")" "BBBB" R"(","vout":0}])", R"([{"vout":0}])"}) {
        BOOST_CHECK_EQUAL(Code([&] { ChainAdapter::ParseInputs(Json(bad)); }), BRIDGE_INVALID_REQUEST);
    }
    const std::string coin{R"({"txid":")" + TXID + R"(","vout":0})"};
    BOOST_CHECK_EQUAL(Code([&] { ChainAdapter::ParseInputs(Json("[" + coin + "," + coin + "]")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(Code([&] { ChainAdapter::ParseInputs(Json(R"([{"txid":")" + TXID + R"(","vout":-1}])")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(Code([&] { ChainAdapter::ParseInputs(Json(R"([{"txid":")" + TXID + R"(","vout":1.5}])")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(Code([&] { ChainAdapter::ParseInputs(Json(R"([{"txid":")" + TXID + R"(","vout":4294967296}])")); }), BRIDGE_INVALID_REQUEST);
    UniValue many{UniValue::VARR};
    for (int i{0}; i < 501; ++i) many.push_back(Json(strprintf(R"({"txid":"%s","vout":%d})", TXID, i)));
    BOOST_CHECK_EQUAL(Code([&] { ChainAdapter::ParseInputs(many); }), BRIDGE_INVALID_REQUEST);

    Nodes nodes;
    ChainAdapter adapter{nodes.Hooks()};
    // The coins offered: spendable ones that hold coins, the largest first, amounts as exact text.
    const UniValue coins{adapter.Coins(P(R"({"stem":"layers"})")).find_value("coins")};
    BOOST_REQUIRE_EQUAL(coins.size(), 2U);
    BOOST_CHECK_EQUAL(coins[0].find_value("amount").get_str(), "20.00000000");
    BOOST_CHECK_EQUAL(coins[0].find_value("label").get_str(), "Savings");
    BOOST_CHECK_EQUAL(coins[1].find_value("amount").get_str(), "1.50000000");
    BOOST_CHECK_EQUAL(nodes.rpcs.back().method, "webuihost");

    // A withdrawal with two coins of the wallet: exactly them, after the amount and the fee.
    const std::string two{"[" + coin + R"(,{"txid":")" + std::string(64, 'd') + R"(","vout":2}])"};
    adapter.Withdraw(P(R"({"stem":"layers","address":"rchn1qmainaddress0","amount":"2","inputs":)" + two + "}"));
    BOOST_REQUIRE_EQUAL(nodes.calls.size(), 1U);
    BOOST_CHECK_EQUAL(nodes.calls[0].second.find_value("params").find_value("params").write(),
                      R"(["rchn1qmainaddress0","2.00000000","0.00010000",[{"txid":")" + TXID + R"(","vout":0},{"txid":")" + std::string(64, 'd') + R"(","vout":2}]])");
    nodes.calls.clear();
    // Not a coin of the wallet, a coin of no value (an asset's), one not spendable: refused, nothing asked.
    for (const std::string& stranger : {std::string(64, 'a'), std::string(64, 'e'), std::string(64, 'f')}) {
        const std::string one{R"([{"txid":")" + stranger + R"(","vout":)" + (stranger[0] == 'e' ? "1" : "0") + "}]"};
        const std::string said{Message([&] { adapter.Withdraw(P(R"({"stem":"layers","address":"rchn1qmainaddress0","amount":"2","inputs":)" + one + "}")); })};
        BOOST_CHECK_MESSAGE(said.find("is not an unspent coin of your Layers wallet") != std::string::npos, said);
    }
    BOOST_CHECK(nodes.calls.empty());

    // A deposit with chosen coins: passed to the deposit (whose hook checks them against this app's wallet).
    adapter.Deposit(P(R"({"stem":"layers","amount":"1","inputs":[)" + coin + "]}"));
    BOOST_REQUIRE_EQUAL(nodes.deposits.size(), 1U);
    BOOST_CHECK_EQUAL(nodes.deposits[0].second.find_value("inputs").write(), "[" + coin + "]");
    BOOST_CHECK_EQUAL(Code([&] { adapter.Deposit(P(R"({"stem":"layers","amount":"1","inputs":[]})")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(nodes.deposits.size(), 1U);

    // MissingCoin: the first coin named that is not listed.
    BOOST_CHECK_EQUAL(ChainAdapter::MissingCoin(Json("[" + coin + "]"), coins), "");
    BOOST_CHECK_EQUAL(ChainAdapter::MissingCoin(Json(R"([{"txid":")" + std::string(64, 'a') + R"(","vout":3}])"), coins), std::string(64, 'a') + ":3");
}


// ---- The DEX (doc/unified-app.md §22) ----

namespace {
//! The value of a row of the app's own in a question, by its label ("" when none).
std::string RowValue(const UniValue& rows, const std::string& label)
{
    for (const UniValue& r : rows.getValues()) {
        if (r.find_value("label").get_str() == label) return r.find_value("value").get_str();
    }
    return {};
}
const UniValue& Inner(const UniValue& request) { return request.find_value("params"); }
} // namespace

BOOST_AUTO_TEST_CASE(dex_capabilities_and_pure_parts)
{
    const UniValue layers{ChainAdapter::CapabilitiesJson(*ChainAdapter::Find("layers"))};
    const UniValue& dex{layers.find_value("dexFeatures")};
    BOOST_CHECK(dex.find_value("pair").isTrue() && dex.find_value("route").isTrue() && dex.find_value("twap").isTrue() && dex.find_value("curves").isTrue());
    BOOST_CHECK(dex.find_value("orders").isFalse() && dex.find_value("stats").isFalse());
    const UniValue bitassets{ChainAdapter::CapabilitiesJson(*ChainAdapter::Find("bitassets"))};
    const UniValue& ba{bitassets.find_value("dexFeatures")};
    BOOST_CHECK(ba.find_value("pair").isTrue() && ba.find_value("route").isFalse() && ba.find_value("twap").isFalse() && ba.find_value("curves").isFalse());
    BOOST_CHECK(ChainAdapter::CapabilitiesJson(*ChainAdapter::Find("thunder")).find_value("dexFeatures").isNull());

    BOOST_CHECK_EQUAL(ChainAdapter::AssetIdOf(UniValue{"0x" + ASSET}), ASSET);
    BOOST_CHECK_EQUAL(ChainAdapter::AssetIdOf(UniValue{ASSET}), ASSET);
    BOOST_CHECK_EQUAL(ChainAdapter::AssetIdOf(UniValue{std::string(64, 'C')}), "");
    BOOST_CHECK_EQUAL(ChainAdapter::AssetIdOf(UniValue{"GOLD"}), "");
    BOOST_CHECK_EQUAL(ChainAdapter::AssetIdOf(UniValue{std::string(63, 'c')}), "");
    BOOST_CHECK(ChainAdapter::IsCoinId(COINID) && !ChainAdapter::IsCoinId(ASSET));

    BOOST_CHECK_EQUAL(ChainAdapter::SlippageText(50), "0.5");
    BOOST_CHECK_EQUAL(ChainAdapter::SlippageText(100), "1");
    BOOST_CHECK_EQUAL(ChainAdapter::SlippageText(5), "0.05");
    BOOST_CHECK_EQUAL(ChainAdapter::SlippageText(1234), "12.34");
    BOOST_CHECK_EQUAL(ChainAdapter::LessSlippage(10000, 50), 9950);
    BOOST_CHECK_EQUAL(ChainAdapter::LessSlippage(199, 100), 197);
    BOOST_CHECK_EQUAL(ChainAdapter::LessSlippage(1, 50), 1);
    BOOST_CHECK_EQUAL(ChainAdapter::LessSlippage(int64_t{9'000'000'000'000'000'000}, 1), int64_t{8'999'100'000'000'000'000});

    BOOST_CHECK_EQUAL(ChainAdapter::WhyNotPoolKind("constant-product", 3000, 0, 0), "");
    BOOST_CHECK_EQUAL(ChainAdapter::WhyNotPoolKind("stable", 500, 100, 0), "");
    BOOST_CHECK_EQUAL(ChainAdapter::WhyNotPoolKind("weighted", 10000, 0, 80), "");
    BOOST_CHECK(!ChainAdapter::WhyNotPoolKind("concentrated", 3000, 0, 0).empty());
    BOOST_CHECK(!ChainAdapter::WhyNotPoolKind("constant-product", 2500, 0, 0).empty());
    BOOST_CHECK(!ChainAdapter::WhyNotPoolKind("stable", 500, 0, 0).empty());
    BOOST_CHECK(!ChainAdapter::WhyNotPoolKind("stable", 500, 10001, 0).empty());
    BOOST_CHECK(!ChainAdapter::WhyNotPoolKind("weighted", 3000, 0, 50).empty());
    BOOST_CHECK(!ChainAdapter::WhyNotPoolKind("constant-product", 3000, 5, 0).empty());

    // A pool whose node says no kind: constant product at 0.30 %; a kind said by name or number; a bad one dropped.
    const UniValue legacy{ChainAdapter::PoolRow(Json(R"({"pool":")" + POOL1 + R"(","asset_a":"GOLD","asset_b":"CHN","asset_a_id":")" + ASSET + R"(","asset_b_id":")" + COINID + R"(","reserve_a":10000.00,"reserve_b":20.00000000,"shares":5})"))};
    BOOST_CHECK_EQUAL(legacy.find_value("curve").get_str(), "constant-product");
    BOOST_CHECK_EQUAL(legacy.find_value("fee").getInt<int>(), 3000);
    BOOST_CHECK(legacy.find_value("legacy").isTrue());
    BOOST_CHECK_EQUAL(legacy.find_value("reserveA").get_str(), "10000.00");
    const UniValue weighted{ChainAdapter::PoolRow(Json(R"({"pool":")" + POOL2 + R"(","asset_a":"SILV","asset_b":"CHN","asset_a_id":")" + SILV + R"(","asset_b_id":")" + COINID + R"(","reserve_a":1,"reserve_b":2,"curve":2,"fee_millionths":10000,"weight_a":80})"))};
    BOOST_CHECK_EQUAL(weighted.find_value("curve").get_str(), "weighted");
    BOOST_CHECK_EQUAL(weighted.find_value("weightA").getInt<int>(), 80);
    BOOST_CHECK(weighted.find_value("legacy").isFalse());
    BOOST_CHECK(ChainAdapter::PoolRow(Json(R"({"pool":")" + POOL2 + R"(","asset_a_id":")" + SILV + R"(","asset_b_id":")" + COINID + R"(","reserve_a":1,"reserve_b":2,"curve":"orderbook"})")).isNull());
    // The fee as the node says it: in percent exactly (0.05: 500), in millionths; one that is no tier, or a kind
    // without a fee: not a pool shown (never shown as 0.30 %).
    BOOST_CHECK_EQUAL(ChainAdapter::PoolRow(Json(R"({"pool":")" + POOL2 + R"(","asset_a_id":")" + SILV + R"(","asset_b_id":")" + COINID + R"(","reserve_a":1,"reserve_b":2,"curve":"stable","fee":0.05,"amp":9})")).find_value("fee").getInt<int>(), 500);
    for (const char* bad : {R"("fee":0.25)", R"("fee_millionths":2500)", R"("fee":7)", R"("fee_millionths":0)"}) {
        BOOST_CHECK_MESSAGE(ChainAdapter::PoolRow(Json(R"({"pool":")" + POOL2 + R"(","asset_a_id":")" + SILV + R"(","asset_b_id":")" + COINID + R"(","reserve_a":1,"reserve_b":2,"curve":"constant_product",)" + std::string{bad} + "}")).isNull(), bad);
    }
    BOOST_CHECK(ChainAdapter::PoolRow(Json(R"({"pool":")" + POOL2 + R"(","asset_a_id":")" + SILV + R"(","asset_b_id":")" + COINID + R"(","reserve_a":1,"reserve_b":2,"curve":"stable"})")).isNull());
    BOOST_CHECK_EQUAL(ChainAdapter::FeePercentText(3000), "0.3");
    BOOST_CHECK_EQUAL(ChainAdapter::FeePercentText(100), "0.01");
    BOOST_CHECK_EQUAL(ChainAdapter::FeePercentText(10000), "1");
    BOOST_CHECK(ChainAdapter::PoolRow(Json(R"({"pool":")" + POOL2 + R"(","asset_a_id":")" + SILV + R"(","asset_b_id":")" + SILV + R"(","reserve_a":1,"reserve_b":2})")).isNull());
    BOOST_CHECK(ChainAdapter::PoolRow(Json(R"({"pool":")" + POOL2 + R"(","asset_a_id":")" + SILV + R"(","asset_b_id":")" + COINID + R"(","reserve_a":-1,"reserve_b":2})")).isNull());
}

BOOST_AUTO_TEST_CASE(dex_reads_pools_info_quote_chart_positions)
{
    Nodes nodes;
    ChainAdapter adapter{nodes.Hooks()};
    // Its bounds; no pool kinds until the node says they apply.
    UniValue info{adapter.DexInfo(P(R"({"stem":"layers"})"))};
    BOOST_CHECK(info.find_value("active").isTrue() && info.find_value("features").find_value("route").isTrue());
    BOOST_CHECK_EQUAL(info.find_value("maxHops").getInt<int>(), 4);
    BOOST_CHECK_EQUAL(info.find_value("curves").size(), 1u);
    BOOST_CHECK_EQUAL(info.find_value("feeTiers")[0].getInt<int>(), 3000);
    nodes.curves_active = true;
    info = adapter.DexInfo(P(R"({"stem":"layers"})"));
    BOOST_CHECK(info.find_value("curvesActive").isTrue());
    BOOST_CHECK_EQUAL(info.find_value("curves").size(), 3u);
    BOOST_CHECK_EQUAL(info.find_value("feeTiers").size(), 4u);
    BOOST_CHECK_EQUAL(info.find_value("weights").size(), 12u);
    // A chain with no DEX, one not running.
    BOOST_CHECK_EQUAL(Code([&] { adapter.DexInfo(P(R"({"stem":"thunder"})")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(Code([&] { adapter.DexPools(P(R"({"stem":"bitassets"})")); }), BRIDGE_NOT_READY);

    // Every pool, checked (a row that is none dropped); the tip for an estimate of a yearly rate.
    const UniValue pools{adapter.DexPools(P(R"({"stem":"layers"})"))};
    BOOST_CHECK_EQUAL(pools.find_value("pools").size(), 2u);
    BOOST_CHECK_EQUAL(pools.find_value("height").getInt<int>(), 42);
    BOOST_CHECK_EQUAL(pools.find_value("pools")[1].find_value("curve").get_str(), "stable");
    BOOST_CHECK_EQUAL(pools.find_value("pools")[1].find_value("fee").getInt<int>(), 500);
    for (const UniValue& r : pools.find_value("pools").getValues()) BOOST_CHECK(r.find_value("pool").get_str() != std::string(64, '6')); // its fee no tier
    BOOST_CHECK_EQUAL(adapter.DexPools(P(R"({"stem":"layers","asset":"0x)" + ASSET + R"("})")).find_value("pools").size(), 1u);
    BOOST_CHECK_EQUAL(Code([&] { adapter.DexPools(P(R"({"stem":"layers","asset":"GOLD"})")); }), BRIDGE_INVALID_REQUEST);

    // A quote: the route with each pool's kind, the price impact against the pools' prices; amounts exact.
    const UniValue q{adapter.DexQuote(P(R"({"stem":"layers","in":")" + ASSET + R"(","out":")" + SILV + R"(","amount":"10"})"))};
    BOOST_CHECK_EQUAL(q.find_value("via").get_str(), "route");
    BOOST_CHECK_EQUAL(q.find_value("amountIn").get_str(), "10.00");
    BOOST_CHECK_EQUAL(q.find_value("amountOut").get_str(), "12.3456");
    BOOST_CHECK_EQUAL(q.find_value("decimalsOut").getInt<int>(), 4);
    const UniValue& route{q.find_value("routes")[0]};
    BOOST_CHECK_EQUAL(route.find_value("path").size(), 3u);
    BOOST_CHECK_EQUAL(route.find_value("labels")[1].get_str(), "CHN");
    BOOST_CHECK_EQUAL(route.find_value("hops")[0].find_value("kind").get_str(), "constant product · 0.30 %");
    BOOST_CHECK_EQUAL(route.find_value("hops")[1].find_value("kind").get_str(), "stable (A 100) · 0.05 %");
    BOOST_CHECK_EQUAL(route.find_value("fees").getInt<int>(), 3500);
    // 10 GOLD at 0.002 CHN, then at 250 SILV: 5 SILV at the pools' prices; 12.3456 is better (a stand-in): no impact.
    BOOST_CHECK(route.find_value("priceImpact").isStr());
    // More decimals than the asset has: refused before the node is asked.
    const size_t before{nodes.rpcs.size()};
    BOOST_CHECK_EQUAL(Code([&] { adapter.DexQuote(P(R"({"stem":"layers","in":")" + ASSET + R"(","out":")" + SILV + R"(","amount":"10.555"})")); }), BRIDGE_INVALID_REQUEST);
    for (size_t i{before}; i < nodes.rpcs.size(); ++i) BOOST_CHECK(nodes.rpcs[i].params[0].find_value("method").get_str() != "quoteroute");
    BOOST_CHECK_EQUAL(Code([&] { adapter.DexQuote(P(R"({"stem":"layers","in":")" + ASSET + R"(","out":")" + ASSET + R"(","amount":"1"})")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK(!Message([&] { adapter.DexQuote(P(R"({"stem":"layers","in":")" + std::string(64, '9') + R"(","out":")" + ASSET + R"(","amount":"1"})")); }).empty());

    // An older node (its pages' policy without quoteroute): the pool of the two, as every DEX quotes.
    nodes.routes_offered = false;
    const UniValue pair{adapter.DexQuote(P(R"({"stem":"layers","in":")" + ASSET + R"(","out":")" + COINID + R"(","amount":"10"})"))};
    BOOST_CHECK_EQUAL(pair.find_value("via").get_str(), "pair");
    BOOST_CHECK_EQUAL(pair.find_value("routes")[0].find_value("hops")[0].find_value("pool").get_str(), POOL1);
    BOOST_CHECK_EQUAL(pair.find_value("routes")[0].find_value("priceImpact").get_str(), "0.3988");
    BOOST_CHECK_EQUAL(adapter.DexQuote(P(R"({"stem":"layers","in":")" + ASSET + R"(","out":")" + SILV + R"(","amount":"10"})")).find_value("routes").size(), 0u);
    BOOST_CHECK(adapter.DexInfo(P(R"({"stem":"layers"})")).find_value("features").find_value("route").isFalse());
    nodes.routes_offered = true;

    // The chart: 24 parts of the window the pool keeps, oldest first, each its average (rising here).
    const UniValue chart{adapter.DexChart(P(R"({"stem":"layers","pool":")" + POOL1 + R"("})"))};
    const UniValue& points{chart.find_value("points")};
    BOOST_CHECK_EQUAL(points.size(), 24u);
    BOOST_CHECK(points[0].find_value("t").getInt<int64_t>() < points[23].find_value("t").getInt<int64_t>());
    BOOST_CHECK(std::stod(points[0].find_value("price").get_str()) < std::stod(points[23].find_value("price").get_str()));
    BOOST_CHECK_EQUAL(chart.find_value("labelA").get_str(), "GOLD");
    // A pool with no observation yet: no points, said.
    BOOST_CHECK_EQUAL(adapter.DexChart(P(R"({"stem":"layers","pool":")" + POOL2 + R"("})")).find_value("reason").get_str(), "young");
    BOOST_CHECK_EQUAL(Code([&] { adapter.DexChart(P(R"({"stem":"layers","pool":"x"})")); }), BRIDGE_INVALID_REQUEST);

    // The wallet's shares (a row that is none dropped).
    const UniValue pos{adapter.DexPositions(P(R"({"stem":"layers"})"))};
    BOOST_CHECK_EQUAL(pos.find_value("positions").size(), 1u);
    BOOST_CHECK_EQUAL(pos.find_value("positions")[0].find_value("valueB").get_str(), "0.50000000");
    BOOST_CHECK_EQUAL(pos.find_value("positions")[0].find_value("b").find_value("id").get_str(), COINID);

    // A read that asks is refused, not asked.
    nodes.read_asks = true;
    BOOST_CHECK_EQUAL(Code([&] { adapter.DexPools(P(R"({"stem":"layers"})")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK(nodes.calls.empty());
}

BOOST_AUTO_TEST_CASE(dex_swap_quoted_again_its_route_checked_and_shown)
{
    Nodes nodes;
    nodes.curves_active = true;
    ChainAdapter adapter{nodes.Hooks()};
    const auto swap{[&](const std::string& extra) {
        return adapter.DexSwap(P(R"({"stem":"layers","in":")" + ASSET + R"(","out":")" + SILV + R"(","amount":"10","slippage":50,"minOut":"12.2",)" + extra + "}"));
    }};
    const std::string path{R"("path":[")" + ASSET + R"(",")" + COINID + R"(",")" + SILV + R"("])"};
    const std::string route{path + R"(,"pools":[")" + POOL1 + R"(",")" + POOL2 + R"("])"};
    const UniValue done{swap(route)};
    BOOST_CHECK_EQUAL(done.find_value("txid").get_str(), TXID);
    BOOST_REQUIRE_EQUAL(nodes.calls.size(), 1u);
    // By the pools' ids: exactly the pools shown, whatever else each pair has.
    const UniValue req{Inner(nodes.calls[0].second)};
    BOOST_CHECK_EQUAL(req.find_value("method").get_str(), "swappools");
    BOOST_CHECK_EQUAL(req.find_value("params")[0].get_str(), "0x" + ASSET);
    BOOST_CHECK_EQUAL(req.find_value("params")[1].get_str(), "10.00");
    BOOST_CHECK_EQUAL(req.find_value("params")[2][0].get_str(), POOL1);
    BOOST_CHECK_EQUAL(req.find_value("params")[2][1].get_str(), POOL2);
    BOOST_CHECK_EQUAL(req.find_value("params")[3].getValStr(), "0.5");
    BOOST_CHECK_EQUAL(req.find_value("params")[4].get_str(), "12.2000");
    BOOST_CHECK_EQUAL(req.find_value("wallet").get_str(), "main");
    // What the app checked, in the question before the chain's rows: each pool by its id, kind and fee.
    const UniValue rows{nodes.app_rows[0]};
    BOOST_CHECK_EQUAL(rows[0].find_value("label").get_str(), "Checked by Backbone");
    BOOST_CHECK_EQUAL(rows[0].find_value("kind").get_str(), "heading");
    BOOST_CHECK_EQUAL(RowValue(rows, "Route"), "GOLD → CHN → SILV (2 pools)");
    BOOST_CHECK_EQUAL(RowValue(rows, "Pool 1"), "GOLD / CHN · constant product · 0.30 %\n" + POOL1);
    BOOST_CHECK_EQUAL(RowValue(rows, "Pool 2"), "CHN / SILV · stable (A 100) · 0.05 %\n" + POOL2);
    BOOST_CHECK_EQUAL(RowValue(rows, "You pay"), "10.00 GOLD");
    BOOST_CHECK_EQUAL(RowValue(rows, "Quoted again just now"), "12.3456 SILV");
    BOOST_CHECK_EQUAL(RowValue(rows, "You receive at least"), "12.2000 SILV (slippage 0.50 %)");

    // The price moved: the route now gives less (less the slippage) than what was accepted: refused, nothing asked.
    BOOST_CHECK(Message([&] { adapter.DexSwap(P(R"({"stem":"layers","in":")" + ASSET + R"(","out":")" + SILV + R"(","amount":"10","slippage":50,"minOut":"12.3",)" + route + "}")); }).starts_with("The price moved"));
    // Another pool of a pair than the route's (the same assets): not this route; pools that are not one a hop, twice, none.
    BOOST_CHECK(Message([&] { swap(path + R"(,"pools":[")" + POOL3 + R"(",")" + POOL2 + R"("])"); }).find("no longer offer") != std::string::npos);
    BOOST_CHECK_EQUAL(Code([&] { swap(path + R"(,"pools":[")" + POOL1 + R"("])"); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(Code([&] { swap(path + R"(,"pools":[")" + POOL1 + R"(",")" + POOL1 + R"("])"); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(Code([&] { swap(path); }), BRIDGE_INVALID_REQUEST);
    // A route the pools do not offer (the pool of the two does not exist), an asset twice, ends that are not the swap's.
    BOOST_CHECK(Message([&] { swap(R"("path":[")" + ASSET + R"(",")" + SILV + R"("],"pools":[")" + POOL1 + R"("])"); }).find("no longer offer") != std::string::npos);
    BOOST_CHECK_EQUAL(Code([&] { swap(R"("path":[")" + ASSET + R"(",")" + COINID + R"(",")" + ASSET + R"(",")" + SILV + R"("],"pools":[")" + POOL1 + R"(",")" + POOL2 + R"(",")" + POOL3 + R"("])"); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(Code([&] { swap(R"("path":[")" + COINID + R"(",")" + SILV + R"("],"pools":[")" + POOL2 + R"("])"); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(Code([&] { swap(R"("path":"GOLD,SILV")"); }), BRIDGE_INVALID_REQUEST);
    // Amounts: more than held (250.5 GOLD), too many decimals, a slippage out of bounds, a number for the least.
    BOOST_CHECK(Message([&] { adapter.DexSwap(P(R"({"stem":"layers","in":")" + ASSET + R"(","out":")" + SILV + R"(","amount":"300","minOut":"1",)" + route + "}")); }).starts_with("More than"));
    BOOST_CHECK_EQUAL(Code([&] { adapter.DexSwap(P(R"({"stem":"layers","in":")" + ASSET + R"(","out":")" + SILV + R"(","amount":"1.001","minOut":"1",)" + route + "}")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(Code([&] { adapter.DexSwap(P(R"({"stem":"layers","in":")" + ASSET + R"(","out":")" + SILV + R"(","amount":"1","minOut":"1","slippage":9000,)" + route + "}")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(Code([&] { adapter.DexSwap(P(R"({"stem":"layers","in":")" + ASSET + R"(","out":")" + SILV + R"(","amount":"1","minOut":1,)" + route + "}")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(nodes.calls.size(), 1u);

    // A chain that names a swap's pools by their assets only (no pool kinds): a route through a pool of another kind
    // cannot be named: refused, nothing asked.
    nodes.curves_active = false;
    BOOST_CHECK(Message([&] { swap(route); }).find("by their assets only") != std::string::npos);
    BOOST_CHECK_EQUAL(nodes.calls.size(), 1u);
    // In one pool of the default kind: swapasset (its assets name that pool), its bound on the price impact the chain's own;
    // a second pool of the pair of another kind does not change which pool it is.
    nodes.second_pool = true;
    adapter.DexSwap(P(R"({"stem":"layers","in":")" + ASSET + R"(","out":")" + COINID + R"(","amount":"10","minOut":"0.0198","path":[")" + ASSET + R"(",")" + COINID + R"("],"pools":[")" + POOL1 + R"("]})"));
    const UniValue one{Inner(nodes.calls.back().second)};
    BOOST_CHECK_EQUAL(one.find_value("method").get_str(), "swapasset");
    BOOST_CHECK_EQUAL(one.find_value("params")[0].get_str(), "0x" + ASSET);
    BOOST_CHECK_EQUAL(one.find_value("params")[2].get_str(), "CHN"); // the chain's coin by its own word
    BOOST_CHECK(one.find_value("params")[4].isNull());
    BOOST_CHECK_EQUAL(one.find_value("params")[5].get_str(), "0.01980000");
    BOOST_CHECK_EQUAL(RowValue(nodes.app_rows.back(), "Route"), "GOLD → CHN (1 pool)");
    BOOST_CHECK_EQUAL(RowValue(nodes.app_rows.back(), "Pool"), "GOLD / CHN · constant product · 0.30 %\n" + POOL1);
    // The pair's other pool named: not the one swapasset acts on.
    BOOST_CHECK(Message([&] { adapter.DexSwap(P(R"({"stem":"layers","in":")" + ASSET + R"(","out":")" + COINID + R"(","amount":"10","minOut":"0.0198","path":[")" + ASSET + R"(",")" + COINID + R"("],"pools":[")" + POOL3 + R"("]})")); }).find("no longer offer") != std::string::npos);

    // A chain that swaps in one pool only: a route of three refused.
    nodes.up.push_back("bitassets");
    BOOST_CHECK(Message([&] { adapter.DexSwap(P(R"({"stem":"bitassets","in":")" + ASSET + R"(","out":")" + SILV + R"(","amount":"1","minOut":"1",)" + route + "}")); }).find("one pool") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(dex_liquidity_in_and_out_and_new_pools)
{
    Nodes nodes;
    ChainAdapter adapter{nodes.Hooks()};
    const std::string gold_chn{R"("a":")" + ASSET + R"(","b":")" + COINID + R"(")"};
    // In, on a chain that names a pool by its assets (no pool kinds): the pair's one pool, confirmed by its id.
    adapter.DexAddLiquidity(P(R"({"stem":"layers",)" + gold_chn + R"(,"pool":")" + POOL1 + R"(","amountA":"100","amountB":"0.2","setsPrice":false})"));
    const UniValue add{Inner(nodes.calls.back().second)};
    BOOST_CHECK_EQUAL(add.find_value("method").get_str(), "addliquidity");
    BOOST_CHECK_EQUAL(add.find_value("params")[1].get_str(), "100.00");
    BOOST_CHECK_EQUAL(add.find_value("params")[3].get_str(), "0.20000000");
    BOOST_CHECK_EQUAL(add.find_value("params")[4].getValStr(), "1");
    BOOST_CHECK(add.find_value("params")[5].isFalse());
    BOOST_CHECK_EQUAL(RowValue(nodes.app_rows.back(), "Pool"), "GOLD / CHN · constant product · 0.30 %\n" + POOL1);
    // No pool named, one of other assets, one of no such id; more than held, the same asset twice.
    size_t asked{nodes.calls.size()};
    BOOST_CHECK_EQUAL(Code([&] { adapter.DexAddLiquidity(P(R"({"stem":"layers",)" + gold_chn + R"(,"amountA":"100"})")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK(Message([&] { adapter.DexAddLiquidity(P(R"({"stem":"layers",)" + gold_chn + R"(,"pool":")" + POOL2 + R"(","amountA":"100"})")); }).find("No pool of these two assets") != std::string::npos);
    BOOST_CHECK(Message([&] { adapter.DexAddLiquidity(P(R"({"stem":"layers",)" + gold_chn + R"(,"pool":")" + std::string(64, '3') + R"(","amountA":"100"})")); }).find("No pool of these two assets") != std::string::npos);
    BOOST_CHECK(Message([&] { adapter.DexAddLiquidity(P(R"({"stem":"layers",)" + gold_chn + R"(,"pool":")" + POOL1 + R"(","amountA":"100","amountB":"13"})")); }).starts_with("More of the second"));
    BOOST_CHECK_EQUAL(Code([&] { adapter.DexAddLiquidity(P(R"({"stem":"layers","a":")" + ASSET + R"(","b":")" + ASSET + R"(","pool":")" + POOL1 + R"(","amountA":"1"})")); }), BRIDGE_INVALID_REQUEST);
    // A second pool of the pair: by its assets alone the chain cannot be told which: refused, either pool.
    nodes.second_pool = true;
    BOOST_CHECK(Message([&] { adapter.DexAddLiquidity(P(R"({"stem":"layers",)" + gold_chn + R"(,"pool":")" + POOL1 + R"(","amountA":"100"})")); }).find("more than one pool") != std::string::npos);
    BOOST_CHECK(Message([&] { adapter.DexAddLiquidity(P(R"({"stem":"layers",)" + gold_chn + R"(,"pool":")" + POOL3 + R"(","amountA":"10"})")); }).find("not this one") != std::string::npos);
    BOOST_CHECK(Message([&] { adapter.DexRemoveLiquidity(P(R"({"stem":"layers","pool":")" + POOL1 + R"(","percent":"50"})")); }).find("more than one pool") != std::string::npos);
    BOOST_CHECK_EQUAL(nodes.calls.size(), asked);
    // With the pool kinds: by the pool's id (addpoolliquidity), the very pool shown.
    nodes.curves_active = true;
    adapter.DexAddLiquidity(P(R"({"stem":"layers",)" + gold_chn + R"(,"pool":")" + POOL3 + R"(","amountA":"10","amountB":"0.2"})"));
    const UniValue byid{Inner(nodes.calls.back().second)};
    BOOST_CHECK_EQUAL(byid.find_value("method").get_str(), "addpoolliquidity");
    BOOST_CHECK_EQUAL(byid.find_value("params")[0].get_str(), POOL3);
    BOOST_CHECK_EQUAL(byid.find_value("params")[1].find_value("0x" + ASSET).get_str(), "10.00");
    BOOST_CHECK_EQUAL(byid.find_value("params")[1].find_value("CHN").get_str(), "0.20000000");
    BOOST_CHECK_EQUAL(RowValue(nodes.app_rows.back(), "Pool"), "GOLD / CHN · stable (A 50) · 0.01 %\n" + POOL3);

    // Out: only shares the wallet holds, by the pool's id; a percentage exact; what it gives back said.
    adapter.DexRemoveLiquidity(P(R"({"stem":"layers","pool":")" + POOL1 + R"(","percent":"50"})"));
    const UniValue rm{Inner(nodes.calls.back().second)};
    BOOST_CHECK_EQUAL(rm.find_value("method").get_str(), "removepoolliquidity");
    BOOST_CHECK_EQUAL(rm.find_value("params")[0].get_str(), POOL1);
    BOOST_CHECK_EQUAL(rm.find_value("params")[1].getValStr(), "50.00");
    BOOST_CHECK_EQUAL(RowValue(nodes.app_rows.back(), "Pool"), "GOLD / CHN · constant product · 0.30 %\n" + POOL1);
    BOOST_CHECK_EQUAL(RowValue(nodes.app_rows.back(), "You get about"), "125.00 GOLD and 0.25000000 CHN");
    // Without the pool kinds and one pool of the pair: removeliquidity by its assets.
    nodes.curves_active = false;
    nodes.second_pool = false;
    adapter.DexRemoveLiquidity(P(R"({"stem":"layers","pool":")" + POOL1 + R"(","percent":"25"})"));
    const UniValue rm2{Inner(nodes.calls.back().second)};
    BOOST_CHECK_EQUAL(rm2.find_value("method").get_str(), "removeliquidity");
    BOOST_CHECK_EQUAL(rm2.find_value("params")[0].get_str(), "0x" + ASSET);
    BOOST_CHECK_EQUAL(rm2.find_value("params")[1].get_str(), "CHN");
    BOOST_CHECK_EQUAL(rm2.find_value("params")[2].getValStr(), "25.00");
    asked = nodes.calls.size();
    BOOST_CHECK(Message([&] { adapter.DexRemoveLiquidity(P(R"({"stem":"layers","pool":")" + POOL2 + R"(","percent":"50"})")); }).find("holds no shares") != std::string::npos);
    for (const char* bad : {"0", "100.5", "33.333", "-1", "1e2"}) {
        BOOST_CHECK_MESSAGE(Code([&] { adapter.DexRemoveLiquidity(P(R"({"stem":"layers","pool":")" + POOL1 + R"(","percent":")" + bad + R"("})")); }) == BRIDGE_INVALID_REQUEST, bad);
    }
    BOOST_CHECK_EQUAL(nodes.calls.size(), asked);

    // A new pool: without the pool kinds, constant product at 0.30 % is addliquidity whose amounts set the price.
    adapter.DexCreatePool(P(R"({"stem":"layers",)" + gold_chn + R"(,"amountA":"10","amountB":"1","curve":"constant-product","fee":3000})"));
    BOOST_CHECK_EQUAL(Inner(nodes.calls.back().second).find_value("method").get_str(), "addliquidity");
    BOOST_CHECK(Inner(nodes.calls.back().second).find_value("params")[5].isTrue());
    BOOST_CHECK_EQUAL(RowValue(nodes.app_rows.back(), "New pool"), "constant product · 0.30 %");
    // Another kind: only once the chain's node says its pool kinds apply; a kind that is none refused before anything.
    const size_t made{nodes.calls.size()};
    BOOST_CHECK(Message([&] { adapter.DexCreatePool(P(R"({"stem":"layers",)" + gold_chn + R"(,"amountA":"10","amountB":"1","curve":"stable","fee":500,"amp":100})")); }).find("only, for now") != std::string::npos);
    BOOST_CHECK_EQUAL(Code([&] { adapter.DexCreatePool(P(R"({"stem":"layers",)" + gold_chn + R"(,"amountA":"10","amountB":"1","curve":"weighted","fee":3000,"weightA":33})")); }), BRIDGE_INVALID_REQUEST);
    BOOST_CHECK_EQUAL(nodes.calls.size(), made);
    // With them: createpool, the kind named whole (the node's names, the fee in percent); the new pool's id said.
    nodes.curves_active = true;
    adapter.DexCreatePool(P(R"({"stem":"layers",)" + gold_chn + R"(,"amountA":"10","amountB":"1","curve":"stable","fee":500,"amp":100})"));
    const UniValue create{Inner(nodes.calls.back().second)};
    BOOST_CHECK_EQUAL(create.find_value("method").get_str(), "createpool");
    BOOST_CHECK_EQUAL(create.find_value("params")[4].find_value("curve").get_str(), "stable");
    BOOST_CHECK_EQUAL(create.find_value("params")[4].find_value("fee").getValStr(), "0.05");
    BOOST_CHECK_EQUAL(create.find_value("params")[4].find_value("amp").getInt<int>(), 100);
    BOOST_CHECK_EQUAL(RowValue(nodes.app_rows.back(), "New pool"), "stable (A 100) · 0.05 %\n" + std::string(64, '4'));
    adapter.DexCreatePool(P(R"({"stem":"layers",)" + gold_chn + R"(,"amountA":"10","amountB":"1","curve":"weighted","fee":10000,"weightA":80})"));
    BOOST_CHECK_EQUAL(Inner(nodes.calls.back().second).find_value("params")[4].find_value("curve").get_str(), "weighted");
    BOOST_CHECK_EQUAL(Inner(nodes.calls.back().second).find_value("params")[4].find_value("weight_a").getInt<int>(), 80);
    BOOST_CHECK_EQUAL(Inner(nodes.calls.back().second).find_value("params")[4].find_value("fee").getValStr(), "1");
}

BOOST_AUTO_TEST_SUITE_END()
