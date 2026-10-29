// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/miner.h>

#include <chain.h>
#include <chainparams.h>
#include <common/args.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <drivechain/db.h>
#include <drivechain/miner.h>
#include <consensus/params.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <interfaces/types.h>
#include <node/blockstorage.h>
#include <node/kernel_notifications.h>
#include <node/mining_args.h>
#include <node/mining_types.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <pow.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <serialize.h>
#include <sync.h>
#include <tinyformat.h>
#include <txgraph.h>
#include <txmempool.h>
#include <uint256.h>
#include <util/check.h>
#include <util/feefrac.h>
#include <util/log.h>
#include <util/result.h>
#include <util/signalinterrupt.h>
#include <util/time.h>
#include <util/translation.h>
#include <validation.h>
#include <validationinterface.h>
#include <versionbits.h>

#include <algorithm>
#include <compare>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace node {

int64_t GetMinimumTime(const CBlockIndex* pindexPrev, const int64_t difficulty_adjustment_interval)
{
    int64_t min_time{pindexPrev->GetMedianTimePast() + 1};
    // Height of block to be mined.
    const int height{pindexPrev->nHeight + 1};
    // Account for BIP94 timewarp rule on all networks. This makes future
    // activation safer.
    if (height % difficulty_adjustment_interval == 0) {
        min_time = std::max<int64_t>(min_time, pindexPrev->GetBlockTime() - MAX_TIMEWARP);
    }
    // Account for the BIP54 Murch-Zawy rule on all networks: the last block of
    // a difficulty adjustment period may not be earlier than its first block.
    if (height % difficulty_adjustment_interval == difficulty_adjustment_interval - 1) {
        const int first_height{height - static_cast<int>(difficulty_adjustment_interval) + 1};
        const CBlockIndex* first_block{Assert(pindexPrev->GetAncestor(first_height))};
        min_time = std::max<int64_t>(min_time, first_block->GetBlockTime());
    }
    return min_time;
}

int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime{std::max<int64_t>(GetMinimumTime(pindexPrev, consensusParams.DifficultyAdjustmentInterval(pindexPrev->nHeight + 1)),
                                       TicksSinceEpoch<std::chrono::seconds>(NodeClock::now()))};

    if (nOldTime < nNewTime) {
        pblock->nTime = nNewTime;
    }

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks) {
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);
    }

    return nNewTime - nOldTime;
}

void RegenerateCommitments(CBlock& block, ChainstateManager& chainman)
{
    CMutableTransaction tx{*block.vtx.at(0)};
    tx.vout.erase(tx.vout.begin() + GetWitnessCommitmentIndex(block));
    block.vtx.at(0) = MakeTransactionRef(tx);

    const CBlockIndex* prev_block = WITH_LOCK(::cs_main, return chainman.m_blockman.LookupBlockIndex(block.hashPrevBlock));
    chainman.GenerateCoinbaseCommitment(block, prev_block);

    block.hashMerkleRoot = BlockMerkleRoot(block);
}

BlockAssembler::BlockAssembler(Chainstate& chainstate,
                               const CTxMemPool* mempool,
                               BlockCreateOptions options)
    : chainparams{chainstate.m_chainman.GetParams()},
      m_mempool{options.use_mempool ? mempool : nullptr},
      m_chainstate{chainstate},
      m_options{[&] {
          if (auto result{CheckMiningOptions(options, /*use_argnames=*/false)}; !result) {
              throw std::runtime_error(util::ErrorString(result).original);
          }
          // Unless told otherwise, blocks are as large as the chain allows.
          const bool default_weight{!options.block_max_weight};
          BlockCreateOptions flattened{FlattenMiningOptions(std::move(options))};
          // Transactions other than the coinbase have their own weight limit on
          // top of the limit for the whole block.
          const Consensus::Params& consensus{chainstate.m_chainman.GetConsensus()};
          const uint64_t limit{std::min<uint64_t>(consensus.max_block_weight, uint64_t{consensus.max_block_tx_weight} + *flattened.block_reserved_weight)};
          if (default_weight || *flattened.block_max_weight > limit) flattened.block_max_weight = limit;
          return flattened;
      }()}
{
}

void BlockAssembler::resetBlock()
{
    // Reserve space for fixed-size block header, txs count, and coinbase tx.
    nBlockWeight = *Assert(m_options.block_reserved_weight);
    nBlockSigOpsCost = m_options.coinbase_output_max_additional_sigops;

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nFees = 0;
    m_withdrawal_weight = 0;
    m_withdrawal_sigops = 0;
    m_consolidation_weight = 0;
    m_in_block.clear();
    m_coinbase_message_weight = 0;
    m_bmm_accepted.clear();
    m_withdrawal_slots.clear();
}

namespace {
/** The weight `outputs` add to the coinbase, with the growth of its output count (from the payout and the witness commitment). */
uint64_t CoinbaseOutputsWeight(const std::vector<CTxOut>& outputs)
{
    if (outputs.empty()) return 0;
    uint64_t size{0};
    for (const CTxOut& out : outputs) size += ::GetSerializeSize(out);
    size += GetSizeOfCompactSize(2 + outputs.size()) - GetSizeOfCompactSize(2);
    return size * WITNESS_SCALE_FACTOR;
}
} // namespace

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock()
{
    const auto time_start{SteadyClock::now()};

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());
    CBlock* const pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction. It is skipped by the
    // getblocktemplate RPC and mining interface consumers must not use it.
    pblock->vtx.emplace_back();

    LOCK(::cs_main);
    CBlockIndex* pindexPrev = m_chainstate.m_chain.Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    pblock->nVersion = m_chainstate.m_chainman.m_versionbitscache.ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());
    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand()) {
        pblock->nVersion = gArgs.GetIntArg("-blockversion", pblock->nVersion);
    }

    pblock->nTime = TicksSinceEpoch<std::chrono::seconds>(NodeClock::now());
    m_lock_time_cutoff = pindexPrev->GetMedianTimePast();
    m_aggregated_deposits = chainparams.GetConsensus().drivechain.AggregatedDeposits(nHeight);

    // Drivechain: the messages this node puts in the coinbase, and the
    // withdrawal bundles that can be paid out. Withdrawals go last, so that
    // they spend the escrow output as the deposits in the block leave it.
    ChainstateManager& chainman{m_chainstate.m_chainman};
    // What the active chain closed, for good: a failed bundle the sidechain database forgot is not proposed again.
    const auto history{[&](drivechain::SidechainId slot, const uint256& hash) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) -> std::optional<int> {
        const auto closure{chainman.m_blockman.m_drivechain_db->FindClosure(slot, hash, [&](const uint256& block_hash) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) {
            const CBlockIndex* index{chainman.m_blockman.LookupBlockIndex(block_hash)};
            return index && m_chainstate.m_chain.Contains(*index);
        })};
        if (!closure) return std::nullopt;
        return closure->height;
    }};
    chainman.m_drivechain_miner.Prune(m_chainstate.m_scdb, pindexPrev->nHeight + 1, history);
    const auto additions_for{[&](const std::vector<CTransactionRef>& txs) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) {
        return chainman.m_drivechain_miner.CreateBlockAdditions(m_chainstate.m_scdb, chainparams.GetConsensus().drivechain,
                                                                pindexPrev->GetBlockHash(), txs, pindexPrev->nHeight + 1);
    }};
    const auto block_txs{[&] { return std::vector<CTransactionRef>{pblock->vtx.begin() + 1, pblock->vtx.end()}; }};

    // The messages of the coinbase take room in the block (the acks, the votes, the proposals, the accepts of the BMM
    // requests the block takes, counted as they are taken): block_reserved_weight is for the rest of it.
    const drivechain::BlockAdditions before{additions_for({})};
    m_coinbase_message_weight = CoinbaseOutputsWeight(before.coinbase_outputs);
    nBlockWeight += m_coinbase_message_weight;

    if (m_mempool) {
        LOCK(m_mempool->cs);
        // Room is kept for the withdrawals that can be paid out, so that a full mempool cannot keep
        // them out of the block, whatever their fee. Their weight does not depend on the escrow
        // output they spend.
        std::tie(m_withdrawal_weight, m_withdrawal_sigops) = WithdrawalRoom(before.withdrawals);
        m_payable_bundles = before.payable_bundles;
        // A sidechain the coinbase activates is active for the block's transactions (consensus applies the coinbase
        // first): what the mempool took, while it was not, for an ordinary output with its treasury script may be an
        // invalid deposit now, or one the block would have to consolidate. Such transactions wait for the next block.
        const drivechain::SidechainDB after_coinbase{drivechain::StateAfterCoinbase(m_chainstate.m_scdb, chainparams.GetConsensus().drivechain,
                                                                                    before.coinbase_outputs, nHeight)};
        for (const auto& [slot, state] : after_coinbase.GetSlots()) {
            if (!m_chainstate.m_scdb.IsActive(slot)) m_activating_slots.insert(slot);
        }
        m_mempool->StartBlockBuilding();
        addChunks();
        m_mempool->StopBlockBuilding();
        // Room the withdrawals do not take after all (one the block has from the mempool already,
        // one the escrow output no longer pays) goes to transactions: a second pass over the
        // mempool, which takes the chunks the kept room left out.
        if (const auto room{WithdrawalRoom(additions_for(block_txs()).withdrawals)};
            room.first < m_withdrawal_weight || room.second < m_withdrawal_sigops) {
            std::tie(m_withdrawal_weight, m_withdrawal_sigops) = room;
            m_mempool->StartBlockBuilding();
            addChunks();
            m_mempool->StopBlockBuilding();
        }
        m_withdrawal_weight = 0;
        m_withdrawal_sigops = 0;
        m_payable_bundles.clear();
        m_activating_slots.clear();
    }

    const drivechain::BlockAdditions drivechain_additions{additions_for(block_txs())};
    // A template that is asked not to take transactions from the mempool gets none from here either.
    for (size_t i{0}; m_mempool && i < drivechain_additions.withdrawals.size(); ++i) {
        const CTransactionRef& withdrawal{drivechain_additions.withdrawals[i]};
        // A withdrawal that does not fit waits for the next block (the room of the consolidation stays kept).
        if (nBlockWeight + m_consolidation_weight + GetTransactionWeight(*withdrawal) >= *Assert(m_options.block_max_weight)) continue;
        if (nBlockSigOpsCost + WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*withdrawal) >= MAX_BLOCK_SIGOPS_COST) continue;
        pblock->vtx.push_back(withdrawal);
        pblocktemplate->vTxFees.push_back(drivechain_additions.withdrawal_fees[i]);
        pblocktemplate->vTxSigOpsCost.push_back(WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*withdrawal));
        nBlockWeight += GetTransactionWeight(*withdrawal);
        pblocktemplate->m_withdrawal_weight += GetTransactionWeight(*withdrawal);
        nBlockSigOpsCost += pblocktemplate->vTxSigOpsCost.back();
        nFees += drivechain_additions.withdrawal_fees[i];
        ++nBlockTx;
    }
    // Aggregated deposits: the deposits of the block that did not spend the Ctip of their sidechain are consolidated
    // last, by a transaction that spends the Ctip (as the withdrawals left it) and them (as the bip300301_enforcer's
    // block producer does); without it the block is invalid. Its room was kept (m_consolidation_weight). No fee, no sigops.
    if (m_aggregated_deposits) {
        for (const CTransactionRef& consolidation : drivechain::BlockConsolidations(m_chainstate.m_scdb, chainparams.GetConsensus().drivechain,
                                                                                    block_txs(), nHeight)) {
            pblock->vtx.push_back(consolidation);
            pblocktemplate->vTxFees.push_back(0);
            pblocktemplate->vTxSigOpsCost.push_back(WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*consolidation));
            nBlockWeight += GetTransactionWeight(*consolidation);
            pblocktemplate->m_withdrawal_weight += GetTransactionWeight(*consolidation);
            nBlockSigOpsCost += pblocktemplate->vTxSigOpsCost.back();
            ++nBlockTx;
        }
        m_consolidation_weight = 0;
    }

    const auto time_1{SteadyClock::now()};

    m_last_block_num_txs = nBlockTx;
    m_last_block_weight = nBlockWeight;

    // Create coinbase transaction.
    CMutableTransaction coinbaseTx;

    // Construct coinbase transaction struct in parallel
    CoinbaseTx& coinbase_tx{pblocktemplate->m_coinbase_tx};
    coinbase_tx.version = coinbaseTx.version;

    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vin[0].nSequence = CTxIn::MAX_SEQUENCE_NONFINAL; // Make sure timelock is enforced.
    coinbase_tx.sequence = coinbaseTx.vin[0].nSequence;

    // Add an output that spends the full coinbase reward.
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].scriptPubKey = m_options.coinbase_output_script;
    // Block subsidy + fees
    const CAmount block_reward{nFees + GetBlockSubsidy(nHeight, chainparams.GetConsensus())};
    coinbaseTx.vout[0].nValue = block_reward;
    coinbase_tx.block_reward_remaining = block_reward;
    // Drivechain messages. Mining clients that build their own coinbase have to include them.
    for (const CTxOut& message : drivechain_additions.coinbase_outputs) {
        coinbaseTx.vout.push_back(message);
    }

    // Start the coinbase scriptSig with the block height as required by BIP34.
    // Mining clients are expected to append extra data to this prefix, so
    // increasing its length would reduce the space they can use and may break
    // existing clients.
    coinbaseTx.vin[0].scriptSig = CScript() << nHeight;
    // Set script_sig_prefix here, so IPC mining clients are not affected by
    // the optional scriptSig padding below. They provide their own extraNonce,
    // and in a typical setup a pool name or realistic extraNonce already makes
    // the scriptSig long enough.
    coinbase_tx.script_sig_prefix = coinbaseTx.vin[0].scriptSig;
    if (nHeight <= 16) {
        // For blocks at heights <= 16, the BIP34-encoded height alone is only
        // one byte. Consensus requires coinbase scriptSigs to be at least two
        // bytes long (bad-cb-length), so an OP_0 is always appended at those
        // heights.
        coinbaseTx.vin[0].scriptSig << OP_0;
    }
    Assert(nHeight > 0);
    coinbaseTx.nLockTime = static_cast<uint32_t>(nHeight - 1);
    coinbase_tx.lock_time = coinbaseTx.nLockTime;

    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));
    m_chainstate.m_chainman.GenerateCoinbaseCommitment(*pblock, pindexPrev);

    const CTransactionRef& final_coinbase{pblock->vtx[0]};
    if (final_coinbase->HasWitness()) {
        const auto& witness_stack{final_coinbase->vin[0].scriptWitness.stack};
        // Consensus requires the coinbase witness stack to have exactly one
        // element of 32 bytes.
        Assert(witness_stack.size() == 1 && witness_stack[0].size() == 32);
        coinbase_tx.witness = uint256(witness_stack[0]);
    }
    if (const int witness_index = GetWitnessCommitmentIndex(*pblock); witness_index != NO_WITNESS_COMMITMENT) {
        Assert(witness_index >= 0 && static_cast<size_t>(witness_index) < final_coinbase->vout.size());
        coinbase_tx.required_outputs.push_back(final_coinbase->vout[witness_index]);
    }
    coinbase_tx.required_outputs.insert(coinbase_tx.required_outputs.end(), drivechain_additions.coinbase_outputs.begin(), drivechain_additions.coinbase_outputs.end());

    LogDebug(BCLog::MINING, "CreateNewBlock(): block weight: %u txs: %u fees: %ld sigops %d\n", GetBlockWeight(*pblock), nBlockTx, nFees, nBlockSigOpsCost);

    // Fill in header
    pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
    UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
    pblock->nBits          = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
    pblock->nNonce         = 0;

    if (m_options.test_block_validity) {
        if (BlockValidationState state{TestBlockValidity(m_chainstate, *pblock, /*check_pow=*/false, /*check_merkle_root=*/false)}; !state.IsValid()) {
            throw std::runtime_error(strprintf("TestBlockValidity failed: %s", state.ToString()));
        }
    }
    const auto time_2{SteadyClock::now()};

    LogDebug(BCLog::BENCH, "CreateNewBlock() chunks: %.2fms, validity: %.2fms (total %.2fms)\n",
             Ticks<MillisecondsDouble>(time_1 - time_start),
             Ticks<MillisecondsDouble>(time_2 - time_1),
             Ticks<MillisecondsDouble>(time_2 - time_start));

    return std::move(pblocktemplate);
}

std::pair<uint64_t, uint64_t> BlockAssembler::WithdrawalRoom(const std::vector<CTransactionRef>& withdrawals) const
{
    uint64_t weight{0};
    uint64_t sigops{0};
    for (const CTransactionRef& withdrawal : withdrawals) {
        const uint64_t tx_weight(GetTransactionWeight(*withdrawal));
        const uint64_t tx_sigops(WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*withdrawal));
        if (*m_options.block_reserved_weight + m_coinbase_message_weight + weight + tx_weight >= *Assert(m_options.block_max_weight)) continue;
        if (m_options.coinbase_output_max_additional_sigops + sigops + tx_sigops >= MAX_BLOCK_SIGOPS_COST) continue;
        weight += tx_weight;
        sigops += tx_sigops;
    }
    return {weight, sigops};
}

bool BlockAssembler::TestChunkBlockLimits(int64_t chunk_weight, int64_t chunk_sigops_cost) const
{
    // block_max_weight has been flattened before block assembly limit checks.
    Assert(m_options.block_max_weight);
    // The room kept for the withdrawals and the consolidation counts as taken.
    if (nBlockWeight + m_withdrawal_weight + m_consolidation_weight + chunk_weight >= m_options.block_max_weight) {
        return false;
    }
    if (nBlockSigOpsCost + m_withdrawal_sigops + chunk_sigops_cost >= MAX_BLOCK_SIGOPS_COST) {
        return false;
    }
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
bool BlockAssembler::TestChunkTransactions(const std::vector<CTxMemPoolEntryRef>& txs) const
{
    std::set<drivechain::SidechainId> withdrawal_slots{m_withdrawal_slots};
    for (const auto tx : txs) {
        if (!IsFinalTx(tx.get().GetTx(), nHeight, m_lock_time_cutoff, chainparams.GetConsensus().replay_locktime)) {
            return false;
        }
        // Skipping the chunk skips the rest of its cluster, so the transactions that depend on the
        // request stay out as well.
        if (!m_options.include_bmm_requests && drivechain::GetBmmRequest(tx.get().GetTx())) {
            return false;
        }
        // A withdrawal of a bundle this block's own votes (or its age) leave without the score would
        // make the block invalid; it and what depends on it wait. So does a second one of a sidechain.
        if (!WithdrawalPayable(tx.get().GetTx(), withdrawal_slots)) return false;
        if (TouchesActivatingSlot(tx.get().GetTx())) return false;
    }
    return true;
}

uint64_t BlockAssembler::ConsolidationRoom(const CTransaction& tx) const
{
    if (!m_aggregated_deposits) return 0;
    // Every treasury output: one that does not spend the Ctip is consolidated (an M6 or a deposit that spends it
    // is not, which this over-reserves, as the enforcer's mempool does).
    const size_t treasuries{static_cast<size_t>(std::count_if(tx.vout.begin(), tx.vout.end(), [](const CTxOut& out) {
        return drivechain::ParseEscrowScript(out.scriptPubKey).has_value();
    }))};
    return treasuries == 0 ? 0 : static_cast<uint64_t>(drivechain::ConsolidationWeight(treasuries));
}

uint64_t BlockAssembler::BmmAcceptWeight(const CTransaction& tx, std::set<drivechain::SidechainId>& accepted) const
{
    uint64_t weight{0};
    for (const drivechain::BmmRequest& request : drivechain::GetBmmRequests(tx)) {
        if (request.slot >= chainparams.GetConsensus().drivechain.max_sidechains) continue;
        if (!accepted.insert(request.slot).second) continue;
        weight += ::GetSerializeSize(CTxOut{0, drivechain::BmmAcceptScript(request.slot, request.side_block_hash)}) * WITNESS_SCALE_FACTOR;
    }
    // The output count of the coinbase may take more bytes as well.
    return weight == 0 ? 0 : weight + 2 * WITNESS_SCALE_FACTOR;
}

bool BlockAssembler::TouchesActivatingSlot(const CTransaction& tx) const
{
    if (m_activating_slots.empty()) return false;
    const auto activating{[&](const CScript& script) {
        const auto slot{drivechain::ParseEscrowScript(script)};
        return slot && m_activating_slots.contains(*slot);
    }};
    if (std::any_of(tx.vout.begin(), tx.vout.end(), [&](const CTxOut& out) { return activating(out.scriptPubKey); })) return true;
    return std::any_of(tx.vin.begin(), tx.vin.end(), [&](const CTxIn& in) EXCLUSIVE_LOCKS_REQUIRED(::cs_main, m_mempool->cs) {
        if (const CTransactionRef parent{m_mempool->get(in.prevout.hash)}) {
            return in.prevout.n < parent->vout.size() && activating(parent->vout[in.prevout.n].scriptPubKey);
        }
        const auto coin{m_chainstate.CoinsTip().GetCoin(in.prevout)};
        return coin && activating(coin->out.scriptPubKey);
    });
}

std::optional<std::pair<drivechain::SidechainId, uint256>> BlockAssembler::WithdrawalOf(const CTransaction& tx) const
{
    // A withdrawal (M6): the treasury output its only input, the new treasury output 0, less in it.
    if (tx.vin.size() != 1 || tx.vout.empty()) return std::nullopt;
    const auto slot{drivechain::ParseEscrowScript(tx.vout[0].scriptPubKey)};
    if (!slot) return std::nullopt;
    const COutPoint& prevout{tx.vin[0].prevout};
    std::optional<CTxOut> spent;
    if (const CTransactionRef parent{m_mempool->get(prevout.hash)}) {
        if (prevout.n < parent->vout.size()) spent = parent->vout[prevout.n];
    } else if (const auto coin{m_chainstate.CoinsTip().GetCoin(prevout)}) {
        spent = coin->out;
    }
    if (!spent || drivechain::ParseEscrowScript(spent->scriptPubKey) != slot || tx.vout[0].nValue >= spent->nValue) return std::nullopt;
    const auto bundle{drivechain::BlindWithdrawalHash(tx, spent->nValue)};
    if (!bundle) return std::nullopt; // not a withdrawal consensus would take either; the mempool has none such
    return std::make_pair(*slot, *bundle);
}

bool BlockAssembler::WithdrawalPayable(const CTransaction& tx, std::set<drivechain::SidechainId>& withdrawal_slots) const
{
    const auto withdrawal{WithdrawalOf(tx)};
    if (!withdrawal) return true;
    const auto& [slot, bundle]{*withdrawal};
    const auto payable{m_payable_bundles.find(slot)};
    if (payable == m_payable_bundles.end() || !payable->second.contains(bundle)) return false;
    // One withdrawal per sidechain per block, as the template's own (the policy of a single payout).
    return withdrawal_slots.insert(slot).second;
}

void BlockAssembler::AddToBlock(const CTxMemPoolEntry& entry)
{
    pblocktemplate->block.vtx.emplace_back(entry.GetSharedTx());
    pblocktemplate->vTxFees.push_back(entry.GetFee());
    pblocktemplate->vTxSigOpsCost.push_back(entry.GetSigOpCost());
    m_in_block.insert(entry.GetTx().GetHash());
    if (const auto withdrawal{WithdrawalOf(entry.GetTx())}) m_withdrawal_slots.insert(withdrawal->first);
    m_consolidation_weight += ConsolidationRoom(entry.GetTx());
    nBlockWeight += entry.GetTxWeight() + BmmAcceptWeight(entry.GetTx(), m_bmm_accepted);
    ++nBlockTx;
    nBlockSigOpsCost += entry.GetSigOpCost();
    nFees += entry.GetFee();

    if (*m_options.print_modified_fee) {
        LogInfo("fee rate %s txid %s\n",
                  CFeeRate(entry.GetModifiedFee(), entry.GetTxSize()).ToString(),
                  entry.GetTx().GetHash().ToString());
    }
}

void BlockAssembler::addChunks()
{
    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    constexpr int32_t BLOCK_FULL_ENOUGH_WEIGHT_DELTA = 4000;
    int64_t nConsecutiveFailed = 0;

    std::vector<CTxMemPoolEntry::CTxMemPoolEntryRef> selected_transactions;
    selected_transactions.reserve(MAX_CLUSTER_COUNT_LIMIT);
    FeePerWeight chunk_feerate;

    // This fills selected_transactions
    chunk_feerate = m_mempool->GetBlockBuilderChunk(selected_transactions);
    FeePerVSize chunk_feerate_vsize = ToFeePerVSize(chunk_feerate);

    while (selected_transactions.size() > 0) {
        // Check to see if min fee rate is still respected.
        if (ByRatio{chunk_feerate_vsize} < ByRatio{m_options.block_min_fee_rate->GetFeePerVSize()}) {
            // Everything else we might consider has a lower feerate
            return;
        }

        // A second pass goes over the chunks of the first one again: those the block has stay in it.
        if (m_in_block.contains(selected_transactions.front().get().GetTx().GetHash())) {
            m_mempool->IncludeBuilderChunk();
            selected_transactions.clear();
            chunk_feerate = m_mempool->GetBlockBuilderChunk(selected_transactions);
            chunk_feerate_vsize = ToFeePerVSize(chunk_feerate);
            continue;
        }

        int64_t chunk_sig_ops = 0;
        int64_t chunk_weight = 0;
        for (const auto& tx : selected_transactions) {
            chunk_sig_ops += tx.get().GetSigOpCost();
            // With the room its deposits take in the consolidation of the block.
            chunk_weight += tx.get().GetTxWeight() + ConsolidationRoom(tx.get().GetTx());
        }
        // And the accepts of its BMM requests, in the coinbase.
        std::set<drivechain::SidechainId> accepted{m_bmm_accepted};
        for (const auto& tx : selected_transactions) chunk_weight += BmmAcceptWeight(tx.get().GetTx(), accepted);

        // Check to see if this chunk will fit.
        if (!TestChunkBlockLimits(chunk_weight, chunk_sig_ops) || !TestChunkTransactions(selected_transactions)) {
            // This chunk won't fit, so we skip it and will try the next best one.
            m_mempool->SkipBuilderChunk();
            ++nConsecutiveFailed;

            // block_max_weight has been flattened before block assembly limit checks.
            Assert(m_options.block_max_weight);
            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockWeight + m_withdrawal_weight +
                    BLOCK_FULL_ENOUGH_WEIGHT_DELTA > *m_options.block_max_weight) {
                // Give up if we're close to full and haven't succeeded in a while
                return;
            }
        } else {
            m_mempool->IncludeBuilderChunk();

            // This chunk will fit, so add it to the block.
            nConsecutiveFailed = 0;
            for (const auto& tx : selected_transactions) {
                AddToBlock(tx);
            }
            pblocktemplate->m_package_feerates.emplace_back(chunk_feerate_vsize);
        }

        selected_transactions.clear();
        chunk_feerate = m_mempool->GetBlockBuilderChunk(selected_transactions);
        chunk_feerate_vsize = ToFeePerVSize(chunk_feerate);
    }
}

void AddMerkleRootAndCoinbase(CBlock& block, CTransactionRef coinbase, uint32_t version, uint32_t timestamp, uint32_t nonce)
{
    if (block.vtx.size() == 0) {
        block.vtx.emplace_back(coinbase);
    } else {
        block.vtx[0] = coinbase;
    }
    block.nVersion = version;
    block.nTime = timestamp;
    block.nNonce = nonce;
    block.hashMerkleRoot = BlockMerkleRoot(block);

    // Reset cached checks
    block.m_checked_witness_commitment = false;
    block.m_checked_merkle_root = false;
    block.fChecked = false;
}

namespace {
class SubmitBlockStateCatcher final : public CValidationInterface
{
public:
    uint256 m_hash;
    bool m_found{false};
    BlockValidationState m_state;

    explicit SubmitBlockStateCatcher(const uint256& hash) : m_hash{hash} {}

protected:
    void BlockChecked(const std::shared_ptr<const CBlock>& block, const BlockValidationState& state) override
    {
        if (block->GetHash() != m_hash) return;
        // ProcessNewBlock emits BlockChecked synchronously while holding cs_main,
        // so SubmitBlock can read these fields after ProcessNewBlock returns
        // without extra synchronization.
        m_found = true;
        m_state = state;
    }
};
} // namespace

bool SubmitBlock(ChainstateManager& chainman, const std::shared_ptr<const CBlock>& block, std::string& reason, std::string& debug)
{
    reason.clear();
    debug.clear();

    // This follows the submitblock RPC's validation-state capture pattern, but
    // is intentionally kept separate from the RPC implementation. The RPC entry
    // point decodes hex, formats BIP22/JSONRPC results, and calls
    // UpdateUncommittedBlockStructures() for legacy witness handling. IPC
    // callers submit already-formed blocks and need bool + reason/debug
    // results.
    auto sc = std::make_shared<SubmitBlockStateCatcher>(block->GetHash());
    CHECK_NONFATAL(chainman.m_options.signals)->RegisterSharedValidationInterface(sc);
    bool new_block;
    bool accepted = chainman.ProcessNewBlock(block, /*force_processing=*/true, /*min_pow_checked=*/true, /*new_block=*/&new_block);
    // No queue drain is needed. The BlockChecked notification used above is
    // emitted synchronously by ProcessNewBlock, unlike most validation signals.
    CHECK_NONFATAL(chainman.m_options.signals)->UnregisterSharedValidationInterface(sc);

    if (!new_block && accepted) {
        reason = "duplicate";
    } else if (!accepted && (!sc->m_found || sc->m_state.IsValid())) {
        // ProcessNewBlock can fail without a validation result, for example
        // from an activation or system error. It can also fail after a valid
        // BlockChecked result. In these cases the validation result is
        // inconclusive.
        reason = "inconclusive";
    } else if (!sc->m_found) {
        // The block was accepted but not connected, for example if it does not
        // have more work than the current tip.
        reason = "inconclusive";
    } else if (!sc->m_state.IsValid()) {
        reason = sc->m_state.GetRejectReason();
        debug = sc->m_state.GetDebugMessage();
    }
    const bool result{accepted && new_block && reason.empty()};
    CHECK_NONFATAL(result == reason.empty());
    return result;
}

void InterruptWait(KernelNotifications& kernel_notifications, bool& interrupt_wait)
{
    LOCK(kernel_notifications.m_tip_block_mutex);
    interrupt_wait = true;
    kernel_notifications.m_tip_block_cv.notify_all();
}

std::unique_ptr<CBlockTemplate> WaitAndCreateNewBlock(ChainstateManager& chainman,
                                                      KernelNotifications& kernel_notifications,
                                                      CTxMemPool* mempool,
                                                      const std::unique_ptr<CBlockTemplate>& block_template,
                                                      const BlockWaitOptions& wait_options,
                                                      const BlockCreateOptions& create_options,
                                                      bool& interrupt_wait)
{
    // Delay calculating the current template fees, just in case a new block
    // comes in before the next tick.
    CAmount current_fees = -1;

    // Alternate waiting for a new tip and checking if fees have risen.
    // The latter check is expensive so we only run it once per second.
    auto now{NodeClock::now()};
    const auto deadline = now + wait_options.timeout;
    const MillisecondsDouble tick{1000};
    const bool allow_min_difficulty{chainman.GetParams().GetConsensus().fPowAllowMinDifficultyBlocks};

    do {
        bool tip_changed{false};
        {
            WAIT_LOCK(kernel_notifications.m_tip_block_mutex, lock);
            // Note that wait_until() checks the predicate before waiting
            kernel_notifications.m_tip_block_cv.wait_until(lock, std::min(now + tick, deadline), [&]() EXCLUSIVE_LOCKS_REQUIRED(kernel_notifications.m_tip_block_mutex) {
                AssertLockHeld(kernel_notifications.m_tip_block_mutex);
                const auto tip_block{kernel_notifications.TipBlock()};
                // We assume tip_block is set, because this is an instance
                // method on BlockTemplate and no template could have been
                // generated before a tip exists.
                tip_changed = Assume(tip_block) && tip_block != block_template->block.hashPrevBlock;
                return tip_changed || chainman.m_interrupt || interrupt_wait;
            });
            if (interrupt_wait) {
                interrupt_wait = false;
                return nullptr;
            }
        }

        if (chainman.m_interrupt) return nullptr;
        // At this point the tip changed, a full tick went by or we reached
        // the deadline.

        // Must release m_tip_block_mutex before locking cs_main, to avoid deadlocks.
        LOCK(::cs_main);

        // On test networks return a minimum difficulty block after 20 minutes
        if (!tip_changed && allow_min_difficulty) {
            const NodeClock::time_point tip_time{std::chrono::seconds{chainman.ActiveChain().Tip()->GetBlockTime()}};
            if (now > tip_time + 20min) {
                tip_changed = true;
            }
        }

        /**
         * We determine if fees increased compared to the previous template by generating
         * a fresh template. There may be more efficient ways to determine how much
         * (approximate) fees for the next block increased, perhaps more so after
         * Cluster Mempool.
         *
         * We'll also create a new template if the tip changed during this iteration.
         */
        if (wait_options.fee_threshold < MAX_MONEY || tip_changed) {
            auto new_tmpl{BlockAssembler{
                chainman.ActiveChainstate(),
                mempool,
                create_options
                }.CreateNewBlock()};

            // If the tip changed, return the new template regardless of its fees.
            if (tip_changed) return new_tmpl;

            // Calculate the original template total fees if we haven't already
            if (current_fees == -1) {
                current_fees = std::accumulate(block_template->vTxFees.begin(), block_template->vTxFees.end(), CAmount{0});
            }

            // Check if fees increased enough to return the new template
            const CAmount new_fees = std::accumulate(new_tmpl->vTxFees.begin(), new_tmpl->vTxFees.end(), CAmount{0});
            Assume(wait_options.fee_threshold != MAX_MONEY);
            if (new_fees >= current_fees + wait_options.fee_threshold) return new_tmpl;
        }

        now = NodeClock::now();
    } while (now < deadline);

    return nullptr;
}

std::optional<BlockRef> GetTip(ChainstateManager& chainman)
{
    LOCK(::cs_main);
    CBlockIndex* tip{chainman.ActiveChain().Tip()};
    if (!tip) return {};
    return BlockRef{tip->GetBlockHash(), tip->nHeight};
}

bool CooldownIfHeadersAhead(ChainstateManager& chainman, KernelNotifications& kernel_notifications, const BlockRef& last_tip, bool& interrupt_mining)
{
    uint256 last_tip_hash{last_tip.hash};

    while (const std::optional<int> remaining = chainman.BlocksAheadOfTip()) {
        const int cooldown_seconds = std::clamp(*remaining, 3, 20);
        const auto cooldown_deadline{MockableSteadyClock::now() + std::chrono::seconds{cooldown_seconds}};

        {
            WAIT_LOCK(kernel_notifications.m_tip_block_mutex, lock);
            kernel_notifications.m_tip_block_cv.wait_until(lock, cooldown_deadline, [&]() EXCLUSIVE_LOCKS_REQUIRED(kernel_notifications.m_tip_block_mutex) {
                const auto tip_block = kernel_notifications.TipBlock();
                return chainman.m_interrupt || interrupt_mining || (tip_block && *tip_block != last_tip_hash);
            });
            if (chainman.m_interrupt || interrupt_mining) {
                interrupt_mining = false;
                return false;
            }

            // If the tip changed during the wait, extend the deadline
            const auto tip_block = kernel_notifications.TipBlock();
            if (tip_block && *tip_block != last_tip_hash) {
                last_tip_hash = *tip_block;
                continue;
            }
        }

        // No tip change and the cooldown window has expired.
        if (MockableSteadyClock::now() >= cooldown_deadline) break;
    }

    return true;
}

std::optional<BlockRef> WaitTipChanged(ChainstateManager& chainman, KernelNotifications& kernel_notifications, const uint256& current_tip, MillisecondsDouble& timeout, bool& interrupt)
{
    Assume(timeout >= 0ms); // No internal callers should use a negative timeout
    if (timeout < 0ms) timeout = 0ms;
    if (timeout > std::chrono::years{100}) timeout = std::chrono::years{100}; // Upper bound to avoid UB in std::chrono
    auto deadline{std::chrono::steady_clock::now() + timeout};
    {
        WAIT_LOCK(kernel_notifications.m_tip_block_mutex, lock);
        // For callers convenience, wait longer than the provided timeout
        // during startup for the tip to be non-null. That way this function
        // always returns valid tip information when possible and only
        // returns null when shutting down, not when timing out.
        kernel_notifications.m_tip_block_cv.wait(lock, [&]() EXCLUSIVE_LOCKS_REQUIRED(kernel_notifications.m_tip_block_mutex) {
            return kernel_notifications.TipBlock() || chainman.m_interrupt || interrupt;
        });
        if (chainman.m_interrupt || interrupt) {
            interrupt = false;
            return {};
        }
        // At this point TipBlock is set, so continue to wait until it is
        // different then `current_tip` provided by caller.
        kernel_notifications.m_tip_block_cv.wait_until(lock, deadline, [&]() EXCLUSIVE_LOCKS_REQUIRED(kernel_notifications.m_tip_block_mutex) {
            return Assume(kernel_notifications.TipBlock()) != current_tip || chainman.m_interrupt || interrupt;
        });
        if (chainman.m_interrupt || interrupt) {
            interrupt = false;
            return {};
        }
    }

    // Must release m_tip_block_mutex before getTip() locks cs_main, to
    // avoid deadlocks.
    return GetTip(chainman);
}

} // namespace node
