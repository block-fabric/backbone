// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <common/secretfile.h>

#include <random.h>
#include <tinyformat.h>
#include <util/fs_helpers.h>
#include <util/strencodings.h>
#include <util/translation.h>

#include <cstdio>

#ifdef WIN32
#include <algorithm>
#include <string>
#include <vector>

#include <windows.h>
#include <sddl.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace common {

util::Result<void> WriteSecretFile(const fs::path& path, std::string_view secret)
{
    const std::string name{fs::PathToString(path)};
    // Next to it (same folder, so that the rename is atomic), with a name no one can guess.
    const fs::path temp{fs::PathFromString(fs::PathToString(path) + ".new-" + HexStr(GetRandHash()).substr(0, 16))};
#ifndef WIN32
    const int fd{open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR)};
    if (fd < 0) return util::Error{Untranslated(strprintf("Cannot create %s", fs::PathToString(temp)))};
    // The umask can only take bits away; make sure no one else was left any.
    bool ok{fchmod(fd, S_IRUSR | S_IWUSR) == 0};
    size_t written{0};
    while (ok && written < secret.size()) {
        const ssize_t n{write(fd, secret.data() + written, secret.size() - written)};
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) ok = false;
        else written += n;
    }
    ok = ok && fsync(fd) == 0;
    ok = (close(fd) == 0) && ok;
#else
    // An access list of its own, protected (nothing inherited from the folder, whose list may let other
    // users read: a data directory on the root of a drive, say), that lets the user running this, and
    // nobody else, at the file. The rename below keeps it.
    std::wstring sddl;
    HANDLE token{nullptr};
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        DWORD size{0};
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::vector<unsigned char> user(size);
        LPWSTR sid{nullptr};
        if (size > 0 && GetTokenInformation(token, TokenUser, user.data(), size, &size) &&
            ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid, &sid)) {
            sddl = L"D:P(A;;FA;;;" + std::wstring{sid} + L")";
            LocalFree(sid);
        }
        CloseHandle(token);
    }
    PSECURITY_DESCRIPTOR descriptor{nullptr};
    if (sddl.empty() || !ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
        return util::Error{Untranslated(strprintf("Cannot make an access list for %s", name))};
    }
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;
    const HANDLE file{CreateFileW(temp.std_path().c_str(), GENERIC_WRITE, /*dwShareMode=*/0, &attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    LocalFree(descriptor);
    if (file == INVALID_HANDLE_VALUE) return util::Error{Untranslated(strprintf("Cannot create %s", fs::PathToString(temp)))};
    bool ok{true};
    size_t written{0};
    while (ok && written < secret.size()) {
        DWORD n{0};
        const DWORD chunk{static_cast<DWORD>(std::min<size_t>(secret.size() - written, 1 << 20))};
        if (!WriteFile(file, secret.data() + written, chunk, &n, nullptr) || n == 0) {
            ok = false;
        } else {
            written += n;
        }
    }
    ok = FlushFileBuffers(file) && ok;
    ok = CloseHandle(file) && ok;
#endif
    if (!ok) {
        std::error_code ec;
        fs::remove(temp, ec);
        return util::Error{Untranslated(strprintf("Cannot write %s", name))};
    }
    if (!RenameOver(temp, path)) {
        std::error_code ec;
        fs::remove(temp, ec);
        return util::Error{Untranslated(strprintf("Cannot replace %s", name))};
    }
    return {};
}

} // namespace common
