/*
 * Cat Arena: Feral Claw — drawing the 20 N10 enemy types (N10-Q3E stage D part 4).
 *
 * Each creature is the N10 mesh (models/catarena/enemies/<type>.emesh, from assets/n6/enemies)
 * posed every frame by the C++20 port of N10's animation (ca_enemy_anim.cpp: walk / run / fly
 * cycles from the creature's own movement, looking at the player, attack wind-up and strike, pain)
 * and drawn as polygons lit by the map's light grid. A dead enemy leaves a body that plays N10's
 * death animation (topple, stay 4.5 s, sink) on the client, or bursts into gibs below -40 health.
 *
 * New file, not part of the ioquake3 import. GPL v2 or later.
 */
#include "cg_local.h"

#ifdef CAT_ARENA_STANDALONE
#include "ca_anim.h"

#define CA_MAX_MESH_VERTS  1024
#define CA_MAX_BODIES      24          /* N10 keeps at most 24 corpses */
#define CA_N10_GRAVITY     ( 18.0f * CA_UNITS_PER_METRE )

typedef struct {
	int uid;                 /* entityState powerups: a new spawn in this slot resets the animation */
	caAnimState_t anim;
	float pose[16];
	int posed;
} caLive_t;

typedef struct {
	int active, type, variant, start;
	float side, facing, floorZ, vz;
	vec3_t feet;
	float pose[16];
	int lastTime;
} caBody_t;

static caLive_t caLive[MAX_GENTITIES];
static caBody_t caBodies[CA_MAX_BODIES];
static int caNextBody;
static qhandle_t caEnemyShader[CA_ENEMY_TYPES];
static caSkinVert_t caSkin[CA_MAX_MESH_VERTS];
static polyVert_t caPolys[CA_MAX_MESH_VERTS];
static int caEnemyMeshes;
static void CA_RegisterMarine( void );

void CG_CA_RegisterEnemies( void ) {
	static unsigned char buffer[ 80 + CA_MAX_MESH_VERTS * 48 ];
	int t;
	caEnemyMeshes = 0;
	for ( t = 0 ; t < CA_ENEMY_TYPES ; t++ ) {
		const caEnemyType_t *def = CA_EnemyType( t );
		fileHandle_t f;
		int len = trap_FS_FOpenFile( va( "models/catarena/enemies/%s.emesh", def->name ), &f, FS_READ );
		caEnemyShader[t] = trap_R_RegisterShader( va( "models/catarena/enemies/%s", def->name ) );
		if ( len <= 0 || !f ) {
			CG_Printf( S_COLOR_YELLOW "WARNING: no mesh for enemy %s\n", def->name );
			continue;
		}
		if ( len > (int)sizeof( buffer ) ) {
			trap_FS_FCloseFile( f );
			CG_Printf( S_COLOR_YELLOW "WARNING: enemy mesh %s too large\n", def->name );
			continue;
		}
		trap_FS_Read( buffer, len, f );
		trap_FS_FCloseFile( f );
		if ( CA_EnemyMeshLoad( t, buffer, len ) > 0 && CA_EnemyMeshVertexCount( t ) <= CA_MAX_MESH_VERTS ) {
			caEnemyMeshes++;
		}
	}
	memset( caLive, 0, sizeof( caLive ) );
	memset( caBodies, 0, sizeof( caBodies ) );
	CG_Printf( "Cat Arena: %d enemy meshes loaded\n", caEnemyMeshes );
	CA_RegisterMarine();
}

/* Light grid at the creature (Quake III entity lighting: ambient + directed by the normal),
   times N10's vertex colour weight, onto each polygon vertex. */
static void CA_DrawSkinned( int type, int count, const vec3_t lightAt ) {
	vec3_t ambient, directed, dir;
	int i;
	if ( !trap_R_LightForPoint( (float *)lightAt, ambient, directed, dir ) ) {
		VectorSet( ambient, 150, 150, 150 );
		VectorSet( directed, 90, 90, 90 );
		VectorSet( dir, -0.3f, 0.6f, 0.8f );
		VectorNormalize( dir );
	}
	for ( i = 0 ; i < 3 ; i++ ) {   /* Quake III keeps a minimum ambient on entities */
		if ( ambient[i] < 52 ) {
			ambient[i] = 52;
		}
	}
	for ( i = 0 ; i < count ; i++ ) {
		const caSkinVert_t *s = &caSkin[i];
		polyVert_t *p = &caPolys[i];
		float d = DotProduct( s->normal, dir ), c;
		int k;
		if ( d < 0 ) {
			d = 0;
		}
		VectorCopy( s->xyz, p->xyz );
		p->st[0] = s->st[0];
		p->st[1] = s->st[1];
		for ( k = 0 ; k < 3 ; k++ ) {
			c = ( ambient[k] + directed[k] * d ) * ( 0.2f + 0.8f * s->color[k] );
			p->modulate[k] = c > 255 ? 255 : c < 0 ? 0 : (byte)c;
		}
		p->modulate[3] = 255;
	}
	trap_R_AddPolysToScene( caEnemyShader[type], 3, caPolys, count / 3 );
}

static qboolean CA_InView( const vec3_t feet, float height ) {
	vec3_t d;
	VectorSubtract( feet, cg.refdef.vieworg, d );
	d[2] += height * 0.5f;
	if ( DotProduct( d, cg.refdef.viewaxis[0] ) < -height * 1.5f ) {
		return qfalse;
	}
	return VectorLengthSquared( d ) < 6000.0f * 6000.0f;
}

/* The local player as the creatures' target (N10 enemyTargetFor) in N10 metres. */
static void CA_Target( caPoseInput_t *in ) {
	in->hasTarget = 1;
	in->tx = cg.predictedPlayerState.origin[0] / CA_UNITS_PER_METRE;
	in->ty = cg.predictedPlayerState.origin[1] / CA_UNITS_PER_METRE;
	in->tz = ( cg.predictedPlayerState.origin[2] - 24 ) / CA_UNITS_PER_METRE;
}

void CG_CA_DrawEnemy( centity_t *cent ) {
	const entityState_t *s = &cent->currentState;
	int type = s->modelindex, n, count;
	caLive_t *live;
	caPoseInput_t in;
	float facing, state4[4];
	vec3_t feet, lightAt;

	if ( type < 0 || type >= CA_ENEMY_TYPES || ( s->eFlags & EF_DEAD ) ) {
		return;
	}
	CG_CA_EnemyVoices( cent );
	n = s->number;
	live = &caLive[n];
	if ( live->uid != s->powerups ) {
		memset( live, 0, sizeof( *live ) );
		live->uid = s->powerups;
		live->anim.rng = (unsigned)( s->powerups * 2654435761u + 12345u );
	}
	VectorCopy( cent->lerpOrigin, feet );
	feet[2] -= 24;
	facing = DEG2RAD( cent->lerpAngles[YAW] );
	memset( &in, 0, sizeof( in ) );
	in.x = feet[0] / CA_UNITS_PER_METRE;
	in.y = feet[1] / CA_UNITS_PER_METRE;
	in.z = feet[2] / CA_UNITS_PER_METRE;
	in.a = facing - M_PI / 2;
	in.hitFlash = 0;
	if ( s->time2 > 0 && cg.time - s->time2 >= 0 && cg.time - s->time2 < CA_ENEMY_HIT_FLASH_MS ) {
		in.hitFlash = ( CA_ENEMY_HIT_FLASH_MS - ( cg.time - s->time2 ) ) / 1000.0;
	}
	in.attackProgress = s->time > 0 ? CA_AttackProgress( type, cg.time - s->time ) : -1;
	CA_Target( &in );
	/* the animation runs even off screen, so a creature walking into view is mid-stride */
	CA_AnimPose( &live->anim, type, &in, cg.time / 1000.0, live->pose );
	live->posed = 1;

	count = CA_EnemyMeshVertexCount( type );
	if ( count <= 0 || count > CA_MAX_MESH_VERTS || !CA_InView( feet, CA_EnemyHeight( type ) * CA_UNITS_PER_METRE ) ) {
		return;
	}
	state4[0] = in.hitFlash * 5.2f;   /* N10 clampd(hit_flash * 5.2, 0, 1) */
	state4[1] = state4[2] = state4[3] = 0;
	CA_EnemySkin( type, live->pose, state4, feet, facing, caSkin );
	VectorCopy( feet, lightAt );
	lightAt[2] += CA_EnemyHeight( type ) * CA_UNITS_PER_METRE * 0.5f;
	CA_DrawSkinned( type, count, lightAt );
}

/* EV_CA_ENEMY_DEATH: a body with N10's death animation, or gibs. */
void CG_CA_EnemyDeath( centity_t *cent, int gibbed ) {
	const entityState_t *s = &cent->currentState;
	caLive_t *live = &caLive[ s->number ];
	caBody_t *b;
	trace_t tr;
	vec3_t down;
	int type = s->modelindex;
	if ( type < 0 || type >= CA_ENEMY_TYPES ) {
		return;
	}
	CG_CA_EnemyDeathVoice( cent );
	if ( gibbed ) {
		CG_CA_Gibs( cent->lerpOrigin, CA_EnemyType( type )->hitClass == 1 ? 1.25f : 1.0f );
		return;
	}
	b = &caBodies[ caNextBody ];
	caNextBody = ( caNextBody + 1 ) % CA_MAX_BODIES;
	memset( b, 0, sizeof( *b ) );
	b->active = 1;
	b->type = type;
	b->start = b->lastTime = cg.time;
	b->facing = DEG2RAD( cent->lerpAngles[YAW] );
	VectorCopy( cent->lerpOrigin, b->feet );
	b->feet[2] -= 24;
	if ( live->posed && live->uid == s->powerups ) {
		memcpy( b->pose, live->pose, sizeof( b->pose ) );
	}
	CA_AnimDeathStart( &live->anim, type, &b->variant, &b->side );
	b->floorZ = b->feet[2];
	if ( CA_EnemyType( type )->flying ) {   /* a dead flyer falls to the floor below it */
		VectorCopy( b->feet, down );
		down[2] -= 4096;
		CG_Trace( &tr, b->feet, NULL, NULL, down, s->number, MASK_SOLID );
		b->floorZ = tr.endpos[2];
	}
}

void CG_CA_AddEnemyBodies( void ) {
	int i;
	for ( i = 0 ; i < CA_MAX_BODIES ; i++ ) {
		caBody_t *b = &caBodies[i];
		float pose[16], st3[3], state4[4], dt;
		int count;
		vec3_t lightAt;
		if ( !b->active ) {
			continue;
		}
		if ( cg.time - b->start >= CA_DEATH_MS || cg.time < b->start ) {
			b->active = 0;
			continue;
		}
		dt = ( cg.time - b->lastTime ) * 0.001f;
		b->lastTime = cg.time;
		if ( b->feet[2] > b->floorZ ) {   /* N10 updateDyingEnemies: flyers fall */
			b->vz -= CA_N10_GRAVITY * dt;
			b->feet[2] += b->vz * dt;
			if ( b->feet[2] < b->floorZ ) {
				b->feet[2] = b->floorZ;
			}
		}
		count = CA_EnemyMeshVertexCount( b->type );
		if ( count <= 0 || count > CA_MAX_MESH_VERTS || !CA_InView( b->feet, CA_EnemyHeight( b->type ) * CA_UNITS_PER_METRE ) ) {
			continue;
		}
		CA_AnimDeath( b->type, b->pose, b->variant, b->side, ( cg.time - b->start ) / 1000.0, pose, st3 );
		state4[0] = 0;
		state4[1] = st3[0];
		state4[2] = st3[1];
		state4[3] = st3[2];
		CA_EnemySkin( b->type, pose, state4, b->feet, b->facing, caSkin );
		VectorCopy( b->feet, lightAt );
		lightAt[2] += 16;
		CA_DrawSkinned( b->type, count, lightAt );
	}
}

void CG_CA_ClearEnemyBodies( void ) {
	memset( caBodies, 0, sizeof( caBodies ) );
}

/* ---- Marine Cats: the other players (N10 EnemyFx::marine / drawMarineCats, stage E) -------------
   N10's Marine Cat mesh posed with N10's soldier rig and the shot / hurt layers, lying on its side
   while down. Drawn as enemy.vert / enemy.frag draw it (MODE_MARINE): armour = world atlas tile 0 x
   vertex colour x the player's tint (28 %) x a fixed light (.69 + .33 lambert, x .80 x 1.19), not lit
   by the map; the cat face = cat-face cell 4 x (.84 + .16 lambert). */
static struct {
	caAnimState_t anim;
	int shotTime, hurtTime, ready;
} caMarine[MAX_CLIENTS];
static qhandle_t caMarineArmour, caMarineFace;
static polyVert_t caMarineFacePolys[CA_MAX_MESH_VERTS];

static void CA_RegisterMarine( void ) {
	static unsigned char buffer[ 80 + CA_MAX_MESH_VERTS * 48 ];
	fileHandle_t f;
	int len = trap_FS_FOpenFile( "models/catarena/marine/marine.emesh", &f, FS_READ );
	caMarineArmour = trap_R_RegisterShader( "models/catarena/marine/armour" );
	caMarineFace = trap_R_RegisterShader( "models/catarena/marine/face" );
	memset( caMarine, 0, sizeof( caMarine ) );
	if ( len <= 0 || !f || len > (int)sizeof( buffer ) ) {
		if ( f ) {
			trap_FS_FCloseFile( f );
		}
		CG_Printf( S_COLOR_YELLOW "WARNING: no Marine Cat mesh; other players are not drawn\n" );
		return;
	}
	trap_FS_Read( buffer, len, f );
	trap_FS_FCloseFile( f );
	if ( CA_EnemyMeshLoad( CA_MARINE_TYPE, buffer, len ) > 0 && CA_EnemyMeshVertexCount( CA_MARINE_TYPE ) <= CA_MAX_MESH_VERTS ) {
		CG_Printf( "Cat Arena: Marine Cat mesh loaded (%d vertices)\n", CA_EnemyMeshVertexCount( CA_MARINE_TYPE ) );
	}
}

/* EV_FIRE_WEAPON / EV_PAIN of a player: N10's _marineShootUntil (.14 s) / _marineHurtUntil (.27 s) */
void CG_CA_MarineEvent( int clientNum, int hurt ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	if ( hurt ) {
		caMarine[clientNum].hurtTime = cg.time;
	} else {
		caMarine[clientNum].shotTime = cg.time;
	}
}

void CG_CA_DrawMarine( centity_t *cent ) {
	const entityState_t *s = &cent->currentState;
	int client = s->clientNum, count, i, nArmour = 0, nFace = 0, downed = ( s->eFlags & EF_CA_DOWNED ) != 0;
	caPoseInput_t in;
	float pose[16], state4[4] = { 0, 0, 0, 0 }, facing, tint[3];
	vec3_t feet;
	if ( client < 0 || client >= MAX_CLIENTS || ( ( s->eFlags & EF_DEAD ) && !downed ) ) {
		return;
	}
	count = CA_EnemyMeshVertexCount( CA_MARINE_TYPE );
	if ( count <= 0 || count > CA_MAX_MESH_VERTS ) {
		return;
	}
	VectorCopy( cent->lerpOrigin, feet );
	feet[2] -= 24;
	facing = DEG2RAD( cent->lerpAngles[YAW] );
	memset( &in, 0, sizeof( in ) );
	in.x = feet[0] / CA_UNITS_PER_METRE;
	in.y = feet[1] / CA_UNITS_PER_METRE;
	in.z = feet[2] / CA_UNITS_PER_METRE;
	in.a = facing;
	if ( !caMarine[client].ready ) {
		memset( &caMarine[client].anim, 0, sizeof( caMarine[client].anim ) );
		caMarine[client].anim.rng = (unsigned)( client * 2654435761u + 977u );
		caMarine[client].ready = 1;
	}
	CA_MarinePose( &caMarine[client].anim, &in, cg.time / 1000.0,
		caMarine[client].shotTime ? ( caMarine[client].shotTime + 140 - cg.time ) / 1000.0 : 0,
		0, caMarine[client].hurtTime ? ( caMarine[client].hurtTime + 270 - cg.time ) / 1000.0 : 0, downed, pose );
	if ( !CA_InView( feet, CA_EnemyHeight( CA_MARINE_TYPE ) * CA_UNITS_PER_METRE ) ) {
		return;
	}
	CA_EnemySkin( CA_MARINE_TYPE, pose, state4, feet, facing, caSkin );
	/* N10 Coop: the host's Marine Cat is tinted green, the guests' blue; a Quake III client does not
	   know which player hosts, so every teammate gets the guests' tint */
	tint[0] = 1 + ( .81f - 1 ) * .28f;
	tint[1] = 1 + ( .89f - 1 ) * .28f;
	tint[2] = 1;
	for ( i = 0 ; i < count ; i++ ) {
		const caSkinVert_t *v = &caSkin[i];
		/* enemy.vert: lambert with the model normal after facing, (-.3, -.6, .625) */
		float lambert = -.3f * v->normal[0] - .6f * v->normal[1] + .625f * v->normal[2], light;
		int k;
		polyVert_t *p;
		lambert = lambert < 0 ? 0 : lambert;
		light = .69f + .33f * lambert;
		if ( v->material >= 16 && v->material <= 20 ) {
			p = &caMarineFacePolys[nFace++];
			for ( k = 0 ; k < 3 ; k++ ) {
				p->modulate[k] = (byte)( 255 * ( .84f + .16f * light ) );
			}
		} else {
			p = &caPolys[nArmour++];
			for ( k = 0 ; k < 3 ; k++ ) {
				float c = 255 * v->color[k] * tint[k] * light * .80f * 1.19f;
				p->modulate[k] = c > 255 ? 255 : (byte)c;
			}
		}
		VectorCopy( v->xyz, p->xyz );
		p->st[0] = v->st[0];
		p->st[1] = v->st[1];
		p->modulate[3] = 255;
	}
	/* the mesh is a triangle soup with one material per triangle */
	if ( nArmour >= 3 ) {
		trap_R_AddPolysToScene( caMarineArmour, 3, caPolys, nArmour / 3 );
	}
	if ( nFace >= 3 ) {
		trap_R_AddPolysToScene( caMarineFace, 3, caMarineFacePolys, nFace / 3 );
	}
}

/* ---- MD3 props (N10 native/src/md3.cpp animate_props) -------------------------------------------
   flag: 16 frames at 12 fps. turret: base (2 frames, 0.6 fps), head on tag_head turning toward the
   player within 22 m (else a slow sweep) at 2.5 rad/s, barrel on tag_barrel spinning at 12 fps while
   the player is within 16 m; the warning light (rgbGen entity) pulses faster then. */
static qhandle_t caPropModel[4];
static struct { int init; float yaw; int last; } caTurret[MAX_GENTITIES];

static void CA_PropFrame( refEntity_t *re, int frames, float fps, float time ) {
	float f = time * fps;
	int i = (int)floor( f );
	re->frame = ( ( i + 1 ) % frames + frames ) % frames;
	re->oldframe = ( i % frames + frames ) % frames;
	re->backlerp = 1.0f - ( f - i );
}

/* Medkit (N10 weapon_fx.cpp: render() 13588 glow and renderGeometry() 12353 billboard): a soft green
   additive glow 0.5 m above the floor, 1.3 m wide, pulsing; the medkit tile of the world atlas on a
   0.65 m quad that bobs 0.20 +- 0.06 m above the floor and turns with the player's yaw. The server
   stops sending it while it recharges. */
void CG_CA_Pickup( centity_t *cent ) {
	static qhandle_t medkit;
	const float U = CA_UNITS_PER_METRE, t = cg.time * 0.001f, x = cent->lerpOrigin[0] / CA_UNITS_PER_METRE;
	const float yaw = DEG2RAD( cg.refdefViewAngles[YAW] ), half = 0.65f * U * 0.5f, h = 0.65f * U;
	const float rx = -sin( yaw ) * half, ry = cos( yaw ) * half;
	const float z = cent->lerpOrigin[2] + ( 0.20f + 0.06f * sin( t * 2 + x ) ) * U;
	const float corner[4][2] = { { -1, 0 }, { 1, 0 }, { 1, 1 }, { -1, 1 } };
	polyVert_t v[4];
	refEntity_t re;
	float a = 0.22f + 0.08f * sin( t * 3 + x );
	int i;
	if ( !medkit ) {
		medkit = trap_R_RegisterShader( "catarena/medkit" );
	}
	memset( &re, 0, sizeof( re ) );
	re.reType = RT_SPRITE;
	VectorCopy( cent->lerpOrigin, re.origin );
	re.origin[2] += 0.5f * U;
	VectorCopy( re.origin, re.oldorigin );
	re.radius = 0.65f * U;
	re.customShader = cgs.media.caGlowShader;
	re.shaderRGBA[0] = (byte)( 0.45f * 255 );
	re.shaderRGBA[1] = 255;
	re.shaderRGBA[2] = (byte)( 0.55f * 255 );
	re.shaderRGBA[3] = (byte)( a * 255 );
	trap_R_AddRefEntityToScene( &re );
	for ( i = 0 ; i < 4 ; i++ ) {
		v[i].xyz[0] = cent->lerpOrigin[0] + corner[i][0] * rx;
		v[i].xyz[1] = cent->lerpOrigin[1] + corner[i][0] * ry;
		v[i].xyz[2] = z + corner[i][1] * h;
		v[i].st[0] = corner[i][0] < 0 ? 0 : 1;
		v[i].st[1] = corner[i][1] > 0 ? 0 : 1;
		v[i].modulate[0] = v[i].modulate[1] = v[i].modulate[2] = v[i].modulate[3] = 255;
	}
	trap_R_AddPolyToScene( medkit, 4, v );
}

void CG_CA_Prop( centity_t *cent ) {
	const entityState_t *s = &cent->currentState;
	refEntity_t base, head, barrel;
	vec3_t angles;
	float scale = s->origin2[0] > 0 ? s->origin2[0] : 1, time = cg.time * 0.001f;
	float phase = fmod( cent->lerpOrigin[0] / CA_UNITS_PER_METRE * .37f + cent->lerpOrigin[1] / CA_UNITS_PER_METRE * .29f, 10.0f );
	int i;
	if ( !caPropModel[0] ) {
		caPropModel[0] = trap_R_RegisterModel( "models/gallery/flag.md3" );
		caPropModel[1] = trap_R_RegisterModel( "models/gallery/turret_base.md3" );
		caPropModel[2] = trap_R_RegisterModel( "models/gallery/turret_head.md3" );
		caPropModel[3] = trap_R_RegisterModel( "models/gallery/turret_barrel.md3" );
	}
	memset( &base, 0, sizeof( base ) );
	VectorCopy( cent->lerpOrigin, base.origin );
	VectorCopy( base.origin, base.oldorigin );
	VectorClear( angles );
	angles[YAW] = s->apos.trBase[YAW];
	AnglesToAxis( angles, base.axis );
	if ( scale != 1 ) {
		for ( i = 0 ; i < 3 ; i++ ) {
			VectorScale( base.axis[i], scale, base.axis[i] );
		}
		base.nonNormalizedAxes = qtrue;
	}
	if ( s->modelindex == 0 ) {
		base.hModel = caPropModel[0];
		CA_PropFrame( &base, 16, 12, time );
		trap_R_AddRefEntityToScene( &base );
		return;
	}
	{
		vec3_t d;
		float aim = sin( time * .4f + phase ) * 1.1f, dist, step, glow;
		int firing = 0, n = s->number;
		VectorSubtract( cg.predictedPlayerState.origin, cent->lerpOrigin, d );
		dist = sqrt( d[0] * d[0] + d[1] * d[1] ) / CA_UNITS_PER_METRE;
		if ( dist < 22 ) {
			aim = atan2( d[1], d[0] ) - DEG2RAD( s->apos.trBase[YAW] );
		}
		firing = dist < 16;
		if ( !caTurret[n].init || cg.time < caTurret[n].last ) {
			caTurret[n].init = 1;
			caTurret[n].yaw = 0;
			caTurret[n].last = cg.time;
		}
		step = 2.5f * Com_Clamp( 0, 0.1f, ( cg.time - caTurret[n].last ) * 0.001f );
		caTurret[n].yaw += Com_Clamp( -step, step, atan2( sin( aim - caTurret[n].yaw ), cos( aim - caTurret[n].yaw ) ) );
		caTurret[n].last = cg.time;
		glow = firing ? .55f + .45f * MAX( 0, sin( time * 9 ) ) : .25f + .1f * sin( time * 2 );
		base.hModel = caPropModel[1];
		CA_PropFrame( &base, 2, .6f, time );
		base.shaderRGBA[0] = (byte)Com_Clamp( 0, 255, glow * 1.1f * 255 );
		base.shaderRGBA[1] = (byte)Com_Clamp( 0, 255, glow * .35f * 255 );
		base.shaderRGBA[2] = (byte)Com_Clamp( 0, 255, glow * .2f * 255 );
		base.shaderRGBA[3] = 255;
		trap_R_AddRefEntityToScene( &base );

		memset( &head, 0, sizeof( head ) );
		head.hModel = caPropModel[2];
		VectorClear( angles );
		angles[YAW] = RAD2DEG( caTurret[n].yaw );
		AnglesToAxis( angles, head.axis );
		CG_PositionRotatedEntityOnTag( &head, &base, base.hModel, "tag_head" );
		Byte4Copy( base.shaderRGBA, head.shaderRGBA );
		head.nonNormalizedAxes = base.nonNormalizedAxes;
		trap_R_AddRefEntityToScene( &head );

		memset( &barrel, 0, sizeof( barrel ) );
		barrel.hModel = caPropModel[3];
		AxisClear( barrel.axis );
		CG_PositionRotatedEntityOnTag( &barrel, &head, head.hModel, "tag_barrel" );
		if ( firing ) {
			CA_PropFrame( &barrel, 8, 12, time );
		}
		Byte4Copy( base.shaderRGBA, barrel.shaderRGBA );
		barrel.nonNormalizedAxes = base.nonNormalizedAxes;
		trap_R_AddRefEntityToScene( &barrel );
	}
}

#endif
