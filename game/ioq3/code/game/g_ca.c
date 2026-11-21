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
static struct {
	int type;          /* N10 enemy type */
	int uid;           /* changes with every spawn: the client's animation state follows it */
	int attackTime;    /* level time when the current attack wind-up began (0 = none yet) */
	float face;        /* facing, degrees */
} caEnemyInfo[MAX_GENTITIES];
static int caEnemyUid;
static vec3_t caSpawnSpots[64];
static int caSpawnCount;
static vmCvar_t ca_waves;   /* 0 holds the waves (movement tests); default 1 */
static vmCvar_t ca_log;     /* 1 prints enemy spawns, hits on players and kills (tests) */

int G_CA_Log( void ) { return ca_log.integer; }

/* ---- map entity: info_ca_enemy_spawn ---------------------------------------------------- */
void SP_info_ca_enemy_spawn( gentity_t *ent ) {
	if ( caSpawnCount < ARRAY_LEN( caSpawnSpots ) ) {
		VectorCopy( ent->s.origin, caSpawnSpots[ caSpawnCount ] );
		caSpawnCount++;
	}
	G_FreeEntity( ent );
}

/* ---- map entity: misc_ca_prop ------------------------------------------------------------ */
/* N10's animated MD3 props (The Arsenal): "model" flag or turret, "angle", "modelscale". The client
   draws and animates them (cg_ca_enemies.c CG_CA_Prop); the server only sends their placement. */
void SP_misc_ca_prop( gentity_t *ent ) {
	char *model;
	float scale;
	G_SpawnString( "model", "flag", &model );
	G_SpawnFloat( "modelscale", "1", &scale );
	ent->s.eType = ET_CA_PROP;
	ent->s.modelindex = !Q_stricmp( model, "turret" ) ? 1 : 0;
	ent->s.origin2[0] = scale;
	ent->s.number = ent - g_entities;
	G_SetOrigin( ent, ent->s.origin );
	VectorCopy( ent->s.angles, ent->s.apos.trBase );
	VectorSet( ent->r.mins, -80 * scale, -80 * scale, -40 * scale );
	VectorSet( ent->r.maxs, 80 * scale, 80 * scale, 80 * scale );
	ent->r.contents = 0;
	trap_LinkEntity( ent );
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
		VectorCopy( cl->ps.velocity, out[ n ].velocity );
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
		G_CA_Reroll( attacker, -1 );   /* N10 rerollOne: a frag replaces the weapon in hand */
	}
	if ( ca_log.integer ) {
		G_Printf( "Cat Arena: killed %s%s\n", CA_EnemyType( caEnemyInfo[ self->s.number ].type )->name, self->health <= CA_GIB_HEALTH ? " (gibbed)" : "" );
	}
	CA_HordeEnemyKilled( self->s.number );
	self->takedamage = qfalse;
	self->r.contents = 0;
	self->s.eFlags |= EF_DEAD;
	/* the client keeps the body (N10 death animation, or gibs below -40 health) */
	G_AddEvent( self, EV_CA_ENEMY_DEATH, self->health <= CA_GIB_HEALTH ? 1 : 0 );
	self->think = G_FreeEntity;
	self->nextthink = level.time + 400;
	trap_LinkEntity( self );
}

static void CA_EnemyPain( gentity_t *self, gentity_t *attacker, int damage ) {
	self->s.time2 = level.time;   /* hit flash in cgame */
}

static void CA_SyncEnemy( gentity_t *ent ) {
	playerState_t *ps = &caEnemyPS[ ent->s.number ];
	int eFlags = ent->s.eFlags, time2 = ent->s.time2;
	int n = ent->s.number;
	BG_PlayerStateToEntityState( ps, &ent->s, qtrue );
	ent->s.eType = ET_CA_ENEMY;
	ent->s.modelindex = caEnemyInfo[ n ].type;
	ent->s.powerups = caEnemyInfo[ n ].uid;
	ent->s.time = caEnemyInfo[ n ].attackTime;
	ent->s.eFlags = eFlags;
	ent->s.time2 = time2;
	ent->s.apos.trBase[ YAW ] = caEnemyInfo[ n ].face;
	ent->s.apos.trBase[ PITCH ] = 0;
	VectorCopy( ps->origin, ent->r.currentOrigin );
	trap_LinkEntity( ent );
}

static int CA_SpawnEnemy( int type, const float origin[3], float yaw, int health ) {
	gentity_t *ent;
	trace_t tr;
	vec3_t start, mins, maxs;
	playerState_t *ps;
	const caEnemyType_t *def = CA_EnemyType( type );

	if ( !def ) {
		return -1;
	}
	VectorCopy( def->hitMins, mins );
	VectorCopy( def->hitMaxs, maxs );
	VectorCopy( origin, start );
	if ( !def->flying ) {
		start[2] += 9;   /* as SelectSpawnPoint does for players */
	}
	trap_Trace( &tr, start, mins, maxs, start, ENTITYNUM_NONE, MASK_PLAYERSOLID );
	if ( tr.startsolid || tr.allsolid ) {
		return -1;
	}
	ent = G_Spawn();
	ent->classname = "ca_enemy";
	ent->s.eType = ET_CA_ENEMY;
	ent->r.contents = CONTENTS_BODY;
	ent->clipmask = MASK_PLAYERSOLID;
	VectorCopy( mins, ent->r.mins );
	VectorCopy( maxs, ent->r.maxs );
	ent->takedamage = qtrue;
	ent->health = health;
	ent->die = CA_EnemyDie;
	ent->pain = CA_EnemyPain;
	ent->s.eFlags = 0;
	ent->s.time2 = 0;

	caEnemyInfo[ ent->s.number ].type = type;
	caEnemyInfo[ ent->s.number ].uid = ( ++caEnemyUid ) & 0xffff;
	caEnemyInfo[ ent->s.number ].attackTime = 0;
	caEnemyInfo[ ent->s.number ].face = yaw;
	if ( ca_log.integer ) {
		G_Printf( "Cat Arena: spawn %s at %.0f %.0f %.0f\n", def->name, start[0], start[1], start[2] );
	}

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

static void CA_MoveEnemy( int entityNum, float yawDeg, float speedUnits, int moving, int msec, float faceYawDeg ) {
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
	caEnemyInfo[ entityNum ].face = faceYawDeg;
	CA_SyncEnemy( ent );
}

/* Flyers (N10 updateFlyer) are placed directly: their steering already tested the way. */
static void CA_SetEnemyOrigin( int entityNum, const float origin[3], float faceYawDeg ) {
	gentity_t *ent = CA_Enemy( entityNum );
	playerState_t *ps;
	if ( !ent || ent->health <= 0 ) {
		return;
	}
	ps = &caEnemyPS[ entityNum ];
	VectorCopy( origin, ps->origin );
	VectorClear( ps->velocity );
	ps->groundEntityNum = ENTITYNUM_NONE;
	ps->commandTime = level.time;
	caEnemyInfo[ entityNum ].face = faceYawDeg;
	CA_SyncEnemy( ent );
}

static void CA_SetEnemyState( int entityNum, int windingUp ) {
	gentity_t *ent = CA_Enemy( entityNum );
	if ( ent && windingUp ) {
		caEnemyInfo[ entityNum ].attackTime = level.time;
		ent->s.time = level.time;
	}
}

static int CA_BoxFree( const float from[3], const float to[3], const float mins[3], const float maxs[3] ) {
	trace_t tr;
	trap_Trace( &tr, from, mins, maxs, to, ENTITYNUM_NONE, MASK_SOLID );
	return !tr.startsolid && !tr.allsolid && tr.fraction >= 1.0f;
}

static void CA_FireEnemyShot( int owner, int type, int kind, const float origin[3], const float velocity[3], int damage, int lifeMs, int targetClient ) {
	G_CA_EnemyShot( owner, type, kind, origin, velocity, damage, lifeMs );
}

static int CA_Visible( int entityNum, int clientNum ) {
	trace_t tr;
	vec3_t from, to;
	if ( clientNum < 0 || clientNum >= level.maxclients ) {
		return 0;
	}
	VectorCopy( caEnemyPS[ entityNum ].origin, from );
	/* N10 enemy3DHasLineOfSight: from 1.18 m (flyers) / 1.30 m above the feet */
	from[2] += ( CA_EnemyType( caEnemyInfo[ entityNum ].type ) && CA_EnemyType( caEnemyInfo[ entityNum ].type )->flying ? 1.18f : 1.30f ) * CA_UNITS_PER_METRE - 24;
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
	if ( ca_log.integer && attackerEntityNum >= 0 && attackerEntityNum < MAX_GENTITIES && CA_EnemyType( caEnemyInfo[ attackerEntityNum ].type ) ) {
		G_Printf( "Cat Arena: hit by %s melee %d\n", CA_EnemyType( caEnemyInfo[ attackerEntityNum ].type )->name, damage );
	}
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

static const char *CA_MapName( void ) {
	static char name[MAX_QPATH];
	trap_Cvar_VariableStringBuffer( "mapname", name, sizeof( name ) );
	return name;
}

static int CA_ReadFile( const char *path, unsigned char *buffer, int maxBytes ) {
	fileHandle_t f;
	int len = trap_FS_FOpenFile( path, &f, FS_READ );
	if ( len < 0 || !f ) {
		return -1;
	}
	if ( len > maxBytes ) {
		trap_FS_FCloseFile( f );
		return -1;
	}
	trap_FS_Read( buffer, len, f );
	trap_FS_FCloseFile( f );
	return len;
}

static const caGameImport_t caImports = {
	CA_Print, CA_LevelTime, CA_Players, CA_SpawnPoints, CA_SpawnEnemy, CA_RemoveEnemy,
	CA_EnemyHealth, CA_EnemyOrigin, CA_MoveEnemy, CA_SetEnemyState, CA_Visible,
	CA_DamagePlayer, CA_DamageEnemy, CA_SetWaveInfo, CA_MapName, CA_ReadFile,
	CA_SetEnemyOrigin, CA_BoxFree, CA_FireEnemyShot
};

void G_CA_Init( void ) {
	trap_Cvar_Register( &ca_waves, "ca_waves", "1", 0 );
	trap_Cvar_Register( &ca_log, "ca_log", "0", 0 );
	trap_Cvar_Register( NULL, "ca_loadout", "", 0 );   /* "a b": fixed spawn weapons (tests); empty = random */
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
	G_CA_Burn( msec );
	trap_Cvar_Update( &ca_waves );
	trap_Cvar_Update( &ca_log );
	if ( !ca_waves.integer && !CA_HordeLineupActive() ) {
		return;
	}
	CA_HordeFrame( msec );
}

void G_CA_Shutdown( void ) {
	CA_HordeShutdown();
}

/* ---- test commands ------------------------------------------------------------------------ */
void Svcmd_CA_SelfTest_f( void ) {
	int hordeOk = CA_HordeSelfTest();
	int i, fiend = -1, ok;
	float spots[64][3];
	int n = CA_SpawnPoints( spots, 64 );
	/* weapons: the real server firing code against a fiend on this map */
	for ( i = 0 ; i < n && fiend < 0 ; i++ ) {
		fiend = CA_SpawnEnemy( CA_ENEMY_FIEND, spots[i], 0, 100 );
	}
	G_CA_WeaponSelfTest( fiend );
	ok = hordeOk && caWeaponFailsTotal() == 0;
	if ( fiend >= 0 && g_entities[ fiend ].inuse && g_entities[ fiend ].takedamage ) {
		CA_RemoveEnemy( fiend );
	}
	G_Printf( ok ? "ca_selftest: ALL PASS\n" : "ca_selftest: FAILURES\n" );
}

/* ca_state (cheat): one line with the player's movement and weapon state (scripted tests). */
void Cmd_CA_State_f( gentity_t *ent ) {
	const playerState_t *ps;
	if ( !ent->client ) {
		return;
	}
	ps = &ent->client->ps;
	trap_SendServerCommand( ent - g_entities, va( "print \"CA_STATE t=%d pos=%.0f,%.0f,%.0f hspeed=%.0f vz=%.0f sliding=%d slide_ms=%d cooldown_ms=%d ducked=%d clip=%d reloading=%d health=%d score=%d weapon=%d wstate=%d wtime=%d pmflags=%d slot=%d w0=%d w1=%d inhand=%d clip2=%d\n\"",
		level.time, ps->origin[0], ps->origin[1], ps->origin[2], sqrt( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] ), ps->velocity[2],
		( ps->pm_flags & PMF_CA_SLIDING ) ? 1 : 0, ps->stats[STAT_CA_SLIDE], ps->stats[STAT_CA_SLIDE_COOLDOWN], ( ps->pm_flags & PMF_DUCKED ) ? 1 : 0,
		ps->stats[STAT_CA_CLIP], ps->weaponstate == WEAPON_RELOADING, ps->stats[STAT_HEALTH], ps->persistant[PERS_SCORE], ps->weapon, ps->weaponstate, ps->weaponTime, ps->pm_flags,
		CA_LO_ACTIVE_SLOT( ps->stats[STAT_CA_LOADOUT] ), CA_LO_WEAPON( ps->stats[STAT_CA_LOADOUT], 0 ), CA_LO_WEAPON( ps->stats[STAT_CA_LOADOUT], 1 ),
		CA_LO_IN_HAND( ps->stats[STAT_CA_LOADOUT] ), ps->stats[STAT_CA_CLIP2] ) );
}

/* ca_place x y z yaw [pitch] (cheat): put the player there at rest (setviewpos throws the player forward
   at 400 u/s like a teleporter exit, which spoils position tests). */
void Cmd_CA_Place_f( gentity_t *ent ) {
	char buf[MAX_TOKEN_CHARS];
	vec3_t origin, angles;
	int i;
	if ( !ent->client || ( trap_Argc() != 5 && trap_Argc() != 6 ) ) {
		trap_SendServerCommand( ent - g_entities, "print \"usage: ca_place x y z yaw [pitch]\n\"" );
		return;
	}
	for ( i = 0 ; i < 3 ; i++ ) {
		trap_Argv( i + 1, buf, sizeof( buf ) );
		origin[i] = atof( buf );
	}
	trap_Argv( 4, buf, sizeof( buf ) );
	VectorSet( angles, 0, atof( buf ), 0 );
	if ( trap_Argc() == 6 ) {
		trap_Argv( 5, buf, sizeof( buf ) );
		angles[PITCH] = atof( buf );
	}
	VectorCopy( origin, ent->client->ps.origin );
	VectorClear( ent->client->ps.velocity );
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT;
	SetClientViewAngle( ent, angles );
	BG_PlayerStateToEntityState( &ent->client->ps, &ent->s, qtrue );
	VectorCopy( ent->client->ps.origin, ent->r.currentOrigin );
	trap_LinkEntity( ent );
}

/* ca_enemies: every living enemy with its type, position and health (tests). */
void Cmd_CA_Enemies_f( gentity_t *ent ) {
	int i;
	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		gentity_t *e = &g_entities[ i ];
		if ( !e->inuse || e->s.eType != ET_CA_ENEMY || e->health <= 0 ) {
			continue;
		}
		trap_SendServerCommand( ent - g_entities, va( "print \"CA_ENEMY %d %s pos=%.0f,%.0f,%.0f health=%d\n\"", i,
			CA_EnemyType( caEnemyInfo[ i ].type )->name, e->r.currentOrigin[0], e->r.currentOrigin[1], e->r.currentOrigin[2], e->health ) );
	}
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

#ifdef CAT_ARENA_STANDALONE
/* ca_lineup [off] (cheat): all 20 enemy types in four rows in front of the player, held still;
   every 2.5 s they play their attack wind-up (pictures of the models and their animation). */
void Cmd_CA_Lineup_f( gentity_t *ent ) {
	char buf[16];
	vec3_t feet;
	if ( !ent->client ) {
		return;
	}
	if ( trap_Argc() > 1 ) {
		trap_Argv( 1, buf, sizeof( buf ) );
		if ( !Q_stricmp( buf, "off" ) ) {
			CA_HordeLineup( NULL, 0 );
			return;
		}
	}
	VectorCopy( ent->client->ps.origin, feet );
	CA_HordeLineup( feet, ent->client->ps.viewangles[YAW] );
}
#endif
