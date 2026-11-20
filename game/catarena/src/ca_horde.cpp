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
// Not yet ported (later stages): the other 19 types, ranged attacks, flyers, the N10 navigation
// graph (fiends chase in a straight line here; a fiend that stops making progress detours to one
// side for 0.7 s, enough for pillars and boxes), corpses, voices.
//
// GPL v2 or later (docs/q3e/LICENSING.md).
#include "ca_game.h"
#include "ca_bridge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
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
    void init(const caGameImport_t& im) { im_ = im; enemies_ = {}; waves_ = {}; reset_ms_ = -1; rng_.seed(0xCA7u); print("Cat Arena horde ready (fiend, N10 wave rules)\n"); }
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

    caGameImport_t im_{};
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
    float move_yaw = e.yaw;
    if(!close) {
        e.check_ms += msec;
        if(e.check_ms >= 400) {   // progress over the last 0.4 s below a quarter of the speed: detour
            const float moved = std::hypot(pos[0] - e.check_pos[0], pos[1] - e.check_pos[1]);
            if(e.detour_ms <= 0 && moved < e.speed * 0.4f * 0.25f) { e.detour_ms = 700; e.detour_side = -e.detour_side; }
            e.check_pos[0] = pos[0]; e.check_pos[1] = pos[1]; e.check_ms = 0;
        }
        if(e.detour_ms > 0) { e.detour_ms -= msec; move_yaw += float(e.detour_side) * 1.3f; }
    }
    im_.moveEnemy(e.entity, move_yaw * 180 / kPi, e.speed, close ? 0 : 1, msec);
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
        char line[160];
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
        const int kills_before = waves_.kills;
        const int ent = im_.spawnEnemy(CA_ENEMY_FIEND, spots[0], 0, kFiend.health);
        check(ent >= 0, "fiend entity spawned");
        if(ent >= 0) {
            enemies_[CA_MAX_ENEMIES - 1] = Enemy{};
            enemies_[CA_MAX_ENEMIES - 1].entity = ent;
            check(im_.enemyHealth(ent) == 100, "fiend health 100");
            float before[3], after[3];
            im_.enemyOrigin(ent, before);
            im_.moveEnemy(ent, 0, kFiend.speed, 1, 500);
            im_.enemyOrigin(ent, after);
            const float moved = dist2d(before, after);
            char what[96];
            std::snprintf(what, sizeof what, "fiend moves with Pmove (%.0f units in 0.5 s)", moved);
            check(moved > 50 && moved < kFiend.speed * 0.5f + 1, what);
            im_.damageEnemy(ent, 13 * 4);
            check(im_.enemyHealth(ent) == 48, "four nailgun hits (13) leave 48 health");
            im_.damageEnemy(ent, 13 * 4);
            check(waves_.kills == kills_before + 1 && !enemies_[CA_MAX_ENEMIES - 1].alive(), "fiend dies and counts as a kill");
        }
    }
    const caWeapon_t* nail = CA_WeaponForSlot(2);
    check(nail && nail->damage == 13 && nail->delayMs == 90 && nail->clip == 50 && nail->reloadMs == 1350, "NAILGUN in the machinegun slot: 13 dmg, 90 ms, 50 rounds, 1.35 s reload");
    print(fails == 0 ? "ca_selftest: ALL PASS\n" : "ca_selftest: FAILURES\n");
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
