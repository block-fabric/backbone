#!/usr/bin/env python3
# Copyright (c) 2026 The Backbone developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""The bip300301_enforcer's gRPC interface, served by backboned (-grpc), on an eCash regtest (-testecash).

A scripted chain: a sidechain proposed and activated, deposits, blind merged mining, withdrawal bundles
paid out and failed, a reorg; and the replies of each method checked against what the enforcer's code
answers (commit 17eb2ce, the one betanet runs; file:line in the comments, paths of that repository),
with clients that are not the server's code: gRPC through gRPC's own C++ library (backbone-grpc-client,
built when grpc++ is installed) and the Connect protocol through http.client, each with this test's
own protocol buffers codec (test_framework/cusf.py).
"""
import http.client
import json
import os
import socket
import subprocess
from decimal import Decimal

from test_framework.address import address_to_scriptpubkey
from test_framework.cusf import (
    ALREADY_EXISTS, FAILED_PRECONDITION, INTERNAL, INVALID_ARGUMENT, NOT_FOUND, OK, PERMISSION_DENIED, UNAUTHENTICATED, UNAVAILABLE, UNIMPLEMENTED,
    ConnectClient, GrpcClient, Schema, client_binary, descriptor_source, fields_of, rh, u32, unhex,
)
from test_framework.key import ECKey
from test_framework.messages import COIN, CTransaction, CTxOut, hash256, sha256
from test_framework.script import CScript, OP_RETURN
from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import assert_equal, assert_greater_than, p2p_port

SLOT = 7
V = 'cusf.mainchain.v1.ValidatorService/'
W = 'cusf.mainchain.v1.WalletService/'
B = 'cusf.mainchain.v1.BlockProducerService/'
M = 'cusf.mainchain.v1.MiningService/'
C = 'cusf.crypto.v1.CryptoService/'
TOKEN = 's3cret'
TITLE, DESCRIPTION = 'grpctest', 'a sidechain for the gRPC test'
HASH_ID_1, HASH_ID_2 = bytes(range(32)), bytes(range(20))


def free_port():
    """A free port for the gRPC server, from those of p2p_port(0) on, 49000-49999 (not 49x54, 4966x) on this machine."""
    # Not the node's own ports (p2p_port(0) is free until the node starts: the gRPC server would then lose the bind
    # to the P2P listener, and the client talk to the P2P port -- "Socket closed", about one run in three).
    from test_framework.util import PORT_RANGE, rpc_port
    taken = {f(n) for f in (p2p_port, rpc_port, lambda n: p2p_port(n) + 2 * PORT_RANGE) for n in range(12)}
    start = 49000 + p2p_port(0) % 1000
    for i in range(1000):
        port = 49000 + (start - 49000 + i) % 1000
        if port % 100 == 54 or 49660 <= port <= 49669 or port in taken:
            continue
        with socket.socket() as s:
            try:
                s.bind(('127.0.0.1', port))
                return port
            except OSError:
                continue
    raise AssertionError('no free port')


def ctip(txid, vout, value, sequence):
    """GetCtipResponse.Ctip: plain uint32/uint64 fields, a 0 not written."""
    c = {'txid': rh(txid), 'vout': vout, 'value': value, 'sequence_number': sequence}
    return {'ctip': {k: v for k, v in c.items() if v != 0}}


def consensus(display_hex):
    """A hash as the enforcer's ConsensusHex writes it (its bytes in order): the reverse of the RPC's hex."""
    return bytes.fromhex(display_hex)[::-1].hex()


def description_bytes():
    """BIP300 M1 v0 (lib/types.rs:410-420): version, title length, title, description, hash_id_1, hash_id_2."""
    return bytes([0, len(TITLE)]) + TITLE.encode() + DESCRIPTION.encode() + HASH_ID_1 + HASH_ID_2


def consensus_vec(data):
    """ConsensusHex of a Vec<u8>: its compact size first (rust-bitcoin's Encodable for Vec<u8>)."""
    assert len(data) < 253
    return (bytes([len(data)]) + data).hex()


class EcashGrpcTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.wallet_names = ['w']

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        if not self.config['components'].getboolean('ENABLE_GRPC', fallback=False):
            raise SkipTest('backboned built without gRPC (WITH_GRPC)')
        if not os.path.exists(client_binary(self.config)):
            raise SkipTest('backbone-grpc-client not built (grpc++ not installed)')

    def setup_network(self):
        self.grpc_port = free_port()
        self.base_args = ['-testecash', '-grpc', f'-grpcport={self.grpc_port}', '-fallbackfee=0.0002', '-grpcwallet=w']
        self.extra_args = [self.base_args + [f'-grpcauth={TOKEN}', '-grpcallowstop']]
        self.setup_nodes()

    def mine(self, n=1):
        return self.generatetoaddress(self.node, n, self.address, sync_fun=self.no_op)

    def info(self, block_hash):
        """GetBlockInfo of one block, for SLOT."""
        reply = self.grpc.unary(V + 'GetBlockInfo', {'block_hash': rh(block_hash), 'sidechain_id': u32(SLOT)})
        assert_equal(len(reply['infos']), 1)
        return reply['infos'][0]

    def events(self, block_hash):
        return self.info(block_hash)['block_info'].get('events', [])

    def run_test(self):
        self.node = self.nodes[0]
        self.address = self.node.getnewaddress()
        schema = Schema.from_source(descriptor_source(self.config))
        self.schema = schema
        self.grpc = GrpcClient(client_binary(self.config), schema, f'127.0.0.1:{self.grpc_port}', [f'authorization:Bearer {TOKEN}'])
        self.anonymous = GrpcClient(client_binary(self.config), schema, f'127.0.0.1:{self.grpc_port}')
        self.connect = ConnectClient(schema, '127.0.0.1', self.grpc_port)
        self.mine(110)

        self.test_health_and_reflection()
        self.test_chain()
        self.test_crypto()
        self.test_sidechain_activation()
        self.test_subscription_and_deposits()
        self.test_bmm()
        self.test_bundles()
        self.test_two_way_peg_data()
        self.test_reorg()
        self.test_connect_protocol()
        self.test_errors()
        self.test_security()
        self.test_stop()

    def test_health_and_reflection(self):
        self.log.info('grpc.health.v1: SERVING for the services served, NOT_FOUND for others (connectrpc-health, app/main.rs:774-775)')
        h = 'grpc.health.v1.Health/Check'
        for service in ['', 'cusf.mainchain.v1.ValidatorService', 'cusf.mainchain.v1.WalletService', 'cusf.mainchain.v1.BlockProducerService',
                        'cusf.mainchain.v1.MiningService', 'cusf.crypto.v1.CryptoService']:
            assert_equal(self.grpc.unary(h, {'service': service}), {'status': 1})
        code, _, _ = self.grpc.call(h, {'service': 'cusf.mainchain.v1.Nothing'})
        assert_equal(code, NOT_FOUND)

        self.log.info('grpc.reflection.v1: ListServices, and a file by a symbol (connectrpc-reflection, app/main.rs:763-776)')
        r = 'grpc.reflection.v1.ServerReflection/ServerReflectionInfo'
        req_type, _, _ = self.grpc.schema.method(r)
        payloads = [self.grpc.schema.encode(req_type, {'list_services': ''}),
                    self.grpc.schema.encode(req_type, {'file_containing_symbol': 'cusf.mainchain.v1.ValidatorService'}),
                    self.grpc.schema.encode(req_type, {'file_by_filename': 'nope.proto'})]
        code, _, replies = self.grpc.call_raw_lines(r, payloads)
        assert_equal(code, OK)
        assert_equal(len(replies), 3)
        services = sorted(s['name'] for s in replies[0]['list_services_response']['service'])
        assert_equal(services, ['cusf.crypto.v1.CryptoService', 'cusf.mainchain.v1.BlockProducerService', 'cusf.mainchain.v1.MiningService',
                                'cusf.mainchain.v1.ValidatorService', 'cusf.mainchain.v1.WalletService'])
        files = replies[1]['file_descriptor_response']['file_descriptor_proto']
        # The file first, then those it imports: each a FileDescriptorProto, its name in field 1.
        names = [next(v for n, _, v in fields_of(f) if n == 1).decode() for f in files]
        assert_equal(names[0], 'cusf/mainchain/v1/validator.proto')
        assert 'google/protobuf/wrappers.proto' in names and 'cusf/common/v1/common.proto' in names
        assert_equal(replies[2]['error_response']['error_code'], NOT_FOUND)

    def test_chain(self):
        node = self.node
        self.log.info('GetChainInfo: regtest, the enforcer\'s regtest thresholds (Thresholds::SHORT, lib/types.rs:66-73), from genesis (grpc.rs:169-200)')
        reply = self.grpc.unary(V + 'GetChainInfo')
        assert_equal(reply['network'], 3)
        assert_equal(reply['bip300_constants'], {'withdrawal_bundle_max_age': 10, 'withdrawal_bundle_inclusion_threshold': 5,
                                                 'used_sidechain_slot_proposal_max_age': 10, 'used_sidechain_slot_activation_threshold': 5,
                                                 'unused_sidechain_slot_proposal_max_age': 10, 'unused_sidechain_slot_activation_threshold': 5})

        self.log.info('GetChainTip and GetBlockHeaderInfo: hash and parent reversed, the block\'s own work 32 bytes little endian, its time (lib/proto.rs:813-823)')
        tip = node.getbestblockhash()
        header = node.getblockheader(tip)
        expected = {'block_hash': rh(tip), 'prev_block_hash': rh(header['previousblockhash']), 'height': header['height'],
                    'work': rh((2).to_bytes(32, 'little').hex()), 'timestamp': header['time']}
        assert_equal(self.grpc.unary(V + 'GetChainTip')['block_header_info'], expected)
        # The block and up to max_ancestors ancestors, newest first; to genesis at most (grpc.rs:45-72,
        # lib/validator/dbs/block_hashes.rs:416-437).
        infos = self.grpc.unary(V + 'GetBlockHeaderInfo', {'block_hash': rh(tip), 'max_ancestors': 2})['header_infos']
        assert_equal([unhex(i['block_hash']) for i in infos], [tip, header['previousblockhash'], node.getblockhash(header['height'] - 2)])
        assert_equal(len(self.grpc.unary(V + 'GetBlockHeaderInfo', {'block_hash': rh(node.getblockhash(1)), 'max_ancestors': 100})['header_infos']), 2)
        genesis = self.grpc.unary(V + 'GetBlockHeaderInfo', {'block_hash': rh(node.getblockhash(0))})['header_infos'][0]
        assert_equal(genesis['prev_block_hash'], rh('00' * 32))
        assert 'height' not in genesis  # proto3: 0 is not written
        # An unknown block: no infos (not NOT_FOUND), which the sidechains rely on.
        assert_equal(self.grpc.unary(V + 'GetBlockHeaderInfo', {'block_hash': rh('11' * 32)}), {})
        assert_equal(self.grpc.unary(V + 'GetBlockInfo', {'block_hash': rh('11' * 32), 'sidechain_id': u32(SLOT)}), {})
        # GetBmmHStarCommitment of an unknown block: block_not_found (grpc.rs:141-147).
        assert_equal(self.grpc.unary(V + 'GetBmmHStarCommitment', {'block_hash': rh('11' * 32), 'sidechain_id': u32(SLOT)}),
                     {'block_not_found': {'block_hash': rh('11' * 32)}})

        self.log.info('SubscribeHeaderSyncProgress: UNAVAILABLE when no header sync is going on (grpc.rs:462-474)')
        code, message, _ = self.grpc.call(V + 'SubscribeHeaderSyncProgress', {})
        assert_equal((code, message), (UNAVAILABLE, 'No header sync in progress'))

    def test_crypto(self):
        self.log.info('CryptoService (lib/server/crypto.rs)')
        assert_equal(self.grpc.unary(C + 'Ripemd160', {'msg': rh('')}), {'digest': rh('9c1185a5c5e9fc54612808977ee8f548b2258d31')})
        # RFC 4231 test case 2.
        assert_equal(self.grpc.unary(C + 'HmacSha512', {'key': rh(b'Jefe'.hex()), 'msg': rh(b'what do ya want for nothing?'.hex())})['hmac'],
                     rh('164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737'))
        key = ECKey()
        key.set((7).to_bytes(32, 'big'), True)
        pub = self.grpc.unary(C + 'Secp256k1SecretKeyToPublicKey', {'secret_key': rh((7).to_bytes(32, 'big').hex())})['public_key']
        assert_equal(unhex(pub), key.get_pubkey().get_bytes().hex())
        message = b'a message'
        sig = unhex(self.grpc.unary(C + 'Secp256k1Sign', {'message': rh(message.hex()), 'secret_key': rh((7).to_bytes(32, 'big').hex())})['signature'])
        # sha256 of the message, RFC 6979 without extra entropy, low S, the 64 byte compact form (crypto.rs:95-121):
        # what test_framework/key.py makes of the same.
        der = key.sign_ecdsa(sha256(message), low_s=True, rfc6979=True)
        r_len = der[3]
        r = int.from_bytes(der[4:4 + r_len], 'big')
        s = int.from_bytes(der[6 + r_len:], 'big')
        assert_equal(sig, (r.to_bytes(32, 'big') + s.to_bytes(32, 'big')).hex())
        verify = {'message': rh(message.hex()), 'signature': rh(sig), 'public_key': pub}
        assert_equal(self.grpc.unary(C + 'Secp256k1Verify', verify), {'valid': True})
        verify['message'] = rh(b'another'.hex())
        assert_equal(self.grpc.unary(C + 'Secp256k1Verify', verify), {})  # valid: false, the default, is not written
        code, message, _ = self.grpc.call(C + 'Secp256k1Verify', {'message': rh(''), 'signature': rh('00'), 'public_key': pub})
        assert_equal(code, INVALID_ARGUMENT)
        assert 'invalid signature length, must be 64' in message
        # A secret key of 31 bytes: a ConsensusHex of [u8; 32] (crypto.rs:77-81).
        code, message, _ = self.grpc.call(C + 'Secp256k1SecretKeyToPublicKey', {'secret_key': rh('07' * 31)})
        assert_equal(code, INVALID_ARGUMENT)
        assert message.startswith('Invalid field value in field `secret_key` of message `cusf.crypto.v1.Secp256k1SecretKeyToPublicKeyRequest`: `' + '07' * 31 + '`')

    def test_sidechain_activation(self):
        node = self.node
        self.log.info('SubmitSidechainProposal: the next blocks propose it (M1); again: ALREADY_EXISTS (lib/server/block_producer.rs)')
        declaration = {'v0': {'title': {'value': TITLE}, 'description': {'value': DESCRIPTION},
                              'hash_id_1': rh(HASH_ID_1.hex()), 'hash_id_2': rh(HASH_ID_2.hex())}}
        self.grpc.unary(B + 'SubmitSidechainProposal', {'sidechain_id': u32(SLOT), 'declaration': declaration})
        code, message, _ = self.grpc.call(B + 'SubmitSidechainProposal', {'sidechain_id': u32(SLOT), 'declaration': declaration})
        assert_equal((code, message), (ALREADY_EXISTS, 'Sidechain proposal already exists'))
        desc = description_bytes()
        desc_hash = hash256(desc)[::-1].hex()
        state = self.grpc.unary(B + 'GetBlockProducerState')
        assert_equal(state['pending_proposals'], [{'sidechain_number': u32(SLOT), 'description_sha256d_hash': rh(desc_hash),
                                                   'declaration': declaration, 'description': rh(desc.hex())}])
        assert_equal(state['ack_policy'], 1)
        assert_equal(state['withdrawal_bundle_policy'], 2)

        proposed_at = node.getblockcount() + 1
        self.mine()
        self.log.info('GetSidechainProposals: the proposal collecting acks (grpc.rs:307-355)')
        proposals = self.grpc.unary(V + 'GetSidechainProposals')['sidechain_proposals']
        assert_equal(proposals, [{'sidechain_number': u32(SLOT), 'description': rh(consensus_vec(desc)), 'declaration': declaration,
                                  'description_sha256d_hash': rh(desc_hash), 'vote_count': u32(0), 'proposal_height': u32(proposed_at),
                                  'proposal_age': u32(0)}])
        self.mine(2)
        proposals = self.grpc.unary(V + 'GetSidechainProposals')['sidechain_proposals']
        assert_equal((proposals[0]['vote_count'], proposals[0]['proposal_age']), (u32(2), u32(2)))
        self.mine(4)
        # An unused slot activates with more than 5 acks within 10 blocks (lib/validator/task/mod.rs:262-280).
        sidechains = self.grpc.unary(V + 'GetSidechains')['sidechains']
        assert_equal(sidechains, [{'sidechain_number': u32(SLOT), 'description': rh(consensus_vec(desc)), 'vote_count': u32(6),
                                   'proposal_height': u32(proposed_at), 'activation_height': u32(proposed_at + 6), 'declaration': declaration}])
        assert_equal(self.grpc.unary(V + 'GetSidechainProposals'), {})
        assert 'pending_proposals' not in self.grpc.unary(B + 'GetBlockProducerState')

    def test_subscription_and_deposits(self):
        node = self.node
        self.log.info('SubscribeEvents: nothing on subscribing (grpc.rs:440-460; the sidechains\' GetChainTip handshake needs that)')
        self.stream = self.grpc.subscribe(V + 'SubscribeEvents', {'sidechain_id': u32(SLOT)})
        assert self.stream.nothing_within(2)
        self.log.info('GetCtip: none before the first deposit (grpc.rs:270-305)')
        assert_equal(self.grpc.unary(V + 'GetCtip', {'sidechain_number': u32(SLOT)}), {})

        self.log.info('CreateDepositTransaction, and the deposits in the blocks: sequence numbers from 0, the amount added, the address as given')
        txid1 = unhex(self.grpc.unary(W + 'CreateDepositTransaction', {'sidechain_id': u32(SLOT), 'address': {'value': 'abc'},
                                                                     'value_sats': u32(COIN), 'fee_sats': u32(1000)})['txid'])
        assert txid1 in node.getrawmempool()
        block1 = self.mine()[0]
        tx1 = node.getrawtransaction(txid1, True, block1)
        vout1 = next(o['n'] for o in tx1['vout'] if o['value'] == Decimal(1))
        expected1 = [{'deposit': {'sequence_number': u32(0), 'outpoint': {'txid': rh(txid1), 'vout': u32(vout1)},
                                  'output': {'address': rh(b'abc'.hex()), 'value_sats': u32(COIN)}}}]
        assert_equal(self.events(block1), expected1)
        self.check_event(block1)
        assert_equal(self.grpc.unary(V + 'GetCtip', {'sidechain_number': u32(SLOT)}), ctip(txid1, vout1, COIN, 0))

        txid2 = unhex(self.grpc.unary(W + 'CreateDepositTransaction', {'sidechain_id': u32(SLOT), 'address': {'value': 'xyz123'},
                                                                     'value_sats': u32(COIN // 2), 'fee_sats': u32(1000)})['txid'])
        block2 = self.mine()[0]
        tx2 = node.getrawtransaction(txid2, True, block2)
        vout2 = next(o['n'] for o in tx2['vout'] if o['value'] == Decimal('1.5'))
        # The value is what the deposit added, not the treasury's total (task/mod.rs:862-874).
        assert_equal(self.events(block2), [{'deposit': {'sequence_number': u32(1), 'outpoint': {'txid': rh(txid2), 'vout': u32(vout2)},
                                                        'output': {'address': rh(b'xyz123'.hex()), 'value_sats': u32(COIN // 2)}}}])
        self.check_event(block2)
        assert_equal(self.grpc.unary(V + 'GetCtip', {'sidechain_number': u32(SLOT)}), ctip(txid2, vout2, COIN * 3 // 2, 1))
        self.deposit_blocks = [block1, block2]

        self.log.info('Another slot: no events for it (BlockInfo::only_sidechain, lib/types.rs:805-843)')
        other = self.grpc.unary(V + 'GetBlockInfo', {'block_hash': rh(block2), 'sidechain_id': u32(3)})['infos'][0]['block_info']
        assert_equal(other, {})

    def check_event(self, block_hash):
        """The stream's next event: ConnectBlock of this block, with what GetBlockInfo answers (lib/proto.rs:953-978)."""
        event = self.stream.next()
        assert event is not None, self.stream.status
        info = self.info(block_hash)
        assert_equal(event, {'event': {'connect_block': {'header_info': info['header_info'], 'block_info': info['block_info']}}})

    def test_bmm(self):
        node = self.node
        self.log.info('CreateBmmCriticalDataTransaction: an M8 on the tip, the bid as its fee; the next block commits to it (wallet/grpc.rs:161-257)')
        tip = node.getbestblockhash()
        h_star = '5a' * 31 + '01'
        request = {'sidechain_id': u32(SLOT), 'value_sats': u32(5000), 'height': u32(node.getblockcount()),
                   'critical_hash': rh(h_star), 'prev_bytes': rh(tip)}
        # Not the tip: INVALID_ARGUMENT naming prev_bytes, which sidechains read as "the tip moved".
        code, message, _ = self.grpc.call(W + 'CreateBmmCriticalDataTransaction', dict(request, prev_bytes=rh(node.getblockhash(5))))
        assert_equal(code, INVALID_ARGUMENT)
        assert_equal(message, f'invalid prev_bytes {node.getblockhash(5)}: expected {tip}')
        txid = unhex(self.grpc.unary(W + 'CreateBmmCriticalDataTransaction', request)['txid'])
        assert txid in node.getrawmempool()
        self.log.info('GetSeenBmmRequests (the enforcer\'s master, grpc.rs:310-366): the requests built on a block, richest first')
        seen = self.grpc.unary(V + 'GetSeenBmmRequests', {'prev_block_hash': rh(tip)})
        assert_equal(seen, {'requests': [{'sidechain_number': SLOT, 'txid': rh(txid), 'critical_hash': rh(h_star),
                                          'bid_sats': node.getmempoolentry(txid)['fees']['base'] * COIN}]})
        assert_equal(self.grpc.unary(V + 'GetSeenBmmRequests', {'prev_block_hash': rh(tip), 'sidechain_number': u32(3)}), {})
        block = self.mine()[0]
        assert_equal(self.info(block)['block_info'], {'bmm_commitment': rh(h_star)})
        self.check_event(block)
        self.log.info('GetBmmHStarCommitment: the block\'s, then its ancestors\' (grpc.rs:111-167)')
        reply = self.grpc.unary(V + 'GetBmmHStarCommitment', {'block_hash': rh(block), 'sidechain_id': u32(SLOT), 'max_ancestors': 2})
        assert_equal(reply, {'commitment': {'commitment': rh(h_star), 'ancestor_commitments': [{}, {}]}})
        self.bmm_block = block

    def blind_bundle(self, payout, fee, address):
        """A withdrawal bundle as sidechains hand it: no inputs, output 0 OP_RETURN fee (8 bytes BE), then the payouts."""
        bundle = CTransaction()
        bundle.vin = []
        bundle.vout = [CTxOut(0, CScript([OP_RETURN, fee.to_bytes(8, 'big')])), CTxOut(payout, address_to_scriptpubkey(address))]
        return bundle

    def test_bundles(self):
        node = self.node
        self.log.info('ProposeWithdrawalBundle: proposed (M3: Submitted), voted, paid out (M6: Succeeded with the transaction)')
        bundle = self.blind_bundle(COIN // 4, 1000, node.getnewaddress())
        m6id = consensus(bundle.txid_hex)
        # Errors first (store_withdrawal_bundle, lib/server/mod.rs:38-78).
        code, message, _ = self.grpc.call(B + 'ProposeWithdrawalBundle', {'sidechain_id': u32(3), 'transaction': {'value': bundle.serialize()}})
        assert_equal((code, message), (FAILED_PRECONDITION, 'cannot accept a withdrawal bundle for sidechain 3: not active'))
        code, _, _ = self.grpc.call(B + 'ProposeWithdrawalBundle', {'sidechain_id': u32(SLOT), 'transaction': {'value': b'\x02\x00'}})
        assert_equal(code, INVALID_ARGUMENT)
        self.grpc.unary(B + 'ProposeWithdrawalBundle', {'sidechain_id': u32(SLOT), 'transaction': {'value': bundle.serialize()}})
        proposed = self.mine()[0]
        proposed_height = node.getblockcount()
        assert_equal(self.events(proposed), [{'withdrawal_bundle': {'m6id': rh(m6id), 'event': {'submitted': {}}}}])
        self.check_event(proposed)
        # Pending, from 1 (PendingM6idInfo::new, lib/types.rs:1227-1235).
        assert_equal(self.grpc.unary(V + 'GetWithdrawalBundleProposals', {'sidechain_id': u32(SLOT)}),
                     {'proposals': [{'m6id': rh(m6id), 'vote_count': u32(1), 'proposal_height': u32(proposed_height)}]})
        paid_block = None
        for _ in range(8):
            block = self.mine()[0]
            self.check_event(block)
            events = self.events(block)
            if events:
                paid_block = block
                break
        assert paid_block is not None
        # More than 5 votes (task/mod.rs:651-697): the template pays it out.
        ev = events[0]['withdrawal_bundle']
        assert_equal(ev['m6id'], rh(m6id))
        succeeded = ev['event']['succeeded']
        assert_equal(succeeded['sequence_number'], u32(2))
        m6 = node.getrawtransaction(node.getblock(paid_block)["tx"][-1], True, paid_block)
        assert_equal(unhex(succeeded['transaction']), m6['hex'])
        assert_equal(self.grpc.unary(V + 'GetWithdrawalBundleProposals', {'sidechain_id': u32(SLOT)}), {})
        reply = self.grpc.unary(V + 'GetCtip', {'sidechain_number': u32(SLOT)})['ctip']
        assert_equal((unhex(reply['txid']), reply.get('vout', 0), reply['sequence_number']), (m6['txid'], 0, 2))

        self.log.info('A bundle nobody votes for fails once older than 10 blocks: Failed (handle_failed_m6ids, task/mod.rs:612-649)')
        self.grpc.unary(B + 'SetWithdrawalBundlePolicy', {'policy': 1})  # NONE: abstain
        assert_equal(self.grpc.unary(B + 'GetBlockProducerState')['withdrawal_bundle_policy'], 1)
        other = self.blind_bundle(COIN // 8, 1000, node.getnewaddress())
        other_id = consensus(other.txid_hex)
        self.grpc.unary(B + 'ProposeWithdrawalBundle', {'sidechain_id': u32(SLOT), 'transaction': {'value': other.serialize()}})
        other_submitted = self.mine()[0]
        self.check_event(other_submitted)
        assert_equal(self.events(other_submitted), [{'withdrawal_bundle': {'m6id': rh(other_id), 'event': {'submitted': {}}}}])
        # The vote message of each block abstains (and its score goes to 0, then stays).
        for _ in range(10):
            block = self.mine()[0]
            self.check_event(block)
            assert_equal(self.events(block), [])
        failed = self.mine()[0]
        self.check_event(failed)
        assert_equal(self.events(failed), [{'withdrawal_bundle': {'m6id': rh(other_id), 'event': {'failed': {}}}}])
        self.grpc.unary(B + 'SetWithdrawalBundlePolicy', {'policy': 2})
        self.bundle_blocks = [proposed, paid_block, other_submitted, failed]

    def test_two_way_peg_data(self):
        node = self.node
        self.log.info('GetTwoWayPegData: the blocks after the start (not the start itself) to the end, oldest first, those with something for the slot (block_hashes.rs:502-541, lib/proto.rs:932-951)')
        tip = node.getbestblockhash()
        blocks = self.grpc.unary(V + 'GetTwoWayPegData', {'sidechain_id': u32(SLOT), 'end_block_hash': rh(tip)})['blocks']
        hashes = [unhex(b['block_header_info']['block_hash']) for b in blocks]
        expected = self.deposit_blocks + [self.bmm_block] + self.bundle_blocks
        assert_equal(hashes, expected)
        for b in blocks:
            info = self.info(unhex(b['block_header_info']['block_hash']))
            assert_equal(b, {'block_header_info': info['header_info'], 'block_info': info['block_info']})
        after_first = self.grpc.unary(V + 'GetTwoWayPegData', {'sidechain_id': u32(SLOT), 'start_block_hash': rh(self.deposit_blocks[0]), 'end_block_hash': rh(tip)})['blocks']
        assert_equal([unhex(b['block_header_info']['block_hash']) for b in after_first], expected[1:])
        # The start being the end: that block alone.
        only = self.grpc.unary(V + 'GetTwoWayPegData', {'sidechain_id': u32(SLOT), 'start_block_hash': rh(self.bmm_block), 'end_block_hash': rh(self.bmm_block)})['blocks']
        assert_equal([unhex(b['block_header_info']['block_hash']) for b in only], [self.bmm_block])
        self.log.info('Errors: INTERNAL with the enforcer\'s texts (internal_err, grpc.rs:399-402)')
        code, message, _ = self.grpc.call(V + 'GetTwoWayPegData', {'sidechain_id': u32(SLOT), 'start_block_hash': rh(tip), 'end_block_hash': rh(self.bmm_block)})
        assert_equal((code, message), (INTERNAL, f'Start block `{tip}` is not an ancestor of end block `{self.bmm_block}`'))
        code, message, _ = self.grpc.call(V + 'GetTwoWayPegData', {'sidechain_id': u32(SLOT), 'end_block_hash': rh('22' * 32)})
        assert_equal((code, message), (INTERNAL, f'End block `{"22" * 32}` not found'))

    def test_reorg(self):
        node = self.node
        self.log.info('A reorg: DisconnectBlock of the blocks taken back, newest first, then ConnectBlock of the new ones (grpc.rs:440-460)')
        txid = unhex(self.grpc.unary(W + 'CreateDepositTransaction', {'sidechain_id': u32(SLOT), 'address': {'value': 'reorg'},
                                                                    'value_sats': u32(COIN // 10), 'fee_sats': u32(1000)})['txid'])
        a = self.mine()[0]
        self.check_event(a)
        b = self.mine()[0]
        self.check_event(b)
        seq = self.events(a)[0]['deposit']['sequence_number']
        assert_equal(self.grpc.unary(V + 'GetCtip', {'sidechain_number': u32(SLOT)})['ctip']['sequence_number'], seq['value'])
        node.invalidateblock(a)
        for gone in (b, a):
            assert_equal(self.stream.next(), {'event': {'disconnect_block': {'block_hash': rh(gone)}}})
        # The treasury is as before the deposit; the stale block keeps its info (the enforcer keeps it, by hash).
        assert_equal(self.grpc.unary(V + 'GetCtip', {'sidechain_number': u32(SLOT)})['ctip']['sequence_number'], seq['value'] - 1)
        assert_equal(self.events(a)[0]['deposit']['sequence_number'], seq)
        # The deposit is mined again on the new branch, with the same sequence number.
        assert txid in node.getrawmempool()
        # (To another address: the same block again would be the invalidated one.)
        new = self.generatetoaddress(node, 2, node.getnewaddress(), sync_fun=self.no_op)
        for block in new:
            self.check_event(block)
        assert_equal(self.events(new[0])[0]['deposit']['outpoint']['txid'], rh(txid))
        assert_equal(self.events(new[0])[0]['deposit']['sequence_number'], seq)
        self.stream.close()

    def test_connect_protocol(self):
        self.log.info('The Connect protocol over HTTP/1.1 (the enforcer serves it on the same port: connectrpc, app/main.rs:777-791)')
        status, reply = self.connect.json('/' + V + 'GetChainTip')
        assert_equal(status, 200)
        tip = self.node.getbestblockhash()
        assert_equal(reply['blockHeaderInfo']['blockHash'], {'hex': tip})
        assert_equal(reply['blockHeaderInfo']['height'], self.node.getblockcount())
        assert_equal(reply['blockHeaderInfo']['timestamp'], str(self.node.getblockheader(tip)['time']))  # uint64 as a string
        status, proto_reply = self.connect.proto('/' + V + 'GetChainTip')
        assert_equal((status, proto_reply), (200, self.grpc.unary(V + 'GetChainTip')))
        status, reply = self.connect.json('/' + V + 'GetCtip', {'sidechainNumber': SLOT})
        assert_equal(status, 200)
        assert_equal(reply['ctip']['value'], str(self.grpc.unary(V + 'GetCtip', {'sidechain_number': u32(SLOT)})['ctip']['value']))
        status, reply = self.connect.json('/' + V + 'GetBlockInfo', {'sidechainId': SLOT})
        assert_equal((status, reply), (400, {'code': 'invalid_argument', 'message': 'Missing field in message `cusf.mainchain.v1.GetBlockInfoRequest`: `block_hash`'}))
        status, reply = self.connect.json('/cusf.mainchain.v1.ValidatorService/Nothing')
        assert_equal(status, 501)

    def test_errors(self):
        self.log.info('Errors as the enforcer words them (lib/proto.rs:238-327, lib/server/mod.rs:82-90)')
        code, message, _ = self.grpc.call(V + 'GetBlockInfo', {'sidechain_id': u32(SLOT)})
        assert_equal((code, message), (INVALID_ARGUMENT, 'Missing field in message `cusf.mainchain.v1.GetBlockInfoRequest`: `block_hash`'))
        code, message, _ = self.grpc.call(V + 'GetBlockInfo', {'block_hash': {}, 'sidechain_id': u32(SLOT)})
        assert_equal((code, message), (INVALID_ARGUMENT, 'Missing field in message `cusf.common.v1.ReverseHex`: `hex`'))
        # The cause as the Rust crates word it (the texts the enforcer 17eb2ce gave on the lab server, 2026-10-21):
        # ReverseHex and Hex through the hex crate, ConsensusHex through rust-bitcoin's lazy deserialize_hex.
        prefix = 'Invalid field value in field `{}` of message `{}`: `{}`: '
        for value, cause in (('zz', "Invalid character 'z' at position 0"), ('0z', "Invalid character 'z' at position 1"),
                             ('\t0', "Invalid character '\\t' at position 0"), ('0\x7f', "Invalid character '\\u{7f}' at position 1"), ('\u00e9', "Invalid character '\u00c3' at position 0"),
                             ('0', 'Odd number of digits'), ('00' * 31, 'IO error: I/O Error: UnexpectedEof'),
                             ('00' * 33, 'parse failed: data not consumed entirely when explicitly deserializing')):
            code, message, _ = self.grpc.call(V + 'GetBlockInfo', {'block_hash': rh(value), 'sidechain_id': u32(SLOT)})
            assert_equal((code, message), (INVALID_ARGUMENT, prefix.format('block_hash', 'cusf.mainchain.v1.GetBlockInfoRequest', value) + cause))
        for value, cause in (('', 'decoding error: invalid consensus encoding: IO error: I/O Error: UnexpectedEof'),
                             ('000', 'odd length, failed to create bytes from hex: odd hex string length 3'),
                             ('zz', 'decoding error: other decoding error: InvalidCharError { invalid: 122 }'),
                             ('00' * 33, 'decoding error: attempted to decode object from an iterator that yielded too many bytes')):
            code, message, _ = self.grpc.call('cusf.crypto.v1.CryptoService/Secp256k1SecretKeyToPublicKey', {'secret_key': rh(value)})
            assert_equal((code, message), (INVALID_ARGUMENT, prefix.format('secret_key', 'cusf.crypto.v1.Secp256k1SecretKeyToPublicKeyRequest', value) + cause))
        # A Vec<u8>: its compact size first (non-minimal: refused), then the bytes.
        for value, cause in (('fd0000', 'decoding error: invalid consensus encoding: non-minimal varint'),
                             ('00' * 32, 'decoding error: attempted to decode object from an iterator that yielded too many bytes')):
            code, message, _ = self.grpc.call(V + 'GetCoinbasePSBT', {'propose_sidechains': [{'sidechain_number': u32(1), 'data': rh(value)}]})
            assert_equal((code, message), (INVALID_ARGUMENT, prefix.format('data', 'cusf.mainchain.v1.GetCoinbasePSBTRequest.ProposeSidechain', value) + cause))
        code, message, _ = self.grpc.call(V + 'GetBlockInfo', {'block_hash': rh('00' * 32), 'sidechain_id': u32(256)})
        assert_equal(code, INVALID_ARGUMENT)
        assert message.startswith('Invalid field value in field `sidechain_id` of message `cusf.mainchain.v1.GetBlockInfoRequest`: `256`')
        # GetWithdrawalBundleProposals names GetTwoWayPegDataRequest in its error (grpc.rs:418).
        code, message, _ = self.grpc.call(V + 'GetWithdrawalBundleProposals', {})
        assert_equal((code, message), (INVALID_ARGUMENT, 'Missing field in message `cusf.mainchain.v1.GetTwoWayPegDataRequest`: `sidechain_id`'))
        # A slot never active: the enforcer's database has no entry (pending_m6ids().get()): INTERNAL.
        # Its text is sneed's MissingValue (the database's path, the key in hex), as the lab compared byte for byte.
        code, message, _ = self.grpc.call(V + 'GetWithdrawalBundleProposals', {'sidechain_id': u32(3)})
        assert_equal(code, INTERNAL)
        db_path = os.path.join(self.node.chain_path, 'blocks', 'drivechain')
        assert_equal(message, f'Missing value from db `active_sidechain_number_to_pending_m6ids` at `{db_path}` (key: 03)')
        # The wallet: an inactive sidechain, an empty address, no value (wallet/grpc.rs:259-308).
        deposit = {'sidechain_id': u32(3), 'address': {'value': 'x'}, 'value_sats': u32(1000), 'fee_sats': u32(1000)}
        code, message, _ = self.grpc.call(W + 'CreateDepositTransaction', deposit)
        assert_equal((code, message), (FAILED_PRECONDITION, 'sidechain 3 is not active'))
        code, message, _ = self.grpc.call(W + 'CreateDepositTransaction', dict(deposit, sidechain_id=u32(SLOT), address={'value': ''}))
        assert_equal((code, message), (INVALID_ARGUMENT, 'address must be non-empty'))
        code, message, _ = self.grpc.call(W + 'CreateDepositTransaction', dict(deposit, sidechain_id=u32(SLOT), value_sats=u32(0)))
        assert_equal((code, message), (INVALID_ARGUMENT, 'value_sats must be greater than zero'))
        # An unknown method: UNIMPLEMENTED (the protocol's code for it).
        out = subprocess.run([self.grpc.binary, self.grpc.address, '/' + V + 'Nothing'], input='\n', capture_output=True, text=True).stdout
        assert out.strip().startswith(f'STATUS {UNIMPLEMENTED} ')

        self.log.info('MiningService.GenerateToAddress, and the older WalletService.GenerateBlocks (a stream)')
        before = self.node.getblockcount()
        reply = self.grpc.unary(M + 'GenerateToAddress', {'blocks': u32(2), 'address': self.address})
        assert_equal([unhex(h) for h in reply['block_hashes']], [self.node.getblockhash(before + 1), self.node.getblockhash(before + 2)])
        code, _, replies = self.grpc.call(W + 'GenerateBlocks', {'blocks': u32(2)})
        assert_equal(code, OK)
        assert_equal([unhex(r['block_hash']) for r in replies], [self.node.getblockhash(before + 3), self.node.getblockhash(before + 4)])
        balance = self.grpc.unary(W + 'GetBalance')
        assert_greater_than(balance['confirmed_sats'], 0)
        assert_equal(balance['has_synced'], True)
        address = self.grpc.unary(W + 'CreateNewAddress')['address']
        assert self.node.getaddressinfo(address)['ismine']

    def test_security(self):
        self.log.info('The token (-grpcauth): needed for the wallet, the block producer, mining and Stop; not for the validator (as the enforcer\'s)')
        for path in [W + 'CreateNewAddress', B + 'GetBlockProducerState', M + 'GenerateToAddress', V + 'Stop']:
            code, message, _ = self.anonymous.call(path, {})
            assert_equal(code, UNAUTHENTICATED)
        self.anonymous.unary(V + 'GetChainTip')
        wrong = GrpcClient(self.grpc.binary, self.schema, self.grpc.address, ['authorization:Bearer wrong'])
        assert_equal(wrong.call(W + 'CreateNewAddress', {})[0], UNAUTHENTICATED)

        self.log.info('Web pages: a request with an Origin header, or a Host that is a name (DNS rebinding), is refused')
        status, reply = self.connect.json('/' + V + 'GetChainTip', headers={'Origin': 'http://evil.example'})
        assert_equal((status, reply['code']), (403, 'permission_denied'))
        status, reply = self.connect.json('/' + V + 'GetChainTip', headers={'Host': f'evil.example:{self.grpc_port}'})
        assert_equal((status, reply['code']), (403, 'permission_denied'))
        assert_equal(self.connect.json('/' + V + 'GetChainTip', headers={'Host': f'localhost:{self.grpc_port}'})[0], 200)
        # The GET form of Connect, for reads only (idempotency_level NO_SIDE_EFFECTS).
        conn = http.client.HTTPConnection('127.0.0.1', self.grpc_port, timeout=30)
        conn.request('GET', '/' + V + 'GetChainTip?encoding=json&message=%7B%7D')
        resp = conn.getresponse()
        assert_equal(resp.status, 200)
        assert_equal(json.loads(resp.read())['blockHeaderInfo']['blockHash'], {'hex': self.node.getbestblockhash()})
        conn.request('GET', '/' + W + 'CreateNewAddress?encoding=json&message=%7B%7D', headers={'Authorization': f'Bearer {TOKEN}'})
        resp = conn.getresponse()
        assert_equal((resp.status, json.loads(resp.read())['code']), (400, 'invalid_argument'))
        conn.close()

        self.log.info('Defaults: Stop refused without -grpcallowstop; no bind beyond localhost without -grpcallowip and -grpcauth')
        self.restart_node(0, self.base_args)
        code, message, _ = self.grpc.call(V + 'Stop', {})
        assert_equal(code, PERMISSION_DENIED)
        self.anonymous.unary(W + 'CreateNewAddress')  # no token set: no token needed (regtest serves the wallet by default)
        self.stop_node(0)
        self.nodes[0].assert_start_raises_init_error(self.base_args + ['-grpcbind=0.0.0.0'],
                                                     'Error: -grpcbind=0.0.0.0:%d is not a loopback address: that needs -grpcallowip and -grpcauth' % self.grpc_port)
        self.start_node(0, self.extra_args[0])

    def test_stop(self):
        self.log.info('Stop: the node shuts down (grpc.rs:476-490)')
        assert_equal(self.grpc.unary(V + 'Stop'), {})
        self.node.wait_until_stopped()


if __name__ == '__main__':
    EcashGrpcTest(__file__).main()
