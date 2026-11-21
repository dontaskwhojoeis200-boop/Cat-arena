// Cat Arena: Feral Claw — enemies and waves on the server (qagame), C++20. N10-Q3E stage C.
//
// Rules ported from N10 (native/src/enemies.cpp, which reproduces the legacy game):
//   waves   beginWave / tickWaves: wave 1 has 10 enemies, each next wave ceil(previous * 1.25);
//           health, speed and damage scale by 1.05^(wave - 1); first batch min(total, 18,
//           10 + wave / 2), then one every 0.20 s while fewer than 20 are alive; spawn spots at
//           least 30 m from the players when possible; 3.5 s pause after a cleared wave.
//   fiend   ENEMY_STATS: 100 health, 10.3275 m/s, turn rate 5.2 rad/s, melee 17 damage; attack
//           starts within 1.9 m with line of sight after the cooldown (first 1.15-2.5 s, then
//           1.14 s), winds up 0.43 s and hits when the target is still within 1.55 m (and 1.55 m
//           in height); without line of sight it re-checks after 0.35 s.
//   routes  stage D: N10's own navigation graph (native/src/navigation.cpp, reused unchanged) built
//           from the N10 compiled map data shipped in the pk3 (maps/<map>.cadata); a fiend walks
//           straight when N10's traverse() says the line is clear, otherwise it follows
//           NavGraph::route() (light profile, as N10's fiend) and replans when the target moved
//           more than 2 m, after 1.5 s, or when stuck; at most 2 route searches per server frame.
//           Maps without N10 data (the test arena) fall back to a straight chase with stuck detours.
// Not yet ported: the other 19 types, ranged attacks, flyers, N10's sliced / shared route
// scheduler, corpses, voices.
//
// GPL v2 or later (docs/q3e/LICENSING.md).
#include "ca_game.h"
#include "ca_bridge.h"

#include "cat/assets.hpp"
#include "cat/navigation.hpp"
#include "cat/physics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>
#include <random>

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float u(double metres) { return float(metres * CA_UNITS_PER_METRE); }

struct FiendDef {
    int health = 100;
    float speed = u(10.3275);       // u/s
    float turn_rate = 5.2f;         // rad/s
    int melee_damage = 17;
    float attack_start = u(1.9), attack_reach = u(1.55), attack_height = u(1.55);
    int wind_ms = 430, cool_ms = 1140, blind_retry_ms = 350;
};
constexpr FiendDef kFiend{};

struct Enemy {
    int entity = -1;
    float yaw = 0;            // radians, Quake III convention (0 = +x)
    int cool_ms = 0;
    int wind_ms = 0;          // > 0 while winding up an attack
    float damage_scale = 1;
    float speed = kFiend.speed;
    // stuck detection (stage C stand-in for the N10 navigation graph)
    float check_pos[2]{};
    int check_ms = 0, detour_ms = 0, detour_side = 1;
    // N10 navigation route (N10 metres)
    std::vector<cat::nav::Point> path;
    std::size_t path_index = 0;
    float goal[3]{};
    int path_age_ms = 0;
    bool alive() const { return entity >= 0; }
};

// Wave arithmetic, separate from the entities so it can be checked on its own.
struct Waves {
    int number = 0, total = 0, spawned = 0, kills = 0;
    int cooldown_ms = -1, spawn_timer_ms = 0;
    void begin(int next) {
        if(next == 1) { number = 1; total = 10; }
        else { number = next; total = int(std::ceil(total * 1.25)); }
        spawned = 0; kills = 0; cooldown_ms = -1; spawn_timer_ms = 0;
    }
    int upfront() const { return std::min(total, std::min(18, 10 + number / 2)); }
    float scale() const { return float(std::pow(1.05, std::max(0, number - 1))); }
};

class Horde {
public:
    void init(const caGameImport_t& im) {
        im_ = im; enemies_ = {}; waves_ = {}; reset_ms_ = -1; rng_.seed(0xCA7u);
        load_navigation();
        print("Cat Arena horde ready (fiend, N10 wave rules)\n");
    }
    void frame(int msec);
    void killed(int entity);
    void shutdown() { for(auto& e : enemies_) if(e.alive()) im_.removeEnemy(e.entity), e.entity = -1; }
    int self_test();

private:
    void print(const char* s) { if(im_.print) im_.print(s); }
    int alive_count() const { return int(std::count_if(enemies_.begin(), enemies_.end(), [](const Enemy& e) { return e.alive(); })); }
    void begin_wave(int next, const caPlayer_t* players, int np);
    bool spawn(const caPlayer_t* players, int np);
    void think(Enemy& e, const caPlayer_t* players, int np, int msec);
    void publish();
    void clear() { for(auto& e : enemies_) if(e.alive()) { im_.removeEnemy(e.entity); e.entity = -1; } }

    void load_navigation();
    // Quake III units (origin = feet + 24) <-> N10 metres (feet), map centred at (cx_, cy_)
    cat::nav::Point to_n10(const float p[3]) const { return {p[0] / CA_UNITS_PER_METRE + cx_, p[1] / CA_UNITS_PER_METRE + cy_, (p[2] - 24) / CA_UNITS_PER_METRE}; }
    void from_n10(const cat::nav::Point& q, float out[3]) const { out[0] = float((q[0] - cx_) * CA_UNITS_PER_METRE); out[1] = float((q[1] - cy_) * CA_UNITS_PER_METRE); out[2] = float(q[2] * CA_UNITS_PER_METRE + 24); }
    float route_yaw(Enemy& e, const float pos[3], const caPlayer_t& target);

    caGameImport_t im_{};
    std::unique_ptr<cat::assets::Value> nav_doc_;
    std::unique_ptr<cat::physics::CollisionWorld> nav_world_;
    std::unique_ptr<cat::nav::NavGraph> nav_;
    double cx_ = 0, cy_ = 0;
    int route_budget_ = 0;
    std::array<Enemy, CA_MAX_ENEMIES> enemies_{};
    Waves waves_{};
    int reset_ms_ = -1;            // squad wipe: restart at wave 1 after this delay
    std::mt19937 rng_{0xCA7u};
    char last_info_[96]{};
};

Horde g_horde;

float dist2d(const float a[3], const float b[3]) { return std::hypot(a[0] - b[0], a[1] - b[1]); }

void Horde::begin_wave(int next, const caPlayer_t* players, int np) {
    clear();
    waves_.begin(next);
    char line[96];
    std::snprintf(line, sizeof line, "Cat Arena: wave %d, %d enemies\n", waves_.number, waves_.total);
    print(line);
    for(int i = 0, n = waves_.upfront(); i < n && alive_count() < CA_MAX_ENEMIES; ++i)
        if(spawn(players, np)) ++waves_.spawned;
}

bool Horde::spawn(const caPlayer_t* players, int np) {
    float spots[64][3];
    const int ns = im_.spawnPoints(spots, 64);
    if(ns <= 0) return false;
    // Prefer spots at least 30 m from every player (N10 spawn rule); otherwise the farthest spots.
    std::array<std::pair<float, int>, 64> order{};
    for(int i = 0; i < ns; ++i) {
        float nearest = 1e9f;
        for(int p = 0; p < np; ++p) nearest = std::min(nearest, dist2d(spots[i], players[p].origin));
        order[size_t(i)] = {nearest, i};
    }
    std::sort(order.begin(), order.begin() + ns, [](auto a, auto b) { return a.first > b.first; });
    int far_count = 0;
    while(far_count < ns && order[size_t(far_count)].first >= u(30)) ++far_count;
    const int pool = far_count > 0 ? far_count : std::max(1, ns / 2);
    const int start = int(rng_() % unsigned(pool));
    for(int k = 0; k < ns; ++k) {   // the chosen spot, then the others if it is occupied
        const float* s = spots[order[size_t((start + k) % ns)].second];
        const float yaw = float(rng_() % 360);
        const float scale = waves_.scale();
        const int ent = im_.spawnEnemy(CA_ENEMY_FIEND, s, yaw, int(std::lround(kFiend.health * scale)));
        if(ent < 0) continue;
        for(auto& e : enemies_) if(!e.alive()) {
            e = Enemy{};
            e.entity = ent;
            e.yaw = yaw * kPi / 180;
            e.cool_ms = 1150 + int(rng_() % 1350);   // b.cool = 1.15 + rng * 1.35
            e.damage_scale = scale;
            e.speed = kFiend.speed * scale;
            return true;
        }
        im_.removeEnemy(ent);
        return false;
    }
    return false;
}

void Horde::think(Enemy& e, const caPlayer_t* players, int np, int msec) {
    float pos[3];
    im_.enemyOrigin(e.entity, pos);
    const caPlayer_t* target = nullptr;
    float best = 1e9f;
    for(int p = 0; p < np; ++p) {
        if(!players[p].alive) continue;
        const float d = dist2d(pos, players[p].origin);
        if(d < best) { best = d; target = &players[p]; }
    }
    if(!target) { im_.moveEnemy(e.entity, e.yaw * 180 / kPi, 0, 0, msec); return; }
    // turn toward the target at the type's turn rate
    const float desired = std::atan2(target->origin[1] - pos[1], target->origin[0] - pos[0]);
    float diff = std::remainder(desired - e.yaw, 2 * kPi);
    const float step = kFiend.turn_rate * float(msec) / 1000.0f;
    e.yaw += std::clamp(diff, -step, step);
    const float dz = std::fabs(target->origin[2] - pos[2]);
    if(e.wind_ms > 0) {   // winding up: stand, then strike
        e.wind_ms -= msec;
        im_.moveEnemy(e.entity, e.yaw * 180 / kPi, 0, 0, msec);
        if(e.wind_ms <= 0) {
            e.wind_ms = 0;
            im_.setEnemyState(e.entity, 0);
            if(best < kFiend.attack_reach && dz < kFiend.attack_height)
                im_.damagePlayer(target->clientNum, int(std::lround(kFiend.melee_damage * e.damage_scale)), e.entity);
        }
        return;
    }
    e.cool_ms -= msec;
    if(e.cool_ms <= 0 && best < kFiend.attack_start) {
        if(im_.visible(e.entity, target->clientNum)) {
            e.wind_ms = kFiend.wind_ms;
            e.cool_ms = kFiend.cool_ms;
            im_.setEnemyState(e.entity, 1);
            im_.moveEnemy(e.entity, e.yaw * 180 / kPi, 0, 0, msec);
            return;
        }
        e.cool_ms = kFiend.blind_retry_ms;
    }
    const bool close = best < u(1.2);
    float move_yaw = nav_ ? route_yaw(e, pos, *target) : e.yaw;
    if(!close) {
        e.check_ms += msec;
        if(e.check_ms >= 400) {   // progress over the last 0.4 s below a quarter of the speed: detour
            const float moved = std::hypot(pos[0] - e.check_pos[0], pos[1] - e.check_pos[1]);
            if(e.detour_ms <= 0 && moved < e.speed * 0.4f * 0.25f) { e.detour_ms = 700; e.detour_side = -e.detour_side; }
            e.check_pos[0] = pos[0]; e.check_pos[1] = pos[1]; e.check_ms = 0;
        }
        if(e.detour_ms > 0) {
            e.detour_ms -= msec;
            move_yaw += float(e.detour_side) * 1.3f;
            e.path.clear();   // stuck: replan
        }
    }
    im_.moveEnemy(e.entity, move_yaw * 180 / kPi, e.speed, close ? 0 : 1, msec);
}

void Horde::load_navigation() {
    nav_.reset(); nav_world_.reset(); nav_doc_.reset();
    const char* map = im_.mapName ? im_.mapName() : "";
    char path[96];
    std::snprintf(path, sizeof path, "maps/%s.cadata", map);
    std::vector<unsigned char> bytes(8u << 20);
    const int n = im_.readFile ? im_.readFile(path, bytes.data(), int(bytes.size())) : -1;
    if(n <= 0) {
        char line[256];
        std::snprintf(line, sizeof line, "Cat Arena: no N10 navigation data (%s); enemies chase directly\n", path);
        print(line);
        return;
    }
    try {
        nav_doc_ = std::make_unique<cat::assets::Value>(cat::assets::decode_data({bytes.data(), size_t(n)}));
        const auto& compiled = nav_doc_->at("compiled");
        const auto& center = compiled.at("meta").at("center").array();
        cx_ = center.at(0).number();
        cy_ = center.at(1).number();
        nav_world_ = std::make_unique<cat::physics::CollisionWorld>(compiled);
        nav_ = std::make_unique<cat::nav::NavGraph>(*nav_world_);
        char line[256];
        std::snprintf(line, sizeof line, "Cat Arena: N10 navigation graph for %s (%d nodes)\n", map, int(nav_->nodes().size()));
        print(line);
    } catch(const std::exception& ex) {
        nav_.reset(); nav_world_.reset(); nav_doc_.reset();
        char line[200];
        std::snprintf(line, sizeof line, "Cat Arena: navigation data %s rejected: %s\n", path, ex.what());
        print(line);
    }
}

float Horde::route_yaw(Enemy& e, const float pos[3], const caPlayer_t& target) {
    const auto& profile = cat::nav::kLight;   // N10 fiend: nav profile 0
    const auto from = to_n10(pos), to = to_n10(target.origin);
    auto aim = [&](const cat::nav::Point& q) { return std::atan2(float(q[1] - from[1]), float(q[0] - from[0])); };
    // straight line when N10's own check says the way is clear
    if(nav_->traverse(from[0], from[1], from[2], to[0], to[1], to[2], profile)) { e.path.clear(); return aim(to); }
    e.path_age_ms += 50;
    const float moved_goal = std::hypot(target.origin[0] - e.goal[0], target.origin[1] - e.goal[1]) + std::fabs(target.origin[2] - e.goal[2]);
    if((e.path.empty() || e.path_index >= e.path.size() || moved_goal > u(2) || e.path_age_ms > 1500) && route_budget_ > 0) {
        --route_budget_;
        e.path = nav_->route(from, to, profile);
        e.path_index = 0;
        e.path_age_ms = 0;
        e.goal[0] = target.origin[0]; e.goal[1] = target.origin[1]; e.goal[2] = target.origin[2];
    }
    while(e.path_index < e.path.size()) {   // next waypoint not yet reached (0.6 m)
        const auto& q = e.path[e.path_index];
        if(std::hypot(q[0] - from[0], q[1] - from[1]) < 0.6 && std::fabs(q[2] - from[2]) < 1.2) ++e.path_index;
        else break;
    }
    if(e.path_index < e.path.size()) return aim(e.path[e.path_index]);
    return aim(to);
}

void Horde::publish() {
    char info[96];
    std::snprintf(info, sizeof info, "%d %d %d %d %d", waves_.number, waves_.total, waves_.kills, alive_count(),
                  waves_.cooldown_ms > 0 ? (waves_.cooldown_ms + 999) / 1000 : 0);
    if(std::strcmp(info, last_info_) != 0) {
        std::snprintf(last_info_, sizeof last_info_, "%s", info);
        im_.setWaveInfo(info);
    }
}

void Horde::frame(int msec) {
    caPlayer_t players[16];
    const int np = im_.players(players, 16);
    int living = 0;
    for(int p = 0; p < np; ++p) living += players[p].alive ? 1 : 0;
    if(np == 0) {   // nobody playing: idle, no wave
        if(waves_.number != 0) { clear(); waves_ = {}; }
        publish();
        return;
    }
    if(waves_.number == 0) begin_wave(1, players, np);
    // squad wipe (every player down at once): the wave restarts at 1 after 3 s (N10 / legacy co-op
    // rule); a respawn during those 3 s does not cancel it
    if(living == 0 && reset_ms_ < 0) { reset_ms_ = 3000; print("Cat Arena: squad wiped\n"); }
    if(reset_ms_ >= 0) {
        reset_ms_ -= msec;
        for(auto& e : enemies_) if(e.alive()) im_.moveEnemy(e.entity, e.yaw * 180 / kPi, 0, 0, msec);
        if(reset_ms_ <= 0) { reset_ms_ = -1; begin_wave(1, players, np); }
        publish();
        return;
    }
    if(waves_.cooldown_ms >= 0) {
        waves_.cooldown_ms -= msec;
        if(waves_.cooldown_ms <= 0) begin_wave(waves_.number + 1, players, np);
        publish();
        return;
    }
    route_budget_ = 2;
    waves_.spawn_timer_ms -= msec;
    if(waves_.spawned < waves_.total && alive_count() < CA_MAX_ENEMIES && waves_.spawn_timer_ms <= 0) {
        if(spawn(players, np)) ++waves_.spawned;
        waves_.spawn_timer_ms = 200;
    }
    for(auto& e : enemies_) if(e.alive()) think(e, players, np, msec);
    if(waves_.kills >= waves_.total && alive_count() == 0) {
        waves_.cooldown_ms = 3500;
        char line[64];
        std::snprintf(line, sizeof line, "Cat Arena: wave %d cleared\n", waves_.number);
        print(line);
    }
    publish();
}

void Horde::killed(int entity) {
    for(auto& e : enemies_) if(e.entity == entity) { e.entity = -1; ++waves_.kills; return; }
}

int Horde::self_test() {
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        char line[256];
        std::snprintf(line, sizeof line, "ca_selftest: %s %s\n", ok ? "PASS" : "FAIL", what);
        print(line);
        if(!ok) ++fails;
    };
    // wave arithmetic (N10 beginWave)
    Waves w;
    w.begin(1);
    check(w.total == 10 && w.upfront() == 10, "wave 1: 10 enemies, all spawned at once");
    w.begin(2);
    check(w.total == 13 && w.upfront() == 11, "wave 2: ceil(10 * 1.25) = 13, first batch 11");
    w.begin(3);
    check(w.total == 17 && std::fabs(w.scale() - 1.1025f) < 1e-4f, "wave 3: 17 enemies, scale 1.05^2");
    // a live fiend entity: spawn, damage, death callback
    float spots[64][3];
    const int ns = im_.spawnPoints(spots, 64);
    check(ns > 0, "enemy spawn points found in the map");
    if(ns > 0) {
        // move 0.5 s in each of four directions from fresh spawns; the best direction counts
        // (a spawn spot may face a wall: Reactor Bastion's first spawn does)
        float moved = 0;
        for(int dir = 0; dir < 4; ++dir) {
            const int probe = im_.spawnEnemy(CA_ENEMY_FIEND, spots[0], 0, kFiend.health);
            if(probe < 0) continue;
            float before[3], after[3];
            im_.enemyOrigin(probe, before);
            im_.moveEnemy(probe, float(dir * 90), kFiend.speed, 1, 500);
            im_.enemyOrigin(probe, after);
            moved = std::max(moved, dist2d(before, after));
            im_.removeEnemy(probe);
        }
        char what[96];
        std::snprintf(what, sizeof what, "fiend moves with Pmove (best of 4 directions: %.0f units in 0.5 s)", moved);
        check(moved > 50 && moved < kFiend.speed * 0.5f + 1, what);
        const int kills_before = waves_.kills;
        const int ent = im_.spawnEnemy(CA_ENEMY_FIEND, spots[0], 0, kFiend.health);
        check(ent >= 0, "fiend entity spawned");
        if(ent >= 0) {
            enemies_[CA_MAX_ENEMIES - 1] = Enemy{};
            enemies_[CA_MAX_ENEMIES - 1].entity = ent;
            check(im_.enemyHealth(ent) == 100, "fiend health 100");
            im_.damageEnemy(ent, 13 * 4);
            check(im_.enemyHealth(ent) == 48, "four nailgun hits (13) leave 48 health");
            im_.damageEnemy(ent, 13 * 4);
            check(waves_.kills == kills_before + 1 && !enemies_[CA_MAX_ENEMIES - 1].alive(), "fiend dies and counts as a kill");
        }
    }
    if(nav_) {   // N10 navigation: routes from the lowest spawn spot to every other spot, per level
        int lo = 0;
        for(int i = 0; i < ns; ++i) if(spots[i][2] < spots[lo][2]) lo = i;
        int reach[3]{}, total[3]{};
        for(int i = 0; i < ns; ++i) {
            if(i == lo) continue;
            const auto to = to_n10(spots[i]);
            const int level = to[2] <= -2 ? 0 : to[2] >= 4.5 ? 2 : 1;   // N10 layers l1 / l2 / l3
            ++total[level];
            const auto route = nav_->route(to_n10(spots[lo]), to, cat::nav::kLight);
            if(route.size() >= 2 && std::fabs(route.back()[2] - to[2]) < 1.0) ++reach[level];
        }
        char what[160];
        std::snprintf(what, sizeof what, "N10 routes from the lowest spot: L1 %d/%d, L2 %d/%d, L3 %d/%d spots reachable",
                      reach[0], total[0], reach[1], total[1], reach[2], total[2]);
        check(reach[1] > 0 && reach[2] > 0, what);
    }
    {   // the 16 N10 weapons (native/src/combat.cpp kWeapons): name, damage, delay ms, clip, reload ms, pellets, range m
        struct Row { const char* name; int damage, delay, clip, reload, pellets, range; };
        static constexpr Row kExpect[CA_WEAPON_COUNT] = {
            {"NAILGUN", 13, 90, 50, 1350, 1, 27}, {"SHOTGUN", 18, 720, 8, 1800, 8, 13}, {"GRENADE LAUNCHER", 108, 780, 6, 2000, 1, 24},
            {"RAILGUN", 132, 980, 7, 2100, 1, 100}, {"ROCKET LAUNCHER", 95, 860, 6, 2100, 1, 33}, {"SUPER NAILGUN", 20, 130, 36, 1650, 1, 31},
            {"THUNDERBOLT", 15, 75, 75, 1700, 1, 20}, {"SUPER SHOTGUN", 16, 880, 12, 2000, 14, 14}, {"PLASMA REPEATER", 34, 190, 48, 1700, 1, 50},
            {"TRIPLE SPIKE", 13, 340, 36, 1550, 3, 36}, {"RIFT CANNON", 125, 1150, 5, 2400, 1, 36}, {"IRON CHAINGUN", 10, 65, 100, 2100, 1, 70},
            {"HAND CANNON", 78, 820, 7, 2000, 1, 95}, {"HELLFIRE CARBINE", 37, 280, 28, 1700, 1, 42}, {"SAWBLADE DRIVER", 39, 380, 22, 1900, 1, 54},
            {"QUAD SHOTGUN", 14, 1150, 8, 2500, 20, 17}};
        int same = 0;
        for(int i = 0; i < CA_WEAPON_COUNT; ++i) {
            const caWeapon_t* w = CA_Weapon(i);
            const Row& r = kExpect[i];
            if(w && std::strcmp(w->name, r.name) == 0 && w->damage == r.damage && w->delayMs == r.delay && w->clip == r.clip && w->reloadMs == r.reload
               && w->pellets == r.pellets && std::fabs(w->rangeUnits - r.range * CA_UNITS_PER_METRE) < .01F) ++same;
        }
        char what[96];
        std::snprintf(what, sizeof what, "N10 weapon table: %d/16 weapons match (damage, rate, magazine, reload, pellets, range)", same);
        check(same == CA_WEAPON_COUNT, what);
    }
    print(fails == 0 ? "ca_selftest: horde PASS\n" : "ca_selftest: horde FAILURES\n");
    return fails == 0;
}

}  // namespace

extern "C" void CA_HordeInit(const caGameImport_t* imports) {
    try { if(imports) g_horde.init(*imports); } catch(...) {}
}
extern "C" void CA_HordeFrame(int msec) {
    try { g_horde.frame(msec); } catch(...) {}
}
extern "C" void CA_HordeEnemyKilled(int entityNum) {
    try { g_horde.killed(entityNum); } catch(...) {}
}
extern "C" void CA_HordeShutdown(void) {
    try { g_horde.shutdown(); } catch(...) {}
}
extern "C" int CA_HordeSelfTest(void) {
    try { return g_horde.self_test(); } catch(...) { return 0; }
}
