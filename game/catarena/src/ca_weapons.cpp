// Cat Arena: Feral Claw — weapon table (N10 native/src/combat.cpp kWeapons = legacy WEAPONS 5140-5156)
// and projectile constants (N10 cat/combat.hpp), exposed to the C game code through ca_bridge.h.
// N10-Q3E stage C (NAILGUN), stage D part 3 (all 16). GPL v2 or later.
#include "ca_bridge.h"

#include <array>
#include <cmath>
#include <utility>

namespace {

enum Kind { hitscan = CA_FIRE_HITSCAN, rocket = CA_FIRE_ROCKET, grenade = CA_FIRE_GRENADE, rift = CA_FIRE_RIFT };
struct Def {
    const char* name;
    double damage, delay;
    int clip;
    double reload;
    int pellets;
    double spread, range_m;
    Kind kind;
    bool shotgun, rail, lightning;
    unsigned color;
};
// The 16 N10 weapons in the legacy order (the index is the weapon id everywhere).
constexpr std::array<Def, CA_WEAPON_COUNT> kN10{{
    {"NAILGUN", 13, .09, 50, 1.35, 1, .022, 27, hitscan, false, false, false, 0xa6c6ff},
    {"SHOTGUN", 18, .72, 8, 1.8, 8, .14, 13, hitscan, true, false, false, 0xffc27e},
    {"GRENADE LAUNCHER", 108, .78, 6, 2.0, 1, .01, 24, grenade, false, false, false, 0xd0ef84},
    {"RAILGUN", 132, .98, 7, 2.1, 1, 0, 100, hitscan, false, true, false, 0xf0b2ff},
    {"ROCKET LAUNCHER", 95, .86, 6, 2.1, 1, .006, 33, rocket, false, false, false, 0xfba17a},
    {"SUPER NAILGUN", 20, .13, 36, 1.65, 1, .030, 31, hitscan, false, false, false, 0x8df4ab},
    {"THUNDERBOLT", 15, .075, 75, 1.7, 1, 0, 20, hitscan, false, false, true, 0x7deaf4},
    {"SUPER SHOTGUN", 16, .88, 12, 2.0, 14, .19, 14, hitscan, true, false, false, 0xffd29a},
    {"PLASMA REPEATER", 34, .19, 48, 1.7, 1, .007, 50, hitscan, false, false, false, 0x8cf7b7},   // hitscan in N10 despite its label
    {"TRIPLE SPIKE", 13, .34, 36, 1.55, 3, .045, 36, hitscan, false, false, false, 0xf1a56d},
    {"RIFT CANNON", 125, 1.15, 5, 2.4, 1, 0, 36, rift, false, false, false, 0xc1a3ff},
    {"IRON CHAINGUN", 10, .065, 100, 2.1, 1, .06, 70, hitscan, false, false, false, 0xd0bc91},
    {"HAND CANNON", 78, .82, 7, 2.0, 1, .008, 95, hitscan, false, false, false, 0xf4bd77},
    {"HELLFIRE CARBINE", 37, .28, 28, 1.7, 1, .018, 42, hitscan, false, false, false, 0xf98e4c},  // hitscan in N10 despite its label
    {"SAWBLADE DRIVER", 39, .38, 22, 1.9, 1, .02, 54, hitscan, false, false, false, 0xb8cfd1},
    {"QUAD SHOTGUN", 14, 1.15, 8, 2.5, 20, .225, 17, hitscan, true, false, false, 0xe7bb81},
}};

constexpr caWeapon_t convert(const Def& d) {
    return caWeapon_t{d.name,
                      int(d.damage + .5),
                      int(d.delay * 1000 + .5),
                      d.clip,
                      int(d.reload * 1000 + .5),
                      d.pellets,
                      float(d.spread),
                      float(d.range_m * CA_UNITS_PER_METRE),
                      int(d.kind),
                      d.shotgun ? 1 : 0,
                      d.rail ? 1 : 0,
                      d.lightning ? 1 : 0,
                      {float((d.color >> 16) & 255) / 255.0F, float((d.color >> 8) & 255) / 255.0F, float(d.color & 255) / 255.0F}};
}

template <std::size_t... I>
constexpr std::array<caWeapon_t, CA_WEAPON_COUNT> convert_all(std::index_sequence<I...>) { return {{convert(kN10[I])...}}; }
constexpr auto kWeapons = convert_all(std::make_index_sequence<CA_WEAPON_COUNT>{});
static_assert(kWeapons[0].delayMs == 90 && kWeapons[0].clip == 50 && kWeapons[0].reloadMs == 1350 && kWeapons[0].damage == 13);
static_assert(kWeapons[15].pellets == 20 && kWeapons[15].delayMs == 1150 && kWeapons[6].delayMs == 75);

// N10: rocket speed 26 m/s, life 2.6 s, power 1.15, radius .14 m; grenade 22 m/s + 1.5 m/s lift,
// gravity 15 m/s^2, life 2.3 s, power 1.08, radius .15 m; rift 19 m/s, life 2.6 s, power 1.42,
// radius .23 m. Every projectile explodes on its first contact or at the end of its life.
constexpr float U = CA_UNITS_PER_METRE;
constexpr caProjectile_t kRocket{26 * U, 0, 0, 2600, 1.15F, .14F * U};
constexpr caProjectile_t kGrenade{22 * U, 1.5F * U, 15 * U, 2300, 1.08F, .15F * U};
constexpr caProjectile_t kRift{19 * U, 0, 0, 2600, 1.42F, .23F * U};

}  // namespace

extern "C" const caWeapon_t* CA_Weapon(int index) {
    return index >= 0 && index < CA_WEAPON_COUNT ? &kWeapons[static_cast<std::size_t>(index)] : nullptr;
}

extern "C" const caProjectile_t* CA_Projectile(int kind) {
    switch(kind) {
    case CA_FIRE_ROCKET: return &kRocket;
    case CA_FIRE_GRENADE: return &kGrenade;
    case CA_FIRE_RIFT: return &kRift;
    default: return nullptr;
    }
}

// applyShotgunRecoil (legacy 11992): force 4.7 / 5.7 / 6.7 m/s (SHOTGUN / SUPER / QUAD); in the
// air 55 % of it, and the vertical speed raised to at least 7.3 / 7.8 / 8.2 m/s.
extern "C" float CA_ShotgunRecoil(int index) {
    return float((index == 15 ? 6.7 : index == 7 ? 5.7 : 4.7) * U);
}
extern "C" float CA_ShotgunAirLift(int index) {
    return float((index == 15 ? 8.2 : index == 7 ? 7.8 : 7.3) * U);
}
