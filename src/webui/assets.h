// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WEBUI_ASSETS_H
#define BITCOIN_WEBUI_ASSETS_H

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace webui {

/** A file of the interface, built into the program. */
struct Asset {
    //! Path from the root of the interface, starting with "/": "/index.html", "/assets/index-1a2b3c.js".
    std::string_view path;
    std::span<const unsigned char> data;
};

/** The files of the interface (generated at build time from the output of the frontend build). */
std::span<const Asset> BuiltinAssets();

/** The files of a set of assets that a request path names, or nothing. "/" is "/index.html"; a query or fragment is ignored. */
const Asset* FindAsset(std::span<const Asset> assets, std::string_view request_path);

/** The media type of a file, from its extension. */
std::string_view MimeType(std::string_view path);

/** The scheme the interface is served from where the platform lets the app serve one: chains-app://app/ */
inline constexpr std::string_view SCHEME{"chains-app"};
inline constexpr std::string_view ORIGIN{"chains-app://app"};

/**
 * The content security policy of the interface: nothing but its own files,
 * no remote content, no eval, no plug-ins; frames only of the pages of the sidechains (PACK_SCHEME).
 */
std::string ContentSecurityPolicy();

/**
 * The pages of each sidechain the app runs (doc/unified-app.md §13) are served under an origin of their own,
 * chains-pack://<stem>/, never the app's: a frame of them cannot reach the page of the app (its bridge, the other
 * chains), only send it messages, which the page takes from that frame alone, for that chain alone.
 */
inline constexpr std::string_view PACK_SCHEME{"chains-pack"};
/** The file of the assets a request of a pack's origin may have (its own files under /packs/<stem>/, and the shim), or nothing. */
std::optional<std::string> PackAssetPath(std::string_view uri);
/** The policy of a pack's pages: its own files only, no network, no frames, framed by the app only. */
std::string PackContentSecurityPolicy();
/** Whether the app's own origin serves a path: not the packs (served under their own origins only). */
bool AppServes(std::string_view path);

/**
 * One self-contained HTML document with every script, style sheet and font of
 * the interface inside it, for the platforms where the app cannot serve files
 * under a scheme of its own: the script and the style sheet inline (allowed by
 * their hashes in the policy, not by 'unsafe-inline'), fonts and images as
 * data: URIs. Nothing if the index or a file it names is missing.
 */
std::optional<std::string> InlineDocument(std::span<const Asset> assets);

} // namespace webui

#endif // BITCOIN_WEBUI_ASSETS_H
