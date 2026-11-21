/*
 * Cat Arena: Feral Claw — client effects of the 16 N10 weapons (N10-Q3E stage D part 3).
 *
 * Presentation only (the server decides every hit): tracers, the railgun trail, the thunderbolt
 * beam, impact sparks, projectile glows with trails, detonations, muzzle flashes, and the frag
 * reroll notice. Colours are the N10 weapon colours (ca_bridge.h). The N10 sprite / particle
 * presentation (WFX) is approximated with additive sprites, camera-facing beams and dynamic
 * lights; a closer port of N10's effect atlas is later work.
 *
 * New file, not part of the ioquake3 import. GPL v2 or later.
 */
#include "cg_local.h"

#ifdef CAT_ARENA_STANDALONE
#include "cg_ca_weapon_models.h"

#define CA_MAX_BEAMS   192
#define CA_MAX_SPRITES 256

typedef struct {
	int   start, life;
	vec3_t from, to;
	float width;
	float rgba[4];
} caBeam_t;

typedef struct {
	int   start, life;
	vec3_t origin, velocity;
	float radius0, radius1;
	float rgba[4];
	float light;          /* dynamic light intensity at the start (0 = none) */
} caSprite_t;

static caBeam_t caBeams[CA_MAX_BEAMS];
static caSprite_t caSprites[CA_MAX_SPRITES];
static int caNextBeam, caNextSprite;

void CG_CA_RegisterEffects( void ) {
	int i;
	cgs.media.caGlowShader = trap_R_RegisterShader( "catarena/fx/glow" );
	cgs.media.caBeamShader = trap_R_RegisterShader( "catarena/fx/beam" );
	for ( i = 0 ; i < CA_WEAPON_COUNT ; i++ ) {
		cgs.media.caWeaponModels[i] = trap_R_RegisterModel( caWeaponModels[i].model );
	}
	memset( caBeams, 0, sizeof( caBeams ) );
	memset( caSprites, 0, sizeof( caSprites ) );
}

qhandle_t CG_CA_WeaponModel( int index ) {
	return index >= 0 && index < CA_WEAPON_COUNT ? cgs.media.caWeaponModels[index] : 0;
}

void CG_CA_WeaponMuzzle( int index, vec3_t out ) {
	if ( index < 0 || index >= CA_WEAPON_COUNT ) {
		index = 0;
	}
	VectorCopy( caWeaponModels[index].muzzle, out );
}

static void CA_Color( int weapon, float scale, float alpha, float out[4] ) {
	const caWeapon_t *w = CA_Weapon( weapon );
	if ( !w ) {
		w = CA_Weapon( 0 );
	}
	out[0] = w->color[0] * scale;
	out[1] = w->color[1] * scale;
	out[2] = w->color[2] * scale;
	out[3] = alpha;
}

static void CA_AddBeam( const vec3_t from, const vec3_t to, float width, const float rgba[4], int life ) {
	caBeam_t *b = &caBeams[ caNextBeam ];
	caNextBeam = ( caNextBeam + 1 ) % CA_MAX_BEAMS;
	b->start = cg.time;
	b->life = life;
	VectorCopy( from, b->from );
	VectorCopy( to, b->to );
	b->width = width;
	Vector4Copy( rgba, b->rgba );
}

static void CA_AddSprite( const vec3_t origin, const vec3_t velocity, float r0, float r1, const float rgba[4], int life, float light ) {
	caSprite_t *s = &caSprites[ caNextSprite ];
	caNextSprite = ( caNextSprite + 1 ) % CA_MAX_SPRITES;
	s->start = cg.time;
	s->life = life;
	VectorCopy( origin, s->origin );
	if ( velocity ) {
		VectorCopy( velocity, s->velocity );
	} else {
		VectorClear( s->velocity );
	}
	s->radius0 = r0;
	s->radius1 = r1;
	Vector4Copy( rgba, s->rgba );
	s->light = light;
}

/* A camera-facing quad from `from` to `to`. */
static void CA_DrawBeam( const vec3_t from, const vec3_t to, float width, const float rgba[4], float fade ) {
	polyVert_t v[4];
	vec3_t dir, view, side;
	byte c[4];
	int i;
	VectorSubtract( to, from, dir );
	if ( VectorNormalize( dir ) < 0.5f ) {
		return;
	}
	VectorSubtract( from, cg.refdef.vieworg, view );
	CrossProduct( dir, view, side );
	if ( VectorNormalize( side ) < 0.001f ) {
		return;
	}
	VectorScale( side, width * 0.5f, side );
	for ( i = 0 ; i < 4 ; i++ ) {
		float x = rgba[i] * fade * 255.0f;
		c[i] = x > 255 ? 255 : x < 0 ? 0 : (byte)x;
	}
	VectorAdd( from, side, v[0].xyz );
	VectorSubtract( from, side, v[1].xyz );
	VectorSubtract( to, side, v[2].xyz );
	VectorAdd( to, side, v[3].xyz );
	v[0].st[0] = 0; v[0].st[1] = 0;
	v[1].st[0] = 0; v[1].st[1] = 1;
	v[2].st[0] = 1; v[2].st[1] = 1;
	v[3].st[0] = 1; v[3].st[1] = 0;
	for ( i = 0 ; i < 4 ; i++ ) {
		v[i].modulate[0] = c[0];
		v[i].modulate[1] = c[1];
		v[i].modulate[2] = c[2];
		v[i].modulate[3] = c[3];
	}
	trap_R_AddPolyToScene( cgs.media.caBeamShader, 4, v );
}

static void CA_DrawSprite( const vec3_t origin, float radius, const float rgba[4], float fade, int renderfx ) {
	refEntity_t re;
	int i;
	memset( &re, 0, sizeof( re ) );
	re.reType = RT_SPRITE;
	VectorCopy( origin, re.origin );
	VectorCopy( origin, re.oldorigin );
	re.radius = radius;
	re.customShader = cgs.media.caGlowShader;
	re.renderfx = renderfx;
	for ( i = 0 ; i < 4 ; i++ ) {
		float x = rgba[i] * fade * 255.0f;
		re.shaderRGBA[i] = x > 255 ? 255 : x < 0 ? 0 : (byte)x;
	}
	trap_R_AddRefEntityToScene( &re );
}

/* Called every frame after the local entities. */
void CG_CA_AddEffects( void ) {
	int i;
	for ( i = 0 ; i < CA_MAX_BEAMS ; i++ ) {
		caBeam_t *b = &caBeams[i];
		int age = cg.time - b->start;
		if ( !b->life || age < 0 || age >= b->life ) {
			continue;
		}
		CA_DrawBeam( b->from, b->to, b->width, b->rgba, 1.0f - (float)age / b->life );
	}
	for ( i = 0 ; i < CA_MAX_SPRITES ; i++ ) {
		caSprite_t *s = &caSprites[i];
		int age = cg.time - s->start;
		float t, radius, fade;
		vec3_t at;
		if ( !s->life || age < 0 || age >= s->life ) {
			continue;
		}
		t = (float)age / s->life;
		radius = s->radius0 + ( s->radius1 - s->radius0 ) * t;
		fade = 1.0f - t;
		VectorMA( s->origin, age * 0.001f, s->velocity, at );
		CA_DrawSprite( at, radius, s->rgba, fade, 0 );
		if ( s->light > 0 ) {
			trap_R_AddLightToScene( at, s->light * fade, s->rgba[0], s->rgba[1], s->rgba[2] );
		}
	}
}

/* Where the local viewmodel's muzzle is now (for tracers of the local player's own shots). */
static void CA_LocalMuzzle( int weapon, vec3_t out ) {
	vec3_t m;
	CG_CA_WeaponMuzzle( weapon, m );
	VectorCopy( cg.refdef.vieworg, out );
	VectorMA( out, m[0], cg.refdef.viewaxis[0], out );
	VectorMA( out, m[1], cg.refdef.viewaxis[1], out );
	VectorMA( out, m[2], cg.refdef.viewaxis[2], out );
}

/* EV_CA_SHOT: one hitscan pellet. */
void CG_CA_Shot( const entityState_t *es ) {
	int weapon = es->eventParm;
	const caWeapon_t *w = CA_Weapon( weapon );
	vec3_t from, to;
	float rgba[4], white[4] = { 1, 1, 1, 1 };
	if ( !w ) {
		return;
	}
	VectorCopy( es->pos.trBase, to );
	VectorCopy( es->origin2, from );
	if ( es->otherEntityNum == cg.snap->ps.clientNum && !cg.renderingThirdPerson ) {
		CA_LocalMuzzle( weapon, from );
	}
	CA_Color( weapon, 1.0f, 0.9f, rgba );
	if ( w->rail ) {
		CA_AddBeam( from, to, 5.0f, rgba, 520 );
		CA_AddBeam( from, to, 1.6f, white, 260 );
	} else if ( w->lightning ) {
		vec3_t mid, dir;
		VectorSubtract( to, from, dir );
		VectorMA( from, 0.5f, dir, mid );
		mid[0] += crandom() * 6;
		mid[1] += crandom() * 6;
		mid[2] += crandom() * 6;
		CA_AddBeam( from, mid, 4.0f, rgba, 95 );
		CA_AddBeam( mid, to, 4.0f, rgba, 95 );
		CA_AddBeam( from, to, 1.2f, white, 60 );
	} else {
		CA_AddBeam( from, to, w->pellets > 1 ? 0.7f : 1.1f, rgba, w->pellets > 1 ? 70 : 85 );
	}
	if ( es->generic1 >= 1 ) {   /* impact: wall 1, enemy 2, head 3 */
		float spark[4];
		CA_Color( weapon, 1.0f, 1.0f, spark );
		if ( es->generic1 >= 2 ) {
			spark[0] = 1.0f; spark[1] = 0.35f; spark[2] = 0.25f;
		}
		CA_AddSprite( to, NULL, es->generic1 == 3 ? 6 : 3, es->generic1 == 3 ? 14 : 8, spark, 160, w->rail ? 120 : 0 );
	}
}

/* EV_CA_BLAST: a detonation (power x 100 in generic1). */
void CG_CA_Blast( const entityState_t *es ) {
	float power = es->generic1 * 0.01f;
	int weapon = es->eventParm == CA_FIRE_GRENADE ? 2 : es->eventParm == CA_FIRE_RIFT ? 10 : 4;
	float rgba[4], core[4] = { 1, 0.95f, 0.85f, 1 };
	vec3_t up = { 0, 0, 24 };
	int i;
	CA_Color( weapon, 1.0f, 1.0f, rgba );
	CA_AddSprite( es->pos.trBase, NULL, 16 * power, 3.35f * CA_UNITS_PER_METRE * power * 0.6f, rgba, 480, 420 * power );
	CA_AddSprite( es->pos.trBase, NULL, 10 * power, 40 * power, core, 220, 0 );
	for ( i = 0 ; i < 6 ; i++ ) {
		vec3_t v;
		float smoke[4] = { 0.25f, 0.23f, 0.22f, 0.55f };
		VectorSet( v, crandom() * 60, crandom() * 60, crandom() * 40 );
		VectorAdd( v, up, v );
		CA_AddSprite( es->pos.trBase, v, 12 * power, 34 * power, smoke, 900 + i * 60, 0 );
	}
}

/* An enemy projectile (N10 EFX enemy shots): a glow in the type's colour, sized by the kind's sweep
   radius, with a trail. */
static void CA_EnemyProjectile( centity_t *cent ) {
	const entityState_t *s = &cent->currentState;
	const caEnemyType_t *def = CA_EnemyType( s->generic1 );
	const caEnemyShot_t *k = CA_EnemyShot( s->otherEntityNum2 - 10 );
	float rgba[4], core[4] = { 1, 1, 1, 0.9f }, radius;
	if ( !def || !k ) {
		return;
	}
	rgba[0] = def->lookColor[0]; rgba[1] = def->lookColor[1]; rgba[2] = def->lookColor[2]; rgba[3] = 1;
	radius = k->radius * 2.6f + 3;
	CA_DrawSprite( cent->lerpOrigin, radius, rgba, 1.0f, 0 );
	CA_DrawSprite( cent->lerpOrigin, radius * 0.4f, core, 1.0f, 0 );
	trap_R_AddLightToScene( cent->lerpOrigin, 60 + k->radius * 18, rgba[0], rgba[1], rgba[2] );
	if ( cg.time - cent->trailTime >= 30 || cg.time < cent->trailTime ) {
		float trail[4];
		Vector4Copy( rgba, trail );
		trail[3] = 0.55f;
		CA_AddSprite( cent->lerpOrigin, NULL, radius * 0.7f, radius * 0.2f, trail, 260, 0 );
		cent->trailTime = cg.time;
	}
}

/* EV_CA_ENEMY_IMPACT: an enemy projectile hit a player or the world, or burst. */
void CG_CA_EnemyImpact( const entityState_t *es ) {
	const caEnemyType_t *def = CA_EnemyType( es->generic1 );
	const caEnemyShot_t *k = CA_EnemyShot( es->eventParm );
	float rgba[4];
	if ( !def || !k ) {
		return;
	}
	rgba[0] = def->lookColor[0]; rgba[1] = def->lookColor[1]; rgba[2] = def->lookColor[2]; rgba[3] = 1;
	CA_AddSprite( es->pos.trBase, NULL, k->radius * 3, k->radius * 9 + 10, rgba, es->eventParm >= CA_SHOT_FIREBALL ? 420 : 220, 160 + k->radius * 20 );
}

/* WFX.gibs (approximation): a burst of dark red pieces. */
void CG_CA_Gibs( const vec3_t origin, float scale ) {
	int i;
	for ( i = 0 ; i < 12 ; i++ ) {
		vec3_t at, v;
		float blood[4] = { 0.45f, 0.04f, 0.03f, 0.95f };
		VectorCopy( origin, at );
		at[2] += crandom() * 16;
		VectorSet( v, crandom() * 160, crandom() * 160, 60 + random() * 180 );
		CA_AddSprite( at, v, ( 5 + random() * 5 ) * scale, ( 9 + random() * 6 ) * scale, blood, 600 + (int)( random() * 500 ), 0 );
	}
}

/* ET_MISSILE of a Cat Arena projectile: glow, light and a trail. */
void CG_CA_Projectile( centity_t *cent ) {
	const entityState_t *s = &cent->currentState;
	int weapon = s->generic1, kind = s->otherEntityNum2;
	if ( kind >= 10 ) {
		CA_EnemyProjectile( cent );
		return;
	}
	float rgba[4], radius = kind == CA_FIRE_RIFT ? 14 : kind == CA_FIRE_GRENADE ? 7 : 9;
	CA_Color( weapon, 1.0f, 1.0f, rgba );
	CA_DrawSprite( cent->lerpOrigin, radius, rgba, 1.0f, 0 );
	trap_R_AddLightToScene( cent->lerpOrigin, kind == CA_FIRE_RIFT ? 220 : 150, rgba[0], rgba[1], rgba[2] );
	if ( cg.time - cent->trailTime >= 25 || cg.time < cent->trailTime ) {
		float trail[4];
		CA_Color( weapon, kind == CA_FIRE_GRENADE ? 0.5f : 0.8f, 0.6f, trail );
		CA_AddSprite( cent->lerpOrigin, NULL, radius * 0.6f, radius * 1.4f, trail, kind == CA_FIRE_ROCKET ? 420 : 260, 0 );
		cent->trailTime = cg.time;
	}
}

/* Muzzle flash on the viewmodel (called by CG_AddViewWeapon). */
void CG_CA_MuzzleFlash( int weapon, const vec3_t muzzle ) {
	float rgba[4];
	const caWeapon_t *w = CA_Weapon( weapon );
	float size = !w ? 4 : w->kind != CA_FIRE_HITSCAN ? 7 : w->pellets > 1 ? 6.5f : 4.5f;
	CA_Color( weapon, 1.0f, 1.0f, rgba );
	CA_DrawSprite( muzzle, size, rgba, 1.0f, RF_DEPTHHACK | RF_FIRST_PERSON );
	trap_R_AddLightToScene( muzzle, 170, rgba[0], rgba[1], rgba[2] );
}

/* EV_CA_REROLL on the local player: the new weapon's name. */
void CG_CA_Reroll( const entityState_t *es ) {
	const caWeapon_t *w = CA_Weapon( es->eventParm );
	if ( w && es->number == cg.snap->ps.clientNum ) {
		CG_CenterPrint( va( "%s", w->name ), SCREEN_HEIGHT * 0.62f, BIGCHAR_WIDTH * 0.75f );
	}
}

#endif
