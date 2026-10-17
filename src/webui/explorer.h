// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WEBUI_EXPLORER_H
#define BITCOIN_WEBUI_EXPLORER_H

#include <univalue.h>

#include <cstddef>

class CBlock;
class ChainstateManager;
class uint256;
namespace Consensus {
struct Params;
} // namespace Consensus

namespace webui::explorer {

//! The most blocks of the explorer's list one call reads whole for their coinbase (explorer.blocks with details).
inline constexpr int64_t MAX_DETAILED_ROWS{100};
//! The most coinbase outputs, escrow changes and pending bundles explorer.block lists (each list says how many there were).
inline constexpr size_t MAX_LISTED{500};

/**
 * What the row of a block in the explorer's list adds from the block itself (explorer.blocks, `details`):
 *   size, weight, subsidy, reward (what the coinbase pays), fees (reward - subsidy), all in sats;
 *   coinbase (its txid), script (its scriptSig, hex: the miner's tag), payout (the address the coinbase pays most,
 *   if any), payouts (outputs that pay coins), bmm (slots of its BMM accepts, M7), votes (sidechains its vote message
 *   upvotes or downvotes), voteForm ("" without one, "one", "two", "repeat", "leading"), acks (M2), proposals (M1),
 *   bundles (M3), escrow (transactions that change an escrow: deposits and withdrawals).
 */
UniValue BlockSummary(const CBlock& block, int height, const Consensus::Params& consensus);

/**
 * Everything the coinbase of a block says and what the block did to the sidechains, for the block shown by the
 * explorer (explorer.block): the subsidy, the fees, each coinbase output with what it is (miner payout, witness
 * commitment, BIP300 M1-M4, BIP301 M7, other data), the BMM accepts with their requests and bids, sidechain
 * proposals, acks and activations, the votes on bundles per sidechain, the bundles it proposed and closed,
 * the escrow changes (deposits, withdrawals with their payouts) and the bundles pending after it.
 * One block: read from disk without cs_main, which is held only to look the block and the sidechain
 * database up. Throws HostError if the block is unknown or not on disk.
 */
UniValue BlockDetail(ChainstateManager& chainman, const uint256& hash);

} // namespace webui::explorer

#endif // BITCOIN_WEBUI_EXPLORER_H
