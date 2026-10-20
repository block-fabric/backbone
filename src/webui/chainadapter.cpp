// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <webui/chainadapter.h>

#include <consensus/amount.h>
#include <tinyformat.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <webui/bridge.h>

#include <algorithm>
#include <set>

namespace webui {
namespace {

const UniValue& Param(const UniValue& params, const std::string& key)
{
    return params.isObject() ? params.find_value(key) : NullUniValue;
}

std::string Str(const UniValue& o, const std::string& key)
{
    const UniValue& v{Param(o, key)};
    return v.isStr() ? v.get_str() : std::string{};
}

bool IsHex64(const std::string& s)
{
    return s.size() == 64 && IsHex(s) && std::none_of(s.begin(), s.end(), [](char c) { return c >= 'A' && c <= 'F'; });
}

//! Text of a node, for the page: one bounded line, control characters out (the page escapes it too).
std::string Line(const std::string& text, size_t max)
{
    std::string out;
    out.reserve(std::min(text.size(), max));
    for (const unsigned char c : text) {
        if (out.size() >= max) break;
        out.push_back(c < 0x20 || c == 0x7f ? ' ' : static_cast<char>(c));
    }
    return out;
}

//! A node's answer kept for a page: plain values, bounded in depth, count and length (a view, never a channel for more).
UniValue Bounded(const UniValue& v, int depth = 0)
{
    switch (v.getType()) {
    case UniValue::VSTR:
        return UniValue{Line(v.get_str(), 300)};
    case UniValue::VNUM: {
        // An amount (a number written with decimals, as the nodes write coins): its exact text, never a float in the
        // page; a count stays a number.
        if (v.getValStr().find_first_of(".eE") != std::string::npos) return UniValue{v.getValStr()};
        UniValue n;
        n.setNumStr(v.getValStr());
        return n;
    }
    case UniValue::VBOOL:
        return UniValue{v.get_bool()};
    case UniValue::VARR: {
        UniValue out{UniValue::VARR};
        if (depth >= 3) return out;
        for (size_t i{0}; i < v.size() && i < 200; ++i) out.push_back(Bounded(v[i], depth + 1));
        return out;
    }
    case UniValue::VOBJ: {
        UniValue out{UniValue::VOBJ};
        if (depth >= 3) return out;
        for (size_t i{0}; i < v.size() && i < 48; ++i) out.pushKV(Line(v.getKeys()[i], 40), Bounded(v.getValues()[i], depth + 1));
        return out;
    }
    default:
        return UniValue{};
    }
}

UniValue Array(std::initializer_list<UniValue> items)
{
    UniValue out{UniValue::VARR};
    for (const UniValue& i : items) out.push_back(i);
    return out;
}

constexpr int COIN_DECIMALS{8};
} // namespace

const std::vector<ChainAdapter::Capabilities>& ChainAdapter::Table()
{
    // One row per sidechain of the catalog (supervisor.cpp). What a chain does is written here, with its own pages the
    // host shows inside its own where it has no view of its own for them (the module's frame).
    static const std::vector<Capabilities> table{
        {.stem = "layers", .assets = true, .dex = true, .staking = true, .embed = {"trade", "auctions", "staking", "assets", "my-assets", "issue"}},
        {.stem = "fastblocks", .staking = true, .embed = {"staking"}},
        {.stem = "thunder", .embed = {}},
        {.stem = "bitnames", .names = true, .embed = {"names", "register", "directory"}},
        {.stem = "bitassets", .assets = true, .dex = true, .embed = {"trade", "auctions", "assets", "my-assets", "issue"}},
        {.stem = "zside", .shielded = true, .embed = {"private", "zhistory"}},
        {.stem = "hivemind", .markets = true, .embed = {"markets", "votecoin", "decisions", "portfolio"}},
    };
    return table;
}

const ChainAdapter::Capabilities* ChainAdapter::Find(std::string_view stem)
{
    for (const Capabilities& c : Table()) {
        if (c.stem == stem) return &c;
    }
    return nullptr;
}

UniValue ChainAdapter::CapabilitiesJson(const Capabilities& caps)
{
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", caps.stem);
    out.pushKV("decimals", COIN_DECIMALS);
    out.pushKV("assets", caps.assets);
    out.pushKV("dex", caps.dex);
    out.pushKV("staking", caps.staking);
    out.pushKV("names", caps.names);
    out.pushKV("markets", caps.markets);
    out.pushKV("shielded", caps.shielded);
    // What every sidechain does.
    out.pushKV("send", true);
    out.pushKV("receive", true);
    out.pushKV("deposit", true);
    out.pushKV("withdraw", true);
    UniValue embed{UniValue::VARR};
    for (const std::string& e : caps.embed) embed.push_back(e);
    out.pushKV("embed", std::move(embed));
    return out;
}

std::optional<int64_t> ChainAdapter::ParseUnits(std::string_view text, int decimals)
{
    if (decimals < 0 || decimals > 18 || text.empty() || text.size() > 40) return std::nullopt;
    const size_t dot{text.find('.')};
    const std::string_view whole{text.substr(0, dot)};
    const std::string_view frac{dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1)};
    if (whole.empty() && frac.empty()) return std::nullopt;
    if (dot != std::string_view::npos && frac.empty()) return std::nullopt;
    if (static_cast<int>(frac.size()) > decimals) return std::nullopt;
    const auto digits{[](std::string_view s) { return std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); }};
    if (!digits(whole) || !digits(frac)) return std::nullopt;
    __int128 value{0};
    for (const char c : whole) {
        value = value * 10 + (c - '0');
        if (value > std::numeric_limits<int64_t>::max()) return std::nullopt;
    }
    for (int i{0}; i < decimals; ++i) {
        value = value * 10 + (i < static_cast<int>(frac.size()) ? frac[i] - '0' : 0);
        if (value > std::numeric_limits<int64_t>::max()) return std::nullopt;
    }
    return static_cast<int64_t>(value);
}

std::string ChainAdapter::FormatUnits(int64_t units, int decimals)
{
    const bool negative{units < 0};
    const uint64_t abs{negative ? static_cast<uint64_t>(-(units + 1)) + 1 : static_cast<uint64_t>(units)};
    uint64_t scale{1};
    for (int i{0}; i < decimals; ++i) scale *= 10;
    std::string out{strprintf("%s%u", negative ? "-" : "", abs / scale)};
    if (decimals > 0) out += strprintf(".%0*u", decimals, abs % scale);
    return out;
}

std::string ChainAdapter::AmountText(const UniValue& value)
{
    if (value.isNum() || value.isStr()) return value.getValStr();
    return {};
}

ChainAdapter::AddressKind ChainAdapter::KindOf(std::string_view address, std::string* hrp)
{
    if (address.empty()) return AddressKind::EMPTY;
    // s<slot>_<address>_<checksum> (drivechain/sidechain.h): what pays from the mainchain into a sidechain.
    if (address.size() > 3 && address[0] == 's') {
        size_t i{1};
        while (i < address.size() && address[i] >= '0' && address[i] <= '9') ++i;
        if (i > 1 && i < address.size() && address[i] == '_') return AddressKind::DEPOSIT;
    }
    const size_t one{address.rfind('1')};
    if (one != std::string_view::npos && one > 0 && address.size() - one >= 7) {
        static constexpr std::string_view CHARSET{"qpzry9x8gf2tvdw0s3jn54khce6mua7l"};
        std::string lower;
        for (const char c : address) lower.push_back(c >= 'A' && c <= 'Z' ? c + 32 : c);
        const bool mixed{lower != address && std::any_of(address.begin(), address.end(), [](char c) { return c >= 'a' && c <= 'z'; })};
        const std::string_view data{std::string_view{lower}.substr(one + 1)};
        if (!mixed && std::all_of(data.begin(), data.end(), [](char c) { return CHARSET.find(c) != std::string_view::npos; })) {
            if (hrp) *hrp = lower.substr(0, one);
            return AddressKind::BECH32;
        }
    }
    static constexpr std::string_view BASE58{"123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"};
    if (address.size() >= 25 && address.size() <= 36 && std::all_of(address.begin(), address.end(), [](char c) { return BASE58.find(c) != std::string_view::npos; })) {
        return AddressKind::BASE58;
    }
    return AddressKind::OTHER;
}

std::string ChainAdapter::PickWallet(const UniValue& loaded, const std::string& wanted)
{
    if (!loaded.isArray() || loaded.empty()) return {};
    bool has_main{false};
    for (const UniValue& w : loaded.getValues()) {
        if (!w.isStr()) continue;
        if (!wanted.empty() && w.get_str() == wanted) return wanted;
        if (w.get_str() == "main") has_main = true;
    }
    if (has_main) return "main";
    return loaded[0].isStr() ? loaded[0].get_str() : std::string{};
}

UniValue ChainAdapter::TransactionRows(const UniValue& listed, size_t max)
{
    UniValue out{UniValue::VARR};
    if (!listed.isArray()) return out;
    // listtransactions gives the oldest first.
    for (size_t n{listed.size()}; n > 0 && out.size() < max; --n) {
        const UniValue& t{listed[n - 1]};
        if (!t.isObject() || !IsHex64(Str(t, "txid")) || !Param(t, "amount").isNum()) continue;
        UniValue row{UniValue::VOBJ};
        row.pushKV("txid", Str(t, "txid"));
        row.pushKV("vout", Param(t, "vout").isNum() ? Param(t, "vout").getInt<int>() : 0);
        row.pushKV("category", Line(Str(t, "category"), 16));
        row.pushKV("amount", AmountText(Param(t, "amount")));
        if (Param(t, "fee").isNum()) row.pushKV("fee", AmountText(Param(t, "fee")));
        row.pushKV("address", Line(Str(t, "address"), 120));
        row.pushKV("label", Line(Str(t, "label"), 120));
        row.pushKV("confirmations", Param(t, "confirmations").isNum() ? Param(t, "confirmations").getInt<int64_t>() : 0);
        row.pushKV("time", Param(t, "time").isNum() ? Param(t, "time").getInt<int64_t>() : 0);
        row.pushKV("abandoned", Param(t, "abandoned").isTrue());
        out.push_back(std::move(row));
    }
    return out;
}

UniValue ChainAdapter::AssetRows(const UniValue& listed)
{
    UniValue out{UniValue::VARR};
    const UniValue assets{Param(listed, "assets")};
    if (!assets.isArray()) return out;
    for (const UniValue& a : assets.getValues()) {
        if (out.size() >= 200) break;
        if (!a.isObject() || !IsHex64(Str(a, "asset"))) continue;
        const int decimals{Param(a, "decimals").isNum() ? Param(a, "decimals").getInt<int>() : -1};
        if (decimals < 0 || decimals > 18) continue;
        UniValue row{UniValue::VOBJ};
        row.pushKV("id", Str(a, "asset"));
        row.pushKV("label", Line(Str(a, "label"), 64));
        row.pushKV("decimals", decimals);
        row.pushKV("balance", AmountText(Param(a, "balance")));
        row.pushKV("pending", AmountText(Param(a, "pending")));
        row.pushKV("control", Param(a, "control").isTrue());
        row.pushKV("supply", AmountText(Param(a, "supply")));
        out.push_back(std::move(row));
    }
    return out;
}

UniValue ChainAdapter::PlainFields(const UniValue& object, const std::vector<std::string>& keys)
{
    UniValue out{UniValue::VOBJ};
    if (!object.isObject()) return out;
    for (const std::string& key : keys) {
        const UniValue& v{object.find_value(key)};
        if (v.isStr()) out.pushKV(key, Line(v.get_str(), 200));
        else if (v.isNum() || v.isBool()) out.pushKV(key, Bounded(v));
    }
    return out;
}

UniValue ChainAdapter::PrepareSendRequest(const std::string& wallet, const std::string& address, int64_t sats, bool subtract_fee)
{
    UniValue recipient{UniValue::VOBJ};
    recipient.pushKV("address", address);
    recipient.pushKV("amount", sats);
    recipient.pushKV("subtractFee", subtract_fee);
    UniValue params{UniValue::VOBJ};
    params.pushKV("wallet", wallet);
    params.pushKV("recipients", Array({recipient}));
    UniValue request{UniValue::VOBJ};
    request.pushKV("kind", "host");
    request.pushKV("method", "wallet.send.prepare");
    request.pushKV("params", std::move(params));
    return request;
}

UniValue ChainAdapter::CommitSendRequest(const std::string& wallet, const std::string& token)
{
    UniValue params{UniValue::VOBJ};
    params.pushKV("wallet", wallet);
    params.pushKV("token", token);
    UniValue request{UniValue::VOBJ};
    request.pushKV("kind", "host");
    request.pushKV("method", "wallet.send.commit");
    request.pushKV("params", std::move(params));
    return request;
}

UniValue ChainAdapter::ConfirmedRequest(const std::string& wallet, const std::string& method, const UniValue& args)
{
    UniValue params{UniValue::VOBJ};
    params.pushKV("method", method);
    params.pushKV("params", args);
    params.pushKV("wallet", wallet);
    UniValue request{UniValue::VOBJ};
    request.pushKV("kind", "host");
    request.pushKV("method", "rpc.confirmed");
    request.pushKV("params", std::move(params));
    return request;
}

UniValue ChainAdapter::AssetSendRequest(const std::string& wallet, const std::string& address, const std::string& asset_id, const std::string& amount)
{
    // By its hash, never by a name (two assets may look alike): "0x" and the hash, as the chain's commands take it.
    return ConfirmedRequest(wallet, "sendasset", Array({UniValue{address}, UniValue{"0x" + asset_id}, UniValue{amount}}));
}

UniValue ChainAdapter::ShieldedSendRequest(const std::string& wallet, const std::string& address, const std::string& amount, const std::string& memo)
{
    UniValue amounts{UniValue::VOBJ};
    amounts.pushKV(address, amount);
    UniValue args{UniValue::VARR};
    args.push_back(std::move(amounts));
    if (!memo.empty()) args.push_back(memo);
    return ConfirmedRequest(wallet, "sendshielded", args);
}

UniValue ChainAdapter::WithdrawRequest(const std::string& wallet, const std::string& mainchain_address, const std::string& amount, const std::string& fee)
{
    return ConfirmedRequest(wallet, "createwithdrawal", Array({UniValue{mainchain_address}, UniValue{amount}, UniValue{fee}}));
}

// ---- Over the nodes ----

const ChainAdapter::Capabilities& ChainAdapter::Caps(const UniValue& params, std::string& stem) const
{
    stem = Str(params, "stem");
    const Capabilities* caps{Find(stem)};
    if (!caps) throw HostError(BRIDGE_INVALID_REQUEST, "No such sidechain");
    return *caps;
}

std::string ChainAdapter::Wallet(const std::string& stem, const UniValue& params) const
{
    if (!m_hooks.running(stem)) throw HostError(BRIDGE_NOT_READY, strprintf("The %s node is not running", m_hooks.name(stem)));
    const UniValue loaded{m_hooks.rpc(stem, "listwallets", UniValue{UniValue::VARR}, "")};
    const std::string wallet{PickWallet(loaded, Str(params, "wallet"))};
    if (wallet.empty()) throw HostError(-18, strprintf("%s has no wallet loaded", m_hooks.name(stem)));
    return wallet;
}

UniValue ChainAdapter::PageRead(const std::string& stem, const std::string& method, const UniValue& params, const std::string& wallet) const
{
    UniValue request{UniValue::VOBJ};
    request.pushKV("kind", "rpc");
    request.pushKV("method", method);
    request.pushKV("params", params);
    if (!wallet.empty()) request.pushKV("wallet", wallet);
    const UniValue answer{m_hooks.rpc(stem, "webuihost", Array({request}), "")};
    // A read never asks: one that comes back with a question is not run, and not asked either.
    if (answer.find_value("confirm").isObject()) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("%s asked a question for a read (%s): not asked", m_hooks.name(stem), method));
    return answer.find_value("result");
}

UniValue ChainAdapter::CheckAddress(const std::string& stem, const std::string& address, bool shielded) const
{
    const std::string name{m_hooks.name(stem)};
    const std::string product{m_hooks.product ? m_hooks.product() : std::string{"the mainchain"}};
    std::string hrp;
    const AddressKind kind{KindOf(address, &hrp)};
    if (kind == AddressKind::EMPTY) throw HostError(WRONG_ADDRESS, strprintf("Enter an address of %s.", name));
    if (kind == AddressKind::DEPOSIT) {
        throw HostError(WRONG_ADDRESS, strprintf("This is a deposit address: it moves coins from %s into a sidechain (Sidechains → Deposit). Enter an address of %s.", product, name));
    }
    const auto valid_on{[&](const std::string& other) {
        try {
            return PageRead(other, "validateaddress", Array({UniValue{address}}), "").find_value("isvalid").isTrue();
        } catch (const std::exception&) {
            return false;
        }
    }};
    UniValue main{UniValue::VOBJ};
    try {
        if (m_hooks.mainchain_address) main = m_hooks.mainchain_address(address);
    } catch (const std::exception&) {
    }
    const std::vector<std::string> others{[&] {
        std::vector<std::string> list{m_hooks.running_chains ? m_hooks.running_chains() : std::vector<std::string>{}};
        std::erase(list, stem);
        return list;
    }()};
    const bool valid{valid_on(stem)};
    if (!valid) {
        if (main.find_value("isvalid").isTrue()) {
            throw HostError(WRONG_ADDRESS, strprintf("This is an address of %s, not of %s. To move coins back to %s, withdraw them on the Sidechains page.", product, name, product));
        }
        for (const std::string& other : others) {
            if (valid_on(other)) throw HostError(WRONG_ADDRESS, strprintf("This is an address of %s, not of %s.", m_hooks.name(other), name));
        }
        // A shielded address is not one validateaddress knows: the chain's own describer checks it in its question.
        if (shielded) {
            UniValue out{UniValue::VOBJ};
            out.pushKV("address", address);
            out.pushKV("own", false);
            out.pushKV("shielded", true);
            return out;
        }
        throw HostError(WRONG_ADDRESS, strprintf("This is not an address of %s.", name));
    }
    // Legacy (base58) addresses read the same on the mainchain and every sidechain: nothing tells the chain they are for.
    if (kind == AddressKind::BASE58) {
        throw HostError(WRONG_ADDRESS, strprintf("This kind of address (legacy) reads the same on %s and on its sidechains: it cannot say which chain it is for. Ask for a bech32 address of %s.", product, name));
    }
    if (main.find_value("ismine").isTrue()) {
        throw HostError(WRONG_ADDRESS, strprintf("This address is of your %s wallet: paid on %s, the coins would not reach it.", product, name));
    }
    // An address of one of the user's own wallets on another chain (the chains of one network may share a prefix): the
    // likeliest wrong-chain send, refused.
    for (const std::string& other : others) {
        try {
            const UniValue loaded{m_hooks.rpc(other, "listwallets", UniValue{UniValue::VARR}, "")};
            for (size_t i{0}; loaded.isArray() && i < loaded.size() && i < 8; ++i) {
                if (!loaded[i].isStr()) continue;
                const UniValue info{PageRead(other, "getaddressinfo", Array({UniValue{address}}), loaded[i].get_str())};
                if (info.find_value("ismine").isTrue()) {
                    throw HostError(WRONG_ADDRESS, strprintf("This address is of your %s wallet (%s): paid on %s, the coins would not reach it.", m_hooks.name(other), loaded[i].get_str(), name));
                }
            }
        } catch (const HostError& e) {
            if (e.code == WRONG_ADDRESS) throw;
        } catch (const std::exception&) {
        }
    }
    UniValue out{UniValue::VOBJ};
    out.pushKV("address", address);
    out.pushKV("hrp", hrp);
    out.pushKV("own", false);
    return out;
}

UniValue ChainAdapter::Validate(const UniValue& params) const
{
    std::string stem;
    const Capabilities& caps{Caps(params, stem)};
    if (!m_hooks.running(stem)) throw HostError(BRIDGE_NOT_READY, strprintf("The %s node is not running", m_hooks.name(stem)));
    UniValue out{CheckAddress(stem, util::TrimString(Str(params, "address")), caps.shielded && Param(params, "shielded").isTrue())};
    // Whether it is of the wallet that pays (a send to oneself), said, not refused.
    try {
        const std::string wallet{Wallet(stem, params)};
        const UniValue info{PageRead(stem, "getaddressinfo", Array({UniValue{out.find_value("address").get_str()}}), wallet)};
        if (info.find_value("ismine").isTrue()) out.pushKV("own", true);
    } catch (const std::exception&) {
    }
    return out;
}

UniValue ChainAdapter::Summary(const UniValue& params) const
{
    std::string stem;
    const Capabilities& caps{Caps(params, stem)};
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("name", m_hooks.name(stem));
    out.pushKV("capabilities", CapabilitiesJson(caps));
    const bool running{m_hooks.running(stem)};
    out.pushKV("running", running);
    if (!running) return out;
    UniValue errors{UniValue::VOBJ};
    const auto section{[&](const char* key, const std::function<void()>& fn) {
        try {
            fn();
        } catch (const std::exception& e) {
            errors.pushKV(key, Line(e.what(), 300));
        }
    }};
    std::string wallet;
    section("wallet", [&] {
        const UniValue loaded{m_hooks.rpc(stem, "listwallets", UniValue{UniValue::VARR}, "")};
        wallet = PickWallet(loaded, Str(params, "wallet"));
        UniValue names{UniValue::VARR};
        for (size_t i{0}; loaded.isArray() && i < loaded.size() && i < 50; ++i) {
            if (loaded[i].isStr()) names.push_back(Line(loaded[i].get_str(), 64));
        }
        out.pushKV("wallets", std::move(names));
    });
    out.pushKV("wallet", wallet);
    out.pushKV("walletMode", m_hooks.wallet_mode ? m_hooks.wallet_mode(stem) : std::string{"shared"});
    section("height", [&] { out.pushKV("height", m_hooks.rpc(stem, "getblockcount", UniValue{UniValue::VARR}, "")); });
    section("mainchain", [&] {
        const UniValue m{m_hooks.rpc(stem, "getmainchaininfo", UniValue{UniValue::VARR}, "")};
        out.pushKV("mainchain", PlainFields(m, {"slot", "connected", "state", "height", "tipheight", "behind_blocks", "error"}));
    });
    if (!wallet.empty()) {
        section("balances", [&] {
            const UniValue b{m_hooks.rpc(stem, "getbalances", UniValue{UniValue::VARR}, wallet)};
            const UniValue& mine{b.find_value("mine")};
            UniValue bal{UniValue::VOBJ};
            bal.pushKV("trusted", AmountText(mine.find_value("trusted")));
            bal.pushKV("pending", AmountText(mine.find_value("untrusted_pending")));
            bal.pushKV("immature", AmountText(mine.find_value("immature")));
            out.pushKV("balances", std::move(bal));
        });
        if (caps.assets) section("assets", [&] { out.pushKV("assets", AssetRows(PageRead(stem, "listmyassets", UniValue{UniValue::VARR}, wallet))); });
        if (caps.shielded) {
            section("shielded", [&] {
                const UniValue s{PageRead(stem, "getshieldedbalance", UniValue{UniValue::VARR}, wallet)};
                UniValue sh{UniValue::VOBJ};
                for (const char* key : {"available", "spendable", "pending"}) sh.pushKV(key, AmountText(s.find_value(key)));
                out.pushKV("shielded", std::move(sh));
            });
        }
        // Withdrawals this wallet made that the mainchain has not paid yet (their refund address is the wallet's).
        section("withdrawals", [&] {
            const UniValue list{PageRead(stem, "listwithdrawals", UniValue{UniValue::VARR}, "")};
            int64_t total{0};
            int count{0};
            for (size_t i{0}; list.isArray() && i < list.size() && i < 40; ++i) {
                const std::string refund{Str(list[i], "refundaddress")};
                if (refund.empty()) continue;
                const UniValue info{PageRead(stem, "getaddressinfo", Array({UniValue{refund}}), wallet)};
                if (!info.find_value("ismine").isTrue()) continue;
                if (const auto units{ParseUnits(AmountText(list[i].find_value("amount")), COIN_DECIMALS)}) total += *units;
                ++count;
            }
            UniValue w{UniValue::VOBJ};
            w.pushKV("count", count);
            w.pushKV("amount", FormatUnits(total, COIN_DECIMALS));
            out.pushKV("withdrawals", std::move(w));
        });
    }
    out.pushKV("errors", std::move(errors));
    return out;
}

UniValue ChainAdapter::Transactions(const UniValue& params) const
{
    std::string stem;
    Caps(params, stem);
    const std::string wallet{Wallet(stem, params)};
    const int64_t count{std::clamp<int64_t>(Param(params, "count").isNum() ? Param(params, "count").getInt<int64_t>() : 200, 1, 500)};
    const UniValue listed{m_hooks.rpc(stem, "listtransactions", Array({UniValue{"*"}, UniValue{count}}), wallet)};
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("wallet", wallet);
    out.pushKV("rows", TransactionRows(listed, static_cast<size_t>(count)));
    return out;
}

UniValue ChainAdapter::Transaction(const UniValue& params) const
{
    std::string stem;
    Caps(params, stem);
    const std::string wallet{Wallet(stem, params)};
    const std::string txid{Str(params, "txid")};
    if (!IsHex64(txid)) throw HostError(BRIDGE_INVALID_REQUEST, "txid: 64 hexadecimal digits");
    UniValue p{UniValue::VOBJ};
    p.pushKV("wallet", wallet);
    p.pushKV("txid", txid);
    p.pushKV("idx", Param(params, "idx").isNum() ? Param(params, "idx").getInt<int>() : 0);
    p.pushKV("address", Line(Str(params, "address"), 120));
    UniValue request{UniValue::VOBJ};
    request.pushKV("kind", "host");
    request.pushKV("method", "wallet.transaction");
    request.pushKV("params", std::move(p));
    // The details the kit describes (the same as the host's own, TxDetailsView): a read, never asked.
    const UniValue answer{m_hooks.rpc(stem, "webuihost", Array({request}), "")};
    if (answer.find_value("confirm").isObject()) throw HostError(BRIDGE_INVALID_REQUEST, "A question for a read: not asked");
    return answer.find_value("result");
}

UniValue ChainAdapter::Wallets(const UniValue& params) const
{
    std::string stem;
    Caps(params, stem);
    if (!m_hooks.running(stem)) throw HostError(BRIDGE_NOT_READY, strprintf("The %s node is not running", m_hooks.name(stem)));
    const UniValue loaded{m_hooks.rpc(stem, "listwallets", UniValue{UniValue::VARR}, "")};
    std::set<std::string> on;
    for (const UniValue& w : loaded.getValues()) {
        if (w.isStr()) on.insert(w.get_str());
    }
    std::set<std::string> all{on};
    try {
        const UniValue dir{m_hooks.rpc(stem, "listwalletdir", UniValue{UniValue::VARR}, "")};
        for (const UniValue& w : dir.find_value("wallets").getValues()) {
            if (all.size() >= 50) break;
            if (Str(w, "name").size() <= 64) all.insert(Str(w, "name"));
        }
    } catch (const std::exception&) {
    }
    UniValue list{UniValue::VARR};
    for (const std::string& name : all) {
        UniValue w{UniValue::VOBJ};
        w.pushKV("name", Line(name, 64));
        w.pushKV("loaded", on.contains(name));
        if (on.contains(name)) {
            try {
                const UniValue info{m_hooks.rpc(stem, "getwalletinfo", UniValue{UniValue::VARR}, name)};
                w.pushKV("txcount", info.find_value("txcount").isNum() ? info.find_value("txcount").getInt<int64_t>() : 0);
                w.pushKV("encrypted", info.exists("unlocked_until"));
                w.pushKV("keys", info.find_value("private_keys_enabled").isTrue());
                const UniValue b{m_hooks.rpc(stem, "getbalances", UniValue{UniValue::VARR}, name)};
                w.pushKV("trusted", AmountText(b.find_value("mine").find_value("trusted")));
            } catch (const std::exception& e) {
                w.pushKV("error", Line(e.what(), 200));
            }
        }
        list.push_back(std::move(w));
    }
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("mode", m_hooks.wallet_mode ? m_hooks.wallet_mode(stem) : std::string{"shared"});
    out.pushKV("selected", PickWallet(loaded, Str(params, "wallet")));
    out.pushKV("wallets", std::move(list));
    return out;
}

namespace {
std::string WalletName(const UniValue& params)
{
    const std::string name{Str(params, "name")};
    // The names the app's calls of a wallet can carry (SidechainSupervisor::Rpc puts them in the path).
    if (name.empty() || name.size() > 64 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") != std::string::npos || name.starts_with('.')) {
        throw HostError(BRIDGE_INVALID_REQUEST, "A wallet name of letters, digits, '-', '_' and '.', at most 64");
    }
    return name;
}
} // namespace

UniValue ChainAdapter::WalletCreate(const UniValue& params) const
{
    std::string stem;
    Caps(params, stem);
    if (!m_hooks.running(stem)) throw HostError(BRIDGE_NOT_READY, strprintf("The %s node is not running", m_hooks.name(stem)));
    return m_hooks.rpc(stem, "createwallet", Array({UniValue{WalletName(params)}}), "");
}

UniValue ChainAdapter::WalletLoad(const UniValue& params) const
{
    std::string stem;
    Caps(params, stem);
    if (!m_hooks.running(stem)) throw HostError(BRIDGE_NOT_READY, strprintf("The %s node is not running", m_hooks.name(stem)));
    return m_hooks.rpc(stem, "loadwallet", Array({UniValue{WalletName(params)}}), "");
}

UniValue ChainAdapter::WalletUnload(const UniValue& params) const
{
    std::string stem;
    Caps(params, stem);
    if (!m_hooks.running(stem)) throw HostError(BRIDGE_NOT_READY, strprintf("The %s node is not running", m_hooks.name(stem)));
    UniValue p{UniValue::VOBJ};
    p.pushKV("name", WalletName(params));
    UniValue request{UniValue::VOBJ};
    request.pushKV("kind", "host");
    request.pushKV("method", "wallets.close");
    request.pushKV("params", std::move(p));
    return m_hooks.call(stem, request);
}

UniValue ChainAdapter::WalletBackup(const UniValue& params) const
{
    std::string stem;
    Caps(params, stem);
    const std::string wallet{Wallet(stem, params)};
    // The app's own chooser (for this chain's host: the file is given to that node only, by a token), then the backup.
    UniValue choose{UniValue::VOBJ};
    choose.pushKV("save", true);
    choose.pushKV("title", strprintf("Back up the %s wallet %s", m_hooks.name(stem), wallet));
    choose.pushKV("name", strprintf("%s-%s.backup", stem, wallet));
    UniValue request{UniValue::VOBJ};
    request.pushKV("kind", "host");
    request.pushKV("method", "file.choose");
    request.pushKV("params", std::move(choose));
    const UniValue chosen{m_hooks.call(stem, request)};
    if (!chosen.isStr()) return UniValue{};
    UniValue p{UniValue::VOBJ};
    p.pushKV("wallet", wallet);
    p.pushKV("file", chosen.get_str());
    UniValue backup{UniValue::VOBJ};
    backup.pushKV("kind", "host");
    backup.pushKV("method", "wallet.backup");
    backup.pushKV("params", std::move(p));
    return m_hooks.call(stem, backup);
}

UniValue ChainAdapter::Receive(const UniValue& params) const
{
    std::string stem;
    const Capabilities& caps{Caps(params, stem)};
    const std::string wallet{Wallet(stem, params)};
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("wallet", wallet);
    if (caps.shielded && Param(params, "shielded").isTrue()) {
        out.pushKV("address", PageRead(stem, "getshieldedaddress", UniValue{UniValue::VARR}, wallet));
        out.pushKV("shielded", true);
        return out;
    }
    UniValue p{UniValue::VOBJ};
    p.pushKV("wallet", wallet);
    p.pushKV("label", Line(Str(params, "label"), 100));
    UniValue request{UniValue::VOBJ};
    request.pushKV("kind", "host");
    request.pushKV("method", "wallet.newAddress");
    request.pushKV("params", std::move(p));
    // A new address asks nothing, unless the wallet needs its keys to make more (its question: "Unlock the wallet").
    out.pushKV("address", m_hooks.call(stem, request));
    return out;
}

UniValue ChainAdapter::DepositAddress(const UniValue& params) const
{
    std::string stem;
    Caps(params, stem);
    const std::string wallet{Wallet(stem, params)};
    const UniValue dep{m_hooks.rpc(stem, "getdepositaddress", UniValue{UniValue::VARR}, wallet)};
    UniValue out{PlainFields(dep, {"depositaddress", "address", "slot"})};
    out.pushKV("wallet", wallet);
    return out;
}

UniValue ChainAdapter::Send(const UniValue& params) const
{
    std::string stem;
    const Capabilities& caps{Caps(params, stem)};
    const std::string wallet{Wallet(stem, params)};
    const std::string name{m_hooks.name(stem)};
    const std::string address{util::TrimString(Str(params, "address"))};
    const std::string asset{Str(params, "asset")};
    const bool shielded{Param(params, "shielded").isTrue()};
    if (!asset.empty() && !caps.assets) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("%s has no assets", name));
    if (shielded && !caps.shielded) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("%s has no shielded account", name));
    if (!asset.empty() && shielded) throw HostError(BRIDGE_INVALID_REQUEST, "An asset is not sent shielded");
    if (!Param(params, "amount").isStr()) throw HostError(BRIDGE_INVALID_REQUEST, "amount: the amount as text");
    const std::string amount{util::TrimString(Str(params, "amount"))};
    // Wrong chains first: before anything is prepared or asked.
    CheckAddress(stem, address, shielded);
    UniValue result;
    if (!asset.empty()) {
        if (!IsHex64(asset)) throw HostError(BRIDGE_INVALID_REQUEST, "asset: the hash of the asset");
        const UniValue rows{AssetRows(PageRead(stem, "listmyassets", UniValue{UniValue::VARR}, wallet))};
        const UniValue* row{nullptr};
        for (const UniValue& r : rows.getValues()) {
            if (r.find_value("id").get_str() == asset) row = &r;
        }
        if (!row) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("The %s wallet %s holds no such asset", name, wallet));
        const int decimals{row->find_value("decimals").getInt<int>()};
        const auto units{ParseUnits(amount, decimals)};
        if (!units || *units <= 0) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("The amount: a positive number with at most %d decimals", decimals));
        const auto held{ParseUnits(row->find_value("balance").get_str(), decimals)};
        if (held && *units > *held) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("More than the wallet holds (%s %s)", row->find_value("balance").get_str(), row->find_value("label").get_str()));
        result = m_hooks.call(stem, AssetSendRequest(wallet, address, asset, FormatUnits(*units, decimals)));
    } else {
        const auto sats{ParseUnits(amount, COIN_DECIMALS)};
        if (!sats || *sats <= 0 || !MoneyRange(*sats)) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("The amount: a positive number with at most %d decimals", COIN_DECIMALS));
        if (shielded) {
            const std::string memo{Str(params, "memo")};
            if (memo.size() > 512) throw HostError(BRIDGE_INVALID_REQUEST, "The memo: at most 512 bytes");
            result = m_hooks.call(stem, ShieldedSendRequest(wallet, address, FormatUnits(*sats, COIN_DECIMALS), memo));
        } else {
            // Made, not signed, by the chain's wallet; its question (destinations, amounts, fee, change) is its own.
            const UniValue prepared{m_hooks.call(stem, PrepareSendRequest(wallet, address, *sats, Param(params, "subtractFee").isTrue()))};
            const std::string token{Str(prepared, "token")};
            if (token.empty() || token.size() > 128) throw HostError(strprintf("%s did not prepare the send", name));
            try {
                result = m_hooks.call(stem, CommitSendRequest(wallet, token));
            } catch (...) {
                // Declined or failed: the prepared send goes (the kit keeps a few).
                UniValue p{UniValue::VOBJ};
                p.pushKV("token", token);
                UniValue discard{UniValue::VOBJ};
                discard.pushKV("kind", "host");
                discard.pushKV("method", "wallet.send.discard");
                discard.pushKV("params", std::move(p));
                try {
                    m_hooks.call(stem, discard);
                } catch (const std::exception&) {
                }
                throw;
            }
        }
    }
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("wallet", wallet);
    if (result.isStr() && IsHex64(result.get_str())) out.pushKV("txid", result.get_str());
    else if (IsHex64(Str(result, "txid"))) out.pushKV("txid", Str(result, "txid"));
    out.pushKV("result", Bounded(result));
    return out;
}

UniValue ChainAdapter::Withdraw(const UniValue& params) const
{
    std::string stem;
    Caps(params, stem);
    const std::string wallet{Wallet(stem, params)};
    const std::string product{m_hooks.product ? m_hooks.product() : std::string{"the mainchain"}};
    const std::string address{util::TrimString(Str(params, "address"))};
    if (KindOf(address) == AddressKind::DEPOSIT) throw HostError(WRONG_ADDRESS, strprintf("A withdrawal pays an address of %s, not a deposit address.", product));
    const UniValue main{m_hooks.mainchain_address ? m_hooks.mainchain_address(address) : UniValue{}};
    if (!main.find_value("isvalid").isTrue()) throw HostError(WRONG_ADDRESS, strprintf("A withdrawal pays an address of %s: this is not one.", product));
    const auto amount{ParseUnits(util::TrimString(Str(params, "amount")), COIN_DECIMALS)};
    if (!amount || *amount <= 0 || !MoneyRange(*amount)) throw HostError(BRIDGE_INVALID_REQUEST, "The amount: a positive number with at most 8 decimals");
    const std::string fee_text{Param(params, "fee").isStr() && !Str(params, "fee").empty() ? util::TrimString(Str(params, "fee")) : std::string{"0.0001"}};
    const auto fee{ParseUnits(fee_text, COIN_DECIMALS)};
    if (!fee || !MoneyRange(*fee)) throw HostError(BRIDGE_INVALID_REQUEST, "The fee: a number with at most 8 decimals");
    const UniValue result{m_hooks.call(stem, WithdrawRequest(wallet, address, FormatUnits(*amount, COIN_DECIMALS), FormatUnits(*fee, COIN_DECIMALS)))};
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    if (IsHex64(Str(result, "txid"))) out.pushKV("txid", Str(result, "txid"));
    out.pushKV("result", Bounded(result));
    return out;
}

UniValue ChainAdapter::Deposit(const UniValue& params) const
{
    std::string stem;
    Caps(params, stem);
    const std::string wallet{Wallet(stem, params)};
    const auto amount{ParseUnits(util::TrimString(Str(params, "amount")), COIN_DECIMALS)};
    if (!amount || *amount <= 0 || !MoneyRange(*amount)) throw HostError(BRIDGE_INVALID_REQUEST, "The amount: a positive number with at most 8 decimals");
    // The deposit address of that chain's wallet, from its node (never typed): Chains' own question names its slot.
    const UniValue dep{m_hooks.rpc(stem, "getdepositaddress", UniValue{UniValue::VARR}, wallet)};
    if (Str(dep, "depositaddress").empty()) throw HostError(strprintf("%s gave no deposit address", m_hooks.name(stem)));
    UniValue p{UniValue::VOBJ};
    p.pushKV("destination", Str(dep, "depositaddress"));
    p.pushKV("amount", FormatUnits(*amount, COIN_DECIMALS));
    const UniValue result{m_hooks.deposit(stem, p)};
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("wallet", wallet);
    out.pushKV("address", Str(dep, "address"));
    if (IsHex64(Str(result, "txid"))) out.pushKV("txid", Str(result, "txid"));
    out.pushKV("result", Bounded(result));
    return out;
}

UniValue ChainAdapter::Staking(const UniValue& params) const
{
    std::string stem;
    const Capabilities& caps{Caps(params, stem)};
    if (!caps.staking) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("%s does not stake", m_hooks.name(stem)));
    const std::string wallet{Wallet(stem, params)};
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("wallet", wallet);
    UniValue errors{UniValue::VOBJ};
    for (const auto& [key, method, with_wallet] : {std::tuple{"node", "getstakinginfo", false}, std::tuple{"chain", "getposinfo", false}, std::tuple{"set", "getvalidators", false}, std::tuple{"bonds", "listbonds", true}}) {
        try {
            out.pushKV(key, Bounded(PageRead(stem, method, UniValue{UniValue::VARR}, with_wallet ? wallet : std::string{})));
        } catch (const std::exception& e) {
            errors.pushKV(key, Line(e.what(), 300));
        }
    }
    out.pushKV("errors", std::move(errors));
    return out;
}

UniValue ChainAdapter::Peg(const UniValue& params) const
{
    std::string stem;
    Caps(params, stem);
    const std::string wallet{Wallet(stem, params)};
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("wallet", wallet);
    out.pushKV("mainchain", PlainFields(m_hooks.rpc(stem, "getmainchaininfo", UniValue{UniValue::VARR}, ""), {"slot", "connected", "state", "height", "tipheight", "behind_blocks", "error"}));
    out.pushKV("bmm", PlainFields(m_hooks.rpc(stem, "getbmminfo", UniValue{UniValue::VARR}, ""), {"mining", "address", "always", "amount", "idle", "lastfees", "lastoffer", "requests", "blocks", "outbid", "error", "candidates"}));
    UniValue rows{UniValue::VARR};
    const UniValue list{PageRead(stem, "listwithdrawals", UniValue{UniValue::VARR}, "")};
    for (size_t i{0}; list.isArray() && i < list.size() && i < 100; ++i) {
        UniValue row{PlainFields(list[i], {"txid", "vout", "amount", "mainchainfee", "refundaddress", "height", "status"})};
        bool mine{false};
        if (i < 40 && !Str(list[i], "refundaddress").empty()) {
            try {
                mine = PageRead(stem, "getaddressinfo", Array({list[i].find_value("refundaddress")}), wallet).find_value("ismine").isTrue();
            } catch (const std::exception&) {
            }
        }
        row.pushKV("mine", mine);
        rows.push_back(std::move(row));
    }
    out.pushKV("withdrawals", std::move(rows));
    return out;
}

UniValue ChainAdapter::Assets(const UniValue& params) const
{
    std::string stem;
    const Capabilities& caps{Caps(params, stem)};
    if (!caps.assets) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("%s has no assets", m_hooks.name(stem)));
    const std::string wallet{Wallet(stem, params)};
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("wallet", wallet);
    out.pushKV("assets", AssetRows(PageRead(stem, "listmyassets", UniValue{UniValue::VARR}, wallet)));
    return out;
}

UniValue ChainAdapter::Names(const UniValue& params) const
{
    std::string stem;
    const Capabilities& caps{Caps(params, stem)};
    if (!caps.names) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("%s has no names", m_hooks.name(stem)));
    const std::string wallet{Wallet(stem, params)};
    const UniValue list{PageRead(stem, "listmynames", UniValue{UniValue::VARR}, wallet)};
    UniValue rows{UniValue::VARR};
    for (size_t i{0}; list.isArray() && i < list.size() && i < 200; ++i) rows.push_back(PlainFields(list[i], {"kind", "name", "namehash", "seq", "address", "txid", "vout"}));
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("wallet", wallet);
    out.pushKV("names", std::move(rows));
    return out;
}

UniValue ChainAdapter::Markets(const UniValue& params) const
{
    std::string stem;
    const Capabilities& caps{Caps(params, stem)};
    if (!caps.markets) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("%s has no markets", m_hooks.name(stem)));
    if (!m_hooks.running(stem)) throw HostError(BRIDGE_NOT_READY, strprintf("The %s node is not running", m_hooks.name(stem)));
    const UniValue list{PageRead(stem, "listmarkets", UniValue{UniValue::VARR}, "")};
    UniValue rows{UniValue::VARR};
    for (size_t i{0}; list.isArray() && i < list.size() && i < 200; ++i) {
        UniValue row{PlainFields(list[i], {"id", "text", "status", "liquidity", "feebps", "height", "treasury", "fees"})};
        UniValue states{UniValue::VARR};
        const UniValue& s{list[i].find_value("states")};
        for (size_t j{0}; s.isArray() && j < s.size() && j < 16; ++j) states.push_back(PlainFields(s[j], {"state", "answers", "shares", "price"}));
        row.pushKV("states", std::move(states));
        rows.push_back(std::move(row));
    }
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("markets", std::move(rows));
    return out;
}

} // namespace webui
