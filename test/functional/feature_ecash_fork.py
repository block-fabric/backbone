#!/usr/bin/env python3
# Copyright (c) 2026 The Backbone developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""The eCash fork on regtest: one node with Bitcoin's rules, one with eCash's (-testecash) and a fork
height (-testecashfork), both retargeting as Bitcoin does.

- Up to the fork the chain is shared, and an ordinary transaction is valid on both (it replays both ways).
- A transaction with the magic locktime of replay protection is final on eCash, non-final on Bitcoin.
- At the fork height each node refuses the other's block: the difficulty reset is the split.
- A repurpose transaction (-testrepurposetx) is valid on eCash without its input scripts; an unlisted one is not.
- Any script of four bytes starting with OP_NOP8 is standard and anyone-can-spend on eCash.
- A reorg across the fork height on the eCash node, and getblocktemplate's bip300301 rule.
"""
from test_framework.blocktools import COINBASE_MATURITY
from test_framework.messages import COIN, CTxOut, CTxInWitness, tx_from_hex
from test_framework.script import CScript, OP_RETURN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.wallet import MiniWallet

FORK_HEIGHT = 288  # 2 * 144, the retarget interval of regtest
FORK_BITS = "207fffff"
MAGIC_LOCKTIME = 499999998
NON_FINAL = 0xfffffffe


class EcashForkTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.bitcoin_args = ["-testpowretarget"]
        self.ecash_args = ["-testecash", f"-testecashfork={FORK_HEIGHT}@{FORK_BITS}"]
        self.extra_args = [self.bitcoin_args, self.ecash_args]

    def run_test(self):
        bitcoin, ecash = self.nodes
        wallet = MiniWallet(bitcoin)
        assert_equal(ecash.getblockchaininfo()["network"], "regtest")

        self.log.info("A shared chain up to the block before the fork")
        self.generate(wallet, FORK_HEIGHT - 1 - COINBASE_MATURITY)
        self.generate(bitcoin, COINBASE_MATURITY)
        self.sync_all()
        assert_equal(ecash.getblockcount(), FORK_HEIGHT - 1)
        # Both retargeted at 144, alike.
        assert_equal(bitcoin.getblockheader(bitcoin.getbestblockhash())["bits"], ecash.getblockheader(ecash.getbestblockhash())["bits"])
        repurpose_utxo = wallet.get_utxo()
        nop8_utxo = wallet.get_utxo()

        self.log.info("An ordinary transaction replays both ways")
        plain = wallet.send_self_transfer(from_node=bitcoin)
        plain2 = wallet.create_self_transfer()
        ecash.sendrawtransaction(plain2["hex"])
        self.sync_mempools()
        assert plain["txid"] in ecash.getrawmempool() and plain2["txid"] in bitcoin.getrawmempool()

        self.log.info("The magic locktime is final on eCash only")
        protected = wallet.create_self_transfer(locktime=MAGIC_LOCKTIME, sequence=NON_FINAL)
        assert_raises_rpc_error(-26, "non-final", bitcoin.sendrawtransaction, protected["hex"])
        ecash.sendrawtransaction(protected["hex"])
        # The value of betanet is an ordinary (far) height lock here.
        betanet_value = wallet.create_self_transfer(locktime=MAGIC_LOCKTIME + 1, sequence=NON_FINAL)
        assert_raises_rpc_error(-26, "non-final", ecash.sendrawtransaction, betanet_value["hex"])

        self.log.info("getblocktemplate on eCash asks for the bip300301 rule, and says !bip300301")
        assert_raises_rpc_error(-8, "bip300301", ecash.getblocktemplate, {"rules": ["segwit"]})
        template = ecash.getblocktemplate({"rules": ["segwit", "bip300301"]})
        assert "!bip300301" in template["rules"]
        assert_equal(template["bits"], FORK_BITS)
        assert_equal(template["height"], FORK_HEIGHT)
        assert ecash.getblocktemplate({"rules": ["segwit"], "capabilities": ["coinbasetxn"]})
        assert "!bip300301" not in bitcoin.getblocktemplate({"rules": ["segwit"]})["rules"]

        self.log.info("The fork block: each node refuses the other's")
        self.disconnect_nodes(0, 1)
        bitcoin_block = self.generate(bitcoin, 1, sync_fun=self.no_op)[0]
        ecash_block = self.generate(ecash, 1, sync_fun=self.no_op)[0]
        assert_equal(ecash.getblockheader(ecash_block)["bits"], FORK_BITS)
        assert bitcoin.getblockheader(bitcoin_block)["bits"] != FORK_BITS
        assert_equal(ecash.submitblock(bitcoin.getblock(bitcoin_block, 0)), "bad-diffbits")
        assert_equal(bitcoin.submitblock(ecash.getblock(ecash_block, 0)), "bad-diffbits")
        assert protected["txid"] in ecash.getblock(ecash_block)["tx"]
        assert_equal(bitcoin.getbestblockhash(), bitcoin_block)
        assert_equal(ecash.getbestblockhash(), ecash_block)

        self.log.info("A repurpose transaction: valid without its input scripts, on eCash, if listed")
        unsigned = wallet.create_self_transfer(utxo_to_spend=repurpose_utxo)["tx"]
        unsigned.wit.vtxinwit = [CTxInWitness()]  # no witness: the script cannot succeed
        other = tx_from_hex(unsigned.serialize().hex())
        other.vout[0].nValue -= 1000
        self.restart_node(1, extra_args=self.ecash_args + [f"-testrepurposetx={unsigned.txid_hex}"])
        assert_raises_rpc_error(-26, "script-verify-flag-failed", ecash.sendrawtransaction, other.serialize().hex())
        assert_raises_rpc_error(-26, "script-verify-flag-failed", bitcoin.sendrawtransaction, unsigned.serialize().hex())
        ecash.sendrawtransaction(unsigned.serialize().hex())
        block = self.generate(ecash, 1, sync_fun=self.no_op)[0]
        assert unsigned.txid_hex in ecash.getblock(block)["tx"]

        self.log.info("OP_NOP8: any script of four bytes that starts with it is standard and anyone-can-spend")
        nop8_script = CScript(bytes([0xb7, 0x00, 0x00, 0x00]))
        fund = wallet.create_self_transfer(utxo_to_spend=nop8_utxo)["tx"]
        fund.vout[0].scriptPubKey = nop8_script
        fund.vout[0].nValue = int(nop8_utxo["value"] * COIN) - 2000
        fund.vout.append(CTxOut(0, CScript([OP_RETURN, b"eCash"])))  # (a standard size)
        assert_raises_rpc_error(-26, "scriptpubkey", bitcoin.sendrawtransaction, fund.serialize().hex())
        ecash.sendrawtransaction(fund.serialize().hex())
        spend = tx_from_hex(fund.serialize().hex())
        spend.vin[0].prevout.hash = int(fund.txid_hex, 16)
        spend.vin[0].prevout.n = 0
        spend.wit.vtxinwit = []
        spend.vout = [CTxOut(fund.vout[0].nValue - 2000, wallet.get_output_script())]
        ecash.sendrawtransaction(spend.serialize().hex())
        self.generate(ecash, 1, sync_fun=self.no_op)
        assert_equal(ecash.getrawmempool(), [])

        self.log.info("A reorg across the fork height on the eCash node")
        tip_height = ecash.getblockcount()
        ecash.invalidateblock(ecash_block)
        assert_equal(ecash.getblockcount(), FORK_HEIGHT - 1)
        new_blocks = self.generate(ecash, tip_height - FORK_HEIGHT + 2, sync_fun=self.no_op)
        assert_equal(ecash.getblockheader(new_blocks[0])["bits"], FORK_BITS)
        assert_equal(ecash.getblockcount(), tip_height + 1)
        ecash.reconsiderblock(ecash_block)
        assert_equal(ecash.getbestblockhash(), new_blocks[-1])


if __name__ == '__main__':
    EcashForkTest(__file__).main()
