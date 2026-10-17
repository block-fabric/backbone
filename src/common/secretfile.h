// Copyright (c) 2026 The Chains developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_COMMON_SECRETFILE_H
#define BITCOIN_COMMON_SECRETFILE_H

#include <util/fs.h>
#include <util/result.h>

#include <string_view>

namespace common {

/**
 * Write a secret (a password another program reads) to a file only its owner can read, replacing the file
 * atomically: the content goes to a new file next to it, created exclusively (O_CREAT|O_EXCL) with mode 0600
 * on Unix, which is then renamed over it. Whatever was there before, whatever its permissions, is gone and
 * no reader ever sees half a file. On Windows the new file gets a protected access list of its own that
 * lets only the user running this at it (nothing inherited from its directory). Errors never contain the secret.
 */
util::Result<void> WriteSecretFile(const fs::path& path, std::string_view secret);

} // namespace common

#endif // BITCOIN_COMMON_SECRETFILE_H
