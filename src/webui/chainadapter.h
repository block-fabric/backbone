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
    };
    static const std::vector<Capabilities>& Table();
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
    static std::string PickWallet(const UniValue& loaded, const std::string& wanted);
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
    static UniValue ShieldedSendRequest(const std::string& wallet, const std::string& address, const std::string& amount, const std::string& memo);
    static UniValue WithdrawRequest(const std::string& wallet, const std::string& mainchain_address, const std::string& amount, const std::string& fee);

    // ---- Over the nodes ----

    struct Hooks {
        //! A method of a chain's node as the app's user there (AppRpcMethods); wallet "" for none. Throws std::runtime_error.
        std::function<UniValue(const std::string& stem, const std::string& method, const UniValue& params, const std::string& wallet)> rpc;
        //! A request of the chain's host over RPC, its questions asked in the trusted dialog (SidechainPages::Call). Throws HostError.
        std::function<UniValue(const std::string& stem, const UniValue& request)> call;
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
    UniValue Receive(const UniValue& params) const;
    UniValue DepositAddress(const UniValue& params) const;
    /** {stem, address, shielded?} → {address, own, hrp} or a HostError that says which chain the address is of. */
    UniValue Validate(const UniValue& params) const;
    /** {stem, wallet, address, amount, asset?, subtractFee?, shielded?, memo?}: the chain's question in the trusted dialog. */
    UniValue Send(const UniValue& params) const;
    /** {stem, wallet, address (of this app's chain), amount, fee?}. */
    UniValue Withdraw(const UniValue& params) const;
    /** {stem, wallet (the sidechain's, credited), amount}: paid by this app's wallet, asked in Chains' own dialog. */
    UniValue Deposit(const UniValue& params) const;
    UniValue Staking(const UniValue& params) const;
    UniValue Peg(const UniValue& params) const;
    UniValue Assets(const UniValue& params) const;
    UniValue Names(const UniValue& params) const;
    UniValue Markets(const UniValue& params) const;
    /** {stem (a chain of markets), wallet?} → the wallet's open positions, each with its market's words, outcome and price. */
    UniValue Positions(const UniValue& params) const;
    /** {stem} → {height, blocks (its latest 6: height, hash, time, txs), mempool: {size, rows (its newest 8: txid, time, fee, vsize)}}. */
    UniValue Activity(const UniValue& params) const;
    /** {stem (a chain of assets), asset (64 hex)} → {asset, hash, mime, url} (a data: URL of PNG or JPEG bytes) or null. */
    UniValue AssetLogo(const UniValue& params) const;
    //! A logo as a page may show it: a data: URL of PNG or JPEG in base64, bounded; anything else is not one.
    static bool IsLogoUrl(const std::string& url);

    //! The error a wrong address is refused with (RPC_INVALID_ADDRESS_OR_KEY).
    static constexpr int WRONG_ADDRESS{-5};

private:
    const Capabilities& Caps(const UniValue& params, std::string& stem) const;
    std::string Wallet(const std::string& stem, const UniValue& params) const;
    //! A read of the chain's own pages' policy, through its host over RPC: never a question.
    UniValue PageRead(const std::string& stem, const std::string& method, const UniValue& params, const std::string& wallet) const;
    //! The checks of an address against the chain it is sent on and every other chain this app runs.
    UniValue CheckAddress(const std::string& stem, const std::string& address, bool shielded) const;

    const Hooks m_hooks;
};

} // namespace webui

#endif // BITCOIN_WEBUI_CHAINADAPTER_H
