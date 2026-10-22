// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <webui/seedkeeper.h>

#include <common/secretfile.h>
#include <logging.h>
#include <random.h>
#include <rpc/protocol.h>
#include <support/cleanse.h>
#include <tinyformat.h>
#include <util/time.h>
#include <webui/bridge.h>
#include <webui/supervisor.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <tuple>

using namespace std::chrono_literals;

namespace webui {
namespace {

const UniValue& Param(const UniValue& params, const std::string& key)
{
    return params.isObject() ? params.find_value(key) : NullUniValue;
}

//! Every string of a value wiped (a request that carried keys or a passphrase, once sent).
void WipeValue(UniValue& value)
{
    if (value.isStr()) {
        std::string& text{const_cast<std::string&>(value.get_str())};
        memory_cleanse(text.data(), text.size());
        return;
    }
    if (value.isArray() || value.isObject()) {
        for (size_t i{0}; i < value.size(); ++i) WipeValue(const_cast<UniValue&>(value[i]));
    }
}

dialog::Field MakeField(std::string id, dialog::Field::Kind kind, std::string label, std::string hint, bool optional = false)
{
    dialog::Field f;
    f.id = std::move(id);
    f.kind = kind;
    f.label = std::move(label);
    f.hint = std::move(hint);
    f.optional = optional;
    return f;
}

std::string Str(const SecureString& s)
{
    return std::string{s.begin(), s.end()};
}

SecureString FieldOf(const dialog::Answer& answer, const std::string& id)
{
    const auto it{answer.fields.find(id)};
    return it == answer.fields.end() ? SecureString{} : it->second;
}

/** "YYYY-MM-DD" (a day of the calendar, UTC) as the time of its start, or nothing. */
std::optional<int64_t> DayOf(std::string_view text)
{
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return std::nullopt;
    int y{0}, m{0}, d{0};
    for (const auto& [from, to, out] : {std::tuple{0, 4, &y}, {5, 7, &m}, {8, 10, &d}}) {
        for (int i{from}; i < to; ++i) {
            if (text[i] < '0' || text[i] > '9') return std::nullopt;
            *out = *out * 10 + (text[i] - '0');
        }
    }
    if (y < 2009 || m < 1 || m > 12 || d < 1 || d > 31) return std::nullopt;
    // Days from the civil date (H. Hinnant's algorithm).
    const int yy{m <= 2 ? y - 1 : y};
    const int era{yy / 400};
    const int yoe{yy - era * 400};
    const int doy{(153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1};
    const int doe{yoe * 365 + yoe / 4 - yoe / 100 + doy};
    const int64_t days{int64_t{era} * 146097 + doe - 719468};
    return days * 86400;
}

int64_t Now()
{
    return GetTime<std::chrono::seconds>().count();
}

std::vector<std::string> Names(const UniValue& listwalletdir)
{
    std::vector<std::string> out;
    for (const UniValue& w : listwalletdir.find_value("wallets").getValues()) {
        if (w.find_value("name").isStr()) out.push_back(w.find_value("name").get_str());
    }
    return out;
}

bool Has(const std::vector<std::string>& names, const std::string& name)
{
    return std::ranges::find(names, name) != names.end();
}

UniValue Cancelled()
{
    UniValue out{UniValue::VOBJ};
    out.pushKV("cancelled", true);
    return out;
}

} // namespace

SeedKeeper::SeedKeeper(Hooks hooks) : m_hooks(std::move(hooks))
{
    m_expiry = std::thread([this] { ExpireLoop(); });
}

SeedKeeper::~SeedKeeper()
{
    Stop();
}

void SeedKeeper::Stop()
{
    std::vector<std::thread> threads;
    std::vector<std::string> scanning;
    {
        LOCK(m_mutex);
        if (m_stopping && !m_expiry.joinable()) return;
        m_stopping = true;
        Wipe();
        for (auto& [id, job] : m_jobs) {
            if (!job->done && id == APP_CHAIN && !job->wallet.empty()) scanning.push_back(job->wallet);
            if (job->thread.joinable()) threads.push_back(std::move(job->thread));
        }
        for (auto& t : m_finished) threads.push_back(std::move(t));
        m_finished.clear();
    }
    m_cv.notify_all();
    // A rescan of the app's own node is stopped (the sidechains' nodes are stopped by the supervisor before this).
    for (const std::string& wallet : scanning) {
        try {
            m_hooks.node_rpc("abortrescan", UniValue{UniValue::VARR}, wallet);
        } catch (const std::exception&) {
        }
    }
    for (auto& t : threads) t.join();
    if (m_expiry.joinable()) m_expiry.join();
}

void SeedKeeper::ExpireLoop()
{
    WAIT_LOCK(m_mutex, lock);
    while (!m_stopping) {
        if (m_session && std::chrono::steady_clock::now() >= m_session->until) {
            Wipe();
            REVERSE_LOCK(lock, m_mutex);
            Changed();
            continue;
        }
        if (m_session) {
            m_cv.wait_until(lock, m_session->until);
        } else {
            m_cv.wait(lock);
        }
    }
}

void SeedKeeper::Wipe()
{
    // The session's secret and passphrase are in secure memory: wiped as they go.
    m_session.reset();
}

void SeedKeeper::Changed()
{
    if (m_hooks.changed) m_hooks.changed();
}

bool SeedKeeper::Exists() const
{
    return fs::exists(m_hooks.file);
}

std::optional<std::string> SeedKeeper::ReadFile(std::string& error) const
{
    std::ifstream file{static_cast<const std::filesystem::path&>(m_hooks.file)};
    if (!file) {
        error = "There is no backup phrase on this computer yet.";
        return std::nullopt;
    }
    std::stringstream text;
    text << file.rdbuf();
    if (text.str().size() > 64 * 1024) {
        error = "The file of the backup phrase is damaged.";
        return std::nullopt;
    }
    return text.str();
}

std::optional<seed::Header> SeedKeeper::FileHeader() const
{
    std::string error;
    const auto text{ReadFile(error)};
    if (!text) return std::nullopt;
    return seed::ReadHeader(*text, error);
}

UniValue SeedKeeper::Records() const
{
    UniValue r{m_hooks.load ? m_hooks.load() : UniValue{}};
    return r.isObject() ? r : UniValue{UniValue::VOBJ};
}

std::optional<SeedKeeper::Record> SeedKeeper::RecordOf(const UniValue& records, const std::string& id) const
{
    const UniValue& chains{records.find_value("chains")};
    if (!chains.isObject()) return std::nullopt;
    const UniValue& r{chains.find_value(id)};
    if (!r.isObject() || !r.find_value("wallet").isStr()) return std::nullopt;
    Record out;
    out.wallet = r.find_value("wallet").get_str();
    // Checked when read back: a name the supervisor and the adapter take (SidechainSupervisor::Rpc puts it in a path).
    if (out.wallet.empty() || out.wallet.size() > 64 || out.wallet.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") != std::string::npos) return std::nullopt;
    out.account = r.find_value("account").isNum() ? static_cast<uint32_t>(r.find_value("account").getInt<int64_t>() & 0x7fffffff) : 0;
    out.genesis = r.find_value("genesis").isStr() ? r.find_value("genesis").get_str() : std::string{};
    out.imported = r.find_value("imported").isTrue();
    return out;
}

void SeedKeeper::SetRecord(const std::string& id, const Record& record)
{
    LOCK(m_mutex);
    UniValue records{Records()};
    UniValue chains{records.find_value("chains").isObject() ? records.find_value("chains") : UniValue{UniValue::VOBJ}};
    UniValue r{UniValue::VOBJ};
    r.pushKV("wallet", record.wallet);
    r.pushKV("account", static_cast<int64_t>(record.account));
    if (!record.genesis.empty()) r.pushKV("genesis", record.genesis);
    r.pushKV("imported", record.imported);
    r.pushKV("time", Now());
    UniValue next{UniValue::VOBJ};
    for (const std::string& key : chains.getKeys()) {
        if (key != id) next.pushKV(key, chains.find_value(key));
    }
    next.pushKV(id, std::move(r));
    UniValue out{UniValue::VOBJ};
    for (const std::string& key : records.getKeys()) {
        if (key != "chains") out.pushKV(key, records.find_value(key));
    }
    out.pushKV("chains", std::move(next));
    if (m_hooks.store) m_hooks.store(out);
}

void SeedKeeper::SetFlag(const std::string& key)
{
    LOCK(m_mutex);
    UniValue records{Records()};
    UniValue out{UniValue::VOBJ};
    for (const std::string& k : records.getKeys()) {
        if (k != key) out.pushKV(k, records.find_value(k));
    }
    out.pushKV(key, Now());
    if (m_hooks.store) m_hooks.store(out);
}

bool SeedKeeper::Running(const std::string& id) const
{
    if (id == APP_CHAIN) return m_hooks.node_ready && m_hooks.node_ready();
    return m_hooks.side_running && m_hooks.side_running(id);
}

UniValue SeedKeeper::ChainRpc(const std::string& id, const std::string& method, const UniValue& params, const std::string& wallet, std::chrono::milliseconds timeout) const
{
    if (id == APP_CHAIN) return m_hooks.node_rpc(method, params, wallet);
    return m_hooks.side_rpc(id, method, params, wallet, timeout);
}

int64_t SeedKeeper::ScanFrom(const seed::Header& header)
{
    // A phrase made here has no coins from before; a restored one is read from about when it was made (a day earlier), or
    // from the start of each chain.
    return std::max<int64_t>(0, header.birthday);
}

UniValue SeedKeeper::Status()
{
    UniValue out{UniValue::VOBJ};
    const auto header{FileHeader()};
    const bool exists{Exists()};
    out.pushKV("exists", exists);
    out.pushKV("minPassphrase", static_cast<uint64_t>(seed::MIN_PASSPHRASE));
    if (exists && !header) out.pushKV("damaged", true);
    if (header) {
        out.pushKV("words", header->words);
        out.pushKV("created", header->created);
        out.pushKV("restored", header->birthday != header->created);
        out.pushKV("fingerprint", header->fingerprint);
        out.pushKV("extraWord", header->extra);
    }
    const UniValue records{Records()};
    struct JobState {
        std::string state, error, wallet;
        bool done;
    };
    std::map<std::string, JobState> jobs;
    {
        LOCK(m_mutex);
        out.pushKV("unlocked", m_session.has_value());
        if (m_session) out.pushKV("expiresIn", std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::seconds>(m_session->until - std::chrono::steady_clock::now()).count()));
        for (const auto& [id, job] : m_jobs) jobs[id] = {job->state, job->error, job->wallet, job->done};
    }
    const bool node{m_hooks.node_ready && m_hooks.node_ready()};
    out.pushKV("nodeReady", node);
    // The first start (no phrase, no wallet of the app's chain yet) and the offer to those who have wallets (decision 6).
    if (!exists && node) {
        bool any{false};
        bool wallets{true};
        try {
            any = !Names(m_hooks.node_rpc("listwalletdir", UniValue{UniValue::VARR}, "")).empty();
        } catch (const std::exception&) {
            // No wallets in this node (-disablewallet): nothing to make, nothing to offer.
            wallets = false;
        }
        out.pushKV("hasWallets", any);
        out.pushKV("firstRun", wallets && !any && !records.find_value("firstRunDone").isNum());
        out.pushKV("offer", wallets && any && !records.find_value("offerShown").isNum());
    } else {
        out.pushKV("firstRun", false);
        out.pushKV("offer", false);
    }
    std::vector<std::string> ids{std::string{APP_CHAIN}};
    const std::vector<std::string> shared{m_hooks.shared_chains ? m_hooks.shared_chains() : std::vector<std::string>{}};
    const std::vector<std::string> separate{m_hooks.separate_chains ? m_hooks.separate_chains() : std::vector<std::string>{}};
    for (const auto& s : shared) ids.push_back(s);
    for (const auto& s : separate) ids.push_back(s);
    if (records.find_value("chains").isObject()) {
        for (const std::string& key : records.find_value("chains").getKeys()) {
            if (!Has(ids, key) && SidechainSupervisor::FindSpec(key)) ids.push_back(key);
        }
    }
    UniValue chains{UniValue::VARR};
    for (const std::string& id : ids) {
        UniValue c{UniValue::VOBJ};
        c.pushKV("id", id);
        c.pushKV("name", id == APP_CHAIN ? m_hooks.product : (m_hooks.name ? m_hooks.name(id) : id));
        const auto record{RecordOf(records, id)};
        const bool on{id == APP_CHAIN || Has(shared, id) || Has(separate, id)};
        const bool separate_mode{Has(separate, id)};
        std::string mode{separate_mode ? "separate" : exists ? "phrase" : "own"};
        std::string state;
        if (const auto it{jobs.find(id)}; it != jobs.end() && !it->second.done) {
            state = it->second.state;
            c.pushKV("wallet", it->second.wallet);
            if (state == "scanning" && !it->second.wallet.empty()) {
                try {
                    const UniValue info{ChainRpc(id, "getwalletinfo", UniValue{UniValue::VARR}, it->second.wallet, 5s)};
                    const UniValue& scan{info.find_value("scanning")};
                    if (scan.isObject() && scan.find_value("progress").isNum()) c.pushKV("progress", scan.find_value("progress").get_real());
                } catch (const std::exception&) {
                }
            }
        } else if (record && record->imported) {
            state = on ? "ready" : "off";
            c.pushKV("wallet", record->wallet);
            c.pushKV("account", static_cast<int64_t>(record->account));
            mode = "phrase";
        } else if (const auto it2{jobs.find(id)}; it2 != jobs.end() && !it2->second.error.empty()) {
            state = "error";
            c.pushKV("error", it2->second.error);
        } else if (separate_mode || !exists) {
            state = on ? "own" : "off";
        } else if (!Running(id)) {
            state = "waiting";
        } else {
            state = "needs-keys";
        }
        c.pushKV("mode", mode);
        c.pushKV("state", state);
        chains.push_back(std::move(c));
    }
    out.pushKV("chains", std::move(chains));
    return out;
}

std::optional<std::pair<SecureString, SecureString>> SeedKeeper::AskNewPassphrase(bool extra_word, const std::string& title)
{
    Window::ConfirmDialog d;
    d.title = title;
    d.message = strprintf("It encrypts your backup phrase on this computer and the wallet of each chain made from it. You type it to send "
                          "coins, and to make the keys of a sidechain you turn on.\n\nIt is not part of your backup: on another computer your "
                          "words bring everything back, and you choose a passphrase there.");
    d.accept = "Make my wallet";
    d.level = dialog::Level::INFO;
    d.kind = "seed-passphrase";
    d.fields.push_back(MakeField("passphrase", dialog::Field::Kind::NEW_PASSWORD, "Passphrase", strprintf("At least %u characters. A few words you will remember are good.", seed::MIN_PASSPHRASE)));
    d.fields.push_back(MakeField("repeat", dialog::Field::Kind::PASSWORD, "Type it again", ""));
    if (extra_word) {
        d.fields.push_back(MakeField("extra", dialog::Field::Kind::SECRET, "Extra word",
                            "Part of your backup: without it your words bring back another, empty wallet. Write it down with them. Letters, digits and signs of a keyboard, no accents."));
    }
    for (int attempt{0}; attempt < 10; ++attempt) {
        dialog::Answer answer{m_hooks.ask(d)};
        if (!answer.accepted) return std::nullopt;
        SecureString pass{FieldOf(answer, "passphrase")};
        const SecureString repeat{FieldOf(answer, "repeat")};
        SecureString extra{FieldOf(answer, "extra")};
        for (auto& f : d.fields) f.error.clear();
        if (pass.size() < seed::MIN_PASSPHRASE) {
            d.fields[0].error = strprintf("At least %u characters.", seed::MIN_PASSPHRASE);
        } else if (pass.size() > seed::MAX_PASSPHRASE) {
            d.fields[0].error = "Too long.";
        } else if (pass != repeat) {
            d.fields[1].error = "The two are not the same.";
        } else if (extra_word && extra.empty()) {
            d.fields[2].error = "Type the extra word, or go back and make the phrase without one.";
        } else if (extra_word && !bip39::ExtraOk(Str(extra))) {
            d.fields[2].error = "Letters, digits and signs of a keyboard only, no accents, at most 256.";
        } else {
            return std::make_pair(std::move(pass), std::move(extra));
        }
    }
    return std::nullopt;
}

std::optional<std::pair<seed::Opened, SecureString>> SeedKeeper::AskOpen(const std::string& title, const std::string& message)
{
    std::string error;
    const auto text{ReadFile(error)};
    if (!text) throw HostError(error);
    Window::ConfirmDialog d;
    d.title = title;
    d.message = message;
    d.accept = "Unlock";
    d.level = dialog::Level::INFO;
    d.kind = "seed-unlock";
    d.passphrase = true;
    d.passphrase_label = "Your wallet passphrase";
    for (int attempt{0}; attempt < 3; ++attempt) {
        dialog::Answer answer{m_hooks.ask(d)};
        if (!answer.accepted) return std::nullopt;
        seed::Opened opened{seed::Decrypt(*text, std::string_view{answer.passphrase.data(), answer.passphrase.size()})};
        if (opened.secret) return std::make_pair(std::move(opened), std::move(answer.passphrase));
        if (!opened.wrong_passphrase) throw HostError(opened.error);
        d.error = "That is not the passphrase of your wallet. Try again.";
    }
    throw HostError(RPC_WALLET_PASSPHRASE_INCORRECT, "The passphrase is not right.");
}

void SeedKeeper::Keep(seed::Secret secret, SecureString passphrase, seed::Header header, std::set<std::string> awaited)
{
    const std::string text{seed::Encrypt(secret, std::string_view{passphrase.data(), passphrase.size()}, header)};
    if (text.empty()) throw HostError("The backup phrase could not be encrypted.");
    if (Exists()) throw HostError("This app has a backup phrase already.");
    if (!m_hooks.file.parent_path().empty()) fs::create_directories(m_hooks.file.parent_path());
    if (const auto written{common::WriteSecretFile(m_hooks.file, text)}; !written) throw HostError(strprintf("The backup phrase could not be saved: %s", util::ErrorString(written).original));
    LogInfo("webui: a backup phrase was %s (fingerprint %s)", header.birthday == header.created ? "made" : "restored", header.fingerprint);
    {
        LOCK(m_mutex);
        m_session = Session{std::move(secret), std::move(passphrase), header, std::chrono::steady_clock::now() + m_hooks.session, std::move(awaited)};
    }
    m_cv.notify_all();
}

void SeedKeeper::StartWaiting()
{
    const UniValue records{Records()};
    std::vector<std::string> ids;
    if (Running(std::string{APP_CHAIN})) ids.emplace_back(APP_CHAIN);
    for (const std::string& stem : m_hooks.shared_chains ? m_hooks.shared_chains() : std::vector<std::string>{}) {
        if (Running(stem)) ids.push_back(stem);
    }
    {
        LOCK(m_mutex);
        if (!m_session || m_stopping) return;
        const bool waited{!m_session->awaited.empty()};
        for (const std::string& id : ids) {
            const auto record{RecordOf(records, id)};
            if (record && record->imported) {
                m_session->awaited.erase(id);
                continue;
            }
            StartJob(id);
        }
        // Opened for chains that all have their keys already: nothing to keep it for.
        if (waited && m_session->awaited.empty()) Wipe();
    }
    Changed();
}

void SeedKeeper::StartJob(const std::string& id)
{
    if (!m_session) return;
    if (const auto it{m_jobs.find(id)}; it != m_jobs.end()) {
        if (!it->second->done) return;
        if (it->second->thread.joinable()) m_finished.push_back(std::move(it->second->thread));
    }
    auto job{std::make_shared<Job>()};
    m_jobs[id] = job;
    job->thread = std::thread([this, id, secret = m_session->secret, pass = m_session->passphrase, from = ScanFrom(m_session->header)]() mutable {
        RunJob(id, std::move(secret), std::move(pass), from);
    });
}

void SeedKeeper::RunJob(const std::string& id, seed::Secret secret, SecureString passphrase, int64_t timestamp)
{
    std::shared_ptr<Job> job;
    {
        LOCK(m_mutex);
        job = m_jobs[id];
    }
    const auto set_state{[&](const std::string& state, const std::string& wallet = {}) {
        LOCK(m_mutex);
        job->state = state;
        if (!wallet.empty()) job->wallet = wallet;
    }};
    std::string wallet;
    bool unlocked{false};
    try {
        const bool test{m_hooks.test_chain};
        std::string genesis;
        uint32_t account{0};
        if (id != APP_CHAIN) {
            UniValue zero{UniValue::VARR};
            zero.push_back(0);
            const UniValue hash{ChainRpc(id, "getblockhash", zero, "", 30s)};
            if (!hash.isStr() || hash.get_str().size() != 64) throw std::runtime_error("its node did not give its first block");
            genesis = hash.get_str();
            account = seed::Account(id, genesis);
        }
        // The passphrase of its wallet: the user's own on the app's chain (this process, where seed.dat is); on a sidechain one
        // made from the seed for that chain alone (seed::WalletPassphrase): the user's never reaches a sidechain's node.
        const SecureString wallet_pass{id == APP_CHAIN ? passphrase : seed::WalletPassphrase(secret.seed, id, genesis)};
        // The wallet of the phrase on this chain: the one made for it before if its keys were not all in yet (the same
        // genesis), else a new one: "main" where there is none, else "seed" (never a wallet that exists: an old wallet's
        // keys are never touched).
        const auto record{RecordOf(Records(), id)};
        const std::vector<std::string> names{Names(ChainRpc(id, "listwalletdir", UniValue{UniValue::VARR}, "", 30s))};
        if (record && !record->imported && record->genesis == genesis && Has(names, record->wallet)) {
            wallet = record->wallet;
            const UniValue loaded{ChainRpc(id, "listwallets", UniValue{UniValue::VARR}, "", 30s)};
            bool is_loaded{false};
            for (const UniValue& w : loaded.getValues()) is_loaded = is_loaded || (w.isStr() && w.get_str() == wallet);
            if (!is_loaded) {
                UniValue p{UniValue::VARR};
                p.push_back(wallet);
                ChainRpc(id, "loadwallet", p, "", 120s);
            }
        } else {
            wallet = Has(names, "main") ? "seed" : "main";
            for (int i{2}; Has(names, wallet); ++i) wallet = strprintf("seed-%d", i);
            UniValue p{UniValue::VARR};
            p.push_back(wallet);
            p.push_back(false); // private keys
            p.push_back(true);  // blank: no keys of its own, only those of the phrase
            p.push_back(Str(wallet_pass));
            p.push_back(false); // avoid_reuse
            p.push_back(true);  // descriptors
            p.push_back(true);  // loaded at each start
            try {
                ChainRpc(id, "createwallet", p, "", 120s);
            } catch (...) {
                WipeValue(p);
                throw;
            }
            WipeValue(p);
        }
        set_state("importing", wallet);
        SetRecord(id, {wallet, account, genesis, false});
        {
            UniValue p{UniValue::VARR};
            p.push_back(Str(wallet_pass));
            p.push_back(900);
            try {
                ChainRpc(id, "walletpassphrase", p, wallet, 60s);
            } catch (...) {
                WipeValue(p);
                throw;
            }
            WipeValue(p);
            unlocked = true;
        }
        const uint32_t coin{seed::CoinType(test)};
        std::vector<seed::Descriptor> descriptors{seed::AccountDescriptors(secret.seed, coin, account, test)};
        if (descriptors.empty()) throw std::runtime_error("its keys could not be made");
        UniValue requests{UniValue::VARR};
        for (const auto& d : descriptors) {
            UniValue r{UniValue::VOBJ};
            r.pushKV("desc", Str(d.text));
            r.pushKV("active", true);
            r.pushKV("internal", d.internal);
            if (timestamp >= Now() - 86400) r.pushKV("timestamp", "now");
            else r.pushKV("timestamp", timestamp);
            requests.push_back(std::move(r));
        }
        descriptors.clear();
        if (timestamp < Now() - 86400) set_state("scanning", wallet);
        UniValue args{UniValue::VARR};
        args.push_back(std::move(requests));
        UniValue result;
        try {
            result = ChainRpc(id, "importdescriptors", args, wallet, m_hooks.import_timeout);
        } catch (...) {
            WipeValue(args);
            throw;
        }
        WipeValue(args);
        std::string failed;
        for (const UniValue& r : result.getValues()) {
            if (r.find_value("success").isTrue()) continue;
            const UniValue& message{r.find_value("error").find_value("message")};
            failed = message.isStr() ? message.get_str() : "a key was refused";
        }
        if (!result.isArray() || result.size() != 4) failed = failed.empty() ? "its node did not take the keys" : failed;
        if (!failed.empty()) throw std::runtime_error(failed);
        SetRecord(id, {wallet, account, genesis, true});
        LogInfo("webui: the wallet %s of %s is made from the backup phrase (account %u)", wallet, id, account);
    } catch (const std::exception& e) {
        const std::string error{seed::Scrubbed(e.what())};
        LogWarning("webui: the keys of %s were not made from the backup phrase: %s", id, error);
        LOCK(m_mutex);
        job->error = error;
    }
    if (unlocked) {
        try {
            ChainRpc(id, "walletlock", UniValue{UniValue::VARR}, wallet, 30s);
        } catch (const std::exception&) {
        }
    }
    {
        LOCK(m_mutex);
        job->done = true;
        job->state = job->error.empty() ? "ready" : "error";
        // The session was opened for these chains: once each has its keys, it is wiped at once.
        if (m_session && job->error.empty()) {
            const bool waited{!m_session->awaited.empty()};
            m_session->awaited.erase(id);
            if (waited && m_session->awaited.empty()) Wipe();
        }
    }
    Changed();
}

UniValue SeedKeeper::Create(const UniValue& params)
{
    if (Exists()) throw HostError("This app has a backup phrase already.");
    if (!m_hooks.node_ready || !m_hooks.node_ready()) throw HostError(BRIDGE_NOT_READY, "The node is not ready yet");
    bool expected{false};
    if (!m_busy.compare_exchange_strong(expected, true)) throw HostError("The backup phrase is being made or shown already.");
    struct Done {
        std::atomic<bool>& busy;
        ~Done() { busy = false; }
    } done{m_busy};
    const UniValue count{Param(params, "words")};
    const int words{count.isNum() && count.getInt<int>() == 24 ? 24 : 12};
    const bool extra_word{Param(params, "extraWord").isTrue()};
    bip39::Bytes entropy(words == 24 ? 32 : 16);
    GetStrongRandBytes(std::span{entropy});
    const bip39::Words phrase{bip39::FromEntropy(entropy)};
    auto shown{std::make_shared<std::vector<SecureString>>(phrase.begin(), phrase.end())};

    // 1. The words, once; ticked "written down".
    Window::ConfirmDialog show;
    show.title = "Your backup phrase";
    show.message = strprintf("Write these %d words on paper, in this order, and keep the paper somewhere safe.\n\n"
                             "They are the key to your coins on %s and on every sidechain: anyone who has them can take the coins, and "
                             "without them a lost or broken computer means lost coins. Never type them into a website, a chat or another "
                             "app, and never take a picture of them.\n\nThis window does not let them be copied: what is copied can be read by other programs.",
                             words, m_hooks.product);
    show.words = shown;
    show.checkbox = strprintf("I wrote the %d words down, in order", words);
    show.accept = "Next";
    show.level = dialog::Level::WARNING;
    show.kind = "seed-words";
    if (!m_hooks.ask(show).accepted) return Cancelled();

    // 2. Three of them typed back, from the paper.
    FastRandomContext rng;
    std::vector<int> positions;
    while (positions.size() < 3) {
        const int p{static_cast<int>(rng.randrange(words))};
        if (std::ranges::find(positions, p) == positions.end()) positions.push_back(p);
    }
    std::ranges::sort(positions);
    Window::ConfirmDialog check;
    check.title = "Check your backup";
    check.message = "Type these words from your paper, to be sure it is right.\n\nCancel to start over: the words are not kept.";
    check.accept = "Check";
    check.level = dialog::Level::INFO;
    check.kind = "seed-check";
    for (const int p : positions) check.fields.push_back(MakeField(strprintf("word-%d", p + 1), dialog::Field::Kind::WORD, strprintf("Word %d", p + 1), ""));
    for (int attempt{0};; ++attempt) {
        if (attempt == 5) throw HostError("The words typed were not those of the phrase five times: start over, and write them down carefully.");
        dialog::Answer answer{m_hooks.ask(check)};
        if (!answer.accepted) return Cancelled();
        bool right{true};
        for (size_t i{0}; i < positions.size(); ++i) {
            SecureString typed{FieldOf(answer, check.fields[i].id)};
            std::ranges::transform(typed, typed.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            while (!typed.empty() && typed.back() == ' ') typed.pop_back();
            while (!typed.empty() && typed.front() == ' ') typed.erase(typed.begin());
            const bool ok{typed == phrase[positions[i]]};
            check.fields[i].error = ok ? "" : "Not this word: look at your paper again.";
            right = right && ok;
        }
        if (right) break;
    }

    // 3. The passphrase that encrypts it (and every wallet made from it); the extra word when asked for.
    auto pass{AskNewPassphrase(extra_word, "A passphrase for your wallet")};
    if (!pass) return Cancelled();
    seed::Secret secret;
    secret.entropy = entropy;
    secret.seed = bip39::Seed(phrase, std::string_view{pass->second.data(), pass->second.size()});
    seed::Header header;
    header.network = ChainTypeToString(m_hooks.chain);
    header.created = Now();
    header.birthday = header.created;
    header.words = words;
    header.extra = extra_word;
    header.fingerprint = seed::Fingerprint(secret.seed);
    const std::string fingerprint{header.fingerprint};
    // Unlocked for a while (Hooks::session): the first run picks its sidechains next, whose keys are made as they start.
    Keep(std::move(secret), std::move(pass->first), header, {});
    StartWaiting();
    UniValue out{UniValue::VOBJ};
    out.pushKV("created", true);
    out.pushKV("fingerprint", fingerprint);
    out.pushKV("words", words);
    return out;
}

UniValue SeedKeeper::Restore(const UniValue& params)
{
    (void)params;
    if (Exists()) throw HostError("This app has a backup phrase already: a phrase is restored into a new data folder.");
    if (!m_hooks.node_ready || !m_hooks.node_ready()) throw HostError(BRIDGE_NOT_READY, "The node is not ready yet");
    bool expected{false};
    if (!m_busy.compare_exchange_strong(expected, true)) throw HostError("The backup phrase is being made or shown already.");
    struct Done {
        std::atomic<bool>& busy;
        ~Done() { busy = false; }
    } done{m_busy};
    Window::ConfirmDialog d;
    d.title = "Restore from your backup phrase";
    d.message = strprintf("Type the words of your backup phrase, in order, with spaces between them.\n\nUse a phrase made for %s, not one of "
                          "a Bitcoin wallet: the same phrase gives the same keys on both, and your addresses on one could be linked to the other.",
                          m_hooks.product);
    d.accept = "Restore";
    d.level = dialog::Level::INFO;
    d.kind = "seed-restore";
    d.fields.push_back(MakeField("words", dialog::Field::Kind::WORDS, "Your backup phrase", "12 or 24 words"));
    d.fields.push_back(MakeField("day", dialog::Field::Kind::DATE, "About when did you make it?",
                        "Optional. Each chain is read again from that day; without it, from its start, which takes longer.", true));
    d.fields.push_back(MakeField("extra", dialog::Field::Kind::SECRET, "Extra word", "Only if you set one when the phrase was made.", true));
    bip39::Parsed parsed;
    SecureString extra;
    int64_t birthday{0};
    for (int attempt{0};; ++attempt) {
        if (attempt == 10) throw HostError("The phrase was not restored.");
        dialog::Answer answer{m_hooks.ask(d)};
        if (!answer.accepted) return Cancelled();
        for (auto& f : d.fields) {
            f.error.clear();
            f.value = FieldOf(answer, f.id);
        }
        const SecureString text{FieldOf(answer, "words")};
        parsed = bip39::Parse(std::string_view{text.data(), text.size()});
        extra = FieldOf(answer, "extra");
        const SecureString day{FieldOf(answer, "day")};
        std::optional<int64_t> when;
        if (!day.empty()) when = DayOf(std::string_view{day.data(), day.size()});
        if (!parsed.ok) {
            d.fields[0].error = parsed.error;
        } else if (!day.empty() && (!when || *when > Now() + 86400)) {
            d.fields[1].error = "A day such as 2026-10-21, not after today.";
        } else if (!bip39::ExtraOk(Str(extra))) {
            d.fields[2].error = "Letters, digits and signs of a keyboard only, no accents, at most 256.";
        } else {
            // A day early, as a phrase made late that day may have been paid at once.
            birthday = when ? std::max<int64_t>(0, *when - 86400) : 0;
            break;
        }
    }
    for (auto& f : d.fields) f.value.clear();
    auto pass{AskNewPassphrase(false, "A passphrase for your wallet")};
    if (!pass) return Cancelled();
    seed::Secret secret;
    secret.entropy = parsed.entropy;
    secret.seed = bip39::Seed(parsed.words, std::string_view{extra.data(), extra.size()});
    seed::Header header;
    header.network = ChainTypeToString(m_hooks.chain);
    header.created = Now();
    header.birthday = birthday;
    // Never the same as created: a restored phrase says so (Status "restored").
    if (header.birthday == header.created) header.birthday -= 1;
    header.words = static_cast<int>(parsed.words.size());
    header.extra = !extra.empty();
    header.fingerprint = seed::Fingerprint(secret.seed);
    const std::string fingerprint{header.fingerprint};
    Keep(std::move(secret), std::move(pass->first), header, {});
    StartWaiting();
    UniValue out{UniValue::VOBJ};
    out.pushKV("restored", true);
    out.pushKV("fingerprint", fingerprint);
    out.pushKV("words", header.words);
    return out;
}

UniValue SeedKeeper::Unlock(const UniValue& params)
{
    if (!Exists()) throw HostError("There is no backup phrase on this computer yet.");
    std::set<std::string> awaited;
    std::vector<std::string> names;
    const UniValue stems{Param(params, "stems")};
    if (stems.isArray()) {
        for (const UniValue& s : stems.getValues()) {
            if (s.isStr() && SidechainSupervisor::FindSpec(s.get_str())) {
                awaited.insert(s.get_str());
                names.push_back(m_hooks.name ? m_hooks.name(s.get_str()) : s.get_str());
            }
        }
    }
    {
        LOCK(m_mutex);
        if (m_session) {
            m_session->awaited.insert(awaited.begin(), awaited.end());
            m_session->until = std::max(m_session->until, std::chrono::steady_clock::now() + m_hooks.session);
        }
    }
    if (WITH_LOCK(m_mutex, return m_session.has_value())) {
        m_cv.notify_all();
        StartWaiting();
        UniValue out{UniValue::VOBJ};
        out.pushKV("unlocked", true);
        return out;
    }
    std::string what;
    for (size_t i{0}; i < names.size(); ++i) what += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
    const std::string message{what.empty() ? strprintf("To make the keys of your chains from your backup phrase.") :
                                             strprintf("To make the keys of %s from your backup phrase. It stays unlocked until they are made, %d minutes at most.",
                                                       what, std::chrono::duration_cast<std::chrono::minutes>(m_hooks.session).count())};
    auto opened{AskOpen("Your wallet passphrase", message)};
    if (!opened) return Cancelled();
    {
        LOCK(m_mutex);
        m_session = Session{std::move(*opened->first.secret), std::move(opened->second), opened->first.header, std::chrono::steady_clock::now() + m_hooks.session, std::move(awaited)};
    }
    m_cv.notify_all();
    StartWaiting();
    UniValue out{UniValue::VOBJ};
    out.pushKV("unlocked", true);
    return out;
}

void SeedKeeper::Lock()
{
    {
        LOCK(m_mutex);
        Wipe();
    }
    m_cv.notify_all();
    Changed();
}

UniValue SeedKeeper::AddChain(const std::string& stem)
{
    if (!SidechainSupervisor::FindSpec(stem)) throw HostError(BRIDGE_INVALID_REQUEST, "No such sidechain");
    if (!Running(stem)) throw HostError(strprintf("%s is not running: turn it on first.", m_hooks.name ? m_hooks.name(stem) : stem));
    UniValue params{UniValue::VOBJ};
    UniValue stems{UniValue::VARR};
    stems.push_back(stem);
    params.pushKV("stems", std::move(stems));
    {
        // A failed import is tried again.
        LOCK(m_mutex);
        if (const auto it{m_jobs.find(stem)}; it != m_jobs.end() && it->second->done && !it->second->error.empty()) it->second->error.clear();
    }
    return Unlock(params);
}

UniValue SeedKeeper::Show()
{
    bool expected{false};
    if (!m_busy.compare_exchange_strong(expected, true)) throw HostError("The backup phrase is being made or shown already.");
    struct Done {
        std::atomic<bool>& busy;
        ~Done() { busy = false; }
    } done{m_busy};
    auto opened{AskOpen("Your wallet passphrase", "To show your backup phrase. Make sure nobody can see your screen.")};
    if (!opened) return Cancelled();
    const bip39::Words phrase{bip39::FromEntropy(opened->first.secret->entropy)};
    Window::ConfirmDialog show;
    show.title = "Your backup phrase";
    show.message = strprintf("The %d words of your backup phrase, in order.%s\n\nClose this window when you are done.", phrase.size(),
                             opened->first.header.extra ? " Your phrase also has an extra word, which is not shown: you wrote it down with them." : "");
    show.words = std::make_shared<std::vector<SecureString>>(phrase.begin(), phrase.end());
    show.accept = "Done";
    show.cancel = "Close";
    show.level = dialog::Level::WARNING;
    show.kind = "seed-show";
    m_hooks.ask(show);
    UniValue out{UniValue::VOBJ};
    out.pushKV("shown", true);
    return out;
}

UniValue SeedKeeper::ChangePassphrase()
{
    std::string error;
    const auto text{ReadFile(error)};
    if (!text) throw HostError(error);
    bool expected{false};
    if (!m_busy.compare_exchange_strong(expected, true)) throw HostError("The backup phrase is being made or shown already.");
    struct Done {
        std::atomic<bool>& busy;
        ~Done() { busy = false; }
    } done{m_busy};
    // The user's passphrase is that of seed.dat and of the app's own wallet made from it (both in this process). The
    // sidechains' wallets have passphrases of their own made from the seed (seed::WalletPassphrase): they do not change.
    const auto record{RecordOf(Records(), std::string{APP_CHAIN})};
    std::vector<std::pair<std::string, std::string>> wallets;
    if (record && record->imported) {
        if (!Running(std::string{APP_CHAIN})) throw HostError("The node is not ready yet");
        wallets.emplace_back(std::string{APP_CHAIN}, record->wallet);
    }
    Window::ConfirmDialog d;
    d.title = "Change the wallet passphrase";
    d.message = strprintf("It changes for your backup phrase kept on this computer and for your %s wallet made from it, at once. "
                          "Your words do not change, and neither do the keys of the sidechains' wallets: they are made from your phrase.",
                          m_hooks.product);
    d.accept = "Change";
    d.level = dialog::Level::INFO;
    d.kind = "seed-change";
    d.fields.push_back(MakeField("old", dialog::Field::Kind::PASSWORD, "Current passphrase", ""));
    d.fields.push_back(MakeField("passphrase", dialog::Field::Kind::NEW_PASSWORD, "New passphrase", strprintf("At least %u characters.", seed::MIN_PASSPHRASE)));
    d.fields.push_back(MakeField("repeat", dialog::Field::Kind::PASSWORD, "Type the new one again", ""));
    for (int attempt{0}; attempt < 10; ++attempt) {
        dialog::Answer answer{m_hooks.ask(d)};
        if (!answer.accepted) return Cancelled();
        for (auto& f : d.fields) f.error.clear();
        const SecureString old_pass{FieldOf(answer, "old")};
        const SecureString new_pass{FieldOf(answer, "passphrase")};
        if (new_pass.size() < seed::MIN_PASSPHRASE) {
            d.fields[1].error = strprintf("At least %u characters.", seed::MIN_PASSPHRASE);
            continue;
        }
        if (new_pass.size() > seed::MAX_PASSPHRASE) {
            d.fields[1].error = "Too long.";
            continue;
        }
        if (new_pass != FieldOf(answer, "repeat")) {
            d.fields[2].error = "The two are not the same.";
            continue;
        }
        seed::Opened opened{seed::Decrypt(*text, std::string_view{old_pass.data(), old_pass.size()})};
        if (!opened.secret) {
            if (!opened.wrong_passphrase) throw HostError(opened.error);
            d.fields[0].error = "That is not the passphrase of your wallet.";
            continue;
        }
        // The wallet first, then seed.dat; seed.dat not written: the wallet is changed back.
        const auto change{[&](const std::pair<std::string, std::string>& w, const SecureString& from, const SecureString& to) {
            UniValue p{UniValue::VARR};
            p.push_back(Str(from));
            p.push_back(Str(to));
            try {
                ChainRpc(w.first, "walletpassphrasechange", p, w.second, 120s);
            } catch (...) {
                WipeValue(p);
                throw;
            }
            WipeValue(p);
        }};
        size_t changed{0};
        std::string failed;
        for (; changed < wallets.size(); ++changed) {
            try {
                change(wallets[changed], old_pass, new_pass);
            } catch (const std::exception& e) {
                failed = strprintf("your %s wallet %s: %s", m_hooks.product, wallets[changed].second, e.what());
                break;
            }
        }
        std::string written;
        if (failed.empty()) {
            const std::string next{seed::Encrypt(*opened.secret, std::string_view{new_pass.data(), new_pass.size()}, opened.header)};
            if (next.empty()) {
                written = "the backup phrase could not be encrypted";
            } else if (const auto ok{common::WriteSecretFile(m_hooks.file, next)}; !ok) {
                written = util::ErrorString(ok).original;
            }
        }
        if (!failed.empty() || !written.empty()) {
            for (size_t i{0}; i < changed; ++i) {
                try {
                    change(wallets[i], new_pass, old_pass);
                } catch (const std::exception& e) {
                    LogWarning("webui: the passphrase of the wallet %s could not be changed back: %s", wallets[i].second, e.what());
                }
            }
            throw HostError(strprintf("The passphrase was not changed (nothing was): %s", failed.empty() ? written : failed));
        }
        {
            LOCK(m_mutex);
            if (m_session) m_session->passphrase = new_pass;
        }
        LogInfo("webui: the passphrase of the backup phrase was changed");
        UniValue out{UniValue::VOBJ};
        out.pushKV("changed", true);
        out.pushKV("wallets", static_cast<uint64_t>(wallets.size()));
        return out;
    }
    throw HostError("The passphrase was not changed.");
}

SecureString SeedKeeper::PassphraseFor(const std::string& stem, const SecureString& typed)
{
    // Not the phrase's passphrase: the wallet's own (a separate wallet, one made before the phrase), sent as typed.
    if (typed.empty() || !Exists()) return typed;
    std::string error;
    const auto text{ReadFile(error)};
    if (!text) return typed;
    const seed::Opened opened{seed::Decrypt(*text, std::string_view{typed.data(), typed.size()})};
    if (!opened.secret) return typed;
    // The phrase's: never sent to a sidechain's node. Its wallet there has a passphrase of its own, made from the seed; a
    // chain without one gets nothing (the node says the passphrase is wrong, the user is asked again).
    const auto record{RecordOf(Records(), stem)};
    if (!record || !record->imported) return {};
    return seed::WalletPassphrase(opened.secret->seed, stem, record->genesis);
}

void SeedKeeper::Dismiss(const std::string& what)
{
    if (what == "firstRun") SetFlag("firstRunDone");
    else if (what == "offer") SetFlag("offerShown");
    else throw HostError(BRIDGE_INVALID_REQUEST, "what: firstRun or offer");
    Changed();
}

std::optional<std::string> SeedKeeper::WalletOf(const std::string& stem)
{
    if (!Exists()) return std::nullopt;
    const std::vector<std::string> shared{m_hooks.shared_chains ? m_hooks.shared_chains() : std::vector<std::string>{}};
    if (!Has(shared, stem)) return std::nullopt;
    const auto record{RecordOf(Records(), stem)};
    return record ? record->wallet : std::string{};
}

std::string SeedKeeper::MadeWallet(const std::string& id)
{
    const auto record{RecordOf(Records(), id)};
    return record && record->imported ? record->wallet : std::string{};
}

void SeedKeeper::ChainReady(const std::string& stem)
{
    if (!Exists()) return;
    const std::vector<std::string> shared{m_hooks.shared_chains ? m_hooks.shared_chains() : std::vector<std::string>{}};
    if (!Has(shared, stem) || !Running(stem)) return;
    const auto record{RecordOf(Records(), stem)};
    if (record && record->imported) return;
    {
        LOCK(m_mutex);
        if (m_session && !m_stopping) StartJob(stem);
    }
    Changed();
}

} // namespace webui
