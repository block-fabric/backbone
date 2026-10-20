// Copyright (c) 2026 The Backbone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#ifdef ENABLE_GRPC

#include <grpc/descriptors.h>
#include <grpc/protobuf.h>
#include <grpc/server.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <nghttp2/nghttp2.h>

#include <boost/test/unit_test.hpp>

#include <map>
#include <thread>

using pb::Message;

namespace {

const pb::Pool& Pool()
{
    static const pb::Pool pool{[] {
        pb::Pool p;
        std::string error;
        if (!p.Load(grpcsrv::DescriptorSet(), error)) throw std::runtime_error(error);
        return p;
    }()};
    return pool;
}

Message New(const std::string& name) { return Message{*Assert(Pool().FindMessage(name))}; }

std::string Hex(const Message& m) { return HexStr(m.Serialize()); }

void SetWrapped(Message& m, const std::string& field, const std::string& hex) { m.Mutable(field).Mutable("hex").SetString("value", hex); }

/** A gRPC client over HTTP/2 (nghttp2's client), wired to a Connection in memory. */
struct H2Client {
    grpcsrv::Connection connection;
    nghttp2_session* session{nullptr};
    struct Response {
        std::map<std::string, std::string> headers;
        std::string body;
        bool closed{false};
        uint32_t error_code{0};
    };
    std::map<int32_t, Response> responses;
    std::map<int32_t, std::string> bodies;
    std::map<int32_t, size_t> sent;

    explicit H2Client(grpcsrv::Server& server) : connection{server}
    {
        nghttp2_session_callbacks* cb;
        nghttp2_session_callbacks_new(&cb);
        nghttp2_session_callbacks_set_on_header_callback(cb, [](nghttp2_session*, const nghttp2_frame* frame, const uint8_t* name, size_t namelen, const uint8_t* value, size_t valuelen, uint8_t, void* user) {
            static_cast<H2Client*>(user)->responses[frame->hd.stream_id].headers[std::string(reinterpret_cast<const char*>(name), namelen)] = std::string(reinterpret_cast<const char*>(value), valuelen);
            return 0;
        });
        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cb, [](nghttp2_session*, uint8_t, int32_t id, const uint8_t* data, size_t len, void* user) {
            static_cast<H2Client*>(user)->responses[id].body.append(reinterpret_cast<const char*>(data), len);
            return 0;
        });
        nghttp2_session_callbacks_set_on_stream_close_callback(cb, [](nghttp2_session*, int32_t id, uint32_t error_code, void* user) {
            auto& r{static_cast<H2Client*>(user)->responses[id]};
            r.closed = true;
            r.error_code = error_code;
            return 0;
        });
        nghttp2_session_client_new(&session, cb, this);
        nghttp2_session_callbacks_del(cb);
        nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, nullptr, 0);
    }
    ~H2Client() { nghttp2_session_del(session); }

    /** Exchange bytes until neither side has anything more to say. */
    void Pump(int rounds = 50)
    {
        for (int i{0}; i < rounds; ++i) {
            const uint8_t* data;
            std::string out;
            for (ssize_t n; (n = nghttp2_session_mem_send(session, &data)) > 0;) out.append(reinterpret_cast<const char*>(data), n);
            if (!out.empty()) connection.Receive(MakeUCharSpan(out));
            connection.Poll();
            const std::string in{connection.TakeOutput()};
            if (!in.empty()) nghttp2_session_mem_recv(session, reinterpret_cast<const uint8_t*>(in.data()), in.size());
            if (out.empty() && in.empty()) {
                // Handlers run on the server's threads: give them a moment.
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
            }
        }
    }

    int32_t Call(const std::string& path, const std::string& body, const std::string& content_type = "application/grpc")
    {
        const std::vector<std::pair<std::string, std::string>> headers{
            {":method", "POST"}, {":scheme", "http"}, {":authority", "test"}, {":path", path}, {"content-type", content_type}, {"te", "trailers"}};
        std::vector<nghttp2_nv> nvs;
        for (const auto& [n, v] : headers) {
            nvs.push_back({reinterpret_cast<uint8_t*>(const_cast<char*>(n.data())), reinterpret_cast<uint8_t*>(const_cast<char*>(v.data())), n.size(), v.size(), NGHTTP2_NV_FLAG_NONE});
        }
        nghttp2_data_provider provider;
        provider.source.ptr = this;
        provider.read_callback = [](nghttp2_session*, int32_t id, uint8_t* buf, size_t length, uint32_t* flags, nghttp2_data_source* source, void*) -> ssize_t {
            auto& self{*static_cast<H2Client*>(source->ptr)};
            const std::string& body{self.bodies[id]};
            size_t& done{self.sent[id]};
            const size_t n{std::min(length, body.size() - done)};
            std::memcpy(buf, body.data() + done, n);
            done += n;
            if (done == body.size()) *flags |= NGHTTP2_DATA_FLAG_EOF;
            return static_cast<ssize_t>(n);
        };
        // The id is the next one the session gives: the body is set before the provider reads it.
        const int32_t id{nghttp2_session_get_next_stream_id(session)};
        bodies[id] = body;
        BOOST_REQUIRE_EQUAL(nghttp2_submit_request(session, nullptr, nvs.data(), nvs.size(), &provider, nullptr), id);
        return id;
    }
};

std::string Envelope(const std::string& payload, uint8_t flags = 0)
{
    std::string out(1, static_cast<char>(flags));
    for (int s{24}; s >= 0; s -= 8) out.push_back(static_cast<char>((payload.size() >> s) & 0xff));
    return out + payload;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(grpc_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(descriptors)
{
    // The services and methods of the enforcer at 17eb2ce (lib/server, app/main.rs:726-797), plus ours.
    const auto& pool{Pool()};
    const std::map<std::string, size_t> methods{
        {"cusf.mainchain.v1.ValidatorService", 15}, // 14 at 17eb2ce, and GetSeenBmmRequests (master)
        {"cusf.mainchain.v1.WalletService", 14},    // 13, and the older GenerateBlocks
        {"cusf.mainchain.v1.BlockProducerService", 8},
        {"cusf.mainchain.v1.MiningService", 1},
        {"cusf.crypto.v1.CryptoService", 5},
        {"grpc.health.v1.Health", 2},
        {"grpc.reflection.v1.ServerReflection", 1},
        {"grpc.reflection.v1alpha.ServerReflection", 1},
    };
    for (const auto& [service, count] : methods) {
        const pb::ServiceDesc* s{pool.FindService(service)};
        BOOST_REQUIRE_MESSAGE(s, service);
        BOOST_CHECK_EQUAL(s->methods.size(), count);
    }
    const pb::ServiceDesc& validator{*pool.FindService("cusf.mainchain.v1.ValidatorService")};
    for (const pb::MethodDesc& m : validator.methods) {
        BOOST_CHECK_EQUAL(m.server_streaming, m.name == "SubscribeEvents" || m.name == "SubscribeHeaderSyncProgress");
        BOOST_CHECK(!m.client_streaming);
    }
    // Reflection: a symbol's file, and the files it needs, itself first.
    BOOST_CHECK_EQUAL(*pool.FileOfSymbol("cusf.mainchain.v1.BlockInfo.Event"), "cusf/mainchain/v1/validator.proto");
    BOOST_CHECK_EQUAL(*pool.FileOfSymbol("cusf.mainchain.v1.ValidatorService.GetChainTip"), "cusf/mainchain/v1/validator.proto");
    BOOST_CHECK_EQUAL(*pool.FileOfSymbol("cusf.mainchain.v1.Network"), "cusf/mainchain/v1/validator.proto");
    const auto files{pool.FileWithDependencies("cusf/mainchain/v1/validator.proto")};
    BOOST_CHECK_EQUAL(files.front(), "cusf/mainchain/v1/validator.proto");
    BOOST_CHECK(std::find(files.begin(), files.end(), "google/protobuf/wrappers.proto") != files.end());
    BOOST_CHECK(std::find(files.begin(), files.end(), "cusf/common/v1/common.proto") != files.end());
    // Field facts the wire depends on.
    const pb::MessageDesc& proposal{*pool.FindMessage("cusf.mainchain.v1.GetSidechainProposalsResponse.SidechainProposal")};
    BOOST_CHECK(proposal.Field(7)->explicit_presence);  // optional SidechainDeclaration declaration = 7
    BOOST_CHECK_EQUAL(proposal.Field(7)->json_name, "declaration");
    const pb::MessageDesc& header_req{*pool.FindMessage("cusf.mainchain.v1.GetBlockHeaderInfoRequest")};
    BOOST_CHECK(header_req.Field(2)->explicit_presence); // optional uint32 max_ancestors = 2
    BOOST_CHECK_EQUAL(header_req.Field(2)->oneof_index, -1);
    BOOST_CHECK(pool.FindMessage("cusf.mainchain.v1.GetInfoResponse.DescriptorsEntry")->map_entry);
}

BOOST_AUTO_TEST_CASE(binary_encoding)
{
    // The hex wrappers: ReverseHex { StringValue hex = 1 { string value = 1 } }.
    Message tip{New("cusf.mainchain.v1.GetChainTipResponse")};
    Message& info{tip.Mutable("block_header_info")};
    SetWrapped(info, "block_hash", "ab");
    BOOST_CHECK_EQUAL(Hex(info), "0a060a040a026162");
    // proto3: a scalar at its default is not written; set, it is, in field number order.
    info.SetUInt("height", 0);
    BOOST_CHECK_EQUAL(Hex(info), "0a060a040a026162");
    info.SetUInt("timestamp", 1);
    info.SetUInt("height", 300);
    BOOST_CHECK_EQUAL(Hex(info), "0a060a040a02616218ac022801");
    BOOST_CHECK_EQUAL(Hex(tip), "0a0d0a060a040a02616218ac022801");
    // An empty wrapper is written (its presence counts); its empty string is not.
    Message hex{New("cusf.common.v1.Hex")};
    hex.Mutable("hex").SetString("value", "");
    BOOST_CHECK_EQUAL(Hex(hex), "0a00");
    // A proto3 optional scalar is written when set, even to 0 (max_ancestors = 2).
    Message req{New("cusf.mainchain.v1.GetBlockHeaderInfoRequest")};
    req.SetUInt("max_ancestors", 0);
    BOOST_CHECK_EQUAL(Hex(req), "1000");
    // Packed repeated scalars.
    Message upvotes{New("cusf.mainchain.v1.GetCoinbasePSBTRequest.AckBundles.Upvotes")};
    upvotes.AddUInt("upvotes", 1);
    upvotes.AddUInt("upvotes", 300);
    BOOST_CHECK_EQUAL(Hex(upvotes), "0a0301ac02");
    // Fields by number, not by declaration: SidechainProposal declares 7 (declaration) before 3.
    Message p{New("cusf.mainchain.v1.GetSidechainProposalsResponse.SidechainProposal")};
    p.Mutable("declaration");
    p.Mutable("proposal_age").SetUInt("value", 2);
    p.Mutable("sidechain_number").SetUInt("value", 1);
    BOOST_CHECK_EQUAL(Hex(p), "0a020801320208023a00");
    // A oneof: setting one member clears the other.
    Message ev{New("cusf.mainchain.v1.SubscribeEventsResponse.Event")};
    ev.Mutable("connect_block");
    SetWrapped(ev.Mutable("disconnect_block"), "block_hash", "00");
    BOOST_CHECK(!ev.Has("connect_block"));
    BOOST_CHECK_EQUAL(ev.WhichOneof("event")->name, "disconnect_block");
    BOOST_CHECK_EQUAL(Hex(ev), "12080a060a040a023030");
    // Enums, and a negative int32 (ten bytes, as protobuf writes it).
    Message chain_info{New("cusf.mainchain.v1.GetChainInfoResponse")};
    chain_info.SetUInt("network", 3);
    BOOST_CHECK_EQUAL(Hex(chain_info), "0803");
    Message err{New("grpc.reflection.v1.ErrorResponse")};
    err.SetInt("error_code", -1);
    BOOST_CHECK_EQUAL(Hex(err), "08ffffffffffffffffff01");
    // A map: entries as messages (key = 1, value = 2).
    Message gi{New("cusf.mainchain.v1.GetInfoResponse")};
    Message& e{gi.Add("descriptors")};
    e.SetString("key", "external");
    e.SetString("value", "wpkh()");
    BOOST_CHECK_EQUAL(Hex(gi), "2212" "0a0865787465726e616c" "120677706b682829");
}

BOOST_AUTO_TEST_CASE(binary_decoding)
{
    std::string error;
    // Unknown fields are skipped; a repeated scalar unpacked or packed; the last value of a scalar wins.
    Message up{New("cusf.mainchain.v1.GetCoinbasePSBTRequest.AckBundles.Upvotes")};
    BOOST_REQUIRE(up.Parse(ParseHex("0801" "0a0102" "9806 07" "0803"), error));
    BOOST_CHECK_EQUAL(up.Count("upvotes"), 3U);
    BOOST_CHECK_EQUAL(up.UIntAt("upvotes", 1), 2U);
    BOOST_CHECK_EQUAL(up.UIntAt("upvotes", 2), 3U);
    Message info{New("cusf.mainchain.v1.BlockHeaderInfo")};
    BOOST_REQUIRE(info.Parse(ParseHex("1801" "1802"), error));
    BOOST_CHECK_EQUAL(info.GetUInt("height"), 2U);
    // A message field seen twice is merged.
    BOOST_REQUIRE(info.Parse(ParseHex("0a060a040a026162" "0a00"), error));
    BOOST_CHECK_EQUAL(info.GetMessage("block_hash")->GetMessage("hex")->GetString("value"), "ab");
    // Malformed: truncated, a wire type that does not fit, invalid UTF-8 in a string.
    Message bad{New("cusf.mainchain.v1.BlockHeaderInfo")};
    BOOST_CHECK(!bad.Parse(ParseHex("0a05"), error));
    BOOST_CHECK(!bad.Parse(ParseHex("1a00"), error));
    Message s{New("google.protobuf.StringValue")};
    BOOST_CHECK(!s.Parse(ParseHex("0a01ff"), error));
    BOOST_CHECK(s.Parse(ParseHex("0a02c3a9"), error));
    // Round trip.
    Message tip{New("cusf.mainchain.v1.GetChainTipResponse")};
    BOOST_REQUIRE(tip.Parse(ParseHex("0a0d0a060a040a02616218ac022801"), error));
    BOOST_CHECK_EQUAL(Hex(tip), "0a0d0a060a040a02616218ac022801");
}

BOOST_AUTO_TEST_CASE(json_mapping)
{
    // The proto3 JSON mapping, as the enforcer's Connect server writes it (buffa json): lowerCamelCase
    // names, wrappers as their values, 64 bit integers as strings, enums by name, defaults left out.
    Message resp{New("cusf.mainchain.v1.GetChainInfoResponse")};
    resp.SetUInt("network", 2);
    resp.Mutable("bip300_constants").SetUInt("withdrawal_bundle_max_age", 26300);
    BOOST_CHECK_EQUAL(resp.ToJson().write(), R"({"network":"NETWORK_MAINNET","bip300Constants":{"withdrawalBundleMaxAge":26300}})");
    Message d{New("cusf.mainchain.v1.Deposit")};
    d.Mutable("sequence_number").SetUInt("value", 18446744073709551615ULL);
    d.Mutable("output").Mutable("address").Mutable("hex").SetString("value", "6869");
    BOOST_CHECK_EQUAL(d.ToJson().write(), R"({"sequenceNumber":"18446744073709551615","output":{"address":{"hex":"6869"}}})");
    Message b{New("cusf.mainchain.v1.ProposeWithdrawalBundleRequest")};
    b.Mutable("transaction").SetString("value", std::string("\x00\xff", 2));
    BOOST_CHECK_EQUAL(b.ToJson().write(), R"({"transaction":"AP8="})");
    Message t{New("google.protobuf.Timestamp")};
    t.SetInt("seconds", 1700000000);
    BOOST_CHECK_EQUAL(t.ToJson().write(), R"("2023-11-14T22:13:20Z")");
    t.SetInt("nanos", 5000000);
    BOOST_CHECK_EQUAL(t.ToJson().write(), R"("2023-11-14T22:13:20.005Z")");

    // And read: either name, numbers as strings or numbers, enum names or numbers, base64 either alphabet.
    std::string error;
    UniValue json;
    BOOST_REQUIRE(json.read(R"({"sidechain_id":7,"maxAncestors":"0","blockHash":{"hex":"aa"}})"));
    Message req{New("cusf.mainchain.v1.GetBlockInfoRequest")};
    BOOST_REQUIRE_MESSAGE(req.FromJson(json, error), error);
    BOOST_CHECK_EQUAL(req.GetMessage("sidechain_id")->GetUInt("value"), 7U);
    BOOST_CHECK(req.Has("max_ancestors"));
    BOOST_CHECK_EQUAL(Hex(req), "0a060a040a0261611202080718" "00");
    BOOST_REQUIRE(json.read(R"({"policy":"WITHDRAWAL_BUNDLE_POLICY_ALARM"})"));
    Message pol{New("cusf.mainchain.v1.SetWithdrawalBundlePolicyRequest")};
    BOOST_REQUIRE(pol.FromJson(json, error));
    BOOST_CHECK_EQUAL(pol.GetUInt("policy"), 4U);
    BOOST_REQUIRE(json.read(R"({"transaction":"AP8"})"));
    Message b2{New("cusf.mainchain.v1.ProposeWithdrawalBundleRequest")};
    BOOST_REQUIRE(b2.FromJson(json, error));
    BOOST_CHECK(b2 == b);
    BOOST_REQUIRE(json.read(R"({"maxAncestors":-1})"));
    Message neg{New("cusf.mainchain.v1.GetBlockHeaderInfoRequest")};
    BOOST_CHECK(!neg.FromJson(json, error));
    BOOST_REQUIRE(json.read(R"({"seconds":"1"})"));
    Message rt{New("google.protobuf.Timestamp")};
    BOOST_CHECK(rt.FromJson(UniValue{"2023-11-14T22:13:20.005Z"}, error));
    BOOST_CHECK(rt == t);
}

BOOST_AUTO_TEST_CASE(transport)
{
    grpcsrv::Server server{Pool()};
    // Without Start, handlers run on the connection's thread: the test is deterministic.
    std::shared_ptr<grpcsrv::Writer> stream_writer;
    BOOST_REQUIRE(server.Register("cusf.crypto.v1.CryptoService", "Ripemd160", [](const Message& req, const std::shared_ptr<grpcsrv::Writer>& w) {
        if (!req.GetMessage("msg")) return w->Finish({grpcsrv::Code::INVALID_ARGUMENT, "Missing field: `msg` 100%"});
        Message resp{New("cusf.crypto.v1.Ripemd160Response")};
        SetWrapped(resp, "digest", "00");
        w->Write(resp);
        w->Finish({});
    }));
    BOOST_REQUIRE(server.Register("cusf.mainchain.v1.ValidatorService", "SubscribeEvents", [&](const Message&, const std::shared_ptr<grpcsrv::Writer>& w) { stream_writer = w; }));
    BOOST_CHECK(!server.Register("cusf.mainchain.v1.ValidatorService", "NoSuchMethod", {}));

    {
        H2Client client{server};
        Message req{New("cusf.crypto.v1.Ripemd160Request")};
        SetWrapped(req, "msg", "");
        const int32_t ok{client.Call("/cusf.crypto.v1.CryptoService/Ripemd160", Envelope(req.Serialize()))};
        const int32_t err{client.Call("/cusf.crypto.v1.CryptoService/Ripemd160", Envelope(""))};
        const int32_t unknown{client.Call("/cusf.crypto.v1.CryptoService/Nope", Envelope(""))};
        const int32_t compressed{client.Call("/cusf.crypto.v1.CryptoService/Ripemd160", Envelope("", 1))};
        const int32_t two{client.Call("/cusf.crypto.v1.CryptoService/Ripemd160", Envelope("") + Envelope(""))};
        client.Pump();
        // A reply: headers, the message, then the status in the trailers.
        const auto& r{client.responses[ok]};
        BOOST_CHECK(r.closed);
        BOOST_CHECK_EQUAL(r.headers.at(":status"), "200");
        BOOST_CHECK_EQUAL(r.headers.at("content-type"), "application/grpc");
        BOOST_CHECK_EQUAL(r.headers.at("grpc-status"), "0");
        BOOST_CHECK_EQUAL(HexStr(r.body), "0000000008" "0a060a040a023030");
        // An error: trailers only, the message percent-encoded.
        const auto& e{client.responses[err]};
        BOOST_CHECK_EQUAL(e.headers.at("grpc-status"), "3");
        BOOST_CHECK_EQUAL(e.headers.at("grpc-message"), "Missing field: `msg` 100%25");
        BOOST_CHECK(e.body.empty());
        BOOST_CHECK_EQUAL(client.responses[unknown].headers.at("grpc-status"), "12");
        BOOST_CHECK_EQUAL(client.responses[compressed].headers.at("grpc-status"), "12");
        BOOST_CHECK_EQUAL(client.responses[two].headers.at("grpc-status"), "13");

        // A server stream: replies as they are written, from another thread, until it is finished.
        const int32_t sub{client.Call("/cusf.mainchain.v1.ValidatorService/SubscribeEvents", Envelope(""))};
        client.Pump(5);
        BOOST_REQUIRE(stream_writer);
        Message ev{New("cusf.mainchain.v1.SubscribeEventsResponse")};
        ev.Mutable("event").Mutable("disconnect_block");
        std::thread{[&] { stream_writer->Write(ev); }}.join();
        client.Pump(5);
        BOOST_CHECK_EQUAL(HexStr(client.responses[sub].body), "0000000004" "0a021200");
        BOOST_CHECK(!client.responses[sub].closed);
        stream_writer->Write(ev);
        stream_writer->Finish({grpcsrv::Code::UNKNOWN, "Events stream closed due to overflow"});
        client.Pump(5);
        BOOST_CHECK(client.responses[sub].closed);
        BOOST_CHECK_EQUAL(client.responses[sub].body.size(), 18U);
        BOOST_CHECK_EQUAL(client.responses[sub].headers.at("grpc-status"), "2");

        // A client that cancels: the writer is closed, its callback called.
        const int32_t sub2{client.Call("/cusf.mainchain.v1.ValidatorService/SubscribeEvents", Envelope(""))};
        client.Pump(5);
        bool closed{false};
        stream_writer->OnClose([&] { closed = true; });
        nghttp2_submit_rst_stream(client.session, NGHTTP2_FLAG_NONE, sub2, NGHTTP2_CANCEL);
        client.Pump(5);
        BOOST_CHECK(closed);
        BOOST_CHECK(stream_writer->Closed());
        BOOST_CHECK(!stream_writer->Write(ev));

        // Connect over HTTP/2: a unary call with a JSON body.
        const int32_t cj{client.Call("/cusf.crypto.v1.CryptoService/Ripemd160", R"({"msg":{"hex":""}})", "application/json")};
        client.Pump(5);
        BOOST_CHECK_EQUAL(client.responses[cj].headers.at(":status"), "200");
        BOOST_CHECK_EQUAL(client.responses[cj].body, R"({"digest":{"hex":"00"}})");
    }

    // HTTP/1.1: Connect unary (proto and JSON, an error with its HTTP status) and gRPC-Web.
    {
        grpcsrv::Connection c{server};
        const auto exchange{[&](const std::string& request) {
            c.Receive(MakeUCharSpan(request));
            c.Poll();
            return c.TakeOutput();
        }};
        std::string out{exchange("POST /cusf.crypto.v1.CryptoService/Ripemd160 HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}")};
        BOOST_CHECK(out.starts_with("HTTP/1.1 400 "));
        BOOST_CHECK(out.find(R"({"code":"invalid_argument","message":"Missing field: `msg` 100%"})") != std::string::npos);
        Message req{New("cusf.crypto.v1.Ripemd160Request")};
        SetWrapped(req, "msg", "");
        const std::string body{req.Serialize()};
        out = exchange(strprintf("POST /cusf.crypto.v1.CryptoService/Ripemd160 HTTP/1.1\r\nContent-Type: application/proto\r\nContent-Length: %u\r\n\r\n", body.size()) + body);
        BOOST_CHECK(out.starts_with("HTTP/1.1 200 OK\r\nContent-Type: application/proto\r\nContent-Length: 8\r\n"));
        BOOST_CHECK(out.ends_with(std::string{"\x0a\x06\x0a\x04\x0a\x02\x30\x30"}));
        const std::string web{Envelope(body)};
        out = exchange(strprintf("POST /cusf.crypto.v1.CryptoService/Ripemd160 HTTP/1.1\r\nContent-Type: application/grpc-web+proto\r\nContent-Length: %u\r\n\r\n", web.size()) + web);
        BOOST_CHECK(out.starts_with("HTTP/1.1 200 OK\r\nContent-Type: application/grpc-web+proto\r\nTransfer-Encoding: chunked\r\n"));
        BOOST_CHECK(out.find(Envelope(std::string{"\x0a\x06\x0a\x04\x0a\x02\x30\x30"}) + Envelope("grpc-status: 0\r\n", 0x80)) != std::string::npos);
        out = exchange("POST /nope HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 0\r\n\r\n");
        BOOST_CHECK(out.starts_with("HTTP/1.1 501 "));
        // Connect's GET form, for a method without side effects.
        out = exchange("GET /cusf.crypto.v1.CryptoService/Ripemd160?encoding=json&message=%7B%22msg%22%3A%7B%22hex%22%3A%22%22%7D%7D HTTP/1.1\r\n\r\n");
        BOOST_CHECK(out.starts_with("HTTP/1.1 200 OK"));
        BOOST_CHECK(out.ends_with(R"({"digest":{"hex":"00"}})"));
        // gRPC needs HTTP/2.
        out = exchange("POST /cusf.crypto.v1.CryptoService/Ripemd160 HTTP/1.1\r\nContent-Type: application/grpc\r\nContent-Length: 0\r\n\r\n");
        BOOST_CHECK(out.starts_with("HTTP/1.1 415 "));
    }
}

BOOST_AUTO_TEST_SUITE_END()

#endif // ENABLE_GRPC
