// Copyright (c) 2026 The Backbone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <grpc/services.h>

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/amount.h>
#include <core_io.h>
#include <crypto/hmac_sha512.h>
#include <crypto/ripemd160.h>
#include <crypto/sha256.h>
#include <drivechain/db.h>
#include <drivechain/miner.h>
#include <drivechain/scdb.h>
#include <drivechain/sidechain.h>
#include <hash.h>
#include <kernel/types.h>
#include <key_io.h>
#include <logging.h>
#include <node/blockstorage.h>
#include <node/context.h>
#include <primitives/transaction.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <rpc/server.h>
#include <secp256k1.h>
#include <streams.h>
#include <sync.h>
#include <tinyformat.h>
#include <txmempool.h>
#include <uint256.h>
#include <util/check.h>
#include <util/signalinterrupt.h>
#include <util/strencodings.h>
#include <util/thread.h>
#include <validation.h>
#include <validationinterface.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <set>
#include <thread>

namespace grpcsrv {

using drivechain::SidechainId;
using pb::Message;

namespace {

// ---------------------------------------------------------------------------
// The enforcer's conventions (lib/proto.rs, lib/server/mod.rs at 17eb2ce).

/** Error::MissingField (lib/proto.rs:263), INVALID_ARGUMENT (lib/proto.rs:323-327). */
Status MissingField(std::string_view message, std::string_view field)
{
    return {Code::INVALID_ARGUMENT, strprintf("Missing field in message `%s`: `%s`", std::string{message}, std::string{field})};
}

/** Error::InvalidFieldValue (lib/proto.rs:248), with its source, as `{:#}` prints it. */
Status InvalidField(std::string_view message, std::string_view field, std::string_view value, std::string_view cause)
{
    return {Code::INVALID_ARGUMENT, strprintf("Invalid field value in field `%s` of message `%s`: `%s`: %s", std::string{field}, std::string{message}, std::string{value}, std::string{cause})};
}

/** The hex string of a ConsensusHex, Hex or ReverseHex field. */
std::optional<std::string> WrappedHex(const Message& m, std::string_view field, Status& status)
{
    const Message* wrapper{m.GetMessage(field)};
    if (!wrapper) {
        status = MissingField(m.Desc().full_name, field);
        return std::nullopt;
    }
    const Message* hex{wrapper->GetMessage("hex")};
    if (!hex) {
        status = MissingField(wrapper->Desc().full_name, "hex");
        return std::nullopt;
    }
    return hex->GetString("value");
}

std::optional<std::vector<unsigned char>> DecodeHex(const std::string& hex, std::string& cause)
{
    if (hex.size() % 2) {
        cause = strprintf("odd number of hex digits (%u)", hex.size());
        return std::nullopt;
    }
    auto bytes{TryParseHex<unsigned char>(hex)};
    if (!bytes) cause = "invalid hex character";
    return bytes;
}

/** A ConsensusHex of a fixed size array (`size` bytes, no prefix) or of a Vec<u8> (`size` nullopt: compact size prefix). */
std::optional<std::vector<unsigned char>> ReadConsensusBytes(const Message& m, std::string_view field, std::optional<size_t> size, Status& status)
{
    const auto hex{WrappedHex(m, field, status)};
    if (!hex) return std::nullopt;
    std::string cause;
    auto bytes{DecodeHex(*hex, cause)};
    if (bytes) {
        if (size) {
            if (bytes->size() == *size) return bytes;
            cause = bytes->size() < *size ? "unexpected end of data" : "data not consumed entirely when explicitly deserializing";
        } else {
            try {
                DataStream ds{*bytes};
                std::vector<unsigned char> out;
                ds >> out;
                if (ds.empty()) return out;
                cause = "data not consumed entirely when explicitly deserializing";
            } catch (const std::exception&) {
                cause = "unexpected end of data";
            }
        }
    }
    status = InvalidField(m.Desc().full_name, field, *hex, cause);
    return std::nullopt;
}

/** A ReverseHex of a 32 byte hash (a block hash, a txid): reversed, as explorers show it. */
std::optional<uint256> ReadReverseHash(const Message& m, std::string_view field, Status& status)
{
    const auto hex{WrappedHex(m, field, status)};
    if (!hex) return std::nullopt;
    std::string cause;
    auto bytes{DecodeHex(*hex, cause)};
    if (bytes && bytes->size() == 32) {
        std::reverse(bytes->begin(), bytes->end());
        uint256 hash;
        std::memcpy(hash.begin(), bytes->data(), 32);
        return hash;
    }
    if (bytes) cause = bytes->size() < 32 ? "unexpected end of data" : "data not consumed entirely when explicitly deserializing";
    status = InvalidField(m.Desc().full_name, field, *hex, cause);
    return std::nullopt;
}

/** A Hex: bytes as they are; of `size` bytes if given. */
std::optional<std::vector<unsigned char>> ReadRawHex(const Message& m, std::string_view field, std::optional<size_t> size, Status& status)
{
    const auto hex{WrappedHex(m, field, status)};
    if (!hex) return std::nullopt;
    std::string cause;
    auto bytes{DecodeHex(*hex, cause)};
    if (bytes && (!size || bytes->size() == *size)) return bytes;
    if (bytes) cause = "invalid string length";
    status = InvalidField(m.Desc().full_name, field, *hex, cause);
    return std::nullopt;
}

std::optional<uint256> ReadConsensusHash(const Message& m, std::string_view field, Status& status)
{
    const auto bytes{ReadConsensusBytes(m, field, 32, status)};
    if (!bytes) return std::nullopt;
    uint256 hash;
    std::memcpy(hash.begin(), bytes->data(), 32);
    return hash;
}

/**
 * parse_sidechain_id (lib/server/mod.rs:82-90): a UInt32Value that must be set and fit a byte.
 * `message` is the name the enforcer puts in the error, which is not always the request's
 * (GetWithdrawalBundleProposals names GetTwoWayPegDataRequest, lib/server/validator/grpc.rs:418).
 */
std::optional<SidechainId> ReadSlot(const Message& m, std::string_view field, std::string_view message, Status& status)
{
    const Message* wrapper{m.GetMessage(field)};
    if (!wrapper) {
        status = MissingField(message, field);
        return std::nullopt;
    }
    const uint64_t value{wrapper->GetUInt("value")};
    if (value > 255) {
        status = InvalidField(message, field, util::ToString(value), "out of range integral type conversion attempted");
        return std::nullopt;
    }
    return static_cast<SidechainId>(value);
}

void SetHex(Message& m, std::string_view field, std::string hex) { m.Mutable(field).Mutable("hex").SetString("value", std::move(hex)); }
void SetU32(Message& m, std::string_view field, uint64_t value) { m.Mutable(field).SetUInt("value", value); }
void SetU64(Message& m, std::string_view field, uint64_t value) { m.Mutable(field).SetUInt("value", value); }
void SetStr(Message& m, std::string_view field, std::string value) { m.Mutable(field).SetString("value", std::move(value)); }

/** ConsensusHex of a Vec<u8>: its compact size, then the bytes. */
std::string ConsensusVecHex(std::span<const unsigned char> bytes)
{
    DataStream ds;
    ds << std::vector<unsigned char>(bytes.begin(), bytes.end());
    return HexStr(ds);
}

/**
 * SidechainDeclaration::try_parse (lib/types.rs:433-498): version 0, a title of the length its byte
 * says, then the description up to the two hashes (32 and 20 bytes); title and description UTF-8.
 */
struct Declaration {
    std::string title, description;
    std::span<const unsigned char> hash_id_1, hash_id_2;
};

/** UTF-8 validity, without UniValue's caveats on quotes and backslashes. */
bool IsUtf8(std::string_view s)
{
    size_t i{0};
    while (i < s.size()) {
        const unsigned char c{static_cast<unsigned char>(s[i])};
        size_t n;
        uint32_t cp;
        if (c < 0x80) { ++i; continue; }
        if ((c & 0xe0) == 0xc0) { n = 1; cp = c & 0x1f; }
        else if ((c & 0xf0) == 0xe0) { n = 2; cp = c & 0x0f; }
        else if ((c & 0xf8) == 0xf0) { n = 3; cp = c & 0x07; }
        else return false;
        if (s.size() - i <= n) return false;
        for (size_t k{1}; k <= n; ++k) {
            const unsigned char cc{static_cast<unsigned char>(s[i + k])};
            if ((cc & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3f);
        }
        if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
        i += n + 1;
    }
    return true;
}

std::optional<Declaration> ParseDeclaration(std::span<const unsigned char> d)
{
    if (d.size() < 2 || d[0] != 0) return std::nullopt;
    const size_t title_len{d[1]};
    if (d.size() < 2 + title_len) return std::nullopt;
    const std::span<const unsigned char> rest{d.subspan(2 + title_len)};
    if (rest.size() < 52) return std::nullopt;
    Declaration decl;
    decl.title.assign(d.begin() + 2, d.begin() + 2 + title_len);
    decl.description.assign(rest.begin(), rest.end() - 52);
    if (!IsUtf8(decl.title) || !IsUtf8(decl.description)) return std::nullopt;
    decl.hash_id_1 = rest.subspan(rest.size() - 52, 32);
    decl.hash_id_2 = rest.subspan(rest.size() - 20, 20);
    return decl;
}

void SetDeclaration(Message& parent, std::string_view field, const Declaration& decl)
{
    Message& v0{parent.Mutable(field).Mutable("v0")};
    SetStr(v0, "title", decl.title);
    SetStr(v0, "description", decl.description);
    SetHex(v0, "hash_id_1", HexStr(decl.hash_id_1));
    SetHex(v0, "hash_id_2", HexStr(decl.hash_id_2));
}

/** A declaration of a request (SidechainDeclaration -> V0), as TryFrom<SidechainDeclaration> (lib/proto.rs:498-566). */
std::optional<drivechain::Sidechain> ReadDeclaration(const Message& req, std::string_view field, SidechainId slot, Status& status)
{
    const Message* decl{req.GetMessage(field)};
    if (!decl) {
        status = MissingField(req.Desc().full_name, field);
        return std::nullopt;
    }
    const Message* v0{decl->GetMessage("v0")};
    if (!v0) {
        status = MissingField(decl->Desc().full_name, "sidechain_declaration");
        return std::nullopt;
    }
    const std::string v0_name{v0->Desc().full_name};
    if (!v0->GetMessage("title")) { status = MissingField(v0_name, "title"); return std::nullopt; }
    if (!v0->GetMessage("description")) { status = MissingField(v0_name, "description"); return std::nullopt; }
    const auto h1{ReadConsensusBytes(*v0, "hash_id_1", 32, status)};
    if (!h1) return std::nullopt;
    const auto h2{ReadRawHex(*v0, "hash_id_2", 20, status)};
    if (!h2) return std::nullopt;
    drivechain::Sidechain sidechain;
    sidechain.slot = slot;
    sidechain.version = 0;
    sidechain.title = v0->GetMessage("title")->GetString("value");
    sidechain.description = v0->GetMessage("description")->GetString("value");
    std::memcpy(sidechain.hash_id1.begin(), h1->data(), 32);
    std::memcpy(sidechain.hash_id2.begin(), h2->data(), 20);
    if (sidechain.title.size() > 255) {
        status = {Code::UNKNOWN, "the title of a sidechain proposal has at most 255 bytes"};
        return std::nullopt;
    }
    return sidechain;
}

/** The enforcer's Network (lib/proto.rs:627-637) for a network: eCash's main network and betanet run as Bitcoin's. */
int NetworkOf(ChainType chain)
{
    switch (chain) {
    case ChainType::MAIN: case ChainType::ECASH: case ChainType::ECASH_BETANET: return 2;
    case ChainType::REGTEST: return 3;
    case ChainType::SIGNET: return 4;
    case ChainType::TESTNET: case ChainType::ECASH_TESTNET: return 5;
    }
    return 1;
}

/** bitcoin::Network's Display, which the enforcer's WalletService.GetInfo reports. */
std::string NetworkName(ChainType chain)
{
    switch (chain) {
    case ChainType::MAIN: case ChainType::ECASH: case ChainType::ECASH_BETANET: return "bitcoin";
    case ChainType::REGTEST: return "regtest";
    case ChainType::SIGNET: return "signet";
    case ChainType::TESTNET: return "testnet";
    case ChainType::ECASH_TESTNET: return "testnet4";
    }
    return "bitcoin";
}

} // namespace

// ---------------------------------------------------------------------------
// What a block did, for one sidechain (BlockInfo, lib/types.rs:777-843, lib/proto.rs:903-930).

Message MakeHeaderInfo(const pb::Pool& pool, const CBlockIndex& block)
{
    // HeaderInfo (lib/validator/dbs/block_hashes.rs:373-398; lib/proto.rs:813-823): the work is the
    // block's own (header.work()), 32 bytes little endian; the timestamp its header time.
    Message m{*pool.FindMessage("cusf.mainchain.v1.BlockHeaderInfo")};
    SetHex(m, "block_hash", block.GetBlockHash().GetHex());
    SetHex(m, "prev_block_hash", block.pprev ? block.pprev->GetBlockHash().GetHex() : uint256{}.GetHex());
    m.SetUInt("height", static_cast<uint32_t>(block.nHeight));
    SetHex(m, "work", HexStr(ArithToUint256(GetBlockProof(block))));
    m.SetUInt("timestamp", block.nTime);
    return m;
}

std::optional<Message> MakeBlockInfo(const pb::Pool& pool, ChainstateManager& chainman, const CBlockIndex& block, uint32_t slot)
{
    AssertLockHeld(::cs_main);
    Message info{*pool.FindMessage("cusf.mainchain.v1.BlockInfo")};
    const auto& params{chainman.GetConsensus().drivechain};
    drivechain::Database& db{*chainman.m_blockman.m_drivechain_db};
    const uint256 hash{block.GetBlockHash()};
    // The genesis block is not connected (it is in the parameters): the enforcer records it empty.
    if (block.nHeight == 0) return info;
    drivechain::BlockEvents events;
    // Only a block that was connected has info (the enforcer's try_get_block_info is None otherwise).
    if (!db.ReadBlockEvents(hash, events)) return std::nullopt;
    // Below the activation height a block is plain Bitcoin history: empty (lib/validator/task/mod.rs:1115-1118).
    if (block.nHeight < params.activation_height) return info;

    // bmm_commitment: the M7 of the slot (BmmCommitments, handle_coinbase_message M7).
    drivechain::BmmAccepts accepts;
    if (!db.ReadBmmAccepts(hash, accepts)) {
        CBlock full;
        if (chainman.m_blockman.ReadBlock(full, block)) accepts = drivechain::BmmAcceptsOf(*full.vtx[0]);
    }
    for (const auto& [s, side_hash] : accepts) {
        if (s == slot) {
            SetHex(info, "bmm_commitment", HexStr(side_hash));
            break;
        }
    }

    // The events in the order connect_block makes them (lib/validator/task/mod.rs:1157-1280): the
    // coinbase's (the M3 proposals: Submitted), then the bundles that failed (handle_failed_m6ids, in
    // pending order), then the transactions' (deposits, and withdrawals: Succeeded), in block order.
    const auto add_bundle_event{[&](const uint256& m6id) -> Message& {
        Message& wb{info.Add("events").Mutable("withdrawal_bundle")};
        SetHex(wb, "m6id", HexStr(m6id));
        return wb.Mutable("event");
    }};
    for (const auto& [s, bundle] : events.proposed) {
        if (s == slot) add_bundle_event(bundle).Mutable("submitted");
    }
    const std::vector<drivechain::Deposit> deposits{db.ListBlockDeposits(slot, block.nHeight, hash)};
    // Failed: pending after the block before, not pending after this one, and not paid out by it.
    // (Not the block's closures: a bundle paid before, proposed again and failing, is remembered as
    // paid there, and the enforcer reports its failure.)
    std::set<uint256> paid;
    for (const drivechain::Deposit& d : deposits) {
        if (!d.bundle.IsNull()) paid.insert(d.bundle);
    }
    std::vector<uint256> before;
    if (block.pprev && block.pprev->nHeight >= std::max(1, params.activation_height)) {
        drivechain::BlockEvents prev;
        if (db.ReadBlockEvents(block.pprev->GetBlockHash(), prev)) {
            for (const auto& [s, bundles] : prev.pending) {
                if (s != slot) continue;
                for (const auto& [h, score] : bundles) before.push_back(h);
            }
        }
    }
    std::set<uint256> after;
    for (const auto& [s, bundles] : events.pending) {
        if (s != slot) continue;
        for (const auto& [h, score] : bundles) after.insert(h);
    }
    for (const uint256& h : before) {
        if (!after.contains(h) && !paid.contains(h)) add_bundle_event(h).Mutable("failed");
    }
    // Sequence numbers: the treasury UTXOs of the slot before (lib/validator/dbs/mod.rs:110-145), on this block's chain.
    uint64_t sequence{0};
    if (!deposits.empty()) {
        sequence = db.CountDeposits(slot, block.nHeight, [&](const uint256& h) {
            const CBlockIndex* p{chainman.m_blockman.LookupBlockIndex(h)};
            return p && block.GetAncestor(p->nHeight) == p;
        });
    }
    for (const drivechain::Deposit& d : deposits) {
        if (!d.bundle.IsNull()) {
            Message& succeeded{add_bundle_event(d.bundle).Mutable("succeeded")};
            SetU64(succeeded, "sequence_number", sequence);
            DataStream ds;
            ds << TX_WITH_WITNESS(*d.tx);
            SetHex(succeeded, "transaction", HexStr(ds));
        } else {
            Message& deposit{info.Add("events").Mutable("deposit")};
            SetU64(deposit, "sequence_number", sequence);
            Message& outpoint{deposit.Mutable("outpoint")};
            SetHex(outpoint, "txid", d.tx->GetHash().GetHex());
            SetU32(outpoint, "vout", d.burn_index);
            Message& output{deposit.Mutable("output")};
            SetHex(output, "address", HexStr(MakeUCharSpan(d.destination)));
            SetU64(output, "value_sats", static_cast<uint64_t>(d.amount));
        }
        ++sequence;
    }
    return info;
}

namespace {

/** The node's state, and what the handlers share. */
struct Ctx {
    const pb::Pool& pool;
    node::NodeContext& node;
    ServiceOptions options;
    Mutex generate_mutex;

    ChainstateManager& Chainman() const { return *Assert(node.chainman); }
    Message New(std::string_view name) const
    {
        const pb::MessageDesc* desc{pool.FindMessage(name)};
        if (!desc) throw std::logic_error("no message type " + std::string{name});
        return Message{*desc};
    }
    const Consensus::DrivechainParams& Params() const { return Chainman().GetConsensus().drivechain; }

    /** Run an RPC method in this process, as the RPC server would; wallet methods with the -grpcwallet wallet. */
    std::optional<UniValue> Rpc(const std::string& method, UniValue params, bool wallet, Status& status) const
    {
        JSONRPCRequest request;
        request.context = &node;
        request.strMethod = method;
        request.params = std::move(params);
        request.mode = JSONRPCRequest::EXECUTE;
        if (wallet && !options.wallet.empty()) request.URI = "/wallet/" + options.wallet;
        try {
            return tableRPC.execute(request);
        } catch (const UniValue& err) {
            const int code{err.find_value("code").isNum() ? err.find_value("code").getInt<int>() : RPC_MISC_ERROR};
            const std::string message{err.find_value("message").isStr() ? err.find_value("message").get_str() : "error"};
            switch (code) {
            case RPC_INVALID_PARAMETER: case RPC_INVALID_ADDRESS_OR_KEY: case RPC_TYPE_ERROR: case RPC_INVALID_PARAMS:
                status = {Code::INVALID_ARGUMENT, message};
                break;
            case RPC_WALLET_NOT_FOUND: case RPC_WALLET_NOT_SPECIFIED: case RPC_WALLET_UNLOCK_NEEDED: case RPC_WALLET_INSUFFICIENT_FUNDS:
            case RPC_WALLET_PASSPHRASE_INCORRECT: case RPC_WALLET_ALREADY_UNLOCKED:
                status = {Code::FAILED_PRECONDITION, message};
                break;
            case RPC_METHOD_NOT_FOUND:
                status = {Code::UNIMPLEMENTED, wallet ? "this node has no wallet support" : message};
                break;
            case RPC_IN_WARMUP:
                status = {Code::UNAVAILABLE, message};
                break;
            default:
                status = {Code::INTERNAL, message};
            }
            return std::nullopt;
        } catch (const std::exception& e) {
            status = {Code::INTERNAL, e.what()};
            return std::nullopt;
        }
    }
};

/** The writer's call ends with a reply (or the error). */
void Reply(const std::shared_ptr<Writer>& writer, const Message& reply)
{
    writer->Write(reply);
    writer->Finish({});
}

void Fail(const std::shared_ptr<Writer>& writer, const Status& status) { writer->Finish(status); }

const CBlockIndex* Lookup(Ctx& ctx, const uint256& hash) EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    return ctx.Chainman().m_blockman.LookupBlockIndex(hash);
}

/** The next block's height reaches the activation height (ensure_bip300_activation_height_reached, lib/server/block_producer.rs). */
std::optional<Status> BeforeActivation(Ctx& ctx, const std::string& impossible)
{
    const int activation{ctx.Params().activation_height};
    const int next{WITH_LOCK(::cs_main, return ctx.Chainman().ActiveChain().Height()) + 1};
    if (next < activation) return Status{Code::FAILED_PRECONDITION, strprintf("BIP300 activates at block height %d! %s before then", activation, impossible)};
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// The event streams (SubscribeEvents, CreateSidechainProposal, SubscribeHeaderSyncProgress, Health.Watch).

/** As the enforcer's events channel (lib/validator/mod.rs:436-446), a subscriber that does not keep up is dropped. */
constexpr size_t MAX_PENDING_EVENT_BYTES{256 << 20};

class Hub final : public CValidationInterface
{
public:
    explicit Hub(Ctx& ctx) : m_ctx{ctx}
    {
        m_ticker = std::thread(&util::TraceThread, "grpcticker", [this] { Tick(); });
    }
    ~Hub()
    {
        {
            LOCK(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();
        if (m_ticker.joinable()) m_ticker.join();
    }

    void AddEvents(SidechainId slot, const std::shared_ptr<Writer>& writer) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex)
    {
        LOCK(m_mutex);
        m_events.emplace_back(slot, writer);
    }

    struct ProposalWatch {
        drivechain::Sidechain sidechain;
        std::shared_ptr<Writer> writer;
        std::map<uint256, std::pair<uint32_t, COutPoint>> confirmations;
    };
    void AddProposal(ProposalWatch watch) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex)
    {
        LOCK(m_mutex);
        m_proposals.push_back(std::move(watch));
    }
    void AddHeaderSync(const std::shared_ptr<Writer>& writer) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex)
    {
        LOCK(m_mutex);
        m_header_sync.emplace_back(writer, -1);
    }
    void AddHealthWatch(const std::shared_ptr<Writer>& writer) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex)
    {
        LOCK(m_mutex);
        m_health.push_back(writer);
    }

    void BlockConnected(const kernel::ChainstateRole& role, const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex) override EXCLUSIVE_LOCKS_REQUIRED(!m_mutex)
    {
        // The background chainstate of an assumeutxo snapshot fills in history the enforcer never reports.
        if (role.historical) return;
        std::vector<std::pair<SidechainId, std::shared_ptr<Writer>>> events;
        std::vector<ProposalWatch*> proposals;
        LOCK(m_mutex);
        std::erase_if(m_events, [](const auto& e) { return e.second->Closed(); });
        std::erase_if(m_proposals, [](const ProposalWatch& w) { return w.writer->Closed(); });
        if (m_events.empty() && m_proposals.empty()) return;
        LOCK(::cs_main);
        // One event per slot, written to each of its subscribers (Event::into_proto, lib/proto.rs:953-978).
        std::map<SidechainId, Message> by_slot;
        for (const auto& [slot, writer] : m_events) {
            auto it{by_slot.find(slot)};
            if (it == by_slot.end()) {
                Message resp{m_ctx.New("cusf.mainchain.v1.SubscribeEventsResponse")};
                Message& connect{resp.Mutable("event").Mutable("connect_block")};
                connect.Mutable("header_info") = MakeHeaderInfo(m_ctx.pool, *pindex);
                if (auto info{MakeBlockInfo(m_ctx.pool, m_ctx.Chainman(), *pindex, slot)}) {
                    connect.Mutable("block_info") = std::move(*info);
                } else {
                    connect.Mutable("block_info");
                }
                it = by_slot.emplace(slot, std::move(resp)).first;
            }
            Send(*writer, it->second);
        }
        for (ProposalWatch& watch : m_proposals) ProposalEvent(watch, *block, *pindex);
    }

    void BlockDisconnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex) override EXCLUSIVE_LOCKS_REQUIRED(!m_mutex)
    {
        LOCK(m_mutex);
        std::erase_if(m_events, [](const auto& e) { return e.second->Closed(); });
        if (m_events.empty()) return;
        Message resp{m_ctx.New("cusf.mainchain.v1.SubscribeEventsResponse")};
        SetHex(resp.Mutable("event").Mutable("disconnect_block"), "block_hash", pindex->GetBlockHash().GetHex());
        for (const auto& [slot, writer] : m_events) Send(*writer, resp);
        (void)block;
    }

private:
    static void Send(Writer& writer, const Message& msg)
    {
        if (writer.Pending() > MAX_PENDING_EVENT_BYTES) {
            // EventsStreamError::Overflow (lib/validator/mod.rs:382-393): UNKNOWN.
            writer.Finish({Code::UNKNOWN, "Events stream closed due to overflow"});
            return;
        }
        writer.Write(msg);
    }

    /** stream_proposal_confirmations (lib/server/block_producer.rs). */
    void ProposalEvent(ProposalWatch& watch, const CBlock& block, const CBlockIndex& index) EXCLUSIVE_LOCKS_REQUIRED(::cs_main, m_mutex)
    {
        const uint256 hash{watch.sidechain.GetHash()};
        std::optional<uint32_t> vout;
        const auto& coinbase{*block.vtx[0]};
        for (uint32_t i{0}; i < coinbase.vout.size(); ++i) {
            const auto proposal{drivechain::ParseProposalScriptAnyDescription(coinbase.vout[i].scriptPubKey)};
            if (proposal && proposal->slot == watch.sidechain.slot && proposal->GetHash() == hash) {
                vout = i;
                break;
            }
        }
        // A new proposal of the block (the enforcer's NewSidechainProposal event): one proposed at this height.
        if (vout) {
            const drivechain::Proposal* p{m_ctx.Chainman().ActiveChainstate().m_scdb.GetProposal(watch.sidechain.slot, hash)};
            if (!p || p->height != index.nHeight) vout.reset();
        }
        Message resp{m_ctx.New("cusf.mainchain.v1.CreateSidechainProposalResponse")};
        const std::string prev{index.pprev ? index.pprev->GetBlockHash().GetHex() : uint256{}.GetHex()};
        std::pair<uint32_t, COutPoint> conf;
        if (vout) {
            conf = {1, COutPoint{coinbase.GetHash(), *vout}};
        } else if (const auto it{index.pprev ? watch.confirmations.find(index.pprev->GetBlockHash()) : watch.confirmations.end()}; it != watch.confirmations.end()) {
            conf = it->second;
        } else {
            Message& nc{resp.Mutable("not_confirmed")};
            SetHex(nc, "block_hash", index.GetBlockHash().GetHex());
            SetU32(nc, "height", static_cast<uint32_t>(index.nHeight));
            SetHex(nc, "prev_block_hash", prev);
            Send(*watch.writer, resp);
            return;
        }
        Message& c{resp.Mutable("confirmed")};
        SetHex(c, "block_hash", index.GetBlockHash().GetHex());
        SetU32(c, "confirmations", conf.first);
        SetU32(c, "height", static_cast<uint32_t>(index.nHeight));
        Message& outpoint{c.Mutable("outpoint")};
        SetHex(outpoint, "txid", conf.second.hash.GetHex());
        SetU32(outpoint, "vout", conf.second.n);
        SetHex(c, "prev_block_hash", prev);
        watch.confirmations[index.GetBlockHash()] = conf;
        Send(*watch.writer, resp);
    }

    /** Twice a second: the header sync streams (a watch of the best header's height while syncing). */
    void Tick() EXCLUSIVE_LOCKS_REQUIRED(!m_mutex)
    {
        WAIT_LOCK(m_mutex, lock);
        while (!m_stop) {
            m_cv.wait_for(lock, std::chrono::milliseconds{500});
            if (m_stop) break;
            std::erase_if(m_health, [](const auto& w) { return w->Closed(); });
            std::erase_if(m_header_sync, [](const auto& e) { return e.first->Closed(); });
            if (m_header_sync.empty()) continue;
            int height;
            bool syncing;
            {
                REVERSE_LOCK(lock, m_mutex);
                LOCK(::cs_main);
                ChainstateManager& chainman{m_ctx.Chainman()};
                syncing = chainman.IsInitialBlockDownload();
                height = chainman.m_best_header ? chainman.m_best_header->nHeight : 0;
            }
            for (auto& [writer, last] : m_header_sync) {
                if (!syncing) {
                    // The sync is over: the watch ends (WatchStream of a dropped sender).
                    writer->Finish({});
                    continue;
                }
                if (height == last) continue;
                last = height;
                Message resp{m_ctx.New("cusf.mainchain.v1.SubscribeHeaderSyncProgressResponse")};
                SetU32(resp, "current_height", static_cast<uint32_t>(height));
                writer->Write(resp);
            }
        }
    }

    Ctx& m_ctx;
    Mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop GUARDED_BY(m_mutex){false};
    std::vector<std::pair<SidechainId, std::shared_ptr<Writer>>> m_events GUARDED_BY(m_mutex);
    std::vector<ProposalWatch> m_proposals GUARDED_BY(m_mutex);
    std::vector<std::pair<std::shared_ptr<Writer>, int>> m_header_sync GUARDED_BY(m_mutex);
    std::vector<std::shared_ptr<Writer>> m_health GUARDED_BY(m_mutex);
    std::thread m_ticker;
};

class ServicesImpl final : public Services
{
public:
    ServicesImpl(Server& server, const pb::Pool& pool, node::NodeContext& node, const ServiceOptions& options)
        : m_ctx{pool, node, options}
    {
        m_hub = std::make_shared<Hub>(m_ctx);
        if (node.validation_signals) node.validation_signals->RegisterSharedValidationInterface(m_hub);
        RegisterValidator(server);
        RegisterCrypto(server);
        RegisterMining(server);
        RegisterBlockProducer(server);
        if (options.wallet_service) RegisterWallet(server);
        RegisterHealth(server);
        RegisterReflection(server);
    }
    ~ServicesImpl() override
    {
        if (m_ctx.node.validation_signals) {
            m_ctx.node.validation_signals->UnregisterSharedValidationInterface(m_hub);
            m_ctx.node.validation_signals->SyncWithValidationInterfaceQueue();
        }
    }

private:
    using Fn = std::function<void(const Message&, const std::shared_ptr<Writer>&)>;
    void Add(Server& server, const std::string& service, const std::string& method, Fn fn)
    {
        if (!server.Register(service, method, std::move(fn))) throw std::logic_error("no method " + service + "/" + method);
    }

    void RegisterValidator(Server& server);
    void RegisterCrypto(Server& server);
    void RegisterMining(Server& server);
    void RegisterBlockProducer(Server& server);
    void RegisterWallet(Server& server);
    void RegisterHealth(Server& server);
    void RegisterReflection(Server& server);
    void ProposeBundle(const Message& req, const std::shared_ptr<Writer>& w, const std::string& reply);

    Ctx m_ctx;
    std::shared_ptr<Hub> m_hub;
    std::vector<std::string> m_services;
};

// ---------------------------------------------------------------------------
// ValidatorService (lib/server/validator/grpc.rs at 17eb2ce)

void ServicesImpl::RegisterValidator(Server& server)
{
    const std::string svc{"cusf.mainchain.v1.ValidatorService"};
    m_services.push_back(svc);
    Ctx& ctx{m_ctx};

    // get_block_header_info (grpc.rs:45-72): the block and up to max_ancestors ancestors, newest first; empty if unknown.
    Add(server, svc, "GetBlockHeaderInfo", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        Status st;
        const auto hash{ReadReverseHash(req, "block_hash", st)};
        if (!hash) return Fail(w, st);
        const uint64_t max_ancestors{req.Has("max_ancestors") ? req.GetUInt("max_ancestors") : 0};
        Message resp{ctx.New("cusf.mainchain.v1.GetBlockHeaderInfoResponse")};
        LOCK(::cs_main);
        for (const CBlockIndex* p{Lookup(ctx, *hash)}; p; p = p->pprev) {
            resp.Add("header_infos") = MakeHeaderInfo(ctx.pool, *p);
            if (resp.Count("header_infos") > max_ancestors) break;
        }
        Reply(w, resp);
    });

    // get_block_info (grpc.rs:74-109; lib/validator/mod.rs:642-675): ancestors while they have info.
    Add(server, svc, "GetBlockInfo", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        Status st;
        const auto hash{ReadReverseHash(req, "block_hash", st)};
        if (!hash) return Fail(w, st);
        const auto slot{ReadSlot(req, "sidechain_id", req.Desc().full_name, st)};
        if (!slot) return Fail(w, st);
        const uint64_t max_ancestors{req.Has("max_ancestors") ? req.GetUInt("max_ancestors") : 0};
        Message resp{ctx.New("cusf.mainchain.v1.GetBlockInfoResponse")};
        LOCK(::cs_main);
        for (const CBlockIndex* p{Lookup(ctx, *hash)}; p; p = p->pprev) {
            auto info{MakeBlockInfo(ctx.pool, ctx.Chainman(), *p, *slot)};
            if (!info) break;
            Message& item{resp.Add("infos")};
            item.Mutable("header_info") = MakeHeaderInfo(ctx.pool, *p);
            item.Mutable("block_info") = std::move(*info);
            if (resp.Count("infos") > max_ancestors) break;
        }
        Reply(w, resp);
    });

    // get_bmm_h_star_commitment (grpc.rs:111-167; lib/validator/mod.rs:764-790).
    Add(server, svc, "GetBmmHStarCommitment", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        Status st;
        const auto hash{ReadReverseHash(req, "block_hash", st)};
        if (!hash) return Fail(w, st);
        const auto slot{ReadSlot(req, "sidechain_id", req.Desc().full_name, st)};
        if (!slot) return Fail(w, st);
        const uint64_t max_ancestors{req.Has("max_ancestors") ? req.GetUInt("max_ancestors") : 0};
        Message resp{ctx.New("cusf.mainchain.v1.GetBmmHStarCommitmentResponse")};
        LOCK(::cs_main);
        std::vector<std::optional<Message>> found;
        for (const CBlockIndex* p{Lookup(ctx, *hash)}; p && found.size() <= max_ancestors; p = p->pprev) {
            auto info{MakeBlockInfo(ctx.pool, ctx.Chainman(), *p, *slot)};
            if (!info) break;
            const Message* c{info->GetMessage("bmm_commitment")};
            found.push_back(c ? std::optional<Message>{*c} : std::nullopt);
        }
        if (found.empty()) {
            SetHex(resp.Mutable("block_not_found"), "block_hash", hash->GetHex());
        } else {
            Message& commitment{resp.Mutable("commitment")};
            if (found[0]) commitment.Mutable("commitment") = *found[0];
            for (size_t i{1}; i < found.size(); ++i) {
                Message& opt{commitment.Add("ancestor_commitments")};
                if (found[i]) opt.Mutable("commitment") = *found[i];
            }
        }
        Reply(w, resp);
    });

    // get_chain_info (grpc.rs:169-200).
    Add(server, svc, "GetChainInfo", [&ctx](const Message&, const std::shared_ptr<Writer>& w) {
        const auto& p{ctx.Params()};
        Message resp{ctx.New("cusf.mainchain.v1.GetChainInfoResponse")};
        resp.SetUInt("network", NetworkOf(ctx.Chainman().GetParams().GetChainType()));
        Message& c{resp.Mutable("bip300_constants")};
        c.SetUInt("withdrawal_bundle_max_age", p.bundle_max_age);
        c.SetUInt("withdrawal_bundle_inclusion_threshold", p.bundle_threshold);
        c.SetUInt("used_sidechain_slot_proposal_max_age", p.used_slot_max_age);
        c.SetUInt("used_sidechain_slot_activation_threshold", p.used_slot_threshold);
        c.SetUInt("unused_sidechain_slot_proposal_max_age", p.unused_slot_max_age);
        c.SetUInt("unused_sidechain_slot_activation_threshold", p.unused_slot_threshold);
        c.SetUInt("activation_height", std::max(0, p.activation_height));
        Reply(w, resp);
    });

    // get_chain_tip (grpc.rs:202-221).
    Add(server, svc, "GetChainTip", [&ctx](const Message&, const std::shared_ptr<Writer>& w) {
        Message resp{ctx.New("cusf.mainchain.v1.GetChainTipResponse")};
        LOCK(::cs_main);
        const CBlockIndex* tip{ctx.Chainman().ActiveChain().Tip()};
        if (!tip) return Fail(w, {Code::UNAVAILABLE, "Validator is not synced"});
        resp.Mutable("block_header_info") = MakeHeaderInfo(ctx.pool, *tip);
        Reply(w, resp);
    });

    // get_coinbase_psbt (grpc.rs:223-268; lib/proto.rs:639-811): the messages as zero value outputs of a transaction.
    Add(server, svc, "GetCoinbasePSBT", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        Status st;
        CMutableTransaction tx;
        tx.version = 2;
        tx.nLockTime = 0;
        const auto slot_of{[&](const Message& m, std::optional<SidechainId>& out) {
            const Message* wrapper{m.GetMessage("sidechain_number")};
            if (!wrapper) { st = MissingField(m.Desc().full_name, "sidechain_number"); return false; }
            const uint64_t v{wrapper->GetUInt("value")};
            if (v > 255) { st = InvalidField(m.Desc().full_name, "sidechain_number", util::ToString(v), "out of range integral type conversion attempted"); return false; }
            out = static_cast<SidechainId>(v);
            return true;
        }};
        for (size_t i{0}; i < req.Count("propose_sidechains"); ++i) {
            const Message& m{req.At("propose_sidechains", i)};
            std::optional<SidechainId> slot;
            if (!slot_of(m, slot)) return Fail(w, st);
            const auto data{ReadConsensusBytes(m, "data", std::nullopt, st)};
            if (!data) return Fail(w, st);
            drivechain::Sidechain sidechain;
            sidechain.slot = *slot;
            sidechain.version = drivechain::Sidechain::RAW_DESCRIPTION;
            sidechain.description.assign(data->begin(), data->end());
            tx.vout.emplace_back(0, drivechain::ProposalScript(sidechain));
        }
        for (size_t i{0}; i < req.Count("ack_sidechains"); ++i) {
            const Message& m{req.At("ack_sidechains", i)};
            std::optional<SidechainId> slot;
            if (!slot_of(m, slot)) return Fail(w, st);
            const auto hash{ReadConsensusHash(m, "data_hash", st)};
            if (!hash) return Fail(w, st);
            tx.vout.emplace_back(0, drivechain::AckScript(*slot, *hash));
        }
        for (size_t i{0}; i < req.Count("propose_bundles"); ++i) {
            const Message& m{req.At("propose_bundles", i)};
            std::optional<SidechainId> slot;
            if (!slot_of(m, slot)) return Fail(w, st);
            const auto hash{ReadReverseHash(m, "bundle_txid", st)};
            if (!hash) return Fail(w, st);
            tx.vout.emplace_back(0, drivechain::BundleScript(*slot, *hash));
        }
        const Message* ack{req.GetMessage("ack_bundles")};
        if (!ack) return Fail(w, MissingField(req.Desc().full_name, "ack_bundles"));
        drivechain::VoteMessage votes;
        const pb::FieldDesc* which{ack->WhichOneof("ack_bundles")};
        if (!which) return Fail(w, MissingField(ack->Desc().full_name, "ack_bundles"));
        if (which->name == "repeat_previous") {
            votes.form = drivechain::VoteForm::REPEAT_PREVIOUS;
        } else if (which->name == "leading_by_50") {
            votes.form = drivechain::VoteForm::LEADING_BY_50;
        } else {
            const Message& up{*ack->GetMessage("upvotes")};
            bool two_bytes{false};
            for (size_t i{0}; i < up.Count("upvotes"); ++i) {
                const uint64_t v{up.UIntAt("upvotes", i)};
                if (v > 0xffff) return Fail(w, {Code::INVALID_ARGUMENT, strprintf("Invalid value in repeated field `upvotes` of message `%s`: `%u`", up.Desc().full_name, v)});
                if (v > 0xff) two_bytes = true;
                votes.votes.push_back(static_cast<uint16_t>(v));
            }
            votes.form = two_bytes ? drivechain::VoteForm::TWO_BYTES : drivechain::VoteForm::ONE_BYTE;
        }
        tx.vout.emplace_back(0, drivechain::VoteScript(votes));
        Message resp{ctx.New("cusf.mainchain.v1.GetCoinbasePSBTResponse")};
        DataStream ds;
        ds << TX_NO_WITNESS(tx);
        SetHex(resp, "psbt", HexStr(ds));
        Reply(w, resp);
    });

    // get_ctip (grpc.rs:270-305): the treasury UTXO and its sequence number; nothing if the slot has none.
    Add(server, svc, "GetCtip", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        Status st;
        const auto slot{ReadSlot(req, "sidechain_number", req.Desc().full_name, st)};
        if (!slot) return Fail(w, st);
        Message resp{ctx.New("cusf.mainchain.v1.GetCtipResponse")};
        LOCK(::cs_main);
        const drivechain::Slot* s{ctx.Chainman().ActiveChainstate().m_scdb.GetSlot(*slot)};
        if (s && s->has_ctip) {
            const CChain& chain{ctx.Chainman().ActiveChain()};
            const uint64_t count{ctx.Chainman().m_blockman.m_drivechain_db->CountDeposits(*slot, chain.Height() + 1, [&](const uint256& h) {
                const CBlockIndex* p{Lookup(ctx, h)};
                return p && chain.Contains(*p);
            })};
            Message& ctip{resp.Mutable("ctip")};
            SetHex(ctip, "txid", s->ctip.outpoint.hash.GetHex());
            ctip.SetUInt("vout", s->ctip.outpoint.n);
            ctip.SetUInt("value", static_cast<uint64_t>(s->ctip.amount));
            ctip.SetUInt("sequence_number", count == 0 ? 0 : count - 1);
        }
        Reply(w, resp);
    });

    // get_seen_bmm_requests (enforcer master ceb608a, lib/server/validator/grpc.rs:310-366): the M8 in the
    // mempool built on a block, richest bid first.
    Add(server, svc, "GetSeenBmmRequests", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        Status st;
        const auto prev{ReadReverseHash(req, "prev_block_hash", st)};
        if (!prev) return Fail(w, st);
        std::optional<SidechainId> wanted;
        if (const Message* sn{req.GetMessage("sidechain_number")}) {
            const uint64_t v{sn->GetUInt("value")};
            if (v > 255) return Fail(w, InvalidField(req.Desc().full_name, "sidechain_number", util::ToString(v), "out of range integral type conversion attempted"));
            wanted = static_cast<SidechainId>(v);
        }
        struct Seen {
            SidechainId slot;
            uint256 txid, hash;
            CAmount fee;
        };
        std::vector<Seen> seen;
        if (ctx.node.mempool) {
            CTxMemPool& pool{*ctx.node.mempool};
            LOCK(pool.cs);
            for (const auto& entry : pool.entryAll()) {
                for (const drivechain::BmmRequest& r : drivechain::GetBmmRequests(entry.get().GetTx())) {
                    if (r.prev_main_block_hash != *prev || (wanted && r.slot != *wanted)) continue;
                    seen.push_back({r.slot, entry.get().GetTx().GetHash().ToUint256(), r.side_block_hash, entry.get().GetFee()});
                }
            }
        }
        std::stable_sort(seen.begin(), seen.end(), [](const Seen& a, const Seen& b) { return a.fee > b.fee; });
        Message resp{ctx.New("cusf.mainchain.v1.GetSeenBmmRequestsResponse")};
        for (const Seen& s : seen) {
            Message& r{resp.Add("requests")};
            r.SetUInt("sidechain_number", s.slot);
            SetHex(r, "txid", s.txid.GetHex());
            SetHex(r, "critical_hash", HexStr(s.hash));
            r.SetUInt("bid_sats", static_cast<uint64_t>(s.fee));
        }
        Reply(w, resp);
    });

    // get_sidechain_proposals (grpc.rs:307-355): the proposals collecting acks, by (slot, hash) as the
    // enforcer's database keeps them (SidechainProposalId in bincode: the slot byte, then the hash bytes).
    Add(server, svc, "GetSidechainProposals", [&ctx](const Message&, const std::shared_ptr<Writer>& w) {
        Message resp{ctx.New("cusf.mainchain.v1.GetSidechainProposalsResponse")};
        LOCK(::cs_main);
        const int tip_height{ctx.Chainman().ActiveChain().Height()};
        std::vector<drivechain::Proposal> proposals{ctx.Chainman().ActiveChainstate().m_scdb.GetProposals()};
        std::sort(proposals.begin(), proposals.end(), [](const drivechain::Proposal& a, const drivechain::Proposal& b) {
            if (a.sidechain.slot != b.sidechain.slot) return a.sidechain.slot < b.sidechain.slot;
            return std::memcmp(a.hash.begin(), b.hash.begin(), 32) < 0;
        });
        for (const drivechain::Proposal& p : proposals) {
            Message& item{resp.Add("sidechain_proposals")};
            const std::vector<unsigned char> description{p.sidechain.Description()};
            SetU32(item, "sidechain_number", p.sidechain.slot);
            SetHex(item, "description", ConsensusVecHex(description));
            if (const auto decl{ParseDeclaration(description)}) SetDeclaration(item, "declaration", *decl);
            SetHex(item, "description_sha256d_hash", p.hash.GetHex());
            SetU32(item, "vote_count", p.acks);
            SetU32(item, "proposal_height", static_cast<uint32_t>(p.height));
            SetU32(item, "proposal_age", static_cast<uint32_t>(std::max(0, tip_height - p.height)));
        }
        Reply(w, resp);
    });

    // get_sidechains (grpc.rs:357-368; lib/proto.rs:986-1005): the active sidechains, by slot.
    Add(server, svc, "GetSidechains", [&ctx](const Message&, const std::shared_ptr<Writer>& w) {
        Message resp{ctx.New("cusf.mainchain.v1.GetSidechainsResponse")};
        LOCK(::cs_main);
        const CChain& chain{ctx.Chainman().ActiveChain()};
        for (const auto& [id, slot] : ctx.Chainman().ActiveChainstate().m_scdb.GetSlots()) {
            Message& item{resp.Add("sidechains")};
            const std::vector<unsigned char> description{slot.sidechain.Description()};
            SetU32(item, "sidechain_number", id);
            SetHex(item, "description", ConsensusVecHex(description));
            // The proposal as it was when it activated (the enforcer keeps its status): recorded by the
            // drivechain database since this version; left out for an older activation.
            if (const CBlockIndex* at{chain[slot.activation_height]}) {
                if (const auto activation{ctx.Chainman().m_blockman.m_drivechain_db->ReadActivation(id, at->GetBlockHash())}) {
                    SetU32(item, "vote_count", activation->acks);
                    SetU32(item, "proposal_height", static_cast<uint32_t>(activation->proposal_height));
                }
            }
            SetU32(item, "activation_height", static_cast<uint32_t>(slot.activation_height));
            if (const auto decl{ParseDeclaration(description)}) SetDeclaration(item, "declaration", *decl);
        }
        Reply(w, resp);
    });

    // get_two_way_peg_data (grpc.rs:370-408; lib/validator/dbs/block_hashes.rs:502-541): the blocks after
    // start_block_hash (from genesis if unset) up to end_block_hash, oldest first, but those with
    // nothing for the sidechain; the start block itself only when it is the end block.
    Add(server, svc, "GetTwoWayPegData", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        Status st;
        const auto slot{ReadSlot(req, "sidechain_id", req.Desc().full_name, st)};
        if (!slot) return Fail(w, st);
        std::optional<uint256> start;
        if (req.GetMessage("start_block_hash")) {
            start = ReadReverseHash(req, "start_block_hash", st);
            if (!start) return Fail(w, st);
        }
        const auto end{ReadReverseHash(req, "end_block_hash", st)};
        if (!end) return Fail(w, st);
        Message resp{ctx.New("cusf.mainchain.v1.GetTwoWayPegDataResponse")};
        LOCK(::cs_main);
        const CBlockIndex* pend{Lookup(ctx, *end)};
        const auto info_of{[&](const CBlockIndex* p) { return MakeBlockInfo(ctx.pool, ctx.Chainman(), *p, *slot); }};
        std::optional<Message> end_info;
        if (pend) end_info = info_of(pend);
        // Errors through internal_err: INTERNAL, with the error's text (block hashes as the enforcer prints them).
        if (!end_info) return Fail(w, {Code::INTERNAL, strprintf("End block `%s` not found", end->GetHex())});
        int first{0};
        if (start && *start != *end) {
            const CBlockIndex* pstart{Lookup(ctx, *start)};
            if (!pstart || pend->GetAncestor(pstart->nHeight) != pstart || pstart == pend) {
                return Fail(w, {Code::INTERNAL, strprintf("Start block `%s` is not an ancestor of end block `%s`", start->GetHex(), end->GetHex())});
            }
            first = pstart->nHeight + 1;
        } else if (start) {
            first = pend->nHeight;
        }
        // Blocks below the activation height have nothing for any sidechain: not read at all.
        const int skip_below{std::max(first, ctx.Params().activation_height)};
        const auto push{[&](const CBlockIndex* p, Message info) {
            if (!info.GetMessage("bmm_commitment") && info.Count("events") == 0) return;
            Message& item{resp.Add("blocks")};
            item.Mutable("block_header_info") = MakeHeaderInfo(ctx.pool, *p);
            item.Mutable("block_info") = std::move(info);
        }};
        for (int h{skip_below}; h < pend->nHeight; ++h) {
            const CBlockIndex* p{pend->GetAncestor(h)};
            auto info{info_of(p)};
            if (!info) {
                const CBlockIndex* child{pend->GetAncestor(h + 1)};
                return Fail(w, {Code::INTERNAL, strprintf("Previous block `%s` not found for block `%s`", child->GetBlockHash().GetHex(), p->GetBlockHash().GetHex())});
            }
            push(p, std::move(*info));
        }
        if (pend->nHeight >= first) push(pend, std::move(*end_info));
        Reply(w, resp);
    });

    // get_withdrawal_bundle_proposals (grpc.rs:410-438): the pending bundles of an active slot, oldest first.
    Add(server, svc, "GetWithdrawalBundleProposals", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        Status st;
        // The enforcer names GetTwoWayPegDataRequest in this error (grpc.rs:418).
        const auto slot{ReadSlot(req, "sidechain_id", "cusf.mainchain.v1.GetTwoWayPegDataRequest", st)};
        if (!slot) return Fail(w, st);
        Message resp{ctx.New("cusf.mainchain.v1.GetWithdrawalBundleProposalsResponse")};
        LOCK(::cs_main);
        const drivechain::Slot* s{ctx.Chainman().ActiveChainstate().m_scdb.GetSlot(*slot)};
        // The enforcer's pending_m6ids().get() of a slot never active: a database error, INTERNAL.
        if (!s) return Fail(w, {Code::INTERNAL, strprintf("Missing value from database `active_sidechain_number_to_pending_m6ids` for sidechain %u", *slot)});
        for (const drivechain::Bundle& b : s->bundles) {
            Message& item{resp.Add("proposals")};
            SetHex(item, "m6id", HexStr(b.hash));
            SetU32(item, "vote_count", std::min<uint32_t>(b.score, 0xffff));
            SetU32(item, "proposal_height", static_cast<uint32_t>(b.height));
        }
        Reply(w, resp);
    });

    // subscribe_events (grpc.rs:440-461): a stream of the blocks connected and disconnected from now on.
    // Nothing is sent on subscribing: the clients' handshake (GetChainTip, SubscribeEvents, GetChainTip)
    // expects that. The events queued before are delivered first, so that none is seen twice.
    Add(server, svc, "SubscribeEvents", [&ctx, hub = m_hub](const Message& req, const std::shared_ptr<Writer>& w) {
        Status st;
        const auto slot{ReadSlot(req, "sidechain_id", req.Desc().full_name, st)};
        if (!slot) return Fail(w, st);
        if (ctx.node.validation_signals) ctx.node.validation_signals->SyncWithValidationInterfaceQueue();
        hub->AddEvents(*slot, w);
    });

    // subscribe_header_sync_progress (grpc.rs:463-475): while the node syncs, the height of its best header.
    Add(server, svc, "SubscribeHeaderSyncProgress", [&ctx, hub = m_hub](const Message&, const std::shared_ptr<Writer>& w) {
        int height;
        {
            LOCK(::cs_main);
            if (!ctx.Chainman().IsInitialBlockDownload()) return Fail(w, {Code::UNAVAILABLE, "No header sync in progress"});
            height = ctx.Chainman().m_best_header ? ctx.Chainman().m_best_header->nHeight : 0;
        }
        Message resp{ctx.New("cusf.mainchain.v1.SubscribeHeaderSyncProgressResponse")};
        SetU32(resp, "current_height", static_cast<uint32_t>(height));
        w->Write(resp);
        hub->AddHeaderSync(w);
    });

    // stop (grpc.rs:477-491).
    Add(server, svc, "Stop", [&ctx](const Message&, const std::shared_ptr<Writer>& w) {
        if (!ctx.options.allow_stop) return Fail(w, {Code::PERMISSION_DENIED, "Stop is disabled (-grpcallowstop=0)"});
        if (ctx.node.shutdown_signal && static_cast<bool>(*ctx.node.shutdown_signal)) return Fail(w, {Code::UNAVAILABLE, "Validator is already shutting down"});
        LogInfo("gRPC: received stop request, shutting down");
        Reply(w, ctx.New("cusf.mainchain.v1.StopResponse"));
        if (ctx.node.shutdown_request) ctx.node.shutdown_request();
    });
}

// ---------------------------------------------------------------------------
// CryptoService (lib/server/crypto.rs)

secp256k1_context* Secp()
{
    static secp256k1_context* const context{secp256k1_context_create(SECP256K1_CONTEXT_NONE)};
    return context;
}

void ServicesImpl::RegisterCrypto(Server& server)
{
    const std::string svc{"cusf.crypto.v1.CryptoService"};
    m_services.push_back(svc);
    Ctx& ctx{m_ctx};

    Add(server, svc, "Ripemd160", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        Status st;
        const auto msg{ReadRawHex(req, "msg", std::nullopt, st)};
        if (!msg) return Fail(w, st);
        unsigned char digest[CRIPEMD160::OUTPUT_SIZE];
        CRIPEMD160().Write(msg->data(), msg->size()).Finalize(digest);
        Message resp{ctx.New("cusf.crypto.v1.Ripemd160Response")};
        SetHex(resp, "digest", HexStr(digest));
        Reply(w, resp);
    });

    Add(server, svc, "HmacSha512", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        Status st;
        const auto key{ReadRawHex(req, "key", std::nullopt, st)};
        if (!key) return Fail(w, st);
        const auto msg{ReadRawHex(req, "msg", std::nullopt, st)};
        if (!msg) return Fail(w, st);
        unsigned char out[CHMAC_SHA512::OUTPUT_SIZE];
        CHMAC_SHA512(key->data(), key->size()).Write(msg->data(), msg->size()).Finalize(out);
        Message resp{ctx.New("cusf.crypto.v1.HmacSha512Response")};
        SetHex(resp, "hmac", HexStr(out));
        Reply(w, resp);
    });

    Add(server, svc, "Secp256k1SecretKeyToPublicKey", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        Status st;
        const auto key{ReadConsensusBytes(req, "secret_key", 32, st)};
        if (!key) return Fail(w, st);
        secp256k1_pubkey pubkey;
        if (!secp256k1_ec_pubkey_create(Secp(), &pubkey, key->data())) {
            return Fail(w, InvalidField(req.Desc().full_name, "secret_key", "", "malformed or out-of-range secret key"));
        }
        unsigned char out[33];
        size_t len{sizeof(out)};
        secp256k1_ec_pubkey_serialize(Secp(), out, &len, &pubkey, SECP256K1_EC_COMPRESSED);
        Message resp{ctx.New("cusf.crypto.v1.Secp256k1SecretKeyToPublicKeyResponse")};
        SetHex(resp, "public_key", HexStr(out));
        Reply(w, resp);
    });

    // The message is hashed with SHA-256; RFC 6979 nonces without extra entropy and no grinding, as
    // rust-secp256k1's sign_ecdsa; the 64 byte compact form.
    Add(server, svc, "Secp256k1Sign", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        Status st;
        const auto msg{ReadRawHex(req, "message", std::nullopt, st)};
        if (!msg) return Fail(w, st);
        const auto key{ReadConsensusBytes(req, "secret_key", 32, st)};
        if (!key) return Fail(w, st);
        unsigned char digest[CSHA256::OUTPUT_SIZE];
        CSHA256().Write(msg->data(), msg->size()).Finalize(digest);
        if (!secp256k1_ec_seckey_verify(Secp(), key->data())) return Fail(w, InvalidField(req.Desc().full_name, "secret_key", "", "malformed or out-of-range secret key"));
        secp256k1_ecdsa_signature sig;
        if (!secp256k1_ecdsa_sign(Secp(), &sig, digest, key->data(), secp256k1_nonce_function_rfc6979, nullptr)) return Fail(w, {Code::INTERNAL, "signing failed"});
        unsigned char compact[64];
        secp256k1_ecdsa_signature_serialize_compact(Secp(), compact, &sig);
        Message resp{ctx.New("cusf.crypto.v1.Secp256k1SignResponse")};
        SetHex(resp, "signature", HexStr(compact));
        Reply(w, resp);
    });

    Add(server, svc, "Secp256k1Verify", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        Status st;
        const auto msg{ReadRawHex(req, "message", std::nullopt, st)};
        if (!msg) return Fail(w, st);
        const auto sig_bytes{ReadRawHex(req, "signature", std::nullopt, st)};
        if (!sig_bytes) return Fail(w, st);
        if (sig_bytes->size() != 64) {
            return Fail(w, InvalidField(req.Desc().full_name, "signature", util::ToString(sig_bytes->size()), "invalid signature length, must be 64"));
        }
        secp256k1_ecdsa_signature sig;
        if (!secp256k1_ecdsa_signature_parse_compact(Secp(), &sig, sig_bytes->data())) return Fail(w, InvalidField(req.Desc().full_name, "signature", "", "malformed signature"));
        const auto pub_bytes{ReadConsensusBytes(req, "public_key", 33, st)};
        if (!pub_bytes) return Fail(w, st);
        secp256k1_pubkey pubkey;
        if (!secp256k1_ec_pubkey_parse(Secp(), &pubkey, pub_bytes->data(), pub_bytes->size())) return Fail(w, InvalidField(req.Desc().full_name, "public_key", "", "malformed public key"));
        unsigned char digest[CSHA256::OUTPUT_SIZE];
        CSHA256().Write(msg->data(), msg->size()).Finalize(digest);
        Message resp{ctx.New("cusf.crypto.v1.Secp256k1VerifyResponse")};
        // A high-S signature does not verify (libsecp256k1, as rust-secp256k1).
        resp.SetBool("valid", secp256k1_ecdsa_verify(Secp(), &sig, digest, &pubkey) == 1);
        Reply(w, resp);
    });
}

// ---------------------------------------------------------------------------
// MiningService (lib/server/mining.rs)

std::optional<Status> Generate(Ctx& ctx, uint32_t blocks, const std::string& address, const std::function<void(const uint256&)>& each)
{
    UniValue params(UniValue::VARR);
    params.push_back(1);
    params.push_back(address);
    for (uint32_t i{0}; i < blocks; ++i) {
        Status st;
        const auto result{ctx.Rpc("generatetoaddress", params, /*wallet=*/false, st)};
        if (!result) return st;
        if (!result->isArray() || result->size() != 1) return Status{Code::INTERNAL, "no block was mined"};
        each(uint256::FromHex((*result)[0].get_str()).value_or(uint256{}));
    }
    return std::nullopt;
}

void ServicesImpl::RegisterMining(Server& server)
{
    const std::string svc{"cusf.mainchain.v1.MiningService"};
    m_services.push_back(svc);
    Ctx& ctx{m_ctx};

    Add(server, svc, "GenerateToAddress", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        const uint64_t blocks{req.GetMessage("blocks") ? req.GetMessage("blocks")->GetUInt("value") : 1};
        if (blocks == 0) return Fail(w, {Code::INVALID_ARGUMENT, "must provide a positive number of blocks"});
        const std::string& address{req.GetString("address")};
        if (address.empty()) return Fail(w, MissingField(req.Desc().full_name, "address"));
        if (!IsValidDestination(DecodeDestination(address))) return Fail(w, InvalidField(req.Desc().full_name, "address", address, "invalid address for this network"));
        // One generation at a time; another caller is refused rather than queued (mining.rs).
        TRY_LOCK(ctx.generate_mutex, lock);
        if (!lock) return Fail(w, {Code::RESOURCE_EXHAUSTED, "block generation is already in progress"});
        Message resp{ctx.New("cusf.mainchain.v1.GenerateToAddressResponse")};
        if (const auto err{Generate(ctx, static_cast<uint32_t>(blocks), address, [&](const uint256& h) { resp.Add("block_hashes").Mutable("hex").SetString("value", h.GetHex()); })}) return Fail(w, *err);
        Reply(w, resp);
    });
}

// ---------------------------------------------------------------------------
// BlockProducerService (lib/server/block_producer.rs), on this node's block template policy

void ServicesImpl::RegisterBlockProducer(Server& server)
{
    const std::string svc{"cusf.mainchain.v1.BlockProducerService"};
    m_services.push_back(svc);
    Ctx& ctx{m_ctx};

    const auto propose{[&ctx](const Message& req, Status& st) -> std::optional<drivechain::Sidechain> {
        if (const auto err{BeforeActivation(ctx, "a sidechain proposal cannot confirm")}) { st = *err; return std::nullopt; }
        const auto slot{ReadSlot(req, "sidechain_id", req.Desc().full_name, st)};
        if (!slot) return std::nullopt;
        auto sidechain{ReadDeclaration(req, "declaration", *slot, st)};
        if (!sidechain) return std::nullopt;
        drivechain::MinerState& miner{ctx.Chainman().m_drivechain_miner};
        for (const drivechain::Sidechain& s : miner.GetProposals()) {
            if (s.slot == sidechain->slot && s.GetHash() == sidechain->GetHash()) {
                st = {Code::ALREADY_EXISTS, "Sidechain proposal already exists"};
                return std::nullopt;
            }
        }
        miner.AddProposal(*sidechain);
        return sidechain;
    }};

    Add(server, svc, "SubmitSidechainProposal", [&ctx, propose](const Message& req, const std::shared_ptr<Writer>& w) {
        Status st;
        if (!propose(req, st)) return Fail(w, st);
        Reply(w, ctx.New("cusf.mainchain.v1.SubmitSidechainProposalResponse"));
    });

    Add(server, svc, "CreateSidechainProposal", [&ctx, propose, hub = m_hub](const Message& req, const std::shared_ptr<Writer>& w) {
        Status st;
        const auto sidechain{propose(req, st)};
        if (!sidechain) return Fail(w, st);
        if (ctx.node.validation_signals) ctx.node.validation_signals->SyncWithValidationInterfaceQueue();
        hub->AddProposal({*sidechain, w, {}});
    });

    Add(server, svc, "SetSidechainAck", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        if (const auto err{BeforeActivation(ctx, "a sidechain ACK cannot take effect")}) return Fail(w, *err);
        Status st;
        const auto slot{ReadSlot(req, "sidechain_number", req.Desc().full_name, st)};
        if (!slot) return Fail(w, st);
        const auto hash{ReadReverseHash(req, "description_sha256d_hash", st)};
        if (!hash) return Fail(w, st);
        ctx.Chainman().m_drivechain_miner.SetAck(*slot, *hash, req.GetBool("ack"));
        Reply(w, ctx.New("cusf.mainchain.v1.SetSidechainAckResponse"));
    });

    Add(server, svc, "SetAckAllProposals", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        using P = drivechain::MinerState::AckPolicy;
        const uint64_t policy{req.GetUInt("policy")};
        P p;
        switch (policy) {
        case 1: p = P::NONE; break;
        case 2: p = P::NEW_SLOTS; break;
        case 3: p = P::ALL; break;
        default: return Fail(w, InvalidField(req.Desc().full_name, "policy", util::ToString(policy), "unknown policy"));
        }
        ctx.Chainman().m_drivechain_miner.SetAckPolicy(p);
        Reply(w, ctx.New("cusf.mainchain.v1.SetAckAllProposalsResponse"));
    });

    // Our votes are per sidechain (one bundle to upvote, or a default for all): an ACK is the vote for
    // that bundle of its sidechain, a NACK withdraws it if it is the vote.
    Add(server, svc, "SetWithdrawalBundleAck", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        if (const auto err{BeforeActivation(ctx, "a withdrawal bundle ACK cannot take effect")}) return Fail(w, *err);
        Status st;
        const auto slot{ReadSlot(req, "sidechain_number", req.Desc().full_name, st)};
        if (!slot) return Fail(w, st);
        const auto m6id{ReadConsensusHash(req, "m6id", st)};
        if (!m6id) return Fail(w, st);
        drivechain::MinerState& miner{ctx.Chainman().m_drivechain_miner};
        if (req.GetBool("ack")) {
            drivechain::Vote vote;
            vote.type = drivechain::Vote::Type::UPVOTE;
            vote.bundle = *m6id;
            miner.SetVote(*slot, vote);
        } else {
            const auto votes{miner.GetVotes()};
            const auto it{votes.find(*slot)};
            if (it != votes.end() && it->second.type == drivechain::Vote::Type::UPVOTE && it->second.bundle == *m6id) miner.ClearVote(*slot);
        }
        Reply(w, ctx.New("cusf.mainchain.v1.SetWithdrawalBundleAckResponse"));
    });

    // NONE: abstain; KNOWN: upvote what this node's sidechain node handed it (our default); ALL: also follow
    // the bundle leading by 50 (not the first pending bundle of anybody, which pays whatever is proposed);
    // ALARM: downvote all.
    Add(server, svc, "SetWithdrawalBundlePolicy", [&ctx](const Message& req, const std::shared_ptr<Writer>& w) {
        const uint64_t policy{req.GetUInt("policy")};
        drivechain::MinerState& miner{ctx.Chainman().m_drivechain_miner};
        switch (policy) {
        case 1: miner.SetDefaultVote(drivechain::Vote::Type::ABSTAIN); miner.SetFollow(false); break;
        case 2: miner.SetDefaultVote(drivechain::Vote::Type::UPVOTE); miner.SetFollow(false); break;
        case 3: miner.SetDefaultVote(drivechain::Vote::Type::UPVOTE); miner.SetFollow(true); break;
        case 4: miner.SetDefaultVote(drivechain::Vote::Type::DOWNVOTE); miner.SetFollow(false); break;
        default: return Fail(w, InvalidField(req.Desc().full_name, "policy", util::ToString(policy), "unknown policy"));
        }
        Reply(w, ctx.New("cusf.mainchain.v1.SetWithdrawalBundlePolicyResponse"));
    });

    Add(server, svc, "ProposeWithdrawalBundle", [this](const Message& req, const std::shared_ptr<Writer>& w) { ProposeBundle(req, w, "cusf.mainchain.v1.ProposeWithdrawalBundleResponse"); });

    Add(server, svc, "GetBlockProducerState", [&ctx](const Message&, const std::shared_ptr<Writer>& w) {
        drivechain::MinerState& miner{ctx.Chainman().m_drivechain_miner};
        Message resp{ctx.New("cusf.mainchain.v1.GetBlockProducerStateResponse")};
        {
            LOCK(::cs_main);
            const drivechain::SidechainDB& scdb{ctx.Chainman().ActiveChainstate().m_scdb};
            for (const drivechain::Sidechain& s : miner.GetProposals()) {
                // Authored here and not on the chain yet.
                if (scdb.GetProposal(s.slot, s.GetHash())) continue;
                if (const drivechain::Slot* slot{scdb.GetSlot(s.slot)}; slot && slot->sidechain == s) continue;
                Message& p{resp.Add("pending_proposals")};
                const std::vector<unsigned char> description{s.Description()};
                SetU32(p, "sidechain_number", s.slot);
                SetHex(p, "description_sha256d_hash", s.GetHash().GetHex());
                if (const auto decl{ParseDeclaration(description)}) SetDeclaration(p, "declaration", *decl);
                SetHex(p, "description", HexStr(description));
            }
        }
        for (const auto& [slot, hash] : miner.GetAcks()) {
            Message& a{resp.Add("explicit_acks")};
            SetU32(a, "sidechain_number", slot);
            SetHex(a, "description_sha256d_hash", hash.GetHex());
        }
        using P = drivechain::MinerState::AckPolicy;
        const P ack{miner.GetAckPolicy()};
        resp.SetUInt("ack_policy", ack == P::ALL ? 3 : ack == P::NEW_SLOTS ? 2 : 1);
        const auto vote{miner.GetDefaultVote()};
        resp.SetUInt("withdrawal_bundle_policy", vote == drivechain::Vote::Type::ABSTAIN ? 1 : vote == drivechain::Vote::Type::DOWNVOTE ? 4 : miner.GetFollow() ? 3 : 2);
        for (const auto& [slot, v] : miner.GetVotes()) {
            if (v.type != drivechain::Vote::Type::UPVOTE) continue;
            Message& a{resp.Add("explicit_bundle_acks")};
            SetU32(a, "sidechain_number", slot);
            SetHex(a, "m6id", HexStr(v.bundle));
        }
        Reply(w, resp);
    });
}

// ---------------------------------------------------------------------------
// WalletService (lib/server/wallet/grpc.rs), on a wallet of this node

namespace {

uint64_t Sats(const UniValue& amount) { return static_cast<uint64_t>(std::max<CAmount>(0, AmountFromValue(amount))); }

/** The enforcer's WalletTransaction (lib/proto.rs:1036-1047) of a wallet transaction (gettransaction verbose). */
void FillWalletTx(Ctx& ctx, Message& m, const UniValue& tx, std::map<std::string, bool>& mine_cache, std::map<std::string, UniValue>& tx_cache)
{
    Status ignored;
    const auto is_mine{[&](const std::string& address) {
        if (address.empty()) return false;
        const auto it{mine_cache.find(address)};
        if (it != mine_cache.end()) return it->second;
        UniValue params(UniValue::VARR);
        params.push_back(address);
        const auto info{ctx.Rpc("getaddressinfo", params, true, ignored)};
        const bool mine{info && info->find_value("ismine").isTrue()};
        mine_cache[address] = mine;
        return mine;
    }};
    SetHex(m, "txid", tx.find_value("txid").get_str());
    SetHex(m, "raw_transaction", tx.find_value("hex").get_str());
    const UniValue& decoded{tx.find_value("decoded")};
    uint64_t received{0}, sent{0};
    for (const UniValue& out : decoded.find_value("vout").getValues()) {
        const UniValue& address{out.find_value("scriptPubKey").find_value("address")};
        if (address.isStr() && is_mine(address.get_str())) received += Sats(out.find_value("value"));
    }
    for (const UniValue& in : decoded.find_value("vin").getValues()) {
        if (!in.find_value("txid").isStr()) continue;
        const std::string prev{in.find_value("txid").get_str()};
        auto it{tx_cache.find(prev)};
        if (it == tx_cache.end()) {
            UniValue params(UniValue::VARR);
            params.push_back(prev);
            params.push_back(true);
            params.push_back(true);
            const auto parent{ctx.Rpc("gettransaction", params, true, ignored)};
            it = tx_cache.emplace(prev, parent ? *parent : UniValue{}).first;
        }
        if (it->second.isNull()) continue;
        const int n{in.find_value("vout").getInt<int>()};
        const auto& vouts{it->second.find_value("decoded").find_value("vout").getValues()};
        if (n < 0 || static_cast<size_t>(n) >= vouts.size()) continue;
        const UniValue& address{vouts[n].find_value("scriptPubKey").find_value("address")};
        if (address.isStr() && is_mine(address.get_str())) sent += Sats(vouts[n].find_value("value"));
    }
    int64_t fee{0};
    if (tx.find_value("fee").isNum()) (void)ParseFixedPoint(tx.find_value("fee").getValStr(), 8, &fee);
    m.SetUInt("fee_sats", static_cast<uint64_t>(fee < 0 ? -fee : fee));
    m.SetUInt("received_sats", received);
    m.SetUInt("sent_sats", sent);
    Message& conf{m.Mutable("confirmation_info")};
    if (tx.find_value("confirmations").getInt<int>() > 0) {
        conf.SetUInt("height", tx.find_value("blockheight").getInt<int>());
        SetHex(conf, "block_hash", tx.find_value("blockhash").get_str());
        conf.Mutable("timestamp").SetInt("seconds", tx.find_value("blocktime").getInt<int64_t>());
    } else {
        conf.Mutable("timestamp").SetInt("seconds", tx.find_value("time").getInt<int64_t>());
    }
}

} // namespace

void ServicesImpl::RegisterWallet(Server& server)
{
    const std::string svc{"cusf.mainchain.v1.WalletService"};
    m_services.push_back(svc);
    Ctx& ctx{m_ctx};
    const auto rpc{[&ctx](const std::string& method, UniValue params, const std::shared_ptr<Writer>& w) -> std::optional<UniValue> {
        Status st;
        auto result{ctx.Rpc(method, std::move(params), /*wallet=*/true, st)};
        if (!result) Fail(w, st);
        return result;
    }};

    Add(server, svc, "BroadcastWithdrawalBundle", [this](const Message& req, const std::shared_ptr<Writer>& w) { ProposeBundle(req, w, "cusf.mainchain.v1.BroadcastWithdrawalBundleResponse"); });

    // create_bmm_critical_data_transaction (wallet/grpc.rs:161-257): an M8 that pays the bid as its fee,
    // on the tip, which prev_bytes must be (else INVALID_ARGUMENT naming prev_bytes: sidechains read
    // that as "the tip moved"). With our wallet's createbmmrequest (its locktime is the wallet's, the
    // replay protection on eCash, not `height`).
    Add(server, svc, "CreateBmmCriticalDataTransaction", [&ctx, rpc](const Message& req, const std::shared_ptr<Writer>& w) {
        const std::string name{req.Desc().full_name};
        if (!req.GetMessage("value_sats")) return Fail(w, MissingField(name, "value_sats"));
        const uint64_t value{req.GetMessage("value_sats")->GetUInt("value")};
        if (!req.GetMessage("height")) return Fail(w, MissingField(name, "height"));
        const uint64_t height{req.GetMessage("height")->GetUInt("value")};
        if (height >= 500'000'000) return Fail(w, InvalidField(name, "height", util::ToString(height), "invalid block height"));
        Status st;
        const auto slot{ReadSlot(req, "sidechain_id", name, st)};
        if (!slot) return Fail(w, st);
        if (!WITH_LOCK(::cs_main, return ctx.Chainman().ActiveChainstate().m_scdb.IsActive(*slot))) return Fail(w, {Code::FAILED_PRECONDITION, "sidechain is not active"});
        const auto critical{ReadConsensusHash(req, "critical_hash", st)};
        if (!critical) return Fail(w, st);
        const auto prev{ReadReverseHash(req, "prev_bytes", st)};
        if (!prev) return Fail(w, st);
        const uint256 tip{WITH_LOCK(::cs_main, return ctx.Chainman().ActiveChain().Tip()->GetBlockHash())};
        if (*prev != tip) return Fail(w, {Code::INVALID_ARGUMENT, strprintf("invalid prev_bytes %s: expected %s", prev->GetHex(), tip.GetHex())});
        if (value == 0) return Fail(w, {Code::INVALID_ARGUMENT, "the bid must be positive"});
        UniValue params(UniValue::VARR);
        params.push_back(static_cast<uint64_t>(*slot));
        params.push_back(critical->GetHex());
        params.push_back(ValueFromAmount(static_cast<CAmount>(value)));
        const auto result{rpc("createbmmrequest", params, w)};
        if (!result) return;
        Message resp{ctx.New("cusf.mainchain.v1.CreateBmmCriticalDataTransactionResponse")};
        SetHex(resp, "txid", result->find_value("txid").get_str());
        Reply(w, resp);
    });

    // create_deposit_transaction (wallet/grpc.rs:259-308): the destination written as given. The fee is
    // our wallet's estimate (fee_sats is required, as by the enforcer, but not followed).
    Add(server, svc, "CreateDepositTransaction", [&ctx, rpc](const Message& req, const std::shared_ptr<Writer>& w) {
        const std::string name{req.Desc().full_name};
        Status st;
        const auto slot{ReadSlot(req, "sidechain_id", name, st)};
        if (!slot) return Fail(w, st);
        if (!req.GetMessage("address")) return Fail(w, MissingField(name, "address"));
        const std::string address{req.GetMessage("address")->GetString("value")};
        if (address.empty()) return Fail(w, {Code::INVALID_ARGUMENT, "address must be non-empty"});
        if (!req.GetMessage("value_sats")) return Fail(w, MissingField(name, "value_sats"));
        const uint64_t value{req.GetMessage("value_sats")->GetUInt("value")};
        if (value == 0) return Fail(w, {Code::INVALID_ARGUMENT, "value_sats must be greater than zero"});
        if (!req.GetMessage("fee_sats")) return Fail(w, MissingField(name, "fee_sats"));
        if (!WITH_LOCK(::cs_main, return ctx.Chainman().ActiveChainstate().m_scdb.IsActive(*slot))) return Fail(w, {Code::FAILED_PRECONDITION, strprintf("sidechain %u is not active", *slot)});
        UniValue params(UniValue::VARR);
        params.push_back(static_cast<uint64_t>(*slot));
        params.push_back(address);
        params.push_back(ValueFromAmount(static_cast<CAmount>(value)));
        const auto result{rpc("createsidechaindeposit", params, w)};
        if (!result) return;
        Message resp{ctx.New("cusf.mainchain.v1.CreateDepositTransactionResponse")};
        SetHex(resp, "txid", result->find_value("txid").get_str());
        Reply(w, resp);
    });

    Add(server, svc, "CreateNewAddress", [&ctx, rpc](const Message&, const std::shared_ptr<Writer>& w) {
        UniValue params(UniValue::VARR);
        params.push_back("");
        params.push_back("bech32");
        const auto result{rpc("getnewaddress", params, w)};
        if (!result) return;
        Message resp{ctx.New("cusf.mainchain.v1.CreateNewAddressResponse")};
        resp.SetString("address", result->get_str());
        Reply(w, resp);
    });

    // A BIP39 mnemonic has no place in our wallets (descriptor wallets of BIP32 seeds): refused. Without one,
    // a wallet of that name (the -grpcwallet one, or "grpc"), encrypted with the password if one is given.
    Add(server, svc, "CreateWallet", [&ctx, rpc](const Message& req, const std::shared_ptr<Writer>& w) {
        if (req.Count("mnemonic_words") || !req.GetString("mnemonic_path").empty()) {
            return Fail(w, {Code::UNIMPLEMENTED, "BIP39 mnemonics are not supported: Backbone's wallets are descriptor wallets (createwallet, importdescriptors)"});
        }
        Status st;
        if (ctx.Rpc("getwalletinfo", UniValue(UniValue::VARR), true, st)) return Fail(w, {Code::ALREADY_EXISTS, "Wallet already exists"});
        UniValue params(UniValue::VARR);
        params.push_back(ctx.options.wallet.empty() ? "grpc" : ctx.options.wallet);
        params.push_back(false);
        params.push_back(false);
        params.push_back(req.GetString("password"));
        if (!rpc("createwallet", params, w)) return;
        Reply(w, ctx.New("cusf.mainchain.v1.CreateWalletResponse"));
    });

    Add(server, svc, "FullScan", [&ctx, rpc](const Message&, const std::shared_ptr<Writer>& w) {
        Status st;
        if (!ctx.Rpc("rescanblockchain", UniValue(UniValue::VARR), true, st)) {
            if (st.message.find("rescanning") != std::string::npos) st.code = Code::ABORTED;
            return Fail(w, st);
        }
        const auto info{rpc("getwalletinfo", UniValue(UniValue::VARR), w)};
        if (!info) return;
        Message resp{ctx.New("cusf.mainchain.v1.FullScanResponse")};
        SetHex(resp, "tip_hash", info->find_value("lastprocessedblock").find_value("hash").get_str());
        Reply(w, resp);
    });

    // get_balance (wallet/grpc.rs:310-324): confirmed, and the rest (unconfirmed, immature) as pending.
    Add(server, svc, "GetBalance", [&ctx, rpc](const Message&, const std::shared_ptr<Writer>& w) {
        const auto balances{rpc("getbalances", UniValue(UniValue::VARR), w)};
        if (!balances) return;
        UniValue params(UniValue::VARR);
        params.push_back("*");
        params.push_back(1);
        const auto confirmed{rpc("getbalance", params, w)};
        if (!confirmed) return;
        const UniValue& mine{balances->find_value("mine")};
        const uint64_t total{Sats(mine.find_value("trusted")) + Sats(mine.find_value("untrusted_pending")) + Sats(mine.find_value("immature"))};
        const uint64_t conf{Sats(*confirmed)};
        Message resp{ctx.New("cusf.mainchain.v1.GetBalanceResponse")};
        resp.SetUInt("confirmed_sats", conf);
        resp.SetUInt("pending_sats", total > conf ? total - conf : 0);
        const auto info{rpc("getwalletinfo", UniValue(UniValue::VARR), w)};
        if (!info) return;
        const int tip{WITH_LOCK(::cs_main, return ctx.Chainman().ActiveChain().Height())};
        resp.SetBool("has_synced", !info->find_value("scanning").isObject() && info->find_value("lastprocessedblock").find_value("height").getInt<int>() == tip);
        Reply(w, resp);
    });

    const auto list_txs{[&ctx, rpc](const std::shared_ptr<Writer>& w, bool deposits_only) -> std::optional<Message> {
        UniValue params(UniValue::VARR);
        params.push_back("*");
        params.push_back(1'000'000);
        params.push_back(0);
        params.push_back(true);
        const auto list{rpc("listtransactions", params, w)};
        if (!list) return std::nullopt;
        Message resp{ctx.New(deposits_only ? "cusf.mainchain.v1.ListSidechainDepositTransactionsResponse" : "cusf.mainchain.v1.ListTransactionsResponse")};
        std::set<std::string> seen;
        std::map<std::string, bool> mine_cache;
        std::map<std::string, UniValue> tx_cache;
        for (const UniValue& entry : list->getValues()) {
            const std::string txid{entry.find_value("txid").get_str()};
            if (!seen.insert(txid).second) continue;
            UniValue p(UniValue::VARR);
            p.push_back(txid);
            p.push_back(true);
            p.push_back(true);
            Status st;
            const auto tx{ctx.Rpc("gettransaction", p, true, st)};
            if (!tx) continue;
            if (deposits_only) {
                // A deposit: a treasury output of an active sidechain, bigger than the one it spends.
                CMutableTransaction mtx;
                if (!DecodeHexTx(mtx, tx->find_value("hex").get_str())) continue;
                std::optional<SidechainId> slot;
                for (const CTxOut& out : mtx.vout) {
                    if (const auto s{drivechain::ParseEscrowScript(out.scriptPubKey)}) slot = *s;
                }
                if (!slot) continue;
                Message& item{resp.Add("transactions")};
                SetU32(item, "sidechain_number", *slot);
                FillWalletTx(ctx, item.Mutable("tx"), *tx, mine_cache, tx_cache);
            } else {
                FillWalletTx(ctx, resp.Add("transactions"), *tx, mine_cache, tx_cache);
            }
        }
        return resp;
    }};
    Add(server, svc, "ListTransactions", [list_txs](const Message&, const std::shared_ptr<Writer>& w) {
        if (auto resp{list_txs(w, false)}) Reply(w, *resp);
    });
    Add(server, svc, "ListSidechainDepositTransactions", [list_txs](const Message&, const std::shared_ptr<Writer>& w) {
        if (auto resp{list_txs(w, true)}) Reply(w, *resp);
    });

    Add(server, svc, "ListUnspentOutputs", [&ctx, rpc](const Message&, const std::shared_ptr<Writer>& w) {
        UniValue params(UniValue::VARR);
        params.push_back(0);
        const auto list{rpc("listunspent", params, w)};
        if (!list) return;
        Message resp{ctx.New("cusf.mainchain.v1.ListUnspentOutputsResponse")};
        const int tip{WITH_LOCK(::cs_main, return ctx.Chainman().ActiveChain().Height())};
        for (const UniValue& u : list->getValues()) {
            Message& out{resp.Add("outputs")};
            SetHex(out, "txid", u.find_value("txid").get_str());
            out.SetUInt("vout", u.find_value("vout").getInt<int>());
            out.SetUInt("value_sats", Sats(u.find_value("amount")));
            const UniValue& address{u.find_value("address")};
            if (address.isStr()) {
                Status st;
                UniValue p(UniValue::VARR);
                p.push_back(address.get_str());
                const auto info{ctx.Rpc("getaddressinfo", p, true, st)};
                out.SetBool("is_internal", info && info->find_value("ischange").isTrue());
                SetStr(out, "address", address.get_str());
            }
            const int confirmations{u.find_value("confirmations").getInt<int>()};
            out.SetBool("is_confirmed", confirmations > 0);
            if (confirmations > 0) {
                const int height{tip - confirmations + 1};
                out.SetUInt("confirmed_at_block", height);
                const int64_t time{WITH_LOCK(::cs_main, const CBlockIndex* b{ctx.Chainman().ActiveChain()[height]}; return b ? b->GetBlockTime() : 0)};
                out.Mutable("confirmed_at_time").SetInt("seconds", time);
            }
        }
        Reply(w, resp);
    });

    Add(server, svc, "GetInfo", [&ctx, rpc](const Message&, const std::shared_ptr<Writer>& w) {
        const auto info{rpc("getwalletinfo", UniValue(UniValue::VARR), w)};
        if (!info) return;
        UniValue p(UniValue::VARR);
        p.push_back(0);
        const auto unspent{rpc("listunspent", p, w)};
        if (!unspent) return;
        Message resp{ctx.New("cusf.mainchain.v1.GetInfoResponse")};
        resp.SetString("network", NetworkName(ctx.Chainman().GetParams().GetChainType()));
        resp.SetUInt("transaction_count", info->find_value("txcount").getInt<int>());
        resp.SetUInt("unspent_output_count", unspent->size());
        Status st;
        if (const auto descs{ctx.Rpc("listdescriptors", UniValue(UniValue::VARR), true, st)}) {
            // The active wpkh descriptors, as the enforcer's external and internal keychains.
            std::map<std::string, std::string> found;
            for (const UniValue& d : descs->find_value("descriptors").getValues()) {
                const std::string desc{d.find_value("desc").get_str()};
                if (!d.find_value("active").isTrue() || !desc.starts_with("wpkh(")) continue;
                found[d.find_value("internal").isTrue() ? "internal" : "external"] = desc;
            }
            for (const auto& [k, v] : found) {
                Message& e{resp.Add("descriptors")};
                e.SetString("key", k);
                e.SetString("value", v);
            }
        }
        Message& tip{resp.Mutable("tip")};
        const UniValue& last{info->find_value("lastprocessedblock")};
        tip.SetUInt("height", last.find_value("height").getInt<int>());
        SetHex(tip, "hash", last.find_value("hash").get_str());
        Message& sync{resp.Mutable("sync_source")};
        sync.SetUInt("kind", 3);  // KIND_DISABLED: the wallet follows the node's blocks
        sync.SetUInt("state", 1); // STATE_DISABLED
        Reply(w, resp);
    });

    // send_transaction (wallet/grpc.rs:419-512), with the wallet's send / sendall.
    Add(server, svc, "SendTransaction", [&ctx, rpc](const Message& req, const std::shared_ptr<Writer>& w) {
        const std::string name{req.Desc().full_name};
        Status st;
        UniValue outputs(UniValue::VARR);
        for (size_t i{0}; i < req.Count("destinations"); ++i) {
            const Message& e{req.At("destinations", i)};
            const std::string address{e.GetString("key")};
            if (!IsValidDestination(DecodeDestination(address))) return Fail(w, {Code::INVALID_ARGUMENT, strprintf("invalid address: %s", address)});
            UniValue o(UniValue::VOBJ);
            o.pushKV(address, ValueFromAmount(static_cast<CAmount>(e.GetUInt("value"))));
            outputs.push_back(std::move(o));
        }
        std::optional<std::string> op_return;
        if (req.GetMessage("op_return_message")) {
            const auto bytes{ReadRawHex(req, "op_return_message", std::nullopt, st)};
            if (!bytes) return Fail(w, st);
            op_return = HexStr(*bytes);
        }
        const bool drain{req.Has("drain_wallet_to")};
        if (outputs.empty() && !op_return && !drain) return Fail(w, {Code::INVALID_ARGUMENT, "no destinations or op_return_message provided"});
        if (drain && req.Count("required_utxos")) return Fail(w, {Code::INVALID_ARGUMENT, "cannot provide both drain_wallet_to and required_utxos"});
        UniValue options(UniValue::VOBJ);
        if (const Message* fee{req.GetMessage("fee_rate")}) {
            const pb::FieldDesc* which{fee->WhichOneof("fee")};
            if (!which) return Fail(w, MissingField(fee->Desc().full_name, "fee"));
            if (which->name == "sats") return Fail(w, {Code::UNIMPLEMENTED, "an absolute fee is not supported; give sat_per_vbyte"});
            if (fee->GetUInt("sat_per_vbyte") == 0) return Fail(w, InvalidField(fee->Desc().full_name, "fee", "0", "Invalid sats per vbyte"));
            options.pushKV("fee_rate", fee->GetUInt("sat_per_vbyte"));
        }
        std::optional<UniValue> result;
        if (drain) {
            UniValue recipients(UniValue::VARR);
            recipients.push_back(req.GetString("drain_wallet_to"));
            UniValue params(UniValue::VARR);
            params.push_back(recipients);
            params.push_back(UniValue::VNULL);
            params.push_back(UniValue::VNULL);
            params.push_back(UniValue::VNULL);
            params.push_back(options);
            result = rpc("sendall", params, w);
        } else {
            if (op_return) {
                UniValue o(UniValue::VOBJ);
                o.pushKV("data", *op_return);
                outputs.push_back(std::move(o));
            }
            if (req.Count("required_utxos")) {
                UniValue inputs(UniValue::VARR);
                for (size_t i{0}; i < req.Count("required_utxos"); ++i) {
                    const Message& u{req.At("required_utxos", i)};
                    const auto txid{ReadReverseHash(u, "txid", st)};
                    if (!txid) return Fail(w, st);
                    UniValue in(UniValue::VOBJ);
                    in.pushKV("txid", txid->GetHex());
                    in.pushKV("vout", u.GetUInt("vout"));
                    inputs.push_back(std::move(in));
                }
                options.pushKV("inputs", inputs);
                options.pushKV("add_inputs", true);
            }
            UniValue params(UniValue::VARR);
            params.push_back(outputs);
            params.push_back(UniValue::VNULL);
            params.push_back(UniValue::VNULL);
            params.push_back(UniValue::VNULL);
            params.push_back(options);
            result = rpc("send", params, w);
        }
        if (!result) return;
        Message resp{ctx.New("cusf.mainchain.v1.SendTransactionResponse")};
        SetHex(resp, "txid", result->find_value("txid").get_str());
        Reply(w, resp);
    });

    // Unlocked for as long as the wallet allows (the enforcer's stays unlocked until restarted).
    Add(server, svc, "UnlockWallet", [&ctx, rpc](const Message& req, const std::shared_ptr<Writer>& w) {
        UniValue params(UniValue::VARR);
        params.push_back(req.GetString("password"));
        params.push_back(100'000'000);
        if (!rpc("walletpassphrase", params, w)) return;
        Reply(w, ctx.New("cusf.mainchain.v1.UnlockWalletResponse"));
    });

    // The older interface's GenerateBlocks (cusf_sidechain_proto, wallet.proto:223-233), which
    // plain-bitassets and zside call: blocks to a new address of the wallet, one reply per block.
    Add(server, svc, "GenerateBlocks", [&ctx, rpc](const Message& req, const std::shared_ptr<Writer>& w) {
        const uint64_t blocks{req.GetMessage("blocks") ? req.GetMessage("blocks")->GetUInt("value") : 1};
        if (blocks == 0) return Fail(w, {Code::INVALID_ARGUMENT, "must provide a positive number of blocks"});
        UniValue params(UniValue::VARR);
        params.push_back("");
        params.push_back("bech32");
        const auto address{rpc("getnewaddress", params, w)};
        if (!address) return;
        TRY_LOCK(ctx.generate_mutex, lock);
        if (!lock) return Fail(w, {Code::RESOURCE_EXHAUSTED, "block generation is already in progress"});
        drivechain::MinerState& miner{ctx.Chainman().m_drivechain_miner};
        const auto policy{miner.GetAckPolicy()};
        if (req.GetBool("ack_all_proposals")) miner.SetAckPolicy(drivechain::MinerState::AckPolicy::ALL);
        const auto err{Generate(ctx, static_cast<uint32_t>(blocks), address->get_str(), [&](const uint256& h) {
            Message resp{ctx.New("cusf.mainchain.v1.GenerateBlocksResponse")};
            SetHex(resp, "block_hash", h.GetHex());
            w->Write(resp);
        })};
        miner.SetAckPolicy(policy);
        w->Finish(err ? *err : Status{});
    });
}

// ---------------------------------------------------------------------------
// grpc.health.v1 (connectrpc-health, StaticChecker with the services served: app/main.rs:774-775)

void ServicesImpl::RegisterHealth(Server& server)
{
    Ctx& ctx{m_ctx};
    const auto status_of{[this](const std::string& service) -> std::optional<int> {
        if (service.empty() || std::find(m_services.begin(), m_services.end(), service) != m_services.end()) return 1; // SERVING
        return std::nullopt;
    }};
    Add(server, "grpc.health.v1.Health", "Check", [&ctx, status_of](const Message& req, const std::shared_ptr<Writer>& w) {
        const auto status{status_of(req.GetString("service"))};
        // An unknown service: NOT_FOUND, which the clients read as "not served".
        if (!status) return Fail(w, {Code::NOT_FOUND, "unknown service " + req.GetString("service")});
        Message resp{ctx.New("grpc.health.v1.HealthCheckResponse")};
        resp.SetUInt("status", *status);
        Reply(w, resp);
    });
    Add(server, "grpc.health.v1.Health", "Watch", [&ctx, status_of, hub = m_hub](const Message& req, const std::shared_ptr<Writer>& w) {
        Message resp{ctx.New("grpc.health.v1.HealthCheckResponse")};
        resp.SetUInt("status", status_of(req.GetString("service")).value_or(3)); // SERVICE_UNKNOWN
        w->Write(resp);
        hub->AddHealthWatch(w);
    });
}

// ---------------------------------------------------------------------------
// grpc.reflection.v1 and v1alpha (connectrpc-reflection, app/main.rs:763-776): the services served, and
// the descriptors of the files, with those they import.

void ServicesImpl::RegisterReflection(Server& server)
{
    Ctx& ctx{m_ctx};
    for (const std::string version : {"v1", "v1alpha"}) {
        const std::string pkg{"grpc.reflection." + version};
        Add(server, pkg + ".ServerReflection", "ServerReflectionInfo", [this, &ctx, pkg](const Message& req, const std::shared_ptr<Writer>& w) {
            Message resp{ctx.New(pkg + ".ServerReflectionResponse")};
            resp.SetString("valid_host", req.GetString("host"));
            resp.Mutable("original_request") = req;
            const auto files{[&](const std::string& file) {
                Message& fd{resp.Mutable("file_descriptor_response")};
                for (const std::string& f : ctx.pool.FileWithDependencies(file)) fd.AddString("file_descriptor_proto", *ctx.pool.FileProto(f));
            }};
            const auto error{[&](int code, const std::string& message) {
                Message& e{resp.Mutable("error_response")};
                e.SetInt("error_code", code);
                e.SetString("error_message", message);
            }};
            const pb::FieldDesc* which{req.WhichOneof("message_request")};
            const std::string name{which ? which->name : ""};
            if (name == "list_services") {
                Message& list{resp.Mutable("list_services_response")};
                for (const std::string& s : m_services) list.Add("service").SetString("name", s);
            } else if (name == "file_by_filename") {
                const std::string& f{req.GetString("file_by_filename")};
                if (ctx.pool.FileProto(f)) files(f);
                else error(5, "file not found: " + f);
            } else if (name == "file_containing_symbol") {
                std::string symbol{req.GetString("file_containing_symbol")};
                if (symbol.starts_with('.')) symbol.erase(0, 1);
                const std::string* f{ctx.pool.FileOfSymbol(symbol)};
                // A field or an enum value names its message's file.
                for (std::string s{symbol}; !f && s.find('.') != std::string::npos;) {
                    s.resize(s.rfind('.'));
                    f = ctx.pool.FileOfSymbol(s);
                }
                if (f) files(*f);
                else error(5, "symbol not found: " + symbol);
            } else if (name == "all_extension_numbers_of_type") {
                const std::string& type{req.GetString("all_extension_numbers_of_type")};
                if (ctx.pool.FindMessage(type)) resp.Mutable("all_extension_numbers_response").SetString("base_type_name", type);
                else error(5, "type not found: " + type);
            } else if (name == "file_containing_extension") {
                error(5, "extensions are not used");
            } else {
                error(3, "invalid reflection request");
            }
            w->Write(resp);
        });
    }
}

// store_withdrawal_bundle (lib/server/mod.rs:38-78), for ProposeWithdrawalBundle and the deprecated
// BroadcastWithdrawalBundle: a blinded M6 (no inputs, in the legacy serialization) of an active
// sidechain with a treasury, which the block template proposes (M3), votes for and pays out.
void ServicesImpl::ProposeBundle(const Message& req, const std::shared_ptr<Writer>& w, const std::string& reply)
{
    const std::string name{req.Desc().full_name};
    Status st;
    const auto slot{ReadSlot(req, "sidechain_id", name, st)};
    if (!slot) return Fail(w, st);
    const Message* transaction{req.GetMessage("transaction")};
    if (!transaction) return Fail(w, MissingField(name, "transaction"));
    const std::string& bytes{transaction->GetString("value")};
    {
        LOCK(::cs_main);
        const drivechain::Slot* s{m_ctx.Chainman().ActiveChainstate().m_scdb.GetSlot(*slot)};
        if (!s) return Fail(w, {Code::FAILED_PRECONDITION, strprintf("cannot accept a withdrawal bundle for sidechain %u: not active", *slot)});
        if (!s->has_ctip) return Fail(w, {Code::FAILED_PRECONDITION, strprintf("cannot accept a withdrawal bundle for sidechain %u: no treasury UTXO", *slot)});
    }
    CMutableTransaction tx;
    try {
        DataStream ds{MakeByteSpan(bytes)};
        ds >> TX_NO_WITNESS(tx);
        if (!ds.empty()) return Fail(w, InvalidField(name, "transaction", HexStr(MakeUCharSpan(bytes)), "data not consumed entirely when explicitly deserializing"));
    } catch (const std::exception& e) {
        return Fail(w, InvalidField(name, "transaction", HexStr(MakeUCharSpan(bytes)), e.what()));
    }
    std::string error;
    if (!m_ctx.Chainman().m_drivechain_miner.AddBundle(*slot, tx, error)) return Fail(w, InvalidField(name, "transaction", HexStr(MakeUCharSpan(bytes)), error));
    Reply(w, m_ctx.New(reply));
}

} // namespace

std::unique_ptr<Services> RegisterServices(Server& server, const pb::Pool& pool, node::NodeContext& node, const ServiceOptions& options)
{
    return std::make_unique<ServicesImpl>(server, pool, node, options);
}

} // namespace grpcsrv
