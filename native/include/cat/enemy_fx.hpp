#pragma once
// N6 enemy presentation: skeleton poses of the 20 creature types and the Marine Cat, death animation,
// the per-frame instance lists of the creature renderer (live bodies, corpses, the "final five"
// x-ray outline, projected shadows), enemy projectile sprites, casting glows, melee arcs, blob
// shadows, impacts, blood and gibs. GPU/SDL independent; it never changes gameplay state.
//
// A port of the legacy enemy-anim.js (legacy/index.html 10575-10777: ENEMY_CLASS, ENEMY_ATTACK_ANIM,
// GAIT_STRIDE, attackPhases, enemyEvalPose, enemyStartDeath, enemyDeathState, enemyHandWorld),
// enemy-fx.js (11190-11313: EFX.update / build / shotImpact), enemyDeathFx (11905), WFX.gibs (10962),
// fillEnemyInstances / drawFinalFiveXray / drawEnemyShadows (13345-13409), drawMarineCats (13303)
// and inView (5302). The skeletons (enemyRigFromMesh) are not re-measured here: the tables in
// generated/enemy_tables.inc are the values the legacy computed (tools/n6/legacy_enemy_assets.cjs).
//
// Random numbers: the animation (gait phase, idle seed, pain side, death variant and side) has its
// own generator; particles use the cosmetic generator of the WeaponFx they are added to. Neither
// touches the gameplay stream of the Arena.
// Verified against the unmodified legacy game: the animation by replay (tools/n6/legacy_enemies.cjs
// --pose-out, tests/enemy_fx_tests.cpp); the pictures by image comparison (docs/N6_ENEMIES.md).
#include "cat/enemies.hpp"
#include "cat/weapon_fx.hpp"
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cat::fx {
#include "cat/generated/enemy_tables.inc"

// The 16 joint angles of a pose, laid out as the instPose0..3 vertex attributes:
// [hipL, hipR, kneeL, kneeR | shoulderPitchL, shoulderPitchR, elbowL, elbowR |
//  shoulderRollL, shoulderRollR, spinePitch, spineYaw | headPitch, headYaw, bob, rootRoll].
// For flyers the first four are wing flap L/R and wing tip L/R. Stored as float32, as the legacy does.
using Pose = std::array<float, 16>;
struct AnimState {   // b._anim
    bool ready{};
    double phase{}, speed{}, walk{}, px{}, py{}, pz{}, pa{}, turn{}, pain{}, pain_dir{1}, seed{}, last_t{}, look{}, look_p{};
};
struct PoseInput {
    double x{}, y{}, z{}, a{}, hit_flash{};
    double attack_progress{-1};      // enemyAttackProgress; -1 = not attacking
    bool has_target{};               // enemies look at the player; the Marine Cat has no target
    double tx{}, ty{}, tz{};
};
struct AttackPhases { bool on{}; double wind{}, strike{}, hold{}; };
AttackPhases attack_phases(double progress);
// enemyEvalPose: advances `state` to `time` and writes the pose.
void eval_pose(AnimState& state, const Rig& rig, const PoseInput& in, double time, combat::Rng& rng, Pose& out);
struct DeathState { double fall{}, side{}, sink{}; };
// enemyDeathState: pose and topple of a body `t` seconds after its death.
DeathState death_state(const Rig& rig, const Pose& at_death, int variant, double side, double t, Pose& out);
// enemyHandWorld: forward kinematics of a hand (side -1 left, +1 right), mirroring the vertex shader.
std::array<double, 3> hand_world(const Rig& rig, double x, double y, double z, double a, int side, const Pose& pose);
const Rig& enemy_rig(int type);
// inView: the legacy sphere / frustum test with its margins (eye = feet + 1.5 - crouch offset).
bool in_view(const physics::PlayerState& player, double fov_deg, double aspect, double x, double y, double z, double r);
// Marine Cat hit part of a point in model space (+Y forward, +Z up), from the hit metadata of
// cat_marine_hitboxes.json in the rest pose: index into kMarinePart, or -1 outside every part.
int marine_part_at(double x, double y, double z);

// One instance of the creature renderer (112 bytes, the four legacy instance buffers side by side).
struct EnemyInstance {
    float pos[4];     // x, y, z, facing
    float state[4];   // hit flash, fall, side roll, sink
    float pose[16];
    float tint[4];    // x = floor height (projected shadow); Marine Cat: rgb team colour
};
struct EnemyFrame {
    std::array<std::vector<EnemyInstance>, 20> solid;    // live creatures, then corpses
    std::array<std::vector<EnemyInstance>, 20> xray;     // "final five" outline through walls (empty unless active)
    std::array<std::vector<EnemyInstance>, 20> shadow;   // cg_shadows 3 (empty in the default mode 1)
    std::vector<EnemyInstance> marines;                  // Marine Cats to draw (never the local player in first person)
    bool xray_on{};
    std::array<float, 2> shadow_dir{};                   // metres of offset per metre of height
    std::size_t instances() const;
    void clear();
};
struct Body { std::uint32_t corpse_id{}; int type{}, variant{}; double side{1}, flash{1}; Pose pose{}; };   // dyingEnemies entry (animation part)
// Another player as a Marine Cat (netPlayers in the legacy; N8 networking fills these, staged pictures
// and tests may too). Always drawn, also in first person: only the LOCAL player is never drawn there.
struct Guest {
    std::uint32_t id{};                  // keeps the animation state of this player between frames
    double x{}, y{}, z{}, a{}, vx{}, vy{}, step{};
    std::array<float, 3> color{1, 1, 1};
    double shoot_until{}, melee_until{}, hurt_until{};   // _marineShootUntil / _marineMeleeUntil / _marineHurtUntil
    bool downed{};
};
struct BuildOptions {
    double fov_deg{90}, aspect{16.0 / 9};
    int shadows{1};              // Q3R.S.shadows: 0 none, 1 blob (default), 3 projected bodies + blobs under flyers
    double sun_azimuth{-.62};    // WORLD.meta.sky.sunAzimuth
    bool local_marine{};         // also draw the local player's Marine Cat (third-person / mirror views only)
    std::vector<Guest> guests;   // other players (at most 4 Marine Cats are drawn, the local one first)
};

class EnemyFx {
public:
    EnemyFx(WeaponFx& fx, std::uint32_t seed);
    void clear();
    // Once per simulation tick, right after Arena::step(). Calls WeaponFx::tick itself, at the place
    // the legacy runs WFX.update: death and impact effects first, then WFX.update, EFX.update and
    // the corpse bookkeeping.
    void tick(const combat::Arena& arena, const combat::Horde& horde, double horizontal_fov_deg, double aspect);
    // Once per rendered frame, after WeaponFx::build(arena, fov, aspect, frame): poses the visible
    // creatures, fills the instance lists and adds the EFX quads and lights to `frame`.
    void build(const combat::Arena& arena, const combat::Horde& horde, const BuildOptions& options, Frame& frame, EnemyFrame& out);

    // Pieces, public for tests.
    const Pose& pose(const combat::Target& enemy, const combat::Arena& arena);   // enemyEvalPose at the arena's clock (b._pose)
    const Pose& marine_pose(const combat::Arena& arena);                         // the local player as a Marine Cat
    void marine_event(double shoot_until, double melee_until, double hurt_until) { marine_shoot_ = shoot_until; marine_melee_ = melee_until; marine_hurt_ = hurt_until; }
    // Staged scenes: the animation part of a body (enemyStartDeath with a given variant and side) for
    // the corpse `corpse_id` of the Horde. `at_death` is the enemy as it was when it died.
    void stage_body(std::uint32_t corpse_id, int type, const PoseInput& at_death, double time, int variant, double side, double flash);
    combat::Rng& random_stream() { return rng_; }
    const std::vector<Body>& bodies() const { return bodies_; }
    const combat::Rng& rng() const { return rng_; }
    std::size_t animated() const { return anim_.size(); }
private:
    struct Live { AnimState anim; Pose pose{}; bool posed{}; double efx_p{-1}; std::uint64_t frame{}; };
    void start_death(const combat::DeathEvent& e);
    void gibs(double x, double y, double z, double scale);
    void shot_impact(const combat::ShotImpact& s);
    void update(const combat::Arena& arena, const combat::Horde& horde, double dt);   // EFX.update
    void marine(AnimState& anim, Pose& pose, double x, double y, double z, double a, double now, double shoot_until, double melee_until, double hurt_until, bool downed);
    void build_fx(const combat::Arena& arena, const combat::Horde& horde, const BuildOptions& options, Frame& frame);   // EFX.build

    WeaponFx* fx_;
    combat::Rng rng_;
    std::unordered_map<std::uint32_t, Live> anim_;        // by enemy uid
    std::unordered_map<std::uint32_t, double> trail_;     // enemy shot id -> time to the next trail puff (s._ft)
    std::vector<Body> bodies_;
    AnimState marine_anim_;
    Pose marine_pose_{}, scratch_{};
    double marine_shoot_{}, marine_melee_{}, marine_hurt_{};
    std::unordered_map<std::uint32_t, std::pair<AnimState, Pose>> guests_;
    std::uint64_t frame_{};
};
}  // namespace cat::fx
