// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WEBUI_MODULEINSTALLER_H
#define BITCOIN_WEBUI_MODULEINSTALLER_H

#include <sync.h>
#include <univalue.h>
#include <util/chaintype.h>
#include <util/fs.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace webui {

/**
 * The programs of the sidechains, downloaded from their releases (doc/unified-app.md, "Downloads and updates"):
 * the releases of block-fabric/<stem> on GitHub (a fixed list of repositories, never an address of the page), the
 * package for this computer, checked against SHA256SUMS-<stem>.txt and against the release's manifest
 * (manifest-<stem>.json: the host API the daemon speaks, the file, its hash and size, the programs in it), and its
 * signature by a release key built into the app (manifest-<stem>.json.sig, BIP340) when there is one; then only the
 * programs named unpacked into <bin root>/<stem>/<tag>/, made current atomically, the version before kept for rollback.
 *
 * A release without a manifest, or of another host API, is not compatible: it is never downloaded.
 *
 * Backbone's sidechains (the eCash family, doc/unified-app.md §26) are taken another way, the one their releases have:
 * only the repository's latest release (GitHub's releases/latest: never a pre-release), its package for this computer
 * found by its name (<prefix>-<version>-<platform>.tar.gz, .zip on win64), checked against the release's SHA256SUMS; the
 * programs named found anywhere in the package, and its pages (pack/<stem>/ in the package, else the release's
 * <prefix>-<version>-pack.tar.gz) installed beside them. SHA256SUMS.sig (BIP340, over SHA256SUMS) is checked against the
 * release keys of the family when there are some (P3: the offline key; -sidechainsignatures=require then refuses an
 * unsigned release). The version made current before is kept for rollback, as above.
 */
class ModuleInstaller
{
public:
    //! What the pages of a sidechain ask of their host (sidechains.json hostApi, pack.info): a daemon must speak it.
    static constexpr int HOST_API{1};
    static constexpr uint64_t MAX_API_BYTES{2 * 1024 * 1024};
    static constexpr uint64_t MAX_SUMS_BYTES{64 * 1024};
    static constexpr uint64_t MAX_MANIFEST_BYTES{64 * 1024};
    static constexpr uint64_t MAX_SIG_BYTES{1024};
    static constexpr uint64_t MAX_ARCHIVE_BYTES{200 * 1024 * 1024};
    //! The tag of the hash a manifest is signed over (BIP340 tagged hash of the manifest's bytes).
    static constexpr std::string_view SIGNATURE_TAG{"ChainsReleaseManifest/1"};
    //! The tag of the hash the SHA256SUMS of a latest-release repository are signed over (SHA256SUMS.sig).
    static constexpr std::string_view SUMS_SIGNATURE_TAG{"BackboneReleaseSums/1"};
    //! A pack's files: at most so many, of so many bytes in all (its fonts and languages: about 5 MB).
    static constexpr uint64_t MAX_PACK_BYTES{64 * 1024 * 1024};
    static constexpr size_t MAX_PACK_FILES{4000};

    /**
     * Where the releases of a sidechain are: the one table of the repositories asked (no other is ever asked, and the
     * page cannot name one). `prefix` begins the names of the release's files: <prefix>-<tag>-<platform>.tar.gz,
     * SHA256SUMS-<prefix>.txt, manifest-<prefix>.json(.sig). Today block-fabric/<stem> with the stem as prefix; a renamed
     * repository (block-fabric/backbone-<stem>) is one line here.
     */
    struct Repo {
        std::string stem;
        std::string owner;
        std::string repo;
        std::string prefix;
        //! Backbone's sidechains: the latest release only, checked by its SHA256SUMS (no manifest), its pages with it.
        bool latest{false};
        //! The programs installed (without .exe), the first one the daemon; empty: <stem>d and <stem>-cli.
        std::vector<std::string> programs{};
    };
    /**
     * The table of the network: Chains' repositories on the Chains networks; Backbone's on the eCash networks (and on
     * regtest with eCash's rules, ecash_regtest): block-fabric/backbone-flush and block-fabric/backbone-thunder, their
     * latest releases (doc/unified-app.md §26).
     */
    static const std::vector<Repo>& Repos(ChainType chain, bool ecash_regtest = false);
    /** The table of the network the app runs (UseNetwork), Chains' until it is set. */
    static const std::vector<Repo>& Repos();
    /** The network the app runs, for Repos() and what follows from it: set once, when the network is known (App). */
    static void UseNetwork(ChainType chain, bool ecash_regtest = false);
    static const Repo* FindRepo(std::string_view stem);
    /** The sidechains whose releases can be downloaded (the stems of Repos). */
    static std::vector<std::string> AllowedStems();
    static bool IsAllowed(std::string_view stem);
    /** The platform of this build, as the release packages name it: "x86_64-linux-gnu", "win64", or "" (none). */
    static std::string Platform();
    /** <prefix>-<tag>-<platform>.tar.gz (.zip for win64), the prefix of the stem's repository. */
    static std::string ArchiveName(const std::string& stem, const std::string& tag, const std::string& platform);
    /** The names of the programs a release may put on this computer: <stem>d and <stem>-cli (.exe on win64). */
    static std::vector<std::string> AllowedBinaries(const std::string& stem, const std::string& platform);
    /** A tag of a release: letters, digits, '.', '_', '-' (not first), at most 40. */
    static bool IsValidTag(std::string_view tag);
    /** Tags compared as numbers where they are digits (20261020.10 after 20261020.9): <0, 0, >0. */
    static int CompareTags(std::string_view a, std::string_view b);
    /** The release keys built into the app (x-only public keys, hex). Empty until the maintainer makes the key. */
    static const std::vector<std::string>& ReleaseKeys();
    /** The release keys of Backbone's sidechains (SHA256SUMS.sig): empty until P3 (the key made offline). */
    static const std::vector<std::string>& SumsKeys();
    /** Whether a signature (hex, 64 bytes) of a manifest verifies under one of the keys (hex x-only). */
    static bool VerifyManifestSignature(std::string_view manifest, std::string_view sig_hex, const std::vector<std::string>& keys, std::string_view tag = SIGNATURE_TAG);
    /** The file of a latest-release repository's package for a platform among a release's files, "" when there is not one alone. */
    static std::string LatestArchive(const Repo& repo, const std::vector<std::string>& names, const std::string& platform);
    /** Its pages' asset among a release's files (<prefix>-<version>-pack.tar.gz), "". */
    static std::string LatestPack(const Repo& repo, const std::vector<std::string>& names);
    /** The SHA-256 a SHA256SUMS text gives a file ("" when none). */
    static std::string SumOf(std::string_view sums, std::string_view name);

    struct FetchRequest {
        std::string url;
        //! Refused past this many bytes (the download is stopped, the file removed).
        uint64_t max_bytes{0};
        //! The size expected, for the progress (0: not known).
        uint64_t expected{0};
        fs::path out;
        std::function<void(uint64_t done, uint64_t total)> progress;
        std::function<bool()> cancelled;
    };
    struct FetchResult {
        bool ok{false};
        bool cancelled{false};
        bool too_large{false};
        std::string error;
    };
    /** Download one https address into a file (the tests give one of their own). */
    using Fetcher = std::function<FetchResult(const FetchRequest&)>;
    /** The fetcher of the app: curl, https only (redirects too), at most 5 redirects, the size limited, cancellable. */
    static Fetcher CurlFetcher(std::optional<fs::path> cacert = std::nullopt);

    struct Options {
        //! <Chains datadir>/sidechains/bin
        fs::path bin_root;
        //! Where the list of the releases is asked: https://api.github.com (a mirror on regtest, the tests).
        std::string api_base{"https://api.github.com"};
        //! Where the files of a release are: https://github.com (<base>/<owner>/<stem>/releases/download/<tag>/<file>).
        std::string download_base{"https://github.com"};
        std::string platform{Platform()};
        //! Keys trusted beside ReleaseKeys (regtest only: -sidechainreleasekey).
        std::vector<std::string> extra_keys;
        //! "auto": a signature present is checked, an unsigned release is taken; "require": only signed releases.
        std::string signatures{"auto"};
        Fetcher fetcher;
        //! How long a list of releases is kept before it is asked again (checkUpdates).
        std::chrono::seconds cache_for{std::chrono::hours{6}};
    };
    //! Something changed for a sidechain's download (its progress, its versions): the page is told.
    using NotifyFn = std::function<void(const std::string& stem)>;

    struct Release {
        std::string tag;
        std::string name;
        bool prerelease{false};
        std::string published;
        //! The package for this computer ("" when the release has none).
        std::string asset_name;
        uint64_t asset_size{0};
        bool sums{false};
        bool manifest{false};
        bool signature{false};
        bool signed_ok{false};
        //! No release key is configured: checked by its checksums only (shown "unsigned").
        bool unsigned_only{false};
        bool compatible{false};
        //! Why not, in plain words ("" when compatible).
        std::string reason;
        std::string sha256;
        std::vector<std::string> binaries;
        //! Latest-release repositories: the name of the sums file, its signature, and the pages' asset ("" none) with its hash.
        std::string sums_name;
        bool sums_signature{false};
        std::string pack_asset;
        uint64_t pack_size{0};
        std::string pack_sha256;
    };

    ModuleInstaller(Options options, NotifyFn notify = {});
    ~ModuleInstaller();
    ModuleInstaller(const ModuleInstaller&) = delete;
    ModuleInstaller& operator=(const ModuleInstaller&) = delete;

    const Options& GetOptions() const { return m_options; }

    /** The releases of a sidechain, newest first, each checked (asked again past cache_for, or when forced). Throws std::runtime_error. */
    std::vector<Release> Releases(const std::string& stem, bool force = false);
    /** Releases(), as modules.releases answers it (with what is installed; `source` from the supervisor). */
    UniValue ReleasesJson(const std::string& stem, const std::string& source, bool force = false);
    /** The newest compatible release newer than the current one, or nothing. */
    std::optional<std::string> UpdateFor(const std::string& stem, bool force = false);

    /** The version made current, the one before it (for rollback), every one installed. */
    std::optional<std::string> Current(const std::string& stem) const;
    std::optional<std::string> Previous(const std::string& stem) const;
    std::vector<std::string> Installed(const std::string& stem) const;
    /** The folder of the current version, when its daemon is there. */
    std::optional<fs::path> CurrentDir(const std::string& stem) const;
    /** The pages of the current version (its pack/, with pack.json), when the release had them. */
    std::optional<fs::path> CurrentPackDir(const std::string& stem) const;

    /**
     * Download, check and install a release now (the thread of a job, the tests). activate: made current at once
     * (else Activate later: an update stops the daemon first). A version older than the current one only with
     * allow_older (the user chose it; Rollback is the other way back). Throws std::runtime_error with what went wrong; nothing is
     * then installed, and what was there stays as it was.
     */
    void InstallNow(const std::string& stem, const std::string& tag, bool activate = true, bool allow_older = false);
    /** Make an installed version current; the current one becomes the previous one. Throws std::runtime_error. */
    void Activate(const std::string& stem, const std::string& tag);
    /** The previous version becomes current again (and the current one previous). Throws std::runtime_error. */
    void Rollback(const std::string& stem);

    /** InstallNow on a thread of its own (one job per sidechain at a time); then `done(ok)` on that thread. Throws when one runs. */
    void Start(const std::string& stem, const std::string& tag, bool activate, std::function<void(bool ok)> done = {}, bool allow_older = false);
    void Cancel(const std::string& stem);
    /**
     * The app stops: every job is cancelled and its thread (with its `done`) waited for; no job starts after. Call it
     * after the supervisor stopped (a `done` may wait for a daemon) and before the window goes. Idempotent.
     */
    void Stop();
    /** {state, tag, done, total, error}: idle, listing, downloading, verifying, unpacking, installing, done, failed, cancelled. */
    UniValue Progress(const std::string& stem) const;
    bool Busy(const std::string& stem) const;
    /** Wait for the job of a sidechain to end (the tests). */
    bool Wait(const std::string& stem, std::chrono::milliseconds timeout) const;

private:
    struct Job {
        std::string state{"idle"};
        std::string tag;
        uint64_t done{0};
        uint64_t total{0};
        std::string error;
        bool running{false};
        std::shared_ptr<std::atomic<bool>> cancel;
        std::thread thread;
    };
    struct Cached {
        std::chrono::steady_clock::time_point at;
        std::vector<Release> releases;
    };

    fs::path StemDir(const std::string& stem) const;
    std::string ReadPointer(const std::string& stem, const std::string& name) const;
    void WritePointer(const std::string& stem, const std::string& name, const std::string& value) const;
    void Prune(const std::string& stem, const std::string& keep = {}) const;
    std::string FetchSmall(const std::string& url, uint64_t max, const fs::path& tmp, const std::function<bool()>& cancelled) const;
    std::vector<Release> List(const std::string& stem, const std::function<bool()>& cancelled) const;
    void Check(const std::string& stem, Release& r, const std::string& base, const fs::path& tmp, const std::function<bool()>& cancelled) const;
    //! Check of a latest-release repository's release (its SHA256SUMS, its signature when keys are there).
    void CheckLatest(const std::string& stem, Release& r, const std::string& base, const fs::path& tmp, const std::function<bool()>& cancelled) const;
    //! The pages of a package (pack/<stem>/ in it, any depth) unpacked into `dest`: false when it has none. Throws.
    bool UnpackPack(const std::string& stem, const fs::path& archive, bool zip, const std::vector<std::string>& listing, const fs::path& work, const fs::path& dest) const;
    void Install(const std::string& stem, const std::string& tag, bool activate, bool allow_older, const std::function<bool()>& cancelled);
    void SetJob(const std::string& stem, const std::function<void(Job&)>& change, bool force_notify = false);

    const Options m_options;
    NotifyFn m_notify;
    mutable Mutex m_mutex;
    mutable std::condition_variable m_cv;
    std::map<std::string, Job> m_jobs GUARDED_BY(m_mutex);
    //! Stop() was called: no job starts again.
    bool m_stopped GUARDED_BY(m_mutex){false};
    std::map<std::string, Cached> m_cache GUARDED_BY(m_mutex);
    std::map<std::string, std::chrono::steady_clock::time_point> m_last_notify GUARDED_BY(m_mutex);
    //! The pointers (current, previous) of a sidechain are changed by one at a time.
    mutable Mutex m_install_mutex;
};

} // namespace webui

#endif // BITCOIN_WEBUI_MODULEINSTALLER_H
