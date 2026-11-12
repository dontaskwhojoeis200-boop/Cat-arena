// N6 creature passes: push constants shared by enemy.vert / enemy.frag.
layout(push_constant, std430) uniform EnemyPush {
    vec4 rig0;   // hip, knee, shoulder, elbow heights (legacy enemyRig0)
    vec4 rig1;   // neck, spine, wing root |x|, height
    vec4 rig2;   // shoulder |x|, wing tip |x|, body depth, body half width
    vec4 mode;   // x: 0 creature, 1 "final five" outline, 2 projected shadow, 3 Marine Cat; yz: shadow direction; w: 1 = light grid on
} E;
const int MODE_CREATURE = 0, MODE_XRAY = 1, MODE_SHADOW = 2, MODE_MARINE = 3;
int enemyMode() { return int(E.mode.x + .5); }
