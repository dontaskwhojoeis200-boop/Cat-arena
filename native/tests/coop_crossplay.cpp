// N8 cross-play helper: one native co-op client running in real time against the ORIGINAL browser game
// (legacy/index.html in Chromium, driven by tools/n8/crossplay_legacy.cjs).
//
//   cat_arena_coop_crossplay host  <assets/n1> <port> <seconds>   native relay on <port> + native host on Reactor Bastion
//   cat_arena_coop_crossplay guest <assets/n1> <relay> <seconds>  native guest of the first room listed by <relay>
//
// Prints one JSON object per line: {"event": ...} for milestones and {"status": ...} once a second.
// Commands on stdin (one per line), so that the driver sets the pace (the headless browser is slow):
//   freeze   host: no enemies, nothing left to spawn; the first teammate back to 100 HP, not downed
//   damage   host: 30 HP of damage to the first teammate (remoteApplyDamage)
//   down     host: 200 HP of damage to the first teammate (downed)
//   walk / stop   guest: walk forward (and fire now and then) / stand still
// The native host itself is kept at full health (it is not what is under test).
#include "cat/assets.hpp"
#include "cat/coop.hpp"
#include "cat/navigation.hpp"
#include "cat/relay.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

using namespace cat;
namespace {
std::string esc(const std::string& s) { std::string o; for(char c : s) { if(c == '"' || c == '\\') o += '\\'; o += c; } return o; }
void line(const std::string& json) { std::printf("%s\n", json.c_str()); std::fflush(stdout); }
}

int main(int argc, char** argv) {
    if(argc < 5) { std::printf("usage: cat_arena_coop_crossplay host <assets> <port> <seconds> | guest <assets> <relay> <seconds>\n"); return 2; }
    const std::string mode = argv[1], root = argv[2], where = argv[3];
    const double seconds = std::stod(argv[4]);
    const auto doc = assets::load_data(root + "/maps/reactor-bastion/map.cadata");
    const physics::CollisionWorld world(doc.at("compiled"));
    nav::NavGraph graph(world);
    const auto& sp = world.spawns().front();
    net::RelayServer relay;
    net::Coop coop;
    std::string addr = where;
    if(mode == "host") {
        std::string error;
        if(!relay.start("127.0.0.1", std::stoi(where), error)) { line("{\"event\": \"error\", \"text\": \"" + esc(error) + "\"}"); return 1; }
        addr = "127.0.0.1:" + std::to_string(relay.port());
        coop.host(addr, "Native Host", "Native Server", "reactor-bastion-v1");
    } else {
        const auto list = net::fetch_servers(addr, 10000);
        if(!list.ok || list.rooms.empty()) { line("{\"event\": \"error\", \"text\": \"no room listed: " + esc(list.status) + "\"}"); return 1; }
        line("{\"event\": \"listed\", \"room\": \"" + list.rooms[0].id + "\", \"title\": \"" + esc(list.rooms[0].title) + "\", \"host\": \"" + esc(list.rooms[0].host) + "\", \"players\": " + std::to_string(list.rooms[0].players) + "}");
        coop.join(addr, list.rooms[0].id, "Native Guest", list.rooms[0].map_id);
    }
    std::mutex cmd_mutex;
    std::deque<std::string> commands;
    std::thread([&] { std::string l; while(std::getline(std::cin, l)) { std::lock_guard<std::mutex> g(cmd_mutex); commands.push_back(l); } }).detach();
    bool walking = mode == "guest";
    std::unique_ptr<combat::Arena> arena;
    std::unique_ptr<combat::Horde> horde;
    const auto t0 = std::chrono::steady_clock::now();
    auto ms = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
    double next_status = 1000, first_pose_ms = -1;
    bool left = false;
    std::uint64_t ticks = 0;
    while(ms() < seconds * 1000 && !left) {
        coop.poll(ms());
        for(const auto& e : coop.take_events()) {
            using K = net::CoopEvent::Kind;
            if(e.kind == K::ready) {
                arena = std::make_unique<combat::Arena>(world, 4242);
                double x = sp.x, y = sp.y, z = sp.z;
                if(coop.mode() == net::Coop::Mode::guest) { const auto s = coop.find_spawn(world, sp.x, sp.y, sp.z, coop.slot()); x = s[0]; y = s[1]; z = s[2]; }
                arena->spawn(x, y, z, sp.a);
                arena->set_loadout(0, 1);
                horde = std::make_unique<combat::Horde>(*arena, graph);
                arena->attach(horde.get());
                if(coop.mode() == net::Coop::Mode::host) horde->begin_wave(1);
                coop.attach(arena.get(), horde.get());
                line("{\"event\": \"ready\", \"role\": \"" + std::string(coop.mode() == net::Coop::Mode::host ? "host" : "guest") + "\", \"room\": \"" + coop.room_code() + "\", \"slot\": " + std::to_string(coop.slot()) + "}");
            } else if(e.kind == K::announce) line("{\"event\": \"announce\", \"text\": \"" + esc(e.text) + "\"}");
            else if(e.kind == K::disconnected) { line("{\"event\": \"disconnected\", \"text\": \"" + esc(e.text) + "\"}"); left = true; }
            else if(e.kind == K::restart) line("{\"event\": \"restart\"}");
            else if(e.kind == K::remote_volley) line("{\"event\": \"volley\", \"weapon\": " + std::to_string(e.weapon) + "}");
        }
        if(arena) {   // real time at 120 Hz
            const auto target = static_cast<std::uint64_t>(ms() / (1000.0 / 120));
            for(; ticks < target; ++ticks) {
                std::uint32_t keys = 0;
                if(coop.mode() == net::Coop::Mode::guest && walking) {   // walk forward for 1.5 s of every 4 s, fire now and then
                    const double t = static_cast<double>(ticks) / 120;
                    if(std::fmod(t, 4) < 1.5) keys |= physics::key_forward;
                    if(std::fmod(t, 3) < .1) keys |= combat::key_fire;
                    if(std::fmod(t, 4) < .01) arena->player().aim(120, 0);
                }
                if(coop.mode() == net::Coop::Mode::host && !arena->player().state().downed) arena->set_health(100);   // the native host is not under test
                arena->step({keys});
                coop.after_step();
            }
            if(coop.mode() == net::Coop::Mode::host && !arena->team().empty()) {
                auto& m = arena->team().front();
                if(m.pose_ms >= 0 && first_pose_ms < 0) { first_pose_ms = ms(); line("{\"event\": \"first_pose\", \"name\": \"" + esc(m.name) + "\"}"); }
            }
        }
        std::deque<std::string> todo;
        { std::lock_guard<std::mutex> g(cmd_mutex); todo.swap(commands); }
        for(const auto& c : todo) {
            if(c == "walk" || c == "stop") { walking = c == "walk"; line("{\"event\": \"" + c + "\"}"); continue; }
            if(!arena || coop.mode() != net::Coop::Mode::host || arena->team().empty()) { line("{\"event\": \"ignored\", \"command\": \"" + esc(c) + "\"}"); continue; }
            auto& m = arena->team().front();
            if(c == "freeze") { arena->clear_targets(); horde->stage_wave(1, 50, 50, 0); m.hp = 100; m.downed = false; line("{\"event\": \"frozen\"}"); }
            else if(c == "damage") { arena->remote_damage(m, 30); line("{\"event\": \"damaged\", \"hp\": " + std::to_string(m.hp) + "}"); }
            else if(c == "down") { arena->remote_damage(m, 200); line("{\"event\": \"downed\", \"downed\": " + std::string(m.downed ? "true" : "false") + "}"); }
        }
        if(ms() > next_status) {
            next_status += 1000;
            std::string s = "{\"status\": {\"t\": " + std::to_string(static_cast<int>(ms())) + ", \"mode\": " + std::to_string(static_cast<int>(coop.mode())) + ", \"players\": " + std::to_string(coop.players_in_room());
            if(arena) {
                const auto& p = arena->player().state();
                s += ", \"hp\": " + std::to_string(arena->health()) + ", \"downed\": " + (p.downed ? "true" : "false") + ", \"x\": " + std::to_string(p.x) + ", \"y\": " + std::to_string(p.y);
                s += ", \"enemies\": " + std::to_string(arena->targets().size()) + ", \"wave\": " + std::to_string(horde->wave_number()) + ", \"frags\": " + std::to_string(arena->frags());
                s += ", \"worldAgeMs\": " + std::to_string(static_cast<int>(coop.stats().last_world_age_ms)) + ", \"posesReceived\": " + std::to_string(coop.stats().poses);
                s += ", \"others\": [";
                bool first = true;
                for(const auto& o : coop.players()) {
                    s += std::string(first ? "" : ", ") + "{\"name\": \"" + esc(o.name) + "\", \"x\": " + std::to_string(o.x) + ", \"y\": " + std::to_string(o.y) + ", \"hp\": " + std::to_string(o.hp) + ", \"downed\": " + (o.downed ? "true" : "false") + ", \"host\": " + (o.is_host ? "true" : "false") + "}";
                    first = false;
                }
                s += "]";
            }
            line(s + "}}");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    coop.attach(nullptr, nullptr);
    coop.leave();
    line("{\"event\": \"end\"}");
    relay.stop();
    return 0;
}
