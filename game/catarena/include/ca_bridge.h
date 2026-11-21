/*
 * Cat Arena: Feral Claw — C interface between the Quake III game code (C, GPL v2) and the Cat
 * Arena gameplay code (C++20). N10-Q3E stage B.
 *
 * Rules for everything declared here:
 *   - plain C types only, C linkage, callable from C game code;
 *   - no C++ exception ever leaves a function declared here (they are noexcept and catch);
 *   - no allocation ownership crosses the boundary (C++ owns its pools, C owns its arrays).
 *
 * Copyright (C) 2026 Cat Arena: Feral Claw. Licensed under the GNU GPL v2 or later, like the game
 * code it is linked with (docs/q3e/LICENSING.md).
 */
#ifndef CA_BRIDGE_H
#define CA_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Quake III units per metre. Cat Arena maps and gameplay values are authored in metres; the
 * stock Quake III movement (owner decision D1) runs in Quake III units. 32 u/m makes the 1.8 m
 * Cat Arena player 57.6 u tall against Quake III's 56 u player box. */
#define CA_UNITS_PER_METRE 32.0f

/* Build identification, e.g. "Cat Arena: Feral Claw N10-Q3E-B (qagame, C++20 bridge)". The
 * returned string is static and never freed. */
const char *CA_BuildString( const char *module );

/* Metres <-> Quake III units (CA_UNITS_PER_METRE). */
float CA_MetresToUnits( float metres );
float CA_UnitsToMetres( float units );

/* Self-check of the C/C++ boundary, run once at module start: returns 1 when the C++ side is
 * usable (layout assumptions hold, an exception thrown and caught inside C++ does not escape),
 * 0 otherwise. `report` receives a one-line description (may be NULL). */
int CA_SelfTest( void (*report)( const char *line ) );

/* ---- Cat Arena weapons (N10 table, legacy WEAPONS 5140-5156) -------------------------------- */
#define CA_WEAPON_COUNT 16

/* How a weapon fires (N10 FireKind). */
#define CA_FIRE_HITSCAN 0
#define CA_FIRE_ROCKET  1
#define CA_FIRE_GRENADE 2
#define CA_FIRE_RIFT    3

typedef struct {
	const char *name;
	int   damage;       /* per pellet / hit (explosives use the blast formula instead) */
	int   delayMs;      /* time between shots */
	int   clip;         /* magazine size */
	int   reloadMs;
	int   pellets;
	float spread;       /* N10 spread (half-width factor, radians) */
	float rangeUnits;   /* N10 range in metres, converted to Quake III units */
	int   kind;         /* CA_FIRE_* */
	int   shotgun;      /* N10 SHOTGUN_IDS {1, 7, 15}: recoil on firing, mid-air double jump */
	int   rail;         /* rail trail */
	int   lightning;    /* beam */
	float color[3];     /* N10 weapon colour (muzzle light, tracers, projectiles) */
} caWeapon_t;

/* Cat Arena weapon by N10 index (0..CA_WEAPON_COUNT-1), or NULL. Shared by qagame and cgame
 * (bg_pmove uses it for rate, clip, reload and shotgun recoil). */
const caWeapon_t *CA_Weapon( int index );

/* Projectile and blast constants (N10 cat/combat.hpp), converted to Quake III units. */
typedef struct {
	float speed;        /* u/s */
	float lift;         /* u/s added upwards at launch */
	float gravity;      /* u/s^2 (0 = straight line) */
	int   lifeMs;       /* explodes at the end of its life */
	float power;        /* blast power */
	float radius;       /* sweep radius (u) */
} caProjectile_t;
const caProjectile_t *CA_Projectile( int kind );   /* CA_FIRE_ROCKET / GRENADE / RIFT, else NULL */

#define CA_ENEMY_SPLASH_RADIUS   ( 3.35f * CA_UNITS_PER_METRE )   /* x power */
#define CA_ENEMY_SPLASH_DAMAGE   95.0f                            /* x power */
#define CA_PLAYER_SPLASH_RADIUS  ( 3.25f * CA_UNITS_PER_METRE )   /* x power */
#define CA_PLAYER_SPLASH_DAMAGE  38.0f                            /* x power, rounded, at least 1 */
#define CA_BLAST_HORIZONTAL      ( 8.0f * CA_UNITS_PER_METRE )    /* u/s at full blast */
#define CA_BLAST_HEIGHTS         2.25f                            /* launch height in player heights */

/* Shotgun recoil (N10 applyShotgunRecoil) for weapon `index`: horizontal push (u/s) and the
 * minimum upward speed in the air (u/s). */
float CA_ShotgunRecoil( int index );
float CA_ShotgunAirLift( int index );

#ifdef __cplusplus
}
#endif

#endif /* CA_BRIDGE_H */
