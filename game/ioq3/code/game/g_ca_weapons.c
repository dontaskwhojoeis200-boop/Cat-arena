/*
 * Cat Arena: Feral Claw — server side of the 16 N10 weapons (N10-Q3E stage D part 3).
 *
 * Ported from N10 native/src/combat.cpp (= legacy fireWeapon 12054, shotSweep 11829, damageBot
 * 11585, rerollOne 11593, rollLoadout 11592, explode 12015 and the projectile loop of update()
 * 12308-12343), on Quake III traces and entities:
 *   - every player carries two N10 weapons (stats[STAT_CA_LOADOUT]) in the WP_MACHINEGUN slot;
 *     rate, magazine, reload, switch and shotgun recoil are predicted in bg_pmove.c;
 *   - hitscan weapons resolve every pellet on the firing frame: N10 spread, range and sweep
 *     radius, head hits (upper third of the enemy box) count double, players do not block shots;
 *   - rockets, grenades and rift bolts are missiles with the N10 speed, gravity and life; each
 *     explodes on its first contact or at the end of its life; the blast damages enemies and
 *     players by distance to their boxes (N10 radii and damage, no line-of-sight test, as N10);
 *     a player who survives a blast is launched (N10: up to 2.25 player heights, 8 m/s outwards);
 *   - a frag replaces the weapon in hand with a different random one (rerollOne); every spawn
 *     rolls a new pair of different weapons (rollLoadout).
 *
 * New file, not part of the ioquake3 import. GPL v2 or later.
 */
#include "g_local.h"

#ifdef CAT_ARENA_STANDALONE

#define CA_MUZZLE_OFFSET  ( 0.24f * CA_UNITS_PER_METRE )   /* N10 firePosition: camera + 0.24 m */
#define CA_PITCH_LIMIT    85.37f                           /* N10 clamp +-1.49 rad */
#define CA_HEAD_FRACTION  0.66f                            /* N10 damageBot: head from 66 % of the height */

/* ---- loadout ------------------------------------------------------------------------------ */
static void CA_SetSlotWeapon( playerState_t *ps, int slot, int weapon ) {
	int lo = ps->stats[STAT_CA_LOADOUT];
	int shift = slot ? 4 : 0;
	lo = ( lo & ~( 15 << shift ) ) | ( ( weapon & 15 ) << shift );
	ps->stats[STAT_CA_LOADOUT] = lo;
}

/* rollLoadout: two different random weapons, slot 0 in hand, both magazines full. The cvar
   ca_loadout "a b" fixes the pair (tests); empty = random. */
void G_CA_GiveLoadout( gclient_t *client ) {
	playerState_t *ps = &client->ps;
	char buf[32];
	int a = -1, b = -1;
	trap_Cvar_VariableStringBuffer( "ca_loadout", buf, sizeof( buf ) );
	if ( buf[0] ) {
		char *p = buf;
		const char *t = COM_Parse( &p );
		a = t[0] ? atoi( t ) : -1;
		t = COM_Parse( &p );
		b = t[0] ? atoi( t ) : -1;
	}
	if ( a < 0 || a >= CA_WEAPON_COUNT || b < 0 || b >= CA_WEAPON_COUNT ) {
		a = rand() % CA_WEAPON_COUNT;
		b = rand() % ( CA_WEAPON_COUNT - 1 );
		if ( b >= a ) {
			b++;
		}
	}
	ps->stats[STAT_CA_LOADOUT] = CA_LO_MAKE( a, b );
	ps->stats[STAT_CA_CLIP] = CA_Weapon( a )->clip;
	ps->stats[STAT_CA_CLIP2] = CA_Weapon( b )->clip;
	ps->weaponstate = WEAPON_READY;
	ps->weaponTime = 200;   /* rollLoadout: cooldown 0.2 s */
}

/* rerollOne: a frag replaces the weapon in hand with a different random one (full magazine). */
void G_CA_Reroll( gentity_t *player, int forced ) {
	playerState_t *ps;
	int slot, old, next;
	if ( !player || !player->client ) {
		return;
	}
	ps = &player->client->ps;
	slot = CA_LO_ACTIVE_SLOT( ps->stats[STAT_CA_LOADOUT] );
	old = CA_LO_IN_HAND( ps->stats[STAT_CA_LOADOUT] );
	if ( forced >= 0 && forced < CA_WEAPON_COUNT ) {
		next = forced;
	} else {
		next = rand() % ( CA_WEAPON_COUNT - 1 );
		if ( next >= old ) {
			next++;
		}
	}
	G_Printf( "Cat Arena: %s %s -> %s\n", forced >= 0 ? "give" : "frag reroll", CA_Weapon( old )->name, CA_Weapon( next )->name );
	CA_SetSlotWeapon( ps, slot, next );
	ps->stats[STAT_CA_CLIP] = CA_Weapon( next )->clip;
	ps->weaponstate = WEAPON_READY;   /* a reroll ends a reload */
	ps->weaponTime = CA_REROLL_MS;
	G_AddEvent( player, EV_CA_REROLL, next );
}

/* ---- hitscan ------------------------------------------------------------------------------ */
static qboolean CA_IsHead( const gentity_t *enemy, const vec3_t point ) {
	float height = enemy->r.maxs[2] - enemy->r.mins[2];
	float rel = point[2] - ( enemy->r.currentOrigin[2] + enemy->r.mins[2] );
	return rel >= height * CA_HEAD_FRACTION;
}

/* One pellet from `start` along `dir` (N10 shotSweep: the first wall or enemy within range;
   players are not obstacles). Returns the damage dealt. */
static int CA_Pellet( int shooterNum, const vec3_t start, const vec3_t dir, const caWeapon_t *w, int index ) {
	vec3_t end, from, mins, maxs;
	trace_t tr;
	int passent = shooterNum, i, result = 0, dealt = 0;
	float r = ( w->pellets > 1 ? 0.025f : 0.042f ) * CA_UNITS_PER_METRE;
	gentity_t *te;

	VectorSet( mins, -r, -r, -r );
	VectorSet( maxs, r, r, r );
	VectorMA( start, w->rangeUnits, dir, end );
	VectorCopy( start, from );
	tr.fraction = 1.0f;
	VectorCopy( end, tr.endpos );
	tr.entityNum = ENTITYNUM_NONE;
	for ( i = 0 ; i < 8 ; i++ ) {
		trap_Trace( &tr, from, mins, maxs, end, passent, MASK_SHOT );
		if ( tr.fraction < 1.0f && tr.entityNum < MAX_CLIENTS && !tr.startsolid ) {
			passent = tr.entityNum;   /* through players */
			VectorCopy( tr.endpos, from );
			continue;
		}
		break;
	}
	if ( tr.fraction < 1.0f && tr.entityNum < ENTITYNUM_MAX_NORMAL ) {
		gentity_t *hit = &g_entities[ tr.entityNum ];
		if ( hit->s.eType == ET_CA_ENEMY && hit->takedamage ) {
			qboolean head = CA_IsHead( hit, tr.endpos );
			dealt = w->damage * ( head ? 2 : 1 );
			result = head ? 3 : 2;
			G_Damage( hit, &g_entities[ shooterNum ], &g_entities[ shooterNum ], (float *)dir, tr.endpos, dealt, DAMAGE_NO_KNOCKBACK, MOD_MACHINEGUN );
		} else {
			result = 1;
		}
	} else if ( tr.fraction < 1.0f ) {
		result = ( tr.surfaceFlags & SURF_NOIMPACT ) ? 0 : 1;
	}
	te = G_TempEntity( tr.endpos, EV_CA_SHOT );
	VectorCopy( start, te->s.origin2 );
	te->s.eventParm = index;
	te->s.generic1 = result;
	te->s.otherEntityNum = shooterNum;
	te->r.svFlags |= SVF_BROADCAST;   /* tracers and beams are seen from both ends */
	return dealt;
}

/* ---- projectiles -------------------------------------------------------------------------- */
static float CA_PointBoxDistance( const vec3_t p, const vec3_t mins, const vec3_t maxs ) {
	vec3_t d;
	int i;
	for ( i = 0 ; i < 3 ; i++ ) {
		d[i] = p[i] < mins[i] ? mins[i] - p[i] : p[i] > maxs[i] ? p[i] - maxs[i] : 0;
	}
	return VectorLength( d );
}

/* explode (12015): blast damage by distance to each box; enemies 95 x power over 3.35 m x power,
   players 38 x power over 3.25 m x power (rounded, at least 1) and a launch if they survive.
   Returns the total damage dealt to enemies (self-test). */
int G_CA_Blast( const vec3_t point, int kind, float power, gentity_t *attacker, gentity_t *inflictor ) {
	int i, total = 0;
	float splash = CA_ENEMY_SPLASH_RADIUS * power;
	float range = CA_PLAYER_SPLASH_RADIUS * power;
	gentity_t *te = G_TempEntity( (float *)point, EV_CA_BLAST );
	te->s.eventParm = kind;
	te->s.generic1 = (int)( power * 100 + 0.5f );
	te->r.svFlags |= SVF_BROADCAST;
	if ( !attacker ) {
		attacker = &g_entities[ ENTITYNUM_WORLD ];
	}
	if ( !inflictor ) {
		inflictor = attacker;
	}
	for ( i = 0 ; i < level.num_entities ; i++ ) {
		gentity_t *e = &g_entities[ i ];
		float d;
		if ( !e->inuse || !e->takedamage ) {
			continue;
		}
		if ( e->s.eType == ET_CA_ENEMY ) {
			int dmg;
			d = CA_PointBoxDistance( point, e->r.absmin, e->r.absmax );
			if ( d >= splash ) {
				continue;
			}
			dmg = (int)( CA_ENEMY_SPLASH_DAMAGE * power * ( 1 - d / splash ) + 0.5f );
			if ( dmg > 0 ) {
				total += dmg;
				G_Damage( e, inflictor, attacker, NULL, (float *)point, dmg, DAMAGE_RADIUS | DAMAGE_NO_KNOCKBACK, MOD_ROCKET_SPLASH );
			}
		} else if ( e->client && e->client->ps.stats[STAT_HEALTH] > 0 && e->client->sess.sessionTeam != TEAM_SPECTATOR ) {
			int dmg;
			playerState_t *ps = &e->client->ps;
			d = CA_PointBoxDistance( point, e->r.absmin, e->r.absmax );
			if ( d >= range ) {
				continue;
			}
			dmg = (int)( CA_PLAYER_SPLASH_DAMAGE * power * ( 1 - d / range ) + 0.5f );
			if ( dmg < 1 ) {
				dmg = 1;
			}
			G_Damage( e, inflictor, attacker, NULL, (float *)point, dmg, DAMAGE_RADIUS | DAMAGE_NO_KNOCKBACK, MOD_ROCKET_SPLASH );
			if ( e->health > 0 ) {
				/* N10: launch up to 2.25 player heights at full blast, 8 m/s outwards */
				float pct = ( range - d ) / ( range * 0.58f );
				float height = e->r.maxs[2] - e->r.mins[2];
				float launch, ax, ay, len;
				if ( pct > 1 ) {
					pct = 1;
				}
				if ( pct < 0 ) {
					pct = 0;
				}
				launch = sqrt( 2 * ps->gravity * height * CA_BLAST_HEIGHTS ) * pct;
				if ( ps->velocity[2] < launch ) {
					ps->velocity[2] = launch;
				}
				ps->groundEntityNum = ENTITYNUM_NONE;
				ax = ps->origin[0] - point[0];
				ay = ps->origin[1] - point[1];
				len = sqrt( ax * ax + ay * ay );
				if ( len < 0.025f * CA_UNITS_PER_METRE ) {
					float yaw = DEG2RAD( ps->viewangles[YAW] );
					ax = -cos( yaw );
					ay = -sin( yaw );
					len = 1;
				}
				ps->velocity[0] += ax / len * CA_BLAST_HORIZONTAL * pct;
				ps->velocity[1] += ay / len * CA_BLAST_HORIZONTAL * pct;
				ps->pm_time = 200;   /* no ground friction for a moment, as Quake III knockback */
				ps->pm_flags |= PMF_TIME_KNOCKBACK;
			}
		}
	}
	return total;
}

static void CA_ProjectileExplode( gentity_t *ent, const vec3_t point ) {
	const caProjectile_t *p = CA_Projectile( ent->s.otherEntityNum2 );
	gentity_t *owner = ( ent->r.ownerNum >= 0 && ent->r.ownerNum < ENTITYNUM_MAX_NORMAL ) ? &g_entities[ ent->r.ownerNum ] : NULL;
	if ( owner && !owner->inuse ) {
		owner = NULL;
	}
	G_CA_Blast( point, ent->s.otherEntityNum2, p ? p->power : 1.0f, owner, ent );
	G_FreeEntity( ent );
}

static void CA_ProjectileLifeEnd( gentity_t *ent ) {
	vec3_t here;
	VectorCopy( ent->r.currentOrigin, here );
	CA_ProjectileExplode( ent, here );
}

/* Called from G_RunMissile for every Cat Arena projectile. */
void G_CA_RunProjectile( gentity_t *ent ) {
	vec3_t origin, from;
	trace_t tr;
	int passent = ent->r.ownerNum, i;

	BG_EvaluateTrajectory( &ent->s.pos, level.time, origin );
	VectorCopy( ent->r.currentOrigin, from );
	for ( i = 0 ; i < 8 ; i++ ) {
		trap_Trace( &tr, from, ent->r.mins, ent->r.maxs, origin, passent, ent->clipmask );
		if ( tr.fraction < 1.0f && tr.entityNum < MAX_CLIENTS && !tr.startsolid ) {
			passent = tr.entityNum;   /* players do not stop projectiles (N10 shotSweep) */
			VectorCopy( tr.endpos, from );
			continue;
		}
		break;
	}
	if ( tr.startsolid || tr.allsolid ) {
		CA_ProjectileExplode( ent, ent->r.currentOrigin );
		return;
	}
	VectorCopy( tr.endpos, ent->r.currentOrigin );
	trap_LinkEntity( ent );
	if ( tr.fraction < 1.0f ) {
		if ( tr.surfaceFlags & SURF_NOIMPACT ) {
			G_FreeEntity( ent );   /* into the sky */
			return;
		}
		CA_ProjectileExplode( ent, tr.endpos );
		return;
	}
	G_RunThink( ent );   /* end of life */
}

static gentity_t *CA_LaunchProjectile( int shooterNum, const vec3_t start, const vec3_t dir, int index ) {
	const caWeapon_t *w = CA_Weapon( index );
	const caProjectile_t *p = CA_Projectile( w->kind );
	gentity_t *m = G_Spawn();
	m->classname = "ca_projectile";
	m->nextthink = level.time + p->lifeMs;
	m->think = CA_ProjectileLifeEnd;
	m->s.eType = ET_MISSILE;
	m->r.svFlags = SVF_USE_CURRENT_ORIGIN;
	m->s.weapon = WP_MACHINEGUN;
	m->s.generic1 = index;               /* weapon (colour, model) */
	m->s.otherEntityNum2 = w->kind;      /* CA_FIRE_* */
	m->r.ownerNum = shooterNum;
	m->parent = ( shooterNum >= 0 && shooterNum < ENTITYNUM_MAX_NORMAL ) ? &g_entities[ shooterNum ] : NULL;
	m->clipmask = MASK_SHOT;
	VectorSet( m->r.mins, -p->radius, -p->radius, -p->radius );
	VectorSet( m->r.maxs, p->radius, p->radius, p->radius );
	m->s.pos.trType = p->gravity > 0 ? TR_CA_GRAVITY : TR_LINEAR;
	m->s.pos.trTime = level.time;
	VectorCopy( start, m->s.pos.trBase );
	VectorScale( dir, p->speed, m->s.pos.trDelta );
	m->s.pos.trDelta[2] += p->lift;
	VectorCopy( start, m->r.currentOrigin );
	trap_LinkEntity( m );
	return m;
}

/* ---- firing ------------------------------------------------------------------------------- */
/* fireWeapon from an eye position and view angles: projectiles, or every pellet with the N10
   spread (yaw +-spread, pitch +-0.7 x spread). Returns the damage dealt to enemies by hitscan. */
int G_CA_FireFrom( int shooterNum, const vec3_t eye, const vec3_t viewangles, int index ) {
	const caWeapon_t *w = CA_Weapon( index );
	vec3_t dir, start, angles;
	int n, dealt = 0;
	if ( !w ) {
		return 0;
	}
	if ( w->kind != CA_FIRE_HITSCAN ) {
		AngleVectors( viewangles, dir, NULL, NULL );
		VectorMA( eye, CA_MUZZLE_OFFSET, dir, start );
		CA_LaunchProjectile( shooterNum, start, dir, index );
		return 0;
	}
	for ( n = 0 ; n < w->pellets ; n++ ) {
		VectorCopy( viewangles, angles );
		angles[YAW] += RAD2DEG( ( random() - 0.5f ) * w->spread * 2 );
		angles[PITCH] += RAD2DEG( ( random() - 0.5f ) * w->spread * 1.4f );
		if ( angles[PITCH] > CA_PITCH_LIMIT ) {
			angles[PITCH] = CA_PITCH_LIMIT;
		} else if ( angles[PITCH] < -CA_PITCH_LIMIT ) {
			angles[PITCH] = -CA_PITCH_LIMIT;
		}
		AngleVectors( angles, dir, NULL, NULL );
		VectorMA( eye, CA_MUZZLE_OFFSET, dir, start );
		dealt += CA_Pellet( shooterNum, start, dir, w, index );
	}
	return dealt;
}

/* EV_FIRE_WEAPON of a player: `index` is the weapon the predicted fire used (event parameter). */
void G_CA_FireWeapon( gentity_t *ent, int index ) {
	vec3_t eye;
	if ( !ent->client ) {
		return;
	}
	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	G_CA_FireFrom( ent->s.number, eye, ent->client->ps.viewangles, index );
}

/* ca_give <weapon> (cheat): the weapon in hand becomes N10 weapon <weapon> (0-15), full magazine. */
void Cmd_CA_Give_f( gentity_t *ent ) {
	char buf[MAX_TOKEN_CHARS];
	if ( !ent->client || trap_Argc() != 2 ) {
		return;
	}
	trap_Argv( 1, buf, sizeof( buf ) );
	G_CA_Reroll( ent, atoi( buf ) );
}

/* ---- self-test (ca_selftest, dedicated server) ---------------------------------------------- */
static int caWeaponFails;
static void CA_Check( qboolean ok, const char *what ) {
	G_Printf( "ca_selftest: %s %s\n", ok ? "PASS" : "FAIL", what );
	if ( !ok ) {
		caWeaponFails++;
	}
}

/* An eye point `dist` units from the fiend's centre at height `dz`, on the first of four sides
   with a clear line to it, and the view angles that look at the centre line. */
static qboolean CA_TestEye( const vec3_t center, float dist, float dz, vec3_t eye, vec3_t angles ) {
	int k;
	for ( k = 0 ; k < 4 ; k++ ) {
		trace_t tr;
		vec3_t target;
		float yaw = k * 90.0f;
		VectorSet( eye, center[0] - cos( DEG2RAD( yaw ) ) * dist, center[1] - sin( DEG2RAD( yaw ) ) * dist, center[2] + dz );
		VectorSet( target, center[0], center[1], center[2] + dz );
		trap_Trace( &tr, eye, NULL, NULL, target, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction >= 1.0f && !tr.startsolid ) {
			VectorSet( angles, 0, yaw, 0 );
			return qtrue;
		}
	}
	VectorSet( eye, center[0] - dist, center[1], center[2] + dz );
	VectorSet( angles, 0, 0, 0 );
	return qfalse;
}

/* Fires the real server weapon code at a fiend spawned on the map: body / head hitscan damage,
   a kill with the railgun, pellets of a shotgun, blast falloff, and a projectile's trajectory. */
void G_CA_WeaponSelfTest( int fiendNum ) {
	gentity_t *f;
	vec3_t eye, angles, center, point;
	int before, dealt, expect, i;
	caWeaponFails = 0;

	/* projectile constants against N10 (m -> u at 32 u/m) */
	CA_Check( CA_Projectile( CA_FIRE_ROCKET )->speed == 832.0f && CA_Projectile( CA_FIRE_GRENADE )->gravity == 480.0f
		&& CA_Projectile( CA_FIRE_RIFT )->lifeMs == 2600 && CA_Projectile( CA_FIRE_HITSCAN ) == NULL,
		"weapons: projectile constants (rocket 832 u/s, grenade gravity 480 u/s^2, rift life 2.6 s)" );
	{
		int kinds[4] = { 0, 0, 0, 0 };
		for ( i = 0 ; i < CA_WEAPON_COUNT ; i++ ) {
			kinds[ CA_Weapon( i )->kind ]++;
		}
		CA_Check( kinds[0] == 13 && kinds[1] == 1 && kinds[2] == 1 && kinds[3] == 1 && CA_Weapon( 16 ) == NULL,
			va( "weapons: 16 weapons, %d hitscan + rocket + grenade + rift", kinds[0] ) );
	}

	if ( fiendNum < 0 || !( f = &g_entities[ fiendNum ] )->inuse || f->s.eType != ET_CA_ENEMY ) {
		CA_Check( qfalse, "weapons: no fiend to fire at" );
		G_Printf( "ca_selftest: weapons FAILURES\n" );
		return;
	}
	VectorCopy( f->r.currentOrigin, center );

	/* body hit: NAILGUN from 3 m, aimed at the lower body (spread .022 rad cannot miss at 3 m) */
	CA_TestEye( center, 96, -10, eye, angles );
	f->health = 100;
	before = f->health;
	dealt = G_CA_FireFrom( ENTITYNUM_WORLD, eye, angles, 0 );
	CA_Check( dealt == 13 && before - f->health == 13, va( "weapons: NAILGUN body hit 13 (dealt %d)", dealt ) );

	/* head hit: HAND CANNON at the top of the box doubles 78 -> 156 */
	f->health = 1000;
	CA_TestEye( center, 96, f->r.maxs[2] - 4, eye, angles );
	dealt = G_CA_FireFrom( ENTITYNUM_WORLD, eye, angles, 12 );
	CA_Check( dealt == 156 && f->health == 1000 - 156, va( "weapons: HAND CANNON head hit x2 = 156 (dealt %d)", dealt ) );

	/* pellets: QUAD SHOTGUN at point blank, 20 pellets of 14 */
	f->health = 1000;
	CA_TestEye( center, 40, 0, eye, angles );
	dealt = G_CA_FireFrom( ENTITYNUM_WORLD, eye, angles, 15 );
	CA_Check( dealt >= 14 * 20 && dealt <= 28 * 20 && dealt % 14 == 0, va( "weapons: QUAD SHOTGUN 20 pellets at point blank (dealt %d)", dealt ) );

	/* range: RAILGUN reaches 100 m, THUNDERBOLT only 20 m */
	f->health = 1000;
	if ( CA_TestEye( center, 25 * CA_UNITS_PER_METRE, 0, eye, angles ) ) {
		dealt = G_CA_FireFrom( ENTITYNUM_WORLD, eye, angles, 6 );
		CA_Check( dealt == 0, "weapons: THUNDERBOLT does not reach 25 m (range 20 m)" );
		dealt = G_CA_FireFrom( ENTITYNUM_WORLD, eye, angles, 3 );
		CA_Check( dealt == 132, va( "weapons: RAILGUN hits at 25 m for 132 (dealt %d)", dealt ) );
	} else {
		G_Printf( "ca_selftest: (no clear 25 m line to the fiend on this map; range check skipped)\n" );
	}

	/* blast falloff: a rocket blast 1 m from the fiend's box: 95 x 1.15 x (1 - 32 / (107.2 x 1.15)) */
	f->health = 1000;
	VectorCopy( center, point );
	point[0] = f->r.absmax[0] + 1 * CA_UNITS_PER_METRE;
	{
		float d = CA_PointBoxDistance( point, f->r.absmin, f->r.absmax );
		float splash = CA_ENEMY_SPLASH_RADIUS * 1.15f;
		expect = (int)( CA_ENEMY_SPLASH_DAMAGE * 1.15f * ( 1 - d / splash ) + 0.5f );
	}
	dealt = G_CA_Blast( point, CA_FIRE_ROCKET, 1.15f, NULL, NULL );
	CA_Check( dealt == expect && f->health == 1000 - expect && expect == 81, va( "weapons: rocket blast 1 m away deals %d (expected %d)", dealt, expect ) );
	point[0] = f->r.absmax[0] + 4 * CA_UNITS_PER_METRE;
	dealt = G_CA_Blast( point, CA_FIRE_ROCKET, 1.15f, NULL, NULL );
	CA_Check( dealt == 0, "weapons: no blast damage beyond 3.35 m x power (4 m)" );
	point[0] = f->r.absmax[0] + 4 * CA_UNITS_PER_METRE;
	dealt = G_CA_Blast( point, CA_FIRE_RIFT, 1.42f, NULL, NULL );
	CA_Check( dealt > 0, va( "weapons: rift blast reaches 4 m (radius 4.76 m), dealt %d", dealt ) );

	/* grenade trajectory: N10 gravity 15 m/s^2 and lift 1.5 m/s */
	{
		gentity_t *g;
		vec3_t at, dir = { 1, 0, 0 };
		VectorSet( eye, 0, 0, 0 );
		g = CA_LaunchProjectile( ENTITYNUM_WORLD, eye, dir, 2 );
		BG_EvaluateTrajectory( &g->s.pos, level.time + 1000, at );
		CA_Check( fabs( at[0] - 704.0f ) < 0.5f && fabs( at[2] - ( 48.0f - 240.0f ) ) < 0.5f && g->nextthink == level.time + 2300,
			va( "weapons: grenade after 1 s at (%.1f, %.1f), life 2.3 s", at[0], at[2] ) );
		G_FreeEntity( g );
	}

	/* a kill: RAILGUN body hit on a 100 hp fiend */
	f->health = 100;
	CA_TestEye( center, 96, -10, eye, angles );
	dealt = G_CA_FireFrom( ENTITYNUM_WORLD, eye, angles, 3 );
	CA_Check( dealt == 132 && f->health <= 0 && !f->takedamage, "weapons: RAILGUN kills a 100 hp fiend in one body hit" );

	G_Printf( "ca_selftest: weapons %s (%d failed)\n", caWeaponFails ? "FAILURES" : "PASS", caWeaponFails );
}
int caWeaponFailsTotal( void ) { return caWeaponFails; }

#endif
