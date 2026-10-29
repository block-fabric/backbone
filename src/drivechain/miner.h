// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_DRIVECHAIN_MINER_H
#define BITCOIN_DRIVECHAIN_MINER_H

#include <consensus/amount.h>
#include <consensus/params.h>
#include <drivechain/scdb.h>
#include <drivechain/sidechain.h>
#include <primitives/transaction.h>
#include <serialize.h>
#include <sync.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/fs.h>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace drivechain {

/** What the drivechain rules make a miner add to a block. */
struct BlockAdditions {
    //! Messages for the coinbase, as zero value outputs.
    std::vector<CTxOut> coinbase_outputs;
    //! Withdrawal bundles that can be paid out, to be placed after all other transactions.
    std::vector<CTransactionRef> withdrawals;
    //! Fee of each of the withdrawals.
    std::vector<CAmount> withdrawal_fees;
    /**
     * The bundles a withdrawal (M6) of this block can pay, by slot: those with the score once the
     * block's own votes count, and not out of time (in the enforcer's order: the coinbase, then
     * the bundles that fail, then the transactions). A withdrawal from the mempool for any other
     * bundle would make the block invalid, so it stays out of the template (as the enforcer's
     * "keep unpayable mempool M6s out of served templates").
     */
    std::map<SidechainId, std::set<uint256>> payable_bundles;
};

/**
 * What this node does with the drivechain decisions that are left to miners:
 * the sidechains it proposes and acks, the withdrawal bundles it has been
 * handed by sidechains, and how it votes on them. None of this is consensus;
 * it only shapes the blocks this node builds.
 */
class MinerState
{
public:
    /**
     * Which proposals to ack without an ack of SetAck (the enforcer's AckAllProposalsPolicy): none,
     * those for a slot without a sidechain, or all of them (also those that would replace one).
     */
    enum class AckPolicy : uint8_t { NONE = 0, NEW_SLOTS = 1, ALL = 2 };
    /**
     * Which bundle the default vote UPVOTE upvotes on a slot no sidechain node spoke for:
     * LEADING (Backbone's default) the leading bundle, whatever it pays -- a fraudulent bundle
     * too, if it leads; FOLLOW the one leading by 50; HANDED none (only what a node handed; the
     * default on eCash's main network).
     * Where a sidechain node handed a bundle, that one is upvoted in every mode, and where it
     * said there is none or handed another one, no other bundle is upvoted.
     */
    enum class UpvoteMode : uint8_t { HANDED = 0, FOLLOW = 1, LEADING = 2 };

    /**
     * Keep the state in this file: load what it holds, and write it back
     * whenever something changes.
     * @return false if the file exists but cannot be read
     */
    bool Load(const fs::path& path) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * The vote settings of a node with nothing saved (and what a file from before version 7, which
     * kept the old defaults, takes): set them before Load. The defaults of the network, see
     * DefaultVoteSettings; a saved choice and the options (-defaultwithdrawalvote,
     * -sidechainackpolicy) go over them.
     */
    void SetChainDefaults(UpvoteMode mode, AckPolicy policy) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /** Propose a sidechain in the next block this node builds. */
    void AddProposal(const Sidechain& sidechain) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    bool RemoveProposal(const uint256& hash) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    std::vector<Sidechain> GetProposals() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /** Whether to ack the proposal with this hash in the blocks this node builds. */
    /**
     * Acks are for a proposal in a slot: the same sidechain proposed in another slot, or proposed
     * again in the slot it already holds, is another question, which an operator answers anew.
     */
    void SetAck(SidechainId slot, const uint256& proposal_hash, bool ack) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    /** Stop acking a proposal, in every slot. */
    void ClearAcks(const uint256& proposal_hash) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    bool IsAcked(SidechainId slot, const uint256& proposal_hash) const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    /** The acks of SetAck. */
    std::vector<std::pair<SidechainId, uint256>> GetAcks() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    //! Backbone acks every proposal by default (only new slots on eCash's main network, see
    //! DefaultVoteSettings); an operator opts out per proposal, per slot or here.
    void SetAckPolicy(AckPolicy policy) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    AckPolicy GetAckPolicy() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    /**
     * Never ack this proposal in this slot, whatever the policy (a NACK); false lifts it. An ack
     * of SetAck(true) lifts it too, and SetAck(false) sets it.
     */
    void SetNack(SidechainId slot, const uint256& proposal_hash, bool nack) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    std::vector<std::pair<SidechainId, uint256>> GetNacks() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    /** Ack (true) or NACK (false) every proposal of a slot, over the policy; nullopt: the policy. */
    void SetSlotAck(SidechainId slot, std::optional<bool> ack) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    std::map<SidechainId, bool> GetSlotAcks() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    /**
     * Whether the blocks of this node ack this proposal: its NACK, else its ack, else the override of
     * its slot, else the policy (`slot_active`: whether the slot holds a sidechain, for NEW_SLOTS).
     */
    bool WantsAck(SidechainId slot, const uint256& proposal_hash, bool slot_active) const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    /** The votes of SetVote. */
    std::map<SidechainId, Vote> GetVotes() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * Remember a withdrawal bundle received from a sidechain, in blind form.
     * It is proposed in the next block and paid out once it has the work score.
     * @return the hash of the bundle, or nullopt with `error` set
     */
    std::optional<uint256> AddBundle(SidechainId slot, const CMutableTransaction& blind_tx, std::string& error) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    std::optional<CMutableTransaction> GetBundle(SidechainId slot, const uint256& hash) const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    /**
     * The word of the sidechain node of `slot` on its bundle: the one its chain has (handed before with
     * AddBundle), or none. Votes and proposals follow it. @return false if `hash` was never handed.
     */
    bool Vouch(SidechainId slot, const std::optional<uint256>& hash) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    /** What the sidechain node of `slot` vouches for: nothing known, none (null), or a bundle. */
    std::optional<uint256> Vouched(SidechainId slot) const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    std::vector<std::pair<SidechainId, uint256>> GetBundles() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /** How to vote on the bundles of a sidechain. Sidechains without a vote get the default. */
    void SetVote(SidechainId slot, const Vote& vote) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    void ClearVote(SidechainId slot) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    /** Why ResolveVote votes as it does on a slot. */
    enum class VoteReason : uint8_t {
        OVERRIDE,   //!< the vote of SetVote for the slot
        VERIFIED,   //!< the bundle a sidechain node handed to this node (receivewithdrawalbundle), pending
        NO_BUNDLE,  //!< a sidechain node said its chain has no bundle: the pending ones are downvoted
        MISMATCH,   //!< a sidechain node handed another bundle, not pending (yet): this node abstains
        LEADING,    //!< no sidechain node spoke for the slot: the leading bundle is upvoted, unchecked
        FOLLOW,     //!< no sidechain node spoke: the bundle leading by 50 is upvoted (follow)
        NONE_PENDING, //!< nothing to upvote: no pending bundle, or none leading by 50
        DEFAULT,    //!< the default vote is abstain or downvote, or to upvote only what was handed
    };
    /**
     * Which pending bundle leads: the most votes (score), ties by the lowest hash (as shown, in hex).
     * Bundles `skip` rejects are left out. @return nullptr if none.
     */
    static const Bundle* LeadingBundle(const std::vector<Bundle>& pending, const std::function<bool(const Bundle&)>& skip = {});
    /**
     * The vote on a sidechain with these pending bundles. A vote of SetVote comes first; an
     * UPVOTE of it without a bundle (null hash) upvotes the leading bundle. Where the default
     * applies and is to upvote, the bundle upvoted is the one a sidechain node handed to this
     * node last (see AddBundle), verified by it: a bundle it handed before is one its chain gave
     * up, and is not upvoted even if it has more votes; where it handed one that is not pending,
     * or said there is none, no other bundle is upvoted. Where no node spoke, see UpvoteMode.
     */
    Vote ResolveVote(SidechainId slot, const std::vector<Bundle>& pending, VoteReason* reason = nullptr) const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    void SetDefaultVote(Vote::Type type) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    Vote::Type GetDefaultVote() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    /**
     * Whether, where the default is to upvote and no sidechain node handed this
     * node a bundle, it follows the miners who check them: upvoting the bundle
     * that leads the next by LEADING_BY_50_MARGIN (BIP300 "leading by 50").
     * SetFollow(false) is UpvoteMode::HANDED, and GetFollow whether the mode is FOLLOW.
     */
    void SetFollow(bool follow) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    bool GetFollow() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    //! See UpvoteMode.
    void SetUpvoteMode(UpvoteMode mode) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    UpvoteMode GetUpvoteMode() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /** The height of the last block of the active chain that closed a bundle, if any (see Database::FindClosure). */
    using ClosureLookup = std::function<std::optional<int>(SidechainId, const uint256&)>;

    /**
     * Drop what the chain has made obsolete: proposals and acks of sidechains that activated,
     * bundles closed (paid out or failed) PRUNE_DEPTH blocks ago, kept until then in case a reorg
     * makes them pending again, and bundles handed UNPROPOSED_EXPIRY blocks ago that no block ever
     * proposed (but the one the sidechain node vouches for).
     *
     * Which handed bundles the chain closed is kept, with the height: a bundle the chain closed
     * once is never proposed or upvoted again, even once the sidechain database forgot that it
     * failed (SidechainDB::ForgetFailedBundles) and it looks new; `history` tells about those.
     * The vote and proposal decisions follow what the last call found.
     */
    void Prune(const SidechainDB& scdb, int height, const ClosureLookup& history = {}) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
    static constexpr int PRUNE_DEPTH{6};
    static constexpr int UNPROPOSED_EXPIRY{1008};
    /** Whether the chain closed a handed bundle once, as of the last Prune (a reorg that reopens it excepted). */
    bool WasClosed(SidechainId slot, const uint256& hash) const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /**
     * What to add to a block built on top of the block `scdb` belongs to.
     *
     * @param[in] txs  the transactions selected for the block, without the coinbase
     */
    /**
     * Under the enforcer's rules (the eCash networks) the Chains-only rules are policy here: no bundle
     * is upvoted or paid out that was paid out before, or that was pending when another bundle of its
     * sidechain was paid out (single payout, paid-bundle memory), and no bundle is proposed to a full
     * queue (the pending cap); the vote message (M4) comes first in the coinbase, so that it refers to
     * the bundles as the previous block left them. `height` is the height of the block, if known.
     */
    BlockAdditions CreateBlockAdditions(const SidechainDB& scdb, const Consensus::DrivechainParams& params,
                                        const uint256& prev_block_hash, const std::vector<CTransactionRef>& txs,
                                        int height = -1) const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

private:
    /** Write the state to m_path, if there is one. */
    void Save() const EXCLUSIVE_LOCKS_REQUIRED(m_mutex);

    mutable Mutex m_mutex;
    fs::path m_path GUARDED_BY(m_mutex);
    std::vector<Sidechain> m_proposals GUARDED_BY(m_mutex);
    std::set<std::pair<SidechainId, uint256>> m_acks GUARDED_BY(m_mutex);
    std::map<std::pair<SidechainId, uint256>, CMutableTransaction> m_bundles GUARDED_BY(m_mutex);
    //! The bundle a sidechain node handed last, per slot: the one its chain commits to now.
    std::map<SidechainId, uint256> m_latest_bundle GUARDED_BY(m_mutex);
    /** What Prune learnt of a handed bundle; saved with it. */
    struct BundleInfo {
        //! Height of the first Prune that saw it.
        int32_t handed{-1};
        //! Whether a block ever proposed it.
        bool proposed{false};
        //! Height of the block that closed it, or -1.
        int32_t closed{-1};

        SERIALIZE_METHODS(BundleInfo, obj) { READWRITE(obj.handed, obj.proposed, obj.closed); }
        friend bool operator==(const BundleInfo&, const BundleInfo&) = default;
    };
    std::map<std::pair<SidechainId, uint256>, BundleInfo> m_bundle_info GUARDED_BY(m_mutex);
    bool WasClosedLocked(SidechainId slot, const uint256& hash) const EXCLUSIVE_LOCKS_REQUIRED(m_mutex);
    std::map<SidechainId, Vote> m_votes GUARDED_BY(m_mutex);
    //! To upvote is to upvote what the sidechain node of the operator vouches for, else the leading bundle.
    Vote::Type m_default_vote GUARDED_BY(m_mutex){Vote::Type::UPVOTE};
    UpvoteMode m_upvote_mode GUARDED_BY(m_mutex){UpvoteMode::LEADING};
    AckPolicy m_ack_policy GUARDED_BY(m_mutex){AckPolicy::ALL};
    //! SetChainDefaults: what a fresh state and a file from before version 7 take.
    UpvoteMode m_chain_upvote_mode GUARDED_BY(m_mutex){UpvoteMode::LEADING};
    AckPolicy m_chain_ack_policy GUARDED_BY(m_mutex){AckPolicy::ALL};
    //! Whether the operator chose the upvote mode, the ack policy (SetUpvoteMode, SetAckPolicy: the RPCs and the
    //! options); saved (version 8). What nobody chose follows the network's default (SetChainDefaults) at each load.
    bool m_upvote_mode_chosen GUARDED_BY(m_mutex){false};
    bool m_ack_policy_chosen GUARDED_BY(m_mutex){false};
    std::set<std::pair<SidechainId, uint256>> m_nacks GUARDED_BY(m_mutex);
    std::map<SidechainId, bool> m_slot_acks GUARDED_BY(m_mutex);
};

/**
 * The sidechain database as the coinbase of a block at `height` on top of the block `scdb` belongs to, with these
 * messages (BlockAdditions::coinbase_outputs), leaves it, before the block's transactions: proposals and acks (a
 * sidechain the block activates is active in it), votes, new bundles, and what fails or runs out of time before
 * the transactions. Consensus applies the coinbase so; the template checks its transactions against this. A copy:
 * `scdb` is unchanged. If the coinbase does not pass the rules (which a template's does), `scdb` as it is.
 */
SidechainDB StateAfterCoinbase(const SidechainDB& scdb, const Consensus::DrivechainParams& params,
                               const std::vector<CTxOut>& coinbase_outputs, int height);

/**
 * Aggregated deposits (DrivechainParams::AggregatedDeposits): what consolidates the deposits of a block that did not
 * spend the Ctip of their sidechain, to be placed after all its other transactions (`txs`, without the coinbase, with
 * the withdrawals), as the bip300301_enforcer's block producer appends it: one transaction for all the sidechains, with
 * their Ctips and unconsolidated Ctips as inputs (one transaction per sidechain under the message limits, which allow
 * one treasury output per transaction). Empty if there is nothing to consolidate. `height` is the height of the block.
 */
std::vector<CTransactionRef> BlockConsolidations(const SidechainDB& scdb, const Consensus::DrivechainParams& params,
                                                 const std::vector<CTransactionRef>& txs, int height);
/**
 * The weight a transaction with `n_treasuries` treasury outputs can add to its block through the consolidation:
 * an input for the Ctip, one for its output and a treasury output for each, and room for the counts and the rest of a
 * transaction (lib/types.rs consolidation_tx_weight).
 */
int64_t ConsolidationWeight(size_t n_treasuries);

/**
 * The default vote settings of a network (MinerState::SetChainDefaults). On eCash's main network
 * the blocks of a node with nothing configured upvote only the bundles a sidechain node handed it
 * (verified), and ack only the proposals for a free slot: there, acking every proposal would also
 * ack one that replaces a sidechain in use, and upvoting the leading bundle would upvote one nobody
 * checked. Elsewhere (betanet, the test networks): upvote the leading bundle, ack every proposal.
 */
struct VoteSettings {
    MinerState::UpvoteMode upvote_mode;
    MinerState::AckPolicy ack_policy;
};
VoteSettings DefaultVoteSettings(ChainType chain);

} // namespace drivechain

#endif // BITCOIN_DRIVECHAIN_MINER_H
