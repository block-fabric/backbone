// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include <consensus/consensus.h>
#include <script/verify_flags.h>
#include <uint256.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <vector>

namespace Consensus {

/**
 * A buried deployment is one where the height of the activation has been hardcoded into
 * the client implementation long after the consensus change has activated. See BIP 90.
 * Consensus changes for which the new rules are enforced from genesis are not listed here.
 */
enum BuriedDeployment : int16_t {
    // buried deployments get negative values to avoid overlap with DeploymentPos
    DEPLOYMENT_HEIGHTINCB = std::numeric_limits<int16_t>::min(),
    DEPLOYMENT_CLTV,
    DEPLOYMENT_DERSIG,
    DEPLOYMENT_CSV,
    // SCRIPT_VERIFY_WITNESS is enforced from genesis, but the check for downloading
    // missing witness data is not. BIP 147 also relies on hardcoded activation height.
    DEPLOYMENT_SEGWIT,
};
constexpr bool ValidDeployment(BuriedDeployment dep) { return dep <= DEPLOYMENT_SEGWIT; }

enum DeploymentPos : uint16_t {
    DEPLOYMENT_TESTDUMMY,
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in deploymentinfo.cpp
    // Removing an entry may require bumping MinBIP9WarningHeight.
    MAX_VERSION_BITS_DEPLOYMENTS
};
constexpr bool ValidDeployment(DeploymentPos dep) { return dep < MAX_VERSION_BITS_DEPLOYMENTS; }

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit{28};
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime{NEVER_ACTIVE};
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout{NEVER_ACTIVE};
    /** If lock in occurs, delay activation until at least this block
     *  height.  Note that activation will only occur on a retarget
     *  boundary.
     */
    int min_activation_height{0};
    /** Period of blocks to check signalling in (usually retarget period, ie params.DifficultyAdjustmentInterval()) */
    uint32_t period{2016};
    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargeting period,
     * which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t threshold{1916};

    /** Constant for nTimeout very far in the future. */
    static constexpr int64_t NO_TIMEOUT = std::numeric_limits<int64_t>::max();

    /** Special value for nStartTime indicating that the deployment is always active.
     *  This is useful for testing, as it means tests don't need to deal with the activation
     *  process (which takes at least 3 BIP9 intervals). Only tests that specifically test the
     *  behaviour during activation cannot use this. */
    static constexpr int64_t ALWAYS_ACTIVE = -1;

    /** Special value for nStartTime indicating that the deployment is never active.
     *  This is useful for integrating the code changes for a new feature
     *  prior to deploying it on some or all networks. */
    static constexpr int64_t NEVER_ACTIVE = -2;
};

/**
 * Parameters of the drivechain (sidechain escrow and blind merged mining) rules.
 * All periods and thresholds are counted in blocks.
 */
struct DrivechainParams {
    /** Number of sidechain slots. Slots at or above this number are not subject to any drivechain rule. */
    uint32_t max_sidechains{256};
    /** Age at which a proposal for an empty slot activates. */
    int activation_period{7200};
    /** A proposal is rejected once it has gone this many blocks without an ack. */
    int activation_max_failures{3599};
    /** Age at which a proposal for a slot that is already in use activates, replacing the sidechain in it. */
    int replacement_period{64800};
    /** Number of blocks a withdrawal bundle has to collect its work score. */
    int withdrawal_period{129600};
    /** Work score a withdrawal bundle needs before it can be paid out. */
    int withdrawal_min_score{64800};
    /** Maximum number of pending withdrawal bundles per sidechain. */
    uint32_t max_pending_bundles{64};
    /**
     * A bundle that has been pending idle_expiry_blocks or more and has a score of 0 fails. Miners
     * downvote the bundles their sidechain node does not vouch for -- a bundle a reorg of the
     * sidechain left behind, or one proposed to get in the way -- which so go in that many blocks,
     * rather than the whole withdrawal period.
     */
    int idle_expiry_blocks{1008};
    /**
     * A bundle that upvote_expiry_blocks blocks in a row did not upvote, counting from the block
     * that proposed it or from its last upvote, fails. Whoever proposes a bundle nobody vouches for
     * (to stall the withdrawals and refunds of a sidechain, whose software waits on what is pending)
     * has to keep upvoting it to keep it, rather than wait for the idle expiry.
     *
     * A failed bundle is forgotten withdrawal_period blocks after it failed, so that proposals
     * nobody votes for do not add to the state forever (see SidechainDB::ForgetFailedBundles); one
     * that no block ever upvoted, unvoted_forget_blocks blocks after it failed.
     */
    int upvote_expiry_blocks{144};
    int unvoted_forget_blocks{1008};
    /**
     * From this height on, a vote message (M4) in the two byte form is valid only if a vote in it
     * needs two bytes: a bundle index above 253. ABSTAIN and DOWNVOTE fit in one byte (0xFF, 0xFE),
     * so a message of only those, in two bytes, is the non-canonical form of a one byte message.
     * (Before it, any two byte message with a value above 253, ABSTAIN and DOWNVOTE included, was.)
     */
    int canonical_votes_height{0};

    /**
     * Which rules: Chains' own (CHAINS), or those of the bip300301_enforcer of LayerTwo Labs (ENFORCER),
     * which the eCash networks follow. The enforcer counts votes rather than age: the fields below are
     * its Thresholds, and the Chains-only rules above (single payout, closed bundle memory, idle and
     * upvote expiry, the queue cap, canonical votes) are no consensus there; the block template keeps
     * them as policy (see node::BlockAssembler and drivechain::Miner), until the heights below
     * (safety_activation_height, limits_activation_height) make the enforcer's form of them consensus.
     * idle_expiry_blocks, upvote_expiry_blocks and max_pending_bundles above are then the enforcer's values.
     */
    int32_t rules{RULES_CHAINS};
    static constexpr int32_t RULES_CHAINS{0};
    static constexpr int32_t RULES_ENFORCER{1};
    bool EnforcerRules() const { return rules == RULES_ENFORCER; }
    /**
     * Blocks below this height are not scanned for drivechain messages, deposits or withdrawals (the
     * enforcer's bip300_activation_height): on eCash, the Bitcoin history before the fork.
     */
    int activation_height{0};
    /** Enforcer rules: an unused slot activates a proposal with more acks than this, within unused_slot_max_age blocks. */
    int unused_slot_threshold{1815};
    int unused_slot_max_age{2016};
    /** Enforcer rules: the same for a slot in use (replacing the sidechain in it). */
    int used_slot_threshold{13150};
    int used_slot_max_age{26300};
    /** Enforcer rules: a bundle is paid out with more votes than this; it fails when older than bundle_max_age blocks. */
    int bundle_threshold{13150};
    int bundle_max_age{26300};

    /**
     * Enforcer rules: the withdrawal bundle safety rules of our pull request to the bip300301_enforcer
     * (LayerTwo-Labs/bip300301_enforcer, branch withdrawal-bundle-safety, WithdrawalBundleSafety), consensus from
     * this height on; NEVER (the default on every network): never, the block template keeps them as policy.
     * From it on: paying a bundle out fails the other pending bundles of its sidechain (single payout); a bundle
     * paid out is never proposed (M3) nor paid (M6) again, and fails if it is pending again (paid-bundle memory);
     * a bundle fails once upvote_expiry_blocks blocks in a row, this one included, did not upvote it, or once it
     * is idle_expiry_blocks old with no vote (upvote and idle expiry); and at that very height, a bundle pending
     * since (or proposed in) the block of the latest payout of its sidechain fails, as single payout would have
     * failed it then. (The Chains rules have their own forms of these rules, always on.)
     */
    int safety_activation_height{NEVER};
    /**
     * Enforcer rules: the limits on BIP300 messages of our pull request to the enforcer (branch
     * resource-limits-canonical, MessageLimits), consensus from this height on; NEVER: never. From it on: a slot
     * holds at most max_pending_bundles pending bundles, an M3 into a full slot failing its weakest bundle (the
     * lowest score, oldest first) if that has a score of NEW_BUNDLE_SCORE or less, and being invalid otherwise
     * (pending cap); a block proposes at most one bundle per slot; a vote message in the two byte form carries a
     * bundle index above 253 (canonical votes); a deposit names a destination of at most
     * MAX_DEPOSIT_DESTINATION_SIZE bytes; a transaction creates at most one treasury output.
     */
    int limits_activation_height{NEVER};
    static constexpr int NEVER{-1};
    bool SafetyActive(int height) const { return EnforcerRules() && safety_activation_height != NEVER && height >= safety_activation_height; }
    bool LimitsActive(int height) const { return EnforcerRules() && limits_activation_height != NEVER && height >= limits_activation_height; }
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    /**
     * eCash: the networks of ecash-com/bitcoin. Their rules beside the fields below (the getblocktemplate
     * rule bip300301, the replay protection of the wallet, the drivechain policies of the miner) follow it.
     */
    bool ecash{false};
    /**
     * eCash: height of the fork block, which has to carry ecash_fork_bits (the difficulty is reset there,
     * which is what splits the chain from Bitcoin's). A multiple of the retarget interval, where the
     * retarget computes it (see CalculateNextWorkRequired); 0 for none.
     */
    int ecash_fork_height{0};
    uint32_t ecash_fork_bits{0};
    /**
     * eCash replay protection: a transaction with this nLockTime is final at any height and time, and
     * fails every OP_CHECKLOCKTIMEVERIFY; stock Bitcoin reads it as a height out of reach, so it never
     * confirms there. 499999998 (LOCKTIME_THRESHOLD - 2) on eCash's main and test networks,
     * 499999999 (LOCKTIME_THRESHOLD - 1) on betanet; 0 for none.
     */
    uint32_t replay_locktime{0};
    /**
     * eCash: OP_DRIVECHAIN is OP_NOP8 (0xb7), and a script of four bytes that starts with it is
     * anyone-can-spend, at any height. Otherwise (Chains) it is OP_NOP5, and only the treasury script
     * OP_DRIVECHAIN 0x01 <slot> OP_TRUE has its meaning.
     */
    bool drivechain_nop8{false};
    /**
     * eCash: transactions whose input scripts are not checked (eCash's "repurpose transactions", which
     * reassign coins by consensus). Everything else about them is, as for any transaction.
     */
    const std::set<uint256>* repurpose_txids{nullptr};
    bool IsRepurposeTx(const uint256& txid) const { return repurpose_txids && repurpose_txids->contains(txid); }
    /**
     * Hashes of blocks that
     * - are known to be consensus valid, and
     * - buried in the chain, and
     * - fail if the default script verify flags are applied.
     */
    std::map<uint256, script_verify_flags> script_flag_exceptions;
    /** Block height and hash at which BIP34 becomes active */
    int BIP34Height;
    uint256 BIP34Hash;
    /** Block height at which BIP65 becomes active */
    int BIP65Height;
    /** Block height at which BIP66 becomes active */
    int BIP66Height;
    /** Block height at which CSV (BIP68, BIP112 and BIP113) becomes active */
    int CSVHeight;
    /** Block height at which Segwit (BIP141, BIP143 and BIP147) becomes active.
     * Note that segwit v0 script rules are enforced on all blocks except the
     * BIP 16 exception blocks. */
    int SegwitHeight;
    /** Don't warn about unknown BIP 9 activations below this height.
     * This prevents us from warning about the CSV, segwit and taproot activations. */
    int MinBIP9WarningHeight;
    std::array<BIP9Deployment,MAX_VERSION_BITS_DEPLOYMENTS> vDeployments;
    /** Proof of work parameters */
    uint256 powLimit;
    bool fPowAllowMinDifficultyBlocks;
    /**
      * Enforce BIP94 timewarp attack mitigation. Where minimum difficulty blocks are allowed this also enforces
      * the block storm mitigation.
      */
    bool enforce_BIP94;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetTimespan;
    /**
     * Half-life, in seconds, of the aserti3 difficulty algorithm, which retargets
     * every block. Zero disables it in favour of the periodic nPowTargetTimespan
     * retarget.
     */
    int64_t asert_half_life{0};
    /**
     * Height of the aserti3 anchor block. Blocks below it are mined at the
     * proof of work limit; the anchor's own target is calibrated from the time
     * those blocks took, and aserti3 retargets every block after it.
     */
    int asert_anchor_height{0};
    DrivechainParams drivechain;
    /** Maximum weight of a block. */
    uint32_t max_block_weight{MAX_BLOCK_WEIGHT};
    /**
     * Maximum weight of the transactions of a block other than the coinbase.
     * Where this is below max_block_weight, the difference is set aside for
     * the coinbase, which carries the drivechain messages and the payouts of
     * mining pools.
     */
    uint32_t max_block_tx_weight{MAX_BLOCK_WEIGHT};
    /** Number of blocks before the outputs of a coinbase can be spent. */
    int coinbase_maturity{COINBASE_MATURITY};
    /** Upper bound for the serialized size of a block; a sanity limit, not a consensus rule. */
    uint32_t MaxBlockSerializedSize() const { return std::max<uint32_t>(MAX_BLOCK_SERIALIZED_SIZE, max_block_weight); }
    std::chrono::seconds PowTargetSpacing() const
    {
        return std::chrono::seconds{nPowTargetSpacing};
    }
    int64_t DifficultyAdjustmentInterval() const { return nPowTargetTimespan / nPowTargetSpacing; }
    /** The best chain should have at least this much work */
    uint256 nMinimumChainWork;
    /** By default assume that the signatures in ancestors of this block are valid */
    uint256 defaultAssumeValid;

    /**
     * If true, witness commitments contain a payload equal to a Bitcoin Script solution
     * to the signet challenge. See BIP325.
     */
    bool signet_blocks{false};
    std::vector<uint8_t> signet_challenge;

    int DeploymentHeight(BuriedDeployment dep) const
    {
        switch (dep) {
        case DEPLOYMENT_HEIGHTINCB:
            return BIP34Height;
        case DEPLOYMENT_CLTV:
            return BIP65Height;
        case DEPLOYMENT_DERSIG:
            return BIP66Height;
        case DEPLOYMENT_CSV:
            return CSVHeight;
        case DEPLOYMENT_SEGWIT:
            return SegwitHeight;
        } // no default case, so the compiler can warn about missing cases
        return std::numeric_limits<int>::max();
    }
};

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
