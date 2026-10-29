// Copyright (c) 2026 The Backbone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_GRPC_INIT_H
#define BITCOIN_GRPC_INIT_H

#include <cstdint>

class ArgsManager;
namespace node {
struct NodeContext;
} // namespace node

/** The port of the bip300301_enforcer's gRPC server (its --serve-grpc-addr, 127.0.0.1:50051). */
static constexpr uint16_t DEFAULT_GRPC_PORT{50051};
static constexpr int DEFAULT_GRPC_THREADS{4};
/** Connections the gRPC server holds at most, in all. */
static constexpr int GRPC_MAX_CONNECTIONS{256};

void AddGrpcOptions(ArgsManager& args);
/**
 * Serve the bip300301_enforcer's gRPC interface, if -grpc (on by default on eCash's main network,
 * betanet and test network). @return false (with an init error given) only if it was asked for
 * explicitly and cannot be served; a default that cannot bind is a warning.
 */
bool StartGrpcServer(node::NodeContext& node, const ArgsManager& args);
/**
 * The file descriptors the gRPC server may take, its listening sockets and its connections, if it is to be served
 * (-grpc, as StartGrpcServer reads it); 0 otherwise. For the node's file descriptor budget.
 */
int GrpcFileDescriptors(const ArgsManager& args);
void StopGrpcServer();

#endif // BITCOIN_GRPC_INIT_H
