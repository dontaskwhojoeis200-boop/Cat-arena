/*
 * Cat Arena: Feral Claw on Quake3e (N10-Q3E stage E): N10's co-op rules on a Quake III server.
 *
 * N10 (native/src/combat.cpp, the legacy v3.24 multiplayer) has one host that simulates everything;
 * here the Quake III server does, for every player alike:
 *   - With two or more players a player at 0 health goes down instead of dying (Arena::take_damage /
 *     remote_damage): no movement, no weapon, no damage taken, ignored by the enemies; the view
 *     still turns, the eye at 0.62 m (kDownedEye).
 *   - A live player holding F (+button2, use) within 3.2 m (1.75 m height) of a downed one, with a
 *     line of sight, revives them in 4 s (near_revive, coop_tick): back with 45 health and 2 s in
 *     which no damage is taken. One revive at a time on the server, as N10's host.
 *   - When every player is down for 2.7 s the squad is wiped: the round starts again at wave 1 for
 *     everybody (N10 reloads the map): players respawn, medkits are ready, scores are 0.
 *   - A player left alone while down gets up with 100 health at a spawn spot ('guest_left').
 *   - Alone, a player dies and respawns as before (N10's solo rules).
 * stats[STAT_CA_COOP] carries N10's status line (coopReviveStatus) to each player.
 *
 * GPL v2 or later (docs/q3e/LICENSING.md).
 */
#include "g_local.h"

#ifdef CAT_ARENA_STANDALONE

#define CA_REVIVE_MS		4000
#define CA_REVIVE_DIST		3.2f
#define CA_REVIVE_HEALTH	45
#define CA_REVIVE_GRACE_MS	2000
#define CA_WIPE_MS			2700

static int caReviveTarget = -1, caReviveHolder = -1, caReviveMs, caWipeMs;

int G_CA_PlayingClients( void ) {
	int i, n = 0;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		const gclient_t *cl = &level.clients[i];
		if ( cl->pers.connected == CON_CONNECTED && cl->sess.sessionTeam != TEAM_SPECTATOR ) {
			n++;
		}
	}
	return n;
}

qboolean G_CA_IsDowned( const gentity_t *ent ) {
	return ent && ent->client && ( ent->client->ps.eFlags & EF_CA_DOWNED ) != 0;
}

static void CA_Log( const char *text ) {
	char buf[8];
	trap_Cvar_VariableStringBuffer( "ca_log", buf, sizeof( buf ) );
	if ( atoi( buf ) ) {
		G_Printf( "Cat Arena: %s t=%d\n", text, level.time );
	}
}

/* G_Damage: a player at 0 health with teammates goes down. Returns qfalse when alone (dies). */
qboolean G_CA_DownPlayer( gentity_t *ent ) {
	gclient_t *cl = ent->client;
	if ( !cl || G_CA_PlayingClients() < 2 ) {
		return qfalse;
	}
	ent->health = cl->ps.stats[STAT_HEALTH] = 0;
	cl->ps.eFlags |= EF_CA_DOWNED;
	ent->s.eFlags |= EF_CA_DOWNED;
	VectorClear( cl->ps.velocity );
	cl->caBurnMs = 0;
	ent->takedamage = qfalse;
	ent->r.contents = 0;			/* nothing blocks on it, nothing hits it */
	cl->respawnTime = level.time;
	trap_LinkEntity( ent );
	CA_Log( va( "%s down", cl->pers.netname ) );
	return qtrue;
}

static void CA_GetUp( gentity_t *ent, int health, int graceMs ) {
	gclient_t *cl = ent->client;
	cl->ps.eFlags &= ~EF_CA_DOWNED;
	ent->s.eFlags &= ~EF_CA_DOWNED;
	ent->health = cl->ps.stats[STAT_HEALTH] = health;
	ent->takedamage = qtrue;
	ent->r.contents = CONTENTS_BODY;
	cl->ps.pm_type = PM_NORMAL;
	cl->ps.weapon = WP_MACHINEGUN;
	cl->ps.weaponstate = WEAPON_READY;
	cl->ps.weaponTime = 0;
	cl->caGraceUntil = level.time + graceMs;
	trap_LinkEntity( ent );
}

/* N10 netNearRevive: a live player a can revive a downed b (3.2 m, 1.75 m height, line of sight) */
static qboolean CA_NearRevive( gentity_t *a, gentity_t *b ) {
	const float *pa = a->client->ps.origin, *pb = b->client->ps.origin;
	float d = sqrt( ( pa[0] - pb[0] ) * ( pa[0] - pb[0] ) + ( pa[1] - pb[1] ) * ( pa[1] - pb[1] ) ) / CA_UNITS_PER_METRE;
	vec3_t from, to, mins = { -3, -3, -3 }, maxs = { 3, 3, 3 };	/* N10 radius .09 m */
	trace_t tr;
	if ( !G_CA_IsDowned( b ) || G_CA_IsDowned( a ) || a->health <= 0 ) {
		return qfalse;
	}
	if ( d > CA_REVIVE_DIST || fabs( pa[2] - pb[2] ) / CA_UNITS_PER_METRE > 1.75f || d < 0.01f ) {
		return qfalse;
	}
	VectorSet( from, pa[0], pa[1], pa[2] - 24 + 1.10f * CA_UNITS_PER_METRE );	/* feet + 1.10 m */
	VectorSet( to, pb[0], pb[1], pb[2] - 24 + 1.00f * CA_UNITS_PER_METRE );
	trap_Trace( &tr, from, mins, maxs, to, a->s.number, MASK_SOLID );
	return tr.fraction >= 1.0f && !tr.startsolid;
}

static void CA_RemoveRoundEntities( void ) {	/* projectiles and enemy shots of the round */
	int i;
	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->classname && ( !strcmp( e->classname, "ca_projectile" ) || !strcmp( e->classname, "ca_enemy_shot" ) ) ) {
			G_FreeEntity( e );
		}
	}
}

/* squad wipe: the round again from wave 1 (N10 reloads the map, which resets everything) */
static void CA_SquadRestart( void ) {
	int i;
	CA_Log( "squad wiped, round restarts" );
	CA_RemoveRoundEntities();
	CA_HordeRestart();
	G_CA_MedkitsReset();
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		gentity_t *ent = &g_entities[i];
		gclient_t *cl = &level.clients[i];
		if ( cl->pers.connected != CON_CONNECTED || cl->sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}
		cl->ps.eFlags &= ~EF_CA_DOWNED;
		ent->s.eFlags &= ~EF_CA_DOWNED;
		cl->ps.persistant[PERS_SCORE] = 0;
		ClientSpawn( ent );
	}
	CalculateRanks();
}

/* A player disconnected: a revive involving them stops; someone left alone while down gets up. */
void G_CA_CoopClientLeft( int clientNum ) {
	int i, remaining = 0;
	if ( caReviveTarget == clientNum || caReviveHolder == clientNum ) {
		caReviveTarget = caReviveHolder = -1;
		caReviveMs = 0;
	}
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		const gclient_t *cl = &level.clients[i];
		if ( i != clientNum && cl->pers.connected == CON_CONNECTED && cl->sess.sessionTeam != TEAM_SPECTATOR ) {
			remaining++;
		}
	}
	if ( remaining != 1 ) {
		return;
	}
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		gentity_t *ent = &g_entities[i];
		gclient_t *cl = &level.clients[i];
		if ( i == clientNum || cl->pers.connected != CON_CONNECTED || cl->sess.sessionTeam == TEAM_SPECTATOR || !G_CA_IsDowned( ent ) ) {
			continue;
		}
		CA_Log( va( "%s alone and down: gets up", cl->pers.netname ) );
		cl->ps.eFlags &= ~EF_CA_DOWNED;
		ent->s.eFlags &= ~EF_CA_DOWNED;
		ClientSpawn( ent );
		cl->caGraceUntil = level.time + 2500;	/* N10 finish_respawn */
	}
}

void G_CA_CoopFrame( int msec ) {
	gentity_t *players[MAX_CLIENTS];
	int i, j, n = 0, target = -1, holder = -1;
	qboolean liveAny = qfalse;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		gclient_t *cl = &level.clients[i];
		if ( cl->pers.connected == CON_CONNECTED && cl->sess.sessionTeam != TEAM_SPECTATOR ) {
			players[n++] = &g_entities[i];
		}
	}
	if ( n < 2 ) {
		caReviveTarget = caReviveHolder = -1;
		caReviveMs = caWipeMs = 0;
		for ( i = 0 ; i < n ; i++ ) {
			players[i]->client->ps.stats[STAT_CA_COOP] = 0;
		}
		return;
	}
	/* any live player holding F near a downed one revives them in 4 s (the first holder found) */
	for ( i = 0 ; i < n ; i++ ) {
		gentity_t *src = players[i];
		float best = 1e9f;
		if ( G_CA_IsDowned( src ) || src->health <= 0 ) {
			continue;
		}
		liveAny = qtrue;
		if ( target >= 0 || !( src->client->pers.cmd.buttons & BUTTON_USE_HOLDABLE ) ) {
			continue;
		}
		for ( j = 0 ; j < n ; j++ ) {
			gentity_t *down = players[j];
			float dx, dy, d;
			if ( down == src || !CA_NearRevive( src, down ) ) {
				continue;
			}
			dx = down->client->ps.origin[0] - src->client->ps.origin[0];
			dy = down->client->ps.origin[1] - src->client->ps.origin[1];
			d = dx * dx + dy * dy;
			if ( d < best ) {
				best = d;
				target = down->s.number;
				holder = src->s.number;
			}
		}
	}
	if ( target >= 0 ) {
		if ( target != caReviveTarget || holder != caReviveHolder ) {
			caReviveMs = 0;
			caReviveTarget = target;
			caReviveHolder = holder;
		}
		caReviveMs += msec;
		if ( caReviveMs >= CA_REVIVE_MS ) {
			CA_Log( va( "%s revived by %s", level.clients[target].pers.netname, level.clients[holder].pers.netname ) );
			CA_GetUp( &g_entities[target], CA_REVIVE_HEALTH, CA_REVIVE_GRACE_MS );
			caReviveTarget = caReviveHolder = -1;
			caReviveMs = caWipeMs = 0;
		}
	} else {
		caReviveTarget = caReviveHolder = -1;
		caReviveMs = 0;
	}
	/* the squad is wiped when every player is down for 2.7 s */
	if ( !liveAny ) {
		caWipeMs += msec;
		if ( caWipeMs > CA_WIPE_MS ) {
			caWipeMs = 0;
			CA_SquadRestart();
			for ( i = 0 ; i < n ; i++ ) {
				players[i]->client->ps.stats[STAT_CA_COOP] = 0;
			}
			return;
		}
	} else {
		caWipeMs = 0;
	}
	/* each player's status line (coopReviveStatus) */
	for ( i = 0 ; i < n ; i++ ) {
		gentity_t *me = players[i];
		int pct = (int)floor( caReviveMs * 100.0f / CA_REVIVE_MS + 0.5f ), stat = 0, mate = -1;
		float best = 1e9f;
		if ( pct > 100 ) {
			pct = 100;
		}
		if ( caWipeMs > 0 ) {
			stat = CA_COOP_STAT( CA_COOP_WIPED, -1, 0 );
		} else if ( G_CA_IsDowned( me ) ) {
			stat = caReviveTarget == me->s.number ? CA_COOP_STAT( CA_COOP_BEING_REVIVED, caReviveHolder, pct ) : CA_COOP_STAT( CA_COOP_DOWN, -1, 0 );
		} else {
			for ( j = 0 ; j < n ; j++ ) {
				gentity_t *down = players[j];
				float dx, dy, d;
				if ( down == me || !CA_NearRevive( me, down ) ) {
					continue;
				}
				dx = down->client->ps.origin[0] - me->client->ps.origin[0];
				dy = down->client->ps.origin[1] - me->client->ps.origin[1];
				d = dx * dx + dy * dy;
				if ( d < best ) {
					best = d;
					mate = down->s.number;
				}
			}
			if ( mate >= 0 ) {
				stat = caReviveTarget == mate ? CA_COOP_STAT( CA_COOP_REVIVING, mate, pct ) : CA_COOP_STAT( CA_COOP_HOLD_TO_REVIVE, mate, 0 );
			}
		}
		me->client->ps.stats[STAT_CA_COOP] = stat;
	}
}

/* ---- test commands (dedicated server console): scripted players without a network client ------ */

/* ca_addplayer <name>: a scripted player in a bot slot (no bot AI: its usercmd is set by ca_input) */
void Svcmd_CA_AddPlayer_f( void ) {
	char name[MAX_NETNAME], userinfo[MAX_INFO_STRING];
	int clientNum;
	gentity_t *ent;
	trap_Argv( 1, name, sizeof( name ) );
	if ( !name[0] ) {
		G_Printf( "usage: ca_addplayer <name>\n" );
		return;
	}
	clientNum = trap_BotAllocateClient();
	if ( clientNum < 0 ) {
		G_Printf( "Cat Arena: no free player slot\n" );
		return;
	}
	userinfo[0] = 0;
	Info_SetValueForKey( userinfo, "name", name );
	Info_SetValueForKey( userinfo, "rate", "25000" );
	Info_SetValueForKey( userinfo, "snaps", "20" );
	Info_SetValueForKey( userinfo, "ca_scripted", "1" );
	trap_SetUserinfo( clientNum, userinfo );
	ent = &g_entities[clientNum];
	ent->r.svFlags |= SVF_BOT;
	ent->inuse = qtrue;
	if ( ClientConnect( clientNum, qtrue, qtrue ) ) {
		G_Printf( "Cat Arena: player %s could not connect\n", name );
		trap_DropClient( clientNum, "refused" );
		return;
	}
	ClientBegin( clientNum );
	G_Printf( "Cat Arena: player %d %s joined\n", clientNum, name );
}

static gentity_t *CA_ArgPlayer( int arg ) {
	char buf[16];
	int n;
	trap_Argv( arg, buf, sizeof( buf ) );
	n = atoi( buf );
	if ( !buf[0] || n < 0 || n >= level.maxclients || level.clients[n].pers.connected != CON_CONNECTED ) {
		G_Printf( "Cat Arena: no player %s\n", buf );
		return NULL;
	}
	return &g_entities[n];
}

/* ca_removeplayer <client> */
void Svcmd_CA_RemovePlayer_f( void ) {
	gentity_t *ent = CA_ArgPlayer( 1 );
	if ( ent ) {
		trap_DropClient( ent->s.number, "left the arena" );
	}
}

/* ca_input <client> <forward> <right> <up> <buttons>: a scripted player's usercmd until changed */
void Svcmd_CA_Input_f( void ) {
	gentity_t *ent = CA_ArgPlayer( 1 );
	char buf[16];
	if ( !ent || !( ent->r.svFlags & SVF_BOT ) ) {
		return;
	}
	trap_Argv( 2, buf, sizeof( buf ) );
	ent->client->pers.cmd.forwardmove = atoi( buf );
	trap_Argv( 3, buf, sizeof( buf ) );
	ent->client->pers.cmd.rightmove = atoi( buf );
	trap_Argv( 4, buf, sizeof( buf ) );
	ent->client->pers.cmd.upmove = atoi( buf );
	trap_Argv( 5, buf, sizeof( buf ) );
	ent->client->pers.cmd.buttons = atoi( buf );
}

/* ca_moveplayer <client> <x> <y> <z> <yaw>: a teleport, as ca_place */
void Svcmd_CA_MovePlayer_f( void ) {
	gentity_t *ent = CA_ArgPlayer( 1 );
	char buf[32];
	vec3_t origin, angles = { 0, 0, 0 };
	int i;
	if ( !ent ) {
		return;
	}
	for ( i = 0 ; i < 3 ; i++ ) {
		trap_Argv( i + 2, buf, sizeof( buf ) );
		origin[i] = atof( buf );
	}
	trap_Argv( 5, buf, sizeof( buf ) );
	angles[YAW] = atof( buf );
	VectorCopy( origin, ent->client->ps.origin );
	VectorClear( ent->client->ps.velocity );
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT;
	SetClientViewAngle( ent, angles );
	BG_PlayerStateToEntityState( &ent->client->ps, &ent->s, qtrue );
	VectorCopy( ent->client->ps.origin, ent->r.currentOrigin );
	trap_LinkEntity( ent );
}

/* ca_hurt <client> <damage>: damage through G_Damage, as an enemy hit would */
void Svcmd_CA_Hurt_f( void ) {
	gentity_t *ent = CA_ArgPlayer( 1 );
	char buf[16];
	if ( !ent ) {
		return;
	}
	trap_Argv( 2, buf, sizeof( buf ) );
	G_Damage( ent, NULL, &g_entities[ENTITYNUM_WORLD], NULL, NULL, atoi( buf ), DAMAGE_NO_KNOCKBACK, MOD_UNKNOWN );
}

/* ca_coopstate: every player's health, downed flag, status line, position (tests) */
void Svcmd_CA_CoopState_f( void ) {
	int i;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		gclient_t *cl = &level.clients[i];
		int st;
		if ( cl->pers.connected != CON_CONNECTED ) {
			continue;
		}
		st = cl->ps.stats[STAT_CA_COOP];
		G_Printf( "CA_COOP t=%d client=%d name=%s health=%d downed=%d dead=%d status=%d subject=%d pct=%d pos=%.0f,%.0f,%.0f score=%d grace=%d\n",
			level.time, i, cl->pers.netname, cl->ps.stats[STAT_HEALTH], G_CA_IsDowned( &g_entities[i] ) ? 1 : 0,
			cl->ps.pm_type == PM_DEAD && !G_CA_IsDowned( &g_entities[i] ) ? 1 : 0, ( st >> 12 ) & 7, ( ( st >> 7 ) & 31 ) - 1, st & 127,
			cl->ps.origin[0], cl->ps.origin[1], cl->ps.origin[2], cl->ps.persistant[PERS_SCORE], cl->caGraceUntil > level.time ? 1 : 0 );
	}
}

#endif
