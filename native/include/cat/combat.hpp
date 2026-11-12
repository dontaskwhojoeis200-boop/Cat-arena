#pragma once
// N5 weapons, projectiles, damage and pickups. GPU/SDL independent, deterministic, 1/120 s ticks.
//
// A port of the legacy combat rules (legacy/index.html): WEAPONS (5140-5156), fireWeapon (12054),
// firePosition / shotSweep / worldShotCollisionT (11811-11843), hit boxes and segmentBoxTime
// (11524-11584), damageBot (11585), rerollOne (11593), explode (12015), applyShotgunRecoil (11992),
// switchWeapon / reloadWeapon (12008), the projectile and pickup loops of update() (12308-12344) and
// takeDamage (11912). Each of the 16 weapons keeps its own table row and its own firing branch;
// there is no shared "generic weapon". Verified against tapes recorded from the unmodified legacy
// game (tools/n5/legacy_combat.cjs, tests/combat_tests.cpp).
//
// Enemies arrive in N6. Until then damage is dealt to `Target`s: static bodies with the legacy
// enemy hit boxes (body + oversized head) of the three size classes.
#include "cat/physics.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace cat::combat {
enum class FireKind : std::uint8_t { hitscan, rocket, grenade, rift };
struct WeaponDef {
    const char* name;
    double damage, delay;      // table damage (explosives use the blast formula instead), seconds between shots
    int clip;
    double reload;             // seconds
    int pellets;
    double spread, range;      // radians (half-width factor), metres
    FireKind kind;
    bool shotgun;              // SHOTGUN_IDS {1, 7, 15}: firing recoil / mid-air double jump
    bool rail, lightning;      // muzzle-flash timing class
    std::array<float, 3> color;
};
inline constexpr std::size_t kWeaponCount = 16;
const std::array<WeaponDef, kWeaponCount>& weapons();

// Blast parameters (explode): projectile -> (power, sweep radius).
inline constexpr double kRocketSpeed = 26, kRocketLife = 2.6, kRocketPower = 1.15, kRocketRadius = .14;
inline constexpr double kGrenadeSpeed = 22, kGrenadeLift = 1.50, kGrenadeLife = 2.3, kGrenadeGravity = 15, kGrenadePower = 1.08, kGrenadeRadius = .15;
inline constexpr double kRiftSpeed = 19, kRiftLife = 2.6, kRiftPower = 1.42, kRiftRadius = .23;
inline constexpr double kEnemySplashRadius = 3.35, kEnemySplashDamage = 95;   // x power
inline constexpr double kPlayerSplashRadius = 3.25, kPlayerSplashDamage = 38; // x power, rounded, at least 1
inline constexpr double kBlastHorizontal = 8.0;                              // m/s at full blast
inline constexpr double kMedkitHeal = 40, kMedkitCooldown = 13, kMedkitRadius = .85, kMaxHealth = 100;

// Input bits in addition to cat::physics::Key (one mask per tick).
enum Key : std::uint32_t {
    key_fire = 256, key_reload = 512, key_switch = 1024,
    // Fixture actions, not player inputs (tools/n6/legacy_enemies.cjs): remove the first enemy with
    // 9999 damage; deal 35 damage to the player. Applied after the fire transition, before the look.
    key_fixture_kill = 2048, key_fixture_hurt = 4096
};

enum class TargetClass : std::uint8_t { standard, heavy, fast };   // enemyHitScale: (1, 1), (1.22, 1.10), (.86, .90)
struct Box { double x0, x1, y0, y1, z0, z1; };
// A target of the player's weapons: a static practice target (type < 0) or, since N6, an enemy with
// the full state of a legacy "bot" (spawnBot 11473). Enemy logic lives in cat/enemies.hpp.
struct Target {
    double x{}, y{}, z{};
    TargetClass cls{TargetClass::standard};
    double hp{100}, max_hp{100}, hit_flash{};
    std::uint32_t id{};
    std::array<Box, 2> boxes() const;   // [0] body, [1] head (enemyHitBoxes)
    double height() const;              // enemyHeight
    // ---- enemy state (type >= 0) ----
    int type{-1};                       // index into enemy_types()
    std::uint32_t uid{};                // enemySerial
    bool flying{};
    double speed{}, base_speed{}, damage_scale{1};
    double a{};                         // facing (enemyFacingFromVec convention)
    double cool{}, attack_wind{}, attack_total{}, attack_recover{}, attack_recover_total{};
    double anim_phase{}, walk_amount{}, voice_cooldown{}, ident{};   // ident: per-enemy random phase (legacy b.id)
    double fly_alt{}, fly_phase{}, fly_stall{}, fly_decide{};
    int fly_steer_side{1};
    bool fly_decide_set{}, has_fly_dir{};
    std::array<double, 3> fly_dir{};
    std::vector<std::array<double, 3>> path;   // smoothed route
    std::size_t path_index{};
    std::uint32_t path_serial{};        // changes whenever `path` is replaced (identity of its nodes)
    double path_cooldown{}, nav_stuck{}, nav_stride_acc{};
    int nav_stride_frames{};
    bool nav_decision{}, nav_queued{}, has_goal{};
    double goal_x{}, goal_y{}, goal_z{};
    struct Probe { bool valid{}, ok{}; double t{}, bx{}, by{}, bz{}, tx{}, ty{}, tz{}; } direct;          // cached direct-path probe
    struct Segment { bool valid{}; std::uint32_t serial{}; std::size_t index{}; double t{}, x{}, y{}, z{}; } seg_ok;   // cached route-segment check
};
struct Projectile { FireKind kind{FireKind::rocket}; double x{}, y{}, z{}, vx{}, vy{}, vz{}, life{}; std::uint32_t id{}; };
struct Pickup { double x{}, y{}, z{}, cool{}; };
struct ShotHit { bool hit{}; double t{1}; int target{-1}; bool head{}; double x{}, y{}, z{}; };

// What happened during the last step(); presentation (HUD, effects, audio) reads these.
struct ShotEvent { double x{}, y{}, z{}; bool hit{}, target{}, head{}; };
struct BlastEvent {
    double x{}, y{}, z{}, power{};        // detonation point used for damage
    FireKind kind{FireKind::rocket};
    std::uint32_t seq{};                  // order among this step's presentation events (Arena::next_event_seq)
    double fx_z() const { return z > .05 ? z : .05; }   // height the legacy draws the explosion at (spawnExplosionVisual)
};
struct DamageEvent { std::uint32_t target_id{}; double amount{}; bool head{}, killed{}; };
// The moment of the shot, for presentation: the legacy starts tracers and the muzzle flash from the
// view as it was when fireWeapon() ran (before or after this tick's movement).
struct FireContext { physics::ViewCamera view, eye; double fov_scale{1}, now{}; int weapon_in_hand{-1}; };
struct Events {
    int fired_weapon{-1};                 // weapon index fired this step, -1 if none
    FireContext fire;                     // valid when fired_weapon >= 0; weapon_in_hand = slot weapon after the volley (a frag may have rerolled it)
    std::uint32_t fire_seq{};             // order of the volley (WFX.fire) among this step's presentation events
    std::vector<ShotEvent> shots;         // hitscan pellet end points
    std::vector<BlastEvent> blasts;
    std::vector<DamageEvent> damage;      // damage dealt to targets
    double player_damage{};               // damage taken by the player
    double healed{};
    int reroll{-1};                       // new weapon index after a kill
    bool reload_started{}, reload_done{}, switched{};
};

// mulberry32: the seeded generator also used when recording the legacy reference.
class Rng {
public:
    explicit Rng(std::uint32_t seed = 1) : state_(seed) {}
    double next();
    std::uint64_t calls() const { return calls_; }
    // Reference pictures (cat_arena --enemy-lineup, --enemy-fixture captures): until clear_constant()
    // every draw returns `value`, as the legacy tools replace Math.random by () => 0.5 while they take
    // a picture. Such draws neither advance the stream nor count in calls().
    void set_constant(double value) { constant_ = value; has_constant_ = true; }
    void clear_constant() { has_constant_ = false; }
private:
    double constant_{};
    bool has_constant_{};
    std::uint32_t state_;
    std::uint64_t calls_{};
};

double point_box_distance(double x, double y, double z, const Box& b);                      // pointBoxDistance
// segmentBoxTime: first fraction in [0, 1] at which the segment (radius r) touches the box; negative when it misses.
double segment_box_time(double x, double y, double z, double nx, double ny, double nz, const Box& b, double r);

class Horde;
class Arena {
public:
    Arena(const physics::CollisionWorld& world, std::uint32_t seed);
    physics::PlayerController& player() { return player_; }
    const physics::PlayerController& player() const { return player_; }
    // Full reset as at a map start (finishMapSetup): player at the pose, 100 hp, pickups ready,
    // no projectiles; the loadout is NOT rolled here (call roll_loadout or set_loadout).
    void spawn(double x, double y, double z, double yaw, double pitch = 0, double now = 0);
    void set_loadout(int a, int b);     // both magazines full, slot 0
    void roll_loadout();                // rollLoadout: two different random weapons
    std::uint32_t add_target(double x, double y, double z, TargetClass cls, double hp);
    void clear_targets() { targets_.clear(); }
    void set_health(double hp) { hp_ = hp; }

    // One 1/120 s tick. `input.keys` may contain physics::Key and combat::Key bits.
    void step(const physics::TickInput& input);
    // Pieces, public for tests and direct callers.
    void fire();                        // fireWeapon
    void reload();                      // reloadWeapon
    void switch_weapon();               // switchWeapon
    void explode(double x, double y, double power, double z, FireKind kind);
    bool take_damage(double amount, bool burn = false);   // takeDamage; returns true when the player died
    void damage_target(std::size_t index, double damage, double z_hit, int part);   // damageBot; part: -1 by height, 0 body, 1 head
    Box player_box() const;             // playerHitBox
    // N6: enemies, waves and enemy projectiles of this arena (cat/enemies.hpp). Null = none (N5 behaviour).
    void attach(Horde* horde) { horde_ = horde; }
    // N6: every event that starts a cosmetic effect (volley, blast, enemy death, enemy shot impact) gets
    // the next number of the step, so presentation can replay them in the order the legacy update()
    // started them (the cosmetic random stream depends on that order).
    std::uint32_t next_event_seq() { return ++event_seq_; }
    // N6: called inside step() where the legacy update() runs WFX.update / EFX.update /
    // updateDyingEnemies: after the projectiles, pickups and bodies, BEFORE a pending respawn
    // (which clears every projectile and moves the player) and before the wave bookkeeping.
    // Presentation (fx::EnemyFx::tick, fx::WeaponFx::tick) belongs here, so that it sees the step's
    // world as the legacy effects did. Empty = none (callers may then tick effects after step()).
    void set_presentation_hook(std::function<void()> hook) { presentation_hook_ = std::move(hook); }
    double burn_remaining() const { return burn_remaining_; }
    double now() const { return player_.now(); }
    const physics::CollisionWorld& world() const { return *world_; }
    ShotHit shot_sweep(double x, double y, double z, double nx, double ny, double nz, double r) const;

    double health() const { return hp_; }
    bool dead() const { return respawn_queued_; }             // true only between a lethal hit and the respawn at the end of that tick
    int slot() const { return slot_; }
    int weapon(int slot) const { return loadout_[static_cast<std::size_t>(slot)]; }
    int current_weapon() const { return loadout_[static_cast<std::size_t>(slot_)]; }
    int ammo(int slot) const { return mag_[static_cast<std::size_t>(slot)]; }
    double cooldown_until() const { return cooldown_; }
    double reloading() const { return reloading_; }          // seconds left, 0 when not reloading
    double gun_kick() const { return gun_kick_; }            // 1 at a shot, decays 6/s (viewmodel recoil)
    double muzzle_flash() const { return muzzle_flash_; }    // seconds left
    double damage_flash() const { return flash_; }
    double respawn_grace() const { return respawn_grace_; }
    std::uint64_t frags() const { return frags_; }
    const std::vector<Target>& targets() const { return targets_; }
    std::vector<Projectile> projectiles() const;            // rockets, then grenades, then rift bolts
    std::uint64_t deaths() const { return deaths_; }
    const std::vector<Pickup>& pickups() const { return pickups_; }
    const Events& events() const { return events_; }
    const Rng& rng() const { return rng_; }
    Rng& random_stream() { return rng_; }   // staged pictures only (Rng::set_constant)
private:
    struct FirePose { double x, y, z, dx, dy, dz; };
    FirePose fire_position(double az, double pitch) const;   // firePosition
    void reroll_one(bool head);
    void shotgun_recoil(int index);
    void update_projectiles(double dt);
    void update_pickups(double dt);

    const physics::CollisionWorld* world_;
    physics::PlayerController player_;
    Rng rng_;
    std::vector<Target> targets_;
    std::vector<Projectile> rockets_, grenades_, bolts_;    // separate lists, updated in this order (as the legacy)
    std::vector<Pickup> pickups_;
    Events events_;
    std::array<int, 2> loadout_{0, 1}, mag_{50, 8};
    int slot_{};
    double hp_{kMaxHealth}, cooldown_{}, reloading_{}, gun_kick_{}, muzzle_flash_{}, flash_{}, respawn_grace_{};
    bool fire_held_{}, dead_{}, respawn_queued_{};
    std::uint32_t keys_{}, next_target_id_{1}, next_projectile_id_{1};
    std::uint64_t frags_{}, deaths_{};
    double burn_remaining_{}, burn_tick_{};
    Horde* horde_{};
    std::uint32_t event_seq_{};
    std::function<void()> presentation_hook_;
    friend class Horde;
    void finish_respawn();
};
}  // namespace cat::combat
