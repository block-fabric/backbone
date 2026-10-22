// Copyright (c) 2026 The Backbone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#ifdef ENABLE_GRPC

#include <grpc/descriptors.h>
#include <grpc/protobuf.h>
#include <grpc/server.h>
#include <netbase.h>
#include <test/util/setup_common.h>
#include <util/sock.h>
#include <util/strencodings.h>
#include <util/time.h>

#include <nghttp2/nghttp2.h>

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <map>
#include <set>
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
            {":method", "POST"}, {":scheme", "http"}, {":authority", "127.0.0.1:50051"}, {":path", path}, {"content-type", content_type}, {"te", "trailers"}};
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
    BOOST_CHECK_EQUAL(info.Sub("block_hash")->Sub("hex")->GetString("value"), "ab");
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
    BOOST_CHECK_EQUAL(req.Sub("sidechain_id")->GetUInt("value"), 7U);
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
        if (!req.Sub("msg")) return w->Finish({grpcsrv::Code::INVALID_ARGUMENT, "Missing field: `msg` 100%"});
        Message resp{New("cusf.crypto.v1.Ripemd160Response")};
        SetWrapped(resp, "digest", "00");
        w->Write(resp);
        w->Finish({});
    }));
    BOOST_REQUIRE(server.Register("cusf.mainchain.v1.ValidatorService", "SubscribeEvents", [&](const Message&, const std::shared_ptr<grpcsrv::Writer>& w) { stream_writer = w; }));
    BOOST_REQUIRE(server.Register("cusf.mainchain.v1.ValidatorService", "GetChainTip", [](const Message&, const std::shared_ptr<grpcsrv::Writer>& w) {
        w->Write(New("cusf.mainchain.v1.GetChainTipResponse"));
        w->Finish({});
    }));
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
        std::string out{exchange("POST /cusf.crypto.v1.CryptoService/Ripemd160 HTTP/1.1\r\nHost: 127.0.0.1:50051\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}")};
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
        // Connect's GET form: for a method without side effects only (idempotency_level NO_SIDE_EFFECTS).
        out = exchange("GET /cusf.mainchain.v1.ValidatorService/GetChainTip?encoding=json&message=%7B%7D HTTP/1.1\r\n\r\n");
        BOOST_CHECK(out.starts_with("HTTP/1.1 200 OK"));
        BOOST_CHECK(out.ends_with(R"({})"));
        out = exchange("GET /cusf.crypto.v1.CryptoService/Ripemd160?encoding=json&message=%7B%22msg%22%3A%7B%22hex%22%3A%22%22%7D%7D HTTP/1.1\r\n\r\n");
        BOOST_CHECK(out.starts_with("HTTP/1.1 400 "));
        // A request from a web page (an Origin header), or to a Host that is a name (DNS rebinding): refused.
        out = exchange("POST /cusf.crypto.v1.CryptoService/Ripemd160 HTTP/1.1\r\nOrigin: http://evil.example\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}");
        BOOST_CHECK(out.starts_with("HTTP/1.1 403 "));
        out = exchange("POST /cusf.crypto.v1.CryptoService/Ripemd160 HTTP/1.1\r\nHost: evil.example:50051\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}");
        BOOST_CHECK(out.starts_with("HTTP/1.1 403 "));
        out = exchange("POST /cusf.crypto.v1.CryptoService/Ripemd160 HTTP/1.1\r\nHost: localhost:50051\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}");
        BOOST_CHECK(out.starts_with("HTTP/1.1 400 ")); // let in: the handler's own error
        out = exchange("POST /cusf.crypto.v1.CryptoService/Ripemd160 HTTP/1.1\r\nHost: [::1]:50051\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\n{}");
        BOOST_CHECK(out.starts_with("HTTP/1.1 415 ")); // not a type of these protocols (a form a page could post)
        // gRPC needs HTTP/2.
        out = exchange("POST /cusf.crypto.v1.CryptoService/Ripemd160 HTTP/1.1\r\nContent-Type: application/grpc\r\nContent-Length: 0\r\n\r\n");
        BOOST_CHECK(out.starts_with("HTTP/1.1 415 "));
    }
}

BOOST_AUTO_TEST_CASE(limits)
{
    grpcsrv::Options options;
    options.max_request_bytes = 1000;
    options.max_connection_buffer = 2000;
    grpcsrv::Server server{Pool(), options};
    int calls{0};
    BOOST_REQUIRE(server.Register("cusf.crypto.v1.CryptoService", "Ripemd160", [&](const Message&, const std::shared_ptr<grpcsrv::Writer>& w) {
        ++calls;
        w->Finish({});
    }));
    // The credential (-grpcauth): a guard that refuses a call without it.
    server.SetGuard([](const std::string& path, const std::map<std::string, std::string>& headers) -> std::optional<grpcsrv::Status> {
        if (headers.contains("authorization")) return std::nullopt;
        return grpcsrv::Status{grpcsrv::Code::UNAUTHENTICATED, "token needed for " + path};
    });
    {
        H2Client client{server};
        const int32_t refused{client.Call("/cusf.crypto.v1.CryptoService/Ripemd160", Envelope(""))};
        client.Pump();
        BOOST_CHECK_EQUAL(client.responses[refused].headers.at("grpc-status"), "16");
        BOOST_CHECK_EQUAL(calls, 0);
    }
    {
        // Over the request size: RESOURCE_EXHAUSTED.
        grpcsrv::Server open{Pool(), options};
        BOOST_REQUIRE(open.Register("cusf.crypto.v1.CryptoService", "Ripemd160", [&](const Message&, const std::shared_ptr<grpcsrv::Writer>& w) { w->Finish({}); }));
        H2Client client{open};
        const int32_t big{client.Call("/cusf.crypto.v1.CryptoService/Ripemd160", Envelope(std::string(1500, 'x')))};
        client.Pump();
        BOOST_CHECK_EQUAL(client.responses[big].headers.at("grpc-status"), "8");
    }
    {
        // A request within its size, but over the connection's budget of buffered bytes: the stream is reset.
        grpcsrv::Options roomy{options};
        roomy.max_request_bytes = 5000;
        grpcsrv::Server lenient{Pool(), roomy};
        BOOST_REQUIRE(lenient.Register("cusf.crypto.v1.CryptoService", "Ripemd160", [](const Message&, const std::shared_ptr<grpcsrv::Writer>& w) { w->Finish({}); }));
        H2Client client{lenient};
        const int32_t id{client.Call("/cusf.crypto.v1.CryptoService/Ripemd160", Envelope(std::string(3000, 'y')))};
        client.Pump();
        BOOST_CHECK(client.responses[id].closed);
        BOOST_CHECK_EQUAL(client.responses[id].error_code, uint32_t{NGHTTP2_ENHANCE_YOUR_CALM});
        // The bytes are given back: the next request of the connection goes through.
        const int32_t ok{client.Call("/cusf.crypto.v1.CryptoService/Ripemd160", Envelope(""))};
        client.Pump();
        BOOST_CHECK_EQUAL(client.responses[ok].headers.at("grpc-status"), "0");
    }
    {
        // HTTP/1.1: a chunk declared over the limit closes the connection before anything is buffered.
        grpcsrv::Connection c{server};
        const std::string head{"POST /cusf.crypto.v1.CryptoService/Ripemd160 HTTP/1.1\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n"};
        BOOST_CHECK(!c.Receive(MakeUCharSpan(head + "fffff\r\n")));
        grpcsrv::Connection c2{server};
        BOOST_CHECK(!c2.Receive(MakeUCharSpan(std::string(2000 + (64 << 10), 'z'))));
    }
}

BOOST_AUTO_TEST_CASE(connection_states)
{
    // What the connection's loop times: a request being read (Receiving: the request deadline) and a call running
    // (Busy: never idle); over HTTP/2 each stream from its own first bytes (OldestRequest).
    grpcsrv::Server server{Pool()};
    std::shared_ptr<grpcsrv::Writer> running;
    BOOST_REQUIRE(server.Register("cusf.mainchain.v1.ValidatorService", "SubscribeEvents", [&](const Message&, const std::shared_ptr<grpcsrv::Writer>& w) { running = w; }));
    BOOST_REQUIRE(server.Register("cusf.mainchain.v1.ValidatorService", "GetChainTip", [](const Message&, const std::shared_ptr<grpcsrv::Writer>& w) {
        w->Write(New("cusf.mainchain.v1.GetChainTipResponse"));
        w->Finish({});
    }));
    {
        grpcsrv::Connection c{server};
        BOOST_CHECK(!c.Receiving() && !c.Busy());
        const std::string body{Envelope("{}")};
        const std::string request{strprintf("POST /cusf.mainchain.v1.ValidatorService/SubscribeEvents HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/connect+json\r\nContent-Length: %u\r\n\r\n", body.size()) + body};
        BOOST_CHECK(c.Receive(MakeUCharSpan(request.substr(0, 20))));
        BOOST_CHECK(c.Receiving() && !c.Busy());
        BOOST_CHECK(!c.OldestRequest());
        BOOST_CHECK(c.Receive(MakeUCharSpan(request.substr(20, request.size() - 22))));
        BOOST_CHECK(c.Receiving() && !c.Busy());
        BOOST_CHECK(c.Receive(MakeUCharSpan(request.substr(request.size() - 2))));
        BOOST_CHECK(!c.Receiving() && c.Busy());
        BOOST_REQUIRE(running);
        running->Finish({});
        c.Poll();
        BOOST_CHECK(!c.Receiving() && !c.Busy());
        running.reset();
    }
    {
        H2Client client{server};
        client.Pump(2);
        BOOST_CHECK(!client.connection.Receiving() && !client.connection.Busy());
        // A stream whose headers came, not its body.
        const std::vector<std::pair<std::string, std::string>> headers{
            {":method", "POST"}, {":scheme", "http"}, {":authority", "127.0.0.1:50051"}, {":path", "/cusf.mainchain.v1.ValidatorService/SubscribeEvents"}, {"content-type", "application/grpc"}, {"te", "trailers"}};
        std::vector<nghttp2_nv> nvs;
        for (const auto& [n, v] : headers) nvs.push_back({reinterpret_cast<uint8_t*>(const_cast<char*>(n.data())), reinterpret_cast<uint8_t*>(const_cast<char*>(v.data())), n.size(), v.size(), NGHTTP2_NV_FLAG_NONE});
        const int32_t slow{nghttp2_submit_headers(client.session, NGHTTP2_FLAG_NONE, -1, nullptr, nvs.data(), nvs.size(), nullptr)};
        BOOST_REQUIRE(slow > 0);
        client.Pump(2);
        BOOST_CHECK(client.connection.Receiving() && !client.connection.Busy());
        const auto oldest{client.connection.OldestRequest()};
        BOOST_REQUIRE(oldest);
        // Other streams come and go: the slow one is still the oldest, timed from its own start.
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        const int32_t quick{client.Call("/cusf.mainchain.v1.ValidatorService/GetChainTip", Envelope(""))};
        client.Pump(4);
        BOOST_CHECK_EQUAL(client.responses[quick].headers.at("grpc-status"), "0");
        BOOST_CHECK(client.connection.OldestRequest() == oldest);
        // Its body: the call runs.
        client.bodies[slow] = Envelope("");
        nghttp2_data_provider provider;
        provider.source.ptr = &client;
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
        BOOST_REQUIRE_EQUAL(nghttp2_submit_data(client.session, NGHTTP2_FLAG_END_STREAM, slow, &provider), 0);
        client.Pump(2);
        BOOST_CHECK(!client.connection.Receiving() && client.connection.Busy());
        BOOST_CHECK(!client.connection.OldestRequest());
        BOOST_REQUIRE(running);
        running->Finish({});
        client.Pump(2);
        BOOST_CHECK(!client.connection.Busy());
        // Before an idle close: a GOAWAY.
        client.connection.Goodbye();
        const std::string out{client.connection.TakeOutput()};
        BOOST_REQUIRE(out.size() >= 9);
        BOOST_CHECK_EQUAL(uint8_t(out[3]), NGHTTP2_GOAWAY);
    }
}

namespace {
using namespace std::chrono_literals;

/** A real client socket of the server, read and written by the test. */
struct RawClient {
    std::unique_ptr<Sock> sock;
    std::string in;
    bool closed{false};

    explicit RawClient(const CService& to) : sock{ConnectDirectly(to, /*manual_connection=*/true)} { BOOST_REQUIRE(sock); }

    /** Send (false once the server closed the connection). */
    bool Send(std::string_view data)
    {
        while (!data.empty()) {
            const ssize_t n{sock->Send(data.data(), data.size(), MSG_NOSIGNAL)};
            if (n <= 0) return false;
            data.remove_prefix(static_cast<size_t>(n));
        }
        return true;
    }

    /** Read what comes for up to `wait`; @return true once the server closed the connection. */
    bool ReadFor(std::chrono::milliseconds wait)
    {
        const auto end{NodeClock::now() + wait};
        while (!closed && NodeClock::now() < end) {
            Sock::Event occurred{0};
            if (!sock->Wait(10ms, Sock::RecvEvent, &occurred)) return closed = true;
            if (!(occurred & Sock::RecvEvent)) continue;
            char buf[4096];
            const ssize_t n{sock->Recv(buf, sizeof(buf), 0)};
            if (n <= 0) return closed = true;
            in.append(buf, static_cast<size_t>(n));
        }
        return closed;
    }
};

double Seconds(NodeClock::duration d) { return std::chrono::duration<double>(d).count(); }

/** The types of the HTTP/2 frames in bytes the server sent. */
std::vector<uint8_t> FrameTypes(std::string_view in)
{
    std::vector<uint8_t> types;
    while (in.size() >= 9) {
        const size_t len{(size_t{uint8_t(in[0])} << 16) | (size_t{uint8_t(in[1])} << 8) | uint8_t(in[2])};
        types.push_back(uint8_t(in[3]));
        if (in.size() < 9 + len) break;
        in.remove_prefix(9 + len);
    }
    return types;
}
} // namespace

BOOST_AUTO_TEST_CASE(slow_clients)
{
    grpcsrv::Options options;
    options.request_timeout_seconds = 1;
    options.idle_timeout_seconds = 2;
    options.max_connections_per_ip = 2;
    options.threads = 1;
    grpcsrv::Server server{Pool(), options};
    // A call that runs longer than both timeouts.
    BOOST_REQUIRE(server.Register("cusf.crypto.v1.CryptoService", "Ripemd160", [](const Message&, const std::shared_ptr<grpcsrv::Writer>& w) {
        std::this_thread::sleep_for(3s);
        Message resp{New("cusf.crypto.v1.Ripemd160Response")};
        SetWrapped(resp, "digest", "00");
        w->Write(resp);
        w->Finish({});
    }));
    std::string error;
    BOOST_REQUIRE_MESSAGE(server.Start({*Assert(Lookup("127.0.0.1", 0, /*fAllowLookup=*/false))}, {}, error), error);
    const CService to{server.Bound().at(0)};

    {
        // A request trickling in (a byte every 100 ms, never done) is cut 1 s after its first byte, though bytes keep coming.
        RawClient c{to};
        const std::string head{"POST /cusf.crypto.v1.CryptoService/Ripemd160 HTTP/1.1\r\nHost: 127.0.0.1\r\nX-Pad: " + std::string(200, 'a')};
        const auto start{NodeClock::now()};
        for (const char ch : head) {
            if (!c.Send(std::string_view{&ch, 1}) || c.ReadFor(100ms)) break;
        }
        BOOST_CHECK(c.closed || c.ReadFor(1s));
        const double took{Seconds(NodeClock::now() - start)};
        BOOST_CHECK_MESSAGE(took >= 0.9 && took < 3, "slow request closed after " << took << " s");
        BOOST_CHECK(c.in.empty());
    }
    {
        // HTTP/2 idle with no call: a GOAWAY, then closed, after 2 s.
        RawClient c{to};
        const auto start{NodeClock::now()};
        BOOST_REQUIRE(c.Send(std::string{"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"} + std::string{"\0\0\0\4\0\0\0\0\0", 9}));
        BOOST_CHECK(!c.ReadFor(1500ms));
        BOOST_CHECK(c.ReadFor(3s));
        const double took{Seconds(NodeClock::now() - start)};
        BOOST_CHECK_MESSAGE(took >= 1.9 && took < 4, "idle connection closed after " << took << " s");
        const std::vector<uint8_t> types{FrameTypes(c.in)};
        BOOST_CHECK(!types.empty() && types.front() == NGHTTP2_SETTINGS);
        BOOST_CHECK(std::find(types.begin(), types.end(), NGHTTP2_GOAWAY) != types.end());
    }
    {
        // A running call (3 s) is never cut by either timeout; idle after it, the connection is closed.
        RawClient c{to};
        BOOST_REQUIRE(c.Send("POST /cusf.crypto.v1.CryptoService/Ripemd160 HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}"));
        const auto start{NodeClock::now()};
        while (c.in.find("\r\n\r\n") == std::string::npos && !c.ReadFor(100ms) && NodeClock::now() - start < 5s) {}
        BOOST_CHECK_MESSAGE(c.in.starts_with("HTTP/1.1 200"), c.in);
        BOOST_CHECK_GE(Seconds(NodeClock::now() - start), 2.9);
        const auto replied{NodeClock::now()};
        BOOST_CHECK(c.ReadFor(3500ms));
        const double idle{Seconds(NodeClock::now() - replied)};
        BOOST_CHECK_MESSAGE(idle >= 1.5 && idle < 3.5, "closed " << idle << " s after the reply");
    }
    // The connections above are over (their threads count them out).
    std::this_thread::sleep_for(300ms);
    {
        // Two connections an address: the third is closed at once; once one ends, another is let in.
        RawClient a{to}, b{to};
        BOOST_CHECK(!a.ReadFor(300ms));
        BOOST_CHECK(!b.ReadFor(10ms));
        RawClient refused{to};
        BOOST_CHECK(refused.ReadFor(500ms));
        a.sock.reset();
        std::this_thread::sleep_for(300ms);
        RawClient again{to};
        BOOST_CHECK(!again.ReadFor(500ms));
    }
    server.Stop();

    // The loopback addresses exempted (as the node does): not bounded by address.
    options.per_ip_exempt = {LookupSubNet("127.0.0.0/8")};
    grpcsrv::Server exempt{Pool(), options};
    BOOST_REQUIRE_MESSAGE(exempt.Start({*Assert(Lookup("127.0.0.1", 0, /*fAllowLookup=*/false))}, {}, error), error);
    {
        RawClient a{exempt.Bound().at(0)}, b{exempt.Bound().at(0)}, c{exempt.Bound().at(0)};
        BOOST_CHECK(!c.ReadFor(500ms));
    }
    exempt.Stop();

    // And the total.
    grpcsrv::Options total_options;
    total_options.max_connections = 2;
    grpcsrv::Server total{Pool(), total_options};
    BOOST_REQUIRE_MESSAGE(total.Start({*Assert(Lookup("127.0.0.1", 0, /*fAllowLookup=*/false))}, {}, error), error);
    {
        RawClient a{total.Bound().at(0)}, b{total.Bound().at(0)};
        BOOST_CHECK(!a.ReadFor(300ms));
        RawClient c{total.Bound().at(0)};
        BOOST_CHECK(c.ReadFor(500ms));
    }
    total.Stop();
}

BOOST_AUTO_TEST_CASE(address_keys)
{
    // Connections are counted by address; IPv6 by its /64 (or the prefix given), where one client rotates addresses.
    const auto key{[](const std::string& a, int prefix = 64) { return grpcsrv::Server::AddressKey(*Assert(LookupHost(a, false)), prefix); }};
    BOOST_CHECK_EQUAL(key("2001:db8:1:2::1"), key("2001:db8:1:2:ffff:ffff:ffff:ffff"));
    BOOST_CHECK_EQUAL(key("2001:db8:1:2::1"), "2001:db8:1:2::/64");
    BOOST_CHECK(key("2001:db8:1:2::1") != key("2001:db8:1:3::1"));
    BOOST_CHECK_EQUAL(key("2001:db8:1:2::1", 48), key("2001:db8:1:3::1", 48));
    BOOST_CHECK(key("2001:db8:1:2::1", 128) != key("2001:db8:1:2::2", 128));
    BOOST_CHECK_EQUAL(key("1.2.3.4"), "1.2.3.4");
    BOOST_CHECK(key("1.2.3.4") != key("1.2.3.5"));
    // Many addresses of one /64: one counter, so the per-address cap holds them all.
    std::set<std::string> keys;
    for (int i{1}; i < 200; ++i) keys.insert(key(strprintf("2001:db8:aa:bb:%x:%x::%x", i, i * 7, i * 13)));
    BOOST_CHECK_EQUAL(keys.size(), 1U);
}

BOOST_AUTO_TEST_CASE(slow_reads_and_rotation)
{
    grpcsrv::Options options;
    options.write_timeout_seconds = 1;
    options.idle_timeout_seconds = 2;
    options.request_timeout_seconds = 1;
    options.max_requests_per_connection = 8;
    options.max_abandoned_requests = 5;
    options.threads = 2;
    grpcsrv::Server server{Pool(), options};
    constexpr size_t MESSAGES{512}, MESSAGE_HEX{64 << 10};
    std::atomic<size_t> written{0};
    // A server stream that writes 32 MiB, far more than the sockets hold.
    BOOST_REQUIRE(server.Register("cusf.mainchain.v1.ValidatorService", "SubscribeEvents", [&](const Message&, const std::shared_ptr<grpcsrv::Writer>& w) {
        Message resp{New("cusf.crypto.v1.Ripemd160Response")};
        SetWrapped(resp, "digest", std::string(MESSAGE_HEX, 'a'));
        for (size_t i{0}; i < MESSAGES && w->Write(resp); ++i) ++written;
        w->Finish({});
    }));
    BOOST_REQUIRE(server.Register("cusf.mainchain.v1.ValidatorService", "GetChainTip", [](const Message&, const std::shared_ptr<grpcsrv::Writer>& w) {
        w->Write(New("cusf.mainchain.v1.GetChainTipResponse"));
        w->Finish({});
    }));
    std::string error;
    BOOST_REQUIRE_MESSAGE(server.Start({*Assert(Lookup("127.0.0.1", 0, /*fAllowLookup=*/false))}, {}, error), error);
    const CService to{server.Bound().at(0)};

    {
        // A client that asks and never reads (slow reads): closed once it took nothing for 1 s, though a call runs.
        RawClient c{to};
        const std::string body{Envelope("{}")};
        BOOST_REQUIRE(c.Send(strprintf("POST /cusf.mainchain.v1.ValidatorService/SubscribeEvents HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/connect+json\r\nContent-Length: %u\r\n\r\n", body.size()) + body));
        std::this_thread::sleep_for(3s);
        // Read now as fast as it comes: the server closed the connection long before all of it.
        BOOST_CHECK(c.ReadFor(10s));
        BOOST_CHECK_MESSAGE(c.in.size() < MESSAGES * MESSAGE_HEX, c.in.size());
    }
    {
        // HTTP/2 PINGs alone are not activity: closed (with a GOAWAY) 2 s after it began, though it keeps pinging.
        RawClient c{to};
        const auto start{NodeClock::now()};
        BOOST_REQUIRE(c.Send(std::string{"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"} + std::string{"\0\0\0\4\0\0\0\0\0", 9}));
        const std::string ping{std::string{"\0\0\x08\x06\0\0\0\0\0", 9} + std::string(8, 'p')};
        while (!c.closed && NodeClock::now() - start < 6s) {
            if (!c.Send(ping)) break;
            c.ReadFor(300ms);
        }
        BOOST_CHECK(c.closed || c.ReadFor(1s));
        const double took{Seconds(NodeClock::now() - start)};
        BOOST_CHECK_MESSAGE(took >= 1.9 && took < 4, "pinging connection closed after " << took << " s");
        const std::vector<uint8_t> types{FrameTypes(c.in)};
        BOOST_CHECK(std::find(types.begin(), types.end(), NGHTTP2_PING) != types.end()); // the PINGs answered
        BOOST_CHECK(std::find(types.begin(), types.end(), NGHTTP2_GOAWAY) != types.end());
    }
    {
        // Streams opened and reset again and again: closed past 5 of them, long before it would be idle.
        RawClient c{to};
        nghttp2_session_callbacks* cb;
        nghttp2_session_callbacks_new(&cb);
        nghttp2_session* session;
        nghttp2_session_client_new(&session, cb, nullptr);
        nghttp2_session_callbacks_del(cb);
        nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, nullptr, 0);
        const std::vector<std::pair<std::string, std::string>> headers{
            {":method", "POST"}, {":scheme", "http"}, {":authority", "127.0.0.1"}, {":path", "/cusf.mainchain.v1.ValidatorService/GetChainTip"}, {"content-type", "application/grpc"}};
        std::vector<nghttp2_nv> nvs;
        for (const auto& [n, v] : headers) nvs.push_back({reinterpret_cast<uint8_t*>(const_cast<char*>(n.data())), reinterpret_cast<uint8_t*>(const_cast<char*>(v.data())), n.size(), v.size(), NGHTTP2_NV_FLAG_NONE});
        const auto start{NodeClock::now()};
        int rotations{0};
        for (; rotations < 20 && !c.closed; ++rotations) {
            // The HEADERS sent first (a stream reset before it is sent is never sent), then its reset.
            const int32_t id{nghttp2_submit_headers(session, NGHTTP2_FLAG_NONE, -1, nullptr, nvs.data(), nvs.size(), nullptr)};
            std::string out;
            const uint8_t* data;
            for (ssize_t n; (n = nghttp2_session_mem_send(session, &data)) > 0;) out.append(reinterpret_cast<const char*>(data), n);
            nghttp2_submit_rst_stream(session, NGHTTP2_FLAG_NONE, id, NGHTTP2_CANCEL);
            for (ssize_t n; (n = nghttp2_session_mem_send(session, &data)) > 0;) out.append(reinterpret_cast<const char*>(data), n);
            if (!c.Send(out)) break;
            c.ReadFor(100ms);
        }
        nghttp2_session_del(session);
        BOOST_CHECK(c.closed || c.ReadFor(1s));
        BOOST_CHECK_MESSAGE(rotations >= 6 && rotations < 8, rotations);
        BOOST_CHECK(Seconds(NodeClock::now() - start) < 1.9);
    }
    {
        // Past 8 requests on one connection: closed once its call is over (the client opens another).
        RawClient c{to};
        const std::string request{"POST /cusf.mainchain.v1.ValidatorService/GetChainTip HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}"};
        for (int i{0}; i < 8; ++i) {
            c.in.clear();
            BOOST_REQUIRE(c.Send(request));
            while (c.in.find("\r\n\r\n") == std::string::npos && !c.ReadFor(100ms)) {}
            BOOST_CHECK_MESSAGE(c.in.starts_with("HTTP/1.1 200"), c.in);
            BOOST_CHECK(!c.ReadFor(300ms));
        }
        BOOST_REQUIRE(c.Send(request));
        BOOST_CHECK(c.ReadFor(1s));
    }
    server.Stop();
}

namespace {
/** An HTTP/2 client's bytes (nghttp2's client session; what the server sends is read raw by the test). */
struct H2Bytes {
    nghttp2_session* session{nullptr};
    std::map<int32_t, std::string> bodies;
    std::map<int32_t, size_t> sent;
    explicit H2Bytes(const std::vector<nghttp2_settings_entry>& settings = {})
    {
        nghttp2_session_callbacks* cb;
        nghttp2_session_callbacks_new(&cb);
        nghttp2_session_client_new(&session, cb, this);
        nghttp2_session_callbacks_del(cb);
        nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, settings.data(), settings.size());
    }
    ~H2Bytes() { nghttp2_session_del(session); }
    std::string Out()
    {
        std::string out;
        const uint8_t* data;
        for (ssize_t n; (n = nghttp2_session_mem_send(session, &data)) > 0;) out.append(reinterpret_cast<const char*>(data), n);
        return out;
    }
    /** A whole gRPC request (its body, END_STREAM). */
    int32_t Request(const std::string& path, const std::string& body)
    {
        const std::vector<std::pair<std::string, std::string>> headers{
            {":method", "POST"}, {":scheme", "http"}, {":authority", "127.0.0.1"}, {":path", path}, {"content-type", "application/grpc"}, {"te", "trailers"}};
        std::vector<nghttp2_nv> nvs;
        for (const auto& [n, v] : headers) nvs.push_back({reinterpret_cast<uint8_t*>(const_cast<char*>(n.data())), reinterpret_cast<uint8_t*>(const_cast<char*>(v.data())), n.size(), v.size(), NGHTTP2_NV_FLAG_NONE});
        nghttp2_data_provider provider;
        provider.source.ptr = this;
        provider.read_callback = [](nghttp2_session*, int32_t id, uint8_t* buf, size_t length, uint32_t* flags, nghttp2_data_source* source, void*) -> ssize_t {
            auto& self{*static_cast<H2Bytes*>(source->ptr)};
            const std::string& b{self.bodies[id]};
            size_t& done{self.sent[id]};
            const size_t n{std::min(length, b.size() - done)};
            std::memcpy(buf, b.data() + done, n);
            done += n;
            if (done == b.size()) *flags |= NGHTTP2_DATA_FLAG_EOF;
            return static_cast<ssize_t>(n);
        };
        const int32_t id{static_cast<int32_t>(nghttp2_session_get_next_stream_id(session))};
        bodies[id] = body;
        BOOST_REQUIRE_EQUAL(nghttp2_submit_request(session, nullptr, nvs.data(), nvs.size(), &provider, nullptr), id);
        return id;
    }
};
} // namespace

BOOST_AUTO_TEST_CASE(slow_reader_bounded)
{
    // A client that takes a byte now and then (slow reads) used to keep its connection open with the server moving
    // every reply into its unsent bytes, without bound. Now: backpressure (no more taken from the call above
    // REPLY_HIGH_WATER waiting), the call ends at its own bound, and the client is closed for reading too little.
    grpcsrv::Options options;
    options.write_timeout_seconds = 1;
    options.min_read_bytes_per_second = 64 << 10;
    // A call's bound above what the loopback sockets hold, so that replies keep waiting on the server's side.
    options.max_call_buffer = 16 << 20;
    options.max_reply_buffer = 32 << 20;
    options.idle_timeout_seconds = 30;
    options.threads = 2;
    grpcsrv::Server server{Pool(), options};
    constexpr size_t MESSAGES{4096}, MESSAGE_HEX{64 << 10};
    std::atomic<size_t> written{0};
    std::atomic<bool> refused{false};
    // A server stream that would write 256 MiB as fast as it can.
    BOOST_REQUIRE(server.Register("cusf.mainchain.v1.ValidatorService", "SubscribeEvents", [&](const Message&, const std::shared_ptr<grpcsrv::Writer>& w) {
        Message resp{New("cusf.crypto.v1.Ripemd160Response")};
        SetWrapped(resp, "digest", std::string(MESSAGE_HEX, 'a'));
        for (size_t i{0}; i < MESSAGES; ++i) {
            if (!w->Write(resp)) {
                refused = true;
                break;
            }
            ++written;
        }
        w->Finish({});
    }));
    // A server stream of small events, a few a second, as SubscribeEvents is in use.
    BOOST_REQUIRE(server.Register("cusf.mainchain.v1.ValidatorService", "SubscribeHeaderSyncProgress", [&](const Message&, const std::shared_ptr<grpcsrv::Writer>& w) {
        Message resp{New("cusf.crypto.v1.Ripemd160Response")};
        SetWrapped(resp, "digest", std::string(40, 'b'));
        for (int i{0}; i < 100 && w->Write(resp); ++i) std::this_thread::sleep_for(25ms);
        w->Finish({});
    }));
    std::string error;
    BOOST_REQUIRE_MESSAGE(server.Start({*Assert(Lookup("127.0.0.1", 0, /*fAllowLookup=*/false))}, {}, error), error);
    const CService to{server.Bound().at(0)};
    const auto ask{[](const std::string& method) {
        const std::string body{Envelope("{}")};
        return strprintf("POST /cusf.mainchain.v1.ValidatorService/%s HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/connect+json\r\nContent-Length: %u\r\n\r\n", method, body.size()) + body;
    }};
    {
        RawClient c{to};
        BOOST_REQUIRE(c.Send(ask("SubscribeEvents")));
        const auto start{NodeClock::now()};
        // One byte every 50 ms for 4 s: 20 bytes a second, far under 64 KiB.
        while (!c.closed && NodeClock::now() - start < 4s) {
            Sock::Event occurred{0};
            if (!c.sock->Wait(50ms, Sock::RecvEvent, &occurred)) break;
            if (occurred & Sock::RecvEvent) {
                char b;
                const ssize_t n{c.sock->Recv(&b, 1, 0)};
                if (n <= 0) {
                    c.closed = true;
                    break;
                }
                c.in.push_back(b);
            }
            std::this_thread::sleep_for(50ms);
        }
        // Closed by then for reading too little: what the sockets still hold comes, then the end.
        BOOST_CHECK(c.closed || c.ReadFor(10s));
        // What the server took from the call stayed bounded: its own bound, what waits for the socket and what the
        // sockets hold; not the 256 MiB the call would write.
        BOOST_CHECK(refused.load());
        BOOST_CHECK_MESSAGE(written.load() * MESSAGE_HEX < size_t{48} << 20, written.load() * MESSAGE_HEX);
    }
    {
        // A client that reads as the events come is not cut: all of them, then the end of the stream.
        RawClient c{to};
        BOOST_REQUIRE(c.Send(ask("SubscribeHeaderSyncProgress")));
        const auto start{NodeClock::now()};
        while (!c.closed && NodeClock::now() - start < 10s && c.in.find("0\r\n\r\n") == std::string::npos) c.ReadFor(100ms);
        BOOST_CHECK(c.in.starts_with("HTTP/1.1 200"));
        BOOST_CHECK(c.in.find("0\r\n\r\n") != std::string::npos);
        size_t events{0};
        for (size_t at{0}; (at = c.in.find(std::string(40, 'b'), at)) != std::string::npos; at += 40) ++events;
        BOOST_CHECK_EQUAL(events, 100U);
    }
    {
        // HTTP/2: a client that opens no window and pings all the time, reading every PING ACK: the ACKs are no progress
        // on its replies: the stream is reset, or the connection closed, within the windows of 1 s.
        RawClient c{to};
        H2Bytes client{{{NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 0}}};
        client.Request("/cusf.mainchain.v1.ValidatorService/SubscribeEvents", Envelope(""));
        BOOST_REQUIRE(c.Send(client.Out()));
        const std::string ping{std::string{"\0\0\x08\x06\0\0\0\0\0", 9} + std::string(8, 'p')};
        const auto start{NodeClock::now()};
        bool reset{false};
        while (!c.closed && !reset && NodeClock::now() - start < 6s) {
            if (!c.Send(ping)) break;
            c.ReadFor(20ms);
            const std::vector<uint8_t> types{FrameTypes(c.in)};
            reset = std::find(types.begin(), types.end(), NGHTTP2_RST_STREAM) != types.end();
        }
        BOOST_CHECK(reset || c.closed);
        const double took{Seconds(NodeClock::now() - start)};
        BOOST_CHECK_MESSAGE(took < 4, "pinging zero-window client ended after " << took << " s");
        const std::vector<uint8_t> types{FrameTypes(c.in)};
        BOOST_CHECK(std::find(types.begin(), types.end(), NGHTTP2_PING) != types.end()); // the PINGs were answered
    }
    server.Stop();
}

BOOST_AUTO_TEST_CASE(flow_control_and_rapid_reset)
{
    grpcsrv::Options options;
    options.write_timeout_seconds = 1;
    options.idle_timeout_seconds = 30;
    options.max_abandoned_requests = 5;
    options.max_call_buffer = 1 << 20;
    options.threads = 2;
    grpcsrv::Server server{Pool(), options};
    constexpr size_t MESSAGES{512}, MESSAGE_HEX{64 << 10};
    std::atomic<size_t> written{0};
    std::atomic<bool> refused{false};
    // A server stream that would write 32 MiB.
    BOOST_REQUIRE(server.Register("cusf.mainchain.v1.ValidatorService", "SubscribeEvents", [&](const Message&, const std::shared_ptr<grpcsrv::Writer>& w) {
        Message resp{New("cusf.crypto.v1.Ripemd160Response")};
        SetWrapped(resp, "digest", std::string(MESSAGE_HEX, 'a'));
        for (size_t i{0}; i < MESSAGES; ++i) {
            if (!w->Write(resp)) {
                refused = true;
                break;
            }
            ++written;
        }
        w->Finish({});
    }));
    // A server stream that runs until the client goes.
    std::vector<std::shared_ptr<grpcsrv::Writer>> running;
    Mutex running_mutex;
    BOOST_REQUIRE(server.Register("cusf.mainchain.v1.ValidatorService", "SubscribeHeaderSyncProgress", [&](const Message&, const std::shared_ptr<grpcsrv::Writer>& w) {
        LOCK(running_mutex);
        running.push_back(w);
    }));
    std::string error;
    BOOST_REQUIRE_MESSAGE(server.Start({*Assert(Lookup("127.0.0.1", 0, /*fAllowLookup=*/false))}, {}, error), error);
    const CService to{server.Bound().at(0)};

    {
        // A client that opens no window (initial window 0, never a WINDOW_UPDATE): the call's unsent replies are
        // bounded (it ends with RESOURCE_EXHAUSTED past 1 MiB), and the stream is reset or the connection closed
        // once nothing moved for 1 s.
        RawClient c{to};
        H2Bytes client{{{NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 0}}};
        const int32_t id{client.Request("/cusf.mainchain.v1.ValidatorService/SubscribeEvents", Envelope(""))};
        BOOST_REQUIRE(c.Send(client.Out()));
        const auto start{NodeClock::now()};
        bool reset{false};
        while (!c.closed && !reset && NodeClock::now() - start < 5s) {
            c.ReadFor(100ms);
            const std::vector<uint8_t> types{FrameTypes(c.in)};
            reset = std::find(types.begin(), types.end(), NGHTTP2_RST_STREAM) != types.end();
        }
        BOOST_CHECK(reset || c.closed);
        const double took{Seconds(NodeClock::now() - start)};
        BOOST_CHECK_MESSAGE(took >= 0.9 && took < 4, "zero-window stream ended after " << took << " s");
        BOOST_CHECK(refused.load());
        BOOST_CHECK_LE(written.load() * MESSAGE_HEX, size_t{1} << 20);
        (void)id;
    }
    {
        // Rapid Reset: calls asked for whole, then reset at once, again and again: closed past 5, with a GOAWAY.
        RawClient c{to};
        H2Bytes client;
        int rounds{0};
        for (; rounds < 20 && !c.closed; ++rounds) {
            const int32_t id{client.Request("/cusf.mainchain.v1.ValidatorService/SubscribeHeaderSyncProgress", Envelope(""))};
            std::string out{client.Out()};
            nghttp2_submit_rst_stream(client.session, NGHTTP2_FLAG_NONE, id, NGHTTP2_CANCEL);
            out += client.Out();
            if (!c.Send(out)) break;
            c.ReadFor(100ms);
        }
        BOOST_CHECK(c.closed || c.ReadFor(1s));
        BOOST_CHECK_MESSAGE(rounds >= 6 && rounds < 9, rounds);
        const std::vector<uint8_t> types{FrameTypes(c.in)};
        BOOST_CHECK(std::find(types.begin(), types.end(), NGHTTP2_GOAWAY) != types.end());
    }
    server.Stop();

    // MadeYouReset: a client that makes the server reset its streams (here: requests over the connection's buffer,
    // reset with ENHANCE_YOUR_CALM) without resetting any itself is closed past the bound as well.
    grpcsrv::Options tight{options};
    tight.max_request_bytes = 5000;
    tight.max_connection_buffer = 2000;
    grpcsrv::Server strict{Pool(), tight};
    BOOST_REQUIRE(strict.Register("cusf.crypto.v1.CryptoService", "Ripemd160", [](const Message&, const std::shared_ptr<grpcsrv::Writer>& w) { w->Finish({}); }));
    BOOST_REQUIRE_MESSAGE(strict.Start({*Assert(Lookup("127.0.0.1", 0, /*fAllowLookup=*/false))}, {}, error), error);
    {
        RawClient c{strict.Bound().at(0)};
        H2Bytes client;
        int rounds{0};
        for (; rounds < 20 && !c.closed; ++rounds) {
            client.Request("/cusf.crypto.v1.CryptoService/Ripemd160", Envelope(std::string(3000, 'y')));
            if (!c.Send(client.Out())) break;
            c.ReadFor(100ms);
        }
        BOOST_CHECK(c.closed || c.ReadFor(1s));
        BOOST_CHECK_MESSAGE(rounds >= 6 && rounds < 9, rounds);
        const std::vector<uint8_t> types{FrameTypes(c.in)};
        BOOST_CHECK(std::find(types.begin(), types.end(), NGHTTP2_RST_STREAM) != types.end());
        BOOST_CHECK(std::find(types.begin(), types.end(), NGHTTP2_GOAWAY) != types.end());
    }
    strict.Stop();
}

BOOST_AUTO_TEST_SUITE_END()

#endif // ENABLE_GRPC
