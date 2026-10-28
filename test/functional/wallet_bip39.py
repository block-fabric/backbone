#!/usr/bin/env python3
# Copyright (c) 2026 The Backbone developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Wallets of a BIP39 recovery phrase: createwalletfrommnemonic and generatemnemonic.

- The descriptors of a phrase are the standard ones (BIP84, BIP86, BIP49, BIP44, account 0, receiving and change,
  all active): the coins sent to the first BIP84 and BIP86 addresses of "abandon ... about", derived here from
  its published master key, are found by a rescan, by address type.
- A BIP39 passphrase is a different wallet; the errors of a phrase never repeat its words.
- generatemnemonic and createwalletfrommnemonic round-trip: a new phrase ("now", no rescan), its coins, and the
  wallet made again of the phrase alone finds them.
- An encrypted wallet of a phrase, and one encrypted later, which keeps the keys of its phrase; a pruned node refuses a birthday before the blocks it has, before any wallet is made.
"""
from decimal import Decimal

from test_framework.descriptors import descsum_create
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

ABANDON = " ".join(["abandon"] * 11 + ["about"])
# The master key of ABANDON without a passphrase, of the test networks (BIP49's vector).
ABANDON_TPRV = "tprv8ZgxMBicQKsPe5YMU9gHen4Ez3ApihUfykaqUorj9t6FDqy3nP6eoXiAo2ssvpAjoLroQxHqr3R5nE3a5dU3DHTjTgJDd7zrbniJr6nrCzd"


class WalletBip39Test(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [["-fallbackfee=0.0002"], ["-fastprune", "-prune=1"]]

    def setup_network(self):
        # The pruned node has a chain of its own, which it prunes.
        self.setup_nodes()

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def address(self, desc):
        return self.nodes[0].deriveaddresses(descsum_create(desc))[0]

    def funds(self, result):
        return {f["type"]: f for f in result["funds"]}

    def run_test(self):
        node = self.nodes[0]
        node.createwallet("funder")
        funder = node.get_wallet_rpc("funder")
        self.generatetoaddress(node, 101, funder.getnewaddress(), sync_fun=self.no_op)

        self.log.info("A phrase's coins on BIP84 and BIP86 addresses, found by the rescan, by type")
        bip84 = self.address(f"wpkh({ABANDON_TPRV}/84h/1h/0h/0/0)")
        bip86 = self.address(f"tr({ABANDON_TPRV}/86h/1h/0h/0/0)")
        bip84_change = self.address(f"wpkh({ABANDON_TPRV}/84h/1h/0h/1/0)")
        funder.sendtoaddress(bip84, 1.5)
        funder.sendtoaddress(bip86, 2)
        funder.sendtoaddress(bip84_change, 0.25)
        self.generate(node, 1, sync_fun=self.no_op)

        restored = node.createwalletfrommnemonic("abandon", ABANDON)
        assert_equal(restored["name"], "abandon")
        assert_equal(restored["rescanned"], True)
        funds = self.funds(restored)
        assert_equal(funds["bech32"]["bip"], 84)
        assert_equal(funds["bech32"]["path"], "m/84h/1h/0h")
        assert_equal(funds["bech32"]["transactions"], 2)
        assert_equal(funds["bech32"]["balance"], Decimal("1.75"))
        assert_equal(funds["bech32m"]["bip"], 86)
        assert_equal(funds["bech32m"]["transactions"], 1)
        assert_equal(funds["bech32m"]["received"], Decimal("2"))
        assert_equal(funds["p2sh-segwit"]["transactions"], 0)
        assert_equal(funds["legacy"]["balance"], Decimal("0"))
        wallet = node.get_wallet_rpc("abandon")
        assert_equal(wallet.getbalance(), Decimal("3.75"))
        info = wallet.getwalletinfo()
        assert_equal(info["descriptors"], True)
        assert_equal(info["private_keys_enabled"], True)
        # Eight descriptors, all active, of the master key of the phrase; new addresses are BIP84 ones.
        descs = wallet.listdescriptors(True)["descriptors"]
        assert_equal(len(descs), 8)
        assert all(d["active"] for d in descs)
        assert all(ABANDON_TPRV in d["desc"] for d in descs)
        assert_equal(sorted(d["desc"].split("(")[0] for d in descs if not d["internal"]), ["pkh", "sh", "tr", "wpkh"])
        assert_equal(wallet.getaddressinfo(wallet.getnewaddress())["hdkeypath"], "m/84h/1h/0h/0/1")
        assert wallet.getaddressinfo(bip86)["ismine"]
        # The coins can be spent.
        wallet.sendtoaddress(funder.getnewaddress(), 3)
        self.generate(node, 1, sync_fun=self.no_op)
        # Encrypted later, it keeps the keys of the phrase (not those of a new seed, as a wallet that is not blank).
        before = sorted(d["desc"] for d in wallet.listdescriptors()["descriptors"])
        wallet.encryptwallet("later")
        assert_equal(sorted(d["desc"] for d in wallet.listdescriptors()["descriptors"]), before)

        self.log.info("A BIP39 passphrase makes another wallet")
        other = node.createwalletfrommnemonic(wallet_name="trezor", mnemonic=ABANDON, bip39_passphrase="TREZOR")
        assert all(f["transactions"] == 0 for f in other["funds"])
        assert "xprv9s21ZrQH143K3h3fDYiay8mocZ3afhfULfb5GX8kCBdno77K4HiA15Tg23wpbeF1pLfs1c5SPmYHrEpTuuRhxMwvKDwqdKiGJS9XFKzUsAF" not in str(other)
        assert_equal(node.get_wallet_rpc("trezor").getbalance(), 0)

        self.log.info("The errors of a phrase name a word by its place, never the word")
        assert_raises_rpc_error(-8, "A recovery phrase has 12, 15, 18, 21 or 24 words; this one has 11.", node.createwalletfrommnemonic, "bad", " ".join(["abandon"] * 11))
        assert_raises_rpc_error(-8, "Word 3 of the recovery phrase is not one of the BIP39 English words", node.createwalletfrommnemonic, "bad", ABANDON.replace("abandon abandon abandon", "abandon abandon abandonn", 1))
        assert_raises_rpc_error(-8, "The checksum of the recovery phrase does not match", node.createwalletfrommnemonic, "bad", " ".join(["abandon"] * 12))
        assert_raises_rpc_error(-8, "not ASCII", node.createwalletfrommnemonic, "bad", ABANDON, "café")
        assert_raises_rpc_error(-3, "birthday: a timestamp, or \"now\"", node.createwalletfrommnemonic, "bad", ABANDON, "", "yesterday")
        assert_raises_rpc_error(-4, "already exists", node.createwalletfrommnemonic, "abandon", ABANDON)
        assert "bad" not in node.listwallets()
        assert "bad" not in [w["name"] for w in node.listwalletdir()["wallets"]]

        self.log.info("generatemnemonic and createwalletfrommnemonic round-trip, with an encrypted wallet")
        assert_equal(len(node.generatemnemonic(24).split(" ")), 24)
        assert_raises_rpc_error(-8, "12, 15, 18, 21 or 24", node.generatemnemonic, 13)
        phrase = node.generatemnemonic()
        assert_equal(len(phrase.split(" ")), 12)
        assert phrase != node.generatemnemonic()
        fresh = node.createwalletfrommnemonic("fresh", phrase, "", "now", "secret")
        assert_equal(fresh["rescanned"], False)
        fresh_wallet = node.get_wallet_rpc("fresh")
        assert_equal(fresh_wallet.getwalletinfo()["unlocked_until"], 0)
        first = fresh_wallet.getnewaddress()
        taproot = fresh_wallet.getnewaddress(address_type="bech32m")
        funder.sendtoaddress(first, 0.5)
        funder.sendtoaddress(taproot, 0.125)
        self.generate(node, 1, sync_fun=self.no_op)
        fresh_wallet.unloadwallet()
        again = node.createwalletfrommnemonic("again", phrase)
        again_funds = self.funds(again)
        assert_equal(again_funds["bech32"]["balance"], Decimal("0.5"))
        assert_equal(again_funds["bech32m"]["balance"], Decimal("0.125"))
        again_wallet = node.get_wallet_rpc("again")
        assert_equal(again_wallet.getbalance(), Decimal("0.625"))
        assert "unlocked_until" not in again_wallet.getwalletinfo()
        assert_equal(again_wallet.getaddressinfo(first)["hdkeypath"], "m/84h/1h/0h/0/0")
        assert_equal(again_wallet.getaddressinfo(taproot)["hdkeypath"], "m/86h/1h/0h/0/0")

        self.log.info("A pruned node refuses a birthday before the blocks it has, before any wallet is made")
        pruned = self.nodes[1]
        self.generate(pruned, 800, sync_fun=self.no_op)
        assert pruned.pruneblockchain(500) > 0
        prune_height = pruned.getblockchaininfo()["pruneheight"]
        assert_raises_rpc_error(-1, f"This node is pruned: it no longer has the blocks before height {prune_height}", pruned.createwalletfrommnemonic, "old", ABANDON)
        assert "old" not in [w["name"] for w in pruned.listwalletdir()["wallets"]]
        assert_equal(pruned.createwalletfrommnemonic("new", ABANDON, "", "now")["rescanned"], False)


if __name__ == '__main__':
    WalletBip39Test(__file__).main()
