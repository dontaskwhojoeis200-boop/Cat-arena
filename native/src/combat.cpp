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
    remote_projectiles_.clear(); remote_blasts_.clear(); guest_hit_.reset();
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
    all.insert(all.end(), remote_projectiles_.begin(), remote_projectiles_.end());
    return all;
}

void Arena::step(const physics::TickInput& in) {
    events_ = Events{};
    event_seq_ = 0;
    player_.clear_body_events();
    player_.set_stick(in.stick_x, in.stick_y);
    if(horde_) horde_->begin_step();
    if(guest_hit_) {
        events_.incoming_hit = guest_hit_;
        events_.player_damage += guest_hit_->amount;
        ++events_.player_hits;
        flash_ = std::max(flash_, .62);
        guest_hit_.reset();
    }
    for(auto& blast : remote_blasts_) {
        blast.seq = next_event_seq();
        events_.blasts.push_back(blast);
    }
    remote_blasts_.clear();
    // N8: shots of other players that arrived since the last tick (netHostRemoteFire runs on message arrival)
    if(!remote_fire_queue_.empty()) {
        const auto queue = std::move(remote_fire_queue_);
        remote_fire_queue_.clear();
        for(const auto& r : queue) if(Teammate* m = teammate(r.key)) remote_fire(*m, r.weapon);
    }
    if(!frag_queue_.empty()) { const auto frags = std::move(frag_queue_); frag_queue_.clear(); for(const bool head : frags) remote_frag(head); }
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
    for(auto& p : remote_projectiles_) {
        if(p.kind == FireKind::grenade) p.vz -= kGrenadeGravity * dt;
        p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
        p.life -= dt;
    }
    std::erase_if(remote_projectiles_, [](const Projectile& p) { return p.life <= 0; });
    update_pickups(dt);
    if(horde_) horde_->update_corpses(dt);
    else for(auto& t : targets_) t.hit_flash = std::max(0.0, t.hit_flash - dt);
    if(presentation_hook_) presentation_hook_();   // WFX.update, EFX.update (12347)
    finish_respawn();
    if(role_ == NetRole::host) coop_tick(dt);   // netHostTick (12347)
    if(horde_ && role_ != NetRole::guest) horde_->tick_waves(dt);
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
    if(role_ == NetRole::guest) {
        b.hit_flash = .17;
        events_.damage.push_back({b.id, actual, head, false});
        return;
    }
    b.hp -= actual; b.hit_flash = .21;
    DamageEvent e{b.id, actual, head, b.hp <= 0};
    if(attacker_ == 0) events_.damage.push_back(e);
    if(b.hp <= 0) {   // killBot: remove, score, death presentation, reroll, wave count
        events_.voices.push_back({b.x, b.y, b.z, b.type, VoiceKind::eliminated, b.id});   // playEnemyVoice(b, 'eliminated') (11589)
        const Target dead = b;
        targets_.erase(targets_.begin() + static_cast<std::ptrdiff_t>(index));
        if(horde_) horde_->enemy_died(dead);
        if(attacker_ != 0) {
            if(teammate(attacker_)) events_.remote_frags.push_back({attacker_, head});
        } else {
            ++frags_;
            reroll_one(head);
        }
        if(horde_) horde_->count_kill();
    } else {
        if(rng_.next() < .52) events_.voices.push_back({b.x, b.y, b.z, b.type, VoiceKind::hurt, b.id});   // playEnemyVoice(b, 'hurt') (11590)
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
        player_.note_body(physics::BodySound::jump, 1);   // playBodySound('jump') (12003)
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

bool Arena::take_damage(double amount, bool burn, std::optional<std::array<double, 2>> source) {   // takeDamage (11912)
    if(role_ == NetRole::guest || player_.state().downed) return false;
    amount = std::max(0.0, amount);
    if(amount == 0 || !(amount == amount) || respawn_grace_ > 0) return false;
    hp_ -= amount; flash_ = std::max(flash_, burn ? .22 : .62);
    events_.player_damage += amount;
    events_.incoming_hit = IncomingHit{amount, burn, source};
    if(!burn) ++events_.player_hits;   // beep(100, .08, 'sawtooth', .013) (11923)
    if(hp_ <= 0) {   // committed at the end of the tick (finishPendingRespawn), not inside the enemy / projectile loops
        ++deaths_; hp_ = 0;
        if(role_ == NetRole::host && !team_.empty()) {   // co-op: down until a teammate revives (or the squad is wiped)
            auto& p = player_.state();
            p.downed = true; p.vx = p.vy = p.vz = 0;
            fire_held_ = false;
            events_.downed.push_back(0);
        } else { respawn_queued_ = true; respawn_grace_ = 2.5; }
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
    events_.blasts.push_back({x, y, z, power, kind, next_event_seq(), attacker_});
    // Blast damage comes from the detonation point; a projectile impact deals no separate direct-hit damage.
    const double splash = kEnemySplashRadius * power;
    for(std::size_t i = targets_.size(); i-- > 0;) {
        const auto boxes = targets_[i].boxes();
        const double distance = std::min(point_box_distance(x, y, z, boxes[0]), point_box_distance(x, y, z, boxes[1]));
        if(distance < splash) damage_target(i, kEnemySplashDamage * power * (1 - distance / splash), -10, 0);
    }
    const double range = kPlayerSplashRadius * power;
    if(role_ == NetRole::host) for(auto& other : team_) {   // other players (host): damage only, they move themselves
        if(other.downed) continue;
        const double rd = point_box_distance(x, y, z, teammate_box(other));
        if(rd < range) remote_damage(other, std::max(1.0, std::floor(kPlayerSplashDamage * power * (1 - rd / range) + .5)));
    }
    auto& p = player_.state();
    const double d = point_box_distance(x, y, z, player_box());
    if(d < range) {
        const bool died = take_damage(std::max(1.0, std::floor(kPlayerSplashDamage * power * (1 - d / range) + .5)), false, std::array{x, y});   // Math.round
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
        if(hit.hit || p.life <= 0) { attacker_ = p.owner; rockets_.erase(rockets_.begin() + static_cast<std::ptrdiff_t>(i)); explode(hit.x, hit.y, kRocketPower, hit.z, FireKind::rocket); attacker_ = 0; }
        else { p.x = hit.x; p.y = hit.y; p.z = hit.z; }
    }
    for(std::size_t i = grenades_.size(); i-- > 0;) {
        auto& p = grenades_[i];
        p.life -= dt; p.vz -= kGrenadeGravity * dt;
        const auto hit = shot_sweep(p.x, p.y, p.z, p.x + p.vx * dt, p.y + p.vy * dt, p.z + p.vz * dt, kGrenadeRadius);
        if(hit.hit || p.life <= 0) { attacker_ = p.owner; grenades_.erase(grenades_.begin() + static_cast<std::ptrdiff_t>(i)); explode(hit.x, hit.y, kGrenadePower, hit.z, FireKind::grenade); attacker_ = 0; }
        else { p.x = hit.x; p.y = hit.y; p.z = hit.z; }
    }
    for(std::size_t i = bolts_.size(); i-- > 0;) {
        auto& p = bolts_[i];
        p.life -= dt;
        const auto hit = shot_sweep(p.x, p.y, p.z, p.x + p.vx * dt, p.y + p.vy * dt, p.z + p.vz * dt, kRiftRadius);
        if(hit.hit || p.life <= 0) { attacker_ = p.owner; bolts_.erase(bolts_.begin() + static_cast<std::ptrdiff_t>(i)); explode(hit.x, hit.y, kRiftPower, hit.z, FireKind::rift); attacker_ = 0; }
        else { p.x = hit.x; p.y = hit.y; p.z = hit.z; }
    }
}

void Arena::update_pickups(double dt) {   // update() 12344: medkits
    if(role_ == NetRole::guest) return;   // the host's medkits (world snapshot)
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

// ---- N8 multiplayer (js/net/multiplayer.js) --------------------------------------------------------------
Teammate* Arena::teammate(std::uint32_t key) {
    for(auto& m : team_) if(m.key == key) return &m;
    return nullptr;
}
Teammate& Arena::add_teammate(const std::string& id, const std::string& name) {
    Teammate m;
    m.key = next_teammate_key_++; m.id = id; m.name = name;
    const auto& p = player_.state();   // netMakePlayer: placed near the host until its first pose
    m.x = p.x; m.y = p.y; m.z = p.z;
    team_.push_back(m);
    return team_.back();
}
void Arena::remove_teammate(std::uint32_t key) {
    team_.erase(std::remove_if(team_.begin(), team_.end(), [&](const Teammate& m) { return m.key == key; }), team_.end());
    if(revive_target_ == static_cast<int>(key) || revive_holder_ == static_cast<int>(key)) { revive_target_ = revive_holder_ = -1; revive_seconds_ = 0; }
    // 'guest_left': the host, alone and down, gets up (player.downed = false; hp = 100; respawn)
    if(role_ == NetRole::host && team_.empty() && player_.state().downed) {
        player_.state().downed = false; hp_ = kMaxHealth;
        respawn_queued_ = true;
        finish_respawn();
    }
}
Box Arena::teammate_box(const Teammate& p) const {   // playerHitBox(p)
    const double duck = clamp(p.crouch / .49, 0, 1);
    const double height = physics::kPlayerHeight + (physics::kPlayerCrouchHeight - physics::kPlayerHeight) * duck;
    return {p.x - physics::kPlayerRadius, p.x + physics::kPlayerRadius, p.y - physics::kPlayerRadius, p.y + physics::kPlayerRadius, p.z + .035, p.z + height};
}
std::array<Box, 6> Arena::marine_boxes(const Teammate& p) const {   // marineHitBoxes (11548)
    const double a = (std::isfinite(p.a) ? p.a : 0) - 3.141592653589793 / 2, c = jsmath::cos(a), s = jsmath::sin(a), z = std::isfinite(p.z) ? p.z : 0;
    auto part = [&](double cx, double cy, double cz, double sx, double sy, double sz) {
        const double ox = c * cx - s * cy, oy = s * cx + c * cy;
        const double hx = (std::fabs(c) * sx + std::fabs(s) * sy) * .5, hy = (std::fabs(s) * sx + std::fabs(c) * sy) * .5;
        return Box{p.x + ox - hx, p.x + ox + hx, p.y + oy - hy, p.y + oy + hy, z + cz - sz * .5, z + cz + sz * .5};
    };
    return {part(0, 0, 1.59, .72, .56, .60), part(0, 0, 1.16, .76, .52, .74), part(-.51, .11, .97, .36, .57, .62), part(.51, .11, .97, .36, .57, .62),
            part(-.20, .04, .45, .44, .48, .90), part(.20, .04, .45, .44, .48, .90)};
}
Arena::Aim Arena::target_for(double bx, double by) const {   // enemyTargetFor (15801)
    const auto& p = player_.state();
    const Aim local{p.x, p.y, p.z, p.vx, p.vy, 0};
    if(role_ != NetRole::host || team_.empty()) return local;
    const Teammate* best = nullptr;
    bool local_best = !p.downed;
    double score = local_best ? js_hypot(p.x - bx, p.y - by) : 1e9;
    for(const auto& m : team_) if(!m.downed && m.hp > 0) {
        const double d = js_hypot(m.x - bx, m.y - by);
        if(d < score) { score = d; best = &m; local_best = false; }
    }
    if(best) return {best->x, best->y, best->z, best->vx, best->vy, best->key};
    return local;
}
void Arena::remote_damage(Teammate& p, double damage) {   // remoteApplyDamage (15794)
    if(role_ != NetRole::host || p.downed || p.hp <= 0) return;
    p.hp = std::max(0.0, p.hp - std::max(0.0, damage == damage ? damage : 0.0));
    p.hurt_until = now() + .27;
    if(p.hp == 0) { p.downed = true; p.holding_revive = false; p.vx = p.vy = 0; events_.downed.push_back(p.key); }
}
void Arena::remote_fire(Teammate& p, int i) {   // netHostRemoteFire (15773)
    if(p.downed || p.hp <= 0 || i < 0 || i >= static_cast<int>(kWeaponCount)) return;
    const WeaponDef& w = kWeapons[static_cast<std::size_t>(i)];
    const double t = now();
    if(t < p.next_fire) return;
    p.shoot_until = t + .14;
    p.next_fire = t + std::max(.06, w.delay * .88);
    events_.remote_shots.push_back({p.key, i});
    struct G { double x, y, z, dx, dy, dz; };
    auto gunpos = [&](double ang, double tilt) {
        const double co = jsmath::cos(tilt), dx = jsmath::cos(ang) * co, dy = jsmath::sin(ang) * co, dz = -jsmath::sin(tilt);
        return G{p.x + dx * .24, p.y + dy * .24, p.z + std::max(.83, 1.5 - p.crouch * .60) + dz * .24, dx, dy, dz};
    };
    const G m = gunpos(p.a, p.pitch);
    if(w.kind == FireKind::rocket) rockets_.push_back({FireKind::rocket, m.x, m.y, m.z, m.dx * 26, m.dy * 26, m.dz * 26, 2.6, next_projectile_id_++, p.key});
    else if(w.kind == FireKind::grenade) grenades_.push_back({FireKind::grenade, m.x, m.y, m.z, m.dx * 22, m.dy * 22, m.dz * 22 + 1.5, 2.3, next_projectile_id_++, p.key});
    else if(w.kind == FireKind::rift) bolts_.push_back({FireKind::rift, m.x, m.y, m.z, m.dx * 19, m.dy * 19, m.dz * 19, 2.6, next_projectile_id_++, p.key});
    else {
        attacker_ = p.key;
        for(int n = 0; n < w.pellets; ++n) {
            const double az = p.a + (rng_.next() - .5) * w.spread * 2, pitch = clamp(p.pitch + (rng_.next() - .5) * w.spread * 1.4, -1.49, 1.49);
            const G at = gunpos(az, pitch);
            const auto hit = shot_sweep(at.x, at.y, at.z, at.x + at.dx * w.range, at.y + at.dy * w.range, at.z + at.dz * w.range, w.pellets > 1 ? .025 : .042);
            if(hit.target >= 0) damage_target(static_cast<std::size_t>(hit.target), w.damage, hit.z, hit.head ? 1 : 0);
        }
        attacker_ = 0;
    }
}
void Arena::remote_frag(bool head) { ++frags_; reroll_one(head); }   // 'frag': score++; rerollOne(head)
void Arena::queue_remote_blast(const BlastEvent& blast) {
    if(remote_blasts_.size() < 64) remote_blasts_.push_back(blast);
}
void Arena::queue_guest_health(double health) {
    if(health < hp_) {
        const double loss = hp_ - health;
        if(guest_hit_) guest_hit_->amount += loss;
        else guest_hit_ = IncomingHit{loss, false, {}};
    }
    hp_ = health;
}
void Arena::set_local_downed(bool downed) {
    auto& p = player_.state();
    const bool was = p.downed;
    p.downed = downed;
    if(downed) { p.vx = p.vy = p.vz = 0; fire_held_ = false; if(!was) events_.downed.push_back(0); }
    else if(was) { respawn_grace_ = 1.8; events_.revived.push_back(0); }
}
bool Arena::near_revive(double ax, double ay, double az, bool a_downed, double bx, double by, double bz, bool b_downed) const {   // netNearRevive (15814)
    if(!b_downed || a_downed) return false;
    const double distance = js_hypot(ax - bx, ay - by);
    if(distance > kReviveDistance || std::fabs(az - bz) > 1.75 || distance < .01) return false;
    return world_->shot_collision_t(ax, ay, az + 1.10, bx, by, bz + 1.0, .09) == kInf;   // a 3-D line of sight
}
void Arena::coop_tick(double dt) {   // netHostTick (15821)
    // medkits for every live teammate
    for(auto& p : team_) if(p.hp > 0 && !p.downed) {
        for(auto& k : pickups_) {
            if(k.cool <= 0 && js_hypot(k.x - p.x, k.y - p.y) < kMedkitRadius && std::fabs(p.z - k.z) < 1.2 && p.hp < kMaxHealth) { p.hp = std::min(kMaxHealth, p.hp + kMedkitHeal); k.cool = kMedkitCooldown; break; }
        }
    }
    // any live player holding F near a downed one revives them in 4 s
    struct P { int key; double x, y, z; bool downed; double hp; bool holding; };
    const auto& me = player_.state();
    std::vector<P> all{{0, me.x, me.y, me.z, me.downed, hp_, local_holding_revive()}};
    for(const auto& m : team_) all.push_back({static_cast<int>(m.key), m.x, m.y, m.z, m.downed, m.hp, m.holding_revive});
    int target = -1, holder = -1;
    bool live_any = false;
    for(const auto& src : all) {
        if(src.downed || src.hp <= 0) continue;
        live_any = true;
        if(target >= 0 || !src.holding) continue;
        const P* closest = nullptr;
        double dist = kInf;
        for(const auto& down : all) {
            if(!down.downed || down.key == src.key || !near_revive(src.x, src.y, src.z, src.downed, down.x, down.y, down.z, down.downed)) continue;
            const double d = js_hypot(src.x - down.x, src.y - down.y);
            if(d < dist) { dist = d; closest = &down; }
        }
        if(closest) { target = closest->key; holder = src.key; }
    }
    if(target >= 0) {
        if(target != revive_target_ || holder != revive_holder_) { revive_seconds_ = 0; revive_target_ = target; revive_holder_ = holder; }
        revive_seconds_ += dt;
        if(revive_seconds_ >= kReviveSeconds) {
            if(target == 0) {
                auto& p = player_.state();
                p.downed = false; hp_ = 45; p.vx = p.vy = p.vz = 0;
                respawn_grace_ = 2; burn_remaining_ = 0; burn_tick_ = 0;
            } else if(Teammate* m = teammate(static_cast<std::uint32_t>(target))) { m->downed = false; m->hp = 45; m->vx = m->vy = 0; }
            events_.revived.push_back(static_cast<std::uint32_t>(target));
            revive_target_ = revive_holder_ = -1; revive_seconds_ = 0; wipe_clock_ = 0;
        }
    } else { revive_target_ = revive_holder_ = -1; revive_seconds_ = 0; }
    // the squad is wiped when every connected player is down (solo-host play keeps the solo respawn)
    if(!team_.empty() && !live_any) {
        wipe_clock_ += dt;
        if(wipe_clock_ > 2.7) { wipe_clock_ = 0; events_.wipe = true; }
    } else wipe_clock_ = 0;
}
}  // namespace cat::combat
