/*
 * Cat Arena: Feral Claw on Quake3e (N10-Q3E stage D part 6): N10's sound rules on Quake III's sound
 * system (N10 native/src/audio_game.cpp = the legacy playGunSound, playExplosionSound,
 * playBodySound, playEnemyVoice, beep and the soundtrack).
 *
 * The samples are rendered by tools/q3e/sound_bake.cpp with N10's gain, bus level, voice tone and
 * filters baked in, because Quake III plays a sample at one volume (s_volume). What runs here are
 * the rules: which sound, which take, and the enemy-voice limits (31 m, 3 voices at once (5 for
 * deaths), per-enemy and global cooldowns). The player's own sounds play at full volume like N10's;
 * enemy voices are placed in the world and Quake III attenuates and pans them (its curve, not
 * N10's). Random choices use the client's rand(), as N10's sound draws do not touch the game.
 *
 * GPL v2 or later (docs/q3e/LICENSING.md).
 */
#include "cg_local.h"

#ifdef CAT_ARENA_STANDALONE

#define CA_SND_RATE      44100
#define CA_SND_MAX       128

typedef struct {
	sfxHandle_t h;
	int ms;                 /* length (sound/catarena/sounds.txt) */
	char name[40];
} caSfx_t;

static caSfx_t caGun[16][2], caExplosion[3], caJump, caSlide[3], caLand[4], caStep[2];
static caSfx_t caVoice[3][4][3];   /* family (normal, bass, light), kind (idle, attack, hurt, eliminated), take */
static caSfx_t caBeepSwitch, caBeepReload, caBeepHurt, caBeepMedkit;
static int caGunTake[16];

static struct { char name[40]; int frames; } caLengths[CA_SND_MAX];
static int caLengthCount;

/* voice limits (GameAudio::voice) */
typedef struct {
	int uid;                /* entityState powerups: a new spawn in this slot */
	int attackTime, hitTime;
	int idleAt;             /* next idle voice (N10 voice_cooldown: 2-7 s after the spawn, then 4.4-9.7 s) */
	int lastVoice;          /* N10 last_meow_ */
} caVoiceState_t;
static caVoiceState_t caVoices[MAX_GENTITIES];
static int caVoiceEnd[8];          /* end times of the voices playing (polyphony) */
static int caVoiceGlobal = -100000, caVoiceLast[4] = { -100000, -100000, -100000, -100000 };

enum { CA_VOICE_IDLE, CA_VOICE_ATTACK, CA_VOICE_HURT, CA_VOICE_ELIMINATED };

static int CA_CvarInt( const char *name ) {
	char buf[16];
	trap_Cvar_VariableStringBuffer( name, buf, sizeof( buf ) );
	return atoi( buf );
}

static float CA_SndRand( void ) {
	return ( rand() & 0x7fff ) / 32768.0f;
}

static void CA_ReadLengths( void ) {
	fileHandle_t f;
	char buf[8192], *p, *line;
	int len = trap_FS_FOpenFile( "sound/catarena/sounds.txt", &f, FS_READ );
	caLengthCount = 0;
	if ( !f ) {
		CG_Printf( S_COLOR_YELLOW "Cat Arena: sound/catarena/sounds.txt missing\n" );
		return;
	}
	if ( len >= (int)sizeof( buf ) ) {
		len = sizeof( buf ) - 1;
	}
	trap_FS_Read( buf, len, f );
	trap_FS_FCloseFile( f );
	buf[len] = 0;
	for ( p = buf; *p && caLengthCount < CA_SND_MAX; ) {
		char *tab;
		line = p;
		while ( *p && *p != '\n' ) {
			p++;
		}
		if ( *p ) {
			*p++ = 0;
		}
		tab = strchr( line, '\t' );
		if ( !tab ) {
			continue;
		}
		*tab = 0;
		Q_strncpyz( caLengths[caLengthCount].name, line, sizeof( caLengths[0].name ) );
		caLengths[caLengthCount].frames = atoi( tab + 1 );
		caLengthCount++;
	}
}

static void CA_Load( caSfx_t *s, const char *name ) {
	int i;
	Q_strncpyz( s->name, name, sizeof( s->name ) );
	s->h = trap_S_RegisterSound( va( "sound/catarena/%s.wav", name ), qfalse );
	s->ms = 0;
	for ( i = 0 ; i < caLengthCount ; i++ ) {
		if ( !Q_stricmp( caLengths[i].name, name ) ) {
			s->ms = caLengths[i].frames * 1000 / CA_SND_RATE;
			break;
		}
	}
	if ( !s->h || !s->ms ) {
		CG_Printf( S_COLOR_YELLOW "Cat Arena: sound %s missing\n", name );
	}
}

void CG_CA_RegisterSounds( void ) {
	static const char *families[3] = { "normal", "bass", "light" };
	static const char *kinds[4] = { "idle", "attack", "hurt", "eliminated" };
	int i, k, t, loaded = 0;
	trap_Cvar_Register( NULL, "ca_music", "1", CVAR_ARCHIVE );
	CA_ReadLengths();
	for ( i = 0 ; i < 16 ; i++ ) {
		for ( t = 0 ; t < 2 ; t++ ) {
			CA_Load( &caGun[i][t], va( "gun%i_%i", i, t ) );
		}
	}
	CA_Load( &caExplosion[0], "explosion_rocket" );
	CA_Load( &caExplosion[1], "explosion_grenade" );
	CA_Load( &caExplosion[2], "explosion_rift" );
	CA_Load( &caJump, "body_jump" );
	for ( i = 0 ; i < 3 ; i++ ) {
		CA_Load( &caSlide[i], va( "body_slide_%i", i ) );
	}
	for ( i = 0 ; i < 4 ; i++ ) {
		CA_Load( &caLand[i], va( "body_land_%i", i ) );
	}
	CA_Load( &caStep[0], "body_step_walk" );
	CA_Load( &caStep[1], "body_step_sprint" );
	for ( i = 0 ; i < 3 ; i++ ) {
		for ( k = 0 ; k < 4 ; k++ ) {
			for ( t = 0 ; t < 3 ; t++ ) {
				CA_Load( &caVoice[i][k][t], va( "voice_%s_%s_%i", families[i], kinds[k], t ) );
			}
		}
	}
	CA_Load( &caBeepSwitch, "beep_switch" );
	CA_Load( &caBeepReload, "beep_reload" );
	CA_Load( &caBeepHurt, "beep_hurt" );
	CA_Load( &caBeepMedkit, "beep_medkit" );
	for ( i = 0 ; i < 16 ; i++ ) {
		loaded += ( caGun[i][0].h != 0 ) + ( caGun[i][1].h != 0 );
	}
	for ( i = 0 ; i < 3 ; i++ ) {
		for ( k = 0 ; k < 4 ; k++ ) {
			for ( t = 0 ; t < 3 ; t++ ) {
				loaded += caVoice[i][k][t].h != 0;
			}
		}
	}
	loaded += ( caExplosion[0].h != 0 ) + ( caExplosion[1].h != 0 ) + ( caExplosion[2].h != 0 ) + ( caJump.h != 0 ) + ( caStep[0].h != 0 ) + ( caStep[1].h != 0 )
		+ ( caSlide[0].h != 0 ) + ( caSlide[1].h != 0 ) + ( caSlide[2].h != 0 ) + ( caLand[0].h != 0 ) + ( caLand[1].h != 0 ) + ( caLand[2].h != 0 ) + ( caLand[3].h != 0 )
		+ ( caBeepSwitch.h != 0 ) + ( caBeepReload.h != 0 ) + ( caBeepHurt.h != 0 ) + ( caBeepMedkit.h != 0 );
	CG_Printf( "Cat Arena: %i sounds registered\n", loaded );
	memset( caVoices, 0, sizeof( caVoices ) );
	memset( caVoiceEnd, 0, sizeof( caVoiceEnd ) );
	memset( caGunTake, 0, sizeof( caGunTake ) );
}

/* ca_log 1: every sound started, for the scripted tests */
static void CA_SndLog( const caSfx_t *s, const char *extra ) {
	if ( CA_CvarInt( "ca_log" ) ) {
		CG_Printf( "Cat Arena: sound %s t=%i ms=%i%s\n", s->name, cg.time, s->ms, extra ? extra : "" );
	}
}

/* the local player's own sounds: full volume, both sides (N10: no panner) */
static void CA_Local( const caSfx_t *s, int channel ) {
	if ( !s->h || !cg.snap ) {
		return;
	}
	trap_S_StartSound( NULL, cg.snap->ps.clientNum, channel, s->h );
	CA_SndLog( s, NULL );
}

static qboolean CA_IsLocal( int entityNum ) {
	return cg.snap && entityNum == cg.snap->ps.clientNum;
}

/* playGunSound(index): two takes, alternating, with a random skip (take = counter++ + (r > .57)) & 1 */
void CG_CA_GunSound( int entityNum, int weapon ) {
	int take;
	if ( weapon < 0 || weapon > 15 ) {
		return;
	}
	take = ( caGunTake[weapon]++ + ( CA_SndRand() > 0.57f ? 1 : 0 ) ) & 1;
	if ( !caGun[weapon][take].h ) {
		take = 1 - take;
	}
	if ( CA_IsLocal( entityNum ) ) {
		CA_Local( &caGun[weapon][take], CHAN_WEAPON );
	} else if ( caGun[weapon][take].h ) {   /* a teammate's gun (co-op, Q3E-E): placed in the world */
		trap_S_StartSound( NULL, entityNum, CHAN_WEAPON, caGun[weapon][take].h );
		CA_SndLog( &caGun[weapon][take], " (teammate)" );
	}
}

/* playExplosionSound(power): not placed in the world in N10 */
void CG_CA_ExplosionSound( int kind ) {
	CA_Local( &caExplosion[ kind == CA_FIRE_GRENADE ? 1 : kind == CA_FIRE_RIFT ? 2 : 0 ], CHAN_AUTO );
}

/* playBodySound: the local player's movement only (N10 plays no body sounds for others) */
void CG_CA_BodySound( int entityNum, int event, int parm ) {
	float force;
	if ( !CA_IsLocal( entityNum ) ) {
		return;
	}
	switch ( event ) {
	case EV_FOOTSTEP:
	case EV_FOOTSTEP_METAL:
		CA_Local( &caStep[ parm == 1 ], CHAN_BODY );
		break;
	case EV_JUMP:
	case EV_JUMP_PAD:
		CA_Local( &caJump, CHAN_BODY );
		break;
	case EV_CA_SLIDE:   /* force min(1.2, speed / 10), baked at .6 / .9 / 1.2 */
		force = parm / 100.0f;
		CA_Local( &caSlide[ force < 0.75f ? 0 : force < 1.05f ? 1 : 2 ], CHAN_AUTO );
		break;
	case EV_CA_LAND:    /* force min(1.3, speed / 10), baked at .3 / .6 / .95 / 1.3 */
		force = parm / 100.0f;
		CA_Local( &caLand[ force < 0.45f ? 0 : force < 0.78f ? 1 : force < 1.12f ? 2 : 3 ], CHAN_AUTO );
		break;
	}
}

/* switchWeapon, reloadWeapon, takeDamage and medkit beeps */
void CG_CA_Beep( int which ) {
	CA_Local( which == 0 ? &caBeepSwitch : which == 1 ? &caBeepReload : which == 3 ? &caBeepMedkit : &caBeepHurt, CHAN_AUTO );
}

static int CA_VoiceFamily( int type ) {
	const caEnemyType_t *t = CA_EnemyType( type );
	const char *n = t ? t->name : "";
	if ( !strcmp( n, "shambler" ) || !strcmp( n, "crusher" ) || !strcmp( n, "brute" ) || !strcmp( n, "warden" ) || !strcmp( n, "skyjuggernaut" ) ) {
		return 1;
	}
	if ( !strcmp( n, "fiend" ) || !strcmp( n, "skitter" ) || !strcmp( n, "stalker" ) || !strcmp( n, "banshee" ) || !strcmp( n, "harrier" ) || !strcmp( n, "skyprobe" ) ) {
		return 2;
	}
	return 0;
}

/* playEnemyVoice(b, kind) with N10's limits */
static void CA_Voice( const entityState_t *s, const vec3_t origin, int kind ) {
	const qboolean death = kind == CA_VOICE_ELIMINATED;
	caVoiceState_t *v = &caVoices[ s->number ];
	int i, playing = 0, slot = -1, take, perEnemy;
	float dx, dy, d;
	const caSfx_t *sfx;
	if ( !cg.snap ) {
		return;
	}
	dx = origin[0] - cg.refdef.vieworg[0];
	dy = origin[1] - cg.refdef.vieworg[1];
	d = sqrt( dx * dx + dy * dy ) / CA_UNITS_PER_METRE;
	for ( i = 0 ; i < 8 ; i++ ) {
		if ( caVoiceEnd[i] > cg.time ) {
			playing++;
		} else if ( slot < 0 ) {
			slot = i;
		}
	}
	if ( d > 31 || ( !death && playing >= 3 ) || playing >= 5 || slot < 0 ) {
		return;
	}
	perEnemy = death ? 0 : kind == CA_VOICE_ATTACK ? 420 : kind == CA_VOICE_HURT ? 280 : 1800;
	if ( ( !death && cg.time < v->lastVoice + perEnemy ) || ( !death && cg.time - caVoiceGlobal < 95 )
		|| ( kind == CA_VOICE_IDLE && cg.time - caVoiceLast[kind] < 1150 ) || ( kind == CA_VOICE_HURT && cg.time - caVoiceLast[kind] < 100 ) ) {
		return;
	}
	v->lastVoice = cg.time;
	caVoiceLast[kind] = cg.time;
	caVoiceGlobal = cg.time;
	take = (int)( CA_SndRand() * 3 ) % 3;
	sfx = &caVoice[ CA_VoiceFamily( s->modelindex ) ][kind][take];
	if ( !sfx->h ) {
		return;
	}
	caVoiceEnd[slot] = cg.time + sfx->ms;
	if ( death ) {   /* the entity goes away: a fixed point */
		trap_S_StartSound( (float *)origin, ENTITYNUM_WORLD, CHAN_AUTO, sfx->h );
	} else {
		trap_S_StartSound( NULL, s->number, CHAN_VOICE, sfx->h );
	}
	CA_SndLog( sfx, va( " enemy=%i type=%s d=%.1f playing=%i", s->number, CA_EnemyType( s->modelindex )->name, d, playing ) );
}

/* Called for every live enemy each frame: idle, attack and hurt voices from its state. */
void CG_CA_EnemyVoices( centity_t *cent ) {
	const entityState_t *s = &cent->currentState;
	caVoiceState_t *v = &caVoices[ s->number ];
	if ( v->uid != s->powerups ) {   /* a new enemy in this slot: no voice for what happened before */
		memset( v, 0, sizeof( *v ) );
		v->uid = s->powerups;
		v->attackTime = s->time;
		v->hitTime = s->time2;
		v->lastVoice = -100000;
		v->idleAt = cg.time + 2000 + (int)( CA_SndRand() * 5000 );
		return;
	}
	if ( s->time != v->attackTime ) {   /* a new attack wind-up (N10 'attack' when the wind-up starts) */
		v->attackTime = s->time;
		if ( s->time > 0 ) {
			CA_Voice( s, cent->lerpOrigin, CA_VOICE_ATTACK );
		}
	}
	if ( s->time2 != v->hitTime ) {     /* damaged: 'hurt' with probability .52 */
		v->hitTime = s->time2;
		if ( s->time2 > 0 && CA_SndRand() < 0.52f ) {
			CA_Voice( s, cent->lerpOrigin, CA_VOICE_HURT );
		}
	}
	if ( cg.time >= v->idleAt ) {
		v->idleAt = cg.time + 4400 + (int)( CA_SndRand() * 5300 );
		CA_Voice( s, cent->lerpOrigin, CA_VOICE_IDLE );
	}
}

void CG_CA_EnemyDeathVoice( centity_t *cent ) {
	CA_Voice( &cent->currentState, cent->lerpOrigin, CA_VOICE_ELIMINATED );
}

/* ensureThemeMusic / syncThemePlayback: the soundtrack loops while in a game; ca_music 0 turns it off
   (N10 music enabled). Its level is s_musicvolume (N10 music 46 % x master 70 % = 0.32). */
void CG_CA_StartMusic( void ) {
	if ( CA_CvarInt( "ca_music" ) ) {
		trap_S_StartBackgroundTrack( "music/ashen_drive.ogg", "music/ashen_drive.ogg" );
		if ( CA_CvarInt( "ca_log" ) ) {
			CG_Printf( "Cat Arena: music music/ashen_drive.ogg\n" );
		}
	} else {
		trap_S_StopBackgroundTrack();
	}
}

#endif
