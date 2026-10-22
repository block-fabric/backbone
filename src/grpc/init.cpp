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
    args.AddArg("-grpcwallet=<name>", "Serve the gRPC WalletService (deposits, BMM requests, addresses, sends) with this wallet (empty: the only wallet loaded). "
                                      "Default: served with the only wallet on ecashbeta, ecashtest and regtest; not served on ecash (main) unless given. -nogrpcwallet: not served",
                 ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    args.AddArg("-grpcallowstop", "Let gRPC ValidatorService.Stop shut the node down, as the enforcer does (default: 0)",
                 ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    args.AddArg("-grpcauth=<token>", "Require this token (gRPC metadata \"authorization: Bearer <token>\") for WalletService, BlockProducerService, MiningService and ValidatorService.Stop. "
                                     "The ValidatorService otherwise needs none, as the enforcer's. Required to bind to an address other than localhost",
                 ArgsManager::ALLOW_ANY | ArgsManager::SENSITIVE, OptionsCategory::RPC);
    args.AddArg("-grpcallowedorigin=<origin>", "Accept gRPC requests from web pages of this origin (e.g. http://localhost:3000); requests with another Origin header are refused. Can be specified multiple times",
                 ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    args.AddArg("-grpcallowedhost=<name>", "Accept gRPC requests whose Host is this name, beside IP addresses and localhost (others are refused, against DNS rebinding). Can be specified multiple times",
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
    const std::string token{args.GetArg("-grpcauth", "")};
    std::vector<CService> binds;
    for (const auto& [host, port] : endpoints) {
        const std::optional<CService> addr{Lookup(host, port, false)};
        if (!addr) return InitError(strprintf(_("Cannot resolve -grpcbind address: '%s'"), host));
        // Beyond this machine only with both an allow list and the credential: the ValidatorService has no
        // authentication (as the enforcer's), the others then need the token.
        if ((!addr->IsLocal() || addr->IsBindAny()) && (allow.empty() || token.empty())) {
            return InitError(Untranslated(strprintf("-grpcbind=%s is not a loopback address: that needs -grpcallowip and -grpcauth", addr->ToStringAddrPort())));
        }
        binds.push_back(*addr);
    }

    grpcsrv::Options options;
    options.threads = std::max<int>(1, args.GetIntArg("-grpcthreads", DEFAULT_GRPC_THREADS));
    // Slow clients (slowloris): a request must be all in within 15 s of its first byte however slowly it trickles;
    // a connection with no running call idle 30 s is closed (GOAWAY first over HTTP/2; a server stream such as
    // SubscribeEvents is a running call); 16 connections per address beyond this machine, 256 in all.
    options.max_connections = 256;
    options.max_connections_per_ip = 16;
    options.per_ip_exempt = {LookupSubNet("127.0.0.0/8"), LookupSubNet("::1/128")};
    options.request_timeout_seconds = 15;
    options.idle_timeout_seconds = 30;
    options.allowed_origins = args.GetArgs("-grpcallowedorigin");
    for (const std::string& h : args.GetArgs("-grpcallowedhost")) options.allowed_hosts.push_back(ToLower(h));
    g_server = std::make_unique<grpcsrv::Server>(*g_pool, options);
    if (!token.empty()) {
        // The services that spend, mine, decide votes or stop the node need the token.
        g_server->SetGuard([token](const std::string& path, const std::map<std::string, std::string>& headers) -> std::optional<grpcsrv::Status> {
            const bool guarded{path.starts_with("/cusf.mainchain.v1.WalletService/") || path.starts_with("/cusf.mainchain.v1.BlockProducerService/") ||
                               path.starts_with("/cusf.mainchain.v1.MiningService/") || path == "/cusf.mainchain.v1.ValidatorService/Stop"};
            if (!guarded) return std::nullopt;
            const auto it{headers.find("authorization")};
            const std::string expected{"Bearer " + token};
            if (it != headers.end() && it->second.size() == expected.size() && TimingResistantEqual(it->second, expected)) return std::nullopt;
            return grpcsrv::Status{grpcsrv::Code::UNAUTHENTICATED, "this method needs the -grpcauth token (metadata authorization: Bearer <token>)"};
        });
    }
    grpcsrv::ServiceOptions service_options;
    service_options.allow_stop = args.GetBoolArg("-grpcallowstop", false);
    // WalletService spends: on eCash's main network only when asked for (-grpcwallet).
    service_options.wallet_service = chain == ChainType::ECASH ? (args.IsArgSet("-grpcwallet") && !args.IsArgNegated("-grpcwallet")) : !args.IsArgNegated("-grpcwallet");
    service_options.wallet = service_options.wallet_service ? args.GetArg("-grpcwallet", "") : "";
    LogInfo("gRPC: WalletService %s%s; Stop %s; token %s", service_options.wallet_service ? "served" : "not served",
            service_options.wallet_service ? (service_options.wallet.empty() ? " (the only wallet)" : " (wallet " + service_options.wallet + ")") : (chain == ChainType::ECASH ? " (main network: -grpcwallet to serve it)" : ""),
            service_options.allow_stop ? "allowed" : "refused (-grpcallowstop)", token.empty() ? "none" : "required for the wallet, block producer, mining and Stop");
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
