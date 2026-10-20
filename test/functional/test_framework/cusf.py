#!/usr/bin/env python3
# Copyright (c) 2026 The Backbone developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Clients of the bip300301_enforcer's interface (cusf.mainchain.v1), which backboned serves (-grpc).

- A protocol buffers codec of its own (not the node's): messages as dicts by field name, the types
  read from the descriptor set the node embeds (src/grpc/descriptors.cpp, compiled by protoc from
  src/grpc/proto). Written canonically (fields by number, packed repeated scalars, proto3 defaults
  left out); read leniently. Wrappers stay messages: {"value": ...}.
- GrpcClient: gRPC over HTTP/2 through backbone-grpc-client, which is gRPC's own C++ library
  (grpc++), so an implementation of the protocol other than the server's.
- ConnectClient: the Connect protocol over HTTP/1.1 (JSON or binary), as the enforcer's other
  clients (the DATUM pool, the Dart console) use, with http.client.
"""
import http.client
import json
import os
import re
import select
import subprocess
import time

# Field types (google/protobuf/descriptor.proto FieldDescriptorProto.Type).
DOUBLE, FLOAT, INT64, UINT64, INT32, FIXED64, FIXED32, BOOL, STRING, GROUP, MESSAGE, BYTES, UINT32, ENUM, SFIXED32, SFIXED64, SINT32, SINT64 = range(1, 19)
VARINT_TYPES = {INT64, UINT64, INT32, BOOL, UINT32, ENUM, SINT32, SINT64}
FIXED64_TYPES = {DOUBLE, FIXED64, SFIXED64}
FIXED32_TYPES = {FLOAT, FIXED32, SFIXED32}

# gRPC status codes.
OK, CANCELLED, UNKNOWN, INVALID_ARGUMENT, DEADLINE_EXCEEDED, NOT_FOUND, ALREADY_EXISTS, PERMISSION_DENIED, \
    RESOURCE_EXHAUSTED, FAILED_PRECONDITION, ABORTED, OUT_OF_RANGE, UNIMPLEMENTED, INTERNAL, UNAVAILABLE, DATA_LOSS, UNAUTHENTICATED = range(17)


def read_varint(data, pos):
    value, shift = 0, 0
    while True:
        b = data[pos]
        pos += 1
        value |= (b & 0x7f) << shift
        if not b & 0x80:
            return value, pos
        shift += 7


def varint(value):
    if value < 0:
        value += 1 << 64
    out = bytearray()
    while value >= 0x80:
        out.append((value & 0x7f) | 0x80)
        value >>= 7
    out.append(value)
    return bytes(out)


def fields_of(data):
    """(number, wire type, value) of each field of a message: an int, or bytes for LEN."""
    pos = 0
    while pos < len(data):
        tag, pos = read_varint(data, pos)
        number, wire = tag >> 3, tag & 7
        if wire == 0:
            value, pos = read_varint(data, pos)
        elif wire == 1:
            value, pos = int.from_bytes(data[pos:pos + 8], 'little'), pos + 8
        elif wire == 5:
            value, pos = int.from_bytes(data[pos:pos + 4], 'little'), pos + 4
        elif wire == 2:
            size, pos = read_varint(data, pos)
            value, pos = data[pos:pos + size], pos + size
        else:
            raise ValueError("unsupported wire type %d" % wire)
        yield number, wire, value


class Field:
    def __init__(self, name, number, label, ftype, type_name, oneof, proto3_optional):
        self.name, self.number, self.type, self.type_name = name, number, ftype, type_name.lstrip('.')
        self.repeated = label == 3
        self.presence = proto3_optional or oneof is not None or ftype == MESSAGE
        self.packable = self.repeated and ftype not in (STRING, BYTES, MESSAGE, GROUP)


class Schema:
    """The message types and services of a FileDescriptorSet, read here (not with the node's code)."""

    def __init__(self, data):
        self.messages = {}
        self.services = {}
        for number, _, file_bytes in fields_of(data):
            if number == 1:
                self._file(file_bytes)

    @classmethod
    def from_source(cls, path):
        """The set embedded in src/grpc/descriptors.cpp."""
        text = open(path, encoding='utf8').read()
        body = text[text.index('DESCRIPTOR_SET[] = {'):text.index('};')]
        return cls(bytes(int(h, 16) for h in re.findall(r'0x([0-9a-f]{2})', body)))

    def _file(self, data):
        package = ''
        items = list(fields_of(data))
        for number, _, value in items:
            if number == 2:
                package = value.decode()
        for number, _, value in items:
            if number == 4:
                self._message(value, package)
            elif number == 6:
                self._service(value, package)

    def _message(self, data, scope):
        name, fields, raw = '', [], []
        for number, _, value in fields_of(data):
            if number == 1:
                name = value.decode()
            elif number == 2:
                raw.append(value)
            elif number == 3:
                raw.append(('nested', value))
        full = scope + '.' + name if scope else name
        for item in raw:
            if isinstance(item, tuple):
                self._message(item[1], full)
                continue
            f = {'name': '', 'number': 0, 'label': 1, 'type': 0, 'type_name': '', 'oneof': None, 'p3': False}
            for number, _, value in fields_of(item):
                if number == 1:
                    f['name'] = value.decode()
                elif number == 3:
                    f['number'] = value
                elif number == 4:
                    f['label'] = value
                elif number == 5:
                    f['type'] = value
                elif number == 6:
                    f['type_name'] = value.decode()
                elif number == 9:
                    f['oneof'] = value
                elif number == 17:
                    f['p3'] = bool(value)
            fields.append(Field(f['name'], f['number'], f['label'], f['type'], f['type_name'], f['oneof'], f['p3']))
        self.messages[full] = sorted(fields, key=lambda x: x.number)

    def _service(self, data, package):
        name, methods = '', {}
        for number, _, value in fields_of(data):
            if number == 1:
                name = value.decode()
            elif number == 2:
                m = {}
                for n, _, v in fields_of(value):
                    if n in (1, 2, 3):
                        m[n] = v.decode().lstrip('.')
                    elif n == 6:
                        m['stream'] = bool(v)
                methods[m[1]] = (m[2], m[3], m.get('stream', False))
        self.services[package + '.' + name] = methods

    def method(self, path):
        """(request type, reply type, server streaming) of '/package.Service/Method'."""
        service, method = path.strip('/').split('/')
        return self.services[service][method]

    def encode(self, type_name, obj):
        out = bytearray()
        for f in self.messages[type_name]:
            if f.name not in obj:
                continue
            values = obj[f.name] if f.repeated else [obj[f.name]]
            if f.packable:
                payload = b''.join(self._scalar(f, v) for v in values)
                out += varint(f.number << 3 | 2) + varint(len(payload)) + payload
                continue
            for v in values:
                if f.type == MESSAGE:
                    payload = self.encode(f.type_name, v)
                    out += varint(f.number << 3 | 2) + varint(len(payload)) + payload
                elif f.type in (STRING, BYTES):
                    b = v.encode() if isinstance(v, str) else v
                    if not f.presence and not f.repeated and not b:
                        continue
                    out += varint(f.number << 3 | 2) + varint(len(b)) + b
                else:
                    if not f.presence and not f.repeated and not v:
                        continue
                    wire = 1 if f.type in FIXED64_TYPES else 5 if f.type in FIXED32_TYPES else 0
                    out += varint(f.number << 3 | wire) + self._scalar(f, v)
        return bytes(out)

    @staticmethod
    def _scalar(f, v):
        if f.type in FIXED64_TYPES:
            return (v % (1 << 64)).to_bytes(8, 'little')
        if f.type in FIXED32_TYPES:
            return (v % (1 << 32)).to_bytes(4, 'little')
        if f.type == SINT32 or f.type == SINT64:
            return varint((v << 1) ^ (v >> 63))
        return varint(int(v))

    def decode(self, type_name, data):
        by_number = {f.number: f for f in self.messages[type_name]}
        obj = {}
        for number, wire, value in fields_of(data):
            f = by_number.get(number)
            if f is None:
                continue
            if f.packable and wire == 2:
                vals, pos = [], 0
                while pos < len(value):
                    v, pos = read_varint(value, pos)
                    vals.append(v)
                obj.setdefault(f.name, []).extend(vals)
                continue
            if f.type == MESSAGE:
                v = self.decode(f.type_name, value)
            elif f.type == STRING:
                v = value.decode()
            elif f.type == BYTES:
                v = bytes(value)
            elif f.type in (INT32, INT64) and value >= 1 << 63:
                v = value - (1 << 64)
            elif f.type == BOOL:
                v = bool(value)
            else:
                v = value
            if f.repeated:
                obj.setdefault(f.name, []).append(v)
            else:
                obj[f.name] = v
        return obj


def rh(hex_string):
    """A ConsensusHex, Hex or ReverseHex: {"hex": StringValue}."""
    return {'hex': {'value': hex_string}}


def unhex(wrapper):
    return wrapper['hex']['value']


def u32(value):
    """A UInt32Value or UInt64Value; 0 is not written (proto3), so it reads back as {}."""
    return {'value': value} if value else {}


class GrpcClient:
    """gRPC over HTTP/2, with backbone-grpc-client (grpc++)."""

    def __init__(self, binary, schema, address, headers=()):
        self.binary, self.schema, self.address = binary, schema, address
        self.headers = list(headers)

    def args(self, path, timeout):
        out = [self.binary, self.address, '/' + path.lstrip('/'), '--timeout', str(timeout)]
        for h in self.headers:
            out += ['--header', h]
        return out

    def call(self, path, request=None, raw=None, timeout=60):
        """(status code, status message, replies) of a call with one request."""
        req_type, reply_type, _ = self.schema.method(path)
        payload = raw if raw is not None else self.schema.encode(req_type, request or {})
        out = subprocess.run(self.args(path, timeout), input=payload.hex() + '\n',
                             capture_output=True, text=True, timeout=timeout + 30).stdout
        return self._parse(out, reply_type)

    def call_raw_lines(self, path, payloads, timeout=60):
        """A call with several requests (a client stream: server reflection)."""
        _, reply_type, _ = self.schema.method(path)
        out = subprocess.run(self.args(path, timeout), input=''.join(p.hex() + '\n' for p in payloads),
                             capture_output=True, text=True, timeout=timeout + 30).stdout
        return self._parse(out, reply_type)

    def _parse(self, out, reply_type):
        replies, code, message = [], None, ''
        for line in out.splitlines():
            if line.startswith('MSG '):
                replies.append(self.schema.decode(reply_type, bytes.fromhex(line[4:])))
            elif line.startswith('STATUS '):
                parts = line.split(' ', 2)
                code, message = int(parts[1]), parts[2] if len(parts) > 2 else ''
        assert code is not None, 'no status from the gRPC client: ' + out
        return code, message, replies

    def unary(self, path, request=None):
        code, message, replies = self.call(path, request)
        assert code == OK, 'gRPC %s: %d %s' % (path, code, message)
        assert len(replies) == 1
        return replies[0]

    def subscribe(self, path, request, timeout=600):
        return Stream(self, path, request, timeout)


class Stream:
    """A server stream: the client runs in the background; next() waits for each reply."""

    def __init__(self, client, path, request, timeout):
        req_type, self.reply_type, _ = client.schema.method(path)
        self.schema = client.schema
        self.proc = subprocess.Popen(client.args(path, timeout), stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE)
        self.proc.stdin.write((client.schema.encode(req_type, request).hex() + '\n').encode())
        self.proc.stdin.close()
        self.status = None
        # Lines are read from the pipe here, not through a buffered reader whose buffer select() does not see.
        self.buffer = b''
        self.eof = False

    def _line(self, timeout):
        deadline = time.time() + timeout
        while b'\n' not in self.buffer and not self.eof:
            ready, _, _ = select.select([self.proc.stdout], [], [], max(0, deadline - time.time()))
            if not ready:
                raise AssertionError('no reply in %d s' % timeout)
            chunk = os.read(self.proc.stdout.fileno(), 65536)
            if not chunk:
                self.eof = True
            self.buffer += chunk
        if b'\n' not in self.buffer:
            return None
        line, self.buffer = self.buffer.split(b'\n', 1)
        return line.decode()

    def next(self, timeout=30):
        """The next reply, or None if the stream ended (then .status is set)."""
        while True:
            line = self._line(timeout)
            if line is None:
                return None
            if line.startswith('MSG '):
                return self.schema.decode(self.reply_type, bytes.fromhex(line[4:]))
            if line.startswith('STATUS '):
                parts = line.split(' ', 2)
                self.status = (int(parts[1]), parts[2] if len(parts) > 2 else '')
                return None

    def nothing_within(self, seconds):
        if self.buffer:
            return False
        ready, _, _ = select.select([self.proc.stdout], [], [], seconds)
        return not ready

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            self.proc.wait(10)
        self.proc.stdout.close()


class ConnectClient:
    """The Connect protocol, unary, over HTTP/1.1."""

    def __init__(self, schema, host, port):
        self.schema, self.host, self.port = schema, host, port

    def post(self, path, body, content_type, headers=None):
        conn = http.client.HTTPConnection(self.host, self.port, timeout=60)
        conn.request('POST', path, body=body, headers=dict({'Content-Type': content_type, 'Connect-Protocol-Version': '1'}, **(headers or {})))
        resp = conn.getresponse()
        data = resp.read()
        conn.close()
        return resp.status, resp.getheader('Content-Type'), data

    def json(self, path, obj=None, headers=None):
        status, _, data = self.post(path, json.dumps(obj or {}), 'application/json', headers)
        return status, json.loads(data)

    def proto(self, path, request=None):
        req_type, reply_type, _ = self.schema.method(path)
        status, ctype, data = self.post(path, self.schema.encode(req_type, request or {}), 'application/proto')
        return status, (self.schema.decode(reply_type, data) if ctype == 'application/proto' else json.loads(data))


def descriptor_source(config):
    return os.path.join(config['environment']['SRCDIR'], 'src', 'grpc', 'descriptors.cpp')


def client_binary(config):
    return os.path.join(config['environment']['BUILDDIR'], 'bin', 'backbone-grpc-client')
