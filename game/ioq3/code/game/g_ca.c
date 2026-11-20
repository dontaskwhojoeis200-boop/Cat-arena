/*
 * Cat Arena: Feral Claw — C side of the server gameplay bridge (N10-Q3E stage C).
 * Implements caGameImport_t (ca_game.h) on top of the Quake III game code: enemy entities, their
 * movement through the shared Pmove (the same movement code players use), damage, traces, spawn
 * points and the wave configstring. The enemy and wave rules themselves are C++20
 * (game/catarena/src/ca_horde.cpp).
 *
 * New file, not part of the ioquake3 import. GPL v2 or later.
 */
#include "g_local.h"

#ifdef CAT_ARENA_STANDALONE
#include "ca_game.h"

static playerState_t caEnemyPS[MAX_GENTITIES];   /* movement state per enemy entity */
static vec3_t caSpawnSpots[64];
static int caSpawnCount;
static vmCvar_t ca_waves;   /* 0 holds the waves (movement tests); default 1 */

static const vec3_t caEnemyMins = { -15, -15, -24 };
static const vec3_t caEnemyMaxs = { 15, 15, 32 };

/* ---- map entity: info_ca_enemy_spawn ---------------------------------------------------- */
void SP_info_ca_enemy_spawn( gentity_t *ent ) {
	if ( caSpawnCount < ARRAY_LEN( caSpawnSpots ) ) {
		VectorCopy( ent->s.origin, caSpawnSpots[ caSpawnCount ] );
		caSpawnCount++;
	}
	G_FreeEntity( ent );
}

/* ---- imports ---------------------------------------------------------------------------- */
static void CA_Print( const char *text ) { G_Printf( "%s", text ); }
static int CA_LevelTime( void ) { return level.time; }

static int CA_Players( caPlayer_t *out, int max ) {
	int i, n = 0;
	for ( i = 0 ; i < level.maxclients && n < max ; i++ ) {
		gclient_t *cl = &level.clients[ i ];
		if ( cl->pers.connected != CON_CONNECTED || cl->sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}
		out[ n ].clientNum = i;
		VectorCopy( cl->ps.origin, out[ n ].origin );
		out[ n ].alive = cl->ps.stats[ STAT_HEALTH ] > 0 && cl->ps.pm_type != PM_DEAD;
		n++;
	}
	return n;
}

static int CA_SpawnPoints( float (*out)[3], int max ) {
	int n = 0;
	if ( caSpawnCount > 0 ) {
		for ( ; n < caSpawnCount && n < max ; n++ ) {
			VectorCopy( caSpawnSpots[ n ], out[ n ] );
		}
		return n;
	} else {
		gentity_t *spot = NULL;
		while ( n < max && ( spot = G_Find( spot, FOFS( classname ), "info_player_deathmatch" ) ) != NULL ) {
			VectorCopy( spot->s.origin, out[ n ] );
			n++;
		}
	}
	return n;
}

static void CA_EnemyDie( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod ) {
	if ( attacker && attacker->client ) {
		AddScore( attacker, self->r.currentOrigin, 1 );
	}
	CA_HordeEnemyKilled( self->s.number );
	self->takedamage = qfalse;
	self->r.contents = 0;
	self->s.eFlags |= EF_DEAD;
	self->s.generic1 = 0;
	self->think = G_FreeEntity;
	self->nextthink = level.time + 1500;   /* corpse visible briefly (topples in cgame) */
	trap_LinkEntity( self );
}

static void CA_EnemyPain( gentity_t *self, gentity_t *attacker, int damage ) {
	self->s.time2 = level.time;   /* hit flash in cgame */
}

static void CA_SyncEnemy( gentity_t *ent ) {
	playerState_t *ps = &caEnemyPS[ ent->s.number ];
	int eFlags = ent->s.eFlags, generic1 = ent->s.generic1, time2 = ent->s.time2;
	BG_PlayerStateToEntityState( ps, &ent->s, qtrue );
	ent->s.eType = ET_CA_ENEMY;
	ent->s.modelindex = CA_ENEMY_FIEND;
	ent->s.eFlags = eFlags;
	ent->s.generic1 = generic1;
	ent->s.time2 = time2;
	VectorCopy( ps->origin, ent->r.currentOrigin );
	trap_LinkEntity( ent );
}

static int CA_SpawnEnemy( int type, const float origin[3], float yaw, int health ) {
	gentity_t *ent;
	trace_t tr;
	vec3_t start;
	playerState_t *ps;

	VectorCopy( origin, start );
	start[2] += 9;   /* as SelectSpawnPoint does for players */
	trap_Trace( &tr, start, caEnemyMins, caEnemyMaxs, start, ENTITYNUM_NONE, MASK_PLAYERSOLID );
	if ( tr.startsolid || tr.allsolid ) {
		return -1;
	}
	ent = G_Spawn();
	ent->classname = "ca_enemy";
	ent->s.eType = ET_CA_ENEMY;
	ent->s.modelindex = type;
	ent->r.contents = CONTENTS_BODY;
	ent->clipmask = MASK_PLAYERSOLID;
	VectorCopy( caEnemyMins, ent->r.mins );
	VectorCopy( caEnemyMaxs, ent->r.maxs );
	ent->takedamage = qtrue;
	ent->health = health;
	ent->die = CA_EnemyDie;
	ent->pain = CA_EnemyPain;
	ent->s.eFlags = 0;

	ps = &caEnemyPS[ ent->s.number ];
	memset( ps, 0, sizeof( *ps ) );
	ps->clientNum = ent->s.number;    /* Pmove passes this as the entity to ignore in traces */
	ps->pm_type = PM_NORMAL;
	ps->stats[ STAT_HEALTH ] = health;
	ps->gravity = g_gravity.value;
	VectorCopy( start, ps->origin );
	ps->viewangles[ YAW ] = yaw;
	ps->commandTime = level.time;
	CA_SyncEnemy( ent );
	return ent->s.number;
}

static gentity_t *CA_Enemy( int entityNum ) {
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES ) {
		return NULL;
	}
	if ( !g_entities[ entityNum ].inuse || g_entities[ entityNum ].s.eType != ET_CA_ENEMY ) {
		return NULL;
	}
	return &g_entities[ entityNum ];
}

static void CA_RemoveEnemy( int entityNum ) {
	gentity_t *ent = CA_Enemy( entityNum );
	if ( ent ) {
		G_FreeEntity( ent );
	}
}

static int CA_EnemyHealth( int entityNum ) {
	gentity_t *ent = CA_Enemy( entityNum );
	return ent ? ent->health : 0;
}

static void CA_EnemyOrigin( int entityNum, float out[3] ) {
	gentity_t *ent = CA_Enemy( entityNum );
	if ( ent ) {
		VectorCopy( caEnemyPS[ entityNum ].origin, out );
	} else {
		VectorClear( out );
	}
}

static void CA_MoveEnemy( int entityNum, float yawDeg, float speedUnits, int moving, int msec ) {
	gentity_t *ent = CA_Enemy( entityNum );
	playerState_t *ps;
	pmove_t pm;
	if ( !ent || ent->health <= 0 || msec <= 0 ) {
		return;
	}
	ps = &caEnemyPS[ entityNum ];
	ps->speed = (int)speedUnits;
	ps->gravity = g_gravity.value;
	ps->stats[ STAT_HEALTH ] = ent->health;
	VectorClear( ps->delta_angles );

	memset( &pm, 0, sizeof( pm ) );
	pm.ps = ps;
	pm.cmd.serverTime = ps->commandTime + msec;
	pm.cmd.angles[ YAW ] = ANGLE2SHORT( yawDeg );
	pm.cmd.forwardmove = moving ? 127 : 0;
	pm.cmd.weapon = WP_NONE;
	pm.tracemask = MASK_PLAYERSOLID;
	pm.trace = trap_Trace;
	pm.pointcontents = trap_PointContents;
	pm.debugLevel = 0;
	pm.noFootsteps = qtrue;
	pm.pmove_fixed = qfalse;
	pm.pmove_msec = 8;
	Pmove( &pm );
	ps->eventSequence = 0;   /* enemy movement events (footsteps, landings) are not sent */
	CA_SyncEnemy( ent );
}

static void CA_SetEnemyState( int entityNum, int windingUp ) {
	gentity_t *ent = CA_Enemy( entityNum );
	if ( ent ) {
		ent->s.generic1 = windingUp ? 1 : 0;
	}
}

static int CA_Visible( int entityNum, int clientNum ) {
	trace_t tr;
	vec3_t from, to;
	if ( clientNum < 0 || clientNum >= level.maxclients ) {
		return 0;
	}
	VectorCopy( caEnemyPS[ entityNum ].origin, from );
	from[2] += 20;
	VectorCopy( level.clients[ clientNum ].ps.origin, to );
	to[2] += level.clients[ clientNum ].ps.viewheight;
	trap_Trace( &tr, from, NULL, NULL, to, entityNum, MASK_SOLID );
	return tr.fraction >= 1.0f;
}

static void CA_DamagePlayer( int clientNum, int damage, int attackerEntityNum ) {
	gentity_t *target, *attacker;
	vec3_t dir;
	if ( clientNum < 0 || clientNum >= level.maxclients ) {
		return;
	}
	target = &g_entities[ clientNum ];
	attacker = ( attackerEntityNum >= 0 && attackerEntityNum < MAX_GENTITIES ) ? &g_entities[ attackerEntityNum ] : &g_entities[ ENTITYNUM_WORLD ];
	VectorSubtract( target->r.currentOrigin, attacker->r.currentOrigin, dir );
	VectorNormalize( dir );
	G_Damage( target, attacker, attacker, dir, target->r.currentOrigin, damage, 0, MOD_UNKNOWN );
}

static void CA_DamageEnemy( int entityNum, int damage ) {
	gentity_t *ent = CA_Enemy( entityNum );
	if ( ent ) {
		G_Damage( ent, NULL, &g_entities[ ENTITYNUM_WORLD ], NULL, NULL, damage, DAMAGE_NO_KNOCKBACK, MOD_UNKNOWN );
	}
}

static void CA_SetWaveInfo( const char *configString ) {
	trap_SetConfigstring( CS_CA_WAVE, configString );
}

static const caGameImport_t caImports = {
	CA_Print, CA_LevelTime, CA_Players, CA_SpawnPoints, CA_SpawnEnemy, CA_RemoveEnemy,
	CA_EnemyHealth, CA_EnemyOrigin, CA_MoveEnemy, CA_SetEnemyState, CA_Visible,
	CA_DamagePlayer, CA_DamageEnemy, CA_SetWaveInfo
};

void G_CA_Init( void ) {
	trap_Cvar_Register( &ca_waves, "ca_waves", "1", 0 );
	CA_HordeInit( &caImports );
}

/* Called before G_SpawnEntitiesFromString so spawn points of the previous map are dropped. */
void G_CA_ResetSpawns( void ) {
	caSpawnCount = 0;
}

void G_CA_Frame( int msec ) {
	if ( level.intermissiontime ) {
		return;
	}
	trap_Cvar_Update( &ca_waves );
	if ( !ca_waves.integer ) {
		return;
	}
	CA_HordeFrame( msec );
}

void G_CA_Shutdown( void ) {
	CA_HordeShutdown();
}

/* ---- test commands ------------------------------------------------------------------------ */
void Svcmd_CA_SelfTest_f( void ) {
	CA_HordeSelfTest();
}

/* ca_state (cheat): one line with the player's movement and weapon state (scripted tests). */
void Cmd_CA_State_f( gentity_t *ent ) {
	const playerState_t *ps;
	if ( !ent->client ) {
		return;
	}
	ps = &ent->client->ps;
	trap_SendServerCommand( ent - g_entities, va( "print \"CA_STATE t=%d pos=%.0f,%.0f,%.0f hspeed=%.0f sliding=%d slide_ms=%d cooldown_ms=%d ducked=%d clip=%d reloading=%d health=%d score=%d weapon=%d wstate=%d wtime=%d pmflags=%d\n\"",
		level.time, ps->origin[0], ps->origin[1], ps->origin[2], sqrt( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] ),
		( ps->pm_flags & PMF_CA_SLIDING ) ? 1 : 0, ps->stats[STAT_CA_SLIDE], ps->stats[STAT_CA_SLIDE_COOLDOWN], ( ps->pm_flags & PMF_DUCKED ) ? 1 : 0,
		ps->stats[STAT_CA_CLIP], ps->weaponstate == WEAPON_RELOADING, ps->stats[STAT_HEALTH], ps->persistant[PERS_SCORE], ps->weapon, ps->weaponstate, ps->weaponTime, ps->pm_flags ) );
}

/* ca_place x y z yaw (cheat): put the player there at rest (setviewpos throws the player forward
   at 400 u/s like a teleporter exit, which spoils position tests). */
void Cmd_CA_Place_f( gentity_t *ent ) {
	char buf[MAX_TOKEN_CHARS];
	vec3_t origin, angles;
	int i;
	if ( !ent->client || trap_Argc() != 5 ) {
		trap_SendServerCommand( ent - g_entities, "print \"usage: ca_place x y z yaw\n\"" );
		return;
	}
	for ( i = 0 ; i < 3 ; i++ ) {
		trap_Argv( i + 1, buf, sizeof( buf ) );
		origin[i] = atof( buf );
	}
	trap_Argv( 4, buf, sizeof( buf ) );
	VectorSet( angles, 0, atof( buf ), 0 );
	VectorCopy( origin, ent->client->ps.origin );
	VectorClear( ent->client->ps.velocity );
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT;
	SetClientViewAngle( ent, angles );
	BG_PlayerStateToEntityState( &ent->client->ps, &ent->s, qtrue );
	VectorCopy( ent->client->ps.origin, ent->r.currentOrigin );
	trap_LinkEntity( ent );
}

/* ca_aimnearest (cheat): turn the player toward the nearest living enemy (scripted tests). */
void Cmd_CA_AimNearest_f( gentity_t *ent ) {
	int i, best = -1;
	float bestDist = 1e9f;
	vec3_t eye, dir, angles;
	if ( !ent->client ) {
		return;
	}
	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		gentity_t *e = &g_entities[ i ];
		float d;
		if ( !e->inuse || e->s.eType != ET_CA_ENEMY || e->health <= 0 ) {
			continue;
		}
		d = Distance( eye, e->r.currentOrigin );
		if ( d < bestDist ) {
			trace_t tr;
			trap_Trace( &tr, eye, NULL, NULL, e->r.currentOrigin, ent->s.number, MASK_SHOT );
			if ( tr.fraction < 1.0f && tr.entityNum != i ) {
				continue;	// not in sight
			}
			bestDist = d;
			best = i;
		}
	}
	if ( best < 0 ) {
		return;
	}
	VectorSubtract( g_entities[ best ].r.currentOrigin, eye, dir );
	dir[2] += 8;
	vectoangles( dir, angles );
	SetClientViewAngle( ent, angles );
}
#endif
