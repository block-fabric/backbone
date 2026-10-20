// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <webui/moduleinstaller.h>

#include <crypto/sha256.h>
#include <hash.h>
#include <logging.h>
#include <pubkey.h>
#include <random.h>
#include <tinyformat.h>
#include <uint256.h>
#include <util/fs_helpers.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <util/threadnames.h>
#include <util/time.h>
#include <webui/sidechainnodes.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

#ifndef WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using util::TrimString;

namespace webui {

namespace {

using Error = std::runtime_error;

//! A job stopped by the user: not a failure.
struct Cancelled : std::runtime_error {
    Cancelled() : std::runtime_error("Cancelled") {}
};

bool IsAlphaNumeric(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool IsValidAssetName(std::string_view name)
{
    if (name.empty() || name.size() > 128 || name[0] == '.') return false;
    return std::all_of(name.begin(), name.end(), [](char c) { return IsAlphaNumeric(c) || c == '.' || c == '_' || c == '-' || c == '+'; });
}

std::optional<std::string> ReadFile(const fs::path& path, uint64_t max)
{
    std::ifstream file{path.std_path(), std::ios::binary};
    if (!file) return std::nullopt;
    std::string out;
    char buffer[65536];
    while (file) {
        file.read(buffer, sizeof(buffer));
        out.append(buffer, file.gcount());
        if (out.size() > max) return std::nullopt;
    }
    return out;
}

std::string FileSha256(const fs::path& path)
{
    std::ifstream file{path.std_path(), std::ios::binary};
    if (!file) return {};
    CSHA256 hasher;
    std::vector<char> chunk(1 << 20);
    while (file) {
        file.read(chunk.data(), chunk.size());
        if (file.gcount() > 0) hasher.Write(reinterpret_cast<const unsigned char*>(chunk.data()), file.gcount());
    }
    unsigned char hash[CSHA256::OUTPUT_SIZE];
    hasher.Finalize(hash);
    return HexStr(hash);
}

bool IsSha256Hex(std::string_view s)
{
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

void WriteAtomic(const fs::path& path, const std::string& text)
{
    const fs::path tmp{path.parent_path() / fs::PathFromString("." + fs::PathToString(path.filename()) + ".tmp")};
    {
        std::ofstream file{tmp.std_path(), std::ios::binary | std::ios::trunc};
        if (!file) throw Error(strprintf("Cannot write %s", fs::PathToString(tmp)));
        file << text;
        file.flush();
        if (!file) throw Error(strprintf("Cannot write %s", fs::PathToString(tmp)));
    }
    if (!RenameOver(tmp, path)) throw Error(strprintf("Cannot write %s", fs::PathToString(path)));
}

void RemoveAll(const fs::path& path)
{
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

std::string Random8()
{
    return HexStr(FastRandomContext{}.randbytes(8));
}

//! A member of an archive that could write outside where it is unpacked.
bool UnsafeMember(std::string_view name)
{
    if (name.empty()) return false;
    if (name[0] == '/' || name[0] == '\\') return true;
    if (name.size() > 1 && name[1] == ':') return true;
    if (name.find('\\') != std::string_view::npos) return true;
    for (const auto& part : util::SplitString(name, '/')) {
        if (part == "..") return true;
    }
    return false;
}

std::string HumanSize(uint64_t bytes)
{
    return strprintf("%.1f MB", static_cast<double>(bytes) / 1e6);
}

} // namespace

const std::vector<ModuleInstaller::Repo>& ModuleInstaller::Repos()
{
    // The one table of the repositories whose releases this app downloads (the sidechains of supervisor.cpp's catalog).
    static const std::vector<Repo> repos{
        {"layers", "block-fabric", "layers", "layers"},
        {"fastblocks", "block-fabric", "fastblocks", "fastblocks"},
        {"thunder", "block-fabric", "thunder", "thunder"},
        {"bitnames", "block-fabric", "bitnames", "bitnames"},
        {"bitassets", "block-fabric", "bitassets", "bitassets"},
        {"zside", "block-fabric", "zside", "zside"},
        {"hivemind", "block-fabric", "hivemind", "hivemind"},
    };
    return repos;
}

const ModuleInstaller::Repo* ModuleInstaller::FindRepo(std::string_view stem)
{
    for (const Repo& r : Repos()) {
        if (r.stem == stem) return &r;
    }
    return nullptr;
}

const std::vector<std::string>& ModuleInstaller::AllowedStems()
{
    static const std::vector<std::string> stems{[] {
        std::vector<std::string> out;
        for (const Repo& r : Repos()) out.push_back(r.stem);
        return out;
    }()};
    return stems;
}

bool ModuleInstaller::IsAllowed(std::string_view stem)
{
    return FindRepo(stem) != nullptr;
}

std::string ModuleInstaller::Platform()
{
#if defined(WIN32) && (defined(__x86_64__) || defined(_M_X64))
    return "win64";
#elif defined(__linux__) && defined(__x86_64__)
    return "x86_64-linux-gnu";
#else
    return "";
#endif
}

std::string ModuleInstaller::ArchiveName(const std::string& stem, const std::string& tag, const std::string& platform)
{
    const Repo* repo{FindRepo(stem)};
    return strprintf("%s-%s-%s%s", repo ? repo->prefix : stem, tag, platform, platform == "win64" ? ".zip" : ".tar.gz");
}

std::vector<std::string> ModuleInstaller::AllowedBinaries(const std::string& stem, const std::string& platform)
{
    const std::string ext{platform == "win64" ? ".exe" : ""};
    return {stem + "d" + ext, stem + "-cli" + ext};
}

bool ModuleInstaller::IsValidTag(std::string_view tag)
{
    if (tag.empty() || tag.size() > 40 || tag[0] == '.' || tag[0] == '-') return false;
    return std::all_of(tag.begin(), tag.end(), [](char c) { return IsAlphaNumeric(c) || c == '.' || c == '_' || c == '-'; });
}

int ModuleInstaller::CompareTags(std::string_view a, std::string_view b)
{
    size_t i{0}, j{0};
    const auto digit{[](char c) { return c >= '0' && c <= '9'; }};
    while (i < a.size() && j < b.size()) {
        if (digit(a[i]) && digit(b[j])) {
            size_t ei{i}, ej{j};
            while (ei < a.size() && digit(a[ei])) ++ei;
            while (ej < b.size() && digit(b[ej])) ++ej;
            std::string_view na{a.substr(i, ei - i)}, nb{b.substr(j, ej - j)};
            while (na.size() > 1 && na[0] == '0') na.remove_prefix(1);
            while (nb.size() > 1 && nb[0] == '0') nb.remove_prefix(1);
            if (na.size() != nb.size()) return na.size() < nb.size() ? -1 : 1;
            if (const int c{na.compare(nb)}; c != 0) return c < 0 ? -1 : 1;
            i = ei;
            j = ej;
        } else {
            if (a[i] != b[j]) return a[i] < b[j] ? -1 : 1;
            ++i;
            ++j;
        }
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

const std::vector<std::string>& ModuleInstaller::ReleaseKeys()
{
    // The release key of the sidechains' packages: made OFFLINE by the maintainer (never on a build machine nor in CI),
    // its x-only public key (64 hex digits) written here, so that every app trusts it from its build on. Until then the
    // list is empty: signatures cannot be checked by a release build, and -sidechainsignatures=require refuses every
    // release (doc/unified-app.md, "Downloads and updates").
    static const std::vector<std::string> keys{};
    return keys;
}

bool ModuleInstaller::VerifyManifestSignature(std::string_view manifest, std::string_view sig_hex, const std::vector<std::string>& keys)
{
    const auto sig{TryParseHex<unsigned char>(TrimString(sig_hex))};
    if (!sig || sig->size() != 64) return false;
    HashWriter hasher{TaggedHash(std::string{SIGNATURE_TAG})};
    hasher.write(std::as_bytes(std::span{manifest.data(), manifest.size()}));
    const uint256 hash{hasher.GetSHA256()};
    for (const std::string& key_hex : keys) {
        const auto key{TryParseHex<unsigned char>(key_hex)};
        if (!key || key->size() != 32) continue;
        const XOnlyPubKey key_x{std::span<const unsigned char>{*key}};
        if (!key_x.IsFullyValid()) continue;
        if (key_x.VerifySchnorr(hash, *sig)) return true;
    }
    return false;
}

// ---- The download of one address ----

#ifndef WIN32
ModuleInstaller::Fetcher ModuleInstaller::CurlFetcher(std::optional<fs::path> cacert)
{
    return [cacert](const FetchRequest& req) -> FetchResult {
        FetchResult result;
        if (!req.url.starts_with("https://")) {
            result.error = "Only https:// addresses are downloaded.";
            return result;
        }
        std::vector<std::string> argv{"curl", "--fail", "--location", "--silent", "--show-error", "--proto", "=https", "--proto-redir", "=https", "--max-redirs", "5",
                                      "--max-filesize", strprintf("%d", req.max_bytes), "--connect-timeout", "30", "--speed-limit", "1024", "--speed-time", "60",
                                      "--max-time", "3600", "--user-agent", "sidechain-modules-downloader"};
        if (cacert) {
            argv.push_back("--cacert");
            argv.push_back(fs::PathToString(*cacert));
        }
        argv.push_back("--output");
        argv.push_back(fs::PathToString(req.out));
        argv.push_back("--");
        argv.push_back(req.url);
        std::vector<char*> args;
        for (auto& a : argv) args.push_back(a.data());
        args.push_back(nullptr);
        int err_pipe[2];
        if (pipe2(err_pipe, O_CLOEXEC) != 0) {
            result.error = "The download could not be started.";
            return result;
        }
        const pid_t pid{fork()};
        if (pid == 0) {
            const int devnull{open("/dev/null", O_RDWR)};
            if (devnull >= 0) {
                dup2(devnull, 0);
                dup2(devnull, 1);
            }
            dup2(err_pipe[1], 2);
            const long limit{sysconf(_SC_OPEN_MAX)};
            for (int fd{3}; fd < (limit > 0 ? limit : 4096); ++fd) close(fd);
            execvp(args[0], args.data());
            _exit(127);
        }
        close(err_pipe[1]);
        if (pid < 0) {
            close(err_pipe[0]);
            result.error = "The download could not be started.";
            return result;
        }
        std::string err;
        bool killed{false};
        int status{0};
        bool exited{false};
        pollfd fd{err_pipe[0], POLLIN, 0};
        while (!exited) {
            const int ready{poll(&fd, fd.fd >= 0 ? 1 : 0, 200)};
            if (ready > 0 && fd.fd >= 0) {
                char buffer[4096];
                const ssize_t n{read(fd.fd, buffer, sizeof(buffer))};
                if (n > 0) {
                    if (err.size() < 4096) err.append(buffer, n);
                } else if (n == 0 || errno != EINTR) {
                    close(fd.fd);
                    fd.fd = -1;
                }
            } else if (fd.fd < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds{200});
            }
            std::error_code ec;
            const uint64_t size{std::filesystem::exists(req.out, ec) ? static_cast<uint64_t>(std::filesystem::file_size(req.out, ec)) : 0};
            if (req.progress) req.progress(size, req.expected);
            // Only the program started here is stopped: by its own pid.
            if (!killed && size > req.max_bytes) {
                kill(pid, SIGKILL);
                killed = true;
                result.too_large = true;
            }
            if (!killed && req.cancelled && req.cancelled()) {
                kill(pid, SIGKILL);
                killed = true;
                result.cancelled = true;
            }
            const pid_t done{waitpid(pid, &status, WNOHANG)};
            if (done == pid) exited = true;
            else if (done < 0 && errno != EINTR) exited = true;
        }
        if (fd.fd >= 0) close(fd.fd);
        const auto failed{[&](std::string error) {
            RemoveAll(req.out);
            result.ok = false;
            result.error = std::move(error);
            return result;
        }};
        if (result.cancelled) return failed("Cancelled");
        if (result.too_large) return failed("The file is larger than allowed.");
        if (!WIFEXITED(status)) return failed("The download was stopped.");
        const int code{WEXITSTATUS(status)};
        if (code == 63) {
            result.too_large = true;
            return failed("The file is larger than allowed.");
        }
        if (code == 127 && err.empty()) return failed("The download needs the program curl, which could not be started.");
        if (code != 0) return failed(err.empty() ? strprintf("The download failed (curl %d).", code) : std::string{TrimString(err)}.substr(0, 300));
        std::error_code ec;
        if (std::filesystem::file_size(req.out, ec) > req.max_bytes || ec) {
            result.too_large = !ec;
            return failed(ec ? "The download left no file." : "The file is larger than allowed.");
        }
        result.ok = true;
        return result;
    };
}
#else
ModuleInstaller::Fetcher ModuleInstaller::CurlFetcher(std::optional<fs::path> cacert)
{
    // TODO(windows port): progress and cancel (curl.exe of Windows 10 and later, run to its end).
    return [cacert](const FetchRequest& req) -> FetchResult {
        FetchResult result;
        if (!req.url.starts_with("https://")) {
            result.error = "Only https:// addresses are downloaded.";
            return result;
        }
        std::vector<std::string> argv{"curl", "--fail", "--location", "--silent", "--show-error", "--proto", "=https", "--proto-redir", "=https", "--max-redirs", "5",
                                      "--max-filesize", strprintf("%d", req.max_bytes), "--connect-timeout", "30", "--max-time", "3600"};
        if (cacert) {
            argv.push_back("--cacert");
            argv.push_back(fs::PathToString(*cacert));
        }
        argv.insert(argv.end(), {"--output", fs::PathToString(req.out), "--", req.url});
        const SidechainNodes::Output out{SidechainNodes::Run(argv, 3600)};
        std::error_code ec;
        if (out.code != 0 || std::filesystem::file_size(req.out, ec) > req.max_bytes || ec) {
            RemoveAll(req.out);
            result.too_large = out.code == 63;
            result.error = out.err.empty() ? "The download failed." : std::string{TrimString(out.err)}.substr(0, 300);
            return result;
        }
        result.ok = true;
        return result;
    };
}
#endif

// ---- The installer ----

ModuleInstaller::ModuleInstaller(Options options, NotifyFn notify) : m_options(std::move(options)), m_notify(std::move(notify))
{
    if (!m_options.fetcher) throw Error("ModuleInstaller needs a fetcher");
}

ModuleInstaller::~ModuleInstaller()
{
    std::vector<std::thread> threads;
    {
        LOCK(m_mutex);
        for (auto& [stem, job] : m_jobs) {
            if (job.cancel) *job.cancel = true;
            if (job.thread.joinable()) threads.push_back(std::move(job.thread));
        }
    }
    for (auto& t : threads) t.join();
}

fs::path ModuleInstaller::StemDir(const std::string& stem) const
{
    if (!IsAllowed(stem)) throw Error("No such sidechain");
    return m_options.bin_root / fs::PathFromString(stem);
}

std::string ModuleInstaller::ReadPointer(const std::string& stem, const std::string& name) const
{
    const auto text{ReadFile(StemDir(stem) / fs::PathFromString(name), 256)};
    if (!text) return {};
    const std::string tag{TrimString(*text)};
    return IsValidTag(tag) ? tag : std::string{};
}

void ModuleInstaller::WritePointer(const std::string& stem, const std::string& name, const std::string& value) const
{
    const fs::path path{StemDir(stem) / fs::PathFromString(name)};
    if (value.empty()) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return;
    }
    WriteAtomic(path, value + "\n");
}

std::optional<std::string> ModuleInstaller::Current(const std::string& stem) const
{
    const std::string tag{ReadPointer(stem, "current")};
    if (tag.empty()) return std::nullopt;
    return tag;
}

std::optional<std::string> ModuleInstaller::Previous(const std::string& stem) const
{
    const std::string tag{ReadPointer(stem, "previous")};
    if (tag.empty()) return std::nullopt;
    std::error_code ec;
    if (!std::filesystem::is_directory(StemDir(stem) / fs::PathFromString(tag), ec)) return std::nullopt;
    return tag;
}

std::vector<std::string> ModuleInstaller::Installed(const std::string& stem) const
{
    std::vector<std::string> out;
    std::error_code ec;
    const fs::path dir{StemDir(stem)};
    const std::string daemon{AllowedBinaries(stem, m_options.platform)[0]};
    for (auto it{std::filesystem::directory_iterator(dir, ec)}; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        const std::string name{fs::PathToString(fs::path{it->path().filename()})};
        if (!IsValidTag(name)) continue;
        std::error_code ec2;
        if (std::filesystem::is_regular_file(it->path() / daemon, ec2)) out.push_back(name);
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return CompareTags(a, b) > 0; });
    return out;
}

std::optional<fs::path> ModuleInstaller::CurrentDir(const std::string& stem) const
{
    if (!IsAllowed(stem)) return std::nullopt;
    const auto tag{Current(stem)};
    if (!tag) return std::nullopt;
    const fs::path dir{StemDir(stem) / fs::PathFromString(*tag)};
    std::error_code ec;
    if (!std::filesystem::is_regular_file(dir / fs::PathFromString(AllowedBinaries(stem, m_options.platform)[0]), ec)) return std::nullopt;
    return dir;
}

void ModuleInstaller::Prune(const std::string& stem, const std::string& keep) const
{
    // Kept: the current version and the one before it (rollback). Older ones go.
    const std::string current{ReadPointer(stem, "current")};
    const std::string previous{ReadPointer(stem, "previous")};
    std::error_code ec;
    for (auto it{std::filesystem::directory_iterator(StemDir(stem), ec)}; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        const std::string name{fs::PathToString(fs::path{it->path().filename()})};
        if (!IsValidTag(name) || name == current || name == previous || name == keep) continue;
        std::error_code ec2;
        if (std::filesystem::is_directory(it->path(), ec2)) RemoveAll(it->path());
    }
}

std::string ModuleInstaller::FetchSmall(const std::string& url, uint64_t max, const fs::path& tmp, const std::function<bool()>& cancelled) const
{
    FetchRequest req;
    req.url = url;
    req.max_bytes = max;
    req.out = tmp;
    req.cancelled = cancelled;
    const FetchResult got{m_options.fetcher(req)};
    if (got.cancelled) {
        RemoveAll(tmp);
        throw Cancelled{};
    }
    if (got.too_large) {
        RemoveAll(tmp);
        throw Error(strprintf("%s is larger than allowed (%s at most): refused.", url, HumanSize(max)));
    }
    if (!got.ok) {
        RemoveAll(tmp);
        throw Error(strprintf("%s could not be downloaded: %s", url, got.error));
    }
    const auto text{ReadFile(tmp, max)};
    RemoveAll(tmp);
    if (!text) throw Error(strprintf("%s is larger than allowed (%s at most): refused.", url, HumanSize(max)));
    return *text;
}

void ModuleInstaller::Check(const std::string& stem, Release& r, const std::string& base, const fs::path& tmp, const std::function<bool()>& cancelled) const
{
    const Repo& repo{*FindRepo(stem)};
    const std::string& prefix{repo.prefix};
    const auto url{[&](const std::string& name) { return strprintf("%s/%s/%s/releases/download/%s/%s", base, repo.owner, repo.repo, r.tag, name); }};
    const std::string platform{m_options.platform};
    if (platform.empty()) {
        r.reason = "There are no packages of the sidechains for this kind of computer.";
        return;
    }
    if (r.asset_name.empty()) {
        r.reason = strprintf("No package for this computer (%s) in this release.", platform);
        return;
    }
    if (r.asset_size > MAX_ARCHIVE_BYTES) {
        r.reason = strprintf("Its package is too large (%s; %s at most).", HumanSize(r.asset_size), HumanSize(MAX_ARCHIVE_BYTES));
        return;
    }
    if (!r.manifest) {
        r.reason = strprintf("Made before this app could run it: the release has no manifest-%s.json, which says the version of the host its program speaks (this app needs %d).", prefix, HOST_API);
        return;
    }
    if (!r.sums) {
        r.reason = strprintf("The release has no SHA256SUMS-%s.txt: its package cannot be checked.", prefix);
        return;
    }
    const std::string manifest_text{FetchSmall(url(strprintf("manifest-%s.json", prefix)), MAX_MANIFEST_BYTES, tmp, cancelled)};
    UniValue manifest;
    if (!manifest.read(manifest_text) || !manifest.isObject()) {
        r.reason = "Its manifest cannot be read.";
        return;
    }
    const auto str{[&](const UniValue& o, const std::string& key) { return o.isObject() && o.find_value(key).isStr() ? o.find_value(key).get_str() : std::string{}; }};
    if (!manifest.find_value("format").isNum() || manifest.find_value("format").getInt<int>() != 1) {
        r.reason = "Its manifest is of a format this app does not know: update the app.";
        return;
    }
    if (str(manifest, "stem") != stem || str(manifest, "version") != r.tag) {
        r.reason = "Its manifest is of another sidechain or another release: refused.";
        return;
    }
    const UniValue& host_api{manifest.find_value("hostApi")};
    if (!host_api.isNum()) {
        r.reason = "Its manifest does not say the version of the host its program speaks.";
        return;
    }
    const int api{host_api.getInt<int>()};
    if (api != HOST_API) {
        r.reason = api > HOST_API ? strprintf("Made for a newer app (its program speaks host version %d, this app %d): update the app, or choose an older release.", api, HOST_API)
                                  : strprintf("Made for an older app (its program speaks host version %d, this app %d): choose a newer release.", api, HOST_API);
        return;
    }
    const UniValue& platforms{manifest.find_value("platforms")};
    const UniValue& entry{platforms.isObject() ? platforms.find_value(platform) : NullUniValue};
    if (!entry.isObject()) {
        r.reason = strprintf("Its manifest has no package for this computer (%s).", platform);
        return;
    }
    const std::string sha{ToLower(str(entry, "sha256"))};
    if (str(entry, "file") != r.asset_name || !IsSha256Hex(sha) || !entry.find_value("size").isNum() || entry.find_value("size").getInt<int64_t>() != static_cast<int64_t>(r.asset_size)) {
        r.reason = "Its manifest and the release do not agree on its package (name, size or hash): refused.";
        return;
    }
    const UniValue& bins{entry.find_value("binaries")};
    const std::vector<std::string> allowed{AllowedBinaries(stem, platform)};
    r.binaries.clear();
    if (bins.isArray()) {
        for (size_t i{0}; i < bins.size(); ++i) {
            if (!bins[i].isStr() || std::ranges::find(allowed, bins[i].get_str()) == allowed.end() || std::ranges::find(r.binaries, bins[i].get_str()) != r.binaries.end()) {
                r.reason = "Its manifest names programs this app does not install: refused.";
                r.binaries.clear();
                return;
            }
            r.binaries.push_back(bins[i].get_str());
        }
    }
    if (std::ranges::find(r.binaries, allowed[0]) == r.binaries.end()) {
        r.reason = strprintf("Its package has no %s.", allowed[0]);
        return;
    }
    const std::string sums{FetchSmall(url(strprintf("SHA256SUMS-%s.txt", prefix)), MAX_SUMS_BYTES, tmp, cancelled)};
    std::string summed;
    for (const auto& raw : util::SplitString(sums, '\n')) {
        const std::string line{TrimString(raw)};
        const size_t space{line.find(' ')};
        if (space != 64) continue;
        std::string name{TrimString(line.substr(space))};
        if (!name.empty() && name[0] == '*') name.erase(0, 1);
        if (name == r.asset_name) summed = ToLower(line.substr(0, 64));
    }
    if (summed.empty()) {
        r.reason = strprintf("SHA256SUMS-%s.txt has no line for %s.", prefix, r.asset_name);
        return;
    }
    if (summed != sha) {
        r.reason = strprintf("SHA256SUMS-%s.txt and the manifest give other hashes for %s: refused.", prefix, r.asset_name);
        return;
    }
    r.sha256 = sha;
    std::vector<std::string> keys{ReleaseKeys()};
    keys.insert(keys.end(), m_options.extra_keys.begin(), m_options.extra_keys.end());
    if (r.signature) {
        const std::string sig{FetchSmall(url(strprintf("manifest-%s.json.sig", prefix)), MAX_SIG_BYTES, tmp, cancelled)};
        if (!keys.empty()) {
            if (!VerifyManifestSignature(manifest_text, sig, keys)) {
                r.reason = "Its signature does not verify with the release key of this app: refused.";
                return;
            }
            r.signed_ok = true;
        }
    }
    if (m_options.signatures == "require" && !r.signed_ok) {
        r.reason = keys.empty() ? std::string{"Signed releases are required, and this app has no release key to check them yet."}
                                : std::string{"Not signed with the release key of this app (signed releases are required)."};
        return;
    }
    r.compatible = true;
}

std::vector<ModuleInstaller::Release> ModuleInstaller::List(const std::string& stem, const std::function<bool()>& cancelled) const
{
    const fs::path dir{StemDir(stem)};
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const fs::path tmp{dir / fs::PathFromString(".fetch-" + Random8())};
    // Pre-releases too: the releases of the test network are pre-releases (GitHub's "latest" leaves them out).
    const Repo& repo{*FindRepo(stem)};
    const std::string api{FetchSmall(strprintf("%s/repos/%s/%s/releases?per_page=10", m_options.api_base, repo.owner, repo.repo), MAX_API_BYTES, tmp, cancelled)};
    UniValue list;
    if (!list.read(api) || !list.isArray()) throw Error("The list of the releases cannot be read.");
    std::vector<Release> out;
    for (size_t i{0}; i < list.size() && out.size() < 10; ++i) {
        const UniValue& rel{list[i]};
        if (!rel.isObject() || rel.find_value("draft").isTrue()) continue;
        Release r;
        r.tag = rel.find_value("tag_name").isStr() ? rel.find_value("tag_name").get_str() : "";
        if (!IsValidTag(r.tag)) continue;
        r.name = rel.find_value("name").isStr() ? rel.find_value("name").get_str().substr(0, 120) : r.tag;
        r.prerelease = rel.find_value("prerelease").isTrue();
        r.published = rel.find_value("published_at").isStr() ? rel.find_value("published_at").get_str().substr(0, 40) : "";
        const std::string archive{ArchiveName(stem, r.tag, m_options.platform)};
        const UniValue& assets{rel.find_value("assets")};
        for (size_t a{0}; assets.isArray() && a < assets.size(); ++a) {
            const UniValue& asset{assets[a]};
            if (!asset.isObject() || !asset.find_value("name").isStr()) continue;
            const std::string name{asset.find_value("name").get_str()};
            if (!IsValidAssetName(name)) continue;
            if (name == archive && !m_options.platform.empty()) {
                r.asset_name = name;
                r.asset_size = asset.find_value("size").isNum() ? asset.find_value("size").getInt<int64_t>() : 0;
            }
            if (name == strprintf("SHA256SUMS-%s.txt", repo.prefix)) r.sums = true;
            if (name == strprintf("manifest-%s.json", repo.prefix)) r.manifest = true;
            if (name == strprintf("manifest-%s.json.sig", repo.prefix)) r.signature = true;
        }
        try {
            Check(stem, r, m_options.download_base, tmp, cancelled);
        } catch (const Cancelled&) {
            throw;
        } catch (const std::exception& e) {
            r.compatible = false;
            r.reason = e.what();
        }
        out.push_back(std::move(r));
    }
    std::sort(out.begin(), out.end(), [](const Release& a, const Release& b) { return CompareTags(a.tag, b.tag) > 0; });
    return out;
}

std::vector<ModuleInstaller::Release> ModuleInstaller::Releases(const std::string& stem, bool force)
{
    if (!IsAllowed(stem)) throw Error("No such sidechain");
    {
        LOCK(m_mutex);
        const auto it{m_cache.find(stem)};
        if (!force && it != m_cache.end() && std::chrono::steady_clock::now() - it->second.at < m_options.cache_for) return it->second.releases;
    }
    std::vector<Release> releases{List(stem, {})};
    LOCK(m_mutex);
    m_cache[stem] = Cached{std::chrono::steady_clock::now(), releases};
    return releases;
}

std::optional<std::string> ModuleInstaller::UpdateFor(const std::string& stem, bool force)
{
    const auto current{Current(stem)};
    if (!current) return std::nullopt;
    for (const Release& r : Releases(stem, force)) {
        if (r.compatible) return CompareTags(r.tag, *current) > 0 ? std::optional{r.tag} : std::nullopt;
    }
    return std::nullopt;
}

UniValue ModuleInstaller::ReleasesJson(const std::string& stem, const std::string& source, bool force)
{
    const std::vector<Release> releases{Releases(stem, force)};
    UniValue out{UniValue::VOBJ};
    out.pushKV("repo", strprintf("%s/%s", FindRepo(stem)->owner, FindRepo(stem)->repo));
    out.pushKV("platform", m_options.platform);
    out.pushKV("hostApi", HOST_API);
    out.pushKV("signatures", m_options.signatures);
    out.pushKV("keys", static_cast<int64_t>(ReleaseKeys().size() + m_options.extra_keys.size()));
    const auto current{Current(stem)};
    const auto previous{Previous(stem)};
    out.pushKV("current", current ? UniValue{*current} : UniValue{});
    out.pushKV("previous", previous ? UniValue{*previous} : UniValue{});
    out.pushKV("source", source);
    UniValue installed{UniValue::VARR};
    for (const auto& tag : Installed(stem)) installed.push_back(tag);
    out.pushKV("installed", std::move(installed));
    UniValue list{UniValue::VARR};
    std::optional<std::string> newest;
    for (const Release& r : releases) {
        UniValue e{UniValue::VOBJ};
        e.pushKV("tag", r.tag);
        e.pushKV("name", r.name);
        e.pushKV("prerelease", r.prerelease);
        e.pushKV("published", r.published);
        if (r.asset_name.empty()) {
            e.pushKV("asset", UniValue{});
        } else {
            UniValue asset{UniValue::VOBJ};
            asset.pushKV("name", r.asset_name);
            asset.pushKV("size", static_cast<int64_t>(r.asset_size));
            e.pushKV("asset", std::move(asset));
        }
        e.pushKV("sums", r.sums);
        e.pushKV("manifest", r.manifest);
        e.pushKV("signed", r.signed_ok);
        e.pushKV("compatible", r.compatible);
        e.pushKV("reason", r.reason);
        if (r.compatible && !newest) newest = r.tag;
        list.push_back(std::move(e));
    }
    out.pushKV("releases", std::move(list));
    out.pushKV("newestCompatible", newest ? UniValue{*newest} : UniValue{});
    out.pushKV("updateAvailable", newest && current && CompareTags(*newest, *current) > 0);
    return out;
}

void ModuleInstaller::SetJob(const std::string& stem, const std::function<void(Job&)>& change, bool force_notify)
{
    bool tell{false};
    {
        LOCK(m_mutex);
        Job& job{m_jobs[stem]};
        const std::string before{job.state};
        change(job);
        const auto now{std::chrono::steady_clock::now()};
        auto& last{m_last_notify[stem]};
        // At most about 4 a second, but every change of state.
        if (force_notify || job.state != before || now - last >= std::chrono::milliseconds{250}) {
            last = now;
            tell = true;
        }
    }
    m_cv.notify_all();
    if (tell && m_notify) m_notify(stem);
}

void ModuleInstaller::Install(const std::string& stem, const std::string& tag, bool activate, const std::function<bool()>& cancelled)
{
    if (!IsAllowed(stem)) throw Error("No such sidechain");
    if (!IsValidTag(tag)) throw Error("No such release");
    const fs::path dir{StemDir(stem)};
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) throw Error(strprintf("Cannot make the folder %s: %s", fs::PathToString(dir), ec.message()));
    // What an earlier run left half done (the app stopped in a download).
    for (auto it{std::filesystem::directory_iterator(dir, ec)}; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        const std::string name{fs::PathToString(fs::path{it->path().filename()})};
        if (name.starts_with(".staging-") || name.starts_with(".download-") || name.starts_with(".old-") || name.starts_with(".fetch-")) RemoveAll(it->path());
    }

    SetJob(stem, [&](Job& j) { j.state = "listing"; });
    const std::vector<Release> releases{List(stem, cancelled)};
    {
        LOCK(m_mutex);
        m_cache[stem] = Cached{std::chrono::steady_clock::now(), releases};
    }
    const auto found{std::ranges::find_if(releases, [&](const Release& r) { return r.tag == tag; })};
    if (found == releases.end()) throw Error(strprintf("There is no release %s of %s.", tag, stem));
    const Release r{*found};
    if (!r.compatible) throw Error(strprintf("Release %s cannot be used by this app: %s", tag, r.reason));

    const fs::path part{dir / fs::PathFromString(".download-" + tag + ".part")};
    const fs::path staging{dir / fs::PathFromString(".staging-" + tag + "-" + Random8())};
    // Whatever happens, nothing half done stays.
    struct Cleanup {
        fs::path a, b;
        ~Cleanup()
        {
            RemoveAll(a);
            RemoveAll(b);
        }
    } cleanup{part, staging};

    SetJob(stem, [&](Job& j) { j.state = "downloading"; j.done = 0; j.total = r.asset_size; });
    FetchRequest req;
    const Repo& repo{*FindRepo(stem)};
    req.url = strprintf("%s/%s/%s/releases/download/%s/%s", m_options.download_base, repo.owner, repo.repo, tag, r.asset_name);
    req.max_bytes = r.asset_size;
    req.expected = r.asset_size;
    req.out = part;
    req.cancelled = cancelled;
    req.progress = [&](uint64_t done, uint64_t) { SetJob(stem, [&](Job& j) { j.done = done; }); };
    const FetchResult got{m_options.fetcher(req)};
    if (got.cancelled || (cancelled && cancelled())) throw Cancelled{};
    if (got.too_large) throw Error(strprintf("The package is larger than its release says (%s): refused, nothing installed.", HumanSize(r.asset_size)));
    if (!got.ok) throw Error(strprintf("The download failed: %s. Nothing was installed; try again.", got.error));

    SetJob(stem, [&](Job& j) { j.state = "verifying"; });
    const uint64_t size{static_cast<uint64_t>(std::filesystem::file_size(part, ec))};
    if (ec || size != r.asset_size) throw Error(strprintf("The download is incomplete (%s of %s). Nothing was installed; try again.", HumanSize(ec ? 0 : size), HumanSize(r.asset_size)));
    const std::string hash{FileSha256(part)};
    if (hash != r.sha256) throw Error(strprintf("The package does not match its SHA-256 (expected %s, got %s): it was changed or damaged on its way. Nothing was installed.", r.sha256, hash));
    if (cancelled && cancelled()) throw Cancelled{};

    SetJob(stem, [&](Job& j) { j.state = "unpacking"; });
    const bool zip{m_options.platform == "win64"};
    const std::string archive{fs::PathToString(part)};
    const SidechainNodes::Output listing{SidechainNodes::Run({"tar", zip ? "-tf" : "-tzf", archive}, 300)};
    if (listing.code != 0) throw Error(listing.code == -1 ? "Unpacking needs the program tar, which could not be started." : "The package cannot be read as an archive.");
    std::map<std::string, std::string> members;
    for (const auto& raw : util::SplitString(listing.out, '\n')) {
        std::string name{TrimString(raw, "\r")};
        if (name.empty()) continue;
        if (UnsafeMember(name)) throw Error(strprintf("The package holds a file that would go outside its folder (%s): refused.", name.substr(0, 120)));
        std::string trimmed{name};
        while (trimmed.ends_with('/')) trimmed.pop_back();
        const auto parts{util::SplitString(trimmed, '/')};
        if (parts.empty() || parts.size() > 2) continue;
        const std::string base{parts.back()};
        if (std::ranges::find(r.binaries, base) == r.binaries.end()) continue;
        if (members.contains(base)) throw Error(strprintf("The package holds %s twice: refused.", base));
        members[base] = name;
    }
    for (const auto& b : r.binaries) {
        if (!members.contains(b)) throw Error(strprintf("The package has no %s.", b));
    }
    const fs::path unpack{staging / ".unpack"};
    std::filesystem::create_directories(unpack, ec);
    if (ec) throw Error(strprintf("Cannot make the folder %s: %s", fs::PathToString(unpack), ec.message()));
    std::vector<std::string> extract{"tar", zip ? "-xf" : "-xzf", archive, "-C", fs::PathToString(unpack), "--no-same-owner", "--"};
    for (const auto& [base, name] : members) extract.push_back(name);
    const SidechainNodes::Output unpacked{SidechainNodes::Run(extract, 600)};
    if (unpacked.code != 0) throw Error(strprintf("The package could not be unpacked: %s", TrimString(unpacked.err).substr(0, 200)));
    for (const auto& [base, name] : members) {
        const fs::path from{unpack / fs::PathFromString(name)};
        const auto status{std::filesystem::symlink_status(from, ec)};
        if (ec || !std::filesystem::is_regular_file(status)) throw Error(strprintf("%s in the package is not a plain file (a link?): refused.", base));
        const fs::path to{staging / fs::PathFromString(base)};
        std::filesystem::rename(from, to, ec);
        if (ec) throw Error(strprintf("Cannot install %s: %s", base, ec.message()));
        std::filesystem::permissions(to, std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::group_exec | std::filesystem::perms::others_read | std::filesystem::perms::others_exec, std::filesystem::perm_options::replace, ec);
    }
    RemoveAll(unpack);
    UniValue info{UniValue::VOBJ};
    info.pushKV("stem", stem);
    info.pushKV("tag", tag);
    info.pushKV("file", r.asset_name);
    info.pushKV("sha256", r.sha256);
    info.pushKV("signed", r.signed_ok);
    info.pushKV("installed", FormatISO8601DateTime(GetTime()));
    WriteAtomic(staging / "release.json", info.write(2) + "\n");
    if (cancelled && cancelled()) throw Cancelled{};

    SetJob(stem, [&](Job& j) { j.state = "installing"; });
    {
        LOCK(m_install_mutex);
        const fs::path final_dir{dir / fs::PathFromString(tag)};
        const fs::path old{dir / fs::PathFromString(".old-" + tag + "-" + Random8())};
        bool had{std::filesystem::exists(final_dir, ec)};
        if (had) {
            std::filesystem::rename(final_dir, old, ec);
            if (ec) throw Error(strprintf("Cannot replace %s: %s", fs::PathToString(final_dir), ec.message()));
        }
        // One rename: the version is there whole, or not at all.
        std::filesystem::rename(staging, final_dir, ec);
        if (ec) {
            if (had) {
                std::error_code ec2;
                std::filesystem::rename(old, final_dir, ec2);
            }
            throw Error(strprintf("Cannot install into %s: %s", fs::PathToString(final_dir), ec.message()));
        }
        RemoveAll(old);
    }
    if (activate) Activate(stem, tag);
    else Prune(stem, tag);
}

void ModuleInstaller::InstallNow(const std::string& stem, const std::string& tag, bool activate)
{
    Install(stem, tag, activate, {});
}

void ModuleInstaller::Activate(const std::string& stem, const std::string& tag)
{
    LOCK(m_install_mutex);
    if (!IsValidTag(tag)) throw Error("No such release");
    const fs::path dir{StemDir(stem) / fs::PathFromString(tag)};
    std::error_code ec;
    if (!std::filesystem::is_regular_file(dir / fs::PathFromString(AllowedBinaries(stem, m_options.platform)[0]), ec)) throw Error(strprintf("Release %s of %s is not installed.", tag, stem));
    const std::string current{ReadPointer(stem, "current")};
    if (current != tag) {
        // previous first: a stop between the two writes leaves the old version current, and the new one installed.
        WritePointer(stem, "previous", current);
        WritePointer(stem, "current", tag);
    }
    Prune(stem);
}

void ModuleInstaller::Rollback(const std::string& stem)
{
    const auto previous{Previous(stem)};
    if (!previous) throw Error("There is no earlier version to go back to.");
    Activate(stem, *previous);
}

void ModuleInstaller::Start(const std::string& stem, const std::string& tag, bool activate, std::function<void(bool ok)> done)
{
    if (!IsAllowed(stem)) throw Error("No such sidechain");
    if (!IsValidTag(tag)) throw Error("No such release");
    std::thread old;
    auto cancel{std::make_shared<std::atomic<bool>>(false)};
    {
        LOCK(m_mutex);
        Job& job{m_jobs[stem]};
        if (job.running) throw Error("A download of this sidechain is already running.");
        old = std::move(job.thread);
        job.running = true;
        job.state = "listing";
        job.tag = tag;
        job.done = 0;
        job.total = 0;
        job.error.clear();
        job.cancel = cancel;
    }
    if (old.joinable()) old.join();
    std::thread thread{[this, stem, tag, activate, cancel, done = std::move(done)] {
        util::ThreadRename("webui-module-" + stem);
        bool ok{false};
        std::string error;
        std::string state;
        try {
            Install(stem, tag, activate, [cancel] { return cancel->load(); });
            ok = true;
            state = "done";
        } catch (const Cancelled&) {
            state = "cancelled";
        } catch (const std::exception& e) {
            state = "failed";
            error = e.what();
            LogWarning("webui: the download of %s %s failed: %s", stem, tag, error);
        }
        SetJob(stem, [&](Job& j) {
            j.state = state;
            j.error = error;
        }, true);
        if (done) {
            try {
                done(ok);
            } catch (const std::exception& e) {
                SetJob(stem, [&](Job& j) {
                    j.state = "failed";
                    j.error = e.what();
                }, true);
            }
        }
        SetJob(stem, [&](Job& j) { j.running = false; }, true);
    }};
    LOCK(m_mutex);
    m_jobs[stem].thread = std::move(thread);
}

void ModuleInstaller::Cancel(const std::string& stem)
{
    LOCK(m_mutex);
    const auto it{m_jobs.find(stem)};
    if (it != m_jobs.end() && it->second.cancel) *it->second.cancel = true;
}

UniValue ModuleInstaller::Progress(const std::string& stem) const
{
    LOCK(m_mutex);
    UniValue out{UniValue::VOBJ};
    const auto it{m_jobs.find(stem)};
    if (it == m_jobs.end()) {
        out.pushKV("state", "idle");
        out.pushKV("tag", "");
        out.pushKV("done", 0);
        out.pushKV("total", 0);
        out.pushKV("error", "");
        out.pushKV("running", false);
        return out;
    }
    const Job& j{it->second};
    out.pushKV("state", j.state);
    out.pushKV("tag", j.tag);
    out.pushKV("done", static_cast<int64_t>(j.done));
    out.pushKV("total", static_cast<int64_t>(j.total));
    out.pushKV("error", j.error);
    out.pushKV("running", j.running);
    return out;
}

bool ModuleInstaller::Busy(const std::string& stem) const
{
    LOCK(m_mutex);
    const auto it{m_jobs.find(stem)};
    return it != m_jobs.end() && it->second.running;
}

bool ModuleInstaller::Wait(const std::string& stem, std::chrono::milliseconds timeout) const
{
    WAIT_LOCK(m_mutex, lock);
    return m_cv.wait_for(lock, timeout, [&]() EXCLUSIVE_LOCKS_REQUIRED(m_mutex) {
        const auto it{m_jobs.find(stem)};
        return it == m_jobs.end() || !it->second.running;
    });
}

} // namespace webui
