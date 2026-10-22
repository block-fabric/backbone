#!/usr/bin/env python3
# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""A reference of the single seed of the unified app (doc/unified-app.md §23), apart from the app's own code.

BIP39 (the seed of a phrase: PBKDF2-HMAC-SHA512, 2048 rounds, salt "mnemonic" + the extra word), BIP32 (hardened
derivation, the extended keys in base58), the account of each chain (A = 0 for the app's own chain, 1 for its mining
wallet; a sidechain's from SHA-256 of "sidechain/account/v1" 0x00 stem 0x00 genesis, never below 1024), the passphrase of
a wallet made from the seed, and BIP86's output key of an address. Pure Python, no package: the
end-to-end runner checks with it that every chain's addresses come from the phrase the user wrote down, and the unit
tests' vectors of our paths were made with it (`seedref.py vectors`).
"""

import hashlib
import hmac
import sys

P = 2**256 - 2**32 - 977
N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
G = (0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798, 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8)
ACCOUNT_TAG = b"sidechain/account/v1"
WALLET_PASSPHRASE_LABEL = b"wallet/passphrase/v1"
# The accounts of the app's own chain are below this (0 its wallet, 1 its mining wallet): never a sidechain's.
APP_ACCOUNTS = 1024
MINING_ACCOUNT = 1
B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"
XPRV = {"main": bytes.fromhex("0488ade4"), "test": bytes.fromhex("04358394")}
XPUB = {"main": bytes.fromhex("0488b21e"), "test": bytes.fromhex("043587cf")}


def _add(a, b):
    if a is None:
        return b
    if b is None:
        return a
    if a[0] == b[0] and (a[1] + b[1]) % P == 0:
        return None
    if a == b:
        lam = 3 * a[0] * a[0] * pow(2 * a[1], P - 2, P) % P
    else:
        lam = (b[1] - a[1]) * pow(b[0] - a[0], P - 2, P) % P
    x = (lam * lam - a[0] - b[0]) % P
    return (x, (lam * (a[0] - x) - a[1]) % P)


def point(k):
    r, q = None, G
    while k:
        if k & 1:
            r = _add(r, q)
        q = _add(q, q)
        k >>= 1
    return r


def ser_p(pt):
    return bytes([2 + (pt[1] & 1)]) + pt[0].to_bytes(32, "big")


def hash160(b):
    return hashlib.new("ripemd160", hashlib.sha256(b).digest()).digest()


def b58check(payload):
    data = payload + hashlib.sha256(hashlib.sha256(payload).digest()).digest()[:4]
    n = int.from_bytes(data, "big")
    out = ""
    while n:
        n, r = divmod(n, 58)
        out = B58[r] + out
    return "1" * (len(data) - len(data.lstrip(b"\0"))) + out


def bip39_seed(mnemonic, extra=""):
    return hashlib.pbkdf2_hmac("sha512", mnemonic.encode(), ("mnemonic" + extra).encode(), 2048)


def master(seed):
    i = hmac.new(b"Bitcoin seed", seed, hashlib.sha512).digest()
    return {"key": int.from_bytes(i[:32], "big"), "chain": i[32:], "depth": 0, "parent": b"\0\0\0\0", "child": 0}


def child(k, index):
    """Hardened (index >= 2**31) or not: the private derivation of BIP32."""
    pub = ser_p(point(k["key"]))
    data = (b"\0" + k["key"].to_bytes(32, "big") if index >= 2**31 else pub) + index.to_bytes(4, "big")
    i = hmac.new(k["chain"], data, hashlib.sha512).digest()
    return {"key": (int.from_bytes(i[:32], "big") + k["key"]) % N, "chain": i[32:], "depth": k["depth"] + 1, "parent": hash160(pub)[:4], "child": index}


def path(k, *indexes):
    for i in indexes:
        k = child(k, i)
    return k


def xprv(k, net):
    return b58check(XPRV[net] + bytes([k["depth"]]) + k["parent"] + k["child"].to_bytes(4, "big") + k["chain"] + b"\0" + k["key"].to_bytes(32, "big"))


def xpub(k, net):
    return b58check(XPUB[net] + bytes([k["depth"]]) + k["parent"] + k["child"].to_bytes(4, "big") + k["chain"] + ser_p(point(k["key"])))


def fingerprint(k):
    return hash160(ser_p(point(k["key"])))[:4].hex()


def account(stem, genesis):
    """The account of a chain: 0 for the app's own; a sidechain's from its catalog id and its genesis (hex, as RPC says it)."""
    if stem is None:
        return 0
    digest = hashlib.sha256(ACCOUNT_TAG + b"\0" + stem.encode() + b"\0" + genesis.lower().encode()).digest()
    a = int.from_bytes(digest[:4], "big") & 0x7FFFFFFF
    return a + APP_ACCOUNTS if a < APP_ACCOUNTS else a


def wallet_passphrase(seed, stem, genesis):
    """The passphrase of a wallet made from the seed: a sidechain's (its stem; the user's own never reaches a sidechain's
    node), or the app's own wallet without a password (stem "", its chain's genesis)."""
    return hmac.new(seed, WALLET_PASSPHRASE_LABEL + b"\0" + stem.encode() + b"\0" + genesis.lower().encode(), hashlib.sha256).hexdigest()


def tagged(tag, data):
    t = hashlib.sha256(tag.encode()).digest()
    return hashlib.sha256(t + t + data).digest()


def taproot_output_key(k):
    """BIP86: the x-only key of the output of a key path with no script."""
    pt = point(k["key"])
    if pt[1] & 1:
        pt = (pt[0], P - pt[1])
    t = int.from_bytes(tagged("TapTweak", pt[0].to_bytes(32, "big")), "big")
    return _add(pt, point(t))[0].to_bytes(32, "big").hex()


def descriptors(seed, coin, acct, net):
    """The four descriptors a chain's wallet gets (without their checksums): tr and wpkh, receive and change."""
    m = master(seed)
    fp = fingerprint(m)
    out = []
    for purpose, script in ((86, "tr"), (84, "wpkh")):
        k = path(m, purpose + 2**31, coin + 2**31, acct + 2**31)
        origin = f"[{fp}/{purpose}h/{coin}h/{acct}h]"
        for branch in (0, 1):
            out.append(f"{script}({origin}{xprv(k, net)}/{branch}/*)")
    return out


def vectors():
    words = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"
    m = master(bip39_seed(words, "TREZOR"))
    print("TREZOR root", xprv(m, "main"))
    m = master(bip39_seed(words))
    acct = path(m, 86 + 2**31, 2**31, 2**31)
    print("bip86 account", xprv(acct, "main"), xpub(acct, "main"))
    print("bip86 first output key", taproot_output_key(path(acct, 0, 0)))
    print("fingerprint", fingerprint(m))
    regtest_genesis = "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"
    for stem in ("layers", "thunder"):
        a = account(stem, regtest_genesis)
        k = path(m, 86 + 2**31, 1 + 2**31, a + 2**31)
        print(stem, "regtest account", a, xpub(k, "test"), "first output key", taproot_output_key(path(k, 0, 0)))
    k = path(m, 86 + 2**31, 1 + 2**31, 2**31)
    print("chains test account 0", xpub(k, "test"), "first output key", taproot_output_key(path(k, 0, 0)))
    k = path(m, 86 + 2**31, 1 + 2**31, MINING_ACCOUNT + 2**31)
    print("chains test mining account 1", xpub(k, "test"), "first output key", taproot_output_key(path(k, 0, 0)))
    seed = bip39_seed(words)
    print("layers wallet passphrase", wallet_passphrase(seed, "layers", regtest_genesis))
    print("chains wallet passphrase (no password)", wallet_passphrase(seed, "", regtest_genesis))
    for stem in ("bitassets", "bitnames", "zside", "hivemind", "fastblocks", "flush"):
        print(stem, "regtest account", account(stem, regtest_genesis))


if __name__ == "__main__":
    if sys.argv[1:] == ["vectors"]:
        vectors()
