#pragma once
// N3 physics and first-person traversal. GPU/SDL independent, deterministic, fixed 1/120 s ticks.
//
// The collision world is the N1-baked *compiled* map data (slabs with optional ramp planes, vertical
// wall segments, cavities, footprint, massive solids, jump pads) queried exactly like the legacy
// "Stage 4" player collision (legacy/index.html 8225-8665). The player controller reproduces the
// legacy movement model (5161-5171 constants, 12111-12220 moveMarine, 14949 beginSlide, 14962 key
// handling, 9765 jump pads) and the Quake III style view/weapon motion (10443-10555).
// Reference trajectories recorded from the unmodified legacy game are replayed in
// tests/physics_tests.cpp (tools/n3/legacy_trajectories.cjs).
#include "cat/assets.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cat::physics {
// ---- legacy constants (index.html 5161-5171, 8461-8462, 10457-10466) -------------------------------
inline constexpr double kTick = 1.0 / 120.0;            // SIM_DT
inline constexpr double kWalkSpeed = 7.6, kCrouchMult = 0.80, kSprintMult = 1.50;
inline constexpr double kSlideDuration = 1.12, kSlideCooldown = .72, kSlideBoost = 1.085;
inline constexpr double kGroundAcceleration = 17.5, kAirAcceleration = 1.85;
inline constexpr double kGroundFriction = 6.0, kCrouchFriction = 7.3, kSlideFriction = .62;
inline constexpr double kStopSpeed = 2.9, kJumpImpulse = 7.65, kMaxSkillSpeed = 20.5;
inline constexpr double kJumpBuffer = .115, kCoyoteTime = .09, kLandingSlideBuffer = .30;
inline constexpr double kGradePenalty = .38, kGradeDownhillBonus = .13;
inline constexpr double kPlayerRadius = .30, kPlayerHeight = 1.80, kPlayerCrouchHeight = 1.20, kGravity = 18;
inline constexpr double kSweepStep = .105, kStepHeight = .52, kGroundSnap = .24, kSlabTolerance = .035;
inline constexpr int kMaxSubsteps = 180;
inline constexpr double kCrouchOffsetTarget = .49;      // camera drop while crouching/sliding
inline constexpr double kEyeHeight = 1.5, kDownedEye = .62;
inline constexpr double kLookYaw = .0030, kLookPitch = .003, kPitchLimit = 1.1;   // aim(): radians per mouse count
// Collider height actually used by the legacy collision: max(1.20, 1.80 - crouchOffset*.60).
// With crouchOffset saturating at .49 the crouched collider is 1.506 m tall, NOT the nominal 1.20 m.
// N3 preserves this effective behaviour (see docs/N3_PHYSICS.md, "Crouch collider").
double collider_height(double crouch_offset);

struct Slab {
    std::vector<std::array<double, 2>> poly;
    std::array<double, 4> box{};
    double z{}, thickness{}, base{};
    bool has_base{}, has_plane{}, nav{}, roof{};
    double ox{}, oy{}, dx{}, dy{}, len{}, z0{}, z1{};   // ramp plane
    std::string id, kind;
};
struct Wall { double ax{}, ay{}, bx{}, by{}, z0{}, z1{}; };
struct Pad { std::string id; double x{}, y{}, z{}, r{}, cooldown{}; bool has_apex{}; std::array<double, 3> apex{}, velocity{}; };
struct Spawn { double x{}, y{}, z{}, a{}; std::string tag; };
struct PickupSpot { double x{}, y{}, z{}; std::string tag; };
struct Surface { double z{}; const Slab* slab{}; };
struct WallContact { const Wall* wall{}; double qx{}, qy{}, d{}; };

class CollisionWorld {
public:
    explicit CollisionWorld(const assets::Value& compiled);   // map.cadata "compiled"
    // levelWalkable: inside the footprint and not inside a floor-to-sky solid.
    bool walkable(double x, double y) const;
    bool in_cavity(double x, double y) const;
    // slabQuery: every slab whose polygon contains x/y (edge tolerance .02), in legacy order.
    std::size_t slab_query(double x, double y, std::array<const Slab*, 64>& out) const;
    static double slab_z(const Slab& s, double x, double y);
    static double slab_thickness(const Slab& s, double z);
    double floor_height(double x, double y, double feet) const;                          // levelFloorHeight
    std::optional<Surface> support(double x, double y, double feet, double step) const;   // stage4Support
    bool walkable_radius(double x, double y, double r) const;
    bool wall_hit(double x, double y, double feet, double height, double r) const;
    bool capsule_clear(double x, double y, double feet, double height, double r = kPlayerRadius) const;
    std::optional<double> ceiling_plane(double x, double y, double from_head, double to_head) const;
    std::optional<Surface> landing_plane(double x, double y, double from_feet, double to_feet) const;
    std::optional<WallContact> nearest_wall(double x, double y, double feet, double height) const;
    const std::vector<Slab>& slabs() const { return slabs_; }
    const std::vector<Wall>& walls() const { return walls_; }
    const std::vector<Pad>& pads() const { return pads_; }
    const std::vector<Spawn>& spawns() const { return spawns_; }
    // N5 shots: levelSolidAt (8443) and worldShotCollisionT (11820). solid_at is true outside the
    // footprint, below the lowest floor, inside a solid, within r + 0.095 of a wall segment or inside a slab.
    bool solid_at(double x, double y, double z, double r = .06) const;
    // Fraction of the segment at which a shot of radius r first touches the world (sampled every
    // 0.19 m as in the legacy); infinity when the segment is clear.
    double shot_collision_t(double x, double y, double z, double nx, double ny, double nz, double r = .06) const;
    // N6 flyers and spawning: underside of the lowest roof slab over a point (levelOverhead), whether a
    // vertical span touches a slab body (levelHitsSlab) or a solid prop within r (levelFeatureSolidAt).
    std::optional<double> overhead(double x, double y) const;
    bool hits_slab(double x, double y, double r, double z0, double z1) const;
    bool feature_solid_at(double x, double y, double z0, double z1, double r) const;
    // worldPickupSpots (9738): authored pickups that sit on a walkable floor surface.
    const std::vector<PickupSpot>& pickups() const { return pickups_; }
private:
    using Poly = std::vector<std::array<double, 2>>;
    struct Solid { Poly poly; std::array<double, 4> box{}; bool massive{}; double z0{}, z1{}; };
    const std::vector<std::uint32_t>& cell(const std::vector<std::vector<std::uint32_t>>& grid, double x, double y) const;
    const std::vector<std::uint32_t>* wall_bucket(double x, double y) const;
    double cell_{4};
    long long gw_{};
    std::vector<Slab> slabs_;
    std::vector<Wall> walls_;
    std::vector<Pad> pads_;
    std::vector<Spawn> spawns_;
    std::vector<PickupSpot> pickups_;
    std::vector<Solid> solids_, cavities_, footprint_;
    std::vector<std::vector<std::uint32_t>> slab_grid_, solid_grid_, cavity_grid_;
    std::map<std::pair<long long, long long>, std::vector<std::uint32_t>> wall_buckets_;   // LEVEL_BUCKET = 4
    std::vector<std::uint32_t> empty_;
};

// ---- player -------------------------------------------------------------------------------------
// Input bits = the legacy key codes the movement code reads. One mask per tick; press/release
// events are derived from changes between ticks, like non-repeating keydown/keyup events.
enum Key : std::uint32_t { key_forward = 1, key_back = 2, key_left = 4, key_right = 8, key_jump = 16, key_crouch = 32, key_sprint = 64, key_slide = 128 };
// look: mouse counts since the previous tick. N7: stick = the touch joystick / gamepad stick (legacy
// touchStick: x right, y down, |v| <= 1), added to the movement keys as update() does; 0 for keyboard play.
struct TickInput { std::uint32_t keys{}; double look_dx{}, look_dy{}; double stick_x{}, stick_y{}; };

struct PlayerState {
    double x{}, y{}, z{};          // feet position; z is up
    double vx{}, vy{}, vz{};
    double a{}, pitch{};           // yaw and pitch, radians (legacy player.a / player.pitch)
    double crouch_offset{};
    bool ground{true}, downed{};
};
struct MoveTimers { double slide_time{}, slide_cooldown{}, slide_buffer{}, jump_buffer{}, coyote_time{}, pad_cooldown{}; };
struct Contacts { std::uint64_t horizontal{}, ceiling{}, landings{}, steps{}, iterations{}, recoveries{}; };   // STAGE4_CONTACTS
struct ViewState {   // Q3 object (q3-view-motion.js)
    double bob_cycle{}, bobfracsin{}, xyspeed{}, land_change{}, land_time{-99}, step_change{}, step_time{-99}, last_z{};
    int bob_odd{};
    bool crouched{}, has_last_z{}, last_ground{};
    double bob_scale{1};            // "motion strength" setting, 1 = default
    std::uint64_t footsteps{}, landings{};
    double last_land_delta{};
};
struct ViewCamera { double x{}, y{}, z{}; std::array<double, 3> forward{}, right{}, up{}; double roll{}; };
struct WeaponMotion { double pitch{}, yaw{}, roll{}, drop{}; };   // radians; drop in viewmodel units
enum class Stance { run, sprint, crouch, slide, air_control, air_momentum };
// N7: body sounds of one step() (legacy playBodySound calls); presentation only, no effect on the simulation.
enum class BodySound : std::uint8_t { jump, land, step, slide };
struct BodyEvent { BodySound kind; double force; };

class PlayerController {
public:
    explicit PlayerController(const CollisionWorld& world) : world_(&world) {}
    void spawn(double x, double y, double z, double yaw, double pitch = 0, double now = 0);   // respawn-style full reset
    // N6: what the legacy resets when a fragged player re-enters (finishPendingRespawn): position, zero
    // velocity and pitch, standing on the ground, view cycle, slide / jump timers. Held keys, the jump
    // pad cooldown and the clock are kept.
    void respawn(double x, double y, double z, double yaw);
    // One 1/120 s tick: key transitions, look, then the legacy update order
    // (moveMarine -> stage4Movement -> jump pads -> view cycle).
    void step(const TickInput& input);
    // The pieces of step(), public for tests and for callers that deliver events themselves.
    void key_event(Key key, bool down);
    void aim(double dx, double dy);
    void tick(double dt);

    const PlayerState& state() const { return p_; }
    PlayerState& state() { return p_; }
    const MoveTimers& timers() const { return t_; }
    const Contacts& contacts() const { return contacts_; }
    const ViewState& view() const { return q3_; }
    ViewState& view() { return q3_; }
    double now() const { return now_; }
    double slope_angle() const { return slope_angle_; }
    double fov_scale() const { return fov_scale_; }          // multiply the configured FOV by this
    double height() const { return collider_height(p_.crouch_offset); }
    Stance stance() const { return stance_; }
    std::uint64_t pad_launches() const { return pad_launches_; }
    std::uint64_t jumps() const { return jumps_; }
    ViewCamera shot_origin() const;       // eye without cosmetic motion (q3ShotOrigin)
    ViewCamera view_camera() const;       // rendered eye: bob, landing dip, step smoothing, run pitch/roll
    WeaponMotion weapon_motion() const;   // viewmodel sway for N5 (q3WeaponMotion)
    // N7: playBodySound() calls of the last step() (cleared when step() begins).
    const std::vector<BodyEvent>& body_events() const { return body_events_; }
    void note_body(BodySound kind, double force) { body_events_.push_back({kind, force}); }
    void clear_body_events() { body_events_.clear(); }                    // callers that drive tick() themselves (combat::Arena)
    void set_stick(double x, double y) { stick_x_ = x; stick_y_ = y; }   // touchStick for the next tick()
private:
    bool held(Key k) const { return (keys_ & k) != 0; }
    bool begin_slide(bool from_buffer);
    void friction(double dt, double amount);
    void accelerate(double dx, double dy, double wish_speed, double accel, double dt, bool airborne);
    double slope(double x, double y, double feet, double dx, double dy) const;
    void move_marine(double dt, double wx, double wy, double m);
    void stage4_movement(double dt);
    void trigger_update(double dt);
    void post_move(double dt);
    void crash_land(double impact);

    const CollisionWorld* world_;
    PlayerState p_;
    MoveTimers t_;
    Contacts contacts_;
    ViewState q3_;
    std::uint32_t keys_{};
    double stick_x_{}, stick_y_{};
    double now_{}, slope_angle_{}, fov_scale_{1}, air_strafe_gained_{};
    Stance stance_{Stance::run};
    std::uint64_t pad_launches_{}, jumps_{};
    std::vector<BodyEvent> body_events_;
};

// Math.hypot as V8 computes it (scaled Kahan sum); used wherever the legacy code calls Math.hypot so
// that results track the reference bit for bit where the platform's sqrt is IEEE-correct.
double js_hypot(double a, double b);
double js_hypot3(double a, double b, double c);
}  // namespace cat::physics
