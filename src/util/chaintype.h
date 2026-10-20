// Copyright (c) 2023-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UTIL_CHAINTYPE_H
#define BITCOIN_UTIL_CHAINTYPE_H

#include <optional>
#include <string>
#include <string_view>

enum class ChainType {
    //! The Chains (CHN) networks: legacy, kept for the sidechains, the test hub and the releases that use them.
    MAIN,
    TESTNET,
    SIGNET,
    REGTEST,
    //! eCash (ECX), the drivechain fork of Bitcoin (ecash-com/bitcoin): its main network, its betanet
    //! rehearsal and its test network (Bitcoin's testnet4 under eCash's rules and message start).
    ECASH,
    ECASH_BETANET,
    ECASH_TESTNET,
};

std::string ChainTypeToString(ChainType chain);

std::optional<ChainType> ChainTypeFromString(std::string_view chain);

/**
 * The name the RPC interface reports as "chain" (getblockchaininfo, getmininginfo): what eCash's own node
 * reports on the eCash networks ("main" on its main network and on betanet, "testnet4" on its test
 * network), for the pools, explorers and the bip300301_enforcer that read it; ChainTypeToString otherwise.
 * The "network" field has our name of the network (ChainTypeToString).
 */
std::string ChainTypeToCompatName(ChainType chain);

/** Whether `chain` is one of the eCash networks (ECX), rather than a Chains (CHN) network. */
constexpr bool IsEcashChain(ChainType chain)
{
    return chain == ChainType::ECASH || chain == ChainType::ECASH_BETANET || chain == ChainType::ECASH_TESTNET;
}

#endif // BITCOIN_UTIL_CHAINTYPE_H
