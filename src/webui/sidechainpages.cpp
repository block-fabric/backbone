// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <webui/sidechainpages.h>

#include <logging.h>
#include <support/cleanse.h>
#include <tinyformat.h>
#include <util/strencodings.h>
#include <util/threadnames.h>
#include <webui/assets.h>
#include <webui/guard.h>
#include <webui/supervisor.h>

#include <algorithm>
#include <set>
#include <string_view>

namespace webui {
namespace {
using namespace std::chrono_literals;

//! The error of a token the daemon refused (remotehost.h CONFIRMATION_REFUSED), and of a wrong passphrase.
constexpr int CONFIRMATION_REFUSED{-32802};
constexpr int PASSPHRASE_INCORRECT{-14};
//! How long a file chosen for the pages may be named by them.
constexpr auto CHOSEN_FILE_TIME{10min};
//! How long the app waits for a request of the pages at a daemon (a rescan of a wallet takes long), and for its events.
constexpr auto REQUEST_TIMEOUT{600s};
constexpr int64_t EVENTS_WAIT_MS{1500};

const UniValue& Param(const UniValue& params, const std::string& key)
{
    return params.isObject() ? params.find_value(key) : NullUniValue;
}

std::string Str(const UniValue& o, const std::string& key)
{
    const UniValue& v{Param(o, key)};
    return v.isStr() ? v.get_str() : std::string{};
}

dialog::Row::Kind RowKind(const std::string& name)
{
    using K = dialog::Row::Kind;
    for (const K k : {K::TEXT, K::ADDRESS, K::AMOUNT, K::TOTAL, K::HEADING, K::NOTE, K::IMAGE}) {
        if (dialog::RowKindName(k) == name) return k;
    }
    return K::TEXT;
}

dialog::Level LevelOf(const std::string& name)
{
    if (name == "info") return dialog::Level::INFO;
    if (name == "danger") return dialog::Level::DANGER;
    return dialog::Level::WARNING;
}

void Strings(const UniValue& value, std::set<std::string>& out)
{
    if (value.isStr()) {
        out.insert(value.get_str());
    } else if (value.isArray() || value.isObject()) {
        for (const UniValue& v : value.getValues()) Strings(v, out);
    }
}

std::string_view AssetText(const Asset& asset)
{
    return {reinterpret_cast<const char*>(asset.data.data()), asset.data.size()};
}
} // namespace

SidechainPages::Route SidechainPages::RouteOf(const std::string& method)
{
    // What only a whole app does: never from the pages of a sidechain in this one.
    static const std::set<std::string> refused{"app.quit", "app.restart", "app.updateLanguage", "node.paths", "node.setNetworkActive", "options.get", "options.set",
                                               "options.apply", "options.cancel", "options.reset", "options.chooseFile", "options.chooseProgram", "options.validProxy"};
    static const std::vector<std::string_view> refused_prefixes{"intro.", "test.", "sidechainNodes.", "sidechains.", "pack."};
    // What the app answers for the frame (it has no window, no settings, no files of its own).
    static const std::set<std::string> local{"app.hello", "app.info", "app.locale", "prefs.get", "prefs.set", "file.choose", "peg.deposit", "node.getSetting"};
    // What the app itself does, as for its own pages: the clipboard, links, notices, the files chosen in its chooser.
    static const std::set<std::string> hub{"clipboard.read", "clipboard.write", "app.open", "app.notify", "dialog.prepare", "file.read", "file.write",
                                           "app.languages", "app.quitGuard", "app.help", "app.about", "tray.state"};
    if (refused.contains(method)) return Route::REFUSE;
    for (const auto& p : refused_prefixes) {
        if (method.starts_with(p)) return Route::REFUSE;
    }
    if (local.contains(method) || method.starts_with("window.")) return Route::LOCAL;
    if (hub.contains(method)) return Route::HUB;
    return Route::DAEMON;
}

bool SidechainPages::IsLookPref(const std::string& key)
{
    static const std::set<std::string> look{"theme", "customThemes", "fontAdjust", "fontFamily", "moneyFont", "uiMode", "reduceMotion", "language"};
    return look.contains(key);
}

SidechainPages::SidechainPages(Hooks hooks) : m_hooks(std::move(hooks)) {}

SidechainPages::~SidechainPages()
{
    Stop();
}

void SidechainPages::Stop()
{
    m_stop = true;
    std::map<std::string, std::thread> pumps;
    {
        LOCK(m_mutex);
        pumps.swap(m_pumps);
    }
    for (auto& [stem, thread] : pumps) {
        if (thread.joinable()) thread.join();
    }
}

UniValue SidechainPages::Packs()
{
    const Asset* asset{FindAsset(BuiltinAssets(), "/packs/index.json")};
    UniValue list;
    if (!asset || !list.read(AssetText(*asset)) || !list.isArray()) return UniValue{UniValue::VARR};
    return list;
}

std::string SidechainPages::Logo(const std::string& stem)
{
    if (stem.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789") != std::string::npos) return {};
    const Asset* asset{FindAsset(BuiltinAssets(), "/packs/" + stem + "/icon-256.png")};
    if (!asset) return {};
    std::string url{"data:image/png;base64," + EncodeBase64(asset->data)};
    return dialog::IsImageDataUrl(url) ? url : std::string{};
}

Window::ConfirmDialog SidechainPages::Question(const UniValue& d, const std::string& chain, const std::string& network, const std::string& logo)
{
    Window::ConfirmDialog dialog;
    dialog.title = Str(d, "title");
    dialog.message = Str(d, "message");
    dialog.accept = Str(d, "accept");
    dialog.cancel = Str(d, "cancel");
    dialog.details = Str(d, "details");
    dialog.level = LevelOf(Str(d, "level"));
    dialog.passphrase = Param(d, "passphrase").isTrue();
    dialog.passphrase_label = Str(d, "passphraseLabel");
    dialog.error = Str(d, "error");
    // The chain, in the app's words, first: where it happens, before what (the dialog shows it above the title too).
    dialog.rows.push_back({"Chain", chain, dialog::Row::Kind::TEXT});
    if (const UniValue& rows{Param(d, "rows")}; rows.isArray()) {
        for (const UniValue& r : rows.getValues()) {
            if (!r.isObject()) continue;
            dialog.rows.push_back({Str(r, "label"), Str(r, "value"), RowKind(Str(r, "kind"))});
        }
    }
    dialog.kind = strprintf("pack:%s:%s", chain, Str(d, "kind").empty() ? dialog.title : Str(d, "kind"));
    dialog.chain = chain;
    dialog.chain_network = network;
    dialog.chain_logo = logo;
    return dialog;
}

UniValue SidechainPages::Call(const std::string& stem, const UniValue& request)
{
    if (!request.isObject()) throw HostError(BRIDGE_INVALID_REQUEST, "request: {kind, method, params, wallet?}");
    const std::string kind{Str(request, "kind")};
    const std::string method{Str(request, "method")};
    if (method.empty()) throw HostError(BRIDGE_INVALID_REQUEST, "request.method: a name");
    if (kind != "rpc" && kind != "host") throw HostError(BRIDGE_INVALID_REQUEST, "request.kind: rpc or host");
    // Only what a request of a page is: the daemon parses it again, and must read what was shown (no key given twice).
    UniValue clean{UniValue::VOBJ};
    clean.pushKV("kind", kind);
    clean.pushKV("method", method);
    const UniValue& params{Param(request, "params")};
    if (!params.isNull() && !params.isArray() && !params.isObject()) throw HostError(BRIDGE_INVALID_REQUEST, "request.params: an array or an object");
    clean.pushKV("params", params.isNull() ? UniValue{kind == "rpc" ? UniValue::VARR : UniValue::VOBJ} : params);
    if (Param(request, "wallet").isStr()) clean.pushKV("wallet", Param(request, "wallet").get_str());
    Follow(stem);
    if (kind == "rpc") return Daemon(stem, clean);
    switch (RouteOf(method)) {
    case Route::REFUSE:
        throw HostError(BRIDGE_METHOD_NOT_FOUND, strprintf("%s is not offered to the pages of a sidechain in the Chains app", method));
    case Route::LOCAL:
        return Local(stem, method, clean.find_value("params"));
    case Route::HUB:
        return m_hooks.hub(method, clean.find_value("params"));
    case Route::DAEMON:
        return Daemon(stem, clean);
    }
    return UniValue{};
}

UniValue SidechainPages::FileTokens(const std::string& stem, const UniValue& request)
{
    std::set<std::string> named;
    Strings(request.find_value("params"), named);
    std::vector<std::pair<std::string, bool>> files;
    {
        LOCK(m_mutex);
        auto& chosen{m_chosen[stem]};
        const auto now{std::chrono::steady_clock::now()};
        std::erase_if(chosen, [&](const auto& e) { return now - e.second.second > CHOSEN_FILE_TIME; });
        for (const auto& [path, how] : chosen) {
            if (named.contains(path)) files.emplace_back(path, how.first);
        }
    }
    UniValue tokens{UniValue::VARR};
    for (const auto& [path, write] : files) {
        UniValue args{UniValue::VARR};
        args.push_back(path);
        args.push_back(write);
        try {
            tokens.push_back(m_hooks.rpc(stem, "webuichosenfile", args, 30s));
        } catch (const std::runtime_error& e) {
            throw HostError(strprintf("The file cannot be given to the %s node: %s", m_hooks.name(stem), e.what()));
        }
    }
    return tokens;
}

UniValue SidechainPages::Daemon(const std::string& stem, const UniValue& request)
{
    if (!m_hooks.running(stem)) throw HostError(BRIDGE_NOT_READY, strprintf("The %s node is not running", m_hooks.name(stem)));
    const auto call{[&](const std::string& token, std::string passphrase) {
        UniValue args{UniValue::VARR};
        args.push_back(request);
        args.push_back(token);
        args.push_back(passphrase);
        memory_cleanse(passphrase.data(), passphrase.size());
        args.push_back(FileTokens(stem, request));
        try {
            UniValue out{m_hooks.rpc(stem, "webuihost", args, REQUEST_TIMEOUT)};
            if (args[2].isStr() && !args[2].get_str().empty()) {
                std::string& p{const_cast<std::string&>(args[2].get_str())};
                memory_cleanse(p.data(), p.size());
            }
            return out;
        } catch (const SidechainSupervisor::RpcError& e) {
            if (args[2].isStr() && !args[2].get_str().empty()) {
                std::string& p{const_cast<std::string&>(args[2].get_str())};
                memory_cleanse(p.data(), p.size());
            }
            throw HostError(e.code, e.what());
        } catch (const std::runtime_error& e) {
            throw HostError(e.what());
        }
    }};
    const std::string name{m_hooks.name(stem)};
    const std::string network{m_hooks.network ? m_hooks.network() : std::string{}};
    std::string error;
    for (int attempt{0}; attempt < MAX_TRIES; ++attempt) {
        // Without a token: it runs if it needs no question; else the question of the chain's describers comes back.
        UniValue answer{call("", "")};
        if (!answer.find_value("confirm").isObject()) return answer.find_value("result");
        const UniValue& description{answer.find_value("confirm")};
        const std::string token{Str(description, "token")};
        if (token.size() != 64 || !IsHex(token)) throw HostError(strprintf("The %s node asked a question without its token", name));
        Window::ConfirmDialog dialog{Question(description, name, network, Logo(stem))};
        if (!error.empty()) dialog.error = error;
        dialog::Answer said{m_hooks.ask(dialog)};
        if (!said.accepted) throw HostError(BRIDGE_CANCELLED, strprintf("%s: declined.", dialog.title));
        std::string passphrase{said.passphrase.begin(), said.passphrase.end()};
        try {
            // Exactly the request that was described, with its token: the daemon runs it only if it is that one.
            UniValue done{call(token, std::move(passphrase))};
            if (done.find_value("confirm").isObject()) throw HostError(BRIDGE_INVALID_REQUEST, "The node asked a second question for one request: nothing more was run");
            LogInfo("webui: %s ran on %s as confirmed", guard::OneLine(dialog.title, 80), name);
            return done.find_value("result");
        } catch (const HostError& e) {
            // A wrong passphrase: asked again (the question made again by the daemon), a few times.
            if (e.code == PASSPHRASE_INCORRECT && dialog.passphrase) {
                error = e.what();
                continue;
            }
            if (e.code == CONFIRMATION_REFUSED) LogWarning("webui: %s refused a confirmation: %s", name, e.what());
            throw;
        }
    }
    throw HostError(PASSPHRASE_INCORRECT, error.empty() ? "The passphrase entered for the wallet decryption was incorrect." : error);
}

UniValue SidechainPages::Overlay(UniValue info) const
{
    if (!info.isObject()) info = UniValue{UniValue::VOBJ};
    UniValue out{UniValue::VOBJ};
    for (size_t i{0}; i < info.size(); ++i) {
        const std::string& key{info.getKeys()[i]};
        if (key == "language" || key == "embedded" || key == "datadir" || key == "devtools" || key == "test") continue;
        out.pushKV(key, info.getValues()[i]);
    }
    out.pushKV("language", m_hooks.language ? m_hooks.language() : std::string{});
    out.pushKV("datadir", "");
    out.pushKV("devtools", false);
    out.pushKV("test", false);
    // In a frame of the Chains app: no title bar, no window of its own.
    out.pushKV("embedded", true);
    return out;
}

UniValue SidechainPages::Local(const std::string& stem, const std::string& method, const UniValue& params)
{
    const bool running{m_hooks.running(stem)};
    if (method == "app.hello" || method == "app.info") {
        UniValue hello{UniValue::VOBJ};
        if (running) {
            UniValue request{UniValue::VOBJ};
            request.pushKV("kind", "host");
            request.pushKV("method", "app.hello");
            request.pushKV("params", UniValue{UniValue::VOBJ});
            try {
                hello = Daemon(stem, request);
            } catch (const HostError&) {
                hello = UniValue{UniValue::VOBJ};
            }
        }
        UniValue info{Overlay(hello.find_value("info"))};
        if (!info.exists("name")) info.pushKV("name", m_hooks.name(stem));
        if (method == "app.info") return info;
        UniValue out{UniValue::VOBJ};
        // Until its node answers, the pages wait as an app whose node starts.
        out.pushKV("phase", running && hello.isObject() && hello.find_value("phase").isStr() ? hello.find_value("phase").get_str() : std::string{"init"});
        out.pushKV("phaseData", UniValue{UniValue::VOBJ});
        out.pushKV("dialogs", UniValue{UniValue::VARR});
        out.pushKV("info", std::move(info));
        return out;
    }
    if (method == "app.locale") {
        // The words of the pack, in the language of the app.
        const std::string language{m_hooks.language ? m_hooks.language() : std::string{}};
        UniValue out{UniValue::VOBJ};
        out.pushKV("code", language);
        UniValue catalog;
        const Asset* asset{language.empty() || language.find_first_of("/.\\") != std::string::npos ? nullptr : FindAsset(BuiltinAssets(), "/packs/" + stem + "/locales/" + language + ".json")};
        if (asset && catalog.read(AssetText(*asset)) && catalog.isObject()) {
            out.pushKV("name", catalog.find_value("name"));
            out.pushKV("rtl", catalog.find_value("rtl"));
            out.pushKV("messages", catalog.find_value("gui"));
        } else {
            out.pushKV("name", "English");
            out.pushKV("rtl", false);
            out.pushKV("messages", UniValue{UniValue::VOBJ});
        }
        return out;
    }
    if (method == "prefs.get" || method == "prefs.set") {
        UniValue own{m_hooks.pack_prefs(stem)};
        if (!own.isObject()) own = UniValue{UniValue::VOBJ};
        if (method == "prefs.set") {
            if (!params.isObject()) throw HostError(BRIDGE_INVALID_REQUEST, "prefs.set takes an object of the preferences to change");
            // The look is the app's: a pack does not change it.
            UniValue next{UniValue::VOBJ};
            for (size_t i{0}; i < own.size(); ++i) {
                if (!params.exists(own.getKeys()[i])) next.pushKV(own.getKeys()[i], own.getValues()[i]);
            }
            for (size_t i{0}; i < params.size(); ++i) {
                if (!IsLookPref(params.getKeys()[i])) next.pushKV(params.getKeys()[i], params.getValues()[i]);
            }
            if (next.write().size() > 256 * 1024) throw HostError(BRIDGE_INVALID_REQUEST, "Too many preferences");
            m_hooks.set_pack_prefs(stem, next);
            own = next;
        }
        UniValue prefs{m_hooks.hub("prefs.get", UniValue{UniValue::VOBJ})};
        UniValue out{UniValue::VOBJ};
        for (size_t i{0}; i < prefs.size(); ++i) {
            const std::string& key{prefs.getKeys()[i]};
            if (IsLookPref(key) || !own.exists(key)) out.pushKV(key, prefs.getValues()[i]);
        }
        for (size_t i{0}; i < own.size(); ++i) {
            if (!IsLookPref(own.getKeys()[i])) out.pushKV(own.getKeys()[i], own.getValues()[i]);
        }
        return out;
    }
    if (method == "window.state") {
        // A frame: the app's window has the title bar.
        UniValue out{UniValue::VOBJ};
        out.pushKV("frameless", false);
        out.pushKV("maximized", false);
        out.pushKV("titleBar", "system");
        out.pushKV("supported", false);
        out.pushKV("restartNeeded", false);
        return out;
    }
    if (method.starts_with("window.")) return UniValue{true};
    if (method == "node.getSetting") {
        UniValue out{UniValue::VOBJ};
        out.pushKV("value", UniValue{});
        out.pushKV("ignored", false);
        return out;
    }
    if (method == "file.choose") {
        // The app's own chooser; the file is the pages' to name (to the node of the sidechain) for a while.
        const UniValue chosen{m_hooks.hub("file.choose", params)};
        if (chosen.isStr()) {
            const bool write{Param(params, "save").isTrue()};
            LOCK(m_mutex);
            m_chosen[stem][chosen.get_str()] = {write, std::chrono::steady_clock::now()};
        }
        return chosen;
    }
    if (method == "peg.deposit") return m_hooks.deposit(stem, params);
    throw HostError(BRIDGE_METHOD_NOT_FOUND, strprintf("No host method %s", method));
}

UniValue SidechainPages::Info(const std::string& stem)
{
    UniValue out{UniValue::VOBJ};
    out.pushKV("stem", stem);
    UniValue pack;
    const UniValue packs{Packs()};
    for (const UniValue& p : packs.getValues()) {
        if (Str(p, "stem") == stem) pack = p;
    }
    out.pushKV("pack", pack);
    const bool running{m_hooks.running(stem)};
    out.pushKV("running", running);
    if (pack.isNull()) {
        out.pushKV("compatible", false);
        out.pushKV("reason", "nopack");
        return out;
    }
    if (!Param(pack, "hostApi").isNum() || Param(pack, "hostApi").getInt<int>() != HOST_API) {
        out.pushKV("compatible", false);
        out.pushKV("reason", "pack");
        return out;
    }
    if (!running) {
        out.pushKV("compatible", true);
        out.pushKV("reason", "starting");
        return out;
    }
    // What the daemon speaks: an older one (no host over RPC, or another version) shows read-only, "update needed".
    UniValue request{UniValue::VOBJ};
    request.pushKV("kind", "host");
    request.pushKV("method", "app.info");
    request.pushKV("params", UniValue{UniValue::VOBJ});
    UniValue args{UniValue::VARR};
    args.push_back(request);
    int host_api{0};
    std::string version;
    try {
        const UniValue info{m_hooks.rpc(stem, "webuihost", args, 30s).find_value("result")};
        if (Param(info, "hostApi").isNum()) host_api = Param(info, "hostApi").getInt<int>();
        version = Str(info, "version");
    } catch (const std::runtime_error&) {
        // A daemon without webuihost: not a method of its whitelist, or unknown.
    }
    out.pushKV("hostApi", host_api);
    out.pushKV("version", version);
    out.pushKV("compatible", host_api == HOST_API);
    out.pushKV("reason", host_api == HOST_API ? "" : "daemon");
    return out;
}

void SidechainPages::Follow(const std::string& stem)
{
    LOCK(m_mutex);
    if (m_stop || m_pumps.contains(stem)) return;
    m_pumps.emplace(stem, std::thread{[this, stem] {
        util::ThreadRename("webui-pack-" + stem);
        uint64_t since{0};
        bool first{true};
        while (!m_stop) {
            if (!m_hooks.running(stem)) {
                for (int i{0}; i < 5 && !m_stop; ++i) std::this_thread::sleep_for(100ms);
                continue;
            }
            UniValue args{UniValue::VARR};
            args.push_back(since);
            args.push_back(EVENTS_WAIT_MS);
            try {
                const UniValue got{m_hooks.rpc(stem, "webuievents", args, std::chrono::milliseconds{EVENTS_WAIT_MS + 5000})};
                const bool reset{got.find_value("reset").isTrue()};
                // The first answer gives what the daemon kept: the frame asks for the state itself (app.hello, node.state).
                if (!first || reset) {
                    if (reset && !first) m_hooks.events(stem, "", true);
                    for (const UniValue& batch : got.find_value("batches").getValues()) {
                        if (batch.isStr() && !first) m_hooks.events(stem, batch.get_str(), false);
                    }
                }
                if (got.find_value("seq").isNum()) since = got.find_value("seq").getInt<uint64_t>();
                first = false;
            } catch (const std::exception&) {
                // Restarting, or stopping: tried again in a moment, from the start (a new daemon numbers anew).
                since = 0;
                first = true;
                for (int i{0}; i < 10 && !m_stop; ++i) std::this_thread::sleep_for(100ms);
            }
        }
    }});
}

} // namespace webui
