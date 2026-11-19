// N8 co-op tests.
//  1. Rules (no network): enemyTargetFor, remoteApplyDamage, downed instead of respawn, revive in 4 s
//     with line of sight, squad wipe after 2.7 s, kills by other players, medkits for teammates,
//     guest-side damage / pickups, puppet enemies from snapshots, packet shapes.
//  2. Four real clients (one host, three guests) through the native relay over TCP on this machine,
//     each with its own simulation of Reactor Bastion: server list, join, snapshots, poses, remote
//     shots and kills, damage, downed and revive, squad wipe and restart, leaving, host leaving.
#include "cat/assets.hpp"
#include "cat/coop.hpp"
#include "cat/navigation.hpp"
#include "cat/relay.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace {
using namespace cat;
int failures = 0, checks = 0;
void check(bool ok, const std::string& what) { ++checks; if(!ok) { ++failures; std::printf("FAIL %s\n", what.c_str()); } }

struct World {
    assets::Value doc;
    std::unique_ptr<physics::CollisionWorld> collision;
    std::unique_ptr<nav::NavGraph> graph;
};
World load(const std::string& root, const std::string& map) {
    World w;
    w.doc = assets::load_data(root + "/maps/" + map + "/map.cadata");
    w.collision = std::make_unique<physics::CollisionWorld>(w.doc.at("compiled"));
    w.graph = std::make_unique<nav::NavGraph>(*w.collision);
    return w;
}

// ---- 1. rules ----------------------------------------------------------------------------------------------------
void rules(const World& w) {
    const int before = failures;
    const auto& sp = w.collision->spawns().front();
    {   // targeting
        combat::Arena a(*w.collision, 3);
        a.spawn(sp.x, sp.y, sp.z, sp.a);
        auto aim = a.target_for(sp.x + 5, sp.y);
        check(aim.key == 0 && aim.x == sp.x, "solo: enemies target the local player");
        a.set_role(combat::NetRole::host);
        auto& m = a.add_teammate("g1", "Guest");
        m.x = sp.x + 4; m.y = sp.y; m.z = sp.z;
        aim = a.target_for(sp.x + 5, sp.y);
        check(aim.key == m.key, "host: the nearest live player is the target");
        aim = a.target_for(sp.x - 5, sp.y);
        check(aim.key == 0, "host: the local player when nearer");
        a.remote_damage(*a.teammate(m.key), 150);
        check(a.teammate(m.key)->downed && a.teammate(m.key)->hp == 0, "remoteApplyDamage: 0 hp = downed");
        aim = a.target_for(sp.x + 5, sp.y);
        check(aim.key == 0, "downed players are not targeted");
    }
    {   // the host goes down instead of respawning; revive with F in 4 s; squad wipe
        combat::Arena a(*w.collision, 4);
        a.spawn(sp.x, sp.y, sp.z, sp.a);
        a.set_loadout(0, 1);
        a.set_role(combat::NetRole::host);
        auto& m = a.add_teammate("g1", "Guest");
        m.x = sp.x + 1.2; m.y = sp.y; m.z = sp.z;
        const std::uint32_t key = m.key;
        a.step({});
        a.step({combat::key_fixture_hurt}); a.step({}); a.step({combat::key_fixture_hurt}); a.step({}); a.step({combat::key_fixture_hurt});
        check(a.player().state().downed && a.health() == 0 && !a.dead(), "host with a teammate: down, no respawn");
        bool wiped = false;
        int ticks = 0;
        a.teammate(key)->holding_revive = true;
        while(a.player().state().downed && ticks < 600) { a.step({}); ++ticks; wiped = wiped || a.events().wipe; }
        check(!a.player().state().downed && a.health() == 45 && std::abs(ticks - 480) <= 2 && !wiped, "a teammate holding F revives in 4 s (" + std::to_string(ticks) + " ticks) with 45 hp");
        a.teammate(key)->holding_revive = false;
        for(int i = 0; i < 250; ++i) a.step({});   // the 2 s grace after a revive
        a.remote_damage(*a.teammate(key), 200);
        for(int i = 0; i < 8 && !a.player().state().downed; ++i) a.step({i % 2 ? 0U : combat::key_fixture_hurt});
        ticks = 1;   // the wipe clock started inside the step that downed the host
        while(!wiped && ticks < 600) { a.step({}); ++ticks; wiped = a.events().wipe; }
        check(wiped && std::abs(ticks - 325) <= 4, "every player down: squad wipe after 2.7 s (" + std::to_string(ticks) + " ticks)");
    }
    {   // revive needs line of sight and 3.2 m
        combat::Arena a(*w.collision, 5);
        a.spawn(sp.x, sp.y, sp.z, sp.a);
        check(a.near_revive(sp.x, sp.y, sp.z, false, sp.x + 1, sp.y, sp.z, true), "netNearRevive: 1 m, same floor");
        check(!a.near_revive(sp.x, sp.y, sp.z, false, sp.x + 3.3, sp.y, sp.z, true), "netNearRevive: beyond 3.2 m");
        check(!a.near_revive(sp.x, sp.y, sp.z, false, sp.x + 1, sp.y, sp.z + 2, true), "netNearRevive: another floor");
        check(!a.near_revive(sp.x, sp.y, sp.z, true, sp.x + 1, sp.y, sp.z, true), "netNearRevive: a downed player cannot revive");
    }
    {   // kills by another player: frag credited to them, no reroll here; projectiles carry the owner
        combat::Arena a(*w.collision, 6);
        a.spawn(sp.x, sp.y, sp.z, sp.a);
        a.set_loadout(0, 1);
        nav::NavGraph graph(*w.collision);
        combat::Horde h(a, graph);
        a.attach(&h);
        a.set_role(combat::NetRole::host);
        auto& m = a.add_teammate("g1", "Guest");
        m.x = sp.x; m.y = sp.y; m.z = sp.z; m.a = sp.a; m.pitch = 0;
        auto& b = h.stage_enemy(0, sp.x + std::cos(sp.a) * 3, sp.y + std::sin(sp.a) * 3, sp.z, sp.a + 3.14159);
        b.hp = 1;
        const int loadout = a.current_weapon();
        a.queue_remote_fire(m.key, 0);
        a.step({});
        check(a.events().remote_shots.size() == 1 && a.events().remote_frags.size() == 1 && a.current_weapon() == loadout && a.frags() == 0 && a.events().damage.empty(), "a teammate's hitscan kill: 'frag' for them, the host keeps its weapon");
        a.queue_remote_fire(m.key, 0);
        a.step({});
        check(a.events().remote_shots.empty(), "teammate fire rate limited (delay x 0.88)");
        auto& b2 = h.stage_enemy(0, sp.x + std::cos(sp.a) * 5, sp.y + std::sin(sp.a) * 5, sp.z, 0);
        b2.hp = 1;
        for(int i = 0; i < 60; ++i) a.step({});
        a.queue_remote_fire(m.key, 2);   // rocket launcher
        bool frag = false;
        for(int i = 0; i < 120 && !frag; ++i) { a.step({}); frag = !a.events().remote_frags.empty(); }
        check(frag && a.frags() == 0, "a teammate's rocket kill is credited to them (projectile owner)");
    }
    {   // medkits for teammates
        combat::Arena a(*w.collision, 7);
        a.spawn(sp.x, sp.y, sp.z, sp.a);
        a.set_role(combat::NetRole::host);
        auto& m = a.add_teammate("g1", "Guest");
        check(!a.pickups().empty(), "Reactor Bastion has medkits");
        if(!a.pickups().empty()) {
            const auto k = a.pickups().front();
            m.x = k.x; m.y = k.y; m.z = k.z; m.hp = 30;
            a.step({});
            check(std::abs(a.teammate(m.key)->hp - 70) < 1e-9 && a.pickups().front().cool > 12.9, "a teammate on a medkit heals 40, cooldown 13 s");
        }
    }
    {
        combat::Arena a(*w.collision, 7);
        a.spawn(sp.x, sp.y, sp.z, sp.a);
        combat::Horde h(a, *w.graph);
        auto dead = h.stage_enemy(0, sp.x, sp.y, sp.z, sp.a);
        dead.hp = -50;
        h.enemy_died(dead);
        check(h.deaths().back().gibbed && h.corpses().empty(), "default overkill produces gibs");
        h.set_gibs(false);
        h.enemy_died(dead);
        check(!h.deaths().back().gibbed && h.corpses().size() == 1, "gibs off preserves a normal death animation");
    }
    {   // enemy shots aimed at a teammate (legacy 12285-12297)
        combat::Arena a(*w.collision, 9);
        a.spawn(sp.x, sp.y, sp.z, sp.a);
        a.set_loadout(0, 1);
        nav::NavGraph graph(*w.collision);
        combat::Horde h(a, graph);
        a.attach(&h);
        a.set_role(combat::NetRole::host);
        h.stage_wave(1, 50, 50, 0);
        auto& m = a.add_teammate("g1", "Guest");
        const double dx = std::cos(sp.a), dy = std::sin(sp.a);
        m.x = sp.x + dx * 2.5; m.y = sp.y + dy * 2.5; m.z = sp.z; m.a = sp.a;
        const std::uint32_t key = m.key;
        a.remote_damage(*a.teammate(key), 150);
        // fly a shot from 1 m in front of the host towards the downed teammate; true when it struck a player
        auto fly = [&](double from, std::uint32_t target) {
            combat::EnemyShot s;
            s.x = sp.x + dx * from; s.y = sp.y + dy * from; s.z = sp.z + 1.16; s.vx = dx * 10; s.vy = dy * 10; s.life = 3; s.damage = 12; s.target_key = target;
            h.stage_shot(s);
            bool struck = false;
            for(int i = 0; i < 40 && !h.shots().empty(); ++i) {
                a.clear_targets();
                a.step({});
                for(const auto& im : h.impacts()) struck = struck || im.player;
            }
            return struck && h.shots().empty();
        };
        check(fly(1.0, key) && a.teammate(key)->downed && a.teammate(key)->hp == 0, "a shot at a downed teammate hits their body and is consumed, no damage");
        // a shot aimed at a player who left is tested against the host's box (q = target || player), no damage
        check(fly(-1.2, 999) && a.health() == 100, "a shot aimed at a departed player stops on the host, no damage");
    }
    {   // guest side: no damage authority, puppets from snapshots
        combat::Arena a(*w.collision, 8);
        a.spawn(sp.x, sp.y, sp.z, sp.a);
        a.set_loadout(0, 1);
        nav::NavGraph graph(*w.collision);
        combat::Horde h(a, graph);
        a.attach(&h);
        a.set_role(combat::NetRole::guest);
        h.set_puppet(true);
        a.step({combat::key_fixture_hurt});
        check(a.health() == 100, "guest: takeDamage is the host's");
        combat::WorldState ws;
        ws.wave = 3; ws.total = 15; ws.kills = 2; ws.spawned = 9;
        ws.bots.push_back({77, 2, sp.x + 6, sp.y, sp.z, 1, 80, 120, 0, 1, 0, 0, 0, 0, 0});
        ws.bots.push_back({78, 18, sp.x - 6, sp.y, sp.z + 4, 0, 50, 50});
        ws.shots.push_back({sp.x, sp.y, sp.z + 1, 10, 0, 0, .5, combat::ShotKind::fireball, 9});
        h.queue_world(ws);
        a.step({});
        check(a.targets().size() == 2 && a.targets()[0].uid == 77 && a.targets()[1].flying && h.wave_number() == 3 && h.remaining() == 13 && h.shots().size() == 1, "snapshot: puppets, wave numbers, enemy shots");
        const double x0 = a.targets()[0].x;
        ws.bots[0].x += 2;
        ws.bots.pop_back();
        h.queue_world(ws);
        a.step({});
        check(a.targets().size() == 1 && h.deaths().size() == 1 && h.deaths()[0].uid == 78, "a creature missing from the snapshot dies here");
        check(a.targets()[0].x > x0 && a.targets()[0].x < x0 + 2, "puppets approach the host's position (netGuestInterp)");
        for(int i = 0; i < 70; ++i) a.step({});
        check(h.shots().empty(), "puppet enemy shots expire by their life");
    }
    std::printf("%s co-op rules (targeting, downed, revive, wipe, remote kills, medkits, guest puppets)\n", failures == before ? "PASS" : "FAIL");
}

// ---- 2. four clients through the relay ------------------------------------------------------------------------------
struct Client {
    std::string name;
    std::unique_ptr<combat::Arena> arena;
    std::unique_ptr<nav::NavGraph> graph;
    std::unique_ptr<combat::Horde> horde;
    net::Coop coop;   // last: destroyed first, while the arena it points to still exists
    std::uint32_t keys{};
    std::uint64_t first_round{};   // soak: snapshot rounds the host sent before this guest joined
    std::vector<net::CoopEvent> events;
    void start(const World& w, std::uint32_t seed, double x, double y, double z, double a) {
        coop.attach(nullptr, nullptr);
        arena = std::make_unique<combat::Arena>(*w.collision, seed);
        arena->spawn(x, y, z, a);
        arena->set_loadout(0, 1);
        graph = std::make_unique<nav::NavGraph>(*w.collision);
        horde = std::make_unique<combat::Horde>(*arena, *graph);
        arena->attach(horde.get());
        if(coop.mode() == net::Coop::Mode::host) horde->begin_wave(1);
        coop.attach(arena.get(), horde.get());
    }
    void pump(double now_ms) { coop.poll(now_ms); for(auto& e : coop.take_events()) events.push_back(e); }
    bool saw(net::CoopEvent::Kind k, const std::string& text = {}) const {
        for(const auto& e : events) if(e.kind == k && (text.empty() || e.text.find(text) != std::string::npos)) return true;
        return false;
    }
};

void network(const World& w, const std::string& external) {
    const int before = failures;
    net::RelayServer relay;
    std::string error;
    if(external.empty() && !relay.start("127.0.0.1", 0, error)) { check(false, "relay: " + error); return; }
    const std::string addr = external.empty() ? "127.0.0.1:" + std::to_string(relay.port()) : external;
    std::vector<std::unique_ptr<Client>> c;
    for(const char* n : {"Host", "Ash", "Brick", "Cinder"}) { c.push_back(std::make_unique<Client>()); c.back()->name = n; }
    double clock = 0;   // simulated milliseconds
    auto pump_all = [&](int ms_real = 2) {
        for(auto& x : c) x->pump(clock);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms_real));
        for(auto& x : c) x->pump(clock);
    };
    auto until = [&](const std::function<bool()>& pred, int tries = 1500) { for(int i = 0; i < tries && !pred(); ++i) pump_all(); return pred(); };
    // host
    c[0]->coop.host(addr, "Host", "", "reactor-bastion-v1");
    check(until([&] { return c[0]->saw(net::CoopEvent::Kind::ready); }), "host: room created");
    const auto& sp = w.collision->spawns().front();
    c[0]->start(w, 21, sp.x, sp.y, sp.z, sp.a);
    // server list
    const auto list = net::fetch_servers(addr, 5000);
    check(list.ok && list.rooms.size() == 1 && list.rooms[0].players == 1 && list.rooms[0].map_id == "reactor-bastion-v1" && list.rooms[0].title == "Hosts Server", "server browser lists the room: " + list.status);
    // three guests join from the list, spawn around the host's start (netFindValidSpawn)
    for(std::size_t i = 1; i < 4; ++i) {
        c[i]->coop.join(addr, list.rooms.empty() ? "" : list.rooms[0].id, c[i]->name, list.rooms.empty() ? "" : list.rooms[0].map_id);
        check(until([&] { return c[i]->saw(net::CoopEvent::Kind::ready); }), c[i]->name + " joined");
        const auto s = c[i]->coop.find_spawn(*w.collision, sp.x, sp.y, sp.z, c[i]->coop.slot());
        c[i]->start(w, static_cast<std::uint32_t>(30 + i), s[0], s[1], s[2], sp.a);
        check(c[i]->arena->role() == combat::NetRole::guest && c[i]->horde->puppet(), c[i]->name + ": guest simulation");
    }
    check(until([&] { return c[0]->arena->team().size() == 3 && c[3]->coop.players_in_room() == 4 && c[1]->coop.players_in_room() == 4; }), "roster: 4/4 on every client, 3 teammates in the host simulation");
    check(c[0]->coop.badge() == "HOSTING · 4/4 PLAYERS" && c[2]->coop.badge() == "CONNECTED · 4/4 PLAYERS", "room badges");
    check(c[1]->saw(net::CoopEvent::Kind::announce, "JOINED THE ARENA") || c[0]->saw(net::CoopEvent::Kind::announce, "JOINED THE ARENA"), "join announcements");
    // play: everybody steps 120 Hz; guests walk forward for a while
    auto play = [&](int ticks, const std::function<void(int)>& each = {}) {
        for(int t = 0; t < ticks; ++t) {
            if(each) each(t);
            for(auto& x : c) {
                x->arena->step({x->keys});
                x->coop.after_step();
            }
            clock += 1000.0 / 120;
            if(t % 6 == 0) pump_all(1); else for(auto& x : c) x->pump(clock);
        }
    };
    c[1]->keys = physics::key_forward;
    play(240);
    c[1]->keys = 0;
    play(60);
    const auto& g1 = c[1]->arena->player().state();
    const auto* m1 = c[0]->arena->teammate(c[0]->coop.players().front().key);
    check(m1 && std::hypot(m1->x - g1.x, m1->y - g1.y) < .5, "guest pose reaches the host simulation (" + std::to_string(m1 ? std::hypot(m1->x - g1.x, m1->y - g1.y) : -1) + " m apart)");
    const auto host_bots = c[0]->arena->targets().size();
    bool same = c[2]->arena->targets().size() == host_bots && host_bots > 0;
    if(same) for(std::size_t i = 0; i < host_bots; ++i) same = same && c[2]->arena->targets()[i].uid == c[0]->arena->targets()[i].uid;
    check(same, "guests show the host's enemies (" + std::to_string(c[2]->arena->targets().size()) + " / " + std::to_string(host_bots) + ")");
    check(c[3]->horde->wave_number() == c[0]->horde->wave_number() && c[3]->horde->wave_total() == c[0]->horde->wave_total(), "wave numbers synchronised");
    const auto host_view = c[2]->coop.players();
    bool host_seen = false;
    for(const auto& p : host_view) if(p.is_host) host_seen = std::hypot(p.x - c[0]->arena->player().state().x, p.y - c[0]->arena->player().state().y) < .5;
    check(host_seen && host_view.size() == 3, "guests see the host and each other");
    // remote shot and kill: an enemy with 1 hp in front of guest 1 on the host; guest 1 fires
    {
        auto& gp = c[1]->arena->player().state();
        auto& b = c[0]->horde->stage_enemy(0, gp.x + std::cos(gp.a) * 3, gp.y + std::sin(gp.a) * 3, gp.z, gp.a + 3.14159);
        b.hp = 1;
        const auto frags = c[1]->arena->frags();
        c[1]->keys = combat::key_fire;
        play(2);
        c[1]->keys = 0;
        play(60);
        check(c[1]->arena->frags() == frags + 1, "guest 1's shot killed the host's enemy and the 'frag' came back");
        const auto& seen = c[2]->events;
        check(std::any_of(seen.begin(), seen.end(), [&](const net::CoopEvent& e) {
            return e.kind == net::CoopEvent::Kind::remote_volley && e.positioned && std::hypot(e.x - gp.x, e.y - gp.y) < .5;
        }), "another guest received a positioned shotFx for guest 1's volley");
    }
    {
        auto& host = *c[0]->arena;
        host.set_loadout(4, 2);
        host.player().state().pitch = -.55;
        host.clear_targets();
        c[0]->horde->stage_wave(1, 50, 50, 0);
        play(40);
        c[0]->keys = combat::key_fire;
        bool replicated = false, blasted = false;
        play(340, [&](int tick) {
            if(tick == 2) c[0]->keys = 0;
            const auto projectiles = c[2]->arena->projectiles();
            for(const auto& p : projectiles)
                if((p.id & 0x80000000U) != 0 && p.kind == combat::FireKind::rocket) replicated = true;
            if(!c[2]->arena->events().blasts.empty()) blasted = true;
        });
        check(replicated, "a spectator guest receives a host rocket in flight");
        check(blasted, "a spectator guest receives the host detonation");
        check(c[2]->arena->projectiles().empty(), "host projectile is removed from subsequent snapshots");
    }
    // damage, downed, revive by another guest holding F. The host's wave is frozen first (no enemies,
    // nothing left to spawn) so that only the test deals damage.
    c[0]->arena->clear_targets();
    c[0]->horde->stage_wave(1, 50, 50, 0);
    play(360);
    {
        auto settle = [&](const std::function<bool()>& pred) { for(int i = 0; i < 400 && !pred(); ++i) { clock += 5; pump_all(1); } return pred(); };
        auto& m = *c[0]->arena->teammate(c[0]->coop.players()[0].key);   // guest 1 (Ash)
        check(c[1]->arena->targets().empty() && !m.downed, "the frozen wave reaches the guests");
        const double hp0 = m.hp;
        c[0]->arena->remote_damage(m, 30);
        check(settle([&] { return std::abs(c[1]->arena->health() - (hp0 - 30)) < 1e-9; }), "damage on the host reaches the guest (hp " + std::to_string(c[1]->arena->health()) + ", expected " + std::to_string(hp0 - 30) + ")");
        c[0]->arena->remote_damage(m, 100);
        settle([&] { return c[1]->arena->player().state().downed; });
        play(30);
        check(c[1]->arena->player().state().downed && c[1]->saw(net::CoopEvent::Kind::announce, "YOU ARE DOWN"), "guest 1 is down");
        check(c[1]->coop.revive_status().find("YOU ARE DOWN") == 0, "downed status line: " + c[1]->coop.revive_status());
        // guest 2 walks next to guest 1 (teleport within the pose limits) and holds F
        auto& p2 = c[2]->arena->player().state();
        const auto& p1 = c[1]->arena->player().state();
        p2.x = p1.x + .9; p2.y = p1.y; p2.z = p1.z; p2.vx = p2.vy = 0;
        play(30);
        check(c[2]->coop.revive_status().find("HOLD F TO REVIVE Ash") == 0, "revive prompt: " + c[2]->coop.revive_status());
        c[2]->keys = combat::key_revive;
        play(240);
        check(c[2]->coop.revive_status().find("REVIVING Ash") == 0, "revive progress: " + c[2]->coop.revive_status());
        play(320);
        c[2]->keys = 0;
        play(30);
        check(!c[1]->arena->player().state().downed && std::abs(c[1]->arena->health() - 45) < 1e-9 && c[1]->saw(net::CoopEvent::Kind::announce, "REVIVED"), "guest 1 revived with 45 hp");
    }
    // squad wipe: everybody down -> restart for everybody
    {
        for(auto& t : c[0]->arena->team()) c[0]->arena->remote_damage(t, 500);
        bool wipe = false;
        play(12, [&](int) { c[0]->keys = combat::key_fixture_hurt; });
        c[0]->keys = 0;
        for(int i = 0; i < 400 && !wipe; ++i) { play(1); wipe = c[0]->arena->events().wipe; }
        check(wipe, "all four down: squad wipe on the host");
        // what the game shell does on a wipe: restart the map at wave 1, tell the guests
        c[0]->start(w, 22, sp.x, sp.y, sp.z, sp.a);
        c[0]->coop.round_restarted();
        play(30);
        bool all = true;
        for(std::size_t i = 1; i < 4; ++i) all = all && c[i]->saw(net::CoopEvent::Kind::restart);
        check(all && c[0]->arena->team().size() == 3, "guests told to restart; the host keeps its three teammates");
    }
    // a guest leaves, then the host leaves
    c[3]->coop.leave();
    check(until([&] { return c[0]->arena->team().size() == 2 && c[1]->coop.players_in_room() == 3; }), "a guest leaves: 3 players left");
    check(c[0]->saw(net::CoopEvent::Kind::announce, "TEAMMATE LEFT"), "host told: teammate left");
    const auto stats = c[0]->coop.stats();
    c[0]->coop.leave();
    check(until([&] { return c[1]->saw(net::CoopEvent::Kind::disconnected, "Host disconnected") && c[2]->saw(net::CoopEvent::Kind::disconnected, "Host disconnected"); }), "the host leaves: guests are told the server closed");
    check(c[1]->arena->role() == combat::NetRole::solo, "a disconnected guest's arena is solo again");
    const double seconds = clock / 1000;
    std::printf("  host sent %llu snapshot rounds, %.1f KiB/s of snapshots to three guests over %.1f s of play; %llu poses received\n",
                static_cast<unsigned long long>(stats.snapshots), static_cast<double>(stats.snapshot_bytes) / 1024 / seconds, seconds, static_cast<unsigned long long>(stats.poses));
    std::printf("%s four players through %s (join, snapshots, poses, remote kill, damage, downed, revive, wipe, leave)\n", failures == before ? "PASS" : "FAIL", external.empty() ? "the native relay" : ("the relay at " + external).c_str());
    relay.stop();
}

// ---- 3. soak: four clients with live waves for `seconds` of game time --------------------------------------------
// Guests and host move, jump and fire at random; a squad wipe restarts the round as the game does.
// Checks that nobody is disconnected, snapshots keep coming and the guests keep the host's wave.
void soak(const World& w, double seconds) {
    const int before = failures;
    net::RelayServer relay;
    std::string error;
    if(!relay.start("127.0.0.1", 0, error)) { check(false, "relay: " + error); return; }
    const std::string addr = "127.0.0.1:" + std::to_string(relay.port());
    std::vector<std::unique_ptr<Client>> c;
    for(const char* n : {"Host", "Ash", "Brick", "Cinder"}) { c.push_back(std::make_unique<Client>()); c.back()->name = n; }
    double clock = 0;
    auto pump_all = [&] { for(auto& x : c) x->pump(clock); };
    auto until = [&](const std::function<bool()>& pred) { for(int i = 0; i < 3000 && !pred(); ++i) { pump_all(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); } return pred(); };
    const auto& sp = w.collision->spawns().front();
    c[0]->coop.host(addr, "Host", "Soak", "reactor-bastion-v1");
    check(until([&] { return c[0]->saw(net::CoopEvent::Kind::ready); }), "soak: host");
    c[0]->start(w, 77, sp.x, sp.y, sp.z, sp.a);
    for(std::size_t i = 1; i < 4; ++i) {
        c[i]->coop.join(addr, c[0]->coop.room_code(), c[i]->name, "reactor-bastion-v1");
        check(until([&] { return c[i]->saw(net::CoopEvent::Kind::ready); }), "soak: " + c[i]->name + " joined");
        const auto s = c[i]->coop.find_spawn(*w.collision, sp.x, sp.y, sp.z, c[i]->coop.slot());
        c[i]->start(w, static_cast<std::uint32_t>(80 + i), s[0], s[1], s[2], sp.a);
    }
    check(until([&] { return c[0]->arena->team().size() == 3; }), "soak: 3 teammates");
    combat::Rng rng(12345);
    const int ticks = static_cast<int>(seconds * 120);
    int wipes = 0, guest_restarts = 0, highest_wave = 1, disconnects = 0, late_rounds = 0;
    for(std::size_t i = 1; i < 4; ++i) c[i]->first_round = c[0]->coop.stats().snapshots - c[i]->coop.stats().snapshots;   // rounds sent before this guest was in the room
    double worst_gap = 0;
    std::uint64_t kills_by_guests = 0;
    const auto t_real = std::chrono::steady_clock::now();
    for(int t = 0; t < ticks; ++t) {
        if(t % 60 == 0) for(auto& x : c) {   // a new random intention every half second
            const std::uint32_t moves[] = {physics::key_forward, physics::key_back, physics::key_left, physics::key_right, physics::key_forward | physics::key_left, 0};
            x->keys = moves[static_cast<std::size_t>(rng.next() * 6) % 6] | (rng.next() < .45 ? combat::key_fire : 0U) | (rng.next() < .1 ? physics::key_jump : 0U) | (rng.next() < .2 ? combat::key_revive : 0U);
            x->arena->player().aim((rng.next() - .5) * 300, 0);
        }
        bool wiped = false;
        for(auto& x : c) {
            x->arena->step({x->keys});
            x->coop.after_step();
        }
        wiped = c[0]->arena->events().wipe;
        clock += 1000.0 / 120;
        const auto rounds = c[0]->coop.stats().snapshots;
        pump_all();
        if(c[0]->coop.stats().snapshots != rounds) {   // a snapshot round went out: give the network (real time) up to 0.5 s to deliver it
            const auto sent = c[0]->coop.stats().snapshots;
            const auto t_wait = std::chrono::steady_clock::now();
            auto delivered = [&] { for(std::size_t i = 1; i < 4; ++i) if(c[i]->coop.stats().snapshots < sent - c[i]->first_round) return false; return true; };
            while(!delivered() && std::chrono::steady_clock::now() - t_wait < std::chrono::milliseconds(500)) { std::this_thread::sleep_for(std::chrono::microseconds(100)); pump_all(); }
            if(!delivered()) ++late_rounds;
        }
        if(wiped) {   // the shell: a new arena on the same map, wave 1, guests told to restart
            ++wipes;
            c[0]->start(w, static_cast<std::uint32_t>(200 + wipes), sp.x, sp.y, sp.z, sp.a);
            c[0]->coop.round_restarted();
        }
        for(std::size_t i = 1; i < 4; ++i) {
            auto& x = *c[i];
            for(const auto& e : x.events) {
                if(e.kind == net::CoopEvent::Kind::disconnected) ++disconnects;
                if(e.kind == net::CoopEvent::Kind::restart) {
                    ++guest_restarts;
                    const auto s = x.coop.find_spawn(*w.collision, sp.x, sp.y, sp.z, x.coop.slot());
                    const auto frags = x.arena->frags();
                    x.start(w, static_cast<std::uint32_t>(300 + guest_restarts), s[0], s[1], s[2], sp.a);
                    kills_by_guests += frags;
                }
            }
            x.events.clear();
            const double gap = x.coop.stats().last_world_age_ms;
            if(t > 240) worst_gap = std::max(worst_gap, gap);
        }
        c[0]->events.clear();
        highest_wave = std::max(highest_wave, c[0]->horde->wave_number());
    }
    for(std::size_t i = 1; i < 4; ++i) kills_by_guests += c[i]->arena->frags();
    // settle: the last snapshot reaches everybody
    for(int i = 0; i < 40; ++i) { clock += 10; pump_all(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    bool waves = true;
    for(std::size_t i = 1; i < 4; ++i) waves = waves && c[i]->horde->wave_number() == c[0]->horde->wave_number();
    const double real = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_real).count();
    const auto st = c[0]->coop.stats();
    check(disconnects == 0 && c[0]->coop.players_in_room() == 4 && c[0]->arena->team().size() == 3, "soak: nobody disconnected (" + std::to_string(disconnects) + ")");
    check(worst_gap < 1000 && late_rounds == 0, "soak: snapshots kept coming (worst gap " + std::to_string(static_cast<int>(worst_gap)) + " ms of game time, " + std::to_string(late_rounds) + " rounds not delivered within 0.5 s)");
    check(waves, "soak: guests on the host's wave");
    check(guest_restarts == wipes * 3, "soak: every wipe restarted the three guests (" + std::to_string(guest_restarts) + " / " + std::to_string(wipes * 3) + ")");
    std::printf("  soak: %.0f s of game time in %.1f s; highest wave %d, %d squad wipes, %llu kills by guests; host sent %llu snapshot rounds (%.1f KiB/s), received %llu poses; relay relayed %llu messages\n",
                seconds, real, highest_wave, wipes, static_cast<unsigned long long>(kills_by_guests), static_cast<unsigned long long>(st.snapshots), static_cast<double>(st.snapshot_bytes) / 1024 / seconds,
                static_cast<unsigned long long>(st.poses), static_cast<unsigned long long>(relay.stats().messages_relayed));
    std::printf("  soak: guest upload (poses, volleys, relay envelope) %.1f / %.1f / %.1f KiB/s; host total upload %.1f KiB/s\n", static_cast<double>(c[1]->coop.stats().bytes_sent) / 1024 / seconds,
                static_cast<double>(c[2]->coop.stats().bytes_sent) / 1024 / seconds, static_cast<double>(c[3]->coop.stats().bytes_sent) / 1024 / seconds, static_cast<double>(st.bytes_sent) / 1024 / seconds);
    std::printf("%s soak with four players (%.0f s of game time)\n", failures == before ? "PASS" : "FAIL", seconds);
    for(auto& x : c) x->coop.leave();
    relay.stop();
}
}  // namespace

int main(int argc, char** argv) {
    if(argc < 2) { std::printf("usage: cat_arena_coop_tests <assets/n1> [--external ws://host:port]\n"); return 2; }
    std::string external;
    double soak_seconds = 0;
    for(int i = 2; i + 1 < argc; ++i) {
        if(std::string(argv[i]) == "--external") external = argv[i + 1];
        if(std::string(argv[i]) == "--soak") soak_seconds = std::stod(argv[i + 1]);
    }
    const World w = load(argv[1], "reactor-bastion");
    if(soak_seconds > 0) {
        soak(w, soak_seconds);
        std::printf("%s coop soak (%d checks)\n", failures ? "FAIL" : "PASS", checks);
        return failures ? 1 : 0;
    }
    if(external.empty()) rules(w);
    network(w, external);
    std::printf("%s coop (%d checks)\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
