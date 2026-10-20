// Copyright (c) 2026 The Backbone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_GRPC_SERVICES_H
#define BITCOIN_GRPC_SERVICES_H

#include <grpc/protobuf.h>
#include <grpc/server.h>

#include <memory>
#include <string>

class CBlockIndex;
class ChainstateManager;
namespace node {
struct NodeContext;
} // namespace node

/**
 * The services of the bip300301_enforcer (cusf.mainchain.v1, cusf.crypto.v1), and the standard
 * grpc.health.v1 and grpc.reflection.v1/v1alpha, served from this node's own state: the sidechain
 * database (scdb) and its drivechain database, the chain, the mempool, the block template's
 * drivechain policy (drivechain::MinerState) and a wallet (through the wallet's RPC methods).
 * What each method answers, against the enforcer's code, is in doc/ecash-grpc.md.
 */
namespace grpcsrv {

struct ServiceOptions {
    //! The wallet of WalletService (-grpcwallet); empty: the only wallet loaded.
    std::string wallet;
    //! Whether ValidatorService.Stop shuts the node down (-grpcallowstop).
    bool allow_stop{true};
    //! Serve WalletService (-grpcwallet=0 turns it off).
    bool wallet_service{true};
};

class Services
{
public:
    virtual ~Services() = default;
};

/** Register every method on `server`, and start following the chain for the event streams. */
std::unique_ptr<Services> RegisterServices(Server& server, const pb::Pool& pool, node::NodeContext& node, const ServiceOptions& options);

/**
 * The parts of the replies that tests check on their own: what a block did that concerns one
 * sidechain (BlockInfo), and the header (BlockHeaderInfo); nullopt if the block was not
 * connected. Needs cs_main.
 */
std::optional<pb::Message> MakeBlockInfo(const pb::Pool& pool, ChainstateManager& chainman, const CBlockIndex& block, uint32_t slot);
pb::Message MakeHeaderInfo(const pb::Pool& pool, const CBlockIndex& block);

} // namespace grpcsrv

#endif // BITCOIN_GRPC_SERVICES_H
