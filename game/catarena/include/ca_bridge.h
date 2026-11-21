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

/* ---- Cat Arena enemies (N10 native/src/enemies.cpp kTypes = legacy ENEMY_TYPES 13225) --------- */
#define CA_ENEMY_TYPES 20
#define CA_ENEMY_FIRST_FLYER 17          /* skyprobe, skyraider, skyjuggernaut */

/* Enemy projectile kinds (N10 ShotKind). */
#define CA_SHOT_BLASTER  0               /* 'shot' */
#define CA_SHOT_SPIKE    1
#define CA_SHOT_FIREBALL 2
#define CA_SHOT_VOIDORB  3

typedef struct {
	const char *name;
	int   health;          /* base, x 1.05^(wave-1) */
	float speed;           /* base, u/s, x 1.05^(wave-1) */
	int   flying;
	int   flyingDamage;    /* a flyer's shot */
	int   hitClass;        /* 0 standard, 1 heavy, 2 fast (enemyHitScale) */
	int   ranged, slam, orb, warden;
	int   shot;            /* CA_SHOT_* */
	int   navProfile;      /* 0 light, 1 standard, 2 heavy */
	float turnRate;        /* rad/s */
	int   meleeDamage;
	float palette[3];
	float lookColor[3];    /* projectile / casting colour (EFX_TYPE) */
	/* derived N10 rules */
	int   windMs;          /* attack wind-up: flyer .66, slam .88, orb .73, ranged .61, else .43 s */
	int   recoverMs;       /* recovery (animation only): flyer .32, slam .39, else .29 s */
	int   coolMs;          /* cooldown from the start of an attack: flyer 1.95, slam 2.18, warden 2.4, ranged 1.72, else 1.14 s */
	float attackStart;     /* u: ranged 28 m, slam 2.5 m, else 1.9 m */
	float meleeReach;      /* u: slam 2.2 m, else 1.55 m */
	/* hit box (N10 enemyHitBoxes, both boxes in one), Quake III units around origin = feet + 24 */
	float hitMins[3], hitMaxs[3];
} caEnemyType_t;
const caEnemyType_t *CA_EnemyType( int type );   /* NULL outside 0..19 */

typedef struct {
	float speed;           /* u/s */
	float radius;          /* u */
	float lead;            /* aim lead factor */
	int   damage;          /* ground enemies (flyers use flyingDamage), x wave scale */
	float originHeight;    /* muzzle height above the feet, u (ground enemies) */
	float originForward, originSide;   /* u */
} caEnemyShot_t;
const caEnemyShot_t *CA_EnemyShot( int kind );

#define CA_BURN_MS          3000         /* N10 igniteBurn: 2 damage every 0.5 s for 3 s */
#define CA_BURN_TICK_MS     500
#define CA_BURN_DAMAGE      2
#define CA_ENEMY_HIT_FLASH_MS 210        /* N10 hit_flash .21 s */
#define CA_DEATH_MS         6880         /* N10 ENEMY_DEATH fall .78 + stay 4.5 + sink 1.6 s */
#define CA_GIB_HEALTH       ( -40 )      /* N10: gibs below -40 health */

#ifdef __cplusplus
}
#endif

#endif /* CA_BRIDGE_H */
