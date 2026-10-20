// Copyright (c) 2026 The Backbone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <grpc/init.h>

#include <chainparams.h>
#include <common/args.h>
#include <common/messages.h>
#include <grpc/descriptors.h>
#include <grpc/protobuf.h>
#include <grpc/server.h>
#include <grpc/services.h>
#include <logging.h>
#include <netbase.h>
#include <node/context.h>
#include <node/interface_ui.h>
#include <tinyformat.h>
#include <util/chaintype.h>
#include <util/strencodings.h>
#include <util/translation.h>
#include <validation.h>

#include <memory>

namespace {
std::unique_ptr<pb::Pool> g_pool;
std::unique_ptr<grpcsrv::Server> g_server;
std::unique_ptr<grpcsrv::Services> g_services;
} // namespace

void AddGrpcOptions(ArgsManager& args)
{
    args.AddArg("-grpc", "Serve the gRPC interface of the bip300301_enforcer (cusf.mainchain.v1, cusf.crypto.v1, grpc.health.v1, grpc.reflection), "
                         "and on the same port gRPC-Web and the Connect protocol, so that sidechain software connects to this node as to bitcoind and the enforcer "
                         "(default: 1 on ecash, ecashbeta and ecashtest; 0 elsewhere; the eCash networks only, whose drivechain rules are the enforcer's). "
                         "No TLS and no authentication, as the enforcer's: keep it on localhost (see -grpcallowip)",
                 ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    args.AddArg("-grpcbind=<addr>[:port]", strprintf("Bind the gRPC server to this address (default: 127.0.0.1 and ::1). Port is optional and overrides -grpcport. Use [host]:port notation for IPv6. This option can be specified multiple times"),
                 ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::RPC);
    args.AddArg("-grpcport=<port>", strprintf("Listen for gRPC connections on <port> (default: %u, the enforcer's)", DEFAULT_GRPC_PORT),
                 ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::RPC);
    args.AddArg("-grpcallowip=<ip>", "Allow gRPC connections from this source, beside localhost: a single IP (e.g. 1.2.3.4), a network/netmask, a network/CIDR. Can be specified multiple times",
                 ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    args.AddArg("-grpcwallet=<name>", "The wallet that the gRPC WalletService (deposits, BMM requests, addresses) uses (default: the only wallet loaded). -nogrpcwallet does not serve WalletService",
                 ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    args.AddArg("-grpcallowstop", "Let gRPC ValidatorService.Stop shut the node down, as the enforcer does (default: 1)",
                 ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    args.AddArg("-grpcthreads=<n>", strprintf("Threads that serve gRPC calls (default: %d)", DEFAULT_GRPC_THREADS),
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::RPC);
}

bool StartGrpcServer(node::NodeContext& node, const ArgsManager& args)
{
    const ChainType chain{Params().GetChainType()};
    const bool on_by_default{IsEcashChain(chain)};
    const bool explicit_on{args.IsArgSet("-grpc") && args.GetBoolArg("-grpc", false)};
    if (!args.GetBoolArg("-grpc", on_by_default)) return true;
    if (!Params().GetConsensus().drivechain.EnforcerRules()) {
        // The Chains networks have drivechain rules of their own, which the enforcer's interface does not describe.
        return InitError(_("-grpc serves the interface of the bip300301_enforcer: it is for the eCash networks (or -regtest -testecash), whose drivechain rules are the enforcer's"));
    }
    const auto fail{[&](const std::string& message) {
        if (explicit_on) return InitError(Untranslated(message));
        // On by default: a port taken (an enforcer next to this node, say) leaves the node running without it.
        LogWarning("%s; the gRPC interface is not served", message);
        return true;
    }};

    g_pool = std::make_unique<pb::Pool>();
    std::string error;
    if (!g_pool->Load(grpcsrv::DescriptorSet(), error)) return InitError(Untranslated("gRPC descriptors: " + error));

    std::vector<CSubNet> allow;
    for (const std::string& ip : args.GetArgs("-grpcallowip")) {
        const CSubNet subnet{LookupSubNet(ip)};
        if (!subnet.IsValid()) return InitError(strprintf(_("Invalid -grpcallowip subnet specification: %s"), ip));
        allow.push_back(subnet);
    }
    const uint16_t default_port{static_cast<uint16_t>(args.GetIntArg("-grpcport", DEFAULT_GRPC_PORT))};
    std::vector<std::pair<std::string, uint16_t>> endpoints;
    if (!args.IsArgSet("-grpcbind")) {
        endpoints.emplace_back("::1", default_port);
        endpoints.emplace_back("127.0.0.1", default_port);
    } else {
        for (const std::string& bind : args.GetArgs("-grpcbind")) {
            uint16_t port{default_port};
            std::string host;
            if (!SplitHostPort(bind, port, host)) return InitError(common::InvalidPortErrMsg("-grpcbind", bind));
            endpoints.emplace_back(host, port);
        }
    }
    std::vector<CService> binds;
    for (const auto& [host, port] : endpoints) {
        const std::optional<CService> addr{Lookup(host, port, false)};
        if (!addr) return InitError(strprintf(_("Cannot resolve -grpcbind address: '%s'"), host));
        if (addr->IsBindAny()) LogWarning("The gRPC server has no authentication: it is not safe to expose to untrusted networks");
        binds.push_back(*addr);
    }

    grpcsrv::Options options;
    options.threads = std::max<int>(1, args.GetIntArg("-grpcthreads", DEFAULT_GRPC_THREADS));
    g_server = std::make_unique<grpcsrv::Server>(*g_pool, options);
    grpcsrv::ServiceOptions service_options;
    service_options.allow_stop = args.GetBoolArg("-grpcallowstop", true);
    service_options.wallet_service = !args.IsArgNegated("-grpcwallet");
    service_options.wallet = service_options.wallet_service ? args.GetArg("-grpcwallet", "") : "";
    g_services = grpcsrv::RegisterServices(*g_server, *g_pool, node, service_options);
    if (!g_server->Start(binds, allow, error)) {
        g_services.reset();
        g_server.reset();
        return fail("gRPC server: " + error);
    }
    for (const CService& bound : g_server->Bound()) LogInfo("gRPC: serving the enforcer's interface on %s", bound.ToStringAddrPort());
    return true;
}

void StopGrpcServer()
{
    if (g_server) {
        LogInfo("gRPC: stopping");
        g_server->Stop();
    }
    g_services.reset();
    g_server.reset();
    g_pool.reset();
}
