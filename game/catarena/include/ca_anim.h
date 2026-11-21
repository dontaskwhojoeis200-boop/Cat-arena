/*
 * Cat Arena: Feral Claw — enemy animation for cgame (N10-Q3E stage D part 4).
 * A C interface to the C++20 port of N10's creature posing (native/src/enemy_fx.cpp: enemyEvalPose,
 * enemyDeathState) and of the skeleton deformation N10 does in its vertex shader
 * (native/shaders/enemy.vert), evaluated on the CPU so the posed body can be drawn as polygons.
 * GPL v2 or later (docs/q3e/LICENSING.md).
 */
#ifndef CA_ANIM_H
#define CA_ANIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* per creature animation state (N10 b._anim); zero-initialise for a new creature */
typedef struct {
	int ready;
	double phase, speed, walk, px, py, pz, pa, turn, pain, painDir, seed, lastT, look, lookP;
	unsigned rng;
} caAnimState_t;

/* N10 metres and N10 facing (0 = model +Y; = Quake III yaw - 90 degrees) */
typedef struct {
	double x, y, z, a, hitFlash;     /* hit flash in seconds left (N10 hit_flash, .21 after a hit) */
	double attackProgress;           /* -1 idle, 0 .. .72 wind-up, .72 .. 1 recovery */
	int hasTarget;
	double tx, ty, tz;
} caPoseInput_t;

typedef struct {
	float xyz[3];      /* Quake III world */
	float normal[3];
	float st[2];
	float color[3];    /* N10 vertex colour with the hit flash (enemy.vert vColor) */
} caSkinVert_t;

/* attack progress from the time since the wind-up began (N10 enemyAttackProgress), -1 when idle */
double CA_AttackProgress( int type, int msSinceAttack );
/* enemyEvalPose: advances the state to `time` (seconds) and writes the 16 joint angles */
void CA_AnimPose( caAnimState_t *state, int type, const caPoseInput_t *in, double time, float pose[16] );
/* enemyStartDeath: variant (0-2, flyers 3) and side (-1 / 1) of a new body */
void CA_AnimDeathStart( caAnimState_t *state, int type, int *variant, float *side );
/* enemyDeathState: the pose `t` seconds after death; state3 = fall, side roll, sink (metres) */
void CA_AnimDeath( int type, const float atDeath[16], int variant, float side, double t, float pose[16], float state3[3] );

/* N10 mesh (.emesh, CATENM01) of a type; returns the vertex count or 0 */
int CA_EnemyMeshLoad( int type, const unsigned char *data, int bytes );
int CA_EnemyMeshVertexCount( int type );
/* the posed mesh: state4 = hit flash (0-1), fall, side roll, sink; feet in Quake III units;
   facing = Quake III yaw in radians. Writes CA_EnemyMeshVertexCount(type) vertices (triangles). */
void CA_EnemySkin( int type, const float pose[16], const float state4[4], const float feet[3], float facing, caSkinVert_t *out );
/* model height in metres (rig height), for culling and effects */
float CA_EnemyHeight( int type );

#ifdef __cplusplus
}
#endif

#endif /* CA_ANIM_H */
