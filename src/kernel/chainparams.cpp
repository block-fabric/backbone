// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/amount.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <crypto/hex_base.h>
#include <hash.h>
#include <kernel/ecash_repurpose.h>
#include <kernel/messagestartchars.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/verify_flags.h>
#include <uint256.h>
#include <util/byte_units.h>
#include <util/chaintype.h>
#include <util/log.h>
#include <util/strencodings.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <span>
#include <utility>

using namespace util::hex_literals;

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.version = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "The Times 03/Jan/2009 Chancellor on brink of second bailout for banks";
    const CScript genesisOutputScript = CScript() << "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f"_hex << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

void CChainParams::ApplyDeploymentOptions(const DeploymentOptions& opts)
{
    for (const auto& [dep, height] : opts.activation_heights) {
        switch (dep) {
        case Consensus::BuriedDeployment::DEPLOYMENT_SEGWIT:
            consensus.SegwitHeight = int{height};
            break;
        case Consensus::BuriedDeployment::DEPLOYMENT_HEIGHTINCB:
            consensus.BIP34Height = int{height};
            break;
        case Consensus::BuriedDeployment::DEPLOYMENT_DERSIG:
            consensus.BIP66Height = int{height};
            break;
        case Consensus::BuriedDeployment::DEPLOYMENT_CLTV:
            consensus.BIP65Height = int{height};
            break;
        case Consensus::BuriedDeployment::DEPLOYMENT_CSV:
            consensus.CSVHeight = int{height};
            break;
        }
    }

    for (const auto& [deployment_pos, version_bits_params] : opts.version_bits_parameters) {
        consensus.vDeployments[deployment_pos].nStartTime = version_bits_params.start_time;
        consensus.vDeployments[deployment_pos].nTimeout = version_bits_params.timeout;
        consensus.vDeployments[deployment_pos].min_activation_height = version_bits_params.min_activation_height;
    }
}

/**
 * Chains main network.
 */
class CMainParams : public CChainParams {
public:
    CMainParams(const MainNetOptions& opts) {
        m_chain_type = ChainType::MAIN;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 210000; // ~146 days at one-minute blocks
        // New chain: every buried deployment is active from the first block.
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 0;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nPowTargetSpacing = 60; // one-minute blocks
        consensus.nPowTargetTimespan = 2016 * consensus.nPowTargetSpacing; // only sizes version bits periods; difficulty uses aserti3
        consensus.asert_half_life = 2 * 60 * 60; // two hours, i.e. 120 blocks
        consensus.asert_anchor_height = 2016; // blocks before it are mined at the proof of work limit
        // Blocks of up to 800,000 weight units every minute: as much room for
        // transactions per hour as Bitcoin (400,000 per block), most activity
        // being on the sidechains, and as much again for the coinbase with its
        // drivechain messages and payouts.
        consensus.max_block_weight = CHAINS_MAX_BLOCK_WEIGHT;
        consensus.max_block_tx_weight = CHAINS_MAX_BLOCK_TX_WEIGHT;
        consensus.coinbase_maturity = 360; // six hours
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.enforce_BIP94 = false; // aserti3 is not subject to the timewarp attack
        consensus.fPowNoRetargeting = false;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 1815; // 90%;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 2016;

        ApplyDeploymentOptions(opts.dep_opts);

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xc7;
        pchMessageStart[1] = 0xe8;
        pchMessageStart[2] = 0xa9;
        pchMessageStart[3] = 0xd5;
        nDefaultPort = 9555;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;
        m_min_prune_target = MIN_PRUNE_TARGET;

        // The genesis reward pays an unspendable output: there is no premine.
        genesis = CreateGenesisBlock("Chains 01/Oct/2026 One chain to mine them all: drivechains for everyone", CScript() << OP_RETURN, 1790879567, 1864237343, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"00000000942856f7f35f8af81e0bcade5750224d12b81dfb66cd348c52b7cb4a"});
        assert(genesis.hashMerkleRoot == uint256{"597434e3ce8c541d1b6b739a1f2477b8785cb4b803084ca628c06cd1c2c9b5af"});

        // No DNS or fixed seeds yet; peers are added with -addnode.
        vFixedSeeds.clear();
        vSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,28);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,88);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,156);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "chn";

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        m_assumeutxo_data = {};

        chainTxData = ChainTxData{
            .nTime    = 1790879567,
            .tx_count = 1,
            .dTxRate  = 0,
        };

        // Bitcoin's values, to be regenerated with headerssync-params.py once the chain has history.
        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 649,
            .redownload_buffer_size = 15400,
        };
    }
};

/**
 * Chains public test network (testnet).
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams(const TestNetOptions& opts) {
        m_chain_type = ChainType::TESTNET;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 210000; // ~146 days at one-minute blocks
        // New chain: every buried deployment is active from the first block.
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 0;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nPowTargetSpacing = 60; // one-minute blocks
        consensus.nPowTargetTimespan = 2016 * consensus.nPowTargetSpacing; // only sizes version bits periods; difficulty uses aserti3
        consensus.asert_half_life = 2 * 60 * 60; // two hours, i.e. 120 blocks
        consensus.asert_anchor_height = 2016; // blocks before it are mined at the proof of work limit
        // Blocks of up to 800,000 weight units every minute: as much room for
        // transactions per hour as Bitcoin (400,000 per block), most activity
        // being on the sidechains, and as much again for the coinbase with its
        // drivechain messages and payouts.
        consensus.max_block_weight = CHAINS_MAX_BLOCK_WEIGHT;
        consensus.max_block_tx_weight = CHAINS_MAX_BLOCK_TX_WEIGHT;
        consensus.coinbase_maturity = 360; // six hours
        // Drivechain periods of a test network: short enough to go through a sidechain's life in a day.
        consensus.drivechain.activation_period = 60;
        consensus.drivechain.activation_max_failures = 30;
        consensus.drivechain.replacement_period = 120;
        consensus.drivechain.withdrawal_period = 600;
        consensus.drivechain.withdrawal_min_score = 300;
        consensus.drivechain.idle_expiry_blocks = 30;
        consensus.drivechain.upvote_expiry_blocks = 30;
        consensus.drivechain.unvoted_forget_blocks = 60;
        // The canonical vote form from the first block (the test network was reset on 17/Oct/2026 with
        // every rule from block 0).
        consensus.drivechain.canonical_votes_height = 0;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.enforce_BIP94 = false; // aserti3 is not subject to the timewarp attack
        consensus.fPowNoRetargeting = false;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 1512; // 75%;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 2016;

        ApplyDeploymentOptions(opts.dep_opts);

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xc7;
        pchMessageStart[1] = 0xe8;
        pchMessageStart[2] = 0xb4;
        pchMessageStart[3] = 0xf3;
        nDefaultPort = 19555;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;
        m_min_prune_target = MIN_PRUNE_TARGET;

        // The genesis reward pays an unspendable output: there is no premine.
        // Reset on 17/Oct/2026 (after the seventh audit), with every rule from block 0; restarted before on
        // 08/Oct/2026 ("Chains testnet 08/Oct/2026 after the fourth audit: one chain to mine them all").
        genesis = CreateGenesisBlock("Chains testnet 17/Oct/2026: a fresh start", CScript() << OP_RETURN, 1792210201, 554687656, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"000000002a936e1d0749da638f3fdb6950bcfc10e6d82848c3bedccd208724b1"});
        assert(genesis.hashMerkleRoot == uint256{"21f24c9597db46e18501a7df4bbb7d98507facc9a421305e1d126f03b5d7d07b"});

        // No DNS or fixed seeds yet; peers are added with -addnode.
        vFixedSeeds.clear();
        vSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tchn";

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        m_assumeutxo_data = {};

        chainTxData = ChainTxData{
            .nTime    = 1792210201,
            .tx_count = 1,
            .dTxRate  = 0,
        };

        // Bitcoin's values, to be regenerated with headerssync-params.py once the chain has history.
        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 649,
            .redownload_buffer_size = 15400,
        };
    }
};

/**
 * The parameters every eCash network shares: Bitcoin's (the history up to the fork is Bitcoin's,
 * and the rules after it are Bitcoin's but for the fork rules below), with eCash's fork rules.
 */
static void SetEcashForkRules(Consensus::Params& consensus)
{
    consensus.ecash = true;
    consensus.drivechain_nop8 = true;
    // Bitcoin's block size, maturity and difficulty adjustment (no aserti3).
    consensus.max_block_weight = MAX_BLOCK_WEIGHT;
    consensus.max_block_tx_weight = MAX_BLOCK_WEIGHT;
    consensus.coinbase_maturity = COINBASE_MATURITY;
    consensus.asert_half_life = 0;
    consensus.asert_anchor_height = 0;
    // BIP300/301 as the bip300301_enforcer of LayerTwo Labs (lib/types.rs): votes counted per period,
    // no Chains-only rule in consensus.
    consensus.drivechain.rules = Consensus::DrivechainParams::RULES_ENFORCER;
}

/** Bitcoin's main network up to the eCash fork: the consensus history of eCash's main network and of betanet. */
static void SetBitcoinMainHistory(Consensus::Params& consensus)
{
    consensus.signet_blocks = false;
    consensus.signet_challenge.clear();
    consensus.nSubsidyHalvingInterval = 210000;
    consensus.script_flag_exceptions.emplace( // BIP16 exception
        uint256{"00000000000002dc756eebf4f49723ed8d30cc28a5f108eb94b1ba88ac4f9c22"}, SCRIPT_VERIFY_NONE);
    consensus.script_flag_exceptions.emplace( // Taproot exception
        uint256{"0000000000000000000f14c35b2d841e986ab5441de8c585d5ffe55ea1e395ad"}, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS);
    consensus.BIP34Height = 227931;
    consensus.BIP34Hash = uint256{"000000000000024b89b42a942fe0d9fea3bb44ab7bd1b19115dd6a759c0808b8"};
    consensus.BIP65Height = 388381; // 000000000000000004c2b624ed5d7756c508d90fd0da2c7c679febfa6c4735f0
    consensus.BIP66Height = 363725; // 00000000000000000379eaa19dce8c9b722d46ae6a57c2f1a988119488b50931
    consensus.CSVHeight = 419328; // 000000000000000004a1b34462cb8aeebd5799177f7a29cf28f2d1961716b5b5
    consensus.SegwitHeight = 481824; // 0000000000000000001c8018d9cb3b742ef25114f27563e3fc4a1902167f9893
    consensus.MinBIP9WarningHeight = 711648; // taproot activation height + miner confirmation window
    consensus.powLimit = uint256{"00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
    consensus.nPowTargetSpacing = 10 * 60;
    consensus.fPowAllowMinDifficultyBlocks = false;
    consensus.enforce_BIP94 = false;
    consensus.fPowNoRetargeting = false;
    consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
    consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
    consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
    consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
    consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 1815; // 90%
    consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 2016;
    // Bitcoin Core v32's, below both fork heights (block 966143): the chain work of eCash's chains at the
    // fork is Bitcoin's.
    consensus.nMinimumChainWork = uint256{"000000000000000000000000000000000000000145ec036acc5ba740052af1a0"};
    consensus.defaultAssumeValid = uint256{"00000000000000000000748969ec33043c0e52a763c6dd5193861f559f2c72e3"}; // 966143
    SetEcashForkRules(consensus);
}

/** Bitcoin's assumeutxo snapshots, all of them below both fork heights (as in ecash-com/bitcoin). */
static std::vector<AssumeutxoData> BitcoinMainSnapshots()
{
    return {
        {
            .height = 840'000,
            .hash_serialized = AssumeutxoHash{uint256{"a2a5521b1b5ab65f67818e5e8eccabb7171a517f9e2382208f77687310768f96"}},
            .m_chain_tx_count = 991032194,
            .blockhash = uint256{"0000000000000000000320283a032748cef8227873ff4872689bf23f1cda83a5"},
        },
        {
            .height = 880'000,
            .hash_serialized = AssumeutxoHash{uint256{"dbd190983eaf433ef7c15f78a278ae42c00ef52e0fd2a54953782175fbadcea9"}},
            .m_chain_tx_count = 1145604538,
            .blockhash = uint256{"000000000000000000010b17283c3c400507969a9c2afd1dcf2082ec5cca2880"},
        },
        {
            .height = 910'000,
            .hash_serialized = AssumeutxoHash{uint256{"4daf8a17b4902498c5787966a2b51c613acdab5df5db73f196fa59a4da2f1568"}},
            .m_chain_tx_count = 1226586151,
            .blockhash = uint256{"0000000000000000000108970acb9522ffd516eae17acddcb1bd16469194a821"},
        },
        {
            .height = 935'000,
            .hash_serialized = AssumeutxoHash{uint256{"e4b90ef9eae834f56c4b64d2d50143cee10ad87994c614d7d04125e2a6025050"}},
            .m_chain_tx_count = 1305397408,
            .blockhash = uint256{"0000000000000000000147034958af1652b2b91bba607beacc5e72a56f0fb5ee"},
        },
        {
            .height = 965'000,
            .hash_serialized = AssumeutxoHash{uint256{"4a8d794337118c0c615b574f817c7306c687584a537184b8d233df42bf477ec2"}},
            .m_chain_tx_count = 1429611231,
            .blockhash = uint256{"00000000000000000001595977e6000ce56129f5c9b4073e31ccc30b90b97da9"},
        },
    };
}

/**
 * The main network of eCash (ECX): Bitcoin's chain, forked at block 973,728 (ecash-com/bitcoin, tag
 * ecash-v1.1.0-core31.1, branch master: v1.0.0 and the dampened 288-block retarget after the fork).
 */
class CEcashMainParams : public CChainParams {
public:
    explicit CEcashMainParams(const CChainParams::MainNetOptions& opts) {
        m_chain_type = ChainType::ECASH;
        SetBitcoinMainHistory(consensus);
        consensus.ecash_fork_height = 973728; // Retarget boundary, 2016 * 483
        consensus.ecash_fork_bits = 0x1901a6e1; // Difficulty 2.6e9 at the fork block
        // The reset is computed by the retarget (CalculateNextWorkRequired), which runs at these heights only.
        assert(consensus.ecash_fork_height % consensus.DifficultyAdjustmentInterval() == 0);
        // From the fork block, a dampened retarget every 288 blocks (ecash-v1.1.0-core31.1, 5bd8690fab).
        consensus.ecash_retarget_interval = 288;
        assert(consensus.ecash_fork_height % consensus.ecash_retarget_interval == 0);
        consensus.replay_locktime = LOCKTIME_THRESHOLD - 2;
        consensus.repurpose_txids = &kernel::ECASH_MAIN_REPURPOSE_TXIDS;
        // BIP300/301 from the fork. The bip300301_enforcer has no preset for eCash's main network yet
        // (its betanet preset, commit 17eb2ce, is the latest): the thresholds are those of its
        // Thresholds::MAINNET, those of the BIP300 specification, until LayerTwo Labs publishes one.
        consensus.drivechain.activation_height = consensus.ecash_fork_height;
        consensus.drivechain.unused_slot_threshold = 1815;
        consensus.drivechain.unused_slot_max_age = 2016;
        consensus.drivechain.used_slot_threshold = 13150;
        consensus.drivechain.used_slot_max_age = 26300;
        consensus.drivechain.bundle_threshold = 13150;
        consensus.drivechain.bundle_max_age = 26300;
        // Our safety rules for the enforcer (WithdrawalBundleSafety::MAINNET, MessageLimits::DEFAULT: upvote
        // expiry 144, idle expiry 1008, 64 pending bundles, the defaults): policy until the enforcer adopts
        // them at a height, which goes here (safety_activation_height, limits_activation_height).
        consensus.drivechain.safety_activation_height = Consensus::DrivechainParams::NEVER;
        consensus.drivechain.limits_activation_height = Consensus::DrivechainParams::NEVER;

        ApplyDeploymentOptions(opts.dep_opts);

        pchMessageStart[0] = 0xec;
        pchMessageStart[1] = 0xa5;
        pchMessageStart[2] = 0xdc;
        pchMessageStart[3] = 0xb7;
        nDefaultPort = 8533;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 856;
        m_assumed_chain_state_size = 14;
        m_min_prune_target = MIN_PRUNE_TARGET;

        genesis = CreateGenesisBlock(1231006505, 2083236893, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"});
        assert(genesis.hashMerkleRoot == uint256{"4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"});

        // eCash's seeds; no fixed seeds, as in ecash-com/bitcoin.
        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.emplace_back("seed.ecash.ninja.");
        vSeeds.emplace_back("seed.bip300.xyz.");
        vSeeds.emplace_back("seed.ecash.drivecha.in.");
        vSeeds.emplace_back("seed.ecash.zuexeuz.net.");
        vSeeds.emplace_back("seed.ecash.eu.com.");

        // Bitcoin's: a Bitcoin address is an eCash address.
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,0);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,5);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};
        bech32_hrp = "bc";

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        m_assumeutxo_data = BitcoinMainSnapshots();

        chainTxData = ChainTxData{
            // Bitcoin's, at block 966143 (Bitcoin Core v32): before the fork.
            .nTime    = 1788925573,
            .tx_count = 1434982600,
            .dTxRate  = 7.851784479579372,
        };

        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 649,
            .redownload_buffer_size = 15400, // 15400/649 = ~23.7 commitments
        };
    }
};

/**
 * Betanet: eCash's rehearsal of its main network, Bitcoin's chain forked at block 967,680
 * (ecash-com/bitcoin, branch betanet), whose main network it is.
 */
class CEcashBetanetParams : public CChainParams {
public:
    explicit CEcashBetanetParams(const CChainParams::MainNetOptions& opts) {
        m_chain_type = ChainType::ECASH_BETANET;
        SetBitcoinMainHistory(consensus);
        consensus.ecash_fork_height = 967680; // Retarget boundary, 2016 * 480
        consensus.ecash_fork_bits = 0x19044b7e; // Difficulty 1e9 at the fork block
        // The reset is computed by the retarget (CalculateNextWorkRequired), which runs at these heights only.
        assert(consensus.ecash_fork_height % consensus.DifficultyAdjustmentInterval() == 0);
        consensus.replay_locktime = LOCKTIME_THRESHOLD - 1;
        consensus.repurpose_txids = &kernel::ECASH_BETANET_REPURPOSE_TXIDS;
        // The bip300301_enforcer's betanet preset (commit 17eb2ce, lib/types.rs NetworkParams::betanet,
        // Thresholds::BETANET): from the fork, an unused slot activates with more than 1008 acks in 2016 blocks.
        consensus.drivechain.activation_height = consensus.ecash_fork_height;
        consensus.drivechain.unused_slot_threshold = 1008;
        consensus.drivechain.unused_slot_max_age = 2016;
        consensus.drivechain.used_slot_threshold = 13150;
        consensus.drivechain.used_slot_max_age = 26300;
        consensus.drivechain.bundle_threshold = 13150;
        consensus.drivechain.bundle_max_age = 26300;
        // Our safety rules for the enforcer (its betanet values: WithdrawalBundleSafety::MAINNET,
        // MessageLimits::DEFAULT): policy until the enforcer adopts them at a height, which goes here.
        consensus.drivechain.safety_activation_height = Consensus::DrivechainParams::NEVER;
        consensus.drivechain.limits_activation_height = Consensus::DrivechainParams::NEVER;

        ApplyDeploymentOptions(opts.dep_opts);

        pchMessageStart[0] = 0xec;
        pchMessageStart[1] = 0xa5;
        pchMessageStart[2] = 0xb1;
        pchMessageStart[3] = 0x04;
        nDefaultPort = 8533;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 856;
        m_assumed_chain_state_size = 14;
        m_min_prune_target = MIN_PRUNE_TARGET;

        genesis = CreateGenesisBlock(1231006505, 2083236893, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"});
        assert(genesis.hashMerkleRoot == uint256{"4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"});

        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.emplace_back("seed.beta.ecash.ninja.");
        vSeeds.emplace_back("seed.beta.bip300.xyz.");
        vSeeds.emplace_back("seed.beta.ecash.drivecha.in.");
        vSeeds.emplace_back("seed.beta.ecash.zuexeuz.net.");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,0);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,5);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};
        bech32_hrp = "bc";

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        m_assumeutxo_data = BitcoinMainSnapshots();

        chainTxData = ChainTxData{
            .nTime    = 1788925573,
            .tx_count = 1434982600,
            .dTxRate  = 7.851784479579372,
        };

        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 649,
            .redownload_buffer_size = 15400,
        };
    }
};

/**
 * eCash's test network: Bitcoin's testnet4 under eCash's rules and with eCash's message start
 * (ecash-com/bitcoin, CTestNet4Params). It has no fork height: its chain is testnet4's.
 */
class CEcashTestNetParams : public CChainParams {
public:
    explicit CEcashTestNetParams(const CChainParams::TestNetOptions& opts) {
        m_chain_type = ChainType::ECASH_TESTNET;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.enforce_BIP94 = true;
        consensus.fPowNoRetargeting = false;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 1512; // 75%
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 2016;
        SetEcashForkRules(consensus);
        // eCash's release (tag ecash-v1.0.0-core31.1) makes the same value final on every network.
        consensus.replay_locktime = LOCKTIME_THRESHOLD - 2;
        // The enforcer has no preset for an eCash test network: it runs testnet4 with its Thresholds::SHORT.
        // BIP300/301 starts above testnet4's history (161,280 = 2016 * 80), which is Bitcoin's, scanned by nobody.
        consensus.drivechain.activation_height = 161280;
        consensus.drivechain.unused_slot_threshold = 5;
        consensus.drivechain.unused_slot_max_age = 10;
        consensus.drivechain.used_slot_threshold = 5;
        consensus.drivechain.used_slot_max_age = 10;
        consensus.drivechain.bundle_threshold = 5;
        consensus.drivechain.bundle_max_age = 10;
        // Our safety rules for the enforcer with its values for these thresholds (WithdrawalBundleSafety::SHORT):
        // policy until it adopts them at a height.
        consensus.drivechain.upvote_expiry_blocks = 5;
        consensus.drivechain.idle_expiry_blocks = 5;
        consensus.drivechain.safety_activation_height = Consensus::DrivechainParams::NEVER;
        consensus.drivechain.limits_activation_height = Consensus::DrivechainParams::NEVER;

        ApplyDeploymentOptions(opts.dep_opts);

        // Bitcoin Core v32's for testnet4, whose chain this is.
        consensus.nMinimumChainWork = uint256{"000000000000000000000000000000000000000000000e346a558455ade8eca9"};
        consensus.defaultAssumeValid = uint256{"0000000021df65b91665a342e26ceb05e54826ad7d8fcd40316230058fa3b865"}; // 151604

        pchMessageStart[0] = 0xec;
        pchMessageStart[1] = 0xa5;
        pchMessageStart[2] = 0xb1;
        pchMessageStart[3] = 0x24;
        nDefaultPort = 48533;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 31;
        m_assumed_chain_state_size = 2;
        m_min_prune_target = MIN_PRUNE_TARGET;

        const char* testnet4_genesis_msg = "03/May/2024 000000000000000000001ebd58c244970b3aa9d783bb001011fbe8ea8e98e00e";
        const CScript testnet4_genesis_script = CScript() << "000000000000000000000000000000000000000000000000000000000000000000"_hex << OP_CHECKSIG;
        genesis = CreateGenesisBlock(testnet4_genesis_msg,
                testnet4_genesis_script,
                1714777860,
                393743547,
                0x1d00ffff,
                1,
                50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"00000000da84f2bafbbc53dee25a72ae507ff4914b867c565be350b0da8bf043"});
        assert(genesis.hashMerkleRoot == uint256{"7aa0a7ae1e223414cb807e40cd57e667b718e42aaf9306db9102fe28912b7b4e"});

        // eCash keeps Bitcoin's testnet4 seeds, which list Bitcoin nodes (another message start): an eCash
        // test node finds its peers with -addnode/-seednode.
        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.emplace_back("seed.testnet4.bitcoin.sprovoost.nl."); // Sjors Provoost
        vSeeds.emplace_back("seed.testnet4.wiz.biz."); // Jason Maurice

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
        bech32_hrp = "tb";

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        m_assumeutxo_data = {
            {
                .height = 90'000,
                .hash_serialized = AssumeutxoHash{uint256{"784fb5e98241de66fdd429f4392155c9e7db5c017148e66e8fdbc95746f8b9b5"}},
                .m_chain_tx_count = 11347043,
                .blockhash = uint256{"0000000002ebe8bcda020e0dd6ccfbdfac531d2f6a81457191b99fc2df2dbe3b"},
            },
            {
                .height = 120'000,
                .hash_serialized = AssumeutxoHash{uint256{"10b05d05ad468d0971162e1b222a4aa66caca89da2bb2a93f8f37fb29c4794b0"}},
                .m_chain_tx_count = 14141057,
                .blockhash = uint256{"000000000bd2317e51b3c5794981c35ba894ce27d3e772d5c39ecd9cbce01dc8"},
            },
            {
                .height = 150'000,
                .hash_serialized = AssumeutxoHash{uint256{"ca068cae50679d7c947454bbe4f0e6aeec1fbe2c6c2735a08bb988623649f950"}},
                .m_chain_tx_count = 14810011,
                .blockhash = uint256{"0000000000d9877342754dea8ec1eb24631517d38e3443c370465ee53a8b7434"},
            }
        };

        chainTxData = ChainTxData{
            .nTime    = 1788932739,
            .tx_count = 14825119,
            .dTxRate  = 0.1148250171344748,
        };

        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 616,
            .redownload_buffer_size = 16199, // 16199/616 = ~26.3 commitments
        };
    }
};

/**
 * Signet: test network with an additional consensus parameter (see BIP325).
 */
class SigNetParams : public CChainParams {
public:
    explicit SigNetParams(const SigNetOptions& options)
    {
        std::vector<uint8_t> bin;
        vFixedSeeds.clear();
        vSeeds.clear();

        if (!options.challenge) {
            // The default Chains signet: blocks are signed with a single key.
            bin = "5121022eb69a435e256daf541a170b29fdedbd5ef5f6efbd66f67cfec24402a2b1f35551ae"_hex_v_u8;
        } else {
            bin = *options.challenge;
            LogInfo("Signet with challenge %s", HexStr(bin));
        }
        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;
        m_min_prune_target = MIN_PRUNE_TARGET;

        if (options.seeds) {
            vSeeds = *options.seeds;
        }

        // Apart from the block signatures and the lower proof of work limit,
        // signet follows the rules of the main network.
        m_chain_type = ChainType::SIGNET;
        consensus.signet_blocks = true;
        consensus.signet_challenge.assign(bin.begin(), bin.end());
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;
        consensus.nPowTargetSpacing = 60; // one-minute blocks
        consensus.nPowTargetTimespan = 2016 * consensus.nPowTargetSpacing; // only sizes version bits periods; difficulty uses aserti3
        consensus.asert_half_life = 2 * 60 * 60; // two hours, i.e. 120 blocks
        consensus.asert_anchor_height = 2016; // blocks before it are mined at the proof of work limit
        consensus.max_block_weight = CHAINS_MAX_BLOCK_WEIGHT;
        consensus.max_block_tx_weight = CHAINS_MAX_BLOCK_TX_WEIGHT;
        consensus.coinbase_maturity = 360; // six hours
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.enforce_BIP94 = false; // aserti3 is not subject to the timewarp attack
        consensus.fPowNoRetargeting = false;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"00000377ae000000000000000000000000000000000000000000000000000000"};
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 1815; // 90%
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 2016;

        ApplyDeploymentOptions(options.dep_opts);

        // message start is defined as the first 4 bytes of the sha256d of the block script
        HashWriter h{};
        h << consensus.signet_challenge;
        uint256 hash = h.GetHash();
        std::copy_n(hash.begin(), 4, pchMessageStart.begin());

        nDefaultPort = 39555;
        nPruneAfterHeight = 1000;

        // The genesis reward pays an unspendable output: there is no premine.
        genesis = CreateGenesisBlock("Chains signet 01/Oct/2026 One chain to mine them all: drivechains for everyone", CScript() << OP_RETURN, 1790879568, 9736156, 0x1e0377ae, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"000001ca9ff240bbe9e7883dee99e91d1096c95e10cee82979a6cd1f08364073"});
        assert(genesis.hashMerkleRoot == uint256{"661cbd2140e027a506eb6ced73964fdfe58c7becce7e66ae5a9710a9266057e7"});

        m_assumeutxo_data = {};

        chainTxData = ChainTxData{
            .nTime    = 1790879568,
            .tx_count = 1,
            .dTxRate  = 0,
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // As on Bitcoin, signet shares its address format with testnet.
        bech32_hrp = "tchn";

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        // Bitcoin's values, to be regenerated with headerssync-params.py once the chain has history.
        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 629,
            .redownload_buffer_size = 15885,
        };
    }
};

/**
 * Regression test: intended for private networks only. Has minimal difficulty to ensure that
 * blocks can be found instantly.
 */
class CRegTestParams : public CChainParams
{
public:
    explicit CRegTestParams(const RegTestOptions& opts)
    {
        m_chain_type = ChainType::REGTEST;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 150;
        // Short drivechain periods so that tests can run through them.
        consensus.drivechain.activation_period = 20;
        consensus.drivechain.activation_max_failures = 9;
        consensus.drivechain.replacement_period = 40;
        consensus.drivechain.withdrawal_period = 60;
        consensus.drivechain.withdrawal_min_score = 30;
        consensus.drivechain.upvote_expiry_blocks = 20;
        consensus.drivechain.unvoted_forget_blocks = 20;
        if (opts.ecash) {
            // The rules of the eCash networks; their drivechain enforcer with its regtest thresholds
            // (Thresholds::SHORT), from block 0 unless -testdrivechainparam=activation_height@<height>.
            SetEcashForkRules(consensus);
            consensus.replay_locktime = opts.ecash->replay_locktime;
            m_repurpose_txids = opts.ecash->repurpose_txids;
            consensus.repurpose_txids = &m_repurpose_txids;
            consensus.drivechain.unused_slot_threshold = 5;
            consensus.drivechain.unused_slot_max_age = 10;
            consensus.drivechain.used_slot_threshold = 5;
            consensus.drivechain.used_slot_max_age = 10;
            consensus.drivechain.bundle_threshold = 5;
            consensus.drivechain.bundle_max_age = 10;
            // Our safety rules for the enforcer: its values for these thresholds (WithdrawalBundleSafety::SHORT),
            // not consensus unless -testdrivechainparam=safety_activation_height@<height> (and limits_...).
            consensus.drivechain.upvote_expiry_blocks = 5;
            consensus.drivechain.idle_expiry_blocks = 5;
        }
        for (const auto& [name, value] : opts.drivechain_params) {
            auto& dc{consensus.drivechain};
            if (name == "max_sidechains") {
                dc.max_sidechains = value;
            } else if (name == "activation_period") {
                dc.activation_period = value;
            } else if (name == "activation_max_failures") {
                dc.activation_max_failures = value;
            } else if (name == "replacement_period") {
                dc.replacement_period = value;
            } else if (name == "withdrawal_period") {
                dc.withdrawal_period = value;
            } else if (name == "withdrawal_min_score") {
                dc.withdrawal_min_score = value;
            } else if (name == "max_pending_bundles") {
                dc.max_pending_bundles = value;
            } else if (name == "idle_expiry_blocks") {
                dc.idle_expiry_blocks = value;
            } else if (name == "upvote_expiry_blocks") {
                dc.upvote_expiry_blocks = value;
            } else if (name == "unvoted_forget_blocks") {
                dc.unvoted_forget_blocks = value;
            } else if (name == "canonical_votes_height") {
                dc.canonical_votes_height = value;
            } else if (name == "rules") {
                dc.rules = value;
            } else if (name == "activation_height") {
                dc.activation_height = value;
            } else if (name == "unused_slot_threshold") {
                dc.unused_slot_threshold = value;
            } else if (name == "unused_slot_max_age") {
                dc.unused_slot_max_age = value;
            } else if (name == "used_slot_threshold") {
                dc.used_slot_threshold = value;
            } else if (name == "used_slot_max_age") {
                dc.used_slot_max_age = value;
            } else if (name == "bundle_threshold") {
                dc.bundle_threshold = value;
            } else if (name == "bundle_max_age") {
                dc.bundle_max_age = value;
            } else if (name == "safety_activation_height") {
                dc.safety_activation_height = value;
            } else if (name == "limits_activation_height") {
                dc.limits_activation_height = value;
            } else {
                throw std::runtime_error(strprintf("Invalid name (%s) for -testdrivechainparam=name@value.", name));
            }
        }
        consensus.BIP34Height = 1; // Always active unless overridden
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;  // Always active unless overridden
        consensus.BIP66Height = 1;  // Always active unless overridden
        consensus.CSVHeight = 1;    // Always active unless overridden
        consensus.SegwitHeight = 0; // Always active unless overridden
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nPowTargetTimespan = 24 * 60 * 60; // one day
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.enforce_BIP94 = opts.enforce_bip94;
        consensus.fPowNoRetargeting = !opts.pow_retargeting;
        if (opts.ecash && opts.ecash->fork_height > 0) {
            // A fork as eCash's: the retarget at the fork height resets the difficulty, so regtest retargets.
            consensus.fPowNoRetargeting = false;
            consensus.ecash_fork_height = opts.ecash->fork_height;
            consensus.ecash_fork_bits = opts.ecash->fork_bits;
            if (consensus.ecash_fork_height % consensus.DifficultyAdjustmentInterval() != 0) {
                throw std::runtime_error(strprintf("-testecashfork: the fork height %d is no multiple of the retarget interval (%d).",
                                                   consensus.ecash_fork_height, consensus.DifficultyAdjustmentInterval()));
            }
        }

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].threshold = 108; // 75%
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].period = 144; // Faster than normal for regtest (144 instead of 2016)

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        pchMessageStart[0] = 0xc7;
        pchMessageStart[1] = 0xe8;
        pchMessageStart[2] = 0xc1;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 29555;
        if (opts.ecash && opts.ecash->ecash_magic) {
            // eCash's client on regtest (ecash-com/bitcoin, "Use custom network magic and default ports"): its
            // message start and port, so that a regtest node of each can peer (and read each other's block files).
            // Not the default: -testecash alone shares regtest's with Bitcoin's rules (feature_ecash_fork.py).
            pchMessageStart = {0xec, 0xa5, 0xb1, 0x34};
            nDefaultPort = 18644;
        }
        nPruneAfterHeight = opts.fastprune ? 100 : 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;
        m_min_prune_target = 550_MiB; // Bitcoin's MIN_DISK_SPACE_FOR_BLOCK_FILES, for the tests inherited from Bitcoin

        ApplyDeploymentOptions(opts.dep_opts);

        genesis = CreateGenesisBlock(1296688602, 2, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"});
        assert(genesis.hashMerkleRoot == uint256{"4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"});

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();
        vSeeds.emplace_back("dummySeed.invalid.");

        fDefaultConsistencyChecks = true;
        m_is_mockable_chain = true;

        m_assumeutxo_data = {
            {   // For use by unit tests
                .height = 110,
                .hash_serialized = AssumeutxoHash{uint256{"86e9a1205b418b16dde3a18a78c730e30137e28466bda5dbf6b33ab8fc05447c"}},
                .m_chain_tx_count = 111,
                .blockhash = uint256{"135eec25a6fb277884e5824e7aa7d052c4868161c99a5122170b5266f86c273d"},
            },
            {
                // For use by fuzz target src/test/fuzz/utxo_snapshot.cpp
                .height = 200,
                .hash_serialized = AssumeutxoHash{uint256{"17dcc016d188d16068907cdeb38b75691a118d43053b8cd6a25969419381d13a"}},
                .m_chain_tx_count = 201,
                .blockhash = uint256{"385901ccbd69dff6bbd00065d01fb8a9e464dede7cfe0372443884f9b1dcf6b9"},
            },
            {
                // For use by test/functional/feature_assumeutxo.py and test/functional/tool_bitcoin_chainstate.py
                .height = 299,
                .hash_serialized = AssumeutxoHash{uint256{"106b2c56233e378a824cf0d5ff2be42ed32c72f1605c9be288d00942908a40ac"}},
                .m_chain_tx_count = 334,
                .blockhash = uint256{"0c552ced4721c249a389eb9b08cb8da261cd46f0e7b5f9d064d48f3113406853"},
            },
        };

        chainTxData = ChainTxData{
            .nTime = 0,
            .tx_count = 0,
            .dTxRate = 0.001, // Set a non-zero rate to make it testable
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = opts.ecash && opts.ecash->ecash_regtest ? "bcrt" : "rchn";

        // Copied from Testnet4.
        m_headers_sync_params = HeadersSyncParams{
            .commitment_period = 275,
            .redownload_buffer_size = 7017, // 7017/275 = ~25.5 commitments
        };
    }

private:
    //! The repurpose transactions of -testrepurposetx.
    std::set<uint256> m_repurpose_txids;
};

std::unique_ptr<const CChainParams> CChainParams::SigNet(const SigNetOptions& options)
{
    return std::make_unique<const SigNetParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::RegTest(const RegTestOptions& options)
{
    return std::make_unique<const CRegTestParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::Main(const MainNetOptions& options)
{
    return std::make_unique<const CMainParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::TestNet(const TestNetOptions& options)
{
    return std::make_unique<const CTestNetParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::EcashMain(const MainNetOptions& options)
{
    return std::make_unique<const CEcashMainParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::EcashBetanet(const MainNetOptions& options)
{
    return std::make_unique<const CEcashBetanetParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::EcashTestNet(const TestNetOptions& options)
{
    return std::make_unique<const CEcashTestNetParams>(options);
}


std::vector<int> CChainParams::GetAvailableSnapshotHeights() const
{
    std::vector<int> heights;
    heights.reserve(m_assumeutxo_data.size());

    for (const auto& data : m_assumeutxo_data) {
        heights.emplace_back(data.height);
    }
    return heights;
}

std::optional<ChainType> GetNetworkForMagic(const MessageStartChars& message)
{
    const auto mainnet_msg = CChainParams::Main()->MessageStart();
    const auto testnet_msg = CChainParams::TestNet()->MessageStart();
    const auto regtest_msg = CChainParams::RegTest()->MessageStart();
    const auto signet_msg = CChainParams::SigNet()->MessageStart();
    const auto ecash_msg = CChainParams::EcashMain()->MessageStart();
    const auto ecash_betanet_msg = CChainParams::EcashBetanet()->MessageStart();
    const auto ecash_testnet_msg = CChainParams::EcashTestNet()->MessageStart();

    if (std::ranges::equal(message, ecash_msg)) {
        return ChainType::ECASH;
    } else if (std::ranges::equal(message, ecash_betanet_msg)) {
        return ChainType::ECASH_BETANET;
    } else if (std::ranges::equal(message, ecash_testnet_msg)) {
        return ChainType::ECASH_TESTNET;
    } else if (std::ranges::equal(message, mainnet_msg)) {
        return ChainType::MAIN;
    } else if (std::ranges::equal(message, testnet_msg)) {
        return ChainType::TESTNET;
    } else if (std::ranges::equal(message, regtest_msg)) {
        return ChainType::REGTEST;
    } else if (std::ranges::equal(message, signet_msg)) {
        return ChainType::SIGNET;
    }
    return std::nullopt;
}
