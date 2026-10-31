// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WEBUI_CHAINADAPTER_H
#define BITCOIN_WEBUI_CHAINADAPTER_H

#include <univalue.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace webui {

/**
 * The app's own pages over every chain it runs (doc/unified-app.md §18): one adapter that answers the same questions
 * of each sidechain's node (its balances, wallets, transactions, addresses, sends, deposits and withdrawals, staking,
 * assets, names, markets), so that Overview, Send, Receive, Transactions, Wallets, Sidechains, Staking and Assets
 * show and act on every chain turned on.
 *
 * What each chain can do is a table (Capabilities), never guessed from what its node answers. The reads go to the
 * node over the supervisor's read path: the generic ones are methods the app's user may call there (AppRpcMethods),
 * the chain's own ones go through its host over RPC as reads of its own pages' policy (webuihost "rpc"; a read that
 * comes back with a question is refused, never asked). Everything that moves coins is built here from checked
 * parameters (an amount parsed exactly with the decimals of what is sent, an asset only one the wallet holds, an address
 * of that chain and no other) and sent through the chain's host over RPC: its own describers make the question, the
 * app asks it in its trusted dialog with the chain named, and only then the very same request runs with its token
 * (SidechainPages, §13). A deposit into a sidechain is Chains' own (createsidechaindeposit, its own question).
 */
class ChainAdapter
{
public:
    /** What a chain can do, beyond what every chain does (balances, wallets, transactions, send, receive, peg). */
    struct Capabilities {
        std::string stem;
        //! Assets of its own (Layers, BitAssets): listed, sent by the host; the DEX and auctions in its pages.
        bool assets{false};
        bool dex{false};
        //! Validators, bonds, rewards (FastBlocks, Layers).
        bool staking{false};
        //! Names (BitNames), markets (Hivemind), a shielded account (zSide).
        bool names{false};
        bool markets{false};
        bool shielded{false};
        //! Its pages the host shows inside its own (in the module's frame) where no host view is reasonable.
        std::vector<std::string> embed;
        /**
         * What its DEX does, for the app's Trade page (doc/unified-app.md §22): only with `dex`. Every DEX quotes a pair,
         * swaps in a pool, adds and removes liquidity of constant-product pools at 0.30 %, and lists its pools and the
         * wallet's shares; beyond that:
         */
        struct Dex {
            //! Routes through several pools (quoteroute, swaproute: Layers' DEX).
            bool route{false};
            //! Time-weighted average prices of a pool (getpooltwap): the pool's chart.
            bool twap{false};
            //! Pool kinds (curves, fee tiers, amplification, weights): offered when its node says they apply.
            bool curves{false};
            //! Limit orders and an order book (phase B): none yet.
            bool orders{false};
            //! 24-hour prices and volumes of every pair (phase C): none yet.
            bool stats{false};
            //! Its own Swap and Markets are the Trade page's (in its frame, headless): a DEX beyond the app's views
            //! (Flush: concentrated pools by default, order books, launches on curves, the best price over all of them).
            bool pages{false};
        } dex_features{};
        //! Its coin's word in its DEX's requests (CoinWord).
        std::string coin{"CHN"};
    };
    static const std::vector<Capabilities>& Table();
    /** Whether a chain is a drop-in on the network the app runs (its wallet is its node's own, in its pages). */
    static bool Dropin(std::string_view stem);
    /** The word a chain's RPCs take for its coin in the DEX's requests: Layers' "CHN" (the Chains suite), Flush's "ECX". */
    static std::string CoinWord(std::string_view stem);
    /**
     * The methods of a sidechain's daemon the app's own pages use there (this adapter, chaindex.cpp), by its capabilities:
     * named in that daemon's -rpcwhitelist for this app's user beside its pack's (SidechainSupervisor::AppRpcMethodsFor).
     */
    static std::vector<std::string> MethodsUsed(std::string_view stem);
    static const Capabilities* Find(std::string_view stem);
    static UniValue CapabilitiesJson(const Capabilities& caps);

    // ---- Pure parts (tested without a node) ----

    //! An amount as typed (dot decimals, no sign, no exponent), exactly, in units of 10^-decimals: nullopt when not.
    static std::optional<int64_t> ParseUnits(std::string_view text, int decimals);
    static std::string FormatUnits(int64_t units, int decimals);
    //! A number of a node's answer, as the exact text it wrote (amounts never go through a double here).
    static std::string AmountText(const UniValue& value);
    //! The coins of a wallet as the chain's own getbalance counts them, from getbalances' "mine": available is
    //! trusted + nonmempool (coins spent by a transaction not in the mempool are not available: nonmempool is their
    //! negative sum), never below zero; pending and immature as the node gives them. {trusted, pending, immature}.
    static UniValue WalletBalances(const UniValue& mine);

    enum class AddressKind { EMPTY, DEPOSIT, BECH32, BASE58, OTHER };
    //! What an address looks like (not whether it is valid): its bech32 prefix in hrp when it is one.
    static AddressKind KindOf(std::string_view address, std::string* hrp = nullptr);

    //! The wallet a chain's views use: the one asked for if loaded, else "main", else the first loaded ("" without one).
    /** The wallet a call uses: the one asked for if loaded, else the one of the backup phrase (preferred) if loaded, else "main", else the first. */
    static std::string PickWallet(const UniValue& loaded, const std::string& wanted, const std::string& preferred = {});
    //! The rows of listtransactions as the app shows them: bounded, checked, amounts as text, newest first.
    static UniValue TransactionRows(const UniValue& listed, size_t max);
    //! The assets of listmyassets: {id, label, decimals, balance, pending, control}, at most 200.
    static UniValue AssetRows(const UniValue& listed);
    //! A row of a node's answer with only plain fields of a bounded size (a market, a name): never nested data.
    static UniValue PlainFields(const UniValue& object, const std::vector<std::string>& keys);

    //! The requests the host sends to a chain's host over RPC (the same as its own pages would make).
    static UniValue PrepareSendRequest(const std::string& wallet, const std::string& address, int64_t sats, bool subtract_fee);
    static UniValue CommitSendRequest(const std::string& wallet, const std::string& token);
    static UniValue ConfirmedRequest(const std::string& wallet, const std::string& method, const UniValue& params);
    static UniValue AssetSendRequest(const std::string& wallet, const std::string& address, const std::string& asset_id, const std::string& amount);
    //! sendshielded [{address: amount}, memo, fee]: the fee given (the chain's estimate), so that its question shows what is paid.
    static UniValue ShieldedSendRequest(const std::string& wallet, const std::string& address, const std::string& amount, const std::string& memo, const std::string& fee);
    static UniValue WithdrawRequest(const std::string& wallet, const std::string& mainchain_address, const std::string& amount, const std::string& fee, const UniValue& inputs = UniValue{});

    //! Coins a spend names as its only inputs (coin control): [{txid, vout}], 1 to 500, each a txid of 64 lower-case hex
    //! digits and an output number, none named twice; as a clean array. Null for none given; throws HostError otherwise.
    static UniValue ParseInputs(const UniValue& inputs);
    //! The coins of listunspent as a page may choose them: {txid, vout, amount (text), confirmations, address, label},
    //! spendable ones holding coins only (a coin of no value carries an asset or a name: never paid with), 500 at most,
    //! the largest first.
    static UniValue CoinRows(const UniValue& listed);
    //! Whether every coin named is among the coins listed (CoinRows): the first that is not, else "".
    static std::string MissingCoin(const UniValue& inputs, const UniValue& coins);

    // ---- Over the nodes ----

    struct Hooks {
        //! A method of a chain's node as the app's user there (AppRpcMethods); wallet "" for none. Throws std::runtime_error.
        std::function<UniValue(const std::string& stem, const std::string& method, const UniValue& params, const std::string& wallet)> rpc;
        //! A request of the chain's host over RPC, its questions asked in the trusted dialog (SidechainPages::Call). Throws HostError.
        std::function<UniValue(const std::string& stem, const UniValue& request)> call;
        //! The same, with rows of the app's own shown in the dialog after the chain's name (what the app checked: a
        //! swap's route quoted again). Unset: `call` (the rows are not shown).
        std::function<UniValue(const std::string& stem, const UniValue& request, const UniValue& app_rows)> call_rows;
        //! Its node runs and answers.
        std::function<bool(const std::string& stem)> running;
        //! Its name (the catalog), and the name of this app's own chain and coin.
        std::function<std::string(const std::string& stem)> name;
        std::function<std::string()> product;
        //! The sidechains that run now (for the checks against the other chains).
        std::function<std::vector<std::string>()> running_chains;
        //! An address on this app's own chain: {isvalid, ismine} (its own wallet).
        std::function<UniValue(const std::string& address)> mainchain_address;
        //! A deposit into a sidechain, by this app's wallet with Chains' own question: {destination, amount}.
        std::function<UniValue(const std::string& stem, const UniValue& params)> deposit;
        //! Whether its wallet is made from the app's backup phrase ("shared") or its own ("separate").
        std::function<std::string(const std::string& stem)> wallet_mode;
        //! The wallet of a chain made from the backup phrase (SeedKeeper::MadeWallet, doc §23), "" when none: used first.
        std::function<std::string(const std::string& stem)> seed_wallet;
    };

    explicit ChainAdapter(Hooks hooks) : m_hooks(std::move(hooks)) {}

    //! Every method takes {stem, wallet?} and what it needs; it throws HostError with words for the user.
    UniValue Summary(const UniValue& params) const;
    UniValue Transactions(const UniValue& params) const;
    UniValue Transaction(const UniValue& params) const;
    UniValue Wallets(const UniValue& params) const;
    UniValue WalletCreate(const UniValue& params) const;
    UniValue WalletLoad(const UniValue& params) const;
    UniValue WalletUnload(const UniValue& params) const;
    UniValue WalletBackup(const UniValue& params) const;
    /**
     * {stem, name}: a backup file of that chain's wallet (its own: zSide's, whose shielded keys no backup phrase brings back)
     * chosen in the app's own chooser, restored by its node into a new wallet of that name, loaded. Nothing chosen: null.
     */
    UniValue WalletRestore(const UniValue& params) const;
    UniValue Receive(const UniValue& params) const;
    UniValue DepositAddress(const UniValue& params) const;
    /** {stem, address, shielded?} → {address, own, hrp} or a HostError that says which chain the address is of. */
    UniValue Validate(const UniValue& params) const;
    /** {stem, wallet, address, amount, asset?, subtractFee?, shielded?, memo?}: the chain's question in the trusted dialog. */
    UniValue Send(const UniValue& params) const;
    /** {stem, wallet, address (of this app's chain), amount, fee?, inputs?}: inputs only coins of that wallet (checked here). */
    UniValue Withdraw(const UniValue& params) const;
    /**
     * {stem (a chain with a shielded account: zSide), wallet?, direction: "shield" | "unshield", amount}: coins moved between
     * the wallet's own transparent coins and its shielded account (the chain's shieldcoins, unshieldcoins: to the wallet's
     * own addresses only), its own question in the trusted dialog.
     */
    UniValue Shield(const UniValue& params) const;
    static UniValue ShieldRequest(const std::string& wallet, bool shield, const std::string& amount, const std::string& fee = {});
    /** {stem, wallet (the sidechain's, credited), amount, inputs?}: paid by this app's wallet (with only the coins named,
     *  checked by the deposit hook against that wallet), asked in Chains' own dialog. */
    UniValue Deposit(const UniValue& params) const;
    /** {stem, wallet?} → {stem, wallet, coins: CoinRows}: the coins a withdrawal may be paid with. */
    UniValue Coins(const UniValue& params) const;
    UniValue Staking(const UniValue& params) const;
    UniValue Peg(const UniValue& params) const;
    UniValue Assets(const UniValue& params) const;
    UniValue Names(const UniValue& params) const;
    UniValue Markets(const UniValue& params) const;
    /** {stem (a chain of markets), wallet?} → the wallet's open positions, each with its market's words, outcome and price. */
    UniValue Positions(const UniValue& params) const;
    /** {stem} → {height, blocks (its latest 6: height, hash, time, txs), mempool: {size, rows (its newest 8: txid, time, fee, vsize)}}. */
    UniValue Activity(const UniValue& params) const;
    /**
     * {stem, q} → {stem, results: [{kind: block|tx|address, ...}]}: what a search of the explorer finds on that chain
     * (doc/unified-app.md §20.5): a height (its block), a 64-digit hash (a block, else a transaction its node or its wallet
     * knows), an address (valid there; the wallet's own said). Reads only; no index is asked for.
     */
    UniValue Search(const UniValue& params) const;
    /** chain.block {stem, hash}: a block of a sidechain, its facts and its transactions (the explorer's strips, §21.4). */
    UniValue Block(const UniValue& params) const;
    //! What a search text is: "height", "hash", "address" or "" (nothing a node is asked about).
    static std::string QueryKind(const std::string& q);
    //! Whether a search text may be a name (BitNames): a name, a subname (blog.core), its domain (core.x) or a number
    //! (#1739-0029); never digits alone (a height). The chain's own getname says whether it is one.
    static bool IsNameText(const std::string& q);
    //! The name a search text asks for: a domain of the names' own top level (core.x) is the name core.
    static std::string NameOf(const std::string& q);

    /** {stem (a chain of assets), asset (64 hex)} → {asset, hash, mime, url} (a data: URL of PNG or JPEG bytes) or null. */
    UniValue AssetLogo(const UniValue& params) const;
    //! A logo as a page may show it: a data: URL of PNG or JPEG in base64, bounded; anything else is not one.
    static bool IsLogoUrl(const std::string& url);

    // ---- The DEX (doc/unified-app.md §22): reads of the chain's own pages' policy, spends built here ----

    //! The id of an asset as the app names it: 64 lower-case hex digits, the coin of the chain all zeros. "" when not one.
    static std::string AssetIdOf(const UniValue& value);
    //! Whether an id is the chain's own coin (all zeros).
    static bool IsCoinId(const std::string& id);
    //! A pool of a node's listpools as a page shows it, checked and bounded (null when not one): {pool, a, b, reserveA,
    //! reserveB, price, shares, volumeA, volumeB, swaps, created, abandoned, curve, fee, amp, weightA, legacy}. A pool
    //! whose node says no kind is a constant-product pool at 0.30 %.
    static UniValue PoolRow(const UniValue& node_row);
    //! The slippage as the RPCs take it, from basis points (1 to 5000): "0.5" for 50.
    static std::string SlippageText(int bps);
    //! The least to receive: `units` less `bps` basis points, rounded down, never below 1.
    static int64_t LessSlippage(int64_t units, int bps);
    //! A pool kind as the wizard gives it: why it is not one (fee tiers, amplification, weights), "" when it is.
    static std::string WhyNotPoolKind(const std::string& curve, int64_t fee, int64_t amp, int64_t weight_a);
    //! The requests of the DEX (the same as the chain's own pages would make): assets by "0x" and their hash.
    static UniValue SwapRequest(const std::string& wallet, const std::vector<std::string>& path, const std::string& amount, int slippage_bps, const std::string& min_out, const std::string& coin = "CHN");
    static UniValue AddLiquidityRequest(const std::string& wallet, const std::string& a, const std::string& amount_a, const std::string& b, const std::string& amount_b, int slippage_bps, std::optional<bool> sets_price, const std::string& coin = "CHN");
    static UniValue RemoveLiquidityRequest(const std::string& wallet, const std::string& a, const std::string& b, const std::string& percent, int slippage_bps, const std::string& coin = "CHN");
    static UniValue CreatePoolRequest(const std::string& wallet, const std::string& a, const std::string& amount_a, const std::string& b, const std::string& amount_b, const std::string& curve, int64_t fee, int64_t amp, int64_t weight_a, int slippage_bps, bool by_kind, const std::string& coin = "CHN");
    //! By the pools' ids (the chain's pool kinds): exactly the pools shown, whatever else the pair has.
    static UniValue SwapPoolsRequest(const std::string& wallet, const std::string& asset_in, const std::vector<std::string>& pools, const std::string& amount, int slippage_bps, const std::string& min_out, const std::string& coin = "CHN");
    static UniValue AddPoolLiquidityRequest(const std::string& wallet, const std::string& pool, const std::string& a, const std::string& amount_a, const std::string& b, const std::string& amount_b, int slippage_bps, const std::string& coin = "CHN");
    static UniValue RemovePoolLiquidityRequest(const std::string& wallet, const std::string& pool, const std::string& percent, int slippage_bps);
    //! A fee tier in percent as the chain's createpool takes it: "0.3" for 3000 millionths.
    static std::string FeePercentText(int64_t millionths);

    /** chain.dex.info {stem} → {stem, features, active, maxHops, curvesActive, feeTiers, curves, amp, weights}. */
    UniValue DexInfo(const UniValue& params) const;
    /** chain.dex.pools {stem, asset?} → {stem, height, blockSeconds, pools: [PoolRow]} (at most 200). */
    UniValue DexPools(const UniValue& params) const;
    /** chain.dex.quote {stem, in, out, amount, maxHops?} → {amountIn, amountOut, routes: [{path, labels, amountOut, hops}], priceImpact, ...}. */
    UniValue DexQuote(const UniValue& params) const;
    /** chain.dex.chart {stem, pool} → {points: [{t, price}], spot, assetA, assetB} from its time-weighted averages (24 at most). */
    UniValue DexChart(const UniValue& params) const;
    /** chain.dex.positions {stem, wallet?} → {positions: [{pool, a, b, shares, percent, valueA, valueB}]}: the wallet's shares. */
    UniValue DexPositions(const UniValue& params) const;
    /** chain.dex.swap {stem, wallet?, in, out, amount, path, slippage (bps), minOut}: quoted again here, the route checked, then the chain's question. */
    UniValue DexSwap(const UniValue& params) const;
    /** chain.dex.addLiquidity {stem, wallet?, a, b, amountA, amountB?, slippage?, setsPrice?}. */
    UniValue DexAddLiquidity(const UniValue& params) const;
    /** chain.dex.removeLiquidity {stem, wallet?, pool, percent, slippage?}: only shares the wallet holds. */
    UniValue DexRemoveLiquidity(const UniValue& params) const;
    /** chain.dex.createPool {stem, wallet?, a, b, amountA, amountB, curve, fee, amp?, weightA?}. */
    UniValue DexCreatePool(const UniValue& params) const;

    //! The error a wrong address is refused with (RPC_INVALID_ADDRESS_OR_KEY).
    static constexpr int WRONG_ADDRESS{-5};

private:
    const Capabilities& Caps(const UniValue& params, std::string& stem) const;
    std::string Wallet(const std::string& stem, const UniValue& params) const;
    //! A read of the chain's own pages' policy, through its host over RPC: never a question.
    UniValue PageRead(const std::string& stem, const std::string& method, const UniValue& params, const std::string& wallet) const;
    //! A call with the app's own rows in its question (call_rows), else `call`.
    UniValue CallWithRows(const std::string& stem, const UniValue& request, const UniValue& rows) const;
    //! The DEX of a chain: its capabilities (refused without one).
    const Capabilities& DexCaps(const UniValue& params, std::string& stem) const;
    //! An asset of a chain by its id: {label, decimals} (the coin: its ticker, 8). Throws HostError when it has none such.
    std::pair<std::string, int> AssetOn(const std::string& stem, const std::string& id) const;
    //! What the wallet holds of an asset (or the coin), in units of its decimals.
    int64_t HeldUnits(const std::string& stem, const std::string& wallet, const std::string& id, int decimals) const;
    //! The pools of an asset (listpools by it, every page), as PoolRows, at most `most`.
    UniValue PoolsOf(const std::string& stem, const std::string& id, size_t most = 20000) const;
    //! A pool by its id, exactly (getpool by id, else the one row of that id among the pools of `first`), turned so that
    //! `first` is its asset a; null when not found or not one the app shows.
    UniValue PoolById(const std::string& stem, const std::string& pool, const std::string& first) const;
    //! Every pool of a pair (of every kind).
    std::vector<UniValue> PairPools(const std::string& stem, const std::string& a, const std::string& b) const;
    //! Whether the chain's spends name pools by their ids now (its pool kinds apply).
    bool ById(const std::string& stem) const;
    //! The fee of a payment out of a shielded account as the chain's own estimate says it (estimateshieldedfee).
    std::string ShieldedFee(const std::string& stem, const std::string& wallet, const std::string& address, const std::string& amount) const;
    //! The checks of an address against the chain it is sent on and every other chain this app runs.
    UniValue CheckAddress(const std::string& stem, const std::string& address, bool shielded) const;

    const Hooks m_hooks;
};

} // namespace webui

#endif // BITCOIN_WEBUI_CHAINADAPTER_H
