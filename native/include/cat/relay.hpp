#pragma once
// N8: the Cat Arena relay in C++ — a port of reference/multiplayer/server.js (VOID ARENA v3.24 relay,
// build VA-MODULAR-RB-011): HTTP GET /rooms (the public server list) and /health, WebSocket /ws with
// the create / join / relay / roster / peer messages, rooms of four (one host, three guests), 30 s
// heartbeat, 100 rooms, 410 connections, 128 KiB messages. Same JSON on the wire, so native clients
// work with the original Node.js server and the original game clients work with this one.
//
// Uses: `cat_arena --relay-server [--port 8787]` (a dedicated relay, e.g. on a LAN PC or a VPS) and the
// "HOST ON THIS PC" button of the multiplayer page (an in-process relay other players reach at
// ws://<this PC>:8787). Differences from server.js: GET / answers with a short text page (there is no
// browser game to serve); everything else is the same, including the error texts.
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace cat::net {

inline constexpr const char* kBuildId = "VA-MODULAR-RB-011";   // window.VOID_BUILD_ID (index.html 8177)
inline constexpr int kRoomSize = 4, kMaxRooms = 100, kMaxPeers = 410, kDefaultPort = 8787;
// MAP_IDS of server.js, in the native map order.
inline constexpr const char* kMapIds[4] = {"helios-crucible-v1", "bastion-keep-v1", "reactor-bastion-v1", "q3-gallery-v1"};

class RelayServer {
public:
    struct Stats { int rooms{}, peers{}; std::uint64_t messages_relayed{}, bytes_in{}, bytes_out{}; };
    RelayServer();
    ~RelayServer();
    RelayServer(const RelayServer&) = delete;
    RelayServer& operator=(const RelayServer&) = delete;
    // Listens on host:port (port 0 = any free port) and serves on a background thread.
    bool start(const std::string& host, int port, std::string& error);
    void stop();
    bool running() const;
    int port() const;
    Stats stats() const;
    // Log lines (the console.log calls of server.js); called on the server thread.
    void set_log(std::function<void(const std::string&)> log);
    // Heartbeat period (30 s in server.js; tests shorten it).
    void set_heartbeat_ms(int ms);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cat::net
