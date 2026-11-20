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
typedef struct {
	const char *name;
	int   damage;       /* per pellet / hit */
	int   delayMs;      /* time between shots */
	int   clip;         /* magazine size */
	int   reloadMs;
	int   pellets;
	float spread;       /* N10 spread (half-width factor, radians) */
	float rangeUnits;   /* N10 range in metres, converted to Quake III units */
} caWeapon_t;

/* The Cat Arena weapon that occupies a Quake III weapon slot, or NULL. Stage C: the NAILGUN in
 * WP_MACHINEGUN (2). Shared by qagame and cgame (bg_pmove uses it for rate, clip and reload). */
const caWeapon_t *CA_WeaponForSlot( int q3weapon );

#ifdef __cplusplus
}
#endif

#endif /* CA_BRIDGE_H */
