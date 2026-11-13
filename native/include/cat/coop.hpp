#pragma once
// N8 four-player co-op: the client side of the legacy multiplayer (js/net/multiplayer.js,
// legacy/index.html 15495-16060) on top of cat::net (relay protocol) and combat::Arena (simulation).
//
//   host   creates a room on a relay, runs the only simulation of enemies, waves, damage, pickups,
//          downed / revive and squad wipes, and sends each guest a 'world' snapshot every 90 ms.
//   guest  joins a room from the server list, simulates its own movement and weapons, sends its pose
//          every 48 ms and each volley ('fire'), and shows the host's world (enemies as puppets).
//
// The packets are the legacy JSON messages with the same keys and quantisation (pose, fire, world,
// map, restart, frag, shotFx), so the protocol stays the one of the original game and relay.
// Deliberate differences from the legacy client, documented in docs/N8_MULTIPLAYER.md: a guest loads
// the room's map itself (the legacy page had to be reloaded on that map), remote volleys name their
// shooter, and the host cannot change the map while the room is open (the room's map id is fixed).
#include "cat/combat.hpp"
#include "cat/enemies.hpp"
#include "cat/json.hpp"
#include "cat/net.hpp"

#include <array>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace cat::net {

inline constexpr double kPoseIntervalMs = 48, kWorldIntervalMs = 90, kSnapshotStallMs = 12000;

// Another player as this machine shows it (both roles): position, health, animation cues.
struct RemotePlayer {
    std::string id, name;
    std::uint32_t key{};      // host: the Arena teammate key; guest: 0
    double x{}, y{}, z{}, a{}, pitch{}, vx{}, vy{}, step{}, crouch{};
    double hp{100};
    bool downed{}, is_host{};
    bool placed{};            // a position has arrived (pose / snapshot): drawn and tagged only then
    double shoot_until{};     // arena clock (shotFx)
    std::array<float, 3> color{.81F, .89F, 1.F};
};

struct CoopEvent {
    enum class Kind : std::uint8_t {
        ready,          // joined a room: load `map_id` (host: the map it created the room on)
        announce,       // HUD notice (`heal` = the green style)
        disconnected,   // the room is gone (host left, relay closed, error): back to the menu with `text`
        restart,        // guest: the host restarted the round (squad wipe) — reload the map at wave 1
        remote_volley,  // another player fired `weapon` (gun sound, Marine Cat shoot pose)
    };
    Kind kind{};
    std::string text, map_id;
    bool heal{};
    int weapon{-1};
    double x{}, y{}, z{}, yaw{}, pitch{}, crouch{};
    bool positioned{};
};

struct ServerEntry { std::string id, title, host, map_id; int players{}, max_players{4}; bool open{}; };
struct ServerList {
    bool ok{};
    std::string status;   // "2 LIVE SERVERS · 3 PLAYERS ONLINE" or the error
    std::vector<ServerEntry> rooms;
};
// netBrowseServers: GET /rooms of the relay (blocking; run it with std::async).
ServerList fetch_servers(const std::string& relay_typed, int timeout_ms = 23000);

class Coop {
public:
    enum class Mode : std::uint8_t { solo, connecting, host, guest };
    Coop();
    ~Coop();
    // ---- lobby ----
    // netConnect('host') / netConnect('join', code). `map_id` is one of kMapIds.
    void host(const std::string& relay_typed, const std::string& name, const std::string& title, const std::string& map_id);
    void join(const std::string& relay_typed, const std::string& code, const std::string& name, const std::string& map_id);
    void leave(const std::string& why = {});
    Mode mode() const { return mode_; }
    bool in_room() const { return mode_ == Mode::host || mode_ == Mode::guest; }
    bool busy() const { return mode_ != Mode::solo; }
    const std::string& status() const { return status_; }
    bool status_error() const { return status_error_; }
    const std::string& room_code() const { return room_; }
    const std::string& self_id() const { return self_id_; }
    const std::string& map_id() const { return map_id_; }
    int slot() const { return slot_; }
    int players_in_room() const { return static_cast<int>(players_.size()) + (in_room() ? 1 : 0); }
    std::vector<std::string> roster() const;   // names, "(you)" first
    const std::vector<RemotePlayer>& players() const { return players_; }

    // ---- game ----
    // The arena of the current game (nullptr while loading: relay packets wait in a queue of 60, as
    // loadingNetRelayQueue). Sets the arena's NetRole.
    void attach(combat::Arena* arena, combat::Horde* horde);
    combat::Arena* arena() const { return arena_; }
    // Network I/O and timers; call once per frame. `now_ms` is a steady clock in milliseconds.
    void poll(double now_ms);
    // After each Arena::step (host: frags, volleys, downed / revived announcements; both: own volley).
    void after_step();
    // Host: the round was restarted after a squad wipe (the shell reloaded the map); tells the guests.
    void round_restarted();
    // Guest spawn near the map start, apart from the others (netFindValidSpawn).
    std::array<double, 3> find_spawn(const physics::CollisionWorld& world, double base_x, double base_y, double base_z, int slot) const;
    std::vector<CoopEvent> take_events() { auto e = std::move(events_); events_.clear(); return e; }
    // HUD lines: revive status (coopReviveStatus), room badge.
    std::string revive_status() const;
    std::string badge() const;
    struct Stats { std::uint64_t snapshots{}, snapshot_bytes{}, poses{}, bytes_sent{}, bytes_received{}; double last_world_age_ms{-1}; };
    Stats stats() const;
    // Message builders / readers, public for tests.
    json::Value pose_message() const;                               // netPose
    std::string world_message(const std::string& guest_id) const;   // netSnapshotMessage (the whole relay frame)
    void on_relay(const std::string& from, const json::Value& data, double now_ms);   // netOnRelay
private:
    void connect(const std::string& role, const std::string& relay_typed, const std::string& code, const std::string& name, const std::string& title, const std::string& map_id);
    void handle(const json::Value& packet, double now_ms);
    void apply_roster(const json::Array& list);
    void send(const json::Value& v);
    void relay(const json::Value& data, const std::string& to = {});
    void say(const std::string& text, bool heal = false) { events_.push_back({CoopEvent::Kind::announce, text, {}, heal}); }
    void set_status(const std::string& s, bool error = false) { status_ = s; status_error_ = error; }
    RemotePlayer* player(const std::string& id);
    json::Value hero_local() const;
    json::Value hero(const combat::Teammate& t) const;
    void sync_host_players();

    std::unique_ptr<Connection> conn_;
    Mode mode_{Mode::solo};
    std::string role_wanted_, pending_code_, name_, title_, map_id_, room_, self_id_, peer_id_, peer_name_, status_;
    bool status_error_{};
    int slot_{};
    double last_pose_{-1e9}, last_world_sent_{-1e9}, last_world_{-1}, now_ms_{};
    std::vector<RemotePlayer> players_;
    combat::Arena* arena_{};
    combat::Horde* horde_{};
    std::deque<std::pair<std::string, json::Value>> loading_queue_;
    std::vector<CoopEvent> events_;
    // guest view of the host's revive state
    std::string revive_for_;
    double revive_time_{}, wipe_{};
    bool you_downed_{};
    Stats stats_;
};

}  // namespace cat::net
