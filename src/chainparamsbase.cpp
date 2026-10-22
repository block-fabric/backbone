// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparamsbase.h>

#include <common/args.h>
#include <tinyformat.h>
#include <util/chaintype.h>

#include <cassert>

void SetupChainParamsBaseOptions(ArgsManager& argsman)
{
    argsman.AddArg("-chain=<chain>", "Use the chain <chain> (default: ecash). Allowed values: " LIST_CHAIN_NAMES ". "
                   "ecash, ecashbeta and ecashtest are eCash's main network, its betanet and its test network (testnet4), with their data in the Backbone data directory; "
                   "main, test, signet and regtest are the Chains (CHN) networks, in the Chains data directory.", ArgsManager::ALLOW_ANY, OptionsCategory::CHAINPARAMS);
    argsman.AddArg("-regtest", "Enter regression test mode, which uses a special chain in which blocks can be solved instantly. "
                 "This is intended for regression testing tools and app development. Equivalent to -chain=regtest.", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    argsman.AddArg("-testactivationheight=name@height.", "Set the activation height of 'name' (segwit, bip34, dersig, cltv, csv). (test-only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-testdrivechainparam=name@value", "Set the drivechain parameter 'name' (a field of DrivechainParams, such as withdrawal_period) on regtest. (test-only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-testecash", "Regtest under the rules of the eCash networks: replay protection, OP_DRIVECHAIN as OP_NOP8, the drivechain rules of the bip300301_enforcer (with its regtest thresholds), the bip300301 rule of getblocktemplate and the replay protection of the wallet. (test-only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-ecashregtest=<build>", "Regtest as the regtest of eCash's node (ecash-com/bitcoin), so that both peer and follow one chain: implies -testecash, with eCash's regtest message start (eca5b134), P2P port (18644) and bech32 prefix (bcrt), and the replay nLockTime of <build>: betanet (499999999, the betanet branch) or ecash (499999998, the main network's release). (test-only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-testecashfork=height@bits", "With -testecash: the eCash fork block at <height> (a multiple of 144), which must carry <bits> (hexadecimal); implies -testpowretarget. (test-only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-testreplaylocktime=<n>", "With -testecash: the nLockTime of eCash's replay protection (default: 499999998). (test-only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-testecashmagic", "With -testecash: the message start and default port of eCash's client on regtest (ecash-com/bitcoin: eca5b134, 18644), to peer with a regtest node of it. (test-only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-testrepurposetx=<txid>", "With -testecash: a repurpose transaction, whose input scripts are not checked (can be given more than once). (test-only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-testpowretarget", "Retarget on regtest as on Bitcoin, every 144 blocks. (test-only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-testnet", "Use the test chain. Equivalent to -chain=test.", ArgsManager::ALLOW_ANY, OptionsCategory::CHAINPARAMS);
    argsman.AddArg("-vbparams=deployment:start:end[:min_activation_height]", "Use given start/end times and min_activation_height for specified version bits deployment (test-only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    argsman.AddArg("-signet", "Use the signet chain. Equivalent to -chain=signet. Note that the network is defined by the -signetchallenge parameter", ArgsManager::ALLOW_ANY, OptionsCategory::CHAINPARAMS);
    argsman.AddArg("-signetchallenge", "Blocks must satisfy the given script to be considered valid (only for signet networks; defaults to the global default signet test network challenge)", ArgsManager::ALLOW_ANY | ArgsManager::DISALLOW_NEGATION, OptionsCategory::CHAINPARAMS);
    argsman.AddArg("-signetseednode", "Specify a seed node for the signet network, in the hostname[:port] format, e.g. sig.net:1234 (may be used multiple times to specify multiple seed nodes; defaults to the global default signet test network seed node(s))", ArgsManager::ALLOW_ANY | ArgsManager::DISALLOW_NEGATION, OptionsCategory::CHAINPARAMS);
}

static std::unique_ptr<CBaseChainParams> globalChainBaseParams;

const CBaseChainParams& BaseParams()
{
    assert(globalChainBaseParams);
    return *globalChainBaseParams;
}

/**
 * Port numbers for incoming Tor connections (8334, 18334, 38334, 48334, 18445) have
 * been chosen arbitrarily to keep ranges of used ports tight.
 */
std::unique_ptr<CBaseChainParams> CreateBaseChainParams(const ChainType chain)
{
    switch (chain) {
    case ChainType::MAIN:
        return std::make_unique<CBaseChainParams>("", 9554);
    case ChainType::TESTNET:
        return std::make_unique<CBaseChainParams>("testnet", 19554);
    case ChainType::SIGNET:
        return std::make_unique<CBaseChainParams>("signet", 39554);
    case ChainType::REGTEST:
        return std::make_unique<CBaseChainParams>("regtest", 29554);
    // eCash: the ports of ecash-com/bitcoin, so that a pool, a miner or a wallet made for an eCash node
    // needs no change. Main is the root of the Backbone data directory, as on eCash; betanet (a separate
    // eCash client of its own, whose main it is) has a folder, and the test network the folder of
    // testnet4, as on eCash.
    case ChainType::ECASH:
        return std::make_unique<CBaseChainParams>("", 8532);
    case ChainType::ECASH_BETANET:
        return std::make_unique<CBaseChainParams>("betanet", 8532);
    case ChainType::ECASH_TESTNET:
        return std::make_unique<CBaseChainParams>("testnet4", 48532);
    }
    assert(false);
}

void SelectBaseParams(const ChainType chain)
{
    globalChainBaseParams = CreateBaseChainParams(chain);
    gArgs.SelectConfigNetwork(ChainTypeToString(chain));
}
