// Cat Arena: Feral Claw — weapon table (N10 native/src/combat.cpp kWeapons = legacy WEAPONS 5140-5156),
// exposed to the C game code through ca_bridge.h. N10-Q3E stage C. GPL v2 or later.
#include "ca_bridge.h"

#include <array>
#include <cmath>

namespace {

struct Def {
    const char* name;
    double damage, delay;
    int clip;
    double reload;
    int pellets;
    double spread, range_m;
};
// All 16 N10 weapons; stage C wires the first one (NAILGUN) into Quake III's machinegun slot.
constexpr std::array<Def, 16> kN10{{
    {"NAILGUN", 13, .09, 50, 1.35, 1, .022, 27},
    {"SHOTGUN", 18, .72, 8, 1.8, 8, .14, 13},
    {"GRENADE LAUNCHER", 108, .78, 6, 2.0, 1, .01, 24},
    {"RAILGUN", 132, .98, 7, 2.1, 1, 0, 100},
    {"ROCKET LAUNCHER", 95, .86, 6, 2.1, 1, .006, 33},
    {"SUPER NAILGUN", 20, .13, 36, 1.65, 1, .030, 31},
    {"THUNDERBOLT", 15, .075, 75, 1.7, 1, 0, 20},
    {"SUPER SHOTGUN", 16, .88, 12, 2.0, 14, .19, 14},
    {"PLASMA REPEATER", 34, .19, 48, 1.7, 1, .007, 50},
    {"TRIPLE SPIKE", 13, .34, 36, 1.55, 3, .045, 36},
    {"RIFT CANNON", 125, 1.15, 5, 2.4, 1, 0, 36},
    {"IRON CHAINGUN", 10, .065, 100, 2.1, 1, .06, 70},
    {"HAND CANNON", 78, .82, 7, 2.0, 1, .008, 95},
    {"HELLFIRE CARBINE", 37, .28, 28, 1.7, 1, .018, 42},
    {"SAWBLADE DRIVER", 39, .38, 22, 1.9, 1, .02, 54},
    {"QUAD SHOTGUN", 14, 1.15, 8, 2.5, 20, .225, 17},
}};

constexpr caWeapon_t convert(const Def& d) {
    return caWeapon_t{d.name,
                      int(d.damage + .5),
                      int(d.delay * 1000 + .5),
                      d.clip,
                      int(d.reload * 1000 + .5),
                      d.pellets,
                      float(d.spread),
                      float(d.range_m * CA_UNITS_PER_METRE)};
}

constexpr caWeapon_t kNailgun = convert(kN10[0]);
static_assert(kNailgun.delayMs == 90 && kNailgun.clip == 50 && kNailgun.reloadMs == 1350 && kNailgun.damage == 13);

constexpr int kSlotMachinegun = 2;   // WP_MACHINEGUN in bg_public.h

}  // namespace

extern "C" const caWeapon_t* CA_WeaponForSlot(int q3weapon) {
    return q3weapon == kSlotMachinegun ? &kNailgun : nullptr;
}
