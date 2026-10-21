// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The DEX of the chains this app runs, for its Trade page (doc/unified-app.md §22): the pools, quotes and routes, the
// wallet's shares, a pool's chart, read through each chain's host over RPC as reads of its own pages' policy; swaps
// and liquidity built here from checked fields (assets by their hash, amounts exact with their decimals, only what
// the wallet holds, a swap quoted again and its route checked against the pools now) and asked in the trusted
// dialog by the chain's own describers, with what the app checked shown before the chain's rows.

#include <webui/chainadapter.h>

#include <tinyformat.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <webui/bridge.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
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

//! Text of a node, for a page: one bounded line, control characters out.
std::string Line(const std::string& text, size_t max)
{
    std::string out;
    for (const unsigned char c : text) {
        if (out.size() >= max) break;
        out.push_back(c < 0x20 || c == 0x7f ? ' ' : static_cast<char>(c));
    }
    return out;
}

UniValue Array(std::initializer_list<UniValue> items)
{
    UniValue out{UniValue::VARR};
    for (const UniValue& i : items) out.push_back(i);
    return out;
}

UniValue Number(const std::string& text)
{
    UniValue v;
    v.setNumStr(text);
    return v;
}

constexpr int COIN_DECIMALS{8};
const std::string COIN_ID(64, '0');

//! A plain decimal as a node writes an amount (no sign, no exponent): its text, else "".
std::string AmountOf(const UniValue& v)
{
    const std::string text{ChainAdapter::AmountText(v)};
    if (text.empty() || text.size() > 40) return {};
    const size_t dot{text.find('.')};
    const auto digits{[](std::string_view s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); }};
    if (dot == std::string::npos) return digits(text) ? text : std::string{};
    return digits(std::string_view{text}.substr(0, dot)) && digits(std::string_view{text}.substr(dot + 1)) ? text : std::string{};
}

double Real(const std::string& text)
{
    return text.empty() ? 0.0 : std::strtod(text.c_str(), nullptr);
}

int64_t Int(const UniValue& v, int64_t otherwise = 0)
{
    if (!v.isNum()) return otherwise;
    const std::string& t{v.getValStr()};
    if (t.find_first_of(".eE-") != std::string::npos || t.size() > 18) return otherwise;
    return v.getInt<int64_t>();
}

const std::vector<int64_t> FEE_TIERS{100, 500, 3000, 10000};
const std::vector<int64_t> WEIGHTS{2, 5, 10, 20, 25, 40, 60, 75, 80, 90, 95, 98};
const std::vector<std::string> CURVES{"constant-product", "stable", "weighted"};
constexpr int64_t LEGACY_FEE{3000};
constexpr int64_t MAX_AMP{10000};

//! A fee of millionths as a percentage: "0.30 %".
std::string FeeText(int64_t millionths)
{
    return strprintf("%d.%02d %%", millionths / 10000, (millionths % 10000) / 100);
}

//! A pool's kind in words: "constant product · 0.30 %", "stable (A 100) · 0.05 %", "weighted 80/20 · 0.30 %".
std::string KindWords(const UniValue& row)
{
    const std::string curve{Str(row, "curve")};
    const int64_t fee{Int(Param(row, "fee"), LEGACY_FEE)};
    std::string what{"constant product"};
    if (curve == "stable") what = strprintf("stable (A %d)", Int(Param(row, "amp")));
    if (curve == "weighted") what = strprintf("weighted %d/%d", Int(Param(row, "weightA")), 100 - Int(Param(row, "weightA")));
    return what + " · " + FeeText(fee);
}

//! The name of a curve a node gives (a name or a number), as the app names it; "" when not one.
std::string CurveOf(const UniValue& v)
{
    if (v.isNull()) return "constant-product";
    if (v.isNum()) {
        const int64_t n{Int(v, -1)};
        return n >= 0 && n < static_cast<int64_t>(CURVES.size()) ? CURVES[n] : std::string{};
    }
    if (!v.isStr()) return {};
    std::string s{ToLower(v.get_str())};
    std::replace(s.begin(), s.end(), '_', '-');
    std::replace(s.begin(), s.end(), ' ', '-');
    if (s == "constant-product" || s == "cpmm" || s == "xyk") return "constant-product";
    if (s == "stable" || s == "stableswap") return "stable";
    if (s == "weighted") return "weighted";
    return {};
}

//! Whether a read failed because the chain's pages' policy does not offer the method (an older node): then the app
//! does what every DEX does instead.
bool NotOffered(const std::exception& e)
{
    const std::string what{e.what()};
    return what.find("does not call") != std::string::npos || what.find("not offered") != std::string::npos || what.find("Method not found") != std::string::npos;
}

UniValue Row(const std::string& label, const std::string& value, const std::string& kind = "text")
{
    UniValue r{UniValue::VOBJ};
    r.pushKV("label", label);
    r.pushKV("value", value);
    r.pushKV("kind", kind);
    return r;
}

std::string Joined(const std::vector<std::string>& parts, const std::string& sep)
{
    std::string out;
    for (size_t i{0}; i < parts.size(); ++i) out += (i ? sep : "") + parts[i];
    return out;
}
} // namespace

// ---- Pure parts ----

std::string ChainAdapter::AssetIdOf(const UniValue& value)
{
    if (!value.isStr()) return {};
    std::string s{value.get_str()};
    if (s.size() == 66 && s.starts_with("0x")) s = s.substr(2);
    return IsHex64(s) ? s : std::string{};
}

bool ChainAdapter::IsCoinId(const std::string& id)
{
    return id == COIN_ID;
}

UniValue ChainAdapter::PoolRow(const UniValue& n)
{
    if (!n.isObject()) return UniValue{};
    const std::string pool{Str(n, "pool")};
    const std::string a{AssetIdOf(Param(n, "asset_a_id"))}, b{AssetIdOf(Param(n, "asset_b_id"))};
    const std::string ra{AmountOf(Param(n, "reserve_a"))}, rb{AmountOf(Param(n, "reserve_b"))};
    if (!IsHex64(pool) || a.empty() || b.empty() || a == b || ra.empty() || rb.empty()) return UniValue{};
    const std::string curve{CurveOf(Param(n, "curve"))};
    if (curve.empty()) return UniValue{};
    int64_t fee{Int(Param(n, "fee"), LEGACY_FEE)};
    if (std::find(FEE_TIERS.begin(), FEE_TIERS.end(), fee) == FEE_TIERS.end()) fee = LEGACY_FEE;
    const auto asset{[&](const std::string& id, const std::string& key) {
        UniValue o{UniValue::VOBJ};
        o.pushKV("id", id);
        o.pushKV("label", Line(Str(n, key), 40));
        return o;
    }};
    UniValue row{UniValue::VOBJ};
    row.pushKV("pool", pool);
    row.pushKV("a", asset(a, "asset_a"));
    row.pushKV("b", asset(b, "asset_b"));
    row.pushKV("reserveA", ra);
    row.pushKV("reserveB", rb);
    const std::string price{AmountOf(Param(n, "price"))};
    row.pushKV("price", price.empty() ? UniValue{} : UniValue{price});
    const std::string shares{AmountOf(Param(n, "shares"))};
    row.pushKV("shares", shares.empty() ? std::string{"0"} : shares);
    row.pushKV("volumeA", AmountOf(Param(n, "volume_a")).empty() ? std::string{"0"} : AmountOf(Param(n, "volume_a")));
    row.pushKV("volumeB", AmountOf(Param(n, "volume_b")).empty() ? std::string{"0"} : AmountOf(Param(n, "volume_b")));
    row.pushKV("swaps", Int(Param(n, "swaps")));
    row.pushKV("created", Int(Param(n, "created")));
    row.pushKV("abandoned", Param(n, "abandoned").isTrue());
    row.pushKV("curve", curve);
    row.pushKV("fee", fee);
    row.pushKV("amp", curve == "stable" ? std::clamp<int64_t>(Int(Param(n, "amp")), 0, MAX_AMP) : 0);
    const int64_t weight{Int(Param(n, "weight_a"))};
    row.pushKV("weightA", curve == "weighted" && weight > 0 && weight < 100 ? weight : 0);
    row.pushKV("legacy", curve == "constant-product" && fee == LEGACY_FEE);
    return row;
}

std::string ChainAdapter::SlippageText(int bps)
{
    std::string text{strprintf("%d.%02d", bps / 100, bps % 100)};
    text.erase(text.find_last_not_of('0') + 1);
    if (text.back() == '.') text.pop_back();
    return text;
}

int64_t ChainAdapter::LessSlippage(int64_t units, int bps)
{
    if (units <= 0) return 0;
    const __int128 less{static_cast<__int128>(units) * (10000 - std::clamp(bps, 0, 10000)) / 10000};
    return std::max<int64_t>(1, static_cast<int64_t>(less));
}

std::string ChainAdapter::WhyNotPoolKind(const std::string& curve, int64_t fee, int64_t amp, int64_t weight_a)
{
    if (std::find(CURVES.begin(), CURVES.end(), curve) == CURVES.end()) return "A curve: constant product, stable or weighted";
    if (std::find(FEE_TIERS.begin(), FEE_TIERS.end(), fee) == FEE_TIERS.end()) return "A fee tier: 0.01 %, 0.05 %, 0.30 % or 1.00 %";
    if (curve == "stable") {
        if (amp < 1 || amp > MAX_AMP) return "The amplification of a stable pool: 1 to 10 000";
        if (weight_a != 0) return "A stable pool has no weights";
    } else if (curve == "weighted") {
        if (std::find(WEIGHTS.begin(), WEIGHTS.end(), weight_a) == WEIGHTS.end()) return "The weight of the first asset: 2, 5, 10, 20, 25, 40, 60, 75, 80, 90, 95 or 98 %";
        if (amp != 0) return "A weighted pool has no amplification";
    } else if (amp != 0 || weight_a != 0) {
        return "A constant-product pool has no amplification and no weights";
    }
    return {};
}

UniValue ChainAdapter::SwapRequest(const std::string& wallet, const std::vector<std::string>& path, const std::string& amount, int slippage_bps, const std::string& min_out)
{
    if (path.size() == 2) {
        // In one pool: what every DEX of these chains does (its default bound on the price impact kept).
        return ConfirmedRequest(wallet, "swapasset", Array({UniValue{"0x" + path[0]}, UniValue{amount}, UniValue{"0x" + path[1]}, Number(SlippageText(slippage_bps)), UniValue{}, UniValue{min_out}}));
    }
    UniValue assets{UniValue::VARR};
    for (const std::string& id : path) assets.push_back("0x" + id);
    return ConfirmedRequest(wallet, "swaproute", Array({assets, UniValue{amount}, Number(SlippageText(slippage_bps)), UniValue{min_out}}));
}

UniValue ChainAdapter::AddLiquidityRequest(const std::string& wallet, const std::string& a, const std::string& amount_a, const std::string& b, const std::string& amount_b, int slippage_bps, std::optional<bool> sets_price)
{
    return ConfirmedRequest(wallet, "addliquidity", Array({UniValue{"0x" + a}, UniValue{amount_a}, UniValue{"0x" + b}, amount_b.empty() ? UniValue{} : UniValue{amount_b}, Number(SlippageText(slippage_bps)), sets_price ? UniValue{*sets_price} : UniValue{}}));
}

UniValue ChainAdapter::RemoveLiquidityRequest(const std::string& wallet, const std::string& a, const std::string& b, const std::string& percent, int slippage_bps)
{
    return ConfirmedRequest(wallet, "removeliquidity", Array({UniValue{"0x" + a}, UniValue{"0x" + b}, Number(percent), Number(SlippageText(slippage_bps))}));
}

UniValue ChainAdapter::CreatePoolRequest(const std::string& wallet, const std::string& a, const std::string& amount_a, const std::string& b, const std::string& amount_b, const std::string& curve, int64_t fee, int64_t amp, int64_t weight_a, int slippage_bps)
{
    // A constant-product pool at 0.30 % is the pool addliquidity makes, on every chain; the amounts set its price.
    if (curve == "constant-product" && fee == LEGACY_FEE) return AddLiquidityRequest(wallet, a, amount_a, b, amount_b, slippage_bps, true);
    // Any other kind: Layers' createpool (dex-curves), its kind as an object of the fields the wizard chose.
    UniValue kind{UniValue::VOBJ};
    kind.pushKV("curve", curve);
    kind.pushKV("fee", fee);
    if (curve == "stable") kind.pushKV("amp", amp);
    if (curve == "weighted") kind.pushKV("weight_a", weight_a);
    return ConfirmedRequest(wallet, "createpool", Array({UniValue{"0x" + a}, UniValue{amount_a}, UniValue{"0x" + b}, UniValue{amount_b}, kind, Number(SlippageText(slippage_bps))}));
}

// ---- Over the nodes ----

UniValue ChainAdapter::CallWithRows(const std::string& stem, const UniValue& request, const UniValue& rows) const
{
    if (m_hooks.call_rows) return m_hooks.call_rows(stem, request, rows);
    return m_hooks.call(stem, request);
}

const ChainAdapter::Capabilities& ChainAdapter::DexCaps(const UniValue& params, std::string& stem) const
{
    const Capabilities& caps{Caps(params, stem)};
    if (!caps.dex) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("%s has no market of assets", m_hooks.name(stem)));
    if (!m_hooks.running(stem)) throw HostError(BRIDGE_NOT_READY, strprintf("The %s node is not running", m_hooks.name(stem)));
    return caps;
}

std::pair<std::string, int> ChainAdapter::AssetOn(const std::string& stem, const std::string& id) const
{
    if (IsCoinId(id)) return {"", COIN_DECIMALS};
    UniValue asset;
    try {
        asset = PageRead(stem, "getasset", Array({UniValue{"0x" + id}}), "");
    } catch (const HostError&) {
        throw;
    } catch (const std::exception&) {
    }
    const int64_t decimals{Int(Param(asset, "decimals"), -1)};
    if (!asset.isObject() || decimals < 0 || decimals > 18) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("No such asset on %s", m_hooks.name(stem)));
    return {Line(Str(asset, "label"), 40), static_cast<int>(decimals)};
}

int64_t ChainAdapter::HeldUnits(const std::string& stem, const std::string& wallet, const std::string& id, int decimals) const
{
    if (IsCoinId(id)) {
        const UniValue b{m_hooks.rpc(stem, "getbalances", UniValue{UniValue::VARR}, wallet)};
        const auto units{ParseUnits(WalletBalances(b.find_value("mine")).find_value("trusted").getValStr(), COIN_DECIMALS)};
        return units.value_or(0);
    }
    const UniValue rows{AssetRows(PageRead(stem, "listmyassets", UniValue{UniValue::VARR}, wallet))};
    for (const UniValue& r : rows.getValues()) {
        if (r.find_value("id").get_str() == id) return ParseUnits(r.find_value("balance").get_str(), decimals).value_or(0);
    }
    return 0;
}

UniValue ChainAdapter::PoolsOf(const std::string& stem, const std::string& id) const
{
    const UniValue listed{PageRead(stem, "listpools", Array({UniValue{"0x" + id}, UniValue{200}}), "")};
    UniValue out{UniValue::VARR};
    for (size_t i{0}; listed.isArray() && i < listed.size() && out.size() < 200; ++i) {
        UniValue row{PoolRow(listed[i])};
        if (!row.isNull()) out.push_back(std::move(row));
    }
    return out;
}

UniValue ChainAdapter::DexInfo(const UniValue& params) const
{
    std::string stem;
    const Capabilities& caps{DexCaps(params, stem)};
    bool route{caps.dex_features.route}, active{false}, curves_active{false};
    int max_hops{1};
    UniValue fee_tiers{UniValue::VARR};
    if (caps.dex_features.route || caps.dex_features.curves) {
        try {
            const UniValue info{PageRead(stem, "getdexinfo", UniValue{UniValue::VARR}, "")};
            active = Param(info, "active").isTrue();
            max_hops = static_cast<int>(std::clamp<int64_t>(Int(Param(info, "max_route_hops"), 4), 1, 4));
            // The pool kinds (dex-curves): said by the node, never guessed from a pool.
            curves_active = caps.dex_features.curves && (Param(info, "curves_active").isTrue() || Param(info, "pool_kinds_active").isTrue());
        } catch (const std::exception& e) {
            if (!NotOffered(e)) throw;
            route = false;
        }
    }
    // Routes are orders of the DEX's batches: only once its rules apply.
    if (!active) route = false;
    if (!route) max_hops = 1;
    UniValue features{UniValue::VOBJ};
    features.pushKV("pair", true);
    features.pushKV("liquidity", true);
    features.pushKV("route", route);
    features.pushKV("twap", caps.dex_features.twap && active);
    features.pushKV("curves", curves_active);
    features.pushKV("orders", caps.dex_features.orders);
    features.pushKV("stats", caps.dex_features.stats);
    UniValue curves{UniValue::VARR};
    if (curves_active) {
        for (const std::string& c : CURVES) curves.push_back(c);
        for (const int64_t f : FEE_TIERS) fee_tiers.push_back(f);
    } else {
        curves.push_back("constant-product");
        fee_tiers.push_back(LEGACY_FEE);
    }
    UniValue weights{UniValue::VARR};
    if (curves_active) {
        for (const int64_t w : WEIGHTS) weights.push_back(w);
    }
    UniValue amp{UniValue::VOBJ};
    amp.pushKV("min", 1);
    amp.pushKV("max", MAX_AMP);
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("features", std::move(features));
    out.pushKV("active", active);
    out.pushKV("maxHops", max_hops);
    out.pushKV("curvesActive", curves_active);
    out.pushKV("curves", std::move(curves));
    out.pushKV("feeTiers", std::move(fee_tiers));
    out.pushKV("amp", std::move(amp));
    out.pushKV("weights", std::move(weights));
    return out;
}

UniValue ChainAdapter::DexPools(const UniValue& params) const
{
    std::string stem;
    DexCaps(params, stem);
    UniValue rows{UniValue::VARR};
    const UniValue& asset{Param(params, "asset")};
    if (!asset.isNull()) {
        const std::string id{AssetIdOf(asset)};
        if (id.empty()) throw HostError(BRIDGE_INVALID_REQUEST, "asset: the hash of an asset");
        rows = PoolsOf(stem, id);
    } else {
        const UniValue listed{PageRead(stem, "listpools", Array({UniValue{}, UniValue{200}}), "")};
        for (size_t i{0}; listed.isArray() && i < listed.size() && rows.size() < 200; ++i) {
            UniValue row{PoolRow(listed[i])};
            if (!row.isNull()) rows.push_back(std::move(row));
        }
    }
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    // The tip and how long a block takes there (its last 100): what an estimate of a yearly rate needs.
    int64_t height{0};
    double block_seconds{0};
    try {
        height = m_hooks.rpc(stem, "getblockcount", UniValue{UniValue::VARR}, "").getInt<int64_t>();
        const int64_t span{std::min<int64_t>(height, 100)};
        if (span > 0) {
            const auto time_at{[&](int64_t h) {
                const UniValue hash{m_hooks.rpc(stem, "getblockhash", Array({UniValue{h}}), "")};
                return Int(m_hooks.rpc(stem, "getblockheader", Array({hash}), "").find_value("time"));
            }};
            const int64_t t1{time_at(height)}, t0{time_at(height - span)};
            if (t1 > t0) block_seconds = static_cast<double>(t1 - t0) / static_cast<double>(span);
        }
    } catch (const std::exception&) {
    }
    out.pushKV("height", height);
    out.pushKV("blockSeconds", strprintf("%.3f", block_seconds));
    out.pushKV("pools", std::move(rows));
    return out;
}

UniValue ChainAdapter::DexQuote(const UniValue& params) const
{
    std::string stem;
    const Capabilities& caps{DexCaps(params, stem)};
    const std::string in{AssetIdOf(Param(params, "in"))}, out_id{AssetIdOf(Param(params, "out"))};
    if (in.empty() || out_id.empty()) throw HostError(BRIDGE_INVALID_REQUEST, "in, out: the hashes of two assets");
    if (in == out_id) throw HostError(BRIDGE_INVALID_REQUEST, "The same asset in and out");
    if (!Param(params, "amount").isStr()) throw HostError(BRIDGE_INVALID_REQUEST, "amount: the amount as text");
    const auto [label_in, din]{AssetOn(stem, in)};
    const auto [label_out, dout]{AssetOn(stem, out_id)};
    const auto units{ParseUnits(util::TrimString(Str(params, "amount")), din)};
    if (!units || *units <= 0) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("The amount: a positive number with at most %d decimals", din));
    const std::string amount{FormatUnits(*units, din)};
    int max_hops{static_cast<int>(std::clamp<int64_t>(Int(Param(params, "maxHops"), 4), 1, 4))};

    // The routes: through Layers' pools (quoteroute) where the chain has routes and its node offers them, else the pool
    // of the two assets (quoteswap), as every DEX of these chains quotes.
    UniValue routes{UniValue::VARR};
    std::string via{"pair"};
    std::string pair_impact;
    bool routed{false};
    if (caps.dex_features.route && max_hops > 1) {
        try {
            const UniValue q{PageRead(stem, "quoteroute", Array({UniValue{"0x" + in}, UniValue{amount}, UniValue{"0x" + out_id}, UniValue{max_hops}, UniValue{3}}), "")};
            routed = true;
            via = "route";
            const UniValue& found{Param(q, "routes")};
            for (size_t i{0}; found.isArray() && i < found.size() && i < 5; ++i) {
                const UniValue& r{found[i]};
                const UniValue& ids{Param(r, "path_ids")};
                const UniValue& labels{Param(r, "path")};
                const UniValue& hops{Param(r, "hops")};
                if (!ids.isArray() || ids.size() < 2 || ids.size() > 5 || !hops.isArray() || hops.size() + 1 != ids.size()) continue;
                UniValue path{UniValue::VARR}, names{UniValue::VARR}, hop_rows{UniValue::VARR};
                bool ok{true};
                for (size_t k{0}; k < ids.size(); ++k) {
                    const std::string id{AssetIdOf(ids[k])};
                    ok = ok && !id.empty();
                    path.push_back(id);
                    names.push_back(labels.isArray() && k < labels.size() && labels[k].isStr() ? Line(labels[k].get_str(), 40) : std::string{});
                }
                for (size_t k{0}; ok && k < hops.size(); ++k) {
                    const std::string pool{Str(hops[k], "pool")};
                    const std::string hop_out{AmountOf(Param(hops[k], "amount_out"))};
                    ok = IsHex64(pool) && !hop_out.empty();
                    UniValue hop{UniValue::VOBJ};
                    hop.pushKV("pool", pool);
                    hop.pushKV("amountOut", hop_out);
                    hop_rows.push_back(std::move(hop));
                }
                const std::string route_out{AmountOf(Param(r, "amount_out"))};
                if (!ok || route_out.empty() || path[0].get_str() != in || path[path.size() - 1].get_str() != out_id) continue;
                UniValue route{UniValue::VOBJ};
                route.pushKV("path", std::move(path));
                route.pushKV("labels", std::move(names));
                route.pushKV("amountOut", route_out);
                route.pushKV("hops", std::move(hop_rows));
                routes.push_back(std::move(route));
            }
        } catch (const std::exception& e) {
            if (!NotOffered(e)) throw;
        }
    }
    if (!routed) {
        UniValue q;
        try {
            q = PageRead(stem, "quoteswap", Array({UniValue{"0x" + in}, UniValue{amount}, UniValue{"0x" + out_id}}), "");
        } catch (const HostError&) {
            throw;
        } catch (const std::exception& e) {
            // No pool of the two (the node says why): no route.
            q = UniValue{};
            const std::string why{e.what()};
            if (why.find("no pool") == std::string::npos && why.find("buys nothing") == std::string::npos) throw HostError(Line(why, 200));
        }
        const std::string q_out{AmountOf(Param(q, "amount_out"))};
        if (!q_out.empty()) {
            pair_impact = AmountOf(Param(q, "price_impact"));
            std::string pool;
            std::string la{label_in}, lb{label_out};
            const UniValue of_in{PoolsOf(stem, in)};
            for (const UniValue& row : of_in.getValues()) {
                if (row.find_value("b").find_value("id").get_str() != out_id) continue;
                pool = row.find_value("pool").get_str();
                la = row.find_value("a").find_value("label").get_str();
                lb = row.find_value("b").find_value("label").get_str();
            }
            if (IsHex64(pool)) {
                UniValue hop{UniValue::VOBJ};
                hop.pushKV("pool", pool);
                hop.pushKV("amountOut", q_out);
                UniValue route{UniValue::VOBJ};
                route.pushKV("path", Array({UniValue{in}, UniValue{out_id}}));
                route.pushKV("labels", Array({UniValue{la}, UniValue{lb}}));
                route.pushKV("amountOut", q_out);
                route.pushKV("hops", Array({hop}));
                routes.push_back(std::move(route));
            }
        }
    }

    // Each hop's pool (its kind and fee, its reserves): the route said in words, and the price impact (the route's price
    // against the pools' prices before it, fees included). Display only: the swap is quoted again when it is made.
    std::map<std::string, UniValue> pools_of;
    const auto pool_row{[&](const std::string& from, const std::string& pool) -> UniValue {
        if (!pools_of.contains(from)) pools_of[from] = pools_of.size() < 8 ? PoolsOf(stem, from) : UniValue{UniValue::VARR};
        for (const UniValue& row : pools_of[from].getValues()) {
            if (row.find_value("pool").get_str() == pool) return row;
        }
        return UniValue{};
    }};
    UniValue annotated{UniValue::VARR};
    for (const UniValue& route : routes.getValues()) {
        UniValue r{route};
        const UniValue& path{route.find_value("path")};
        UniValue hops{UniValue::VARR};
        double ideal{Real(amount)};
        bool known{true};
        int64_t fees{0};
        for (size_t k{0}; k < route.find_value("hops").size(); ++k) {
            UniValue hop{route.find_value("hops")[k]};
            const UniValue row{pool_row(path[k].get_str(), hop.find_value("pool").get_str())};
            hop.pushKV("from", path[k].get_str());
            hop.pushKV("to", path[k + 1].get_str());
            if (row.isObject()) {
                for (const char* key : {"curve", "fee", "amp", "weightA", "legacy"}) hop.pushKV(key, row.find_value(key));
                hop.pushKV("kind", KindWords(row));
                hop.pushKV("labelFrom", row.find_value("a").find_value("label"));
                hop.pushKV("labelTo", row.find_value("b").find_value("label"));
                const double ra{Real(row.find_value("reserveA").get_str())}, rb{Real(row.find_value("reserveB").get_str())};
                if (ra > 0 && rb > 0) ideal *= rb / ra;
                else known = false;
                fees += Int(row.find_value("fee"), LEGACY_FEE);
            } else {
                known = false;
            }
            hops.push_back(std::move(hop));
        }
        r.pushKV("hops", std::move(hops));
        const double got{Real(route.find_value("amountOut").get_str())};
        std::string impact;
        if (routes.size() == 1 && !routed && !pair_impact.empty()) impact = pair_impact;
        else if (known && ideal > 0) impact = strprintf("%.4f", std::max(0.0, (1.0 - got / ideal) * 100.0));
        r.pushKV("priceImpact", impact.empty() ? UniValue{} : UniValue{impact});
        r.pushKV("fees", fees);
        annotated.push_back(std::move(r));
    }

    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("in", in);
    out.pushKV("out", out_id);
    out.pushKV("amountIn", amount);
    out.pushKV("decimalsIn", din);
    out.pushKV("decimalsOut", dout);
    out.pushKV("via", via);
    out.pushKV("amountOut", annotated.empty() ? std::string{"0"} : annotated[0].find_value("amountOut").get_str());
    out.pushKV("routes", std::move(annotated));
    return out;
}

UniValue ChainAdapter::DexChart(const UniValue& params) const
{
    std::string stem;
    const Capabilities& caps{DexCaps(params, stem)};
    const std::string pool{Str(params, "pool")};
    if (!IsHex64(pool)) throw HostError(BRIDGE_INVALID_REQUEST, "pool: its id, 64 hex digits");
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("pool", pool);
    UniValue points{UniValue::VARR};
    if (!caps.dex_features.twap) {
        out.pushKV("points", std::move(points));
        out.pushKV("reason", "none");
        return out;
    }
    const auto twap{[&](int64_t seconds) { return PageRead(stem, "getpooltwap", Array({UniValue{pool}, UniValue{}, UniValue{seconds}}), ""); }};
    UniValue head;
    try {
        head = twap(1);
    } catch (const HostError&) {
        throw;
    } catch (const std::exception& e) {
        // No observation yet (a pool no block changed under the DEX's rules), or a node without it.
        out.pushKV("points", std::move(points));
        out.pushKV("reason", NotOffered(e) ? "none" : "young");
        out.pushKV("error", Line(e.what(), 200));
        return out;
    }
    const int64_t to{Int(Param(head, "to_time"))}, oldest{Int(Param(head, "oldest_time"))};
    // The pool's first asset as the chain keeps it (its prices are of it): its label (getpooltwap names it so).
    out.pushKV("labelA", Line(Str(head, "asset_a"), 40));
    out.pushKV("spot", AmountOf(Param(head, "spot_a")));
    out.pushKV("from", oldest);
    out.pushKV("to", to);
    // The average price of each of (at most) 24 equal parts of the window the pool keeps, from the averages over the
    // windows that end now: avg[t_k, t_{k-1}] = (twap_k * s_k - twap_{k-1} * s_{k-1}) / (s_k - s_{k-1}).
    const int64_t span{to - oldest};
    constexpr int64_t PARTS{24};
    if (span >= 2) {
        const int64_t parts{std::min<int64_t>(PARTS, span)};
        double later_sum{0};
        int64_t later_secs{0};
        std::vector<std::pair<int64_t, double>> found;
        for (int64_t k{1}; k <= parts; ++k) {
            const int64_t secs{k == parts ? span : span * k / parts};
            if (secs <= later_secs) continue;
            UniValue t;
            try {
                t = twap(secs);
            } catch (const HostError&) {
                throw;
            } catch (const std::exception& e) {
                out.pushKV("error", Line(e.what(), 200));
                break;
            }
            const double avg{Real(AmountOf(Param(t, "twap_a")))};
            const double sum{avg * static_cast<double>(secs)};
            const double part{(sum - later_sum) / static_cast<double>(secs - later_secs)};
            if (std::isfinite(part) && part > 0) found.emplace_back(to - (secs + later_secs) / 2, part);
            later_sum = sum;
            later_secs = secs;
        }
        std::reverse(found.begin(), found.end());
        for (const auto& [t, p] : found) {
            UniValue pt{UniValue::VOBJ};
            pt.pushKV("t", t);
            pt.pushKV("price", strprintf("%.10g", p));
            points.push_back(std::move(pt));
        }
    }
    out.pushKV("points", std::move(points));
    return out;
}

UniValue ChainAdapter::DexPositions(const UniValue& params) const
{
    std::string stem;
    DexCaps(params, stem);
    const std::string wallet{Wallet(stem, params)};
    const UniValue mine{PageRead(stem, "listmyassets", UniValue{UniValue::VARR}, wallet)};
    const UniValue& liquidity{Param(mine, "liquidity")};
    UniValue rows{UniValue::VARR};
    for (size_t i{0}; liquidity.isArray() && i < liquidity.size() && rows.size() < 200; ++i) {
        const UniValue& n{liquidity[i]};
        const std::string pool{Str(n, "pool")};
        const std::string a{AssetIdOf(Param(n, "asset_a_id"))}, b{AssetIdOf(Param(n, "asset_b_id"))};
        const std::string shares{AmountOf(Param(n, "shares"))};
        if (!IsHex64(pool) || a.empty() || b.empty() || shares.empty()) continue;
        const auto asset{[&](const std::string& id, const std::string& key) {
            UniValue o{UniValue::VOBJ};
            o.pushKV("id", id);
            o.pushKV("label", Line(Str(n, key), 40));
            return o;
        }};
        UniValue row{UniValue::VOBJ};
        row.pushKV("pool", pool);
        row.pushKV("a", asset(a, "asset_a"));
        row.pushKV("b", asset(b, "asset_b"));
        row.pushKV("shares", shares);
        row.pushKV("percent", AmountOf(Param(n, "percent")));
        row.pushKV("valueA", AmountOf(Param(n, "value_a")));
        row.pushKV("valueB", AmountOf(Param(n, "value_b")));
        // Constant-product shares: always in range, their fees in what they are worth (no collect).
        row.pushKV("kind", "shares");
        rows.push_back(std::move(row));
    }
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("wallet", wallet);
    out.pushKV("positions", std::move(rows));
    return out;
}

namespace {
//! The slippage of a request in basis points: 1 to 5000 (50 %), `otherwise` when not given.
int SlippageOf(const UniValue& params, int otherwise)
{
    const UniValue& v{Param(params, "slippage")};
    if (v.isNull()) return otherwise;
    const int64_t bps{Int(v, -1)};
    if (bps < 1 || bps > 5000) throw HostError(BRIDGE_INVALID_REQUEST, "slippage: in basis points, 1 to 5000");
    return static_cast<int>(bps);
}

std::string PercentOf(int bps)
{
    return strprintf("%d.%02d %%", bps / 100, bps % 100);
}
} // namespace

UniValue ChainAdapter::DexSwap(const UniValue& params) const
{
    std::string stem;
    const Capabilities& caps{DexCaps(params, stem)};
    const std::string wallet{Wallet(stem, params)};
    const std::string name{m_hooks.name(stem)};
    const std::string in{AssetIdOf(Param(params, "in"))}, out_id{AssetIdOf(Param(params, "out"))};
    if (in.empty() || out_id.empty()) throw HostError(BRIDGE_INVALID_REQUEST, "in, out: the hashes of two assets");
    if (in == out_id) throw HostError(BRIDGE_INVALID_REQUEST, "The same asset in and out");
    // The route the user saw: its assets by hash, the first what is paid, the last what comes, none twice.
    const UniValue& path_in{Param(params, "path")};
    if (!path_in.isArray() || path_in.size() < 2 || path_in.size() > (caps.dex_features.route ? 5u : 2u)) {
        throw HostError(BRIDGE_INVALID_REQUEST, caps.dex_features.route ? "path: 2 to 5 assets" : strprintf("path: two assets (%s swaps in one pool)", name));
    }
    std::vector<std::string> path;
    for (const UniValue& v : path_in.getValues()) {
        const std::string id{AssetIdOf(v)};
        if (id.empty()) throw HostError(BRIDGE_INVALID_REQUEST, "path: the hashes of its assets");
        path.push_back(id);
    }
    if (path.front() != in || path.back() != out_id) throw HostError(BRIDGE_INVALID_REQUEST, "path: from what is paid to what comes");
    if (std::set<std::string>(path.begin(), path.end()).size() != path.size()) throw HostError(BRIDGE_INVALID_REQUEST, "path: an asset twice");
    if (!Param(params, "amount").isStr() || !Param(params, "minOut").isStr()) throw HostError(BRIDGE_INVALID_REQUEST, "amount, minOut: as text");
    const int bps{SlippageOf(params, 50)};
    const auto [label_in, din]{AssetOn(stem, in)};
    const auto [label_out, dout]{AssetOn(stem, out_id)};
    const auto units{ParseUnits(util::TrimString(Str(params, "amount")), din)};
    if (!units || *units <= 0) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("The amount: a positive number with at most %d decimals", din));
    const auto min_out{ParseUnits(util::TrimString(Str(params, "minOut")), dout)};
    if (!min_out || *min_out <= 0) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("The least to receive: a positive number with at most %d decimals", dout));
    const std::string amount{FormatUnits(*units, din)};
    const int64_t held{HeldUnits(stem, wallet, in, din)};
    if (*units > held) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("More than the %s wallet %s holds (%s)", name, wallet, FormatUnits(held, din)));

    // Quoted again now, and the route checked: the very path must be one the chain's pools offer, and what it gives now,
    // less the slippage, at least what the user accepted. The chain's RPC checks it once more (min_out: never signed
    // for less), and its rules at the clearing (what misses comes back).
    UniValue q{UniValue::VOBJ};
    q.pushKV("stem", stem);
    q.pushKV("in", in);
    q.pushKV("out", out_id);
    q.pushKV("amount", amount);
    q.pushKV("maxHops", static_cast<int>(path.size() - 1));
    const UniValue quote{DexQuote(q)};
    const UniValue* route{nullptr};
    for (const UniValue& r : quote.find_value("routes").getValues()) {
        const UniValue& p{r.find_value("path")};
        if (p.size() != path.size()) continue;
        bool same{true};
        for (size_t k{0}; k < path.size(); ++k) same = same && p[k].get_str() == path[k];
        if (same) route = &r;
        if (same) break;
    }
    if (!route) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("%s's pools no longer offer this route. Quote again.", name));
    const auto fresh{ParseUnits(route->find_value("amountOut").get_str(), dout)};
    if (!fresh || *fresh <= 0) throw HostError(BRIDGE_INVALID_REQUEST, "The route gives nothing now. Quote again.");
    const int64_t least_now{LessSlippage(*fresh, bps)};
    const std::string unit_out{route->find_value("labels")[path.size() - 1].get_str()};
    const std::string unit_in{route->find_value("labels")[0].get_str()};
    if (least_now < *min_out) {
        throw HostError(BRIDGE_INVALID_REQUEST, strprintf("The price moved since your quote: the route now gives at least %s %s, less than the %s %s you accepted. Quote again.",
                                                          FormatUnits(least_now, dout), unit_out, FormatUnits(*min_out, dout), unit_out));
    }

    // What the app checked, said in the trusted dialog after the chain's name, before the chain's own rows.
    std::vector<std::string> labels, kinds;
    for (const UniValue& l : route->find_value("labels").getValues()) labels.push_back(l.get_str());
    UniValue rows{UniValue::VARR};
    const std::string product{m_hooks.product ? m_hooks.product() : std::string{"The app"}};
    rows.push_back(Row(strprintf("Checked by %s", product), "", "heading"));
    rows.push_back(Row("Route", strprintf("%s (%u pool%s)", Joined(labels, " → "), path.size() - 1, path.size() == 2 ? "" : "s")));
    const UniValue& hops{route->find_value("hops")};
    for (size_t k{0}; k < hops.size(); ++k) {
        const UniValue& h{hops[k]};
        const std::string kind{h.find_value("kind").isStr() ? h.find_value("kind").get_str() : std::string{"constant product · 0.30 %"}};
        rows.push_back(Row(hops.size() == 1 ? std::string{"Pool"} : strprintf("Pool %u", k + 1), strprintf("%s / %s · %s\n%s", labels[k], labels[k + 1], kind, h.find_value("pool").get_str())));
    }
    rows.push_back(Row("You pay", strprintf("%s %s", amount, unit_in)));
    rows.push_back(Row("Quoted again just now", strprintf("%s %s", FormatUnits(*fresh, dout), unit_out)));
    rows.push_back(Row("You receive at least", strprintf("%s %s (slippage %s)", FormatUnits(*min_out, dout), unit_out, PercentOf(bps))));
    if (route->find_value("priceImpact").isStr()) rows.push_back(Row("Price impact", strprintf("%.2f %%", Real(route->find_value("priceImpact").get_str()))));
    rows.push_back(Row("", strprintf("%s refuses the swap rather than give less than this; what does not fill comes back.", name), "note"));

    const UniValue result{CallWithRows(stem, SwapRequest(wallet, path, amount, bps, FormatUnits(*min_out, dout)), rows)};
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("wallet", wallet);
    if (IsHex64(Str(result, "txid"))) out.pushKV("txid", Str(result, "txid"));
    out.pushKV("quote", route->find_value("amountOut"));
    out.pushKV("minOut", FormatUnits(*min_out, dout));
    return out;
}

UniValue ChainAdapter::DexAddLiquidity(const UniValue& params) const
{
    std::string stem;
    DexCaps(params, stem);
    const std::string wallet{Wallet(stem, params)};
    const std::string name{m_hooks.name(stem)};
    const std::string a{AssetIdOf(Param(params, "a"))}, b{AssetIdOf(Param(params, "b"))};
    if (a.empty() || b.empty() || a == b) throw HostError(BRIDGE_INVALID_REQUEST, "a, b: the hashes of two different assets");
    if (!Param(params, "amountA").isStr()) throw HostError(BRIDGE_INVALID_REQUEST, "amountA: as text");
    const auto [la, da]{AssetOn(stem, a)};
    const auto [lb, db]{AssetOn(stem, b)};
    const auto ua{ParseUnits(util::TrimString(Str(params, "amountA")), da)};
    if (!ua || *ua <= 0) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("The first amount: a positive number with at most %d decimals", da));
    std::optional<int64_t> ub;
    if (!Param(params, "amountB").isNull()) {
        if (!Param(params, "amountB").isStr()) throw HostError(BRIDGE_INVALID_REQUEST, "amountB: as text");
        ub = ParseUnits(util::TrimString(Str(params, "amountB")), db);
        if (!ub || *ub <= 0) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("The second amount: a positive number with at most %d decimals", db));
    }
    if (*ua > HeldUnits(stem, wallet, a, da)) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("More of the first asset than the %s wallet %s holds", name, wallet));
    if (ub && *ub > HeldUnits(stem, wallet, b, db)) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("More of the second asset than the %s wallet %s holds", name, wallet));
    const int bps{SlippageOf(params, 100)};
    std::optional<bool> sets_price;
    if (Param(params, "setsPrice").isBool()) sets_price = Param(params, "setsPrice").get_bool();
    // The pool (its labels as the chain says them), when there is one.
    std::string pool_words;
    const UniValue of_a{PoolsOf(stem, a)};
    for (const UniValue& row : of_a.getValues()) {
        if (row.find_value("b").find_value("id").get_str() == b) pool_words = strprintf("%s / %s · %s", row.find_value("a").find_value("label").get_str(), row.find_value("b").find_value("label").get_str(), KindWords(row));
    }
    UniValue rows{UniValue::VARR};
    const std::string product{m_hooks.product ? m_hooks.product() : std::string{"The app"}};
    rows.push_back(Row(strprintf("Checked by %s", product), "", "heading"));
    rows.push_back(Row("Pool", pool_words.empty() ? std::string{"a new pool: constant product · 0.30 %"} : pool_words));
    rows.push_back(Row("You put in at most", strprintf("%s of the first asset%s", FormatUnits(*ua, da), ub ? strprintf(", %s of the second", FormatUnits(*ub, db)) : std::string{})));
    if (sets_price && *sets_price) rows.push_back(Row("", "These amounts set the pool's price.", "note"));
    const UniValue result{CallWithRows(stem, AddLiquidityRequest(wallet, a, FormatUnits(*ua, da), b, ub ? FormatUnits(*ub, db) : std::string{}, bps, sets_price), rows)};
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("wallet", wallet);
    if (IsHex64(Str(result, "txid"))) out.pushKV("txid", Str(result, "txid"));
    return out;
}

UniValue ChainAdapter::DexRemoveLiquidity(const UniValue& params) const
{
    std::string stem;
    DexCaps(params, stem);
    const std::string wallet{Wallet(stem, params)};
    const std::string name{m_hooks.name(stem)};
    const std::string pool{Str(params, "pool")};
    if (!IsHex64(pool)) throw HostError(BRIDGE_INVALID_REQUEST, "pool: its id, 64 hex digits");
    // A percentage of the shares held: above 0, at most 100, two decimals at most (exactly as typed).
    const std::string percent{util::TrimString(Str(params, "percent"))};
    const auto hundredths{ParseUnits(percent, 2)};
    if (!hundredths || *hundredths <= 0 || *hundredths > 10000) throw HostError(BRIDGE_INVALID_REQUEST, "The share to take out: a percentage above 0, at most 100, two decimals at most");
    const int bps{SlippageOf(params, 100)};
    // Only shares the wallet holds: the pool and its assets from the wallet's own list.
    UniValue held;
    const UniValue positions{DexPositions(params)};
    for (const UniValue& p : positions.find_value("positions").getValues()) {
        if (p.find_value("pool").get_str() == pool) held = p;
    }
    if (!held.isObject()) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("The %s wallet %s holds no shares of this pool", name, wallet));
    const std::string a{held.find_value("a").find_value("id").get_str()}, b{held.find_value("b").find_value("id").get_str()};
    const std::string la{held.find_value("a").find_value("label").get_str()}, lb{held.find_value("b").find_value("label").get_str()};
    const std::string shown_percent{FormatUnits(*hundredths, 2)};
    UniValue rows{UniValue::VARR};
    const std::string product{m_hooks.product ? m_hooks.product() : std::string{"The app"}};
    rows.push_back(Row(strprintf("Checked by %s", product), "", "heading"));
    rows.push_back(Row("Pool", strprintf("%s / %s\n%s", la, lb, pool)));
    rows.push_back(Row("Shares given back", strprintf("%s %% of the %s you hold", shown_percent, held.find_value("shares").get_str())));
    const auto part{[&](const std::string& value, const std::string& label) {
        const size_t dot{value.find('.')};
        const int decimals{dot == std::string::npos ? 0 : static_cast<int>(value.size() - dot - 1)};
        const auto units{ParseUnits(value, decimals)};
        if (!units) return std::string{};
        return strprintf("%s %s", FormatUnits(static_cast<int64_t>(static_cast<__int128>(*units) * *hundredths / 10000), decimals), label);
    }};
    const std::string about_a{part(held.find_value("valueA").get_str(), la)}, about_b{part(held.find_value("valueB").get_str(), lb)};
    if (!about_a.empty() && !about_b.empty()) rows.push_back(Row("You get about", about_a + " and " + about_b));
    const UniValue result{CallWithRows(stem, RemoveLiquidityRequest(wallet, a, b, shown_percent, bps), rows)};
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("wallet", wallet);
    if (IsHex64(Str(result, "txid"))) out.pushKV("txid", Str(result, "txid"));
    return out;
}

UniValue ChainAdapter::DexCreatePool(const UniValue& params) const
{
    std::string stem;
    DexCaps(params, stem);
    const std::string wallet{Wallet(stem, params)};
    const std::string name{m_hooks.name(stem)};
    const std::string a{AssetIdOf(Param(params, "a"))}, b{AssetIdOf(Param(params, "b"))};
    if (a.empty() || b.empty() || a == b) throw HostError(BRIDGE_INVALID_REQUEST, "a, b: the hashes of two different assets");
    const std::string curve{Str(params, "curve").empty() ? std::string{"constant-product"} : Str(params, "curve")};
    const int64_t fee{Param(params, "fee").isNull() ? LEGACY_FEE : Int(Param(params, "fee"), -1)};
    const int64_t amp{Int(Param(params, "amp"))}, weight{Int(Param(params, "weightA"))};
    if (const std::string why{WhyNotPoolKind(curve, fee, amp, weight)}; !why.empty()) throw HostError(BRIDGE_INVALID_REQUEST, why);
    const bool legacy{curve == "constant-product" && fee == LEGACY_FEE};
    if (!legacy) {
        UniValue p{UniValue::VOBJ};
        p.pushKV("stem", stem);
        if (!DexInfo(p).find_value("curvesActive").isTrue()) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("%s makes constant-product pools at 0.30 %% only, for now", name));
    }
    if (!Param(params, "amountA").isStr() || !Param(params, "amountB").isStr()) throw HostError(BRIDGE_INVALID_REQUEST, "amountA, amountB: both amounts, as text (they set the price)");
    const auto [la, da]{AssetOn(stem, a)};
    const auto [lb, db]{AssetOn(stem, b)};
    const auto ua{ParseUnits(util::TrimString(Str(params, "amountA")), da)}, ub{ParseUnits(util::TrimString(Str(params, "amountB")), db)};
    if (!ua || *ua <= 0 || !ub || *ub <= 0) throw HostError(BRIDGE_INVALID_REQUEST, "Both amounts: positive numbers with the decimals of each asset");
    if (*ua > HeldUnits(stem, wallet, a, da)) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("More of the first asset than the %s wallet %s holds", name, wallet));
    if (*ub > HeldUnits(stem, wallet, b, db)) throw HostError(BRIDGE_INVALID_REQUEST, strprintf("More of the second asset than the %s wallet %s holds", name, wallet));
    const int bps{SlippageOf(params, 100)};
    UniValue kind{UniValue::VOBJ};
    kind.pushKV("curve", curve);
    kind.pushKV("fee", fee);
    kind.pushKV("amp", amp);
    kind.pushKV("weightA", weight);
    UniValue rows{UniValue::VARR};
    const std::string product{m_hooks.product ? m_hooks.product() : std::string{"The app"}};
    rows.push_back(Row(strprintf("Checked by %s", product), "", "heading"));
    rows.push_back(Row("New pool", KindWords(kind)));
    rows.push_back(Row("You put in", strprintf("%s of the first asset and %s of the second", FormatUnits(*ua, da), FormatUnits(*ub, db))));
    rows.push_back(Row("", "These amounts set the pool's first price.", "note"));
    const UniValue result{CallWithRows(stem, CreatePoolRequest(wallet, a, FormatUnits(*ua, da), b, FormatUnits(*ub, db), curve, fee, amp, weight, bps), rows)};
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    out.pushKV("wallet", wallet);
    if (IsHex64(Str(result, "txid"))) out.pushKV("txid", Str(result, "txid"));
    return out;
}

} // namespace webui
