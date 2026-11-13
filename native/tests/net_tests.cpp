// N8 networking tests: JSON, SHA-1 / WebSocket handshake, frames, URLs and the relay protocol.
// Without arguments the relay is the native port (src/relay.cpp) started in-process; with
// `--external ws://host:port` the same protocol checks run against another relay (the original
// Node.js server.js, started by tools/n8/run_with_legacy_relay.cjs).
#include "cat/json.hpp"
#include "cat/net.hpp"
#include "cat/relay.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace {
int failures = 0, checks = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    if(!ok) { ++failures; std::printf("FAIL %s\n", what.c_str()); }
}
using cat::json::Value;

void json_tests() {
    using cat::json::number_text;
    const std::pair<double, const char*> nums[] = {{0, "0"}, {-0.0, "0"}, {42, "42"}, {-7.25, "-7.25"}, {0.1, "0.1"}, {1e21, "1e+21"}, {1e20, "100000000000000000000"},
                                                   {1.5e-7, "1.5e-7"}, {0.000001, "0.000001"}, {123.456, "123.456"}, {1.0 / 3, "0.3333333333333333"}, {5e-324, "5e-324"},
                                                   {1.7976931348623157e308, "1.7976931348623157e+308"}, {12.346, "12.346"}, {-1e-7, "-1e-7"}};
    for(const auto& [d, text] : nums) check(number_text(d) == text, std::string("number_text ") + text + " got " + number_text(d));
    const auto v = cat::json::parse(R"({"t":"relay","data":{"type":"pose","x":12.5,"y":-3,"ok":true,"n":null,"s":"a\"b\\cé😀","list":[1,2,[3]]},"x":1,"x":2})");
    check(v["t"].as_string() == "relay" && v["data"]["x"].as_number() == 12.5 && v["data"]["ok"].as_bool() && v["data"]["n"].is_null(), "parse values");
    check(v["data"]["s"].as_string() == "a\"b\\c\xc3\xa9\xf0\x9f\x98\x80", "parse escapes and surrogate pairs");
    check(v["x"].as_number() == 2 && v.as_object().size() == 3, "duplicate key keeps the last value at the first place (JSON.parse)");
    check(cat::json::dump(v) == R"({"t":"relay","data":{"type":"pose","x":12.5,"y":-3,"ok":true,"n":null,"s":"a\"b\\cé😀","list":[1,2,[3]]},"x":2})", "dump keeps key order and UTF-8");
    check(cat::json::dump(Value(std::string("\x01\n"))) == "\"\\u0001\\n\"", "control characters escaped as JSON.stringify");
    check(cat::json::dump(Value(std::nan(""))) == "null", "NaN written as null");
    check(!cat::json::try_parse("{\"a\":}") && !cat::json::try_parse("[1,]") && !cat::json::try_parse("01") && !cat::json::try_parse("{} x"), "malformed text rejected");
    check(Value(0).truthy() == false && Value("").truthy() == false && Value("x").truthy() && Value(cat::json::Object{}).truthy(), "JavaScript truthiness");
    std::string deep(70, '['); deep += std::string(70, ']');
    check(!cat::json::try_parse(deep), "nesting limit");
}

void codec_tests() {
    const auto h = cat::net::sha1("abc");
    char hex[41];
    for(int i = 0; i < 20; ++i) std::snprintf(hex + i * 2, 3, "%02x", h[static_cast<std::size_t>(i)]);
    check(std::string(hex) == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA-1 test vector");
    check(cat::net::websocket_accept("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "RFC 6455 handshake example");
    for(std::size_t size : {0u, 5u, 125u, 126u, 65535u, 65536u, 100000u}) {
        std::string payload(size, 'x');
        for(std::size_t i = 0; i < size; ++i) payload[i] = static_cast<char>('a' + i % 26);
        for(bool mask : {false, true}) {
            cat::net::FrameReader r(mask, 1 << 20);
            const auto f = cat::net::encode_frame(cat::net::op_text, payload, mask, 0x12345678u);
            for(std::size_t i = 0; i < f.size(); i += 7) r.feed(f.data() + i, std::min<std::size_t>(7, f.size() - i));   // byte-by-byte-ish arrival
            const auto m = r.next();
            check(m && m->payload == payload && !r.failed(), "frame round trip " + std::to_string(size) + (mask ? " masked" : ""));
        }
    }
    {   // fragments with an interleaved ping
        cat::net::FrameReader r(false);
        std::string f = cat::net::encode_frame(cat::net::op_text, "hel", false);
        f[0] = static_cast<char>(f[0] & 0x7F);   // FIN off
        std::string c = cat::net::encode_frame(cat::net::op_continuation, "lo", false);
        const auto ping = cat::net::encode_frame(cat::net::op_ping, "p", false);
        const auto all = f + ping + c;
        r.feed(all.data(), all.size());
        const auto a = r.next(), b = r.next();
        check(a && a->op == cat::net::op_ping && b && b->payload == "hello", "fragmented message with a ping between the fragments");
    }
    {
        cat::net::FrameReader r(true);
        const auto f = cat::net::encode_frame(cat::net::op_text, "x", false);
        r.feed(f.data(), f.size());
        r.next();
        check(r.failed(), "a server refuses unmasked client frames");
        cat::net::FrameReader small(false, 10);
        const auto big = cat::net::encode_frame(cat::net::op_text, std::string(11, 'y'), false);
        small.feed(big.data(), big.size());
        small.next();
        check(small.failed(), "message size limit (maxPayload)");
    }
}

void url_tests() {
    const auto a = cat::net::parse_url("ws://Example.com:8787/ws?x=1");
    check(a && a->host == "example.com" && a->port == 8787 && a->path == "/ws?x=1", "parse ws URL");
    const auto b = cat::net::parse_url("wss://[::1]/ws");
    check(b && b->host == "::1" && b->port == 443 && b->host_header() == "[::1]", "parse IPv6 wss URL");
    auto r = cat::net::relay_address("cat-arena.onrender.com");
    check(r.ws && r.ws->text() == "ws://cat-arena.onrender.com/ws" && r.rooms->text() == "http://cat-arena.onrender.com/rooms", "netRelayURL: bare host gets ws:// and /ws");
    r = cat::net::relay_address("https://cat-arena.onrender.com/");
    check(r.ws && r.ws->text() == "wss://cat-arena.onrender.com/ws" && r.rooms->text() == "https://cat-arena.onrender.com/rooms", "netRelayURL: https becomes wss");
    r = cat::net::relay_address("192.168.1.20:8787");
    check(r.ws && r.ws->text() == "ws://192.168.1.20:8787/ws", "LAN address with a port");
    check(!cat::net::relay_address("   ").ws && !cat::net::relay_address("ftp://x").ws, "unusable addresses refused");
}

// ---- relay protocol ------------------------------------------------------------------------------------------
struct Client {
    std::unique_ptr<cat::net::Connection> c;
    std::vector<Value> inbox;
    void pump() {
        std::vector<std::string> in;
        c->poll(in);
        for(auto& s : in) if(auto v = cat::json::try_parse(s)) inbox.push_back(*v);
    }
    void send(const Value& v) { c->send(cat::json::dump(v)); }
    // First message matching `pred` (removed from the inbox), waiting at most `ms`.
    std::optional<Value> take(const std::function<bool(const Value&)>& pred, std::vector<Client*> others = {}, int ms = 4000) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while(std::chrono::steady_clock::now() < end) {
            pump();
            for(Client* o : others) o->pump();
            for(std::size_t i = 0; i < inbox.size(); ++i) if(pred(inbox[i])) { Value v = inbox[i]; inbox.erase(inbox.begin() + static_cast<std::ptrdiff_t>(i)); return v; }
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        return std::nullopt;
    }
};
std::unique_ptr<Client> open(const cat::net::Url& url) {
    auto c = std::make_unique<Client>();
    c->c = cat::net::connect(url, 5000);
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while(c->c->state() == cat::net::Connection::State::connecting && std::chrono::steady_clock::now() < end) { c->pump(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    return c;
}
auto is(const char* t) { return [t](const Value& v) { return v["t"].as_string() == t; }; }
Value msg(std::initializer_list<std::pair<const char*, Value>> kv) { Value v; for(const auto& [k, x] : kv) v.set(k, x); return v; }

void relay_tests(const cat::net::Url& ws, const cat::net::Url& rooms, bool native) {
    const auto pass_before = failures;
    auto host = open(ws);
    check(host->c->state() == cat::net::Connection::State::open, "WebSocket handshake with the relay (" + ws.text() + "): " + host->c->error());
    if(host->c->state() != cat::net::Connection::State::open) return;
    host->send(msg({{"t", "create"}, {"name", "Host <Cat>!"}, {"build", cat::net::kBuildId}, {"mapId", "reactor-bastion-v1"}}));
    auto ready = host->take(is("ready"));
    check(ready && ready->find("role") && (*ready)["role"].as_string() == "host" && (*ready)["slot"].as_number() == 0 && (*ready)["peer"].is_null() && (*ready)["maxPlayers"].as_number() == 4, "create: ready as host, slot 0");
    const std::string code = ready ? (*ready)["code"].as_string() : "";
    bool code_ok = code.size() == 6;
    for(char c : code) code_ok = code_ok && std::string("ABCDEFGHJKLMNPQRSTUVWXYZ23456789").find(c) != std::string::npos;
    check(code_ok, "room code: six characters of the relay alphabet");
    check(ready && (*ready)["players"].as_array().size() == 1 && (*ready)["players"].as_array()[0]["name"].as_string() == "Host Cat", "names cleaned as nameOf()");
    check(host->take(is("roster")).has_value(), "roster after create");
    auto list = cat::net::http_get(rooms, 4000);
    auto data = cat::json::try_parse(list.body);
    check(list.status == 200 && data && (*data)["version"].as_number() == 1 && (*data)["build"].as_string() == cat::net::kBuildId && (*data)["rooms"].as_array().size() == 1, "GET /rooms lists the room: " + list.error);
    if(data && !(*data)["rooms"].as_array().empty()) {
        const auto& r = (*data)["rooms"].as_array()[0];
        check(r["id"].as_string() == code && r["title"].as_string() == "Host Cats Server" && r["players"].as_number() == 1 && r["open"].as_bool() && r["mapId"].as_string() == "reactor-bastion-v1", "room entry (default title as titleOf())");
    }
    auto health = cat::net::http_get(cat::net::Url{rooms.scheme, rooms.host, rooms.port, "/health"}, 4000);
    check(health.status == 200 && cat::json::try_parse(health.body) && (*cat::json::try_parse(health.body))["status"].as_string() == "ok", "GET /health");
    host->send(msg({{"t", "create"}, {"build", cat::net::kBuildId}, {"mapId", "reactor-bastion-v1"}}));
    auto err = host->take(is("error"));
    check(err && (*err)["message"].as_string() == "Already in a room.", "second create refused");

    auto bad = open(ws);
    bad->send(msg({{"t", "join"}, {"code", code}, {"build", cat::net::kBuildId}, {"mapId", "helios-crucible-v1"}}));
    err = bad->take(is("error"));
    check(err && (*err)["message"].as_string() == "Client version or selected map does not match the host.", "join with another map refused");
    bad->c->send("{not json");
    err = bad->take(is("error"));
    check(err && (*err)["message"].as_string() == "Invalid game message.", "invalid JSON answered with an error");
    auto lost = open(ws);
    lost->send(msg({{"t", "join"}, {"code", "ZZZZZZ"}, {"build", cat::net::kBuildId}, {"mapId", "reactor-bastion-v1"}}));
    err = lost->take(is("error"));
    check(err && (*err)["message"].as_string() == "That server is no longer online. Refresh the server list and select another server.", "join of an unknown room");
    auto oldbuild = open(ws);
    oldbuild->send(msg({{"t", "create"}, {"build", "VA-OLD"}, {"mapId", "reactor-bastion-v1"}}));
    err = oldbuild->take(is("error"));
    check(err && (*err)["message"].as_string() == "Client build or map is incompatible with this Cat Arena server.", "create with another build refused");

    std::vector<std::unique_ptr<Client>> guests;
    std::vector<std::string> ids;
    for(int i = 0; i < 3; ++i) {
        guests.push_back(open(ws));
        guests.back()->send(msg({{"t", "join"}, {"code", code}, {"name", "Guest " + std::to_string(i + 1)}, {"build", cat::net::kBuildId}, {"mapId", "reactor-bastion-v1"}}));
        auto g = guests.back()->take(is("ready"), {host.get()});
        check(g && (*g)["role"].as_string() == "guest" && (*g)["slot"].as_number() == i + 1 && (*g)["peer"]["name"].as_string() == "Host Cat" && (*g)["players"].as_array().size() == static_cast<std::size_t>(i + 2),
              "guest " + std::to_string(i + 1) + " joins in slot " + std::to_string(i + 1));
        ids.push_back(g ? (*g)["id"].as_string() : "");
        auto joined = host->take([](const Value& v) { return v["t"].as_string() == "peer" && v["event"].as_string() == "guest_joined"; });
        check(joined && joined->find("peer") && (*joined)["peer"]["id"].as_string() == ids.back(), "host told: guest_joined");
    }
    auto fifth = open(ws);
    fifth->send(msg({{"t", "join"}, {"code", code}, {"build", cat::net::kBuildId}, {"mapId", "reactor-bastion-v1"}}));
    err = fifth->take(is("error"));
    check(err && (*err)["message"].as_string() == "That server is full (4/4). Refresh the server list.", "a fifth player is refused");
    list = cat::net::http_get(rooms, 4000);
    data = cat::json::try_parse(list.body);
    check(data && !(*data)["rooms"].as_array().empty() && (*data)["rooms"].as_array()[0]["players"].as_number() == 4 && !(*data)["rooms"].as_array()[0]["open"].as_bool(), "full room listed as 4/4, not open");
    // relay: guest -> host, host -> all, host -> one
    guests[1]->send(msg({{"t", "relay"}, {"data", msg({{"type", "pose"}, {"x", 12.5}})}}));
    auto got = host->take(is("relay"), {guests[0].get(), guests[1].get(), guests[2].get()});
    check(got && (*got)["from"].as_string() == ids[1] && (*got)["data"]["x"].as_number() == 12.5, "guest -> host relay with the sender id");
    host->send(msg({{"t", "relay"}, {"data", msg({{"type", "world"}, {"wave", 3}})}}));
    bool all = true;
    for(auto& g : guests) { auto w = g->take(is("relay"), {host.get()}); all = all && w && (*w)["data"]["wave"].as_number() == 3; }
    check(all, "host broadcast reaches every guest");
    host->send(msg({{"t", "relay"}, {"to", ids[2]}, {"data", msg({{"type", "frag"}, {"head", true}})}}));
    auto only = guests[2]->take(is("relay"), {host.get(), guests[0].get(), guests[1].get()});
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    guests[0]->pump(); guests[1]->pump();
    bool others_clean = true;
    for(int i : {0, 1}) for(const auto& v : guests[static_cast<std::size_t>(i)]->inbox) if(v["t"].as_string() == "relay") others_clean = false;
    check(only && (*only)["data"]["head"].as_bool() && others_clean, "host -> one guest");
    guests[0]->send(msg({{"t", "relay"}, {"data", Value(cat::json::Array{1, 2})}}));   // not an object: dropped
    // a guest leaves
    for(auto& g : guests) g->inbox.clear();
    host->inbox.clear();
    guests[0]->c->close();
    auto left = host->take([](const Value& v) { return v["t"].as_string() == "peer" && v["event"].as_string() == "guest_left"; });
    check(left && (*left)["peer"]["id"].as_string() == ids[0], "host told: guest_left");
    auto roster = host->take([](const Value& v) { return v["t"].as_string() == "roster" && v["players"].as_array().size() == 3; });
    check(roster.has_value(), "roster shrinks to 3");
    // the slot is reused
    auto again = open(ws);
    again->send(msg({{"t", "join"}, {"code", code}, {"build", cat::net::kBuildId}, {"mapId", "reactor-bastion-v1"}}));
    auto g4 = again->take(is("ready"), {host.get()});
    check(g4 && (*g4)["slot"].as_number() == 1 && (*g4)["players"].as_array().back()["name"].as_string() == "Marine Cat", "freed slot 1 reused; default name Marine Cat");
    // the host leaves: every guest is told and the room disappears
    host->c->close();
    bool told = true;
    for(Client* g : {guests[1].get(), guests[2].get(), again.get()}) told = told && g->take([](const Value& v) { return v["t"].as_string() == "peer" && v["event"].as_string() == "host_left"; }).has_value();
    check(told, "guests told: host_left");
    list = cat::net::http_get(rooms, 4000);
    data = cat::json::try_parse(list.body);
    check(data && (*data)["rooms"].as_array().empty(), "room removed when the host leaves");
    std::printf("%s relay protocol against the %s relay (%s)\n", failures == pass_before ? "PASS" : "FAIL", native ? "native" : "external (original server.js)", ws.text().c_str());
}
}  // namespace

int main(int argc, char** argv) {
    cat::net::startup();
    std::string external;
    for(int i = 1; i < argc; ++i) if(std::string(argv[i]) == "--external" && i + 1 < argc) external = argv[++i];
    json_tests();
    codec_tests();
    url_tests();
    std::printf("%s JSON, SHA-1, WebSocket frames, URLs\n", failures ? "FAIL" : "PASS");
    if(!external.empty()) {
        const auto addr = cat::net::relay_address(external);
        if(!addr.ws) { std::printf("FAIL bad --external address %s\n", external.c_str()); return 1; }
        relay_tests(*addr.ws, *addr.rooms, false);
    } else {
        cat::net::RelayServer relay;
        std::string error;
        if(!relay.start("127.0.0.1", 0, error)) { std::printf("FAIL cannot start the native relay: %s\n", error.c_str()); return 1; }
        const auto addr = cat::net::relay_address("127.0.0.1:" + std::to_string(relay.port()));
        relay_tests(*addr.ws, *addr.rooms, true);
        const auto stats = relay.stats();
        check(stats.messages_relayed >= 5, "relay statistics count relayed messages");
        relay.stop();
    }
    std::printf("%s net (%d checks)\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
