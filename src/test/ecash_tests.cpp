// Copyright (c) 2026 The Backbone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The rules of the eCash networks (ecash-com/bitcoin, and the bip300301_enforcer for BIP300/301), and
// that the Chains networks keep theirs.

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/amount.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <consensus/tx_verify.h>
#include <drivechain/miner.h>
#include <drivechain/scdb.h>
#include <drivechain/sidechain.h>
#include <hash.h>
#include <kernel/ecash_repurpose.h>
#include <policy/policy.h>
#include <pow.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/signingprovider.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/chaintype.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <limits>

#include <string>
#include <vector>

using namespace drivechain;

namespace {

struct EcashTestingSetup : public BasicTestingSetup {
    EcashTestingSetup() : BasicTestingSetup{ChainType::ECASH_BETANET} {}
};

CScript Bytes(const std::vector<unsigned char>& bytes) { return CScript(bytes.begin(), bytes.end()); }

//! A transaction with one input that is not final and the given locktime.
CMutableTransaction LockedTx(uint32_t lock_time)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].nSequence = CTxIn::SEQUENCE_FINAL - 1;
    tx.vout.resize(1);
    tx.vout[0].nValue = 0;
    tx.nLockTime = lock_time;
    return tx;
}

//! Evaluate `<lock> OP_CHECKLOCKTIMEVERIFY OP_DROP OP_TRUE` as the output script spent by input 0 of `tx`.
bool CltvPasses(const CMutableTransaction& tx, int64_t lock, script_verify_flags flags, ScriptError& error)
{
    const CScript script{CScript() << lock << OP_CHECKLOCKTIMEVERIFY << OP_DROP << OP_TRUE};
    const CTransaction ctx{tx};
    PrecomputedTransactionData txdata;
    txdata.Init(ctx, std::vector<CTxOut>{CTxOut{0, script}});
    return VerifyScript(CScript(), script, nullptr, flags, TransactionSignatureChecker{&ctx, 0, 0, txdata, MissingDataBehavior::FAIL}, &error);
}

//! The parameters of the enforcer's rules with its regtest thresholds (Thresholds::SHORT).
Consensus::DrivechainParams EnforcerParams()
{
    Consensus::DrivechainParams params;
    params.rules = Consensus::DrivechainParams::RULES_ENFORCER;
    params.unused_slot_threshold = 5;
    params.unused_slot_max_age = 10;
    params.used_slot_threshold = 5;
    params.used_slot_max_age = 10;
    params.bundle_threshold = 5;
    params.bundle_max_age = 10;
    return params;
}

Sidechain MakeSidechain(SidechainId slot, const std::string& title = "test")
{
    Sidechain sidechain;
    sidechain.slot = slot;
    sidechain.title = title;
    sidechain.description = "a sidechain";
    sidechain.hash_id1 = uint256{1};
    return sidechain;
}

CScript Ack(const Sidechain& sidechain) { return AckScript(sidechain.slot, sidechain.GetHash()); }
CScript Votes(const std::vector<uint16_t>& votes) { return VoteScript(MakeVoteMessage(votes)); }

/** A chain of blocks under the enforcer's rules, with the sidechain database that follows from it. */
struct EnforcerChain {
    Consensus::DrivechainParams params{EnforcerParams()};
    SidechainDB scdb;
    int height{0};
    uint256 tip{uint256{0xbb}};
    uint32_t counter{0};
    std::string reject_reason;
    std::vector<Deposit> deposits;

    EnforcerChain() { scdb.SetBlockHash(tip); }

    CBlock MakeBlock(const std::vector<CScript>& coinbase_scripts, const std::vector<CMutableTransaction>& txs, const std::vector<CTxOut>& extra = {})
    {
        CBlock block;
        block.hashPrevBlock = tip;
        block.nTime = ++counter;
        CMutableTransaction coinbase;
        coinbase.vin.resize(1);
        coinbase.vin[0].prevout.SetNull();
        coinbase.vin[0].scriptSig = CScript() << (height + 1) << OP_0;
        coinbase.vout.emplace_back(50 * COIN, CScript() << OP_TRUE);
        for (const CTxOut& out : extra) coinbase.vout.push_back(out);
        for (const CScript& script : coinbase_scripts) coinbase.vout.emplace_back(0, script);
        block.vtx.push_back(MakeTransactionRef(coinbase));
        for (const CMutableTransaction& tx : txs) block.vtx.push_back(MakeTransactionRef(tx));
        block.hashMerkleRoot = BlockMerkleRoot(block);
        return block;
    }

    /** Connect a block; a valid one is also disconnected again, which has to restore the state exactly. */
    bool Connect(const std::vector<CScript>& coinbase_scripts = {}, const std::vector<CMutableTransaction>& txs = {}, const std::vector<CTxOut>& extra = {})
    {
        const CBlock block{MakeBlock(coinbase_scripts, txs, extra)};
        const SidechainDB before{scdb};
        SidechainDB after{scdb};
        BlockUndo undo;
        deposits.clear();
        reject_reason.clear();
        if (!after.ConnectBlock(block, height + 1, params, undo, &deposits, reject_reason)) {
            after.DisconnectBlock(undo);
            BOOST_CHECK(after == before);
            return false;
        }
        DataStream stream{};
        stream << undo;
        BlockUndo undo_read;
        stream >> undo_read;
        SidechainDB reverted{after};
        reverted.DisconnectBlock(undo_read);
        BOOST_CHECK(reverted == before);
        BOOST_CHECK(reverted.GetHash() == before.GetHash());
        scdb = after;
        tip = block.GetHash();
        ++height;
        history.push_back(undo);
        return true;
    }
    //! The undo data of the blocks connected, in order (for reorgs over several blocks).
    std::vector<BlockUndo> history;

    /** Take back the last `count` blocks, as a reorg does. */
    void Disconnect(int count)
    {
        for (int i{0}; i < count; ++i) {
            scdb.DisconnectBlock(history.back());
            tip = history.back().prev_block_hash;
            history.pop_back();
            --height;
        }
    }

    /** Propose a sidechain and ack it until it activates: more acks than the threshold. */
    void Activate(const Sidechain& sidechain)
    {
        BOOST_REQUIRE(Connect({ProposalScript(sidechain)}));
        for (int i{0}; i <= params.unused_slot_threshold; ++i) {
            BOOST_REQUIRE(!scdb.IsActive(sidechain.slot) || scdb.GetSlot(sidechain.slot)->sidechain != sidechain);
            BOOST_REQUIRE(Connect({Ack(sidechain)}));
        }
        BOOST_REQUIRE(scdb.IsActive(sidechain.slot));
        BOOST_REQUIRE(scdb.GetSlot(sidechain.slot)->sidechain == sidechain);
    }

    CMutableTransaction BaseTx()
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        ++counter;
        tx.vin[0].prevout = COutPoint{Txid::FromUint256(uint256{static_cast<uint8_t>(counter)}), counter};
        return tx;
    }

    CMutableTransaction DepositTx(SidechainId slot, CAmount total, const std::string& destination = "dest")
    {
        CMutableTransaction tx{BaseTx()};
        if (const Slot* state{scdb.GetSlot(slot)}; state && state->has_ctip) tx.vin.emplace_back(state->ctip.outpoint);
        tx.vout.emplace_back(total, EscrowScript(slot));
        tx.vout.emplace_back(0, DestinationScript(destination));
        return tx;
    }

    static CMutableTransaction BlindBundle(CAmount payout, CAmount fee, uint8_t salt = 0)
    {
        CMutableTransaction tx;
        tx.vout.emplace_back(0, WithdrawalFeeScript(fee));
        tx.vout.emplace_back(payout, CScript() << OP_TRUE << salt);
        return tx;
    }

    CMutableTransaction WithdrawalTx(SidechainId slot, const CMutableTransaction& blind, CAmount payout, CAmount fee)
    {
        const Slot* state{scdb.GetSlot(slot)};
        BOOST_REQUIRE(state && state->has_ctip);
        CMutableTransaction tx{blind};
        tx.vin.assign(1, CTxIn{state->ctip.outpoint});
        tx.vout[0] = CTxOut{state->ctip.amount - payout - fee, EscrowScript(slot)};
        return tx;
    }

    uint32_t Score(SidechainId slot, const uint256& hash) const
    {
        for (const Bundle& bundle : scdb.GetSlot(slot)->bundles) {
            if (bundle.hash == hash) return bundle.score;
        }
        return 0xFFFFFFFF;
    }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(ecash_tests, EcashTestingSetup)

BOOST_AUTO_TEST_CASE(network_parameters)
{
    const auto main{CChainParams::EcashMain()};
    const auto beta{CChainParams::EcashBetanet()};
    const auto test{CChainParams::EcashTestNet()};
    // ecash-com/bitcoin: tag ecash-v1.0.0-core31.1 (main), branch betanet, CTestNet4Params.
    BOOST_CHECK_EQUAL(HexStr(main->MessageStart()), "eca5dcb7");
    BOOST_CHECK_EQUAL(HexStr(beta->MessageStart()), "eca5b104");
    BOOST_CHECK_EQUAL(HexStr(test->MessageStart()), "eca5b124");
    BOOST_CHECK_EQUAL(main->GetDefaultPort(), 8533);
    BOOST_CHECK_EQUAL(beta->GetDefaultPort(), 8533);
    BOOST_CHECK_EQUAL(test->GetDefaultPort(), 48533);
    BOOST_CHECK_EQUAL(main->GetConsensus().ecash_fork_height, 973728);
    BOOST_CHECK_EQUAL(main->GetConsensus().ecash_fork_bits, 0x1901a6e1U);
    BOOST_CHECK_EQUAL(beta->GetConsensus().ecash_fork_height, 967680);
    BOOST_CHECK_EQUAL(beta->GetConsensus().ecash_fork_bits, 0x19044b7eU);
    BOOST_CHECK_EQUAL(test->GetConsensus().ecash_fork_height, 0);
    BOOST_CHECK_EQUAL(main->GetConsensus().replay_locktime, 499999998U);
    BOOST_CHECK_EQUAL(beta->GetConsensus().replay_locktime, 499999999U);
    BOOST_CHECK_EQUAL(test->GetConsensus().replay_locktime, 499999998U);
    // Bitcoin's genesis and addresses.
    BOOST_CHECK(main->GetConsensus().hashGenesisBlock == uint256{"000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"});
    BOOST_CHECK(beta->GetConsensus().hashGenesisBlock == main->GetConsensus().hashGenesisBlock);
    BOOST_CHECK(test->GetConsensus().hashGenesisBlock == uint256{"00000000da84f2bafbbc53dee25a72ae507ff4914b867c565be350b0da8bf043"});
    BOOST_CHECK_EQUAL(main->Bech32HRP(), "bc");
    BOOST_CHECK_EQUAL(test->Bech32HRP(), "tb");
    // The fork heights are retarget boundaries, where the reset is computed.
    for (const auto* params : {main.get(), beta.get()}) {
        const auto& consensus{params->GetConsensus()};
        BOOST_CHECK_EQUAL(consensus.ecash_fork_height % consensus.DifficultyAdjustmentInterval(), 0);
        BOOST_CHECK(consensus.ecash && consensus.drivechain_nop8);
        BOOST_CHECK(consensus.drivechain.EnforcerRules());
        BOOST_CHECK_EQUAL(consensus.drivechain.activation_height, consensus.ecash_fork_height);
        BOOST_CHECK(!params->IsTestChain());
    }
    BOOST_CHECK(test->IsTestChain());
    // The enforcer's betanet preset (17eb2ce lib/types.rs, Thresholds::BETANET).
    const auto& dc{beta->GetConsensus().drivechain};
    BOOST_CHECK_EQUAL(dc.unused_slot_threshold, 1008);
    BOOST_CHECK_EQUAL(dc.unused_slot_max_age, 2016);
    BOOST_CHECK_EQUAL(dc.used_slot_threshold, 13150);
    BOOST_CHECK_EQUAL(dc.used_slot_max_age, 26300);
    BOOST_CHECK_EQUAL(dc.bundle_threshold, 13150);
    BOOST_CHECK_EQUAL(dc.bundle_max_age, 26300);
    // The Chains networks keep their rules.
    for (const auto& chn : {CChainParams::Main(), CChainParams::TestNet(), CChainParams::RegTest()}) {
        const auto& consensus{chn->GetConsensus()};
        BOOST_CHECK(!consensus.ecash && !consensus.drivechain_nop8);
        BOOST_CHECK_EQUAL(consensus.ecash_fork_height, 0);
        BOOST_CHECK_EQUAL(consensus.replay_locktime, 0U);
        BOOST_CHECK(!consensus.drivechain.EnforcerRules());
        BOOST_CHECK(!consensus.IsRepurposeTx(*kernel::ECASH_BETANET_REPURPOSE_TXIDS.begin()));
    }
    // The names of the networks, and what RPC reports as "chain" (eCash's node's).
    BOOST_CHECK(ChainTypeFromString("ecash") == ChainType::ECASH);
    BOOST_CHECK(ChainTypeFromString("ecashbeta") == ChainType::ECASH_BETANET);
    BOOST_CHECK(ChainTypeFromString("ecashtest") == ChainType::ECASH_TESTNET);
    BOOST_CHECK_EQUAL(ChainTypeToCompatName(ChainType::ECASH), "main");
    BOOST_CHECK_EQUAL(ChainTypeToCompatName(ChainType::ECASH_BETANET), "main");
    BOOST_CHECK_EQUAL(ChainTypeToCompatName(ChainType::ECASH_TESTNET), "testnet4");
    BOOST_CHECK_EQUAL(ChainTypeToCompatName(ChainType::TESTNET), "test");
    BOOST_CHECK(GetNetworkForMagic(main->MessageStart()) == ChainType::ECASH);
    BOOST_CHECK(GetNetworkForMagic(beta->MessageStart()) == ChainType::ECASH_BETANET);
    BOOST_CHECK(GetNetworkForMagic(test->MessageStart()) == ChainType::ECASH_TESTNET);
    // Regtest under eCash's rules with -testecashmagic has the message start and port of eCash's client on
    // regtest (ecash-com/bitcoin, branch betanet, CRegTestParams), so that the two peer; without it, regtest's.
    CChainParams::RegTestOptions ecash_opts{};
    ecash_opts.ecash.emplace();
    BOOST_CHECK_EQUAL(HexStr(CChainParams::RegTest(ecash_opts)->MessageStart()), "c7e8c1da");
    ecash_opts.ecash->ecash_magic = true;
    const auto ecash_regtest{CChainParams::RegTest(ecash_opts)};
    BOOST_CHECK_EQUAL(HexStr(ecash_regtest->MessageStart()), "eca5b134");
    BOOST_CHECK_EQUAL(ecash_regtest->GetDefaultPort(), 18644);
    BOOST_CHECK_EQUAL(HexStr(CChainParams::RegTest()->MessageStart()), "c7e8c1da");
    BOOST_CHECK_EQUAL(CChainParams::RegTest()->GetDefaultPort(), 29555);
}

BOOST_AUTO_TEST_CASE(fork_difficulty_reset)
{
    for (const auto& params : {CChainParams::EcashBetanet(), CChainParams::EcashMain()}) {
        const Consensus::Params& consensus{params->GetConsensus()};
        const int fork{consensus.ecash_fork_height};
        const int64_t interval{consensus.DifficultyAdjustmentInterval()};
        const uint32_t bitcoin_bits{0x17023774}; // a Bitcoin difficulty of 2026

        // The retarget into the fork block gives the fork bits, whatever the time the period took.
        CBlockIndex before_fork;
        before_fork.nHeight = fork - 1;
        before_fork.nBits = bitcoin_bits;
        before_fork.nTime = 1'790'000'000;
        for (const int64_t span : {interval * 600 / 8, interval * 600, interval * 600 * 8}) {
            BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&before_fork, before_fork.nTime - span, consensus), consensus.ecash_fork_bits);
        }
        // The retarget before is Bitcoin's.
        CBlockIndex earlier;
        earlier.nBits = bitcoin_bits;
        earlier.nTime = before_fork.nTime;
        earlier.nHeight = fork - 1 - interval;
        BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&earlier, earlier.nTime - interval * 600, consensus), bitcoin_bits);

        // The fork bits hold for the period; the first retarget measures it from the fork block, clamped to 4x.
        // (Betanet: Bitcoin's retarget. The main network's dampened one: ecash_main_dampened_retarget.)
        if (consensus.EcashRetarget(fork)) continue;
        CBlockIndex end_of_period;
        end_of_period.nHeight = fork + interval - 1;
        end_of_period.nBits = consensus.ecash_fork_bits;
        end_of_period.nTime = 1'790'000'000;
        arith_uint256 fork_target;
        fork_target.SetCompact(consensus.ecash_fork_bits);
        // Ten times too fast: the difficulty rises by four only.
        arith_uint256 expected{fork_target};
        expected /= 4;
        BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&end_of_period, end_of_period.nTime - interval * 60, consensus), expected.GetCompact());
        // On time: unchanged.
        BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&end_of_period, end_of_period.nTime - interval * 600, consensus), consensus.ecash_fork_bits);

        // Headers presync admits the reset at the fork height, and only the fork bits there.
        BOOST_CHECK(PermittedDifficultyTransition(consensus, fork, bitcoin_bits, consensus.ecash_fork_bits));
        BOOST_CHECK(!PermittedDifficultyTransition(consensus, fork, bitcoin_bits, bitcoin_bits));
        BOOST_CHECK(!PermittedDifficultyTransition(consensus, fork + interval, consensus.ecash_fork_bits, bitcoin_bits));
    }

    // A fork height that is no retarget boundary could never have a valid block: GetNextWorkRequired does
    // not retarget there, and so asks for the bits of the block before, while the fork block must carry
    // the fork bits (which is why the parameters assert that it is one).
    Consensus::Params consensus{CChainParams::EcashBetanet()->GetConsensus()};
    consensus.ecash_fork_height = 967680 + 1000;
    CBlockIndex prev;
    prev.nHeight = consensus.ecash_fork_height - 1;
    prev.nBits = 0x17023774;
    prev.nTime = 1'790'000'000;
    CBlockHeader next;
    next.nTime = prev.nTime + 600;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&prev, &next, consensus), 0x17023774U);
    BOOST_CHECK(GetNextWorkRequired(&prev, &next, consensus) != consensus.ecash_fork_bits);
}

BOOST_AUTO_TEST_CASE(ecash_main_dampened_retarget)
{
    // eCash's main network from ecash-v1.1.0-core31.1 (5bd8690fab): from the fork block the difficulty
    // retargets every 288 blocks, new target = (6 * old + measured) / 7, measured from the last block of
    // the previous window (from the fork block for the first window), within 4x of 288 target spacings.
    const auto mainnet{CChainParams::EcashMain()};
    const Consensus::Params& consensus{mainnet->GetConsensus()};
    const int fork{consensus.ecash_fork_height};
    BOOST_CHECK_EQUAL(consensus.ecash_retarget_interval, 288);
    BOOST_CHECK_EQUAL(consensus.DifficultyAdjustmentInterval(fork - 1), 2016);
    BOOST_CHECK_EQUAL(consensus.DifficultyAdjustmentInterval(fork), 288);
    BOOST_CHECK_EQUAL(consensus.DifficultyAdjustmentInterval(fork + 100'000), 288);
    // Betanet and the test network keep Bitcoin's retarget.
    for (const auto& other : {CChainParams::EcashBetanet(), CChainParams::EcashTestNet()}) {
        BOOST_CHECK(!other->GetConsensus().EcashRetarget(other->GetConsensus().ecash_fork_height + 288));
        BOOST_CHECK_EQUAL(other->GetConsensus().DifficultyAdjustmentInterval(other->GetConsensus().ecash_fork_height + 288), 2016);
    }

    const int64_t spacing{consensus.nPowTargetSpacing};
    const int64_t timespan{288 * spacing};
    const uint32_t bitcoin_bits{0x17023774};
    // A chain from 300 blocks before the fork: on time up to the fork's first window, then at half the spacing.
    const int first{fork - 300};
    const int last{fork + 3 * 288 + 5};
    std::vector<CBlockIndex> blocks(last - first + 1);
    for (int height{first}; height <= last; ++height) {
        CBlockIndex& block{blocks[height - first]};
        block.nHeight = height;
        block.pprev = height > first ? &blocks[height - first - 1] : nullptr;
        block.nTime = height == first ? 1'790'000'000 : block.pprev->nTime + (height <= fork + 287 ? spacing : spacing / 2);
        if (height == first) {
            block.nBits = bitcoin_bits;
        } else {
            CBlockHeader header;
            header.nTime = block.nTime;
            block.nBits = GetNextWorkRequired(block.pprev, &header, consensus);
            BOOST_CHECK(PermittedDifficultyTransition(consensus, height, block.pprev->nBits, block.nBits));
        }
        // (No skip pointers: they would point below the first block of this chain.)
    }
    const auto at{[&](int height) -> const CBlockIndex& { return blocks[height - first]; }};
    const auto dampened{[&](uint32_t old_bits, int64_t measured) {
        measured = std::clamp<int64_t>(measured, timespan / 4, timespan * 4);
        arith_uint256 target;
        target.SetCompact(old_bits);
        target *= 6 * timespan + measured;
        target /= 7 * timespan;
        return target.GetCompact();
    }};
    BOOST_CHECK_EQUAL(at(fork - 1).nBits, bitcoin_bits);
    BOOST_CHECK_EQUAL(at(fork).nBits, consensus.ecash_fork_bits);
    for (int height{fork + 1}; height < fork + 288; ++height) BOOST_CHECK_EQUAL(at(height).nBits, consensus.ecash_fork_bits);
    // The first window: from the fork block to block fork + 287, 287 spacings on time.
    BOOST_CHECK_EQUAL(at(fork + 288).nBits, dampened(consensus.ecash_fork_bits, 287 * spacing));
    BOOST_CHECK(at(fork + 288).nBits != consensus.ecash_fork_bits);
    for (int height{fork + 289}; height < fork + 576; ++height) BOOST_CHECK_EQUAL(at(height).nBits, at(fork + 288).nBits);
    // The second: from block fork + 287 (the end of the previous window) to fork + 575, 288 blocks at half the spacing.
    BOOST_CHECK_EQUAL(at(fork + 576).nBits, dampened(at(fork + 575).nBits, 288 * (spacing / 2)));
    BOOST_CHECK_EQUAL(at(fork + 864).nBits, dampened(at(fork + 863).nBits, 288 * (spacing / 2)));
    arith_uint256 old_target, new_target;
    old_target.SetCompact(at(fork + 575).nBits);
    new_target.SetCompact(at(fork + 576).nBits);
    BOOST_CHECK(new_target < old_target); // faster blocks: harder

    // Headers presync: the dampened bounds at a retarget height (at most (6 + 4) / 7 of the target, at
    // least (6 + 1/4) / 7 of it), no change elsewhere.
    const uint32_t bits{at(fork + 575).nBits};
    BOOST_CHECK(PermittedDifficultyTransition(consensus, fork + 576, bits, dampened(bits, timespan * 4)));
    BOOST_CHECK(PermittedDifficultyTransition(consensus, fork + 576, bits, dampened(bits, timespan / 4)));
    arith_uint256 quarter;
    quarter.SetCompact(bits);
    quarter /= 4; // Bitcoin's 4x step: too far for the dampened retarget
    BOOST_CHECK(!PermittedDifficultyTransition(consensus, fork + 576, bits, quarter.GetCompact()));
    BOOST_CHECK(!PermittedDifficultyTransition(consensus, fork + 300, bits, dampened(bits, timespan / 4)));
    BOOST_CHECK(PermittedDifficultyTransition(consensus, fork + 300, bits, bits));
}

BOOST_AUTO_TEST_CASE(replay_locktime)
{
    // ecash-com/bitcoin's tx_replay_protection_locktime, for both magic values, and on Chains.
    for (const uint32_t magic : {LOCKTIME_THRESHOLD - 2, LOCKTIME_THRESHOLD - 1}) {
        const uint32_t other{magic == LOCKTIME_THRESHOLD - 2 ? LOCKTIME_THRESHOLD - 1 : LOCKTIME_THRESHOLD - 2};
        // The magic locktime is final at any height and time.
        BOOST_CHECK(IsFinalTx(CTransaction{LockedTx(magic)}, /*nBlockHeight=*/1, /*nBlockTime=*/0, magic));
        // Not on Bitcoin's rules (nor Chains'), nor the other network's value.
        BOOST_CHECK(!IsFinalTx(CTransaction{LockedTx(magic)}, 1, 0));
        BOOST_CHECK(!IsFinalTx(CTransaction{LockedTx(other)}, 1, 0, magic));
        // An unlocked transaction is final, as always; a fully final input makes any transaction final.
        BOOST_CHECK(IsFinalTx(CTransaction{LockedTx(0)}, 1, 0, magic));
        CMutableTransaction final_input{LockedTx(other)};
        final_input.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
        BOOST_CHECK(IsFinalTx(CTransaction{final_input}, 1, 0, magic));
    }

    // OP_CHECKLOCKTIMEVERIFY fails with the magic locktime, by height and by time.
    const script_verify_flags base{SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY};
    ScriptError error;
    for (const auto& [magic, flag] : {std::pair{LOCKTIME_THRESHOLD - 2, script_verify_flags{SCRIPT_VERIFY_REPLAY_LOCKTIME}},
                                      std::pair{LOCKTIME_THRESHOLD - 1, script_verify_flags{SCRIPT_VERIFY_REPLAY_LOCKTIME_BETANET}}}) {
        const CMutableTransaction tx{LockedTx(magic)};
        // Bitcoin reads it as a height lock, which a height lock of 100 is satisfied by...
        BOOST_CHECK(CltvPasses(tx, 100, base, error));
        // ...not on eCash.
        BOOST_CHECK(!CltvPasses(tx, 100, base | flag, error));
        BOOST_CHECK_EQUAL(error, SCRIPT_ERR_UNSATISFIED_LOCKTIME);
        BOOST_CHECK(!CltvPasses(tx, LOCKTIME_THRESHOLD + 1, base | flag, error));
        BOOST_CHECK_EQUAL(error, SCRIPT_ERR_UNSATISFIED_LOCKTIME);
        // Any other locktime, as on Bitcoin.
        const CMutableTransaction plain{LockedTx(magic - 10)};
        BOOST_CHECK(CltvPasses(plain, 100, base | flag, error));
    }
    // The flag of one network leaves the value of the other alone.
    BOOST_CHECK(CltvPasses(LockedTx(LOCKTIME_THRESHOLD - 1), 100, base | SCRIPT_VERIFY_REPLAY_LOCKTIME, error));
    BOOST_CHECK(CltvPasses(LockedTx(LOCKTIME_THRESHOLD - 2), 100, base | SCRIPT_VERIFY_REPLAY_LOCKTIME_BETANET, error));
}

BOOST_AUTO_TEST_CASE(op_drivechain_per_network)
{
    const script_verify_flags ecash{STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_DRIVECHAIN_NOP8};
    const script_verify_flags chains{STANDARD_SCRIPT_VERIFY_FLAGS};
    ScriptError error;

    // Any script of four bytes that starts with OP_NOP8 is anyone-can-spend on eCash, whatever its other bytes.
    for (const CScript& script : {CScript() << OP_NOP8 << std::vector<unsigned char>{5} << OP_TRUE,
                                  Bytes({0xb7, 0x00, 0x00, 0x00}),
                                  Bytes({0xb7, 0x4c, 0xff, 0xff})}) {
        BOOST_CHECK_EQUAL(script.size(), 4U);
        BOOST_CHECK_MESSAGE(VerifyScript(CScript(), script, nullptr, ecash, BaseSignatureChecker{}, &error), ScriptErrorString(error));
        // On Chains it is a NOP reserved for upgrades.
        BOOST_CHECK(!VerifyScript(CScript(), script, nullptr, chains, BaseSignatureChecker{}, &error));
    }
    // Elsewhere OP_NOP8 is a NOP, and a standard one.
    const CScript longer{CScript() << OP_NOP8 << OP_TRUE << OP_TRUE << OP_DROP};
    BOOST_CHECK(VerifyScript(CScript(), longer, nullptr, ecash, BaseSignatureChecker{}, &error));
    BOOST_CHECK(!VerifyScript(CScript(), CScript() << OP_TRUE << OP_NOP8 << OP_DROP << OP_TRUE << OP_DROP, nullptr, chains, BaseSignatureChecker{}, &error));
    // A four byte script whose OP_NOP8 is not first does not end: OP_TRUE OP_NOP8 OP_DROP OP_0 leaves 0 (false).
    BOOST_CHECK(!VerifyScript(CScript(), CScript() << OP_TRUE << OP_NOP8 << OP_DROP << OP_0, nullptr, ecash, BaseSignatureChecker{}, &error));

    // Chains' treasury script (OP_NOP5) is a discouraged NOP on eCash, and Chains' escrow on Chains.
    const CScript nop5_escrow{CScript() << OP_NOP5 << std::vector<unsigned char>{5} << OP_TRUE};
    BOOST_CHECK(!VerifyScript(CScript(), nop5_escrow, nullptr, ecash, BaseSignatureChecker{}, &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS);
    BOOST_CHECK(VerifyScript(CScript(), nop5_escrow, nullptr, chains, BaseSignatureChecker{}, &error));
    // Under the consensus flags (no discouragement) the NOP5 script still passes on eCash, as a NOP.
    BOOST_CHECK(VerifyScript(CScript(), nop5_escrow, nullptr, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_DRIVECHAIN_NOP8, BaseSignatureChecker{}, &error));

    // The script being executed may be a P2SH redeem script, a P2WSH witness script or a tapleaf.
    const CScript inner{Bytes({0xb7, 0x01, 0x07, 0x51})};
    {
        const CScript p2sh{GetScriptForDestination(ScriptHash(inner))};
        const CScript script_sig{CScript() << std::vector<unsigned char>(inner.begin(), inner.end())};
        BOOST_CHECK_MESSAGE(VerifyScript(script_sig, p2sh, nullptr, ecash, BaseSignatureChecker{}, &error), ScriptErrorString(error));
        BOOST_CHECK(!VerifyScript(script_sig, p2sh, nullptr, chains, BaseSignatureChecker{}, &error));
    }
    {
        const CScript p2wsh{GetScriptForDestination(WitnessV0ScriptHash(inner))};
        CScriptWitness witness;
        witness.stack.emplace_back(inner.begin(), inner.end());
        BOOST_CHECK_MESSAGE(VerifyScript(CScript(), p2wsh, &witness, ecash, BaseSignatureChecker{}, &error), ScriptErrorString(error));
        BOOST_CHECK(!VerifyScript(CScript(), p2wsh, &witness, chains, BaseSignatureChecker{}, &error));
    }
    {
        TaprootBuilder builder;
        builder.Add(/*depth=*/0, inner, TAPROOT_LEAF_TAPSCRIPT, /*track=*/true);
        builder.Finalize(XOnlyPubKey::NUMS_H);
        const CScript p2tr{GetScriptForDestination(builder.GetOutput())};
        const TaprootSpendData spend{builder.GetSpendData()};
        const auto& controls{spend.scripts.at({std::vector<unsigned char>(inner.begin(), inner.end()), TAPROOT_LEAF_TAPSCRIPT})};
        CScriptWitness witness;
        witness.stack.emplace_back(inner.begin(), inner.end());
        witness.stack.push_back(*controls.begin());
        CMutableTransaction spending;
        spending.vin.resize(1);
        spending.vout.resize(1);
        const CTransaction ctx{spending};
        PrecomputedTransactionData txdata;
        txdata.Init(ctx, std::vector<CTxOut>{CTxOut{0, p2tr}});
        const TransactionSignatureChecker checker{&ctx, 0, 0, txdata, MissingDataBehavior::FAIL};
        BOOST_CHECK_MESSAGE(VerifyScript(CScript(), p2tr, &witness, ecash, checker, &error), ScriptErrorString(error));
        BOOST_CHECK(!VerifyScript(CScript(), p2tr, &witness, chains, checker, &error));
    }

    // The treasury scripts of the network: OP_NOP8 here (betanet, set by SelectParams).
    BOOST_CHECK(EscrowOpcode() == OP_NOP8);
    BOOST_CHECK(EscrowScript(5) == Bytes({0xb7, 0x01, 0x05, 0x51}));
    BOOST_CHECK(ParseEscrowScript(Bytes({0xb7, 0x01, 0x05, 0x51})) == 5U);
    BOOST_CHECK(!ParseEscrowScript(nop5_escrow));
    BOOST_CHECK(IsEcashDrivechainScript(Bytes({0xb7, 0x00, 0x00, 0x00})));
    // Standard to create and to spend, as on eCash's node, any of them.
    TxoutType type;
    BOOST_CHECK(IsStandard(Bytes({0xb7, 0x00, 0x00, 0x00}), type));
    BOOST_CHECK(IsStandard(EscrowScript(9), type));
    BOOST_CHECK(!IsStandard(nop5_escrow, type));
}

BOOST_AUTO_TEST_CASE(repurpose_transactions)
{
    using kernel::ECASH_BETANET_REPURPOSE_TXIDS;
    using kernel::ECASH_MAIN_REPURPOSE_TXIDS;
    BOOST_CHECK_EQUAL(ECASH_BETANET_REPURPOSE_TXIDS.size(), 232U);
    BOOST_CHECK_EQUAL(ECASH_MAIN_REPURPOSE_TXIDS.size(), 232U);
    // The first of each list of ecash-com/bitcoin (src/repo_txns.h).
    const uint256 beta_first{"0ad49041c33bd4c3574ca6965b3811a5ccb3e9706dd3cc11133fcd1377c4370b"};
    BOOST_CHECK(ECASH_BETANET_REPURPOSE_TXIDS.contains(beta_first));
    const auto main{CChainParams::EcashMain()};
    const auto beta{CChainParams::EcashBetanet()};
    const auto test{CChainParams::EcashTestNet()};
    // Each network its own list, and only its own.
    for (const uint256& txid : ECASH_BETANET_REPURPOSE_TXIDS) {
        BOOST_CHECK(beta->GetConsensus().IsRepurposeTx(txid));
        BOOST_CHECK_EQUAL(main->GetConsensus().IsRepurposeTx(txid), ECASH_MAIN_REPURPOSE_TXIDS.contains(txid));
        BOOST_CHECK(!test->GetConsensus().IsRepurposeTx(txid));
        BOOST_CHECK(!CChainParams::Main()->GetConsensus().IsRepurposeTx(txid));
    }
    for (const uint256& txid : ECASH_MAIN_REPURPOSE_TXIDS) BOOST_CHECK(main->GetConsensus().IsRepurposeTx(txid));
    BOOST_CHECK(!beta->GetConsensus().IsRepurposeTx(uint256{1}));
    BOOST_CHECK(!main->GetConsensus().IsRepurposeTx(uint256{1}));
}

BOOST_AUTO_TEST_CASE(enforcer_activation)
{
    EnforcerChain chain;
    const Sidechain sidechain{MakeSidechain(2)};
    BOOST_REQUIRE(chain.Connect({ProposalScript(sidechain)}));
    // Acks in the proposing block do not count; more than the threshold (5) within the age (10) activate.
    for (int i{0}; i < 5; ++i) BOOST_REQUIRE(chain.Connect({Ack(sidechain)}));
    BOOST_CHECK(!chain.scdb.IsActive(2));
    BOOST_REQUIRE(chain.Connect({Ack(sidechain)}));
    BOOST_CHECK(chain.scdb.IsActive(2));
    BOOST_CHECK_EQUAL(chain.scdb.GetSlot(2)->activation_height, chain.height);
    BOOST_CHECK(chain.scdb.GetProposals().empty());

    // A proposal nobody acks fails once it can no longer reach the threshold: more than
    // max_age - threshold (5) blocks old with as many failures.
    const Sidechain other{MakeSidechain(3)};
    BOOST_REQUIRE(chain.Connect({ProposalScript(other)}));
    for (int i{0}; i < 5; ++i) BOOST_REQUIRE(chain.Connect());
    BOOST_CHECK(chain.scdb.GetProposal(3, other.GetHash()));
    BOOST_REQUIRE(chain.Connect());
    BOOST_CHECK(!chain.scdb.GetProposal(3, other.GetHash()));

    // Two proposals of the same sidechain in a block make it invalid; proposing again what is
    // proposed is ignored; any description is a proposal.
    BOOST_CHECK(!chain.Connect({ProposalScript(other), ProposalScript(other)}));
    BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-duplicate-proposal");
    const CScript raw{CScript() << OP_RETURN << std::vector<unsigned char>{0xD5, 0xE0, 0xC4, 0xAF, 4, 'x'}};
    BOOST_REQUIRE(chain.Connect({raw}));
    BOOST_REQUIRE_EQUAL(chain.scdb.GetProposals().size(), 1U);
    BOOST_CHECK_EQUAL(chain.scdb.GetProposals()[0].sidechain.version, Sidechain::RAW_DESCRIPTION);
    BOOST_CHECK(chain.scdb.GetProposals()[0].hash == Hash(std::vector<unsigned char>{'x'}));
    // Two acks for a slot in a block make it invalid.
    BOOST_CHECK(!chain.Connect({AckScript(4, Hash(std::vector<unsigned char>{'x'})), AckScript(4, uint256{9})}));
    BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-multiple-acks");
}

BOOST_AUTO_TEST_CASE(enforcer_bundles)
{
    EnforcerChain chain;
    // A bundle for an inactive slot makes the block invalid (Chains ignores it).
    BOOST_CHECK(!chain.Connect({BundleScript(2, uint256{7})}));
    BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-bundle-inactive-sidechain");

    chain.Activate(MakeSidechain(2));
    BOOST_REQUIRE(chain.Connect({}, {chain.DepositTx(2, 10 * COIN)}));
    BOOST_REQUIRE(chain.scdb.GetSlot(2)->has_ctip);

    const CMutableTransaction blind_a{EnforcerChain::BlindBundle(COIN, 1000, 1)};
    const CMutableTransaction blind_b{EnforcerChain::BlindBundle(COIN, 1000, 2)};
    const uint256 hash_a{blind_a.GetHash().ToUint256()};
    const uint256 hash_b{blind_b.GetHash().ToUint256()};
    // Several bundles for a slot in a block; the same one twice is invalid.
    BOOST_CHECK(!chain.Connect({BundleScript(2, hash_a), BundleScript(2, hash_a)}));
    BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-bundle-known");
    // A vote message after the bundles sees them: index 1 is the second one of this very block.
    BOOST_REQUIRE(chain.Connect({BundleScript(2, hash_a), BundleScript(2, hash_b), Votes({1})}));
    BOOST_CHECK_EQUAL(chain.Score(2, hash_a), 0U);
    BOOST_CHECK_EQUAL(chain.Score(2, hash_b), 2U);
    // The two byte form with ABSTAIN alone is valid here (an ABSTAIN does not fit in a byte).
    BOOST_REQUIRE(chain.Connect({VoteScript({VoteForm::TWO_BYTES, {VOTE_ABSTAIN}})}));
    BOOST_CHECK(!chain.Connect({VoteScript({VoteForm::TWO_BYTES, {0}})}));
    BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-votes-form");

    // Up to more votes than the threshold (5): a withdrawal before is invalid.
    for (int i{0}; i < 3; ++i) BOOST_REQUIRE(chain.Connect({Votes({1})}));
    BOOST_CHECK_EQUAL(chain.Score(2, hash_b), 5U);
    BOOST_CHECK(!chain.Connect({}, {chain.WithdrawalTx(2, blind_b, COIN, 1000)}));
    BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-withdrawal-score");
    // The votes of the block count before its transactions.
    BOOST_REQUIRE(chain.Connect({Votes({1})}, {chain.WithdrawalTx(2, blind_b, COIN, 1000)}));
    BOOST_CHECK_EQUAL(chain.scdb.GetSlot(2)->ctip.amount, 9 * COIN - 1000);
    // No single payout: the other bundle stays pending; and a paid bundle can be proposed again.
    BOOST_CHECK_EQUAL(chain.Score(2, hash_a), 0U);
    BOOST_CHECK(chain.scdb.WasPaid(2, hash_b) == true);
    BOOST_REQUIRE(chain.Connect({BundleScript(2, hash_b)}));
    BOOST_CHECK_EQUAL(chain.Score(2, hash_b), 1U);
    // Paid again: remembered as paid at the later height (the record of the first payout replaced).
    const auto index_of_b{[&] {
        const auto& bundles{chain.scdb.GetSlot(2)->bundles};
        for (size_t i{0}; i < bundles.size(); ++i) {
            if (bundles[i].hash == hash_b) return static_cast<uint16_t>(i);
        }
        return uint16_t{0};
    }};
    for (int i{0}; i < 5; ++i) BOOST_REQUIRE(chain.Connect({Votes({index_of_b()})}));
    BOOST_REQUIRE(chain.Connect({Votes({index_of_b()})}, {chain.WithdrawalTx(2, blind_b, COIN, 1000)}));
    BOOST_CHECK(chain.scdb.WasPaid(2, hash_b) == true);
    BOOST_CHECK_EQUAL(chain.scdb.GetClosed(2, hash_b)->height, chain.height);

    // The other bundle, older than the maximum age (10) by now, failed.
    BOOST_CHECK_EQUAL(chain.Score(2, hash_a), 0xFFFFFFFFU);
    BOOST_CHECK(chain.scdb.WasPaid(2, hash_a) == false);
    // A bundle fails the block after it reaches the maximum age, not before.
    const uint256 hash_c{77};
    BOOST_REQUIRE(chain.Connect({BundleScript(2, hash_c)}));
    const int c_height{chain.height};
    while (chain.height - c_height < 10) BOOST_REQUIRE(chain.Connect());
    BOOST_CHECK_EQUAL(chain.Score(2, hash_c), 1U);
    BOOST_REQUIRE(chain.Connect());
    BOOST_CHECK_EQUAL(chain.Score(2, hash_c), 0xFFFFFFFFU);
    BOOST_CHECK(chain.scdb.WasPaid(2, hash_c) == false);
}

BOOST_AUTO_TEST_CASE(enforcer_paid_bundle_memory)
{
    // Security review: a bundle paid out, proposed again (consensus allows it under the enforcer's rules,
    // and would pay it again with the votes) and then failing, must stay remembered as paid; the block
    // template never votes for it nor pays it again; reorgs take both closures back exactly.
    EnforcerChain chain;
    chain.Activate(MakeSidechain(2));
    BOOST_REQUIRE(chain.Connect({}, {chain.DepositTx(2, 10 * COIN)}));
    const CMutableTransaction blind{EnforcerChain::BlindBundle(COIN, 1000, 9)};
    const uint256 hash{blind.GetHash().ToUint256()};
    BOOST_REQUIRE(chain.Connect({BundleScript(2, hash)}));
    for (int i{0}; i < 5; ++i) BOOST_REQUIRE(chain.Connect({Votes({0})}));
    const SidechainDB before_payout{chain.scdb};
    const int before_payout_height{chain.height};
    BOOST_REQUIRE(chain.Connect({Votes({0})}, {chain.WithdrawalTx(2, blind, COIN, 1000)}));
    const int paid_height{chain.height};
    BOOST_CHECK(chain.scdb.WasPaid(2, hash) == true);
    BOOST_CHECK(chain.scdb.LastPayoutHeight(2) == paid_height);

    // Proposed again by someone, and left to expire: still paid, at the height it was paid.
    BOOST_REQUIRE(chain.Connect({BundleScript(2, hash)}));
    const int reproposed_height{chain.height};
    while (chain.height - reproposed_height <= chain.params.bundle_max_age) BOOST_REQUIRE(chain.Connect());
    BOOST_CHECK_EQUAL(chain.Score(2, hash), 0xFFFFFFFFU);
    BOOST_CHECK(chain.scdb.WasPaid(2, hash) == true);
    BOOST_CHECK_EQUAL(chain.scdb.GetClosed(2, hash)->height, paid_height);
    BOOST_CHECK(chain.scdb.LastPayoutHeight(2) == paid_height);

    // Proposed a third time: consensus takes it (as the enforcer does), the template does not help it.
    BOOST_REQUIRE(chain.Connect({BundleScript(2, hash)}));
    MinerState miner;
    std::string error;
    BOOST_REQUIRE(miner.AddBundle(2, blind, error));
    for (int i{0}; i < 8; ++i) {
        miner.Prune(chain.scdb, chain.height + 1);
        const auto additions{miner.CreateBlockAdditions(chain.scdb, chain.params, chain.tip, {}, chain.height + 1)};
        BOOST_CHECK(additions.withdrawals.empty());
        for (const CTxOut& out : additions.coinbase_outputs) {
            if (const auto votes{ParseVoteScript(out.scriptPubKey)}) BOOST_CHECK(votes->votes.empty() || votes->votes[0] == VOTE_ABSTAIN);
            BOOST_CHECK(!ParseBundleScript(out.scriptPubKey));
        }
        // Others vote it up: consensus would pay it, the template still does not.
        BOOST_REQUIRE(chain.Connect({Votes({0})}));
    }
    BOOST_CHECK_GT(chain.Score(2, hash), static_cast<uint32_t>(chain.params.bundle_threshold));

    // A reorg back to before the payout takes both closures back exactly; connecting again gives them again.
    chain.Disconnect(chain.height - before_payout_height);
    BOOST_CHECK(chain.scdb == before_payout);
    BOOST_CHECK(!chain.scdb.IsClosed(2, hash));
    BOOST_REQUIRE(chain.Connect({Votes({0})}, {chain.WithdrawalTx(2, blind, COIN, 1000)}));
    BOOST_CHECK(chain.scdb.WasPaid(2, hash) == true);
}

BOOST_AUTO_TEST_CASE(enforcer_votes_repeat_and_alarm)
{
    EnforcerChain chain;
    chain.Activate(MakeSidechain(1));
    const uint256 hash{11};
    BOOST_REQUIRE(chain.Connect({BundleScript(1, hash)}));
    BOOST_REQUIRE(chain.Connect({Votes({0})}));
    BOOST_CHECK_EQUAL(chain.Score(1, hash), 2U);
    // REPEAT_PREVIOUS: what the previous block did.
    BOOST_REQUIRE(chain.Connect({VoteScript({VoteForm::REPEAT_PREVIOUS, {}})}));
    BOOST_CHECK_EQUAL(chain.Score(1, hash), 3U);
    BOOST_REQUIRE(chain.Connect({VoteScript({VoteForm::REPEAT_PREVIOUS, {}})}));
    BOOST_CHECK_EQUAL(chain.Score(1, hash), 4U);
    // A block without votes abstains, and is what the next REPEAT_PREVIOUS repeats.
    BOOST_REQUIRE(chain.Connect());
    BOOST_REQUIRE(chain.Connect({VoteScript({VoteForm::REPEAT_PREVIOUS, {}})}));
    BOOST_CHECK_EQUAL(chain.Score(1, hash), 4U);
    // The alarm (downvote) takes one from every bundle with votes.
    for (int i{0}; i < 4; ++i) BOOST_REQUIRE(chain.Connect({Votes({VOTE_DOWNVOTE})}));
    BOOST_CHECK_EQUAL(chain.Score(1, hash), 0U);
    // An alarm with no bundle above zero does nothing, and so is not repeated: a bundle proposed after it
    // in the same block keeps its vote in the next one.
    const uint256 later{12};
    BOOST_REQUIRE(chain.Connect({Votes({VOTE_DOWNVOTE}), BundleScript(1, later)}));
    BOOST_CHECK_EQUAL(chain.Score(1, later), 1U);
    BOOST_REQUIRE(chain.Connect({VoteScript({VoteForm::REPEAT_PREVIOUS, {}})}));
    BOOST_CHECK_EQUAL(chain.Score(1, later), 1U);
    // In the other order the alarm sees the new bundle.
    BOOST_REQUIRE(chain.Connect({VoteScript({VoteForm::TWO_BYTES, {VOTE_DOWNVOTE}})}));
    BOOST_CHECK_EQUAL(chain.Score(1, later), 0U);
    // One vote per active sidechain, an index among its bundles.
    BOOST_CHECK(!chain.Connect({Votes({0, 0})}));
    BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-votes-size");
    BOOST_CHECK(!chain.Connect({Votes({7})}));
    BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-votes-index");
    BOOST_CHECK(!chain.Connect({Votes({0}), Votes({0})}));
    BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-multiple-votes");
}

BOOST_AUTO_TEST_CASE(enforcer_deposits_and_bmm)
{
    EnforcerChain chain;
    chain.Activate(MakeSidechain(1));
    chain.Activate(MakeSidechain(2));
    // A deposit to two sidechains in one transaction (Chains refuses it).
    CMutableTransaction both{chain.BaseTx()};
    both.vout.emplace_back(COIN, EscrowScript(1));
    both.vout.emplace_back(0, DestinationScript("one"));
    both.vout.emplace_back(2 * COIN, EscrowScript(2));
    both.vout.emplace_back(0, DestinationScript(""));
    BOOST_REQUIRE(chain.Connect({}, {both}));
    BOOST_CHECK_EQUAL(chain.deposits.size(), 2U);
    BOOST_CHECK_EQUAL(chain.scdb.GetSlot(1)->ctip.amount, COIN);
    BOOST_CHECK_EQUAL(chain.scdb.GetSlot(2)->ctip.amount, 2 * COIN);
    // The treasury output of an inactive slot is an ordinary output, of any value; so is one in a coinbase.
    CMutableTransaction inactive{chain.BaseTx()};
    inactive.vout.emplace_back(0, EscrowScript(7));
    BOOST_REQUIRE(chain.Connect({}, {inactive}, {CTxOut{COIN, EscrowScript(1)}}));
    BOOST_CHECK(!chain.scdb.IsActive(7));
    BOOST_CHECK_EQUAL(chain.scdb.GetSlot(1)->ctip.amount, COIN);
    // A treasury output spent without a new one, or not spent by the next deposit, is invalid.
    CMutableTransaction drain{chain.BaseTx()};
    drain.vin.emplace_back(chain.scdb.GetSlot(1)->ctip.outpoint);
    drain.vout.emplace_back(COIN, CScript() << OP_TRUE);
    BOOST_CHECK(!chain.Connect({}, {drain}));
    BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-escrow-spend");
    CMutableTransaction unspent{chain.BaseTx()};
    unspent.vout.emplace_back(2 * COIN, EscrowScript(1));
    unspent.vout.emplace_back(0, DestinationScript("x"));
    BOOST_CHECK(!chain.Connect({}, {unspent}));
    BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-escrow-unspent");

    // An M7 for an inactive slot is fine; an M8 needs the M7 of its slot and the parent block.
    BOOST_REQUIRE(chain.Connect({BmmAcceptScript(9, uint256{3})}));
    BmmRequest request;
    request.slot = 1;
    request.side_block_hash = uint256{5};
    request.prev_main_block_hash = chain.tip;
    CMutableTransaction m8{chain.BaseTx()};
    m8.vout.emplace_back(0, BmmRequestScript(request));
    BOOST_CHECK(!chain.Connect({}, {m8}));
    BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-bmm-not-accepted");
    BOOST_REQUIRE(chain.Connect({BmmAcceptScript(1, uint256{5})}, {m8}));
    BOOST_CHECK(!chain.Connect({BmmAcceptScript(1, uint256{5}), BmmAcceptScript(1, uint256{6})}));
    BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-multiple-bmm-accepts");
}

BOOST_AUTO_TEST_CASE(enforcer_activation_height)
{
    EnforcerChain chain;
    chain.params.activation_height = 5;
    // Before it, nothing is read: a block the rules would refuse is fine, and changes nothing.
    BOOST_REQUIRE(chain.Connect({BundleScript(2, uint256{7}), ProposalScript(MakeSidechain(2))}));
    BOOST_CHECK(chain.scdb.GetProposals().empty());
    while (chain.height < 4) BOOST_REQUIRE(chain.Connect());
    // From it, the rules.
    BOOST_CHECK(!chain.Connect({BundleScript(2, uint256{7})}));
    BOOST_REQUIRE(chain.Connect({ProposalScript(MakeSidechain(2))}));
    BOOST_CHECK_EQUAL(chain.scdb.GetProposals().size(), 1U);
}

BOOST_AUTO_TEST_CASE(enforcer_miner_policy)
{
    EnforcerChain chain;
    chain.Activate(MakeSidechain(2));
    BOOST_REQUIRE(chain.Connect({}, {chain.DepositTx(2, 10 * COIN)}));
    MinerState miner;
    std::string error;
    const CMutableTransaction blind{EnforcerChain::BlindBundle(COIN, 1000, 3)};
    const auto hash{miner.AddBundle(2, blind, error)};
    BOOST_REQUIRE(hash);
    miner.Prune(chain.scdb, chain.height + 1);
    // The bundle is proposed, then upvoted: the vote message first in the coinbase.
    auto additions{miner.CreateBlockAdditions(chain.scdb, chain.params, chain.tip, {}, chain.height + 1)};
    std::vector<CScript> scripts;
    for (const CTxOut& out : additions.coinbase_outputs) scripts.push_back(out.scriptPubKey);
    BOOST_REQUIRE(chain.Connect(scripts));
    BOOST_CHECK_EQUAL(chain.Score(2, *hash), 1U);
    for (int i{0}; i < 6; ++i) {
        miner.Prune(chain.scdb, chain.height + 1);
        additions = miner.CreateBlockAdditions(chain.scdb, chain.params, chain.tip, {}, chain.height + 1);
        scripts.clear();
        for (const CTxOut& out : additions.coinbase_outputs) scripts.push_back(out.scriptPubKey);
        BOOST_REQUIRE(!scripts.empty());
        BOOST_CHECK(ParseVoteScript(scripts.front()));
        std::vector<CMutableTransaction> withdrawals;
        for (const CTransactionRef& tx : additions.withdrawals) withdrawals.emplace_back(*tx);
        // Paid out once it has more votes than the threshold, counting the block's own vote.
        BOOST_REQUIRE(chain.Connect(scripts, withdrawals));
        if (!withdrawals.empty()) break;
    }
    BOOST_CHECK(chain.scdb.WasPaid(2, *hash) == true);
    // Proposed again by someone else, the paid bundle is neither upvoted nor paid by this node (policy).
    BOOST_REQUIRE(chain.Connect({BundleScript(2, *hash)}));
    miner.Prune(chain.scdb, chain.height + 1);
    additions = miner.CreateBlockAdditions(chain.scdb, chain.params, chain.tip, {}, chain.height + 1);
    BOOST_CHECK(additions.withdrawals.empty());
    for (const CTxOut& out : additions.coinbase_outputs) {
        if (const auto votes{ParseVoteScript(out.scriptPubKey)}) BOOST_CHECK(votes->votes.empty() || votes->votes[0] != 0);
    }
}

//! The index of a pending bundle in its slot, as a vote names it.
uint16_t IndexOf(const EnforcerChain& chain, SidechainId slot, const uint256& hash)
{
    const auto& bundles{chain.scdb.GetSlot(slot)->bundles};
    for (size_t i{0}; i < bundles.size(); ++i) {
        if (bundles[i].hash == hash) return static_cast<uint16_t>(i);
    }
    BOOST_FAIL("no such bundle");
    return 0;
}

BOOST_AUTO_TEST_CASE(enforcer_unpayable_mempool_withdrawal)
{
    // The votes of a block count before its transactions: a withdrawal that is valid on the chain as it
    // is (the mempool's view) can be invalid in the block this node builds, if the block's own vote takes
    // its bundle back to the threshold. Such a withdrawal is not among the bundles the block can pay.
    EnforcerChain chain;
    chain.Activate(MakeSidechain(2));
    BOOST_REQUIRE(chain.Connect({}, {chain.DepositTx(2, 10 * COIN)}));
    const CMutableTransaction blind_a{EnforcerChain::BlindBundle(COIN, 1000, 1)};
    const CMutableTransaction blind_b{EnforcerChain::BlindBundle(COIN, 1000, 2)};
    const uint256 a{CTransaction{blind_a}.GetHash().ToUint256()};
    const uint256 b{CTransaction{blind_b}.GetHash().ToUint256()};
    BOOST_REQUIRE(chain.Connect({BundleScript(2, a)}));
    BOOST_REQUIRE(chain.Connect({BundleScript(2, b)}));
    while (chain.Score(2, a) <= static_cast<uint32_t>(chain.params.bundle_threshold)) {
        BOOST_REQUIRE(chain.Connect({Votes({IndexOf(chain, 2, a)})}));
    }
    BOOST_REQUIRE_EQUAL(chain.Score(2, a), static_cast<uint32_t>(chain.params.bundle_threshold) + 1);
    const CMutableTransaction withdrawal_a{chain.WithdrawalTx(2, blind_a, COIN, 1000)};

    // A miner whose sidechain node handed it bundle B upvotes B, which takes A down to the threshold.
    MinerState miner;
    miner.SetChainDefaults(MinerState::UpvoteMode::HANDED, MinerState::AckPolicy::NEW_SLOTS);
    std::string error;
    BOOST_REQUIRE(miner.AddBundle(2, blind_b, error) == b);
    miner.Prune(chain.scdb, chain.height + 1);
    const auto additions{miner.CreateBlockAdditions(chain.scdb, chain.params, chain.tip, {}, chain.height + 1)};
    BOOST_CHECK(!additions.payable_bundles.contains(2) || !additions.payable_bundles.at(2).contains(a));
    std::vector<CScript> scripts;
    for (const CTxOut& out : additions.coinbase_outputs) scripts.push_back(out.scriptPubKey);
    // With A's withdrawal, that block would be invalid.
    BOOST_CHECK(!chain.Connect(scripts, {withdrawal_a}));
    BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-withdrawal-score");

    // A miner that upvotes the leading bundle (A) can pay it, and the block that does is valid.
    MinerState leading;
    leading.Prune(chain.scdb, chain.height + 1);
    const auto paying{leading.CreateBlockAdditions(chain.scdb, chain.params, chain.tip, {}, chain.height + 1)};
    BOOST_REQUIRE(paying.payable_bundles.contains(2));
    BOOST_CHECK(paying.payable_bundles.at(2).contains(a));
    BOOST_CHECK(!paying.payable_bundles.at(2).contains(b));
    scripts.clear();
    for (const CTxOut& out : paying.coinbase_outputs) scripts.push_back(out.scriptPubKey);
    BOOST_CHECK(chain.Connect(scripts, {withdrawal_a}));
}

BOOST_AUTO_TEST_CASE(enforcer_safety_activation_block)
{
    // Our withdrawal bundle safety rules for the enforcer (branch withdrawal-bundle-safety), from an
    // activation height: two chains of the same blocks, one with it, one without (every network today).
    EnforcerChain on, off;
    for (EnforcerChain* chain : {&on, &off}) {
        chain->params.upvote_expiry_blocks = 9;
        chain->params.idle_expiry_blocks = 10;
    }
    BOOST_CHECK(!off.params.SafetyActive(std::numeric_limits<int>::max()));
    on.params.safety_activation_height = 20;
    BOOST_CHECK(!on.params.SafetyActive(19));
    BOOST_CHECK(on.params.SafetyActive(20));
    const CMutableTransaction blind_a{EnforcerChain::BlindBundle(COIN, 1000, 1)};
    const CMutableTransaction blind_b{EnforcerChain::BlindBundle(COIN, 1000, 2)};
    const uint256 a{blind_a.GetHash().ToUint256()}, b{blind_b.GetHash().ToUint256()}, c{0xc}, e{0xe};
    CMutableTransaction second_payout;
    for (EnforcerChain* chain : {&on, &off}) {
        chain->Activate(MakeSidechain(2)); // up to 7
        BOOST_REQUIRE(chain->Connect({}, {chain->DepositTx(2, 10 * COIN)})); // 8
        BOOST_REQUIRE(chain->Connect({BundleScript(2, a), BundleScript(2, b), Votes({1})})); // 9
        for (int i{0}; i < 3; ++i) BOOST_REQUIRE(chain->Connect({Votes({1})}));        // 10-12
        // 13: b paid out; c proposed in the same block (after the votes); a still pending (no single payout yet).
        BOOST_REQUIRE(chain->Connect({Votes({1}), BundleScript(2, c)}, {chain->WithdrawalTx(2, blind_b, COIN, 1000)}));
        BOOST_CHECK(chain->scdb.WasPaid(2, b) == true);
        BOOST_CHECK_EQUAL(chain->Score(2, a), 0U);
        BOOST_CHECK_EQUAL(chain->Score(2, c), 1U);
        // 14: b proposed again, which the enforcer's rules allow; 15-19: voted past the threshold.
        BOOST_REQUIRE(chain->Connect({BundleScript(2, b)}));
        BOOST_REQUIRE(chain->Connect({Votes({IndexOf(*chain, 2, b)})}));
        BOOST_REQUIRE(chain->Connect({Votes({IndexOf(*chain, 2, b)}), BundleScript(2, e)})); // 16: e proposed
        for (int i{0}; i < 3; ++i) BOOST_REQUIRE(chain->Connect({Votes({IndexOf(*chain, 2, b)})}));
        BOOST_REQUIRE_EQUAL(chain->height, 19);
        BOOST_CHECK_EQUAL(chain->Score(2, b), 6U);
    }
    BOOST_CHECK(on.scdb == off.scdb);

    // The mempool checks a withdrawal under the rules of the next block: below the height, paying b again
    // is valid (as with the enforcer today); from it, a bundle paid out is never paid again.
    second_payout = on.WithdrawalTx(2, blind_b, COIN, 1000);
    for (const int height : {19, 20}) {
        SidechainDB scdb{on.scdb.Subset({2})};
        SidechainDB::EscrowOutputs escrow_outputs{scdb.GetEscrowOutputs()};
        BlockUndo undo;
        std::string reason;
        const bool valid{scdb.ConnectTx(CTransaction{second_payout}, on.params, escrow_outputs, undo, nullptr, reason, height)};
        BOOST_CHECK_EQUAL(valid, height < 20);
        if (!valid) BOOST_CHECK_EQUAL(reason, "bad-dc-withdrawal-paid");
    }

    // 20, the activation block: a (too old anyway) and c, pending at the payout of 13, fail as single payout
    // would have failed them; b, proposed again, fails (paid-bundle memory) and stays remembered as paid at 13;
    // e, proposed after the payout, stays.
    BOOST_REQUIRE(on.Connect());
    BOOST_REQUIRE(off.Connect());
    BOOST_CHECK_EQUAL(on.Score(2, c), 0xFFFFFFFFU);
    BOOST_CHECK(on.scdb.WasPaid(2, c) == false);
    BOOST_CHECK_EQUAL(on.Score(2, b), 0xFFFFFFFFU);
    BOOST_CHECK(on.scdb.WasPaid(2, b) == true);
    BOOST_CHECK_EQUAL(on.scdb.GetClosed(2, b)->height, 13);
    BOOST_CHECK_NE(on.Score(2, e), 0xFFFFFFFFU);
    BOOST_CHECK_EQUAL(on.scdb.GetSlot(2)->bundles.size(), 1U);
    // Without the rules: c and b still pending, and b can be paid a second time.
    BOOST_CHECK_EQUAL(off.Score(2, c), 0U);
    BOOST_CHECK_EQUAL(off.Score(2, b), 6U);
    BOOST_REQUIRE(off.Connect({Votes({IndexOf(off, 2, b)})}, {off.WithdrawalTx(2, blind_b, COIN, 1000)}));
    // With them: b cannot even be proposed again.
    BOOST_CHECK(!on.Connect({BundleScript(2, b)}));
    BOOST_CHECK_EQUAL(on.reject_reason, "bad-dc-bundle-paid");
}

BOOST_AUTO_TEST_CASE(enforcer_safety_rules)
{
    // Single payout, paid-bundle memory, upvote and idle expiry: from the activation height (here the genesis)
    // against never (the default).
    for (const bool active : {false, true}) {
        EnforcerChain chain;
        chain.params.upvote_expiry_blocks = 8;
        chain.params.idle_expiry_blocks = 6;
        if (active) chain.params.safety_activation_height = 0;
        chain.Activate(MakeSidechain(2));
        BOOST_REQUIRE(chain.Connect({}, {chain.DepositTx(2, 10 * COIN)}));
        const CMutableTransaction blind_d{EnforcerChain::BlindBundle(COIN, 1000, 4)};
        const uint256 c{0xc}, d{blind_d.GetHash().ToUint256()}, e{0xe}, f{0xf};
        BOOST_REQUIRE(chain.Connect({BundleScript(2, c), BundleScript(2, d), Votes({1})}));
        for (int i{0}; i < 3; ++i) BOOST_REQUIRE(chain.Connect({Votes({1})}));
        BOOST_REQUIRE(chain.Connect({Votes({1})}, {chain.WithdrawalTx(2, blind_d, COIN, 1000)}));
        // Single payout: the other bundle of the slot fails with the payout.
        BOOST_CHECK_EQUAL(chain.Score(2, c), active ? 0xFFFFFFFFU : 0U);
        if (active) BOOST_CHECK(chain.scdb.WasPaid(2, c) == false);
        // Paid-bundle memory: proposing the paid bundle again is invalid.
        BOOST_CHECK_EQUAL(chain.Connect({BundleScript(2, d)}), !active);
        if (active) {
            BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-bundle-paid");
            BOOST_REQUIRE(chain.Connect());
        }
        // Upvote expiry: a bundle that 8 blocks in a row did not upvote fails, at age 8 (the enforcer's
        // rules alone keep it to its maximum age, 10).
        BOOST_REQUIRE(chain.Connect({BundleScript(2, e)}));
        const int e_height{chain.height};
        while (chain.height < e_height + 7) BOOST_REQUIRE(chain.Connect());
        BOOST_CHECK_EQUAL(chain.Score(2, e), 1U);
        BOOST_REQUIRE(chain.Connect());
        BOOST_CHECK_EQUAL(chain.Score(2, e), active ? 0xFFFFFFFFU : 1U);
        // Idle expiry: a bundle with no vote fails at age 6 (before its upvote expiry).
        BOOST_REQUIRE(chain.Connect({BundleScript(2, f)}));
        const int f_height{chain.height};
        BOOST_REQUIRE(chain.Connect({Votes({VOTE_DOWNVOTE})}));
        BOOST_CHECK_EQUAL(chain.Score(2, f), 0U);
        while (chain.height < f_height + 5) BOOST_REQUIRE(chain.Connect());
        BOOST_CHECK_EQUAL(chain.Score(2, f), 0U);
        BOOST_REQUIRE(chain.Connect());
        BOOST_CHECK_EQUAL(chain.Score(2, f), active ? 0xFFFFFFFFU : 0U);
        // An upvote within the window keeps a bundle, the block's own included: here in the 8th block
        // after its proposal, which would otherwise fail it.
        if (active) {
            const uint256 g{0x9};
            BOOST_REQUIRE(chain.Connect({BundleScript(2, g)}));
            for (int i{0}; i < 9; ++i) {
                if (i == 7) {
                    BOOST_REQUIRE(chain.Connect({Votes({IndexOf(chain, 2, g)})}));
                } else {
                    BOOST_REQUIRE(chain.Connect());
                }
            }
            BOOST_CHECK_EQUAL(chain.Score(2, g), 2U);
        }
    }
}

BOOST_AUTO_TEST_CASE(enforcer_message_limits)
{
    // Our message limits for the enforcer (branch resource-limits-canonical): valid below the activation
    // height, invalid from it.
    EnforcerChain chain;
    chain.params.max_pending_bundles = 2;
    chain.Activate(MakeSidechain(2));
    chain.Activate(MakeSidechain(3));
    BOOST_REQUIRE(chain.Connect({}, {chain.DepositTx(2, 10 * COIN)}));
    const std::string long_destination(MAX_DEPOSIT_DESTINATION_SIZE + 1, 'x');
    const auto two_slots{[&] {
        CMutableTransaction tx{chain.BaseTx()};
        for (const SidechainId slot : {SidechainId{2}, SidechainId{3}}) {
            if (const Slot* state{chain.scdb.GetSlot(slot)}; state->has_ctip) tx.vin.emplace_back(state->ctip.outpoint);
            const Slot* state{chain.scdb.GetSlot(slot)};
            tx.vout.emplace_back((state->has_ctip ? state->ctip.amount : 0) + COIN, EscrowScript(slot));
            tx.vout.emplace_back(0, DestinationScript("d"));
        }
        return tx;
    }};
    const auto check_all{[&](bool limits) {
        const SidechainDB before{chain.scdb};
        const int height{chain.height};
        // A two byte vote message of abstains and alarms only.
        BOOST_CHECK_EQUAL(chain.Connect({VoteScript({VoteForm::TWO_BYTES, {VOTE_ABSTAIN, VOTE_DOWNVOTE}})}), !limits);
        if (limits) BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-votes-noncanonical");
        // Two bundles for a slot in a block.
        BOOST_CHECK_EQUAL(chain.Connect({BundleScript(2, uint256{0x51}), BundleScript(2, uint256{0x52})}), !limits);
        if (limits) BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-multiple-bundles");
        // A deposit to a destination of 101 bytes.
        BOOST_CHECK_EQUAL(chain.Connect({}, {chain.DepositTx(2, chain.scdb.GetSlot(2)->ctip.amount + COIN, long_destination)}), !limits);
        if (limits) BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-deposit-destination");
        // Two treasury outputs in one transaction.
        BOOST_CHECK_EQUAL(chain.Connect({}, {two_slots()}), !limits);
        if (limits) BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-multiple-treasury-outputs");
        BOOST_CHECK_EQUAL(chain.height, limits ? height : height + 4);
        if (limits) BOOST_CHECK(chain.scdb == before);
    }};
    check_all(/*limits=*/false);
    // Below the height the slot holds more bundles than the cap (2).
    BOOST_CHECK_EQUAL(chain.scdb.GetSlot(2)->bundles.size(), 2U);
    BOOST_REQUIRE(chain.Connect({BundleScript(2, uint256{0x53})}));
    BOOST_CHECK_EQUAL(chain.scdb.GetSlot(2)->bundles.size(), 3U);
    // Let those fail (maximum age), then the limits from the next block.
    while (!chain.scdb.GetSlot(2)->bundles.empty()) BOOST_REQUIRE(chain.Connect());
    chain.params.limits_activation_height = chain.height + 1;
    check_all(/*limits=*/true);

    // Pending cap: a full slot fails its weakest bundle (lowest score, oldest first) to make room, if that has
    // a new bundle's score or less; otherwise the proposal is invalid.
    const uint256 x{0x61}, y{0x62}, z{0x63}, w{0x64}, v{0x65};
    BOOST_REQUIRE(chain.Connect({BundleScript(2, x)}));
    for (int i{0}; i < 3; ++i) BOOST_REQUIRE(chain.Connect({Votes({0, VOTE_ABSTAIN})}));
    BOOST_REQUIRE(chain.Connect({Votes({0, VOTE_ABSTAIN}), BundleScript(2, y)}));
    BOOST_CHECK_EQUAL(chain.Score(2, x), 5U);
    BOOST_CHECK_EQUAL(chain.Score(2, y), 1U);
    BOOST_REQUIRE(chain.Connect({BundleScript(2, z)}));
    BOOST_CHECK_EQUAL(chain.Score(2, y), 0xFFFFFFFFU);
    BOOST_CHECK(chain.scdb.WasPaid(2, y) == false);
    BOOST_CHECK_EQUAL(chain.Score(2, z), 1U);
    BOOST_REQUIRE(chain.Connect({Votes({1, VOTE_ABSTAIN})})); // z 2, x 4: the weakest has more votes than a new bundle
    BOOST_CHECK(!chain.Connect({BundleScript(2, w)}));
    BOOST_CHECK_EQUAL(chain.reject_reason, "bad-dc-bundles-full");
    BOOST_REQUIRE(chain.Connect({Votes({0, VOTE_ABSTAIN})})); // x 5, z 1: z goes
    BOOST_REQUIRE(chain.Connect({BundleScript(2, v)}));
    BOOST_CHECK_EQUAL(chain.Score(2, z), 0xFFFFFFFFU);
    BOOST_CHECK_EQUAL(chain.Score(2, v), 1U);
    BOOST_CHECK_EQUAL(chain.scdb.GetSlot(2)->bundles.size(), 2U);
}

BOOST_AUTO_TEST_CASE(enforcer_miner_policy_under_safety_rules)
{
    // The block template under our rules for the enforcer, active: its blocks are valid, one payout per
    // sidechain, which fails the slot's other bundles.
    EnforcerChain chain;
    chain.params.safety_activation_height = 0;
    chain.params.limits_activation_height = 0;
    chain.params.upvote_expiry_blocks = 5;
    chain.params.idle_expiry_blocks = 5;
    chain.Activate(MakeSidechain(2));
    BOOST_REQUIRE(chain.Connect({}, {chain.DepositTx(2, 10 * COIN)}));
    MinerState miner;
    std::string error;
    const CMutableTransaction blind{EnforcerChain::BlindBundle(COIN, 1000, 5)};
    const auto hash{miner.AddBundle(2, blind, error)};
    BOOST_REQUIRE(hash);
    // Someone else's bundle, pending beside ours.
    BOOST_REQUIRE(chain.Connect({BundleScript(2, uint256{0x77})}));
    for (int i{0}; i < 10 && chain.scdb.WasPaid(2, *hash) != true; ++i) {
        miner.Prune(chain.scdb, chain.height + 1);
        const auto additions{miner.CreateBlockAdditions(chain.scdb, chain.params, chain.tip, {}, chain.height + 1)};
        std::vector<CScript> scripts;
        for (const CTxOut& out : additions.coinbase_outputs) scripts.push_back(out.scriptPubKey);
        std::vector<CMutableTransaction> withdrawals;
        for (const CTransactionRef& tx : additions.withdrawals) withdrawals.emplace_back(*tx);
        BOOST_REQUIRE_MESSAGE(chain.Connect(scripts, withdrawals), chain.reject_reason);
    }
    BOOST_CHECK(chain.scdb.WasPaid(2, *hash) == true);
    BOOST_CHECK(chain.scdb.GetSlot(2)->bundles.empty());
}

BOOST_AUTO_TEST_SUITE_END()
