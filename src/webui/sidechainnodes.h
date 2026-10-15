// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WEBUI_SIDECHAINNODES_H
#define BITCOIN_WEBUI_SIDECHAINNODES_H

#include <univalue.h>
#include <util/fs.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

class ArgsManager;

namespace webui {

class Bridge;

/**
 * The Sidechain Nodes window of chains-qt (sidechainnodes.cpp), for the page: the sidechains
 * that can be run from here (the built-in ones and the ones the user added), where their
 * programs are (chosen by the user in a folder chooser, or downloaded with a hash check), and
 * their nodes: started in the background pointed at this node, stopped, asked how they are
 * doing, merged mining turned on and off, one block mined for a fee, the end of their log.
 *
 * Only programs in a folder the user chose (or a download the user checked by its hash) are
 * ever run, and only the programs of a sidechain: <stem>d, <stem>-cli, <stem>-qt.
 */
class SidechainNodes
{
public:
    struct Sidechain {
        std::string name;
        //! Stem of the names of its programs: <stem>d, <stem>-cli, <stem>-qt.
        std::string stem;
        int slot{0};
        std::string description;
        bool builtin{true};
    };

    //! The value of the app settings with the sidechains added and their folders (AppConfig::Value), and how to keep it.
    using LoadFn = std::function<UniValue()>;
    using StoreFn = std::function<void(const UniValue&)>;
    //! Ask the user for a folder (title, the folder to start in); nothing when cancelled.
    using ChooseFolderFn = std::function<std::optional<std::string>(const std::string& title, const std::string& start)>;

    SidechainNodes(ArgsManager& args, LoadFn load, StoreFn store, ChooseFolderFn choose_folder);

    /** Make the methods (sidechainNodes.*) available to the page. */
    void Register(Bridge& bridge);

    /** Run a method directly (the tests; Register uses the same functions). Throws HostError. */
    UniValue Call(const std::string& method, const UniValue& params);

    std::vector<Sidechain> Sidechains() const;
    std::string BinDir(const std::string& stem) const;
    /** Where the node of a sidechain run from here keeps its data: <datadir>/sidechains/<stem>/data. */
    fs::path DataDir(const Sidechain& sidechain) const;
    /** The arguments a program of a sidechain is run with to reach its node on the network of this node. */
    std::vector<std::string> NetworkArgs(const Sidechain& sidechain) const;
    /**
     * The arguments its node is started with on top of those (pointed at this node). Never a password: with
     * -rpcpassword, the node reads it from PasswordFile (-mainchainrpcpasswordfile), which WritePasswordFile
     * writes before the node starts.
     */
    std::vector<std::string> NodeArgs(const Sidechain& sidechain, const std::optional<std::string>& wallet) const;
    /** <sidechain data dir>/mainchain.rpcpass */
    fs::path PasswordFile(const Sidechain& sidechain) const;
    /** Write the RPC password of this node to PasswordFile, owner-only and replaced atomically; remove it if this node has none. */
    void WritePasswordFile(const Sidechain& sidechain) const;

    /** The output of a program run to its end: exit code (-1: it could not be started), stdout, stderr. */
    struct Output {
        int code{-1};
        std::string out;
        std::string err;
    };
    /** Run a program and wait for it (at most `timeout_s` seconds, then it is killed). */
    static Output Run(const std::vector<std::string>& argv, int timeout_s);
    /** Start a program that keeps running after this one, sharing nothing with it. */
    static bool Launch(const std::vector<std::string>& argv);

private:
    std::optional<Sidechain> Find(const std::string& stem) const;
    std::string Program(const Sidechain& sidechain, const std::string& suffix) const;
    /** Run a command of the node of a sidechain: its answer (JSON, or a bare word as a string), or the error. */
    std::pair<std::optional<UniValue>, std::string> Cli(const Sidechain& sidechain, const std::vector<std::string>& command) const;
    void SetBinDir(const std::string& stem, const std::string& dir);

    ArgsManager& m_args;
    LoadFn m_load;
    StoreFn m_store;
    ChooseFolderFn m_choose_folder;
};

} // namespace webui

#endif // BITCOIN_WEBUI_SIDECHAINNODES_H
