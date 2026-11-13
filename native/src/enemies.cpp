// N6 enemies: see cat/enemies.hpp. Statement order, floating-point order and the order of random
// draws follow the legacy functions named in the comments.
#include "cat/enemies.hpp"
#include "cat/js_math.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace cat::combat {
namespace {
using physics::js_hypot;
using physics::js_hypot3;
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kTau = 6.283185307179586;
constexpr auto S = TargetClass::standard;
constexpr auto H = TargetClass::heavy;
constexpr auto F = TargetClass::fast;
// ENEMY_TYPES order (13225). Columns: name, hp, speed, flying, flyer attack, hit class, ranged, slam,
// orb, warden, shot kind, nav profile, turn rate, stride, melee damage, palette.
constexpr std::array<EnemyType, kEnemyTypeCount> kTypes{{
    {"grunt", 85, kEnemyStandardSpeed, false, 0, S, true, false, false, false, ShotKind::shot, 1, 4.0, 1.02, 11, {.42F, .54F, .31F}},
    {"ogre", 125, kEnemyStandardSpeed, false, 0, H, true, false, false, false, ShotKind::fireball, 2, 2.7, 1.02, 11, {.71F, .68F, .62F}},
    {"fiend", 100, kEnemyFastSpeed, false, 0, F, false, false, false, false, ShotKind::shot, 0, 5.2, 1.20, 17, {.73F, .43F, .37F}},
    {"shambler", 265, kEnemySlowSpeed, false, 0, H, false, true, false, false, ShotKind::shot, 2, 2.7, .86, 27, {.88F, .84F, .79F}},
    {"marauder", 140, kEnemyStandardSpeed, false, 0, S, true, false, false, false, ShotKind::spike, 1, 4.0, 1.02, 11, {.40F, .42F, .31F}},
    {"warden", 235, kEnemySlowSpeed, false, 0, H, true, false, true, true, ShotKind::voidorb, 2, 2.7, 1.02, 11, {.51F, .51F, .60F}},
    {"skitter", 73, kEnemyFastSpeed, false, 0, F, false, false, false, false, ShotKind::shot, 0, 5.2, 1.02, 11, {.48F, .33F, .34F}},
    {"stalker", 78, kEnemyFastSpeed, false, 0, F, false, false, false, false, ShotKind::shot, 0, 5.2, 1.20, 11, {.36F, .45F, .34F}},
    {"brute", 200, kEnemySlowSpeed, false, 0, H, false, true, false, false, ShotKind::shot, 2, 2.7, 1.02, 27, {.51F, .43F, .34F}},
    {"pyromancer", 105, kEnemyStandardSpeed, false, 0, S, true, false, false, false, ShotKind::fireball, 1, 4.0, 1.02, 11, {.68F, .28F, .20F}},
    {"sentinel", 155, kEnemyStandardSpeed, false, 0, S, true, false, true, true, ShotKind::voidorb, 1, 4.0, 1.02, 11, {.28F, .41F, .51F}},
    {"reaver", 115, kEnemyFastSpeed, false, 0, F, false, false, false, false, ShotKind::shot, 0, 5.2, 1.02, 17, {.64F, .47F, .30F}},
    {"banshee", 92, kEnemyFastSpeed, false, 0, F, true, false, true, false, ShotKind::voidorb, 0, 4.0, 1.02, 11, {.43F, .51F, .66F}},
    {"crusher", 270, kEnemySlowSpeed, false, 0, H, false, true, false, false, ShotKind::shot, 2, 2.7, .86, 27, {.52F, .48F, .46F}},
    {"sapper", 110, kEnemyStandardSpeed, false, 0, S, true, false, false, false, ShotKind::spike, 1, 4.0, 1.02, 11, {.39F, .50F, .29F}},
    {"harrier", 92, kEnemyFastSpeed, false, 0, F, true, false, false, false, ShotKind::spike, 0, 5.2, 1.02, 11, {.62F, .54F, .44F}},
    {"cultist", 125, kEnemyStandardSpeed, false, 0, S, true, false, false, false, ShotKind::fireball, 1, 4.0, 1.02, 11, {.51F, .30F, .45F}},
    {"skyprobe", 78, 7.35, true, 7, F, true, false, false, false, ShotKind::shot, 1, 5.2, 1.02, 11, {.40F, .70F, .88F}},
    {"skyraider", 135, 11.35, true, 12, F, true, false, false, false, ShotKind::spike, 1, 5.2, 1.02, 11, {.75F, .30F, .36F}},
    {"skyjuggernaut", 260, 5.85, true, 24, H, true, false, false, false, ShotKind::voidorb, 1, 2.7, 1.02, 11, {.62F, .49F, .80F}},
}};
const EnemyType& type_of(const Target& b) { return kTypes[static_cast<std::size_t>(b.type)]; }
double ang_norm(double a) { return jsmath::atan2(jsmath::sin(a), jsmath::cos(a)); }
double ang_approach(double cur, double target, double max_step) {
    const double diff = ang_norm(target - cur);
    if(std::fabs(diff) <= max_step) return cur + diff;
    return cur + (diff > 0 ? 1.0 : diff < 0 ? -1.0 : 0.0) * max_step;
}
double facing_from(double vx, double vy) { return jsmath::atan2(-vx, vy); }   // enemyFacingFromVec
constexpr double kDeathSeconds = .78 + 4.5 + 1.6;   // ENEMY_DEATH fall + stay + sink
}  // namespace

const std::array<EnemyType, kEnemyTypeCount>& enemy_types() { return kTypes; }
int enemy_type_index(const char* name) {
    for(std::size_t i = 0; i < kTypes.size(); ++i) if(std::strcmp(kTypes[i].name, name) == 0) return static_cast<int>(i);
    return -1;
}
const nav::Profile& Horde::profile(int index) { return index == 0 ? nav::kLight : index == 2 ? nav::kHeavy : nav::kStandard; }

Horde::Horde(Arena& arena, nav::NavGraph& graph) : arena_(&arena), graph_(&graph) { arena.attach(this); }
double Horde::scale() const { return jsmath::pow(1.05, std::max(0, wave_number_ - 1)); }
Target* Horde::find(std::uint32_t uid) {
    for(auto& t : arena_->targets_) if(t.type >= 0 && t.uid == uid) return &t;
    return nullptr;
}
void Horde::face(Target& b, double desired, double dt) {   // updateEnemyFacing
    if(!std::isfinite(desired)) return;
    if(!std::isfinite(b.a)) b.a = desired;
    b.a = ang_approach(b.a, desired, type_of(b).turn_rate * dt);
}
void Horde::reset_jobs() { ++epoch_; queue_.clear(); job_.reset(); }   // resetNavJobs

// ---- spawning and waves ---------------------------------------------------------------------------
void Horde::spawn_enemy(int forced) {   // spawnBot (11461)
    auto& rng = arena_->rng_;
    const auto& world = *arena_->world_;
    const auto& player = arena_->player_.state();
    const auto& nodes = graph_->nodes();
    const int type = forced >= 0 ? forced : static_cast<int>(std::floor(rng.next() * static_cast<double>(kEnemyTypeCount)));
    const auto& def = kTypes[static_cast<std::size_t>(type)];
    const double scaling = scale();
    // worldRandomSpawn(minDistance, 'player') (9726): shared by flyers and ground enemies as a fallback
    auto player_pool_spawn = [&](double min_distance) -> std::array<double, 3> {
        std::vector<const physics::Spawn*> pool;
        for(const auto& sp : world.spawns()) if(world.capsule_clear(sp.x, sp.y, sp.z, physics::kPlayerHeight, physics::kPlayerRadius)) pool.push_back(&sp);
        if(pool.empty()) {   // worldPlayerStart
            if(!nodes.empty()) return {nodes[0].x, nodes[0].y, nodes[0].z};
            return {58.5, 58.5, 0};
        }
        std::vector<const physics::Spawn*> far;
        for(const auto* sp : pool) if(js_hypot(sp->x - player.x, sp->y - player.y) >= min_distance) far.push_back(sp);
        const auto& list = far.empty() ? pool : far;
        struct Scored { const physics::Spawn* spawn; double v; };
        std::vector<Scored> scored;
        for(const auto* sp : list) {
            double near = kInf;
            for(const auto& b : arena_->targets_) near = std::min(near, js_hypot(b.x - sp->x, b.y - sp->y) + std::fabs(b.z - sp->z));
            scored.push_back({sp, (std::isfinite(near) ? std::min(near, 40.0) : 40.0) + rng.next() * 14});
        }
        std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) { return a.v > b.v; });
        const auto& sp = *scored[static_cast<std::size_t>(std::floor(rng.next() * static_cast<double>(std::min<std::size_t>(3, scored.size()))))].spawn;
        return {sp.x, sp.y, sp.z};
    };
    std::optional<std::array<double, 3>> p;
    if(def.flying) {   // worldRandomSpawn(30, 'air'): an open-sky graph node away from the player
        for(int i = 0; i < 220 && !nodes.empty(); ++i) {
            const auto& n = nodes[static_cast<std::size_t>(std::floor(rng.next() * static_cast<double>(nodes.size())))];
            if(js_hypot(n.x - player.x, n.y - player.y) < 30) continue;
            if(world.overhead(n.x, n.y)) continue;
            std::array<const physics::Slab*, 64> q;
            const auto count = world.slab_query(n.x, n.y, q);
            bool covered = false;
            for(std::size_t k = 0; k < count; ++k) { const double z = physics::CollisionWorld::slab_z(*q[k], n.x, n.y); if(z > n.z + .4 && z < n.z + 9) { covered = true; break; } }
            if(covered) continue;
            p = std::array<double, 3>{n.x, n.y, n.z};
            break;
        }
        if(!p) p = player_pool_spawn(30);
    } else {   // nav3RandomEnemySpawn(type, 30): a node of a randomly chosen layer
        if(!nodes.empty()) {
            const auto& prof = profile(def.nav_profile);
            const double roll = rng.next();
            const nav::Layer layer = roll < .17 ? nav::Layer::l1 : roll < .44 ? nav::Layer::l3 : nav::Layer::l2;
            for(int i = 0; i < 160; ++i) {
                const auto& n = nodes[static_cast<std::size_t>(std::floor(rng.next() * static_cast<double>(nodes.size())))];
                if(n.layer != layer || !world.capsule_clear(n.x, n.y, n.z, prof.height, prof.radius)) continue;
                if(js_hypot(n.x - player.x, n.y - player.y) < 30) continue;
                p = std::array<double, 3>{n.x, n.y, n.z};
                break;
            }
        }
        if(!p) p = player_pool_spawn(30);
    }
    const double base_hp = def.hp, base_speed = def.speed;
    const double hp = base_hp * scaling, speed = base_speed * scaling;
    const double ground = (*p)[2];
    const auto roof = world.overhead((*p)[0], (*p)[1]);
    const double fly_alt = std::min(ground + 4.3 + rng.next() * 2.1, (roof ? *roof : 26.0) - 2.8);
    const double start_z = def.flying ? std::max(ground + 3.6, fly_alt) : ground;
    Target b;
    b.x = (*p)[0]; b.y = (*p)[1]; b.z = start_z; b.hp = hp; b.max_hp = hp; b.speed = speed; b.base_speed = base_speed; b.damage_scale = scaling; b.flying = def.flying;
    b.fly_alt = start_z;
    b.fly_phase = rng.next() * 6.283;
    b.cool = 1.15 + rng.next() * 1.35;
    b.ident = rng.next() * 6.28;
    b.uid = ++serial_;
    b.type = type; b.cls = def.hit_class;
    b.a = rng.next() * kTau;
    b.anim_phase = rng.next() * 6.28;
    b.path_cooldown = rng.next() * .6;
    b.voice_cooldown = 2 + rng.next() * 5;
    b.id = arena_->next_target_id_++;
    arena_->targets_.push_back(std::move(b));
}
void Horde::begin_wave(int next, bool soft) {   // beginWave (11483)
    ++waves_begun_;
    reset_jobs();
    if(next == 1) { wave_number_ = 1; wave_total_ = 10; }
    else { wave_number_ = next; wave_total_ = static_cast<int>(std::ceil(wave_total_ * 1.25)); }
    arena_->targets_.clear(); shots_.clear();
    wave_spawned_ = 0; wave_kills_ = 0; wave_cooldown_ = -1; wave_spawn_timer_ = 0;
    // A small first batch; the first three are flyers from waves 1 / 3 / 5 on.
    const int upfront = soft ? std::min(3, wave_total_) : std::min(wave_total_, std::min(18, 10 + wave_number_ / 2));
    for(int i = 0; i < upfront; ++i) {
        const int forced = i == 0 ? 17 : (i == 1 && wave_number_ >= 3) ? 18 : (i == 2 && wave_number_ >= 5) ? 19 : -1;
        spawn_enemy(forced);
        ++wave_spawned_;
    }
    if(soft) wave_spawn_timer_ = .18;
}
void Horde::tick_waves(double dt) {   // tickWaves (11494)
    if(wave_reset_pending_) { wave_reset_pending_ = false; begin_wave(1, true); return; }
    if(wave_cooldown_ >= 0) { wave_cooldown_ -= dt; if(wave_cooldown_ <= 0) { wave_cooldown_ = -1; begin_wave(wave_number_ + 1); } return; }
    wave_spawn_timer_ -= dt;
    const auto alive = static_cast<int>(arena_->targets_.size());
    if(wave_spawned_ < wave_total_ && alive < kMaxEnemies && wave_spawn_timer_ <= 0) { spawn_enemy(); ++wave_spawned_; wave_spawn_timer_ = .20; }
    if(wave_kills_ >= wave_total_ && arena_->targets_.empty()) wave_cooldown_ = 3.5;
}
// ---- N8 guest: the host's enemies as puppets ---------------------------------------------------------------
const char* shot_kind_name(ShotKind k) {
    switch(k) { case ShotKind::spike: return "spike"; case ShotKind::fireball: return "fireball"; case ShotKind::voidorb: return "voidorb"; default: return "shot"; }
}
std::optional<ShotKind> shot_kind_from(const std::string& n) {
    if(n == "shot") return ShotKind::shot;
    if(n == "spike") return ShotKind::spike;
    if(n == "fireball") return ShotKind::fireball;
    if(n == "voidorb") return ShotKind::voidorb;
    return std::nullopt;
}
void Horde::apply_world(const WorldState& w) {   // netOnRelay 'world' (15709-15748)
    wave_number_ = w.wave; wave_total_ = w.total; wave_kills_ = w.kills; wave_spawned_ = w.spawned; wave_cooldown_ = w.cooldown;
    auto& targets = arena_->targets_;
    std::vector<Target> next;
    next.reserve(w.bots.size());
    for(const auto& p : w.bots) {
        if(next.size() >= 40) break;
        auto it = std::find_if(targets.begin(), targets.end(), [&](const Target& t) { return t.uid == p.uid && t.type >= 0; });
        Target b;
        if(it != targets.end()) b = *it;
        else {   // a creature this player has not seen yet: appears at the host's position
            const int type = p.type >= 0 && p.type < static_cast<int>(kEnemyTypeCount) ? p.type : 0;
            const auto& def = kTypes[static_cast<std::size_t>(type)];
            b.type = type; b.uid = p.uid; b.id = arena_->next_target_id_++;
            b.cls = def.hit_class; b.flying = def.flying;
            b.x = p.x; b.y = p.y; b.z = p.z; b.a = p.a;
            b.base_speed = b.speed = def.speed;
        }
        b.sync_x = p.x; b.sync_y = p.y; b.sync_z = p.z; b.has_sync = true;
        b.a = p.a; b.anim_phase = p.phase; b.walk_amount = p.walk;
        b.hp = p.hp; b.max_hp = p.max_hp; b.hit_flash = p.hit;
        b.attack_wind = p.wind; b.attack_total = p.total_wind; b.attack_recover = p.recover; b.attack_recover_total = p.total_recover;
        next.push_back(std::move(b));
    }
    // creatures that vanished from the host's snapshot died: play their death here (within 70 m)
    const auto& me = arena_->player_.state();
    for(const auto& old : targets) {
        if(old.type < 0) continue;
        const bool alive = std::any_of(next.begin(), next.end(), [&](const Target& t) { return t.uid == old.uid; });
        if(!alive && js_hypot(old.x - me.x, old.y - me.y) < 70) enemy_died(old);
    }
    targets = std::move(next);
    shots_.clear();
    for(const auto& p : w.shots) {
        if(shots_.size() >= 100) break;
        EnemyShot e;
        e.x = p.x; e.y = p.y; e.z = p.z; e.vx = p.vx; e.vy = p.vy; e.vz = p.vz; e.life = p.life; e.kind = p.kind; e.type = p.type < 0 ? 0 : p.type;
        e.id = next_shot_id_++;
        shots_.push_back(e);
    }
}
void Horde::puppet_update(double dt) {
    if(pending_world_) { apply_world(*pending_world_); pending_world_.reset(); }
    const double a = 1 - std::exp(-dt * 16);   // netGuestInterp
    for(auto& b : arena_->targets_) {
        b.hit_flash = std::max(0.0, b.hit_flash - dt);
        if(b.type < 0 || !b.has_sync) continue;
        b.x += (b.sync_x - b.x) * a; b.y += (b.sync_y - b.y) * a; b.z += (b.sync_z - b.z) * a;
    }
    for(std::size_t i = shots_.size(); i-- > 0;) {   // the guest branch of the enemy-shot loop: move, no collision
        auto& s = shots_[i];
        s.life -= dt;
        s.x += s.vx * dt; s.y += s.vy * dt; s.z += s.vz * dt;
        if(s.life <= 0) shots_.erase(shots_.begin() + static_cast<std::ptrdiff_t>(i));
    }
}
void Horde::player_respawned() { shots_.clear(); pending_purge_ = false; wave_reset_pending_ = true; }
double attack_progress(const Target& b) {
    if(!(b.attack_total > 0)) return -1;
    if(b.attack_wind > 0) return std::clamp((1 - b.attack_wind / b.attack_total) * .72, 0.0, .72);
    if(b.attack_recover > 0 && b.attack_recover_total > 0) return .72 + .28 * (1 - std::clamp(b.attack_recover / b.attack_recover_total, 0.0, 1.0));
    return -1;
}
void Horde::enemy_died(const Target& dead) {   // enemyDeathFx: gibs below -40 hp, otherwise a corpse that topples, stays and sinks
    if(dead.type < 0) return;
    const auto& ps = arena_->player_.state();
    DeathEvent e;
    e.uid = dead.uid; e.type = dead.type; e.x = dead.x; e.y = dead.y; e.z = dead.z; e.a = dead.a; e.hp = dead.hp; e.hit_flash = dead.hit_flash;
    e.attack_progress = attack_progress(dead); e.now = arena_->now(); e.player_x = ps.x; e.player_y = ps.y; e.player_z = ps.z; e.flying = dead.flying;
    e.gibbed = dead.hp <= -40;
    e.seq = arena_->next_event_seq();
    if(e.gibbed) { deaths_.push_back(e); return; }
    e.corpse_id = next_corpse_id_;
    deaths_.push_back(e);
    Corpse c;
    c.type = dead.type; c.x = dead.x; c.y = dead.y; c.z = dead.z; c.a = dead.a; c.flyer = dead.flying; c.id = next_corpse_id_++;
    c.floor_z = dead.flying ? arena_->world_->floor_height(dead.x, dead.y, dead.z) : dead.z;
    corpses_.push_back(c);
    if(corpses_.size() > 24) corpses_.erase(corpses_.begin());
}
Target& Horde::stage_enemy(int type, double x, double y, double z, double a) {
    spawn_enemy(type);
    Target& b = arena_->targets_.back();
    b.x = x; b.y = y; b.z = z; b.a = a; b.hit_flash = 0;
    b.path.clear(); b.path_index = 0; ++b.path_serial;
    return b;
}
std::uint32_t Horde::stage_corpse(int type, double x, double y, double z, double floor_z, double a, double t) {
    Corpse c;
    c.type = type; c.x = x; c.y = y; c.z = z; c.floor_z = floor_z; c.a = a; c.t = t; c.flyer = kTypes[static_cast<std::size_t>(type)].flying; c.id = next_corpse_id_++;
    corpses_.push_back(c);
    return c.id;
}
void Horde::update_corpses(double dt) {   // updateDyingEnemies
    for(std::size_t i = corpses_.size(); i-- > 0;) {
        auto& d = corpses_[i];
        d.t += dt;
        if(d.flyer && d.z > d.floor_z) { d.vz -= physics::kGravity * dt; d.z = std::max(d.floor_z, d.z + d.vz * dt); }
        if(d.t > kDeathSeconds) corpses_.erase(corpses_.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

// ---- per tick ---------------------------------------------------------------------------------------
bool Horde::decision_tick(double dt) {
    ai_acc_ += dt;
    const bool decision = ai_acc_ >= 1.0 / 30;
    if(decision) ai_acc_ -= 1.0 / 30;
    return decision;
}
bool Horde::line_of_sight(const Target& b) const { return line_of_sight(b, arena_->target_for(b.x, b.y)); }
bool Horde::line_of_sight(const Target& b, const Arena::Aim& t) const {   // enemy3DHasLineOfSight (11948)
    const double az = b.z + (b.flying ? 1.18 : 1.30), tz = t.z + 1.15;
    const double d = js_hypot3(t.x - b.x, t.y - b.y, tz - az);
    if(d < .20) return true;
    return arena_->world_->shot_collision_t(b.x, b.y, az, t.x, t.y, tz, .035) >= .998;
}
void Horde::update(double dt, bool decision) {   // update() 12236-12305
    impacts_.clear();
    if(puppet_) { puppet_update(dt); return; }
    auto& targets = arena_->targets_;
    if(decision) search_pool_ = 2;
    int budget = search_pool_;
    for(std::size_t i = 0; i < targets.size(); ++i) {
        auto& b = targets[i];
        b.hit_flash = std::max(0.0, b.hit_flash - dt);
        if(b.type < 0) continue;   // practice target
        const auto& def = type_of(b);
        const auto player = arena_->target_for(b.x, b.y);   // enemyTargetFor(b)
        const double dx = player.x - b.x, dy = player.y - b.y, dist = js_hypot(dx, dy);
        if(b.flying) { update_flyer(b, dt); b.nav_stride_acc = 0; }
        else {
            // Distant enemies keep full-speed displacement but steer at 60 / 40 Hz; near, stalled or attacking ones at 120 Hz.
            const int far = dist > 42 ? 3 : dist > 27 ? 2 : 1;
            const int cadence = (b.nav_stuck > .12 || b.attack_wind > 0) ? 1 : far;
            b.nav_stride_acc += dt;
            ++b.nav_stride_frames;
            if(decision) b.nav_decision = true;
            if(b.nav_stride_frames >= cadence) {
                budget = navigate(b, std::min(b.nav_stride_acc, dt * 3), b.nav_decision, budget);
                b.nav_stride_acc = 0; b.nav_stride_frames = 0; b.nav_decision = false;
            }
        }
        b.voice_cooldown -= dt;
        if(b.voice_cooldown <= 0) {   // playEnemyVoice(b, 'idle') (12256)
            arena_->events_.voices.push_back({b.x, b.y, b.z, b.type, VoiceKind::idle, b.id});
            b.voice_cooldown = 4.4 + arena_->rng_.next() * 5.3;
        }
        b.cool -= dt;
        // The hit lands once, when the wind-up ends (72 % of the visible attack); recovery is animation only.
        if(b.attack_wind > 0) {
            b.attack_wind = std::max(0.0, b.attack_wind - dt);
            if(b.attack_wind == 0) {
                b.attack_recover_total = b.flying ? .32 : def.slam ? .39 : .29;
                b.attack_recover = b.attack_recover_total;
                attack(b);
            }
        } else if(b.attack_recover > 0) {
            b.attack_recover = std::max(0.0, b.attack_recover - dt);
        } else if(decision && b.cool <= 0 && dist < (def.ranged ? 28 : def.slam ? 2.5 : 1.9)) {
            if(line_of_sight(b, player)) {
                b.attack_wind = b.flying ? .66 : def.slam ? .88 : def.orb ? .73 : def.ranged ? .61 : .43;
                b.attack_total = b.attack_wind;
                b.attack_recover = 0; b.attack_recover_total = 0;
                b.cool = b.flying ? 1.95 : def.slam ? 2.18 : def.warden ? 2.4 : def.ranged ? 1.72 : 1.14;
            } else b.cool = .35;
        }
    }
    search_pool_ = budget;
    process_jobs();   // one bounded navigation slice per tick
    // Enemy projectiles: swept segment per tick against the player's box and the world.
    const double now = arena_->player_.now();
    (void)now;
    for(std::size_t i = shots_.size(); i-- > 0;) {
        auto& s = shots_[i];
        s.life -= dt;
        const double nx = s.x + s.vx * dt, ny = s.y + s.vy * dt, nz = s.z + s.vz * dt;
        const Teammate* mate = s.target_key ? arena_->teammate(s.target_key) : nullptr;
        const double radius = s.kind == ShotKind::voidorb ? .21 : s.kind == ShotKind::fireball ? .16 : .08;
        double hit_time = -1;
        if(mate) {   // marineShotTime: the six Marine Cat parts of that player
            for(const Box& part : arena_->marine_boxes(*mate)) { const double tt = segment_box_time(s.x, s.y, s.z, nx, ny, nz, part, radius); if(tt >= 0 && (hit_time < 0 || tt < hit_time)) hit_time = tt; }
        } else hit_time = segment_box_time(s.x, s.y, s.z, nx, ny, nz, arena_->player_box(), radius);
        const double wall_time = arena_->world_->shot_collision_t(s.x, s.y, s.z, nx, ny, nz, radius * .70);
        const bool hit = hit_time >= 0 && hit_time <= wall_time, wall = wall_time != kInf;
        const double t = hit ? hit_time : wall ? wall_time : 1;
        const double hx = s.x + (nx - s.x) * t, hy = s.y + (ny - s.y) * t, hz = s.z + (nz - s.z) * t;
        if(hit) {
            impacts_.push_back({hx, hy, hz, s.kind, s.type, true, s.vx, s.vy, s.vz, arena_->next_event_seq()});
            const bool fire = s.kind == ShotKind::fireball;
            if(mate) arena_->remote_damage(*arena_->teammate(s.target_key), s.damage);
            else if(s.target_key) {}   // aimed at a player who left: remoteApplyDamage finds nobody
            else {
                const bool died = arena_->take_damage(s.damage);
                if(!died && arena_->respawn_grace_ <= 0 && fire) { arena_->burn_remaining_ = 3; arena_->burn_tick_ = .5; }   // igniteBurn
            }
            shots_.erase(shots_.begin() + static_cast<std::ptrdiff_t>(i));
        } else if(wall || s.life <= 0) {
            if(wall || s.kind == ShotKind::fireball || s.kind == ShotKind::voidorb) impacts_.push_back({hx, hy, hz, s.kind, s.type, false, s.vx, s.vy, s.vz, arena_->next_event_seq()});
            shots_.erase(shots_.begin() + static_cast<std::ptrdiff_t>(i));
        } else { s.x = nx; s.y = ny; s.z = nz; s.trail = .075; }
    }
    if(pending_purge_) { shots_.clear(); pending_purge_ = false; }
}

void Horde::attack(Target& b) {   // enemyAttack (11955)
    arena_->events_.voices.push_back({b.x, b.y, b.z, b.type, VoiceKind::attack, b.id});   // playEnemyVoice(b, 'attack') (11956)
    const auto& def = type_of(b);
    const auto target = arena_->target_for(b.x, b.y);   // enemyTargetFor(b)
    const double dist = js_hypot(target.x - b.x, target.y - b.y);
    if(!line_of_sight(b, target)) return;
    if(def.ranged) {
        const ShotKind kind = def.shot;
        const bool fire = kind == ShotKind::fireball, orb = kind == ShotKind::voidorb, burst = kind == ShotKind::spike;
        const double speed = fire ? 14.5 : orb ? 16.2 : burst ? 22.0 : 21.0;
        const double lead = dist / speed * (fire ? .34 : orb ? .38 : .42), tx = target.x + target.vx * lead, ty = target.y + target.vy * lead;
        const double dx = tx - b.x, dy = ty - b.y, dz = target.z + 1.15 - (b.z + (b.flying ? 1.18 : fire ? 1.34 : orb ? 1.58 : 1.20)), len = js_hypot3(dx, dy, dz);
        if(len < .1) return;
        const int count = burst ? (b.type == 4 ? 3 : 2) : 1;   // marauder fires three spikes
        if(static_cast<int>(shots_.size()) + count > kMaxEnemyShots) return;
        const double now = arena_->player_.now();
        for(int n = 0; n < count; ++n) {
            const double spread = burst ? (n - (count - 1) / 2.0) * .09 + (arena_->rng_.next() - .5) * .02 : (arena_->rng_.next() - .5) * (orb ? .025 : .060);
            const double vx = dx / len, vy = dy / len;
            // enemyMuzzleOrigin (11939): alternating hand
            const double side = jsmath::sin((now + b.x * .31 + b.y * .27) * 7.0) > 0 ? 1 : -1;
            double ox, oy, oz;
            if(b.flying) { ox = b.x + vx * .54; oy = b.y + vy * .54; oz = b.z + 1.18; }
            else if(fire) { ox = b.x + vx * .48 - vy * .17 * side; oy = b.y + vy * .48 + vx * .17 * side; oz = b.z + 1.34; }
            else if(orb) { ox = b.x + vx * .50 - vy * .16 * side; oy = b.y + vy * .50 + vx * .16 * side; oz = b.z + 1.58; }
            else if(burst) { ox = b.x + vx * .53 - vy * .15 * side; oy = b.y + vy * .53 + vx * .15 * side; oz = b.z + 1.20; }
            else { ox = b.x + vx * .50 - vy * .12 * side; oy = b.y + vy * .50 + vx * .12 * side; oz = b.z + 1.18; }
            EnemyShot s;
            s.x = ox; s.y = oy; s.z = oz;
            s.vx = (vx - vy * spread) * speed; s.vy = (vy + vx * spread) * speed; s.vz = dz / len * speed;
            s.life = dist / speed + 1.35; s.kind = kind; s.type = b.type;
            s.damage = (b.flying ? def.flying_attack : fire ? 4.0 : orb ? 19.0 : burst ? 6.0 : 9.0) * b.damage_scale;
            s.id = next_shot_id_++;
            s.target_key = target.key;
            shots_.push_back(s);
        }
    } else if(dist < (def.slam ? 2.2 : 1.55) && std::fabs(target.z - b.z) < 1.55) {
        if(target.key) { if(Teammate* m = arena_->teammate(target.key)) arena_->remote_damage(*m, def.melee_damage * b.damage_scale); }
        else arena_->take_damage(def.melee_damage * b.damage_scale);
    }
}

// ---- ground navigation ------------------------------------------------------------------------------
double Horde::move(Target& b, double tx, double ty, double tz, double budget, const nav::Profile& p) {   // nav3Move (6471)
    const double dx = tx - b.x, dy = ty - b.y, d = js_hypot(dx, dy);
    if(d < .002) return 0;
    const double step = std::min(d, budget), mx = b.x + dx / d * step, my = b.y + dy / d * step;
    const double expected = b.z + (tz - b.z) * step / d;
    const auto support = arena_->world_->support(mx, my, b.z, p.max_rise);
    const bool equipment = support && !support->slab->has_plane && support->slab->kind == "equipment";
    const double mz = support && !equipment && support->z >= b.z - physics::kGroundSnap - .01 && std::fabs(support->z - expected) < .56 ? support->z : expected;
    if(graph_->traverse(b.x, b.y, b.z, mx, my, mz, p)) { b.x = mx; b.y = my; b.z = mz; return step; }
    // Slide along the nearest free wall at the same height.
    const double sx = b.x + dx / d * step, sy = b.y + dy / d * step;
    if(graph_->traverse(b.x, b.y, b.z, sx, b.y, b.z, p)) { const double old = b.x; b.x = sx; return std::fabs(sx - old); }
    if(graph_->traverse(b.x, b.y, b.z, b.x, sy, b.z, p)) { const double old = b.y; b.y = sy; return std::fabs(sy - old); }
    return 0;
}
int Horde::navigate(Target& b, double dt, bool decision, int search_left) {   // botNavigation (6492)
    const auto& def = type_of(b);
    const auto& p = profile(def.nav_profile);
    const double speed = b.base_speed * scale();
    b.speed = speed;
    const auto target = arena_->target_for(b.x, b.y);   // enemyTargetFor(b)
    const double now = arena_->player_.now();
    const double tx = target.x, ty = target.y, tz = graph_->target_floor(target.x, target.y, target.z);
    const double dx = tx - b.x, dy = ty - b.y, d = js_hypot(dx, dy), vertical = std::fabs(tz - b.z);
    const double range = def.ranged ? (def.orb ? 10 : 8) : (def.slam ? 1.8 : 1.18);
    const bool close = d <= range && vertical <= 2.4;
    // Direct-path probe, reused for 0.12 s while neither side moved 0.6 m.
    bool can_direct = false;
    if(!close && d < 35) {
        auto& c = b.direct;
        if(c.valid && now - c.t < .12 && std::fabs(c.bx - b.x) + std::fabs(c.by - b.y) < .6 && std::fabs(c.bz - b.z) < .2 &&
           std::fabs(c.tx - tx) + std::fabs(c.ty - ty) < .6 && std::fabs(c.tz - tz) < .2) can_direct = c.ok;
        else {
            can_direct = graph_->traverse(b.x, b.y, b.z, tx, ty, tz, p);
            c = {true, can_direct, now, b.x, b.y, b.z, tx, ty, tz};
        }
    }
    b.path_cooldown -= dt;
    std::optional<std::array<double, 3>> route_target;
    if(can_direct) { b.path.clear(); ++b.path_serial; b.path_index = 0; b.nav_stuck = 0; route_target = std::array<double, 3>{tx, ty, tz}; }
    else {
        if(!b.path.empty()) {
            auto reached = [&](std::size_t k) { return js_hypot(b.x - b.path[k][0], b.y - b.path[k][1]) < .13 && std::fabs(b.z - b.path[k][2]) < .25; };
            while(b.path_index < b.path.size() - 1 && reached(b.path_index)) ++b.path_index;
            if(b.path_index < b.path.size() && js_hypot(b.path[b.path_index][0] - b.x, b.path[b.path_index][1] - b.y) < .13 && std::fabs(b.path[b.path_index][2] - b.z) < .25) ++b.path_index;
            const bool has = b.path_index < b.path.size();
            if(has) route_target = b.path[b.path_index];
            // Off the verified centre line: go back to the previous node instead of pushing into a wall.
            const auto& v = b.seg_ok;
            const bool fresh = has && v.valid && v.serial == b.path_serial && v.index == b.path_index && now - v.t < .12 &&
                               std::fabs(v.x - b.x) + std::fabs(v.y - b.y) < .6 && std::fabs(v.z - b.z) < .2;
            if(has && b.path_index > 0 && !fresh) {
                if(graph_->traverse(b.x, b.y, b.z, (*route_target)[0], (*route_target)[1], (*route_target)[2], p)) b.seg_ok = {true, b.path_serial, b.path_index, now, b.x, b.y, b.z};
                else {
                    const auto previous = b.path[b.path_index - 1];
                    if(graph_->traverse(b.x, b.y, b.z, previous[0], previous[1], previous[2], p)) { --b.path_index; route_target = previous; }
                }
            }
        }
        const double gx = b.has_goal ? b.goal_x : tx, gy = b.has_goal ? b.goal_y : ty, gz = b.has_goal ? b.goal_z : tz;
        const bool target_moved = js_hypot(tx - gx, ty - gy) > 2.6 || std::fabs(tz - gz) > .9;
        if(!b.nav_queued && decision && search_left > 0 && b.path_cooldown <= 0 && (!route_target || target_moved || b.nav_stuck > .32)) {
            if(!b.nav_queued && queue_.size() <= 80) {   // nav3Request
                b.nav_queued = true;
                queue_.push_back({b.uid, epoch_, def.nav_profile});
                --search_left;
                b.path_cooldown = .65 + arena_->rng_.next() * .4;
                b.has_goal = true; b.goal_x = tx; b.goal_y = ty; b.goal_z = tz;
            }
        }
    }
    double moved = 0;
    const double origin_x = b.x, origin_y = b.y;
    if(route_target && !close) {
        moved = move(b, (*route_target)[0], (*route_target)[1], (*route_target)[2], speed * dt, p);
        if(moved < speed * dt * .22) {
            b.nav_stuck += dt;
            if(b.nav_stuck > .16) {
                b.path_cooldown = -1;
                // nav3Detour: the first free heading left / right of the wanted one
                const double angle = jsmath::atan2((*route_target)[1] - b.y, (*route_target)[0] - b.x), step = std::min(1.2, speed * dt * 2.1);
                for(const double off : {.40, -.40, .8, -.8, 1.25, -1.25, 1.65, -1.65, 2.2, -2.2}) {
                    const double ex = b.x + jsmath::cos(angle + off) * step, ey = b.y + jsmath::sin(angle + off) * step;
                    if(graph_->traverse(b.x, b.y, b.z, ex, ey, b.z, p)) { moved += move(b, ex, ey, b.z, std::max(0.0, speed * dt - moved), p); break; }
                }
            }
        } else b.nav_stuck = std::max(0.0, b.nav_stuck - dt * 2);
    }
    const double travel = js_hypot(b.x - origin_x, b.y - origin_y);
    if(travel > .003) face(b, facing_from(b.x - origin_x, b.y - origin_y), dt);
    else if(close) face(b, facing_from(dx, dy), dt * .75);
    b.walk_amount += ((moved > dt * .07 ? 1.0 : 0.0) - b.walk_amount) * std::min(1.0, dt * 3.6);
    b.anim_phase += moved * def.stride;
    return search_left;
}
void Horde::apply_route(Target& b, const std::vector<std::uint32_t>& chain, double tx, double ty, double tz, const nav::Profile& p) {   // nav3ApplyRoute (6386)
    b.path = graph_->smooth({b.x, b.y, b.z}, chain, {tx, ty, tz}, p);
    ++b.path_serial;
    b.path_index = 0; b.path_cooldown = .7 + arena_->rng_.next() * .4; b.nav_stuck = 0;
    ++nav_stats_.success;
}
std::optional<std::vector<std::uint32_t>> Horde::shared_route(std::uint32_t start, std::uint32_t goal, int prof) const {   // nav3SharedRoute (6413)
    const double now = arena_->player_.now();
    const Shared* best = nullptr;
    double best_t = -kInf;
    std::size_t at = 0;
    for(const auto& e : routes_) {
        if(e.goal != goal || e.profile != prof || e.epoch != epoch_ || now - e.t > .75 || e.t <= best_t) continue;
        // index of `start` on the route: 0 for the route's own start, i + 1 for its i-th node (first occurrence)
        std::optional<std::size_t> i;
        if(e.start == start) i = 0;
        else for(std::size_t k = 0; k < e.nodes.size(); ++k) if(e.nodes[k] == start) { i = k + 1; break; }
        if(!i) continue;
        best = &e; best_t = e.t; at = *i;
    }
    if(!best) return std::nullopt;
    return std::vector<std::uint32_t>(best->nodes.begin() + static_cast<std::ptrdiff_t>(at), best->nodes.end());
}
bool Horde::start_job() {   // nav3StartJob (6428)
    int shared_now = 0;
    while(!queue_.empty()) {
        const Request q = queue_.front();
        queue_.pop_front();
        Target* b = find(q.uid);
        if(q.epoch != epoch_ || !b) { if(b) b->nav_queued = false; continue; }
        const auto target = arena_->target_for(b->x, b->y);   // enemyTargetFor(b)
        const auto& p = profile(q.profile);
        const double tz = graph_->target_floor(target.x, target.y, target.z);
        const auto* start = graph_->nearest(b->x, b->y, b->z, p);
        const auto* goal = graph_->nearest(target.x, target.y, tz, p, true);
        ++nav_stats_.queries;
        if(!start || !goal) { b->nav_queued = false; b->path_cooldown = .65; ++nav_stats_.failed; continue; }
        if(const auto shared = shared_route(start->id, goal->id, q.profile)) {
            b->nav_queued = false;
            apply_route(*b, *shared, target.x, target.y, tz, p);
            ++nav_stats_.shared;
            if(++shared_now >= 3) return false;   // smoothing costs sweeps: at most three shared routes per slice
            continue;
        }
        graph_->search_begin(start->id, goal->id);
        job_ = Job{q.uid, q.epoch, q.profile, start->id, goal->id, 0, target.x, target.y, tz};
        return true;
    }
    return false;
}
void Horde::finish_job(bool success) {   // nav3Finish (6367)
    const Job job = *job_;
    Target* b = find(job.uid);
    if(b) b->nav_queued = false;
    if(job.epoch == epoch_ && b) {
        std::optional<std::vector<std::uint32_t>> chain;
        if(success) chain = graph_->search_chain(job.start, job.goal);
        if(chain) {
            // nav3StoreRoute: remembered for 0.75 s for other enemies heading to the same goal node
            Shared e{job.goal, job.profile, epoch_, arena_->player_.now(), job.start, *chain};
            if(routes_.size() < 24) routes_.push_back(std::move(e));
            else { routes_[route_slot_] = std::move(e); route_slot_ = (route_slot_ + 1) % 24; }
            apply_route(*b, *chain, job.tx, job.ty, job.tz, profile(job.profile));
        } else { b->path_cooldown = .45; ++nav_stats_.failed; }
    }
    job_.reset();
}
void Horde::process_jobs() {   // processNavJobs (6450), expansion budget only
    if(!job_ && queue_.empty()) return;
    int budget = kNavWorkPerSlice;
    while(budget-- > 0) {
        if(!job_ && !start_job()) break;
        auto& job = *job_;
        if(job.epoch != epoch_ || ((job.expanded & 15U) == 0 && !find(job.uid)) || graph_->search_open_empty() || job.expanded > nav::kMaxSearch) { finish_job(false); continue; }
        const auto result = graph_->search_pop(job.goal, profile(job.profile));
        if(result == nav::NavGraph::Pop::closed) continue;
        ++job.expanded;
        if(result == nav::NavGraph::Pop::goal) finish_job(true);
    }
}

// ---- flyers -----------------------------------------------------------------------------------------
bool Horde::flyer_free(double x, double y, double z, double r, double h) const {   // nav3FlyerFree (6582)
    const auto& w = *arena_->world_;
    if(!w.walkable(x, y) || !std::isfinite(x + y + z) || z < -4.05 || z > 22.5) return false;
    if(w.wall_hit(x, y, z, h, r) || w.feature_solid_at(x, y, z, z + h, r)) return false;
    if(w.hits_slab(x, y, r, z + .02, z + h - .02)) return false;
    const auto roof = w.overhead(x, y);
    if(roof && z + h > *roof - .24) return false;
    return true;
}
bool Horde::flyer_sweep(double x, double y, double z, double nx, double ny, double nz, double r) const {   // nav3FlyerSweep
    const double dist = js_hypot3(nx - x, ny - y, nz - z);
    const int N = std::max(1, static_cast<int>(std::ceil(dist / .34)));
    for(int i = 1; i <= N; ++i) {
        const double t = static_cast<double>(i) / N;
        if(!flyer_free(x + (nx - x) * t, y + (ny - y) * t, z + (nz - z) * t, r)) return false;
    }
    return true;
}
void Horde::update_flyer(Target& b, double dt) {   // updateFlyer (6597)
    const auto target = arena_->target_for(b.x, b.y);   // enemyTargetFor(b)
    const double old_x = b.x, old_y = b.y;
    const double r = b.type == 19 ? .52 : b.type == 18 ? .37 : .29;
    const double target_z = target.z + 2.45;
    b.fly_phase += dt * (b.type == 18 ? 9 : 6);
    const std::array<double, 3> desired{target.x, target.y, std::max(-2.0, std::min(19.0, target_z + jsmath::sin(b.fly_phase) * .16))};
    double dx = desired[0] - b.x, dy = desired[1] - b.y, dz = desired[2] - b.z;
    const double distance = js_hypot3(dx, dy, dz);
    if(distance < .7) { b.walk_amount = .2; return; }
    const double step = std::min(b.speed * dt, distance - .45);
    dx /= distance; dy /= distance; dz /= distance;
    const double yaw = jsmath::atan2(dy, dx), flat = js_hypot(dx, dy);
    // Steering is re-decided at 30 Hz (staggered per flyer); in between the heading is kept, and every step is swept.
    if(!b.fly_decide_set) { b.fly_decide = static_cast<double>(b.uid % 4) / 120; b.fly_decide_set = true; }
    b.fly_decide -= dt;
    auto finish = [&] {
        const double moved = js_hypot(b.x - old_x, b.y - old_y);
        if(moved > .005) face(b, facing_from(b.x - old_x, b.y - old_y), dt);
        b.walk_amount = moved > .003 ? 1 : .2;
        b.anim_phase += dt * 4.5;
    };
    if(b.fly_decide > 0 && b.has_fly_dir) {
        const auto& v = b.fly_dir;
        const double nx = b.x + v[0] * step, ny = b.y + v[1] * step, nz = b.z + v[2] * step;
        if(v[0] * dx + v[1] * dy + v[2] * dz > .2 && flyer_sweep(b.x, b.y, b.z, nx, ny, nz, r)) {
            b.x = nx; b.y = ny; b.z = nz; b.fly_stall = std::max(0.0, b.fly_stall - dt * 2);
            finish();
            return;
        }
    }
    b.fly_decide = 1.0 / 30;
    std::vector<std::array<double, 3>> candidates{{dx, dy, dz}, {dx, dy, 0}, {dx, dy, .52}, {dx, dy, -.52}, {0, 0, 1}, {0, 0, -1}};
    const double steering = b.fly_steer_side;
    for(const double a : {0.45, .9, 1.35, 1.85, 2.35}) for(const double sign : {steering, -steering}) {
        const double rad = yaw + sign * a;
        for(const double lift : {dz, .65, -.65, 0.0}) candidates.push_back({jsmath::cos(rad) * std::max(.25, flat), jsmath::sin(rad) * std::max(.25, flat), lift});
    }
    bool found = false;
    double score = -kInf;
    std::array<double, 3> best{}, best_dir{};
    for(const auto& u : candidates) {
        const double norm = js_hypot3(u[0], u[1], u[2]), n = norm != 0 ? norm : 1;
        const double vx = u[0] / n, vy = u[1] / n, vz = u[2] / n;
        const double nx = b.x + vx * step, ny = b.y + vy * step, nz = b.z + vz * step;
        if(!flyer_sweep(b.x, b.y, b.z, nx, ny, nz, r)) continue;
        const double new_distance = js_hypot3(desired[0] - nx, desired[1] - ny, desired[2] - nz);
        const double dot = vx * dx + vy * dy + vz * dz;
        int clearance = 0;
        for(const double ang : {0.0, 3.141592653589793 * .5, 3.141592653589793, 3.141592653589793 * 1.5}) if(!flyer_free(nx + jsmath::cos(ang) * .55, ny + jsmath::sin(ang) * .55, nz, r)) ++clearance;
        const double s = (distance - new_distance) * 8 + dot * 1.8 - clearance * .2 + (std::fabs(vz) < .8 ? .10 : 0);
        if(s > score) { score = s; best = {nx, ny, nz}; best_dir = {vx, vy, vz}; found = true; }
    }
    b.has_fly_dir = found; b.fly_dir = best_dir;
    if(found) { b.x = best[0]; b.y = best[1]; b.z = best[2]; b.fly_stall = std::max(0.0, b.fly_stall - dt * 2); }
    else {
        b.fly_stall += dt;
        if(b.fly_stall > .35) { b.fly_steer_side = -b.fly_steer_side; b.fly_stall = 0; }
    }
    finish();
}
}  // namespace cat::combat
