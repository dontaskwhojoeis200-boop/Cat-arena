// N8: the relay server (port of reference/multiplayer/server.js). See include/cat/relay.hpp.
#include "cat/relay.hpp"

#include "cat/json.hpp"
#include "cat/net.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <list>
#include <mutex>
#include <random>

namespace cat::net {
namespace {
const char kRoomAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
std::int64_t epoch_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::int64_t mono_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
bool word_char(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
// String(x) of the JavaScript relay for the values a client can send.
std::string js_string(const json::Value& v) {
    if(v.is_string()) return v.as_string();
    if(v.is_number()) { const double d = v.as_number(); return d == d ? (std::isinf(d) ? (d > 0 ? "Infinity" : "-Infinity") : json::number_text(d)) : "NaN"; }
    if(v.is_bool()) return v.as_bool() ? "true" : "false";
    if(v.is_object()) return "[object Object]";
    if(v.is_array()) { std::string s; bool first = true; for(const auto& e : v.as_array()) { if(!first) s += ','; first = false; if(!e.is_null()) s += js_string(e); } return s; }
    return "null";
}
// String(x || fallback).replace(/[^\w .-]/g, '').trim().slice(0, limit) || otherwise
std::string clean(const json::Value& v, const std::string& fallback, std::size_t limit, const std::string& otherwise) {
    const std::string raw = v.truthy() ? js_string(v) : fallback;
    std::string s;
    for(const char c : raw) if(word_char(c) || c == ' ' || c == '.' || c == '-') s += c;
    while(!s.empty() && s.front() == ' ') s.erase(0, 1);
    while(!s.empty() && s.back() == ' ') s.pop_back();
    if(s.size() > limit) s.resize(limit);
    return s.empty() ? otherwise : s;
}
bool valid_map(const json::Value& m) {
    if(!m.is_string()) return false;
    for(const char* id : kMapIds) if(m.as_string() == id) return true;
    return false;
}
bool valid_build(const json::Value& b) { return b.is_string() && b.as_string() == kBuildId; }
}  // namespace

struct RelayServer::Impl {
    struct Peer {
        Socket sock;
        bool ws{}, closing{}, alive{true}, counted{};
        std::string http, out;
        FrameReader reader{true, 128 * 1024};
        std::string id, name, room;
        int slot{};
        std::uint64_t serial{};
    };
    struct Room {
        std::string code, title, build, map_id;
        Peer* host{};
        std::vector<Peer*> guests;   // insertion order (Map in server.js)
        std::int64_t created{};
    };
    Socket listener;
    std::list<Peer> peers;                 // stable addresses
    std::vector<Room> rooms;               // insertion order
    std::thread thread;
    std::atomic<bool> stop{false}, running{false};
    int port{};
    int heartbeat_ms{30000};
    std::int64_t next_beat{};
    mutable std::mutex m;
    Stats stats;
    std::function<void(const std::string&)> log;
    std::mt19937 rng{std::random_device{}()};
    int ws_count{};   // peers.size of server.js (WebSocket connections)

    void say(const std::string& s) { if(log) log(s); }
    std::string code6() {
        std::string c;
        do {
            c.clear();
            for(int i = 0; i < 6; ++i) c += kRoomAlphabet[std::uniform_int_distribution<int>(0, 31)(rng)];
        } while(find_room(c));
        return c;
    }
    std::string random_id() {
        static const char* hex = "0123456789abcdef";
        std::string s;
        for(int i = 0; i < 18; ++i) s += hex[std::uniform_int_distribution<int>(0, 15)(rng)];
        return s;
    }
    Room* find_room(const std::string& code) { for(auto& r : rooms) if(r.code == code) return &r; return nullptr; }
    static bool open(const Peer* p) { return p && p->ws && !p->closing && p->sock.valid(); }
    void send(Peer* p, const json::Value& data) {
        if(!open(p)) return;
        const std::string text = json::dump(data);
        p->out += encode_frame(op_text, text, false);
        std::lock_guard<std::mutex> l(m);
        stats.bytes_out += text.size();
    }
    json::Value member(const Peer* p) { json::Value v; v.set("id", p->id); v.set("name", p->name); return v; }
    std::vector<Peer*> members(const Room& r) { std::vector<Peer*> list{r.host}; list.insert(list.end(), r.guests.begin(), r.guests.end()); return list; }
    void roster(Room& r) {
        json::Value d; d.set("t", "roster"); d.set("code", r.code);
        json::Array players; for(Peer* p : members(r)) players.push_back(member(p));
        d.set("players", players); d.set("maxPlayers", kRoomSize);
        for(Peer* p : members(r)) send(p, d);
    }
    void notify(Room& r, const char* event, const json::Value& peer, const Peer* exclude = nullptr) {
        json::Value d; d.set("t", "peer"); d.set("event", event); d.set("peer", peer);
        for(Peer* p : members(r)) if(p != exclude) send(p, d);
    }
    void error(Peer* p, const char* message) { json::Value d; d.set("t", "error"); d.set("message", message); send(p, d); }

    void disconnect(Peer* ws) {
        if(ws->counted) { --ws_count; ws->counted = false; }
        Room* room = find_room(ws->room);
        ws->room.clear();
        if(!room) return;
        if(room->host == ws) {
            const std::string code = room->code;
            for(Peer* g : room->guests) { g->room.clear(); json::Value d; d.set("t", "peer"); d.set("event", "host_left"); send(g, d); }
            rooms.erase(std::find_if(rooms.begin(), rooms.end(), [&](const Room& r) { return r.code == code; }));
            say("Host closed room " + code);
        } else {
            auto it = std::find(room->guests.begin(), room->guests.end(), ws);
            if(it != room->guests.end()) {
                room->guests.erase(it);
                notify(*room, "guest_left", member(ws));
                roster(*room);
                say("Guest left " + room->code + " " + std::to_string(room->guests.size() + 1) + "/" + std::to_string(kRoomSize));
            }
        }
    }
    void action(Peer* ws, const json::Value& msg) {
        if(!msg.is_object()) return;
        const std::string t = msg["t"].is_string() ? msg["t"].as_string() : "";
        if(t == "create") {
            if(!ws->room.empty()) return error(ws, "Already in a room.");
            if(static_cast<int>(rooms.size()) >= kMaxRooms) return error(ws, "Server is at capacity.");
            if(!valid_build(msg["build"]) || !valid_map(msg["mapId"])) return error(ws, "Client build or map is incompatible with this Cat Arena server.");
            ws->name = clean(msg["name"], "Marine Cat", 20, "Marine Cat");
            ws->id = random_id();
            Room r;
            r.code = code6(); r.host = ws; r.title = clean(msg["title"], ws->name + "'s Server", 32, "Blackstone Keep");
            r.created = epoch_ms(); r.build = msg["build"].as_string(); r.map_id = msg["mapId"].as_string();
            ws->room = r.code;
            rooms.push_back(r);
            Room& room = rooms.back();
            json::Value d; d.set("t", "ready"); d.set("role", "host"); d.set("code", room.code); d.set("id", ws->id); d.set("peer", nullptr);
            d.set("players", json::Array{member(ws)}); d.set("slot", 0); d.set("maxPlayers", kRoomSize);
            send(ws, d);
            roster(room);
            say("Room created " + room.code + " 1/" + std::to_string(kRoomSize));
            return;
        }
        if(t == "join") {
            if(!ws->room.empty()) return error(ws, "Already in a room.");
            std::string code;
            for(char c : (msg["code"].truthy() ? js_string(msg["code"]) : std::string())) {
                if(c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
                if((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) code += c;
            }
            if(code.size() > 6) code.resize(6);
            Room* room = find_room(code);
            if(!room) return error(ws, "That server is no longer online. Refresh the server list and select another server.");
            if(!open(room->host)) return error(ws, "Host disconnected; create a new room.");
            if(static_cast<int>(room->guests.size()) >= kRoomSize - 1) return error(ws, "That server is full (4/4). Refresh the server list.");
            if(!valid_build(msg["build"]) || msg["build"].as_string() != room->build || !valid_map(msg["mapId"]) || msg["mapId"].as_string() != room->map_id)
                return error(ws, "Client version or selected map does not match the host.");
            ws->room = room->code; ws->name = clean(msg["name"], "Marine Cat", 20, "Marine Cat"); ws->id = random_id();
            int slot = 1;
            auto taken = [&](int s) { for(Peer* g : room->guests) if(g->slot == s) return true; return false; };
            while(taken(slot) && slot < kRoomSize) ++slot;
            ws->slot = slot;
            room->guests.push_back(ws);
            json::Value d; d.set("t", "ready"); d.set("role", "guest"); d.set("code", room->code); d.set("id", ws->id); d.set("peer", member(room->host));
            json::Array players; for(Peer* p : members(*room)) players.push_back(member(p));
            d.set("players", players); d.set("slot", slot); d.set("maxPlayers", kRoomSize);
            send(ws, d);
            notify(*room, "guest_joined", member(ws), ws);
            roster(*room);
            say("Guest joined " + room->code + " " + std::to_string(room->guests.size() + 1) + "/" + std::to_string(kRoomSize));
            return;
        }
        if(t == "relay") {
            Room* room = find_room(ws->room);
            const json::Value* data = msg.find("data");
            if(!room || !data || !data->is_object()) return;
            json::Value out; out.set("t", "relay"); out.set("from", ws->id); out.set("data", *data);
            std::uint64_t n = 0;
            if(room->host == ws) {
                const json::Value& to = msg["to"];
                if(to.is_string()) { for(Peer* g : room->guests) if(g->id == to.as_string()) { send(g, out); ++n; break; } }
                else for(Peer* g : room->guests) { send(g, out); ++n; }
            } else if(std::find(room->guests.begin(), room->guests.end(), ws) != room->guests.end()) { send(room->host, out); ++n; }
            std::lock_guard<std::mutex> l(m);
            stats.messages_relayed += n;
        }
    }
    void http_reply(Peer* p, int status, const char* reason, const std::string& type, const std::string& body, bool head, const std::string& extra = {}) {
        p->out += "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) +
                  "\r\n" + extra + "Connection: close\r\n\r\n";
        if(!head) p->out += body;
        p->closing = true;
    }
    void http(Peer* p) {
        const auto end = p->http.find("\r\n\r\n");
        if(end == std::string::npos) { if(p->http.size() > 16384) { p->sock.close(); } return; }
        const std::string head = p->http.substr(0, end);
        std::string rest = p->http.substr(end + 4);
        p->http.clear();
        const auto line_end = head.find("\r\n");
        const std::string line = head.substr(0, line_end);
        const auto sp1 = line.find(' '), sp2 = line.find(' ', sp1 + 1);
        if(sp1 == std::string::npos || sp2 == std::string::npos) { p->sock.close(); return; }
        const std::string method = line.substr(0, sp1);
        const std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
        const std::string route = target.substr(0, target.find('?'));
        std::string lower_head = head;
        for(char& c : lower_head) if(c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        auto header = [&](const std::string& name) -> std::string {
            const auto at = lower_head.find("\r\n" + name + ":");
            if(at == std::string::npos) return {};
            auto v0 = at + name.size() + 3;
            const auto v1 = head.find("\r\n", v0);
            while(v0 < head.size() && head[v0] == ' ') ++v0;
            std::string v = head.substr(v0, (v1 == std::string::npos ? head.size() : v1) - v0);
            while(!v.empty() && v.back() == ' ') v.pop_back();
            return v;
        };
        std::string upgrade = header("upgrade");
        for(char& c : upgrade) if(c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        if(upgrade == "websocket") {   // server.on('upgrade')
            if(route != "/ws") { p->out += "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n"; p->closing = true; return; }
            const std::string key = header("sec-websocket-key");
            if(key.empty() || method != "GET") { http_reply(p, 400, "Bad Request", "text/plain", "Bad WebSocket handshake", false); return; }
            p->out += "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + websocket_accept(key) + "\r\n\r\n";
            p->ws = true;
            p->reader.feed(rest.data(), rest.size());
            if(ws_count >= kMaxPeers) {   // wss.on('connection'): full
                json::Value d; d.set("t", "error"); d.set("message", "Server is full.");
                send(p, d);
                p->out += encode_frame(op_close, std::string("\x03\xf5", 2) + "Server full", false);
                p->closing = true;
                return;
            }
            ++ws_count; p->counted = true; p->alive = true;
            return;
        }
        const bool is_head = method == "HEAD";
        if(method != "GET" && !is_head) { http_reply(p, 405, "Method Not Allowed", "text/plain", "Method not allowed", false); return; }
        if(route == "/rooms") {
            json::Array live;
            for(const auto& r : rooms) {
                if(!open(r.host)) continue;
                json::Value e;
                e.set("id", r.code); e.set("name", r.title); e.set("title", r.title); e.set("host", r.host->name);
                e.set("players", static_cast<int>(r.guests.size()) + 1); e.set("maxPlayers", kRoomSize);
                e.set("open", static_cast<int>(r.guests.size()) < kRoomSize - 1); e.set("createdAt", static_cast<double>(r.created));
                e.set("build", r.build); e.set("mapId", r.map_id);
                live.push_back(e);
            }
            json::Value body; body.set("version", 1); body.set("game", "CAT ARENA"); body.set("build", kBuildId);
            json::Array ids; for(const char* id : kMapIds) ids.push_back(id);
            body.set("mapIds", ids); body.set("rooms", live); body.set("maxPlayers", kRoomSize);
            http_reply(p, 200, "OK", "application/json; charset=utf-8", json::dump(body), is_head, "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nX-Content-Type-Options: nosniff\r\n");
            return;
        }
        if(route == "/health") {
            json::Value body; body.set("status", "ok"); body.set("game", "CAT ARENA v0.17"); body.set("maxPlayersPerRoom", kRoomSize);
            body.set("rooms", static_cast<int>(rooms.size())); body.set("players", ws_count);
            http_reply(p, 200, "OK", "application/json", json::dump(body), is_head, "Cache-Control: no-store\r\n");
            return;
        }
        if(route != "/" && route != "/index.html") { http_reply(p, 404, "Not Found", "text/plain", "Not found", is_head); return; }
        http_reply(p, 200, "OK", "text/plain; charset=utf-8", "CAT ARENA relay (native build). Join from the game: MULTIPLAYER > server address ws://<this computer>:" + std::to_string(port) + "\n", is_head,
                   "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n");
    }
    void read(Peer* p) {
        char buf[16384];
        while(p->sock.valid()) {
            const long n = p->sock.recv_some(buf, sizeof buf);
            if(n == 0) break;
            if(n < 0) { p->sock.close(); return; }
            { std::lock_guard<std::mutex> l(m); stats.bytes_in += static_cast<std::uint64_t>(n); }
            if(p->closing) continue;
            if(!p->ws) { p->http.append(buf, static_cast<std::size_t>(n)); http(p); continue; }
            p->reader.feed(buf, static_cast<std::size_t>(n));
        }
        while(p->ws && !p->closing && p->sock.valid()) {
            auto msg = p->reader.next();
            if(p->reader.failed()) { p->out += encode_frame(op_close, std::string("\x03\xea", 2), false); p->closing = true; break; }   // 1002 protocol error (ws closes; maxPayload 1009)
            if(!msg) break;
            if(msg->op == op_ping) p->out += encode_frame(op_pong, msg->payload, false);
            else if(msg->op == op_pong) p->alive = true;
            else if(msg->op == op_close) { p->out += encode_frame(op_close, msg->payload.substr(0, 2), false); p->closing = true; }
            else if(msg->op == op_text || msg->op == op_binary) {
                if(msg->payload.size() > 128 * 1024) continue;
                auto v = json::try_parse(msg->payload);
                if(!v) { error(p, "Invalid game message."); continue; }
                action(p, *v);
            }
        }
    }
    void write(Peer* p) {
        while(!p->out.empty() && p->sock.valid()) {
            const long n = p->sock.send_some(p->out.data(), p->out.size());
            if(n < 0) { p->sock.close(); return; }
            if(n == 0) return;
            p->out.erase(0, static_cast<std::size_t>(n));
        }
        if(p->out.empty() && p->closing) p->sock.close();
    }
    void heartbeat() {
        const auto now = mono_ms();
        if(now < next_beat) return;
        next_beat = now + heartbeat_ms;
        for(auto& p : peers) {
            if(!p.ws || p.closing || !p.sock.valid()) continue;
            if(!p.alive) { p.sock.close(); continue; }   // ws.terminate()
            p.alive = false;
            p.out += encode_frame(op_ping, "", false);
        }
    }
    void loop() {
        next_beat = mono_ms() + heartbeat_ms;
        while(!stop) {
            std::vector<const Socket*> wait{&listener};
            for(auto& p : peers) wait.push_back(&p.sock);
            wait_readable(wait, 20);
            for(Socket s = listener.accept(); s.valid(); s = listener.accept()) { peers.emplace_back(); peers.back().sock = std::move(s); }
            for(auto& p : peers) { read(&p); }
            heartbeat();
            for(auto& p : peers) write(&p);
            // close: server.js runs disconnect() for each closed WebSocket
            for(auto it = peers.begin(); it != peers.end();) {
                if(!it->sock.valid()) { if(it->ws) disconnect(&*it); for(auto& q : peers) write(&q); it = peers.erase(it); }
                else ++it;
            }
            std::lock_guard<std::mutex> l(m);
            stats.rooms = static_cast<int>(rooms.size()); stats.peers = ws_count;
        }
        peers.clear(); rooms.clear(); ws_count = 0;
        listener.close();
    }
};

RelayServer::RelayServer() : impl_(std::make_unique<Impl>()) {}
RelayServer::~RelayServer() { stop(); }
bool RelayServer::start(const std::string& host, int port, std::string& error) {
    stop();
    if(!impl_->listener.listen(host, port, error)) return false;
    impl_->port = impl_->listener.local_port();
    impl_->stop = false;
    impl_->running = true;
    impl_->thread = std::thread([this] { impl_->loop(); impl_->running = false; });
    impl_->say("CAT ARENA co-op relay (up to " + std::to_string(kRoomSize) + " players) listening on " + (host.empty() ? "0.0.0.0" : host) + ":" + std::to_string(impl_->port));
    return true;
}
void RelayServer::stop() {
    if(!impl_) return;
    impl_->stop = true;
    if(impl_->thread.joinable()) impl_->thread.join();
}
bool RelayServer::running() const { return impl_->running; }
int RelayServer::port() const { return impl_->port; }
RelayServer::Stats RelayServer::stats() const { std::lock_guard<std::mutex> l(impl_->m); return impl_->stats; }
void RelayServer::set_log(std::function<void(const std::string&)> log) { impl_->log = std::move(log); }
void RelayServer::set_heartbeat_ms(int ms) { impl_->heartbeat_ms = ms; }

}  // namespace cat::net
