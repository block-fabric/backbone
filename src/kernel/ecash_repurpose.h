// Copyright (c) 2026 The Backbone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KERNEL_ECASH_REPURPOSE_H
#define BITCOIN_KERNEL_ECASH_REPURPOSE_H

#include <uint256.h>

#include <set>

namespace kernel {
/** eCash's repurpose transactions of its main network (232 txids). */
extern const std::set<uint256> ECASH_MAIN_REPURPOSE_TXIDS;
/** eCash's repurpose transactions of betanet (232 txids). */
extern const std::set<uint256> ECASH_BETANET_REPURPOSE_TXIDS;
} // namespace kernel

#endif // BITCOIN_KERNEL_ECASH_REPURPOSE_H
