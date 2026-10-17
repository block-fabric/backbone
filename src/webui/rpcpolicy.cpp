// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <webui/rpcpolicy.h>

#include <map>
#include <set>

namespace webui {
namespace {
using enum Bridge::Thread;

// Keep in order; LONG for the methods that may run for long (they never hold up the quick ones).
const std::map<std::string, Bridge::Thread>& Methods()
{
    static const std::map<std::string, Bridge::Thread> methods{
        // The chain and the node.
        {"getbestblockhash", WORKER},
        {"getblock", WORKER},
        {"getblockchaininfo", WORKER},
        {"getblockcount", WORKER},
        {"getblockhash", WORKER},
        {"getblockstats", WORKER},
        {"getblockheader", WORKER},
        {"getmempoolentry", WORKER},
        {"getmempoolinfo", WORKER},
        {"getmininginfo", WORKER},
        {"getnetworkhashps", WORKER},
        {"getnetworkinfo", WORKER},
        {"getpeerinfo", WORKER},
        {"getrawmempool", WORKER},
        {"getrawtransaction", WORKER},
        {"gettxout", WORKER},
        {"help", WORKER},
        {"listbanned", WORKER},
        {"uptime", WORKER},
        // Mining with the processor (the Mine page): turning it on is confirmed (rpc.confirmed, confirm.cpp).
        {"getgenerate", WORKER},
        // Drivechains (the Sidechains page): what they read. Deposits, votes, acks and proposals are confirmed on the
        // desktop first (rpc.confirmed, confirm.cpp: audit 7 #1).
        {"getdrivechaininfo", WORKER},
        {"getsidechainevents", WORKER},
        {"listactivesidechains", WORKER},
        {"listsidechaindeposits", WORKER},
        {"listsidechainproposals", WORKER},
        {"listwithdrawalbundles", WORKER},
        // Addresses, scripts and PSBTs (multisig lounge, proof of funds, crypto tools).
        {"createpsbt", WORKER},
        {"decodepsbt", WORKER},
        {"deriveaddresses", WORKER},
        {"finalizepsbt", WORKER},
        {"getdescriptorinfo", WORKER},
        {"scantxoutset", LONG},
        {"utxoupdatepsbt", WORKER},
        {"validateaddress", WORKER},
        {"verifymessage", WORKER},
        // Wallets: what the pages read. Unlocking is the host's (wallet.unlock: the passphrase typed in its dialog).
        {"getaddressinfo", WORKER},
        {"getbalances", WORKER},
        {"getnewaddress", WORKER},
        {"getwalletinfo", WORKER},
        {"listunspent", WORKER},
        {"listwallets", WORKER},
        {"walletlock", WORKER},
        // Nothing that spends or signs: payments, signatures of messages and of PSBTs, and broadcasts go through the host
        // (walletapi.cpp, confirm.cpp), which shows what they do in its own dialog first (audit 7 #1).
    };
    return methods;
}
} // namespace

std::optional<Bridge::Thread> PageRpcPolicy(const std::string& method)
{
    const auto it{Methods().find(method)};
    if (it == Methods().end()) return std::nullopt;
    return it->second;
}

std::vector<std::string> PageRpcMethods()
{
    std::vector<std::string> out;
    for (const auto& [name, thread] : Methods()) out.push_back(name);
    return out;
}

bool KeyedWalletRpc(const std::string& method)
{
    // None of the page's methods of Chains needs the keys: what does goes through the host (confirm.cpp).
    (void)method;
    return false;
}

} // namespace webui
