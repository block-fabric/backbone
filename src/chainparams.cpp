// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>

#include <chainparamsbase.h>
#include <common/args.h>
#include <consensus/params.h>
#include <deploymentinfo.h>
#include <drivechain/sidechain.h>
#include <policy/feerate.h>
#include <script/script.h>
#include <uint256.h>
#include <tinyformat.h>
#include <util/chaintype.h>
#include <util/log.h>
#include <util/strencodings.h>
#include <util/string.h>

#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

using util::SplitString;

static void HandleDeploymentArgs(const ArgsManager& args, CChainParams::DeploymentOptions& options)
{
    for (const std::string& arg : args.GetArgs("-testactivationheight")) {
        const auto found{arg.find('@')};
        if (found == std::string::npos) {
            throw std::runtime_error(strprintf("Invalid format (%s) for -testactivationheight=name@height.", arg));
        }

        const auto value{arg.substr(found + 1)};
        const auto height{ToIntegral<int32_t>(value)};
        if (!height || *height < 0 || *height >= std::numeric_limits<int>::max()) {
            throw std::runtime_error(strprintf("Invalid height value (%s) for -testactivationheight=name@height.", arg));
        }

        const auto deployment_name{arg.substr(0, found)};
        if (const auto buried_deployment = GetBuriedDeployment(deployment_name)) {
            options.activation_heights[*buried_deployment] = *height;
        } else {
            throw std::runtime_error(strprintf("Invalid name (%s) for -testactivationheight=name@height.", arg));
        }
    }

    for (const std::string& strDeployment : args.GetArgs("-vbparams")) {
        std::vector<std::string> vDeploymentParams = SplitString(strDeployment, ':');
        if (vDeploymentParams.size() < 3 || 4 < vDeploymentParams.size()) {
            throw std::runtime_error("Version bits parameters malformed, expecting deployment:start:end[:min_activation_height]");
        }
        CChainParams::VersionBitsParameters vbparams{};
        const auto start_time{ToIntegral<int64_t>(vDeploymentParams[1])};
        if (!start_time) {
            throw std::runtime_error(strprintf("Invalid nStartTime (%s)", vDeploymentParams[1]));
        }
        vbparams.start_time = *start_time;
        const auto timeout{ToIntegral<int64_t>(vDeploymentParams[2])};
        if (!timeout) {
            throw std::runtime_error(strprintf("Invalid nTimeout (%s)", vDeploymentParams[2]));
        }
        vbparams.timeout = *timeout;
        if (vDeploymentParams.size() >= 4) {
            const auto min_activation_height{ToIntegral<int64_t>(vDeploymentParams[3])};
            if (!min_activation_height) {
                throw std::runtime_error(strprintf("Invalid min_activation_height (%s)", vDeploymentParams[3]));
            }
            vbparams.min_activation_height = *min_activation_height;
        } else {
            vbparams.min_activation_height = 0;
        }
        bool found = false;
        for (int j=0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j) {
            if (vDeploymentParams[0] == VersionBitsDeploymentInfo[j].name) {
                options.version_bits_parameters[Consensus::DeploymentPos(j)] = vbparams;
                found = true;
                LogInfo("Setting version bits activation parameters for %s to start=%ld, timeout=%ld, min_activation_height=%d",
                        vDeploymentParams[0], vbparams.start_time, vbparams.timeout, vbparams.min_activation_height);
                break;
            }
        }
        if (!found) {
            throw std::runtime_error(strprintf("Invalid deployment (%s)", vDeploymentParams[0]));
        }
    }
}

void ReadMainNetArgs(const ArgsManager& args, CChainParams::MainNetOptions& options)
{
    HandleDeploymentArgs(args, options.dep_opts);
}

void ReadTestNetArgs(const ArgsManager& args, CChainParams::TestNetOptions& options)
{
    HandleDeploymentArgs(args, options.dep_opts);
}

void ReadSigNetArgs(const ArgsManager& args, CChainParams::SigNetOptions& options)
{
    if (!args.GetArgs("-signetseednode").empty()) {
        options.seeds.emplace(args.GetArgs("-signetseednode"));
    }
    if (!args.GetArgs("-signetchallenge").empty()) {
        const auto signet_challenge = args.GetArgs("-signetchallenge");
        if (signet_challenge.size() != 1) {
            throw std::runtime_error("-signetchallenge cannot be multiple values.");
        }
        const auto val{TryParseHex<uint8_t>(signet_challenge[0])};
        if (!val) {
            throw std::runtime_error(strprintf("-signetchallenge must be hex, not '%s'.", signet_challenge[0]));
        }
        options.challenge.emplace(*val);
    }
    HandleDeploymentArgs(args, options.dep_opts);
}

void ReadRegTestArgs(const ArgsManager& args, CChainParams::RegTestOptions& options)
{
    if (auto value = args.GetBoolArg("-fastprune")) options.fastprune = *value;
    if (HasTestOption(args, "bip94")) options.enforce_bip94 = true;
    options.pow_retargeting = args.GetBoolArg("-testpowretarget", false);
    for (const std::string& arg : args.GetArgs("-testdrivechainparam")) {
        const auto found{arg.find('@')};
        const auto value{found == std::string::npos ? std::nullopt : ToIntegral<int32_t>(arg.substr(found + 1))};
        // An activation height (a name that ends in _height) can be -1: never (DrivechainParams::NEVER).
        const bool never{value && *value == -1 && arg.substr(0, found).ends_with("_height")};
        if (!value || (*value < 0 && !never)) throw std::runtime_error(strprintf("Invalid format (%s) for -testdrivechainparam=name@value.", arg));
        options.drivechain_params.emplace_back(arg.substr(0, found), *value);
    }
    // Regtest under eCash's rules, for the tests of the eCash networks.
    // -ecashregtest: the regtest of eCash's own node (same message start, port, address prefix and replay rule).
    std::optional<uint32_t> ecash_regtest_locktime;
    if (const auto build{args.GetArg("-ecashregtest")}; build && !args.IsArgNegated("-ecashregtest")) {
        if (*build == "betanet" || *build == "" || *build == "1") {
            ecash_regtest_locktime = 499'999'999; // LOCKTIME_THRESHOLD - 1: the betanet branch of ecash-com/bitcoin
        } else if (*build == "ecash" || *build == "main") {
            ecash_regtest_locktime = 499'999'998; // LOCKTIME_THRESHOLD - 2: tag ecash-v1.0.0-core31.1
        } else {
            throw std::runtime_error(strprintf("Invalid value (%s) for -ecashregtest (betanet or ecash).", *build));
        }
    }
    if (args.GetBoolArg("-testecash", false) || ecash_regtest_locktime) {
        auto& ecash{options.ecash.emplace()};
        if (ecash_regtest_locktime) {
            ecash.ecash_regtest = true;
            ecash.replay_locktime = *ecash_regtest_locktime;
        }
        if (const auto fork{args.GetArg("-testecashfork")}) {
            const auto found{fork->find('@')};
            const auto height{found == std::string::npos ? std::nullopt : ToIntegral<int32_t>(fork->substr(0, found))};
            const auto bits{found == std::string::npos ? std::nullopt : ToIntegral<uint32_t>(fork->substr(found + 1), 16)};
            if (!height || *height <= 0 || !bits) throw std::runtime_error(strprintf("Invalid format (%s) for -testecashfork=height@bits (bits in hexadecimal).", *fork));
            ecash.fork_height = *height;
            ecash.fork_bits = *bits;
        }
        if (const auto locktime{args.GetArg("-testreplaylocktime")}) {
            const auto value{ToIntegral<uint32_t>(*locktime)};
            if (!value) throw std::runtime_error(strprintf("Invalid value (%s) for -testreplaylocktime.", *locktime));
            ecash.replay_locktime = *value;
        }
        ecash.ecash_magic = ecash.ecash_regtest || args.GetBoolArg("-testecashmagic", false);
        for (const std::string& txid : args.GetArgs("-testrepurposetx")) {
            const auto hash{uint256::FromHex(txid)};
            if (!hash) throw std::runtime_error(strprintf("Invalid txid (%s) for -testrepurposetx.", txid));
            ecash.repurpose_txids.insert(*hash);
        }
    } else if (args.IsArgSet("-testecashfork") || args.IsArgSet("-testreplaylocktime") || args.IsArgSet("-testrepurposetx") || args.IsArgSet("-testecashmagic")) {
        throw std::runtime_error("-testecashfork, -testreplaylocktime, -testrepurposetx and -testecashmagic need -testecash (or -ecashregtest).");
    }

    HandleDeploymentArgs(args, options.dep_opts);
}

static std::unique_ptr<const CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<const CChainParams> CreateChainParams(const ArgsManager& args, const ChainType chain)
{
    switch (chain) {
    case ChainType::MAIN: {
        auto opts = CChainParams::MainNetOptions{};
        ReadMainNetArgs(args, opts);
        return CChainParams::Main(opts);
    }
    case ChainType::TESTNET: {
        auto opts = CChainParams::TestNetOptions{};
        ReadTestNetArgs(args, opts);
        return CChainParams::TestNet(opts);
    }
    case ChainType::SIGNET: {
        auto opts = CChainParams::SigNetOptions{};
        ReadSigNetArgs(args, opts);
        return CChainParams::SigNet(opts);
    }
    case ChainType::ECASH: {
        auto opts = CChainParams::MainNetOptions{};
        ReadMainNetArgs(args, opts);
        return CChainParams::EcashMain(opts);
    }
    case ChainType::ECASH_BETANET: {
        auto opts = CChainParams::MainNetOptions{};
        ReadMainNetArgs(args, opts);
        return CChainParams::EcashBetanet(opts);
    }
    case ChainType::ECASH_TESTNET: {
        auto opts = CChainParams::TestNetOptions{};
        ReadTestNetArgs(args, opts);
        return CChainParams::EcashTestNet(opts);
    }
    case ChainType::REGTEST: {
        auto opts = CChainParams::RegTestOptions{};
        ReadRegTestArgs(args, opts);
        return CChainParams::RegTest(opts);
    }
    }
    assert(false);
}

void SelectParams(const ChainType chain)
{
    SelectBaseParams(chain);
    globalChainParams = CreateChainParams(gArgs, chain);
    // The treasury scripts of the network (OP_NOP5 on Chains', OP_NOP8 on eCash's).
    drivechain::SetEscrowOpcode(globalChainParams->GetConsensus().drivechain_nop8 ? OP_NOP8 : OP_NOP5);
    // The unit amounts are shown in: eCash's (ECX), or Chains' (CHN).
    SetCurrencyUnitEcash(globalChainParams->GetConsensus().ecash);
}
