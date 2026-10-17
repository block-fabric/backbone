// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <webui/explorer.h>

#include <addresstype.h>
#include <chain.h>
#include <consensus/validation.h>
#include <drivechain/db.h>
#include <drivechain/scdb.h>
#include <drivechain/sidechain.h>
#include <key_io.h>
#include <node/blockstorage.h>
#include <primitives/block.h>
#include <script/solver.h>
#include <sync.h>
#include <undo.h>
#include <util/strencodings.h>
#include <validation.h>
#include <webui/bridge.h>

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace webui::explorer {
namespace {
using drivechain::SidechainId;
using drivechain::VoteForm;

std::string FormName(VoteForm form)
{
    switch (form) {
    case VoteForm::REPEAT_PREVIOUS: return "repeat";
    case VoteForm::ONE_BYTE: return "one";
    case VoteForm::TWO_BYTES: return "two";
    case VoteForm::LEADING_BY_50: return "leading";
    }
    return "";
}

bool IsWitnessCommitment(const CScript& script)
{
    return script.size() >= MINIMUM_WITNESS_COMMITMENT && script[0] == OP_RETURN && script[1] == 0x24 && script[2] == 0xaa &&
           script[3] == 0x21 && script[4] == 0xa9 && script[5] == 0xed;
}

/** The address an output pays, or "" (data, a bare or unknown script). */
std::string AddressOf(const CScript& script)
{
    CTxDestination dest;
    return ExtractDestination(script, dest) ? EncodeDestination(dest) : std::string{};
}

/** Non-abstaining entries of a vote message of the one or two byte form. */
int64_t Cast(const drivechain::VoteMessage& message)
{
    return std::count_if(message.votes.begin(), message.votes.end(), [](uint16_t v) { return v != drivechain::VOTE_ABSTAIN; });
}

/** Printable text of some bytes, if they are all printable: what a data output says, when it says something. */
std::optional<std::string> Printable(std::span<const unsigned char> data)
{
    if (data.empty()) return std::nullopt;
    std::string text;
    for (const unsigned char c : data) {
        if (c < 0x20 || c > 0x7e) return std::nullopt;
        text += static_cast<char>(c);
    }
    return text;
}
} // namespace

UniValue BlockSummary(const CBlock& block, int height, const Consensus::Params& consensus)
{
    UniValue out{UniValue::VOBJ};
    out.pushKV("size", static_cast<int64_t>(::GetSerializeSize(TX_WITH_WITNESS(block))));
    out.pushKV("weight", GetBlockWeight(block));
    const CAmount subsidy{GetBlockSubsidy(height, consensus)};
    CAmount reward{0};
    std::string payout;
    CAmount most{-1};
    int64_t payouts{0}, votes{0}, acks{0}, proposals{0}, bundles{0}, escrow{0};
    std::string vote_form;
    UniValue bmm{UniValue::VARR};
    if (!block.vtx.empty()) {
        const CTransaction& coinbase{*block.vtx[0]};
        for (const CTxOut& txout : coinbase.vout) {
            reward += txout.nValue;
            const CScript& script{txout.scriptPubKey};
            if (txout.nValue > 0 && !script.IsUnspendable()) {
                ++payouts;
                if (txout.nValue > most) {
                    most = txout.nValue;
                    payout = AddressOf(script);
                }
            }
            if (const auto accept{drivechain::ParseBmmAcceptScript(script)}) {
                bmm.push_back(static_cast<int64_t>(accept->first));
            } else if (drivechain::ParseAckScript(script)) {
                ++acks;
            } else if (drivechain::ParseProposalScript(script)) {
                ++proposals;
            } else if (drivechain::ParseBundleScript(script)) {
                ++bundles;
            } else if (const auto message{drivechain::ParseVoteScript(script)}) {
                vote_form = FormName(message->form);
                votes = Cast(*message);
            }
        }
        out.pushKV("coinbase", coinbase.GetHash().GetHex());
        out.pushKV("script", coinbase.vin.empty() ? std::string{} : HexStr(coinbase.vin[0].scriptSig));
    }
    for (size_t i{1}; i < block.vtx.size(); ++i) {
        const auto& vout{block.vtx[i]->vout};
        if (std::any_of(vout.begin(), vout.end(), [](const CTxOut& o) { return drivechain::ParseEscrowScript(o.scriptPubKey).has_value(); })) ++escrow;
    }
    out.pushKV("subsidy", subsidy);
    out.pushKV("reward", reward);
    out.pushKV("fees", std::max<CAmount>(0, reward - subsidy));
    out.pushKV("payout", payout);
    out.pushKV("payouts", payouts);
    out.pushKV("bmm", std::move(bmm));
    out.pushKV("votes", votes);
    out.pushKV("voteForm", vote_form);
    out.pushKV("acks", acks);
    out.pushKV("proposals", proposals);
    out.pushKV("bundles", bundles);
    out.pushKV("escrow", escrow);
    return out;
}

UniValue BlockDetail(ChainstateManager& chainman, const uint256& hash)
{
    const CBlockIndex* pindex;
    bool active, have_data, have_undo;
    {
        LOCK(::cs_main);
        pindex = chainman.m_blockman.LookupBlockIndex(hash);
        if (!pindex) throw HostError(BRIDGE_INVALID_REQUEST, "No block has this hash");
        active = chainman.ActiveChain().Contains(*pindex);
        have_data = pindex->nStatus & BLOCK_HAVE_DATA;
        have_undo = pindex->nStatus & BLOCK_HAVE_UNDO;
    }
    if (!have_data) throw HostError("The block is not on disk (pruned)");
    const int height{pindex->nHeight};
    // The block and the coins it spent: read without the lock of the node.
    CBlock block;
    if (!chainman.m_blockman.ReadBlock(block, *pindex) || block.vtx.empty()) throw HostError("The block cannot be read");
    CBlockUndo spent;
    const bool have_spent{height > 0 && have_undo && chainman.m_blockman.ReadBlockUndo(spent, *pindex) && spent.vtxundo.size() + 1 == block.vtx.size()};
    // The fee a transaction of the block paid (not the coinbase), if the coins it spent are known.
    const auto fee_of{[&](size_t tx_index) -> std::optional<CAmount> {
        if (!have_spent || tx_index == 0 || tx_index >= block.vtx.size()) return std::nullopt;
        CAmount in{0};
        for (const Coin& coin : spent.vtxundo[tx_index - 1].vprevout) in += coin.out.nValue;
        return in - block.vtx[tx_index]->GetValueOut();
    }};
    const CTransaction& coinbase{*block.vtx[0]};
    const Consensus::Params& consensus{chainman.GetConsensus()};

    // What the coinbase says, parsed once.
    std::vector<std::pair<SidechainId, uint256>> acks;
    std::optional<drivechain::VoteMessage> vote_message;
    std::set<SidechainId> escrow_slots;
    for (const CTxOut& txout : coinbase.vout) {
        if (const auto ack{drivechain::ParseAckScript(txout.scriptPubKey)}) acks.push_back(*ack);
        if (!vote_message) vote_message = drivechain::ParseVoteScript(txout.scriptPubKey);
    }
    for (size_t i{1}; i < block.vtx.size(); ++i) {
        for (const CTxOut& txout : block.vtx[i]->vout) {
            if (const auto slot{drivechain::ParseEscrowScript(txout.scriptPubKey)}) escrow_slots.insert(*slot);
        }
    }

    // The sidechain database and the drivechain records of the block, under the lock (a few reads).
    struct SlotName {
        std::string title;
        int activation_height;
    };
    std::map<SidechainId, SlotName> slots;
    std::map<uint256, std::string> ack_titles;
    drivechain::BlockEvents events, prev_events;
    bool have_events{false};
    drivechain::BlockUndo dc_undo;
    bool have_dc_undo{false};
    std::vector<drivechain::Deposit> changes;
    {
        LOCK(::cs_main);
        const drivechain::SidechainDB& scdb{chainman.ActiveChainstate().m_scdb};
        for (const auto& [id, slot] : scdb.GetSlots()) slots.emplace(id, SlotName{slot.sidechain.title, slot.activation_height});
        for (const auto& [slot, proposal_hash] : acks) {
            if (const drivechain::Proposal* proposal{scdb.GetProposal(slot, proposal_hash)}) {
                ack_titles.emplace(proposal_hash, proposal->sidechain.title);
            } else if (const drivechain::Slot* active_slot{scdb.GetSlot(slot)}; active_slot && active_slot->sidechain.GetHash() == proposal_hash) {
                ack_titles.emplace(proposal_hash, active_slot->sidechain.title);
            }
        }
        if (const auto& db{chainman.m_blockman.m_drivechain_db}) {
            have_events = db->ReadBlockEvents(hash, events);
            if (pindex->pprev) db->ReadBlockEvents(pindex->pprev->GetBlockHash(), prev_events);
            have_dc_undo = db->ReadBlockUndo(hash, dc_undo);
            for (const SidechainId slot : escrow_slots) {
                for (auto& change : db->ListBlockDeposits(slot, height, hash)) changes.push_back(std::move(change));
            }
        }
    }
    const auto title_of{[&](SidechainId slot) {
        const auto it{slots.find(slot)};
        return it == slots.end() ? std::string{} : it->second.title;
    }};
    const auto slot_entry{[&](SidechainId slot) {
        UniValue entry{UniValue::VOBJ};
        entry.pushKV("slot", static_cast<int64_t>(slot));
        entry.pushKV("title", title_of(slot));
        return entry;
    }};

    UniValue out{UniValue::VOBJ};
    out.pushKV("hash", hash.GetHex());
    out.pushKV("height", height);
    out.pushKV("active", active);
    const CAmount subsidy{GetBlockSubsidy(height, consensus)};
    const CAmount reward{coinbase.GetValueOut()};
    out.pushKV("subsidy", subsidy);
    out.pushKV("reward", reward);
    // The fees the transactions paid, from the coins they spent; the coinbase may claim less (unclaimed).
    if (have_spent) {
        CAmount fees{0};
        for (size_t i{1}; i < block.vtx.size(); ++i) fees += *fee_of(i);
        out.pushKV("fees", fees);
        out.pushKV("unclaimed", std::max<CAmount>(0, subsidy + fees - reward));
    } else {
        out.pushKV("fees", height == 0 ? UniValue{0} : UniValue{});
    }
    out.pushKV("coinbase", coinbase.GetHash().GetHex());
    out.pushKV("script", coinbase.vin.empty() ? std::string{} : HexStr(coinbase.vin[0].scriptSig));
    out.pushKV("minScore", consensus.drivechain.withdrawal_min_score);

    // --- The outputs of the coinbase, each with what it is. ---
    UniValue outputs{UniValue::VARR};
    for (size_t n{0}; n < coinbase.vout.size() && n < MAX_LISTED; ++n) {
        const CTxOut& txout{coinbase.vout[n]};
        const CScript& script{txout.scriptPubKey};
        UniValue o{UniValue::VOBJ};
        o.pushKV("n", static_cast<int64_t>(n));
        o.pushKV("value", txout.nValue);
        std::vector<std::vector<unsigned char>> solutions;
        o.pushKV("type", GetTxnOutputType(Solver(script, solutions)));
        if (const auto proposal{drivechain::ParseProposalScript(script)}) {
            o.pushKV("kind", "proposal");
            o.pushKV("slot", static_cast<int64_t>(proposal->slot));
            o.pushKV("title", proposal->title);
            o.pushKV("hash", proposal->GetHash().GetHex());
        } else if (const auto ack{drivechain::ParseAckScript(script)}) {
            o.pushKV("kind", "ack");
            o.pushKV("slot", static_cast<int64_t>(ack->first));
            o.pushKV("hash", ack->second.GetHex());
            const auto it{ack_titles.find(ack->second)};
            o.pushKV("title", it == ack_titles.end() ? std::string{} : it->second);
        } else if (const auto bundle{drivechain::ParseBundleScript(script)}) {
            o.pushKV("kind", "bundle");
            o.pushKV("slot", static_cast<int64_t>(bundle->first));
            o.pushKV("title", title_of(bundle->first));
            o.pushKV("hash", bundle->second.GetHex());
        } else if (const auto message{drivechain::ParseVoteScript(script)}) {
            o.pushKV("kind", "vote");
            o.pushKV("form", FormName(message->form));
            o.pushKV("count", static_cast<int64_t>(message->votes.size()));
        } else if (const auto accept{drivechain::ParseBmmAcceptScript(script)}) {
            o.pushKV("kind", "bmm");
            o.pushKV("slot", static_cast<int64_t>(accept->first));
            o.pushKV("title", title_of(accept->first));
            o.pushKV("hash", accept->second.GetHex());
        } else if (IsWitnessCommitment(script)) {
            o.pushKV("kind", "witness");
            o.pushKV("hash", HexStr(std::span{script}.subspan(6, 32)));
        } else if (const auto slot{drivechain::ParseEscrowScript(script)}) {
            o.pushKV("kind", "escrow");
            o.pushKV("slot", static_cast<int64_t>(*slot));
        } else if (script.IsUnspendable()) {
            o.pushKV("kind", "data");
            o.pushKV("hex", HexStr(script));
            if (const auto data{drivechain::ParseDestinationScript(script)}) {
                if (const auto text{Printable(std::span{reinterpret_cast<const unsigned char*>(data->data()), data->size()})}) o.pushKV("text", *text);
            }
        } else {
            o.pushKV("kind", "payout");
            o.pushKV("address", AddressOf(script));
        }
        outputs.push_back(std::move(o));
    }
    out.pushKV("outputs", std::move(outputs));
    out.pushKV("outputCount", static_cast<int64_t>(coinbase.vout.size()));

    // --- Blind merged mining: the accepts (M7), each with the request (M8) it took and the bid paid. ---
    UniValue bmm{UniValue::VARR};
    for (const auto& [slot, side_hash] : drivechain::BmmAcceptsOf(coinbase)) {
        UniValue entry{slot_entry(slot)};
        entry.pushKV("hash", side_hash.GetHex());
        for (size_t i{1}; i < block.vtx.size(); ++i) {
            const auto request{drivechain::GetBmmRequest(*block.vtx[i])};
            if (!request || request->slot != slot || request->side_block_hash != side_hash) continue;
            entry.pushKV("request", block.vtx[i]->GetHash().GetHex());
            if (const auto bid{fee_of(i)}) entry.pushKV("bid", *bid);
            break;
        }
        bmm.push_back(std::move(entry));
    }
    out.pushKV("bmm", std::move(bmm));

    // --- Sidechain proposals (M1), acks (M2), activations. ---
    UniValue proposals{UniValue::VARR}, ack_list{UniValue::VARR}, activations{UniValue::VARR};
    for (const CTxOut& txout : coinbase.vout) {
        const auto proposal{drivechain::ParseProposalScript(txout.scriptPubKey)};
        if (!proposal) continue;
        if (proposals.size() >= drivechain::MAX_PROPOSALS_PER_BLOCK) break;
        UniValue entry{UniValue::VOBJ};
        entry.pushKV("slot", static_cast<int64_t>(proposal->slot));
        entry.pushKV("title", proposal->title);
        entry.pushKV("description", proposal->description);
        entry.pushKV("hash", proposal->GetHash().GetHex());
        entry.pushKV("replaces", title_of(proposal->slot));
        proposals.push_back(std::move(entry));
    }
    for (const auto& [slot, proposal_hash] : acks) {
        UniValue entry{UniValue::VOBJ};
        entry.pushKV("slot", static_cast<int64_t>(slot));
        entry.pushKV("hash", proposal_hash.GetHex());
        const auto it{ack_titles.find(proposal_hash)};
        entry.pushKV("title", it == ack_titles.end() ? std::string{} : it->second);
        ack_list.push_back(std::move(entry));
    }
    if (active) {
        for (const auto& [slot, name] : slots) {
            if (name.activation_height == height) activations.push_back(slot_entry(slot));
        }
    }
    out.pushKV("proposals", std::move(proposals));
    out.pushKV("acks", std::move(ack_list));
    out.pushKV("activations", std::move(activations));

    // --- The votes on withdrawal bundles (M4), per sidechain, against the bundles pending before the block. ---
    std::map<SidechainId, std::vector<std::pair<uint256, uint32_t>>> before;
    for (const auto& [slot, list] : prev_events.pending) before.emplace(slot, list);
    UniValue votes{UniValue::VOBJ};
    votes.pushKV("form", vote_message ? FormName(vote_message->form) : "");
    UniValue entries{UniValue::VARR};
    // `slot` unknown: the entry at that position of the message, for a sidechain that cannot be told.
    const auto vote_entry{[&](std::optional<SidechainId> slot, const std::string& vote, std::optional<uint16_t> index, const std::optional<uint256>& bundle, size_t position = 0) {
        UniValue entry{UniValue::VOBJ};
        if (slot) {
            entry = slot_entry(*slot);
        } else {
            entry.pushKV("position", static_cast<int64_t>(position));
        }
        entry.pushKV("vote", vote);
        if (index) entry.pushKV("index", *index);
        if (bundle) entry.pushKV("bundle", bundle->GetHex());
        entries.push_back(std::move(entry));
    }};
    bool known{true};
    if (vote_message && (vote_message->form == VoteForm::ONE_BYTE || vote_message->form == VoteForm::TWO_BYTES)) {
        // One entry per sidechain active before the block, in slot order (SidechainDB::GetPendingBundles). A slot
        // that a later sidechain took over is counted as it is now; a count that does not match is shown bare.
        std::vector<SidechainId> order;
        for (const auto& [slot, name] : slots) {
            if (name.activation_height < height) order.push_back(slot);
        }
        known = order.size() == vote_message->votes.size();
        for (size_t i{0}; i < vote_message->votes.size(); ++i) {
            const uint16_t v{vote_message->votes[i]};
            const std::optional<SidechainId> slot{known ? std::optional<SidechainId>{order[i]} : std::nullopt};
            if (v == drivechain::VOTE_ABSTAIN) {
                vote_entry(slot, "abstain", std::nullopt, std::nullopt, i);
            } else if (v == drivechain::VOTE_DOWNVOTE) {
                vote_entry(slot, "downvote", std::nullopt, std::nullopt, i);
            } else {
                std::optional<uint256> bundle;
                if (slot) {
                    if (const auto it{before.find(*slot)}; it != before.end() && v < it->second.size()) bundle = it->second[v].first;
                }
                vote_entry(slot, "upvote", v, bundle, i);
            }
        }
    } else if (vote_message && vote_message->form == VoteForm::LEADING_BY_50) {
        // The bundle that leads the next one by LEADING_BY_50_MARGIN, per sidechain (as SidechainDB::ConnectBlock).
        for (const auto& [slot, list] : before) {
            const std::pair<uint256, uint32_t>* leader{nullptr};
            uint32_t second{0};
            for (const auto& b : list) {
                if (!leader || b.second > leader->second) {
                    if (leader) second = std::max(second, leader->second);
                    leader = &b;
                } else {
                    second = std::max(second, b.second);
                }
            }
            if (leader && leader->second >= second + drivechain::LEADING_BY_50_MARGIN) vote_entry(slot, "upvote", std::nullopt, leader->first);
        }
    } else if (vote_message && vote_message->form == VoteForm::REPEAT_PREVIOUS) {
        // The votes of the block before, kept with the undo data of the recent blocks.
        known = have_dc_undo;
        if (have_dc_undo) {
            for (const auto& [slot, vote] : dc_undo.last_votes) {
                if (vote.type == drivechain::Vote::Type::UPVOTE) vote_entry(slot, "upvote", std::nullopt, vote.bundle);
                else if (vote.type == drivechain::Vote::Type::DOWNVOTE) vote_entry(slot, "downvote", std::nullopt, std::nullopt);
            }
        }
    }
    votes.pushKV("known", known);
    votes.pushKV("entries", std::move(entries));
    out.pushKV("votes", std::move(votes));

    // --- Withdrawal bundles: proposed (M3, those that became pending), closed (paid out or failed), pending after. ---
    UniValue proposed{UniValue::VARR}, closed{UniValue::VARR}, pending{UniValue::VARR};
    for (const auto& [slot, bundle_hash] : events.proposed) {
        UniValue entry{slot_entry(slot)};
        entry.pushKV("hash", bundle_hash.GetHex());
        proposed.push_back(std::move(entry));
    }
    for (const auto& c : events.closed) {
        UniValue entry{slot_entry(c.id)};
        entry.pushKV("hash", c.hash.GetHex());
        entry.pushKV("paid", c.paid);
        closed.push_back(std::move(entry));
    }
    size_t listed{0};
    for (const auto& [slot, list] : events.pending) {
        UniValue entry{slot_entry(slot)};
        UniValue bundles{UniValue::VARR};
        for (const auto& [bundle_hash, score] : list) {
            if (listed++ >= MAX_LISTED) break;
            UniValue b{UniValue::VOBJ};
            b.pushKV("hash", bundle_hash.GetHex());
            b.pushKV("score", static_cast<int64_t>(score));
            bundles.push_back(std::move(b));
        }
        entry.pushKV("count", static_cast<int64_t>(list.size()));
        entry.pushKV("bundles", std::move(bundles));
        pending.push_back(std::move(entry));
    }
    out.pushKV("events", have_events || height == 0);
    out.pushKV("proposed", std::move(proposed));
    out.pushKV("closed", std::move(closed));
    out.pushKV("pending", std::move(pending));

    // --- Escrow changes: deposits (M5) and withdrawals (M6), in block order. ---
    std::sort(changes.begin(), changes.end(), [](const auto& a, const auto& b) { return a.tx_index < b.tx_index; });
    UniValue escrow{UniValue::VARR};
    for (size_t i{0}; i < changes.size() && i < MAX_LISTED; ++i) {
        const drivechain::Deposit& change{changes[i]};
        UniValue entry{slot_entry(change.slot)};
        const bool withdrawal{change.destination == drivechain::WITHDRAWAL_RETURN_DEST};
        entry.pushKV("kind", withdrawal ? "withdrawal" : "deposit");
        entry.pushKV("txid", change.tx->GetHash().GetHex());
        entry.pushKV("amount", change.amount);
        entry.pushKV("total", change.total);
        if (withdrawal) {
            entry.pushKV("bundle", change.bundle.GetHex());
            CAmount paid{0};
            int64_t count{0};
            for (size_t n{0}; n < change.tx->vout.size(); ++n) {
                if (n == change.burn_index || change.tx->vout[n].nValue == 0) continue;
                paid += change.tx->vout[n].nValue;
                ++count;
            }
            entry.pushKV("paid", paid);
            entry.pushKV("payouts", count);
            if (const auto fee{fee_of(change.tx_index)}) entry.pushKV("fee", *fee);
        } else {
            entry.pushKV("destination", change.destination);
            // A destination that is an address of this chain (the explorer links it); a sidechain's own address is not.
            if (IsValidDestinationString(change.destination)) entry.pushKV("mainchain", true);
        }
        escrow.push_back(std::move(entry));
    }
    out.pushKV("escrow", std::move(escrow));
    out.pushKV("escrowCount", static_cast<int64_t>(changes.size()));
    return out;
}

} // namespace webui::explorer
