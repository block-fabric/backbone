// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WEBUI_OPTIONS_H
#define BITCOIN_WEBUI_OPTIONS_H

#include <univalue.h>

#include <functional>
#include <optional>
#include <string>

class ArgsManager;
namespace interfaces {
class Node;
} // namespace interfaces

namespace webui {

/**
 * The options of the node that the Settings page changes, as OptionsModel of chains-qt
 * keeps them: in settings.json of the data directory (node.updateRwSetting), with the
 * same names and the same rules (a value set on the command line or in chains.conf wins
 * and cannot be changed here; the address of a proxy that is turned off is kept as
 * "proxy-prev"; the prune size as "prune-prev"; numbers written as strings for old
 * versions). Changes that only take effect after a restart say so.
 */
class NodeOptions
{
public:
    //! Remembers that a restart is needed (OptionsModel fRestartRequired), and tells whether it is.
    using RestartFlag = std::function<bool(std::optional<bool> set)>;

    NodeOptions(interfaces::Node& node, ArgsManager& args, RestartFlag restart);

    /**
     * Every option with its value, as the Settings page shows them:
     *   dbcache, par, prune, pruneSize, spendZeroConf, signer, natpmp, listen, server,
     *   proxyUse, proxyIp, proxyPort, torUse, torIp, torPort, lang, startAtLogin;
     * ignored {setting: true} for the ones set on the command line or in chains.conf,
     * overridden (the text of chains-qt: "-par=4 -listen=0 "), limits, restartRequired,
     * proxyReach {ipv4, ipv6, tor} (the networks the proxy shown is used for now).
     */
    UniValue Get();

    /** Change one option (a name of Get). Throws std::runtime_error for a value that is not valid. @return Get() */
    UniValue Set(const std::string& name, const UniValue& value);

    /** Whether text is the address of a proxy, without a port (ProxyAddressValidator). */
    static bool ValidProxyAddress(const std::string& text);

    /** Start the app when the user logs in (GUIUtil::SetStartOnSystemStartup), per network. */
    static bool StartAtLogin(ArgsManager& args);
    static bool SetStartAtLogin(ArgsManager& args, bool start);

private:
    interfaces::Node& m_node;
    ArgsManager& m_args;
    RestartFlag m_restart;
};

} // namespace webui

#endif // BITCOIN_WEBUI_OPTIONS_H
