/*
 * Cat Arena: Feral Claw on Quake3e (N10-Q3E stage E): the co-op HUD of N8 / N10 (ui_hud.cpp
 * build_coop_hud): name tags over the teammates (name, health bar, DOWN · HOLD F / REVIVING; nearest
 * on top, smaller and fainter with distance, dimmed behind walls) and the revive status line
 * (coopReviveStatus, from stats[STAT_CA_COOP]). Drawn with the Quake III 2D calls in the stage B
 * look; N10's fonts and panels come with the HUD port (stage F).
 *
 * GPL v2 or later (docs/q3e/LICENSING.md).
 */
#include "cg_local.h"

#ifdef CAT_ARENA_STANDALONE

#define CA_Set4( v, a, b, c, d )	( ( v )[0] = ( a ), ( v )[1] = ( b ), ( v )[2] = ( c ), ( v )[3] = ( d ) )

typedef struct {
	int client;
	float x, y, distance;
	qboolean downed, occluded;
	int health;
} caTag_t;

static const char *CA_Upper( const char *name ) {
	static char buf[MAX_QPATH];
	int i, o = 0;
	for ( i = 0 ; name[i] && o < (int)sizeof( buf ) - 1 ; i++ ) {
		if ( name[i] == Q_COLOR_ESCAPE && name[i + 1] ) {	/* no colour codes in the tags */
			i++;
			continue;
		}
		buf[o++] = ( name[i] >= 'a' && name[i] <= 'z' ) ? name[i] - 32 : name[i];
	}
	buf[o] = 0;
	return buf;
}

static const char *CA_Name( int client ) {
	if ( client < 0 || client >= MAX_CLIENTS || !cgs.clientinfo[client].infoValid ) {
		return "MARINE CAT";
	}
	return CA_Upper( cgs.clientinfo[client].name );
}

/* the point above a player's head on the 640 x 480 screen (N10 project_tag: 2.15 m, 0.9 m when down) */
static qboolean CA_Project( const vec3_t feet, qboolean downed, caTag_t *t ) {
	vec3_t p, d;
	float depth, sx, sy;
	trace_t tr;
	VectorCopy( feet, p );
	p[2] += ( downed ? 0.9f : 2.15f ) * CA_UNITS_PER_METRE;
	VectorSubtract( p, cg.refdef.vieworg, d );
	depth = DotProduct( d, cg.refdef.viewaxis[0] );
	if ( depth < 0.3f * CA_UNITS_PER_METRE ) {
		return qfalse;
	}
	sx = -DotProduct( d, cg.refdef.viewaxis[1] ) / ( depth * tan( DEG2RAD( cg.refdef.fov_x ) * 0.5f ) );
	sy = DotProduct( d, cg.refdef.viewaxis[2] ) / ( depth * tan( DEG2RAD( cg.refdef.fov_y ) * 0.5f ) );
	t->x = 320 + sx * 320;
	t->y = 240 - sy * 240;
	t->distance = VectorLength( d ) / CA_UNITS_PER_METRE;
	t->downed = downed;
	CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, p, cg.snap->ps.clientNum, MASK_SOLID );
	t->occluded = tr.fraction < 1.0f;
	return qtrue;
}

static void CA_Text( float cx, float y, const char *s, float w, float h, const vec4_t color ) {
	CG_DrawStringExt( (int)( cx - CG_DrawStrlen( s ) * w * 0.5f ), (int)y, s, color, qtrue, qfalse, (int)w, (int)h, 0 );
}

void CG_CA_DrawCoopHud( void ) {
	caTag_t tags[MAX_CLIENTS];
	int i, j, n = 0, st = cg.snap->ps.stats[STAT_CA_COOP];
	int kind = ( st >> 12 ) & 7, subject = ( ( st >> 7 ) & 31 ) - 1, pct = st & 127;
	char line[128];
	/* name tags */
	for ( i = 0 ; i < cg.snap->numEntities ; i++ ) {
		const entityState_t *s = &cg.snap->entities[i];
		centity_t *cent = &cg_entities[s->number];
		vec3_t feet;
		if ( s->eType != ET_PLAYER || s->number == cg.snap->ps.clientNum || s->number >= MAX_CLIENTS || ( ( s->eFlags & EF_DEAD ) && !( s->eFlags & EF_CA_DOWNED ) ) ) {
			continue;
		}
		VectorCopy( cent->lerpOrigin, feet );
		feet[2] -= 24;
		if ( CA_Project( feet, ( s->eFlags & EF_CA_DOWNED ) != 0, &tags[n] ) ) {
			tags[n].client = s->number;
			tags[n].health = s->generic1;
			n++;
		}
	}
	for ( i = 0 ; i < n ; i++ ) {	/* farthest first: the nearest is drawn on top */
		for ( j = i + 1 ; j < n ; j++ ) {
			if ( tags[j].distance > tags[i].distance ) {
				caTag_t tmp = tags[i];
				tags[i] = tags[j];
				tags[j] = tmp;
			}
		}
	}
	for ( i = 0 ; i < n ; i++ ) {
		const caTag_t *t = &tags[i];
		const char *name = CA_Name( t->client );
		float nearK = 1 - ( t->distance - 6 ) / 60, a, cw, ch, tw, bx, by, bw, bh, hp;
		vec4_t panel, text, bar, back;
		if ( t->x < -60 || t->x > 700 || t->y < -40 || t->y > 520 ) {
			continue;
		}
		nearK = nearK < 0.55f ? 0.55f : nearK > 1 ? 1 : nearK;
		a = 1.25f - t->distance / 90;
		a = ( a < 0.35f ? 0.35f : a > 1 ? 1 : a ) * ( t->occluded ? 0.5f : 1 );
		cw = 7 * nearK;
		ch = 10 * nearK;
		tw = CG_DrawStrlen( name ) * cw;
		if ( tw < 54 * nearK ) {
			tw = 54 * nearK;
		}
		bw = tw + 16;
		bh = ( t->downed ? 34 : 26 ) * nearK;
		bx = t->x - bw * 0.5f;
		by = t->y - 18 * nearK;
		CA_Set4( panel, 0.05f, 0.06f, 0.08f, 0.55f * a );
		CG_FillRect( bx, by, bw, bh, panel );
		if ( t->downed ) {
			CA_Set4( text, 1.0f, 0.60f, 0.54f, a );
		} else {
			CA_Set4( text, 0.93f, 0.91f, 0.85f, a );
		}
		CA_Text( t->x, by + 3 * nearK, name, cw, ch, text );
		CA_Set4( back, 0.03f, 0.035f, 0.05f, 0.9f * a );
		CG_FillRect( t->x - tw * 0.5f, t->y + 1.5f * nearK, tw, 3 * nearK, back );
		hp = t->health / 100.0f;
		hp = hp < 0 ? 0 : hp > 1 ? 1 : hp;
		if ( hp > 0.5f ) {
			CA_Set4( bar, 0.79f, 0.82f, 0.77f, a );
		} else if ( hp > 0.25f ) {
			CA_Set4( bar, 0.85f, 0.73f, 0.51f, a );
		} else {
			CA_Set4( bar, 0.85f, 0.44f, 0.37f, a );
		}
		CG_FillRect( t->x - tw * 0.5f, t->y + 1.5f * nearK, tw * hp, 3 * nearK, bar );
		if ( t->downed ) {
			CA_Set4( text, 1.0f, 0.69f, 0.62f, a );
			CA_Text( t->x, t->y + 6 * nearK, kind == CA_COOP_REVIVING && subject == t->client ? "REVIVING" : "DOWN - HOLD F", 5 * nearK, 8 * nearK, text );
		}
	}
	/* the revive status line */
	line[0] = 0;
	switch ( kind ) {
	case CA_COOP_HOLD_TO_REVIVE:
		Com_sprintf( line, sizeof( line ), "HOLD F TO REVIVE %s - 4 SECONDS", CA_Name( subject ) );
		break;
	case CA_COOP_REVIVING:
		Com_sprintf( line, sizeof( line ), "REVIVING %s - %d%%", CA_Name( subject ), pct );
		break;
	case CA_COOP_BEING_REVIVED:
		Com_sprintf( line, sizeof( line ), "BEING REVIVED - %d%%", pct );
		break;
	case CA_COOP_DOWN:
		Q_strncpyz( line, "YOU ARE DOWN - WAIT FOR ANY TEAMMATE TO HOLD F FOR 4 SECONDS", sizeof( line ) );
		break;
	case CA_COOP_WIPED:
		Q_strncpyz( line, "SQUAD ELIMINATED - RESTARTING ROUND", sizeof( line ) );
		break;
	}
	if ( line[0] ) {
		vec4_t panel, edge, text;
		float cw = 9, chh = 14, tw = CG_DrawStrlen( line ) * cw, y = 480 * 0.67f - 20;
		if ( tw > 600 ) {
			cw = 600.0f / CG_DrawStrlen( line );
			tw = 600;
		}
		CA_Set4( panel, 0.086f, 0.043f, 0.043f, 0.87f );
		CA_Set4( edge, 0.89f, 0.64f, 0.42f, 1 );
		CA_Set4( text, 1.0f, 0.94f, 0.78f, 1 );
		CG_FillRect( 320 - tw * 0.5f - 22, y, tw + 44, 40, panel );
		if ( kind == CA_COOP_DOWN || kind == CA_COOP_WIPED || kind == CA_COOP_BEING_REVIVED ) {	/* N10 revive_alert */
			CG_DrawRect( 320 - tw * 0.5f - 22, y, tw + 44, 40, 1, edge );
		}
		CA_Text( 320, y + 20 - chh * 0.5f, line, cw, chh, text );
		char logv[8];
		trap_Cvar_VariableStringBuffer( "ca_log", logv, sizeof( logv ) );
		if ( atoi( logv ) && ( st & ~127 ) != cg.caLastCoopStat ) {
			CG_Printf( "Cat Arena: status %s\n", line );
		}
	}
	cg.caLastCoopStat = st & ~127;
}

#endif
