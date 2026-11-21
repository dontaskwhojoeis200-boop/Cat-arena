/*
 * Cat Arena: Feral Claw — server-side gameplay (qagame) implemented in C++20: enemies and waves.
 * N10-Q3E stage C. The C game code provides the engine-facing operations through caGameImport_t;
 * the C++ side never touches gentity_t. No exception leaves these functions.
 * GPL v2 or later (docs/q3e/LICENSING.md).
 */
#ifndef CA_GAME_H
#define CA_GAME_H

#ifdef __cplusplus
extern "C" {
#endif

#define CA_MAX_ENEMIES      20     /* N10 kMaxEnemies */
#define CA_ENEMY_FIEND       2     /* N10 type index of the fiend */

typedef struct {
	int   clientNum;
	float origin[3];               /* Quake III units */
	float velocity[3];             /* u/s (enemy aim lead) */
	int   alive;
} caPlayer_t;

typedef struct {
	void  (*print)( const char *text );
	int   (*levelTime)( void );                                   /* ms */
	int   (*players)( caPlayer_t *out, int max );                 /* connected, playing clients */
	int   (*spawnPoints)( float (*out)[3], int max );             /* info_ca_enemy_spawn, else deathmatch spawns */
	/* returns the entity number, or -1 when the spot is blocked */
	int   (*spawnEnemy)( int type, const float origin[3], float yaw, int health );
	void  (*removeEnemy)( int entityNum );
	int   (*enemyHealth)( int entityNum );
	void  (*enemyOrigin)( int entityNum, float out[3] );
	/* runs the shared Quake III movement (Pmove) for msec along moveYawDeg; the body faces faceYawDeg */
	void  (*moveEnemy)( int entityNum, float moveYawDeg, float speedUnits, int moving, int msec, float faceYawDeg );
	/* an attack wind-up starts now (the client animates it from the type's timings) */
	void  (*setEnemyState)( int entityNum, int windingUp );
	int   (*visible)( int entityNum, int clientNum );
	void  (*damagePlayer)( int clientNum, int damage, int attackerEntityNum );
	void  (*damageEnemy)( int entityNum, int damage );           /* self-test only (world attacker) */
	void  (*setWaveInfo)( const char *configString );
	/* stage D: N10 navigation data (maps/<mapname>.cadata in the pk3) */
	const char *(*mapName)( void );
	int   (*readFile)( const char *path, unsigned char *buffer, int maxBytes );   /* bytes read, -1 if missing */
	/* stage D part 4: flyers, ranged attacks */
	void  (*setEnemyOrigin)( int entityNum, const float origin[3], float faceYawDeg );   /* flyers: no Pmove */
	int   (*boxFree)( const float from[3], const float to[3], const float mins[3], const float maxs[3] );   /* 1 = no world in the way */
	void  (*fireEnemyShot)( int ownerEntityNum, int type, int kind, const float origin[3], const float velocity[3], int damage, int lifeMs, int targetClient );
} caGameImport_t;

void CA_HordeInit( const caGameImport_t *imports );
void CA_HordeFrame( int msec );                 /* every server frame */
void CA_HordeEnemyKilled( int entityNum );      /* from the enemy's die callback */
void CA_HordeShutdown( void );
int  CA_HordeSelfTest( void );
void CA_HordeLineup( const float origin[3], float yawDeg );   /* cheat ca_lineup: all 20 types, held still */
int  CA_HordeLineupActive( void );                  /* server command ca_selftest: 1 = all checks passed */

#ifdef __cplusplus
}
#endif

#endif /* CA_GAME_H */
