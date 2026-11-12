// N5 combat rules: see cat/combat.hpp. Statement and floating-point order follow the legacy
// functions named in the comments so that recorded reference tapes replay exactly.
#include "cat/combat.hpp"
#include "cat/js_math.hpp"
#include "cat/enemies.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cat::combat {
namespace {
using physics::js_hypot;
using physics::js_hypot3;
constexpr double kInf = std::numeric_limits<double>::infinity();
double clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
constexpr std::array<float, 3> rgb(unsigned hex) { return {float((hex >> 16) & 255) / 255.0F, float((hex >> 8) & 255) / 255.0F, float(hex & 255) / 255.0F}; }
// WEAPONS (5140-5156), in the legacy order; the index is the weapon id used everywhere.
constexpr std::array<WeaponDef, kWeaponCount> kWeapons{{
    {"NAILGUN", 13, .09, 50, 1.35, 1, .022, 27, FireKind::hitscan, false, false, false, rgb(0xa6c6ff)},
    {"SHOTGUN", 18, .72, 8, 1.8, 8, .14, 13, FireKind::hitscan, true, false, false, rgb(0xffc27e)},
    {"GRENADE LAUNCHER", 108, .78, 6, 2.0, 1, .01, 24, FireKind::grenade, false, false, false, rgb(0xd0ef84)},
    {"RAILGUN", 132, .98, 7, 2.1, 1, 0, 100, FireKind::hitscan, false, true, false, rgb(0xf0b2ff)},
    {"ROCKET LAUNCHER", 95, .86, 6, 2.1, 1, .006, 33, FireKind::rocket, false, false, false, rgb(0xfba17a)},
    {"SUPER NAILGUN", 20, .13, 36, 1.65, 1, .030, 31, FireKind::hitscan, false, false, false, rgb(0x8df4ab)},
    {"THUNDERBOLT", 15, .075, 75, 1.7, 1, 0, 20, FireKind::hitscan, false, false, true, rgb(0x7deaf4)},
    {"SUPER SHOTGUN", 16, .88, 12, 2.0, 14, .19, 14, FireKind::hitscan, true, false, false, rgb(0xffd29a)},
    {"PLASMA REPEATER", 34, .19, 48, 1.7, 1, .007, 50, FireKind::hitscan, false, false, false, rgb(0x8cf7b7)},   // hitscan despite its label
    {"TRIPLE SPIKE", 13, .34, 36, 1.55, 3, .045, 36, FireKind::hitscan, false, false, false, rgb(0xf1a56d)},
    {"RIFT CANNON", 125, 1.15, 5, 2.4, 1, 0, 36, FireKind::rift, false, false, false, rgb(0xc1a3ff)},
    {"IRON CHAINGUN", 10, .065, 100, 2.1, 1, .06, 70, FireKind::hitscan, false, false, false, rgb(0xd0bc91)},
    {"HAND CANNON", 78, .82, 7, 2.0, 1, .008, 95, FireKind::hitscan, false, false, false, rgb(0xf4bd77)},
    {"HELLFIRE CARBINE", 37, .28, 28, 1.7, 1, .018, 42, FireKind::hitscan, false, false, false, rgb(0xf98e4c)},  // hitscan despite its label
    {"SAWBLADE DRIVER", 39, .38, 22, 1.9, 1, .02, 54, FireKind::hitscan, false, false, false, rgb(0xb8cfd1)},
    {"QUAD SHOTGUN", 14, 1.15, 8, 2.5, 20, .225, 17, FireKind::hitscan, true, false, false, rgb(0xe7bb81)},
}};
struct Scale { double w, h; };
Scale hit_scale(TargetClass c) { return c == TargetClass::heavy ? Scale{1.22, 1.10} : c == TargetClass::fast ? Scale{.86, .90} : Scale{1, 1}; }   // enemyHitScale
}  // namespace

const std::array<WeaponDef, kWeaponCount>& weapons() { return kWeapons; }

double Rng::next() {
    if(has_constant_) return constant_;
    ++calls_;
    state_ += 0x6D2B79F5U;
    std::uint32_t t = (state_ ^ (state_ >> 15)) * (1U | state_);
    t = (t + (t ^ (t >> 7)) * (61U | t)) ^ t;
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}

std::array<Box, 2> Target::boxes() const {   // enemyHitBoxes (11530)
    const auto s = hit_scale(cls);
    return {{{x - .39 * s.w, x + .39 * s.w, y - .34 * s.w, y + .34 * s.w, z + .02, z + 1.24 * s.h},
             {x - .62 * s.w, x + .62 * s.w, y - .48 * s.w, y + .49 * s.w, z + 1.06 * s.h, z + 2.18 * s.h}}};
}
double Target::height() const { return 2.18 * hit_scale(cls).h; }

double point_box_distance(double x, double y, double z, const Box& b) {   // pointBoxDistance (11567)
    return js_hypot3(std::max({b.x0 - x, 0.0, x - b.x1}), std::max({b.y0 - y, 0.0, y - b.y1}), std::max({b.z0 - z, 0.0, z - b.z1}));
}
double segment_box_time(double x, double y, double z, double nx, double ny, double nz, const Box& b, double r) {   // segmentBoxTime (11572)
    double lo = 0, hi = 1;
    auto axis = [&](double p, double q, double min, double max) {
        const double d = q - p;
        min -= r; max += r;
        if(std::fabs(d) < 1e-9) return p >= min && p <= max;
        double t0 = (min - p) / d, t1 = (max - p) / d;
        if(t0 > t1) std::swap(t0, t1);
        lo = std::max(lo, t0); hi = std::min(hi, t1);
        return lo <= hi;
    };
    if(!axis(x, nx, b.x0, b.x1) || !axis(y, ny, b.y0, b.y1) || !axis(z, nz, b.z0, b.z1)) return -1;
    return (hi >= 0 && lo <= 1) ? std::max(0.0, lo) : -1;
}

Arena::Arena(const physics::CollisionWorld& world, std::uint32_t seed) : world_(&world), player_(world), rng_(seed) {
    for(const auto& p : world.pickups()) pickups_.push_back({p.x, p.y, p.z, 0});
}
void Arena::spawn(double x, double y, double z, double yaw, double pitch, double now) {
    player_.spawn(x, y, z, yaw, pitch, now);
    rockets_.clear(); grenades_.clear(); bolts_.clear();
    for(auto& p : pickups_) p.cool = 0;
    events_ = Events{};
    hp_ = kMaxHealth; cooldown_ = 0; reloading_ = 0; gun_kick_ = 0; muzzle_flash_ = 0; flash_ = 0; respawn_grace_ = 0;
    fire_held_ = false; dead_ = false; respawn_queued_ = false; keys_ = 0; slot_ = 0; frags_ = 0; deaths_ = 0; burn_remaining_ = 0; burn_tick_ = 0;
}
void Arena::set_loadout(int a, int b) {
    if(a < 0 || b < 0 || a >= static_cast<int>(kWeaponCount) || b >= static_cast<int>(kWeaponCount)) throw std::runtime_error("weapon index out of range");
    loadout_ = {a, b}; slot_ = 0;
    mag_ = {kWeapons[static_cast<std::size_t>(a)].clip, kWeapons[static_cast<std::size_t>(b)].clip};
    reloading_ = 0;
}
void Arena::roll_loadout() {   // rollLoadout (11592)
    const int a = static_cast<int>(std::floor(rng_.next() * static_cast<double>(kWeaponCount)));
    int b = static_cast<int>(std::floor(rng_.next() * static_cast<double>(kWeaponCount - 1)));
    if(b >= a) ++b;
    set_loadout(a, b);
    cooldown_ = player_.now() + .2;
}
std::uint32_t Arena::add_target(double x, double y, double z, TargetClass cls, double hp) {
    Target t;
    t.x = x; t.y = y; t.z = z; t.cls = cls; t.hp = hp; t.max_hp = hp; t.id = next_target_id_++;
    targets_.push_back(t);
    return t.id;
}
std::vector<Projectile> Arena::projectiles() const {
    std::vector<Projectile> all = rockets_;
    all.insert(all.end(), grenades_.begin(), grenades_.end());
    all.insert(all.end(), bolts_.begin(), bolts_.end());
    return all;
}

void Arena::step(const physics::TickInput& in) {
    events_ = Events{};
    event_seq_ = 0;
    if(horde_) horde_->begin_step();
    // Input events between ticks: keyboard in key-bit order (movement, then R, then Q), then the fire button.
    const std::uint32_t changed = in.keys ^ keys_;
    for(std::uint32_t bit = 1; bit <= physics::key_slide; bit <<= 1)
        if(changed & bit) player_.key_event(static_cast<physics::Key>(bit), (in.keys & bit) != 0);
    if((changed & key_reload) && (in.keys & key_reload)) reload();
    if((changed & key_switch) && (in.keys & key_switch)) switch_weapon();
    if(changed & key_fire) {   // mousedown: fireHeld = true; fireWeapon()   mouseup: fireHeld = false
        if(in.keys & key_fire) { fire_held_ = true; fire(); } else fire_held_ = false;
    }
    keys_ = in.keys;
    if((in.keys & key_fixture_kill) && !targets_.empty()) damage_target(0, 9999, targets_[0].z + 1, 0);
    if(in.keys & key_fixture_hurt) take_damage(35);
    if(in.look_dx != 0 || in.look_dy != 0) player_.aim(in.look_dx, in.look_dy);
    // update(dt) (12214): timers, movement, fire, reload timer, projectiles, pickups.
    const double dt = physics::kTick;
    respawn_grace_ = std::max(0.0, respawn_grace_ - dt);
    const bool ai_decision = horde_ ? horde_->decision_tick(dt) : false;   // aiDecisionAcc: 30 Hz decisions inside the 120 Hz tick
    flash_ = std::max(0.0, flash_ - dt * 2);
    gun_kick_ = std::max(0.0, gun_kick_ - dt * 6);
    muzzle_flash_ = std::max(0.0, muzzle_flash_ - dt);
    player_.tick(dt);
    if(burn_remaining_ > 0) {   // fireball burn: 2 damage every 0.5 s for 3 s
        burn_remaining_ = std::max(0.0, burn_remaining_ - dt);
        burn_tick_ -= dt;
        if(burn_tick_ <= 0) { burn_tick_ += .5; take_damage(2, true); }
    }
    if(fire_held_) fire();
    if(reloading_ > 0) {
        reloading_ -= dt;
        if(reloading_ <= 0) { reloading_ = 0; mag_[static_cast<std::size_t>(slot_)] = kWeapons[static_cast<std::size_t>(current_weapon())].clip; events_.reload_done = true; }
    }
    if(horde_) horde_->update(dt, ai_decision);   // enemies, route jobs, enemy projectiles
    update_projectiles(dt);
    update_pickups(dt);
    if(horde_) horde_->update_corpses(dt);
    else for(auto& t : targets_) t.hit_flash = std::max(0.0, t.hit_flash - dt);
    if(presentation_hook_) presentation_hook_();   // WFX.update, EFX.update (12347)
    finish_respawn();
    if(horde_) horde_->tick_waves(dt);
}

Arena::FirePose Arena::fire_position(double az, double pitch) const {   // firePosition (11811)
    const double cp = jsmath::cos(pitch), dx = jsmath::cos(az) * cp, dy = jsmath::sin(az) * cp, dz = -jsmath::sin(pitch);
    const auto cam = player_.shot_origin();
    return {cam.x + dx * .24, cam.y + dy * .24, cam.z + dz * .24, dx, dy, dz};
}

ShotHit Arena::shot_sweep(double x, double y, double z, double nx, double ny, double nz, double r) const {   // shotSweep (11829)
    const double dx = nx - x, dy = ny - y, dz = nz - z;
    double first = world_->shot_collision_t(x, y, z, nx, ny, nz, r);
    int enemy = -1;
    bool head = false;
    for(std::size_t i = 0; i < targets_.size(); ++i) {
        const auto& b = targets_[i];
        if(std::max(std::fabs(b.x - (x + nx) * .5), std::fabs(b.y - (y + ny) * .5)) > js_hypot(dx, dy) * .5 + 1.1) continue;   // broad phase
        const auto boxes = b.boxes();
        for(std::size_t k = 0; k < 2; ++k) {
            const double t = segment_box_time(x, y, z, nx, ny, nz, boxes[k], r);
            if(t >= 0 && t < first) { first = t; enemy = static_cast<int>(i); head = k == 1; }
        }
    }
    if(first != kInf) return {true, first, enemy, head, x + dx * first, y + dy * first, z + dz * first};
    return {false, 1, -1, false, nx, ny, nz};
}

void Arena::reroll_one(bool) {   // rerollOne (11593): a frag replaces the weapon in hand with a different random one
    const int old = current_weapon();
    int next = static_cast<int>(std::floor(rng_.next() * static_cast<double>(kWeaponCount - 1)));
    if(next >= old) ++next;
    loadout_[static_cast<std::size_t>(slot_)] = next;
    mag_[static_cast<std::size_t>(slot_)] = kWeapons[static_cast<std::size_t>(next)].clip;
    reloading_ = 0; cooldown_ = player_.now() + .12;
    events_.reroll = next;
}
void Arena::damage_target(std::size_t index, double damage, double z_hit, int part) {   // damageBot (11585) + killBot
    if(index >= targets_.size()) return;
    auto& b = targets_[index];
    const double rel = z_hit - b.z;
    const bool head = part >= 0 ? part == 1 : (rel >= b.height() * .66 && rel <= b.height() + .10);
    const double actual = damage * (head ? 2 : 1);
    b.hp -= actual; b.hit_flash = .21;
    DamageEvent e{b.id, actual, head, b.hp <= 0};
    events_.damage.push_back(e);
    if(b.hp <= 0) {   // killBot: remove, score, death presentation, reroll, wave count
        const Target dead = b;
        targets_.erase(targets_.begin() + static_cast<std::ptrdiff_t>(index));
        ++frags_;
        if(horde_) horde_->enemy_died(dead);
        reroll_one(head);
        if(horde_) horde_->count_kill();
    } else {
        (void)rng_.next();   // the legacy rolls Math.random() < .52 for a hurt voice here; the draw is kept so the stream stays aligned
    }
}

void Arena::shotgun_recoil(int index) {   // applyShotgunRecoil (11992)
    auto& p = player_.state();
    const double force = index == 15 ? 6.7 : index == 7 ? 5.7 : 4.7;
    const double bx = -jsmath::cos(p.a), by = -jsmath::sin(p.a);
    if(p.ground) { p.vx += bx * force; p.vy += by * force; }
    else {
        p.vx += bx * force * .55; p.vy += by * force * .55;
        p.vz = std::max(p.vz, index == 15 ? 8.2 : index == 7 ? 7.8 : 7.3);   // mid-air double jump
        p.ground = false;
    }
}

void Arena::switch_weapon() { slot_ = 1 - slot_; reloading_ = 0; cooldown_ = player_.now() + .15; events_.switched = true; }   // switchWeapon
void Arena::reload() {   // reloadWeapon
    const auto& d = kWeapons[static_cast<std::size_t>(current_weapon())];
    if(reloading_ > 0 || mag_[static_cast<std::size_t>(slot_)] >= d.clip) return;
    reloading_ = d.reload; events_.reload_started = true;
}

void Arena::fire() {   // fireWeapon (12054)
    if(player_.state().downed) return;   // a fragged solo player can still fire until the respawn at the end of the tick
    const int index = current_weapon();
    const WeaponDef d = kWeapons[static_cast<std::size_t>(index)];   // a kill may reroll the slot mid-volley; the volley keeps this weapon
    const double now = player_.now();
    if(now < cooldown_ || reloading_ > 0) return;
    const auto slot = static_cast<std::size_t>(slot_);
    if(mag_[slot] <= 0) { reload(); return; }
    --mag_[slot]; cooldown_ = now + d.delay; gun_kick_ = 1;
    const bool blast = d.kind != FireKind::hitscan;
    muzzle_flash_ = blast ? .16 : d.pellets > 1 ? .145 : (d.rail || d.lightning) ? .075 : .105;
    events_.fired_weapon = index;
    const auto& p = player_.state();
    const auto origin = fire_position(p.a, p.pitch);
    if(d.kind == FireKind::rocket) {
        rockets_.push_back({FireKind::rocket, origin.x, origin.y, origin.z, origin.dx * kRocketSpeed, origin.dy * kRocketSpeed, origin.dz * kRocketSpeed, kRocketLife, next_projectile_id_++});
    } else if(d.kind == FireKind::grenade) {
        grenades_.push_back({FireKind::grenade, origin.x, origin.y, origin.z, origin.dx * kGrenadeSpeed, origin.dy * kGrenadeSpeed, origin.dz * kGrenadeSpeed + kGrenadeLift, kGrenadeLife, next_projectile_id_++});
    } else if(d.kind == FireKind::rift) {
        bolts_.push_back({FireKind::rift, origin.x, origin.y, origin.z, origin.dx * kRiftSpeed, origin.dy * kRiftSpeed, origin.dz * kRiftSpeed, kRiftLife, next_projectile_id_++});
    } else {
        // Every non-explosive weapon resolves its hits on the firing tick; tracers are presentation only.
        if(d.shotgun) shotgun_recoil(index);
        for(int n = 0; n < d.pellets; ++n) {
            const double az = player_.state().a + (rng_.next() - .5) * d.spread * 2;
            const double pitch = clamp(player_.state().pitch + (rng_.next() - .5) * d.spread * 1.4, -1.49, 1.49);
            const auto q = fire_position(az, pitch);
            const auto hit = shot_sweep(q.x, q.y, q.z, q.x + q.dx * d.range, q.y + q.dy * d.range, q.z + q.dz * d.range, d.pellets > 1 ? .025 : .042);
            if(hit.target >= 0) damage_target(static_cast<std::size_t>(hit.target), d.damage, hit.z, hit.head ? 1 : 0);
            events_.shots.push_back({hit.x, hit.y, hit.z, hit.hit, hit.target >= 0, hit.head});
        }
    }
    // Presentation context, taken where the legacy calls WFX.fire: after the volley (shotgun recoil already applied).
    events_.fire.view = player_.view_camera(); events_.fire.eye = player_.shot_origin(); events_.fire.fov_scale = player_.fov_scale(); events_.fire.now = now;
    events_.fire.weapon_in_hand = current_weapon();
    events_.fire_seq = next_event_seq();   // after the deaths of this volley, as WFX.fire follows damageBot
    if(mag_[slot] <= 0) reload();
}

bool Arena::take_damage(double amount, bool burn) {   // takeDamage (11912)
    if(player_.state().downed) return false;
    amount = std::max(0.0, amount);
    if(amount == 0 || !(amount == amount) || respawn_grace_ > 0) return false;
    hp_ -= amount; flash_ = std::max(flash_, burn ? .22 : .62);
    events_.player_damage += amount;
    if(hp_ <= 0) {   // committed at the end of the tick (finishPendingRespawn), not inside the enemy / projectile loops
        ++deaths_; hp_ = 0; respawn_queued_ = true; respawn_grace_ = 2.5;
        burn_remaining_ = 0; burn_tick_ = 0;
        if(horde_) horde_->purge_shots_after_loop();
        return true;
    }
    return false;
}
Box Arena::player_box() const {   // playerHitBox (11539): the nominal crouch height, not the collider height
    const auto& p = player_.state();
    const double duck = clamp(p.crouch_offset / .49, 0, 1);
    const double height = physics::kPlayerHeight + (physics::kPlayerCrouchHeight - physics::kPlayerHeight) * duck;
    return {p.x - physics::kPlayerRadius, p.x + physics::kPlayerRadius, p.y - physics::kPlayerRadius, p.y + physics::kPlayerRadius, p.z + .035, p.z + height};
}

void Arena::explode(double x, double y, double power, double z, FireKind kind) {   // explode (12015)
    events_.blasts.push_back({x, y, z, power, kind, next_event_seq()});
    // Blast damage comes from the detonation point; a projectile impact deals no separate direct-hit damage.
    const double splash = kEnemySplashRadius * power;
    for(std::size_t i = targets_.size(); i-- > 0;) {
        const auto boxes = targets_[i].boxes();
        const double distance = std::min(point_box_distance(x, y, z, boxes[0]), point_box_distance(x, y, z, boxes[1]));
        if(distance < splash) damage_target(i, kEnemySplashDamage * power * (1 - distance / splash), -10, 0);
    }
    const double range = kPlayerSplashRadius * power;
    auto& p = player_.state();
    const double d = point_box_distance(x, y, z, player_box());
    if(d < range) {
        const bool died = take_damage(std::max(1.0, std::floor(kPlayerSplashDamage * power * (1 - d / range) + .5)));   // Math.round
        if(!died && hp_ > 0) {
            const double pct = clamp((range - d) / (range * .58), 0, 1);
            const double launch = std::sqrt(2 * physics::kGravity * physics::kPlayerHeight * 2.25) * pct;   // 2.25 player heights at full blast
            p.vz = std::max(p.vz, launch); p.ground = false;
            double ax = p.x - x, ay = p.y - y, len = js_hypot(ax, ay);
            if(len < .025) { ax = -jsmath::cos(p.a); ay = -jsmath::sin(p.a); len = 1; }
            const double horiz = kBlastHorizontal * pct;
            p.vx += ax / len * horiz; p.vy += ay / len * horiz;
        }
    }
}

void Arena::update_projectiles(double dt) {   // update() 12308-12343
    for(std::size_t i = rockets_.size(); i-- > 0;) {
        auto& p = rockets_[i];
        p.life -= dt;
        const auto hit = shot_sweep(p.x, p.y, p.z, p.x + p.vx * dt, p.y + p.vy * dt, p.z + p.vz * dt, kRocketRadius);
        if(hit.hit || p.life <= 0) { rockets_.erase(rockets_.begin() + static_cast<std::ptrdiff_t>(i)); explode(hit.x, hit.y, kRocketPower, hit.z, FireKind::rocket); }
        else { p.x = hit.x; p.y = hit.y; p.z = hit.z; }
    }
    for(std::size_t i = grenades_.size(); i-- > 0;) {
        auto& p = grenades_[i];
        p.life -= dt; p.vz -= kGrenadeGravity * dt;
        const auto hit = shot_sweep(p.x, p.y, p.z, p.x + p.vx * dt, p.y + p.vy * dt, p.z + p.vz * dt, kGrenadeRadius);
        if(hit.hit || p.life <= 0) { grenades_.erase(grenades_.begin() + static_cast<std::ptrdiff_t>(i)); explode(hit.x, hit.y, kGrenadePower, hit.z, FireKind::grenade); }
        else { p.x = hit.x; p.y = hit.y; p.z = hit.z; }
    }
    for(std::size_t i = bolts_.size(); i-- > 0;) {
        auto& p = bolts_[i];
        p.life -= dt;
        const auto hit = shot_sweep(p.x, p.y, p.z, p.x + p.vx * dt, p.y + p.vy * dt, p.z + p.vz * dt, kRiftRadius);
        if(hit.hit || p.life <= 0) { bolts_.erase(bolts_.begin() + static_cast<std::ptrdiff_t>(i)); explode(hit.x, hit.y, kRiftPower, hit.z, FireKind::rift); }
        else { p.x = hit.x; p.y = hit.y; p.z = hit.z; }
    }
}

void Arena::update_pickups(double dt) {   // update() 12344: medkits
    const auto& p = player_.state();
    for(auto& k : pickups_) {
        if(k.cool > 0) k.cool -= dt;
        else if(js_hypot(p.x - k.x, p.y - k.y) < kMedkitRadius && std::fabs(p.z - k.z) < 1.2 && !p.downed && hp_ < kMaxHealth) {
            const double before = hp_;
            hp_ = std::min(kMaxHealth, hp_ + kMedkitHeal); k.cool = kMedkitCooldown;
            events_.healed += hp_ - before;
        }
    }
}

void Arena::finish_respawn() {   // finishPendingRespawn (12097), single-player branch
    if(!respawn_queued_) return;
    respawn_queued_ = false; dead_ = false;
    // makeRandomSpawn() = worldRandomSpawn(0, 'player') (9726-9736): clear spawn points, each scored by
    // its distance to the nearest enemy (capped at 40) plus a random 0..14; one of the best three.
    std::vector<const physics::Spawn*> pool;
    for(const auto& sp : world_->spawns()) if(world_->capsule_clear(sp.x, sp.y, sp.z, physics::kPlayerHeight, physics::kPlayerRadius)) pool.push_back(&sp);
    if(!pool.empty()) {
        struct Scored { const physics::Spawn* spawn; double v; };
        std::vector<Scored> list;
        for(const auto* sp : pool) {
            double near = kInf;
            for(const auto& b : targets_) near = std::min(near, js_hypot(b.x - sp->x, b.y - sp->y) + std::fabs(b.z - sp->z));
            const double base = std::isfinite(near) ? std::min(near, 40.0) : 40.0;
            list.push_back({sp, base + rng_.next() * 14});
        }
        std::stable_sort(list.begin(), list.end(), [](const Scored& a, const Scored& b) { return a.v > b.v; });
        const auto pick = static_cast<std::size_t>(std::floor(rng_.next() * static_cast<double>(std::min<std::size_t>(3, list.size()))));
        const auto& sp = *list[pick].spawn;
        player_.respawn(sp.x, sp.y, sp.z, sp.a);
    }
    hp_ = kMaxHealth; rockets_.clear(); grenades_.clear(); bolts_.clear();
    respawn_grace_ = 2.5; gun_kick_ = 0; fire_held_ = false;   // held keys stay held; the fire button needs a new press
    if(horde_) horde_->player_respawned();   // enemy shots removed, wave restarts softly
    roll_loadout();
}
}  // namespace cat::combat
