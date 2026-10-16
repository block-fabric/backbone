// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// A pruned node keeps a good history: on the main network, testnet and signet, -prune below
// MIN_PRUNE_TARGET (10000 MiB, CChainParams::MinPruneTarget) refuses to start and manual pruning leaves
// that much. Regtest keeps Bitcoin's 550 MiB and manual pruning, for the tests inherited from Bitcoin.

#include <chain.h>
#include <chainparams.h>
#include <common/args.h>
#include <kernel/chainparams.h>
#include <node/blockmanager_args.h>
#include <node/blockstorage.h>
#include <node/kernel_notifications.h>
#include <test/util/setup_common.h>
#include <util/byte_units.h>
#include <util/chaintype.h>
#include <util/check.h>
#include <util/result.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <set>
#include <string>
#include <vector>

using node::BlockManager;
using node::KernelNotifications;

namespace {
/** What -prune gives on a network: the prune target, or the error. */
util::Result<uint64_t> ApplyPrune(BasicTestingSetup& setup, ChainType chain, const std::string& prune)
{
    ArgsManager args;
    args.ForceSetArg("-prune", prune);
    const auto params{CreateChainParams(args, chain)};
    KernelNotifications notifications{Assert(setup.m_node.shutdown_request), setup.m_node.exit_status, *Assert(setup.m_node.warnings)};
    BlockManager::Options opts{
        .chainparams = *params,
        .blocks_dir = setup.m_args.GetBlocksDirPath(),
        .notifications = notifications,
        .block_tree_db_params = DBParams{.path = setup.m_args.GetDataDirNet() / "blocks" / "index", .cache_bytes = 0},
    };
    if (auto applied{node::ApplyArgsManOptions(args, opts)}; !applied) return util::Error{util::ErrorString(applied)};
    return opts.prune_target;
}

/**
 * Manual pruning of `files` block files of 1000 MiB each, asked to prune every one of them: returns the
 * number of files pruned, after checking that the oldest went and that asking again prunes nothing more.
 */
size_t PruneAllManually(TestingSetup& setup, int files_wanted, uint64_t keep)
{
    ChainstateManager& chainman{*Assert(setup.m_node.chainman)};
    Chainstate& chainstate{chainman.ActiveChainstate()};
    BlockManager& blockman{chainman.m_blockman};
    BOOST_REQUIRE(blockman.IsPruneMode());

    LOCK(::cs_main);
    // Block files (small, -fastprune) holding the heights 1 to `written`, made 1000 MiB each.
    const CBlock& block{chainman.GetParams().GenesisBlock()};
    int written{0};
    int files{0};
    while (files < files_wanted) files = blockman.WriteBlock(block, ++written).nFile;
    for (int n{0}; n < files; ++n) {
        blockman.GetBlockFileInfo(n)->nSize = 1000_MiB;
        blockman.GetBlockFileInfo(n)->nUndoSize = 0;
    }
    const uint64_t current{blockman.GetBlockFileInfo(files)->nSize + uint64_t{blockman.GetBlockFileInfo(files)->nUndoSize}};

    // A chain whose tip is far enough above the files that every one of them may be pruned by height.
    std::vector<CBlockIndex> fake(written + MIN_BLOCKS_TO_KEEP + 100);
    for (size_t h{0}; h < fake.size(); ++h) {
        fake[h].nHeight = h;
        fake[h].pprev = h ? &fake[h - 1] : nullptr;
        fake[h].BuildSkip();
    }
    CBlockIndex* const genesis{chainstate.m_chain.Genesis()};
    chainstate.m_chain.SetTip(fake.back());
    BOOST_CHECK_EQUAL(chainstate.GetPruneRange(written).second, written);

    std::set<int> pruned;
    blockman.FindFilesToPruneManual(pruned, written, chainstate);
    const uint64_t left{blockman.CalculateCurrentUsage()};
    BOOST_CHECK_GE(left, keep);
    BOOST_CHECK_LT(left, keep + 1000_MiB);
    BOOST_CHECK_EQUAL(left, (files - pruned.size()) * 1000_MiB + current);
    // The oldest ones.
    for (int n{0}; n < static_cast<int>(pruned.size()); ++n) BOOST_CHECK(pruned.contains(n));
    // Asked again: nothing more goes.
    std::set<int> again;
    blockman.FindFilesToPruneManual(again, written, chainstate);
    BOOST_CHECK(again.empty());
    BOOST_CHECK_EQUAL(blockman.CalculateCurrentUsage(), left);

    chainstate.m_chain.SetTip(*genesis);
    BOOST_CHECK_EQUAL(files, files_wanted);
    return pruned.size();
}

struct TestnetManualPruneSetup : public TestingSetup {
    TestnetManualPruneSetup() : TestingSetup{ChainType::TESTNET, {.extra_args = {"-prune=1", "-fastprune"}}} {}
};
struct RegtestManualPruneSetup : public TestingSetup {
    RegtestManualPruneSetup() : TestingSetup{ChainType::REGTEST, {.extra_args = {"-prune=1", "-fastprune"}}} {}
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(prune_minimum_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(prune_minimum_per_network)
{
    BOOST_CHECK_EQUAL(MIN_PRUNE_TARGET, 10000_MiB);
    for (const ChainType chain : {ChainType::MAIN, ChainType::TESTNET, ChainType::SIGNET}) {
        BOOST_TEST_MESSAGE("network " << ChainTypeToString(chain));
        BOOST_CHECK_EQUAL(CreateChainParams(ArgsManager{}, chain)->MinPruneTarget(), MIN_PRUNE_TARGET);
        // Below the minimum: refused, with the reason.
        for (const std::string value : {"2", "550", "4999", "5000", "9999"}) {
            const auto refused{ApplyPrune(*this, chain, value)};
            BOOST_REQUIRE(!refused);
            BOOST_CHECK_EQUAL(util::ErrorString(refused).original, "Prune configured below the minimum of 10000 MiB.  Please use a higher number.");
        }
        // The minimum and above: taken.
        BOOST_CHECK_EQUAL(*Assert(ApplyPrune(*this, chain, "10000")), 10000_MiB);
        BOOST_CHECK_EQUAL(*Assert(ApplyPrune(*this, chain, "12000")), 12000_MiB);
        // Off, and manual pruning (which keeps as much: below).
        BOOST_CHECK_EQUAL(*Assert(ApplyPrune(*this, chain, "0")), 0U);
        BOOST_CHECK_EQUAL(*Assert(ApplyPrune(*this, chain, "1")), BlockManager::PRUNE_TARGET_MANUAL);
    }
    // Regtest, where the tests inherited from Bitcoin run: Bitcoin's minimum.
    BOOST_CHECK_EQUAL(CreateChainParams(ArgsManager{}, ChainType::REGTEST)->MinPruneTarget(), MIN_DISK_SPACE_FOR_BLOCK_FILES);
    BOOST_CHECK_EQUAL(*Assert(ApplyPrune(*this, ChainType::REGTEST, "550")), 550_MiB);
    const auto refused{ApplyPrune(*this, ChainType::REGTEST, "549")};
    BOOST_REQUIRE(!refused);
    BOOST_CHECK_EQUAL(util::ErrorString(refused).original, "Prune configured below the minimum of 550 MiB.  Please use a higher number.");
}

BOOST_FIXTURE_TEST_CASE(prune_minimum_manual_testnet, TestnetManualPruneSetup)
{
    // Asked to prune everything: of 22 files of 1000 MiB the 12 oldest go, 10000 MiB are left.
    BOOST_CHECK_EQUAL(PruneAllManually(*this, 22, MIN_PRUNE_TARGET), 12U);
}

BOOST_FIXTURE_TEST_CASE(prune_minimum_manual_regtest, RegtestManualPruneSetup)
{
    // Bitcoin's manual pruning: every file that may be pruned by height goes.
    BOOST_CHECK_EQUAL(PruneAllManually(*this, 22, 0), 22U);
}

BOOST_AUTO_TEST_SUITE_END()
