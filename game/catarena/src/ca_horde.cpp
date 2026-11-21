// Cat Arena: Feral Claw — enemies and waves on the server (qagame), C++20.
// N10-Q3E stage C (fiend), stage D part 2 (N10 navigation), part 4 (all 20 types).
//
// Rules ported from N10 (native/src/enemies.cpp, which reproduces the legacy game):
//   waves   beginWave / tickWaves: wave 1 has 10 enemies, each next wave ceil(previous * 1.25);
//           health, speed and damage scale by 1.05^(wave - 1); first batch min(total, 18,
//           10 + wave / 2) whose first three are flyers (skyprobe; skyraider from wave 3;
//           skyjuggernaut from wave 5), then one every 0.20 s while fewer than 20 are alive.
//   spawn   spawnBot: a uniformly random type of the 20; ground enemies on a random N10 graph node
//           of a random level (17 % low, 27 % high, 56 % middle) where their navigation capsule
//           fits, at least 30 m from the players; flyers over an open-sky node 4.3-6.4 m up; the
//           map's spawn spots otherwise.
//   types   ENEMY_STATS / FLYING_STATS (ca_enemies.cpp): health, speed, turn rate, hit box class,
//           navigation profile, melee damage, ranged / slam / orb / warden flags, shot kind.
//   attack  every enemy: after its cooldown, within its range (ranged 28 m, slam 2.5 m, else
//           1.9 m) and with line of sight, it winds up (flyer .66, slam .88, orb .73, ranged .61,
//           else .43 s) and acts when the wind-up ends: ranged enemies fire their projectile(s)
//           (marauder three spikes, other spike throwers two; N10 speed, aim lead, spread, damage,
//           life), the others hit if the target is still within reach (slam 2.2 m, else 1.55 m;
//           1.55 m in height). Cooldowns: flyer 1.95, slam 2.18, warden 2.4, ranged 1.72, else
//           1.14 s; without line of sight it re-checks after 0.35 s.
//   ground  N10 botNavigation distances: ranged enemies stop 8 m (orb 10 m) from the target,
//           slam 1.8 m, others 1.18 m; they turn at their turn rate toward where they walk, or
//           toward the target when they stand. Routes as in part 2, with the type's profile.
//   flyers  updateFlyer: hover 2.45 m above the target with a slow bob, swept steering over 6 +
//           40 candidate directions on N10's collision world (Quake III traces on maps without N10
//           data), steering decided at most every 1/30 s.
// Not yet ported: N10's sliced / shared route scheduler, voices.
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

// Unit conversions: N10 metres (feet) <-> Quake III units (origin = feet + 24).
constexpr double kFlyerHeight = 1.15;   // nav3FlyerFree default h

struct Enemy {
    int entity = -1;
    int type = CA_ENEMY_FIEND;
    float yaw = 0;            // facing, radians, Quake III convention (0 = +x)
    int cool_ms = 0;
    int wind_ms = 0;          // > 0 while winding up an attack
    int recover_ms = 0;       // > 0 after the attack (animation only)
    float damage_scale = 1;
    float speed = 0;          // u/s
    // stuck detection
    float check_pos[2]{};
    int check_ms = 0, detour_ms = 0, detour_side = 1;
    // N10 navigation route (N10 metres)
    std::vector<cat::nav::Point> path;
    std::size_t path_index = 0;
    float goal[3]{};
    int path_age_ms = 0;
    // flyers (updateFlyer state)
    double fly_phase = 0, fly_decide = 0, fly_stall = 0, steer_side = 1;
    bool fly_decide_set = false, has_fly_dir = false;
    std::array<double, 3> fly_dir{};
    unsigned uid = 0;
    bool alive() const { return entity >= 0; }
    const caEnemyType_t& def() const { return *CA_EnemyType(type); }
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
    // beginWave: the first three of the first batch are flyers (from waves 1 / 3 / 5)
    int forced_type(int i) const { return i == 0 ? 17 : (i == 1 && number >= 3) ? 18 : (i == 2 && number >= 5) ? 19 : -1; }
};

class Horde {
public:
    void init(const caGameImport_t& im) {
        im_ = im; enemies_ = {}; waves_ = {}; reset_ms_ = -1; rng_.seed(0xCA7u); serial_ = 0;
        load_navigation();
        print("Cat Arena horde ready (20 N10 enemy types, N10 wave rules)\n");
    }
    void frame(int msec);
    void killed(int entity);
    // N10 world.pickups(): the medkit spots N10 keeps (on a navigation floor), in Quake III units at floor level
    int pickups(float* xyz, int max) const {
        if(!nav_world_) return 0;
        int n = 0;
        for(const auto& p : nav_world_->pickups()) {
            if(n >= max) break;
            xyz[3 * n] = float((p.x - cx_) * CA_UNITS_PER_METRE);
            xyz[3 * n + 1] = float((p.y - cy_) * CA_UNITS_PER_METRE);
            xyz[3 * n + 2] = float(p.z * CA_UNITS_PER_METRE);
            ++n;
        }
        return n;
    }
    void shutdown() { for(auto& e : enemies_) if(e.alive()) im_.removeEnemy(e.entity), e.entity = -1; }
    int self_test();
    void fit_survey(int list);
    // ca_lineup (cheat): every type in rows in front of a point, held still
    void lineup(const float origin[3], float yaw_deg);
    bool lineup_active() const { return frozen_; }
    void end_lineup() { if(frozen_) { clear(); frozen_ = false; } }

private:
    void print(const char* s) { if(im_.print) im_.print(s); }
    double rnd() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng_); }
    int alive_count() const { return int(std::count_if(enemies_.begin(), enemies_.end(), [](const Enemy& e) { return e.alive(); })); }
    void begin_wave(int next, const caPlayer_t* players, int np);
    bool spawn(const caPlayer_t* players, int np, int forced_type);
    Enemy* adopt(int entity, int type, float yaw_deg, float scale);
    void think(Enemy& e, const caPlayer_t* players, int np, int msec);
    void think_ground(Enemy& e, const float pos[3], const caPlayer_t& target, float dist, int msec);
    void think_flyer(Enemy& e, const float pos[3], const caPlayer_t& target, int msec);
    void attack(Enemy& e, const float pos[3], const caPlayer_t& target);
    bool flyer_free(double x, double y, double z, double r) const;
    bool flyer_sweep(double x, double y, double z, double nx, double ny, double nz, double r) const;
    void publish();
    void clear() { for(auto& e : enemies_) if(e.alive()) { im_.removeEnemy(e.entity); e.entity = -1; } }

    void load_navigation();
    // Quake III units (origin = feet + 24) <-> N10 metres (feet), map centred at (cx_, cy_)
    cat::nav::Point to_n10(const float p[3]) const { return {p[0] / CA_UNITS_PER_METRE + cx_, p[1] / CA_UNITS_PER_METRE + cy_, (p[2] - 24) / CA_UNITS_PER_METRE}; }
    void from_n10(const cat::nav::Point& q, float out[3]) const { out[0] = float((q[0] - cx_) * CA_UNITS_PER_METRE); out[1] = float((q[1] - cy_) * CA_UNITS_PER_METRE); out[2] = float(q[2] * CA_UNITS_PER_METRE + 24); }
    float route_yaw(Enemy& e, const float pos[3], const caPlayer_t& target);
    static const cat::nav::Profile& profile(int index) { return index == 0 ? cat::nav::kLight : index == 2 ? cat::nav::kHeavy : cat::nav::kStandard; }

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
    unsigned serial_ = 0;
    bool frozen_ = false;          // ca_lineup: enemies stand and only play their attack animation
    int frozen_ms_ = 0;
    char last_info_[96]{};
};

Horde g_horde;

float dist2d(const float a[3], const float b[3]) { return std::hypot(a[0] - b[0], a[1] - b[1]); }
float ang_diff(float a, float b) { return std::remainder(a - b, 2 * kPi); }

void Horde::begin_wave(int next, const caPlayer_t* players, int np) {
    clear();
    frozen_ = false;
    waves_.begin(next);
    char line[96];
    std::snprintf(line, sizeof line, "Cat Arena: wave %d, %d enemies\n", waves_.number, waves_.total);
    print(line);
    for(int i = 0, n = waves_.upfront(); i < n && alive_count() < CA_MAX_ENEMIES; ++i)
        if(spawn(players, np, waves_.forced_type(i))) ++waves_.spawned;
}

Enemy* Horde::adopt(int ent, int type, float yaw_deg, float scale) {
    for(auto& e : enemies_) if(!e.alive()) {
        e = Enemy{};
        e.entity = ent;
        e.type = type;
        e.uid = ++serial_;
        e.yaw = yaw_deg * kPi / 180;
        e.cool_ms = 1150 + int(rnd() * 1350);   // b.cool = 1.15 + rng * 1.35
        e.damage_scale = scale;
        e.speed = e.def().speed * scale;
        e.fly_phase = rnd() * 6.283;
        e.steer_side = 1;
        return &e;
    }
    return nullptr;
}

bool Horde::spawn(const caPlayer_t* players, int np, int forced) {
    const int type = forced >= 0 ? forced : std::min(CA_ENEMY_TYPES - 1, int(rnd() * CA_ENEMY_TYPES));
    const auto& def = *CA_EnemyType(type);
    const float scale = waves_.scale();
    const int health = int(std::lround(def.health * scale));
    auto far_from_players = [&](double x, double y) {   // N10: >= 30 m from the player (here: every player)
        float p[3]{float((x - cx_) * CA_UNITS_PER_METRE), float((y - cy_) * CA_UNITS_PER_METRE), 0};
        for(int i = 0; i < np; ++i) if(dist2d(p, players[i].origin) < u(30)) return false;
        return true;
    };
    std::optional<cat::nav::Point> at;
    if(nav_ && !nav_->nodes().empty()) {
        const auto& nodes = nav_->nodes();
        const auto& world = *nav_world_;
        if(def.flying) {   // worldRandomSpawn(30, 'air'): an open-sky graph node away from the players
            for(int i = 0; i < 220; ++i) {
                const auto& n = nodes[std::min(nodes.size() - 1, std::size_t(rnd() * double(nodes.size())))];
                if(!far_from_players(n.x, n.y) || world.overhead(n.x, n.y)) continue;
                std::array<const cat::physics::Slab*, 64> q;
                const auto count = world.slab_query(n.x, n.y, q);
                bool covered = false;
                for(std::size_t k = 0; k < count; ++k) {
                    const double z = cat::physics::CollisionWorld::slab_z(*q[k], n.x, n.y);
                    if(z > n.z + .4 && z < n.z + 9) { covered = true; break; }
                }
                if(covered) continue;
                const auto roof = world.overhead(n.x, n.y);
                const double fly_alt = std::min(n.z + 4.3 + rnd() * 2.1, (roof ? *roof : 26.0) - 2.8);
                at = cat::nav::Point{n.x, n.y, std::max(n.z + 3.6, fly_alt)};
                break;
            }
        } else {   // nav3RandomEnemySpawn: a node of a randomly chosen level where the capsule fits
            const auto& prof = profile(def.navProfile);
            const double roll = rnd();
            const auto layer = roll < .17 ? cat::nav::Layer::l1 : roll < .44 ? cat::nav::Layer::l3 : cat::nav::Layer::l2;
            for(int i = 0; i < 160; ++i) {
                const auto& n = nodes[std::min(nodes.size() - 1, std::size_t(rnd() * double(nodes.size())))];
                if(n.layer != layer || !world.capsule_clear(n.x, n.y, n.z, prof.height, prof.radius)) continue;
                if(!far_from_players(n.x, n.y)) continue;
                at = cat::nav::Point{n.x, n.y, n.z};
                break;
            }
        }
    }
    const float yaw = float(rnd() * 360);
    if(at) {
        float o[3];
        from_n10(*at, o);
        const int ent = im_.spawnEnemy(type, o, yaw, health);
        if(ent >= 0) {
            if(adopt(ent, type, yaw, scale)) return true;
            im_.removeEnemy(ent);
            return false;
        }
    }
    // the map's spawn spots (N10 worldRandomSpawn 'player' pool): far from the players when possible
    float spots[64][3];
    const int ns = im_.spawnPoints(spots, 64);
    if(ns <= 0) return false;
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
    const int start = int(rnd() * pool) % pool;
    for(int k = 0; k < ns; ++k) {   // the chosen spot, then the others if it is occupied
        float o[3];
        const float* s = spots[order[size_t((start + k) % ns)].second];
        o[0] = s[0]; o[1] = s[1]; o[2] = s[2];
        if(def.flying && nav_world_) {   // N10 spawnEnemy: fly_alt below any roof over the spot (2.8 m under it), at least 3.6 m up
            const auto g = to_n10(o);
            const auto roof = nav_world_->overhead(g[0], g[1]);
            const double fly_alt = std::min(g[2] + 4.3 + rnd() * 2.1, (roof ? *roof : 26.0) - 2.8);
            o[2] = float(std::max(g[2] + 3.6, fly_alt) * CA_UNITS_PER_METRE + 24);
        } else if(def.flying) o[2] += u(4.3 + rnd() * 2.1);
        const int ent = im_.spawnEnemy(type, o, yaw, health);
        if(ent < 0) continue;
        if(adopt(ent, type, yaw, scale)) return true;
        im_.removeEnemy(ent);
        return false;
    }
    return false;
}

void Horde::lineup(const float origin[3], float yaw_deg) {
    clear();
    frozen_ = true;
    frozen_ms_ = 0;
    const float yaw = yaw_deg * kPi / 180;
    const float fx = std::cos(yaw), fy = std::sin(yaw), rx = std::sin(yaw), ry = -std::cos(yaw);
    for(int t = 0; t < CA_ENEMY_TYPES; ++t) {   // 4 rows of 5, 4 m apart, 4.5 m between rows, from 8 m ahead
        const int row = t / 5, col = t % 5;
        const auto& def = *CA_EnemyType(t);
        for(int k = 0; k < 9; ++k) {   // the slot, then nearby places if a wall is in the way
            const float dx = float((k % 3) - 1) * u(1.2), dy = float((k / 3) - 1) * u(1.2);
            const float ahead = u(8 + row * 4.5) + (k ? dy : 0), side = u((col - 2) * 4.0) + (k ? dx : 0);
            float o[3]{origin[0] + fx * ahead + rx * side, origin[1] + fy * ahead + ry * side, origin[2]};
            if(def.flying) o[2] += u(2.5);
            const int ent = im_.spawnEnemy(t, o, yaw_deg + 180, def.health);
            if(ent < 0) continue;
            adopt(ent, t, yaw_deg + 180, 1.0f);
            break;
        }
    }
}

void Horde::think(Enemy& e, const caPlayer_t* players, int np, int msec) {
    float pos[3];
    im_.enemyOrigin(e.entity, pos);
    const caPlayer_t* target = nullptr;   // enemyTargetFor: the nearest living player
    float best = 1e9f;
    for(int p = 0; p < np; ++p) {
        if(!players[p].alive) continue;
        const float d = dist2d(pos, players[p].origin);
        if(d < best) { best = d; target = &players[p]; }
    }
    const auto& def = e.def();
    if(!target || frozen_) {
        if(def.flying) im_.setEnemyOrigin(e.entity, pos, e.yaw * 180 / kPi);
        else im_.moveEnemy(e.entity, e.yaw * 180 / kPi, 0, 0, msec, e.yaw * 180 / kPi);
        return;
    }
    // movement first, as in N10's update()
    if(def.flying) think_flyer(e, pos, *target, msec);
    else think_ground(e, pos, *target, best, msec);
    im_.enemyOrigin(e.entity, pos);
    // attack state machine: the hit lands once, when the wind-up ends; recovery is animation only
    e.cool_ms -= msec;
    if(e.wind_ms > 0) {
        e.wind_ms -= msec;
        if(e.wind_ms <= 0) {
            e.wind_ms = 0;
            e.recover_ms = def.recoverMs;
            attack(e, pos, *target);
        }
    } else if(e.recover_ms > 0) {
        e.recover_ms = std::max(0, e.recover_ms - msec);
    } else if(e.cool_ms <= 0 && best < def.attackStart) {
        if(im_.visible(e.entity, target->clientNum)) {
            e.wind_ms = def.windMs;
            e.cool_ms = def.coolMs;
            im_.setEnemyState(e.entity, 1);
        } else e.cool_ms = 350;
    }
}

void Horde::think_ground(Enemy& e, const float pos[3], const caPlayer_t& target, float dist, int msec) {
    const auto& def = e.def();
    const float dt = float(msec) / 1000.0f;
    // botNavigation: stand within range (ranged 8 m, orb 10 m, slam 1.8 m, else 1.18 m; 2.4 m in height)
    const float range = u(def.ranged ? (def.orb ? 10 : 8) : (def.slam ? 1.8 : 1.18));
    const bool close = dist <= range && std::fabs(target.origin[2] - pos[2]) <= u(2.4);
    const float to_target = std::atan2(target.origin[1] - pos[1], target.origin[0] - pos[0]);
    if(close) {   // face the target at 75 % of the turn rate
        const float step = def.turnRate * dt * .75f;
        e.yaw += std::clamp(ang_diff(to_target, e.yaw), -step, step);
        im_.moveEnemy(e.entity, e.yaw * 180 / kPi, 0, 0, msec, e.yaw * 180 / kPi);
        return;
    }
    float move_yaw = nav_ ? route_yaw(e, pos, target) : to_target;
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
    // the body turns toward where it walks at the type's turn rate
    const float step = def.turnRate * dt;
    e.yaw += std::clamp(ang_diff(move_yaw, e.yaw), -step, step);
    im_.moveEnemy(e.entity, move_yaw * 180 / kPi, e.speed, 1, msec, e.yaw * 180 / kPi);
}

bool Horde::flyer_free(double x, double y, double z, double r) const {   // nav3FlyerFree (6582)
    const double h = kFlyerHeight;
    if(nav_world_) {
        const auto& w = *nav_world_;
        if(!w.walkable(x, y) || !std::isfinite(x + y + z) || z < -4.05 || z > 22.5) return false;
        if(w.wall_hit(x, y, z, h, r) || w.feature_solid_at(x, y, z, z + h, r)) return false;
        if(w.hits_slab(x, y, r, z + .02, z + h - .02)) return false;
        const auto roof = w.overhead(x, y);
        if(roof && z + h > *roof - .24) return false;
        return true;
    }
    // no N10 data: the same capsule as a Quake III box
    const float p[3]{float((x - cx_) * CA_UNITS_PER_METRE), float((y - cy_) * CA_UNITS_PER_METRE), float(z * CA_UNITS_PER_METRE)};
    const float mins[3]{-u(r), -u(r), 0}, maxs[3]{u(r), u(r), u(h)};
    return im_.boxFree(p, p, mins, maxs) != 0;
}
bool Horde::flyer_sweep(double x, double y, double z, double nx, double ny, double nz, double r) const {   // nav3FlyerSweep
    const double dist = std::hypot(nx - x, ny - y, nz - z);
    const int n = std::max(1, int(std::ceil(dist / .34)));
    for(int i = 1; i <= n; ++i) {
        const double t = double(i) / n;
        if(!flyer_free(x + (nx - x) * t, y + (ny - y) * t, z + (nz - z) * t, r)) return false;
    }
    return true;
}

void Horde::think_flyer(Enemy& e, const float pos[3], const caPlayer_t& target, int msec) {   // updateFlyer (6597)
    const double dt = msec / 1000.0;
    const auto& def = e.def();
    const auto b = to_n10(pos), t = to_n10(target.origin);
    const double r = e.type == 19 ? .52 : e.type == 18 ? .37 : .29;
    e.fly_phase += dt * (e.type == 18 ? 9 : 6);
    const std::array<double, 3> desired{t[0], t[1], std::max(-2.0, std::min(19.0, t[2] + 2.45 + std::sin(e.fly_phase) * .16))};
    double dx = desired[0] - b[0], dy = desired[1] - b[1], dz = desired[2] - b[2];
    const double distance = std::hypot(dx, dy, dz);
    double x = b[0], y = b[1], z = b[2];
    auto finish = [&] {
        const double mx = x - b[0], my = y - b[1];
        if(std::hypot(mx, my) > .005) {
            const float want = float(std::atan2(my, mx)), step = def.turnRate * float(dt);
            e.yaw += std::clamp(ang_diff(want, e.yaw), -step, step);
        } else {
            const float want = std::atan2(target.origin[1] - pos[1], target.origin[0] - pos[0]), step = def.turnRate * float(dt) * .75f;
            e.yaw += std::clamp(ang_diff(want, e.yaw), -step, step);
        }
        float o[3];
        from_n10({x, y, z}, o);
        im_.setEnemyOrigin(e.entity, o, e.yaw * 180 / kPi);
    };
    if(distance < .7) { finish(); return; }
    const double step = std::min(e.speed / CA_UNITS_PER_METRE * dt, distance - .45);
    dx /= distance; dy /= distance; dz /= distance;
    const double yaw = std::atan2(dy, dx), flat = std::hypot(dx, dy);
    if(!e.fly_decide_set) { e.fly_decide = double(e.uid % 4) / 120; e.fly_decide_set = true; }
    e.fly_decide -= dt;
    if(e.fly_decide > 0 && e.has_fly_dir) {   // keep the heading between steering decisions
        const auto& v = e.fly_dir;
        const double nx = x + v[0] * step, ny = y + v[1] * step, nz = z + v[2] * step;
        if(v[0] * dx + v[1] * dy + v[2] * dz > .2 && flyer_sweep(x, y, z, nx, ny, nz, r)) {
            x = nx; y = ny; z = nz; e.fly_stall = std::max(0.0, e.fly_stall - dt * 2);
            finish();
            return;
        }
    }
    e.fly_decide = 1.0 / 30;
    std::vector<std::array<double, 3>> candidates{{dx, dy, dz}, {dx, dy, 0}, {dx, dy, .52}, {dx, dy, -.52}, {0, 0, 1}, {0, 0, -1}};
    for(const double a : {0.45, .9, 1.35, 1.85, 2.35}) for(const double sign : {e.steer_side, -e.steer_side}) {
        const double rad = yaw + sign * a;
        for(const double lift : {dz, .65, -.65, 0.0}) candidates.push_back({std::cos(rad) * std::max(.25, flat), std::sin(rad) * std::max(.25, flat), lift});
    }
    bool found = false;
    double score = -1e300;
    std::array<double, 3> best{}, best_dir{};
    for(const auto& c : candidates) {
        const double norm = std::hypot(c[0], c[1], c[2]), n = norm != 0 ? norm : 1;
        const double vx = c[0] / n, vy = c[1] / n, vz = c[2] / n;
        const double nx = x + vx * step, ny = y + vy * step, nz = z + vz * step;
        if(!flyer_sweep(x, y, z, nx, ny, nz, r)) continue;
        const double new_distance = std::hypot(desired[0] - nx, desired[1] - ny, desired[2] - nz);
        const double dot = vx * dx + vy * dy + vz * dz;
        int clearance = 0;
        for(const double ang : {0.0, kPi * .5, double(kPi), kPi * 1.5}) if(!flyer_free(nx + std::cos(ang) * .55, ny + std::sin(ang) * .55, nz, r)) ++clearance;
        const double sc = (distance - new_distance) * 8 + dot * 1.8 - clearance * .2 + (std::fabs(vz) < .8 ? .10 : 0);
        if(sc > score) { score = sc; best = {nx, ny, nz}; best_dir = {vx, vy, vz}; found = true; }
    }
    e.has_fly_dir = found; e.fly_dir = best_dir;
    if(found) { x = best[0]; y = best[1]; z = best[2]; e.fly_stall = std::max(0.0, e.fly_stall - dt * 2); }
    else {
        e.fly_stall += dt;
        if(e.fly_stall > .35) { e.steer_side = -e.steer_side; e.fly_stall = 0; }
    }
    finish();
}

void Horde::attack(Enemy& e, const float pos[3], const caPlayer_t& target) {   // enemyAttack (11955)
    const auto& def = e.def();
    if(!im_.visible(e.entity, target.clientNum)) return;
    const float dist = dist2d(pos, target.origin);
    const float feet = pos[2] - 24, target_feet = target.origin[2] - 24;
    if(def.ranged) {
        const int kind = def.shot;
        const auto& k = *CA_EnemyShot(kind);
        const bool burst = kind == CA_SHOT_SPIKE, orb = kind == CA_SHOT_VOIDORB;
        const float lead = dist / k.speed * k.lead;
        const float tx = target.origin[0] + target.velocity[0] * lead, ty = target.origin[1] + target.velocity[1] * lead;
        const float oz = def.flying ? u(1.18) : k.originHeight;
        const float dx = tx - pos[0], dy = ty - pos[1], dz = target_feet + u(1.15) - (feet + oz);
        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if(len < u(.1)) return;
        const int count = burst ? (e.type == 4 ? 3 : 2) : 1;   // marauder fires three spikes
        const float vx = dx / len, vy = dy / len;
        const float side = std::sin((float(im_.levelTime()) / 1000.0f + pos[0] / CA_UNITS_PER_METRE * .31f + pos[1] / CA_UNITS_PER_METRE * .27f) * 7.0f) > 0 ? 1.0f : -1.0f;
        for(int n = 0; n < count; ++n) {
            const double spread = burst ? (n - (count - 1) / 2.0) * .09 + (rnd() - .5) * .02 : (rnd() - .5) * (orb ? .025 : .060);
            float o[3];
            if(def.flying) { o[0] = pos[0] + vx * u(.54); o[1] = pos[1] + vy * u(.54); }   // enemyMuzzleOrigin: alternating hand
            else { o[0] = pos[0] + vx * k.originForward - vy * k.originSide * side; o[1] = pos[1] + vy * k.originForward + vx * k.originSide * side; }
            o[2] = feet + oz;
            const float vel[3]{float((vx - vy * spread) * k.speed), float((vy + vx * spread) * k.speed), dz / len * k.speed};
            const int damage = int(std::lround((def.flying ? def.flyingDamage : k.damage) * e.damage_scale));
            const int life = int((dist / k.speed + 1.35f) * 1000);
            im_.fireEnemyShot(e.entity, e.type, kind, o, vel, damage, life, target.clientNum);
        }
    } else if(dist < def.meleeReach && std::fabs(target.origin[2] - pos[2]) < u(1.55)) {
        im_.damagePlayer(target.clientNum, int(std::lround(def.meleeDamage * e.damage_scale)), e.entity);
    }
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
    const auto& prof = profile(e.def().navProfile);   // N10 nav3Profile of the type
    const auto from = to_n10(pos), to = to_n10(target.origin);
    auto aim = [&](const cat::nav::Point& q) { return std::atan2(float(q[1] - from[1]), float(q[0] - from[0])); };
    // straight line when N10's own check says the way is clear
    if(nav_->traverse(from[0], from[1], from[2], to[0], to[1], to[2], prof)) { e.path.clear(); return aim(to); }
    e.path_age_ms += 50;
    const float moved_goal = std::hypot(target.origin[0] - e.goal[0], target.origin[1] - e.goal[1]) + std::fabs(target.origin[2] - e.goal[2]);
    if((e.path.empty() || e.path_index >= e.path.size() || moved_goal > u(2) || e.path_age_ms > 1500) && route_budget_ > 0) {
        --route_budget_;
        e.path = nav_->route(from, to, prof);
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
    if(frozen_) {   // ca_lineup: stand still; every 2.5 s all of them play their attack wind-up
        frozen_ms_ += msec;
        const bool swing = frozen_ms_ >= 2500;
        if(swing) frozen_ms_ = 0;
        for(auto& e : enemies_) if(e.alive()) {
            think(e, players, np, msec);
            if(swing) im_.setEnemyState(e.entity, 1);
        }
        return;
    }
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
        for(auto& e : enemies_) if(e.alive()) {
            if(e.def().flying) { float p[3]; im_.enemyOrigin(e.entity, p); im_.setEnemyOrigin(e.entity, p, e.yaw * 180 / kPi); }
            else im_.moveEnemy(e.entity, e.yaw * 180 / kPi, 0, 0, msec, e.yaw * 180 / kPi);
        }
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
        if(spawn(players, np, -1)) ++waves_.spawned;
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

// Stage D review: on every N10 navigation node (1 m grid over N10's floors), does N10's player
// (radius 0.30 m, height 1.80 m) fit, and does the stock Quake III player box (30 x 30 x 56 units =
// 0.94 x 0.94 x 1.75 m) fit? Lists the nodes where only N10's player fits.
void Horde::fit_survey(int list) {
    if(!nav_world_ || !nav_) { print("ca_fitsurvey: no N10 map data\n"); return; }
    const auto& w = *nav_world_;
    int n10 = 0, q3 = 0, only_n10 = 0, only_q3 = 0, shown = 0;
    char line[256];
    for(const auto& n : nav_->nodes()) {
        const double r = .30, h = 1.80, z = n.z + .05;
        const bool a = w.walkable(n.x, n.y) && !w.wall_hit(n.x, n.y, z, h, r) && !w.feature_solid_at(n.x, n.y, z, z + h, r)
                       && !w.hits_slab(n.x, n.y, r, z + .02, z + h - .02);
        const float p[3]{float((n.x - cx_) * CA_UNITS_PER_METRE), float((n.y - cy_) * CA_UNITS_PER_METRE), float(n.z * CA_UNITS_PER_METRE + 24 + 2)};
        const float mins[3]{-15, -15, -24}, maxs[3]{15, 15, 32};
        const bool b = im_.boxFree(p, p, mins, maxs) != 0;
        n10 += a; q3 += b;
        if(a && !b) {
            ++only_n10;
            if(shown < list) {
                std::snprintf(line, sizeof line, "ca_fitsurvey: only N10 fits at %.0f %.0f %.0f (N10 %.1f %.1f %.1f)\n", p[0], p[1], p[2], n.x, n.y, n.z);
                print(line);
                ++shown;
            }
        }
        if(b && !a) ++only_q3;
    }
    std::snprintf(line, sizeof line, "ca_fitsurvey: %zu nodes; N10 player fits at %d, Quake III player at %d; only N10 at %d, only Quake III at %d\n",
                  nav_->nodes().size(), n10, q3, only_n10, only_q3);
    print(line);
    // Reach: flood N10's standard-profile links (radius 0.32 m, the N10 player's size) from the map's
    // spawn spots, once as N10 (all links) and once keeping only links the Quake III box can sweep
    // (box from 20 units above the higher node, so steps up to 18 units pass, to 56 units above it).
    const auto& nodes = nav_->nodes();
    float spots[64][3];
    const int ns = im_.spawnPoints(spots, 64);
    std::vector<char> reach_n10(nodes.size(), 0), reach_q3(nodes.size(), 0);
    auto to_q3 = [&](const cat::nav::Node& n, double zoff) {
        return std::array<float, 3>{float((n.x - cx_) * CA_UNITS_PER_METRE), float((n.y - cy_) * CA_UNITS_PER_METRE), float(n.z * CA_UNITS_PER_METRE + zoff)};
    };
    for(int pass = 0; pass < 2; ++pass) {
        auto& reach = pass == 0 ? reach_n10 : reach_q3;
        std::vector<std::uint32_t> queue;
        for(int i = 0; i < ns; ++i) {
            const auto p = to_n10(spots[i]);
            if(const auto* n = nav_->nearest(p[0], p[1], p[2], cat::nav::kStandard)) {
                if(!reach[n->id]) { reach[n->id] = 1; queue.push_back(n->id); }
            }
        }
        for(std::size_t qi = 0; qi < queue.size(); ++qi) {
            const auto& a = nodes[queue[qi]];
            for(const auto& e : nav_->neighbors(a.id, cat::nav::kStandard)) {
                if(reach[e.to]) continue;
                const auto& b = nodes[e.to];
                if(pass == 1) {
                    const double top = std::max(a.z, b.z);
                    auto pa = to_q3(a, 0), pb = to_q3(b, 0);
                    pa[2] = pb[2] = float(top * CA_UNITS_PER_METRE);
                    const float mins[3]{-15, -15, 20}, maxs[3]{15, 15, 56};
                    if(!im_.boxFree(pa.data(), pb.data(), mins, maxs)) continue;
                }
                reach[e.to] = 1;
                queue.push_back(e.to);
            }
        }
    }
    // how far is each node only N10's player reaches from the nearest node the Quake III box reaches
    // (same level, within 1 m height): next to it = the 0.17 m the box is wider, by walls and edges;
    // further = an area the Quake III player cannot get to
    int rn = 0, rq = 0, lost = 0, near1 = 0, near2 = 0, far = 0;
    shown = 0;
    for(std::size_t i = 0; i < nodes.size(); ++i) {
        rn += reach_n10[i]; rq += reach_q3[i];
        if(!reach_n10[i] || reach_q3[i]) continue;
        ++lost;
        double best = 1e9;
        for(std::size_t j = 0; j < nodes.size(); ++j) {
            if(!reach_q3[j] || std::fabs(nodes[j].z - nodes[i].z) > 1.0) continue;
            best = std::min(best, std::hypot(nodes[j].x - nodes[i].x, nodes[j].y - nodes[i].y));
        }
        if(best <= 1.01) ++near1; else if(best <= 2.01) ++near2; else ++far;
        if(best > 2.01 && shown < list) {
            const auto q = to_q3(nodes[i], 24);
            std::snprintf(line, sizeof line, "ca_fitsurvey: N10's player only, %.1f m from the Quake III reach: %.0f %.0f %.0f\n", best > 1e8 ? -1.0 : best, q[0], q[1], q[2]);
            print(line);
            ++shown;
        }
    }
    std::snprintf(line, sizeof line, "ca_fitsurvey: reach from %d spawn spots over N10 links: N10 player %d nodes, Quake III box %d; %d only by N10's player: %d within 1 m of the Quake III reach, %d within 2 m, %d further\n",
                  ns, rn, rq, lost, near1, near2, far);
    print(line);
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
    check(w.forced_type(0) == 17 && w.forced_type(1) == 18 && w.forced_type(2) == -1, "wave 3: first two of the batch are a skyprobe and a skyraider");
    // the 20 N10 types (ENEMY_STATS / FLYING_STATS, enemyHitScale)
    {
        int ok = 0;
        struct Row { int type, hp; double speed_m; bool flying, ranged, slam; int melee, wind, cool; };
        static constexpr Row kExpect[] = {
            {0, 85, 8.80875, false, true, false, 11, 610, 1720}, {1, 125, 8.80875, false, true, false, 11, 610, 1720},
            {2, 100, 10.3275, false, false, false, 17, 430, 1140}, {3, 265, 4.617, false, false, true, 27, 880, 2180},
            {5, 235, 4.617, false, true, false, 11, 730, 2400}, {12, 92, 10.3275, false, true, false, 11, 730, 1720},
            {13, 270, 4.617, false, false, true, 27, 880, 2180}, {17, 78, 7.35, true, true, false, 11, 660, 1950},
            {18, 135, 11.35, true, true, false, 11, 660, 1950}, {19, 260, 5.85, true, true, false, 11, 660, 1950}};
        for(const auto& r : kExpect) {
            const auto* t = CA_EnemyType(r.type);
            if(t && t->health == r.hp && std::fabs(t->speed - float(r.speed_m * CA_UNITS_PER_METRE)) < .01f && (t->flying != 0) == r.flying &&
               (t->ranged != 0) == r.ranged && (t->slam != 0) == r.slam && t->meleeDamage == r.melee && t->windMs == r.wind && t->coolMs == r.cool) ++ok;
        }
        const auto* heavy = CA_EnemyType(3);
        const bool box = std::fabs(heavy->hitMaxs[0] - float(.62 * 1.22 * 32)) < .01f && std::fabs(heavy->hitMaxs[2] - float(2.18 * 1.1 * 32 - 24)) < .01f &&
                         std::fabs(CA_EnemyType(2)->hitMaxs[2] - float(2.18 * .9 * 32 - 24)) < .01f && CA_EnemyType(20) == nullptr;
        char what[160];
        std::snprintf(what, sizeof what, "N10 enemy table: %d/10 sampled types match (health, speed, flying, ranged, slam, melee, wind-up, cooldown); hit boxes by class", ok);
        check(ok == 10 && box, what);
        const auto* fire = CA_EnemyShot(CA_SHOT_FIREBALL);
        check(fire && std::fabs(fire->speed - 464.0f) < .01f && fire->damage == 4 && CA_EnemyShot(CA_SHOT_VOIDORB)->damage == 19 && CA_EnemyShot(4) == nullptr,
              "enemy projectiles: fireball 14.5 m/s, 4 damage; void orb 19 damage");
    }
    float spots[64][3];
    const int ns = im_.spawnPoints(spots, 64);
    check(ns > 0, "enemy spawn points found in the map");
    if(ns > 0) {
        // every type spawns on this map (its own hit box) with its N10 health
        int spawned = 0;
        for(int t = 0; t < CA_ENEMY_TYPES; ++t) {
            const auto& def = *CA_EnemyType(t);
            for(int i = 0; i < ns; ++i) {
                float o[3]{spots[i][0], spots[i][1], spots[i][2] + (def.flying ? u(4.5) : 0)};
                const int ent = im_.spawnEnemy(t, o, 0, def.health);
                if(ent < 0) continue;
                if(im_.enemyHealth(ent) == def.health) ++spawned;
                im_.removeEnemy(ent);
                break;
            }
        }
        char what[96];
        std::snprintf(what, sizeof what, "all 20 enemy types spawn with their N10 health (%d/20)", spawned);
        check(spawned == CA_ENEMY_TYPES, what);
        // move 0.5 s in each of four directions from fresh spawns; the best direction counts
        // (a spawn spot may face a wall: Reactor Bastion's first spawn does)
        const auto& fiend = *CA_EnemyType(CA_ENEMY_FIEND);
        float moved = 0;
        for(int dir = 0; dir < 4; ++dir) {
            const int probe = im_.spawnEnemy(CA_ENEMY_FIEND, spots[0], 0, fiend.health);
            if(probe < 0) continue;
            float before[3], after[3];
            im_.enemyOrigin(probe, before);
            im_.moveEnemy(probe, float(dir * 90), fiend.speed, 1, 500, float(dir * 90));
            im_.enemyOrigin(probe, after);
            moved = std::max(moved, dist2d(before, after));
            im_.removeEnemy(probe);
        }
        std::snprintf(what, sizeof what, "fiend moves with Pmove (best of 4 directions: %.0f units in 0.5 s)", moved);
        check(moved > 50 && moved < fiend.speed * 0.5f + 1, what);
        // a flyer steers toward a target 1 s away (N10 updateFlyer on this map's collision)
        {
            int best_i = -1;
            float far = 0;
            for(int i = 1; i < ns; ++i) if(dist2d(spots[i], spots[0]) > far) { far = dist2d(spots[i], spots[0]); best_i = i; }
            float progress = -1, start_d = 0, end_d = 0;
            for(int i = 0; i < ns && progress < 0; ++i) {
                if(i == best_i) continue;
                float o[3]{spots[i][0], spots[i][1], spots[i][2] + u(4.5)};
                const auto at = to_n10(o);
                if(!flyer_free(at[0], at[1], at[2], .29)) continue;   // under a roof: no room to fly (N10 spawns flyers in the open)
                const int ent = im_.spawnEnemy(17, o, 0, 78);
                if(ent < 0) continue;
                Enemy* e = adopt(ent, 17, 0, 1);
                if(!e) { im_.removeEnemy(ent); break; }
                caPlayer_t target{};
                target.clientNum = -1;
                std::memcpy(target.origin, spots[best_i >= 0 ? best_i : 0], sizeof target.origin);
                target.alive = 1;
                float p[3];
                im_.enemyOrigin(ent, p);
                start_d = std::hypot(p[0] - target.origin[0], p[1] - target.origin[1], p[2] - (target.origin[2] + u(2.45)));
                for(int f = 0; f < 20; ++f) { im_.enemyOrigin(ent, p); think_flyer(*e, p, target, 50); }
                im_.enemyOrigin(ent, p);
                end_d = std::hypot(p[0] - target.origin[0], p[1] - target.origin[1], p[2] - (target.origin[2] + u(2.45)));
                progress = start_d - end_d;
                im_.removeEnemy(ent);
                e->entity = -1;
            }
            std::snprintf(what, sizeof what, "skyprobe flies toward its target: %.0f of %.0f units closer in 1 s (speed 235 u/s)", progress, start_d);
            check(progress > u(1.5) && progress <= CA_EnemyType(17)->speed * 1.0f + 1, what);
        }
        const int kills_before = waves_.kills;
        const int ent = im_.spawnEnemy(CA_ENEMY_FIEND, spots[0], 0, fiend.health);
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
extern "C" void CA_HordeLineup(const float origin[3], float yawDeg) {
    try { if(origin) g_horde.lineup(origin, yawDeg); else g_horde.end_lineup(); } catch(...) {}
}
extern "C" int CA_HordeLineupActive(void) { return g_horde.lineup_active() ? 1 : 0; }
extern "C" void CA_HordeFitSurvey(int list) {
    try { g_horde.fit_survey(list); } catch(...) {}
}
extern "C" int CA_HordePickups(float* xyz, int max) {
    try { return g_horde.pickups(xyz, max); } catch(...) { return 0; }
}
extern "C" int CA_HordeSelfTest(void) {
    try { return g_horde.self_test(); } catch(...) { return 0; }
}
