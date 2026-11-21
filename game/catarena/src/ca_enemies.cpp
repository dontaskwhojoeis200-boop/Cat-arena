// Cat Arena: Feral Claw — the 20 N10 enemy types and their projectiles, exposed to the C game code
// (qagame and cgame) through ca_bridge.h. N10-Q3E stage D part 4. GPL v2 or later.
//
// Source of every number: N10 native/src/enemies.cpp (kTypes = legacy ENEMY_TYPES 13225, update()
// attack timings 12236-12305, enemyAttack 11955), native/src/combat.cpp (enemyHitBoxes 11530,
// enemyHitScale) and the generated legacy table native/include/cat/generated/enemy_tables.inc
// (EFX_TYPE colours). Metres are converted at CA_UNITS_PER_METRE.
#include "ca_bridge.h"

#include <array>
#include <cstddef>
#include <utility>

namespace gen {
#include "cat/generated/enemy_tables.inc"
}

namespace {

constexpr double U = CA_UNITS_PER_METRE;
constexpr double kSlow = 4.617, kStandard = 8.80875, kFast = 10.3275;   // kEnemySlowSpeed / Standard / Fast
enum Cls { S = 0, H = 1, F = 2 };
enum Shot { shot = CA_SHOT_BLASTER, spike = CA_SHOT_SPIKE, fireball = CA_SHOT_FIREBALL, voidorb = CA_SHOT_VOIDORB };

struct Def {
    const char* name;
    double hp, speed;
    bool flying;
    double flying_attack;
    Cls cls;
    bool ranged, slam, orb, warden;
    Shot shot;
    int nav_profile;
    double turn_rate, stride, melee;
    float palette[3];
};
// N10 kTypes, same order and columns (stride is animation, kept for reference).
constexpr std::array<Def, CA_ENEMY_TYPES> kN10{{
    {"grunt", 85, kStandard, false, 0, S, true, false, false, false, shot, 1, 4.0, 1.02, 11, {.42F, .54F, .31F}},
    {"ogre", 125, kStandard, false, 0, H, true, false, false, false, fireball, 2, 2.7, 1.02, 11, {.71F, .68F, .62F}},
    {"fiend", 100, kFast, false, 0, F, false, false, false, false, shot, 0, 5.2, 1.20, 17, {.73F, .43F, .37F}},
    {"shambler", 265, kSlow, false, 0, H, false, true, false, false, shot, 2, 2.7, .86, 27, {.88F, .84F, .79F}},
    {"marauder", 140, kStandard, false, 0, S, true, false, false, false, spike, 1, 4.0, 1.02, 11, {.40F, .42F, .31F}},
    {"warden", 235, kSlow, false, 0, H, true, false, true, true, voidorb, 2, 2.7, 1.02, 11, {.51F, .51F, .60F}},
    {"skitter", 73, kFast, false, 0, F, false, false, false, false, shot, 0, 5.2, 1.02, 11, {.48F, .33F, .34F}},
    {"stalker", 78, kFast, false, 0, F, false, false, false, false, shot, 0, 5.2, 1.20, 11, {.36F, .45F, .34F}},
    {"brute", 200, kSlow, false, 0, H, false, true, false, false, shot, 2, 2.7, 1.02, 27, {.51F, .43F, .34F}},
    {"pyromancer", 105, kStandard, false, 0, S, true, false, false, false, fireball, 1, 4.0, 1.02, 11, {.68F, .28F, .20F}},
    {"sentinel", 155, kStandard, false, 0, S, true, false, true, true, voidorb, 1, 4.0, 1.02, 11, {.28F, .41F, .51F}},
    {"reaver", 115, kFast, false, 0, F, false, false, false, false, shot, 0, 5.2, 1.02, 17, {.64F, .47F, .30F}},
    {"banshee", 92, kFast, false, 0, F, true, false, true, false, voidorb, 0, 4.0, 1.02, 11, {.43F, .51F, .66F}},
    {"crusher", 270, kSlow, false, 0, H, false, true, false, false, shot, 2, 2.7, .86, 27, {.52F, .48F, .46F}},
    {"sapper", 110, kStandard, false, 0, S, true, false, false, false, spike, 1, 4.0, 1.02, 11, {.39F, .50F, .29F}},
    {"harrier", 92, kFast, false, 0, F, true, false, false, false, spike, 0, 5.2, 1.02, 11, {.62F, .54F, .44F}},
    {"cultist", 125, kStandard, false, 0, S, true, false, false, false, fireball, 1, 4.0, 1.02, 11, {.51F, .30F, .45F}},
    {"skyprobe", 78, 7.35, true, 7, F, true, false, false, false, shot, 1, 5.2, 1.02, 11, {.40F, .70F, .88F}},
    {"skyraider", 135, 11.35, true, 12, F, true, false, false, false, spike, 1, 5.2, 1.02, 11, {.75F, .30F, .36F}},
    {"skyjuggernaut", 260, 5.85, true, 24, H, true, false, false, false, voidorb, 1, 2.7, 1.02, 11, {.62F, .49F, .80F}},
}};

constexpr caEnemyType_t convert(const Def& d, std::size_t i) {
    // enemyHitScale (w, h) and enemyHitBoxes: body x +-.39w, y +-.34w, z .02 .. 1.24h; head x +-.62w,
    // y -.48w .. +.49w, z 1.06h .. 2.18h (metres from the feet). Quake III has one box per entity:
    // their union.
    const double w = d.cls == H ? 1.22 : d.cls == F ? .86 : 1.0;
    const double h = d.cls == H ? 1.10 : d.cls == F ? .90 : 1.0;
    const auto& look = gen::kEnemyLook[i].color;
    return caEnemyType_t{
        d.name,
        int(d.hp),
        float(d.speed * U),
        d.flying ? 1 : 0,
        int(d.flying_attack),
        int(d.cls),
        d.ranged ? 1 : 0, d.slam ? 1 : 0, d.orb ? 1 : 0, d.warden ? 1 : 0,
        int(d.shot),
        d.nav_profile,
        float(d.turn_rate),
        int(d.melee),
        {d.palette[0], d.palette[1], d.palette[2]},
        {float(look[0]), float(look[1]), float(look[2])},
        d.flying ? 660 : d.slam ? 880 : d.orb ? 730 : d.ranged ? 610 : 430,
        d.flying ? 320 : d.slam ? 390 : 290,
        d.flying ? 1950 : d.slam ? 2180 : d.warden ? 2400 : d.ranged ? 1720 : 1140,
        float((d.ranged ? 28 : d.slam ? 2.5 : 1.9) * U),
        float((d.slam ? 2.2 : 1.55) * U),
        {float(-.62 * w * U), float(-.48 * w * U), float(.02 * U - 24)},
        {float(.62 * w * U), float(.49 * w * U), float(2.18 * h * U - 24)},
    };
}
template <std::size_t... I>
constexpr std::array<caEnemyType_t, CA_ENEMY_TYPES> convert_all(std::index_sequence<I...>) { return {{convert(kN10[I], I)...}}; }
constexpr auto kTypes = convert_all(std::make_index_sequence<CA_ENEMY_TYPES>{});
static_assert(kTypes[2].health == 100 && kTypes[2].windMs == 430 && kTypes[2].coolMs == 1140);
static_assert(kTypes[3].windMs == 880 && kTypes[5].coolMs == 2400 && kTypes[17].windMs == 660 && kTypes[0].coolMs == 1720);

// enemyAttack: speed 21 / 22 / 14.5 / 16.2 m/s, lead .42 / .42 / .34 / .38, sweep radius .08 / .08 /
// .16 / .21 m, damage 9 / 6 / 4 / 19, muzzle (forward, side, height) .50 .12 1.18 / .53 .15 1.20 /
// .48 .17 1.34 / .50 .16 1.58 m.
constexpr std::array<caEnemyShot_t, 4> kShots{{
    {float(21 * U), float(.08 * U), .42F, 9, float(1.18 * U), float(.50 * U), float(.12 * U)},
    {float(22 * U), float(.08 * U), .42F, 6, float(1.20 * U), float(.53 * U), float(.15 * U)},
    {float(14.5 * U), float(.16 * U), .34F, 4, float(1.34 * U), float(.48 * U), float(.17 * U)},
    {float(16.2 * U), float(.21 * U), .38F, 19, float(1.58 * U), float(.50 * U), float(.16 * U)},
}};

}  // namespace

extern "C" const caEnemyType_t* CA_EnemyType(int type) {
    return type >= 0 && type < CA_ENEMY_TYPES ? &kTypes[static_cast<std::size_t>(type)] : nullptr;
}
extern "C" const caEnemyShot_t* CA_EnemyShot(int kind) {
    return kind >= 0 && kind < 4 ? &kShots[static_cast<std::size_t>(kind)] : nullptr;
}
