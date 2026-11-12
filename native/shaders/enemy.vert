#version 450
#extension GL_GOOGLE_include_directive : require
// N6 creatures and Marine Cats: the legacy main vertex shader with instanced = 1 (creatures) or 3
// (Marine Cat), isGun = 2 (legacy/index.html 872-942). A hierarchical skeleton evaluated per vertex:
// every triangle belongs to one limb chain (tag in the fraction of the material id: 0 torso, 1/2 legs,
// 3/4 arms, 5/6 wings); knees, elbows, wing tips, neck and spine bend with weights computed from the
// rest position. The 16 pose angles come from the CPU (cat::fx::eval_pose).
#include "frame.glsl"
#include "enemy.glsl"
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 color;
layout(location = 3) in vec2 uv;
layout(location = 4) in float material;
layout(location = 5) in vec4 instPos;     // x y z, facing
layout(location = 6) in vec4 instState;   // hit flash, fall, side roll, sink
layout(location = 7) in vec4 instPose0;
layout(location = 8) in vec4 instPose1;
layout(location = 9) in vec4 instPose2;
layout(location = 10) in vec4 instPose3;
layout(location = 11) in vec4 instTint;   // x = floor height (shadow); Marine Cat: rgb colour
layout(location = 0) out vec3 vLocalPos;
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out vec3 vModelNormal;
layout(location = 3) out vec3 vColor;
layout(location = 4) out vec2 vUV;
layout(location = 5) out float vDist;
layout(location = 6) out float vLight;
layout(location = 7) flat out int vMat;
vec3 rX(vec3 p, float a) { float c = cos(a), s = sin(a); return vec3(p.x, c * p.y - s * p.z, s * p.y + c * p.z); }
vec3 rY(vec3 p, float a) { float c = cos(a), s = sin(a); return vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z); }
vec3 rZ(vec3 p, float a) { float c = cos(a), s = sin(a); return vec3(c * p.x - s * p.y, s * p.x + c * p.y, p.z); }
void main() {
    int mode = enemyMode();
    bool creature = mode != MODE_MARINE;   // instanced == 1
    vec3 v = position;
    vec3 modelNormal = normal;
    if(mode == MODE_XRAY) v = position * 1.045;
    float bone = floor(fract(material) * 25.0 + .48);
    if(creature && !(material >= 400.0 && material < 440.0)) bone = 0.0;
    float hipZ = E.rig0.x, kneeZ = E.rig0.y, shZ = E.rig0.z, elZ = E.rig0.w;
    float neckZ = E.rig1.x, spineZ = E.rig1.y, wingX = E.rig1.z, H = max(.5, E.rig1.w);
    float shX = E.rig2.x, wingTip = E.rig2.y, depthY = E.rig2.z;
    bool left = (bone == 1.0 || bone == 3.0 || bone == 5.0);
    float sg = left ? 1.0 : -1.0;
    float band = H * .085;
    vec3 p = v, n = modelNormal;
    float spineW = 1.0;
    if(bone == 1.0 || bone == 2.0) {
        float hip = left ? instPose0.x : instPose0.y, knee = left ? instPose0.z : instPose0.w;
        float wk = 1.0 - smoothstep(kneeZ - band, kneeZ + band, p.z);
        vec3 kp = vec3(0., 0., kneeZ); p = rX(p - kp, knee * wk) + kp; n = rX(n, knee * wk);
        vec3 hp = vec3(0., 0., hipZ); p = rX(p - hp, hip) + hp; n = rX(n, hip);
        spineW = 0.0;
    } else if(bone == 3.0 || bone == 4.0) {
        float sp = left ? instPose1.x : instPose1.y, el = left ? instPose1.z : instPose1.w, sr = left ? instPose2.x : instPose2.y;
        float wf = 1.0 - smoothstep(elZ - band, elZ + band, p.z);
        vec3 ep = vec3(-sg * shX, 0., elZ); p = rX(p - ep, el * wf) + ep; n = rX(n, el * wf);
        vec3 sp3 = vec3(-sg * shX, 0., shZ); p = rY(p - sp3, sr * sg) + sp3; n = rY(n, sr * sg); p = rX(p - sp3, sp) + sp3; n = rX(n, sp);
    } else if(bone == 5.0 || bone == 6.0) {
        float flap = left ? instPose0.x : instPose0.y, tip = left ? instPose0.z : instPose0.w;
        float wt = smoothstep(wingX, mix(wingX, wingTip, .75), abs(p.x));
        vec3 tp = vec3(-sg * mix(wingX, wingTip, .45), 0., shZ); p = rY(p - tp, tip * wt * sg) + tp; n = rY(n, tip * wt * sg);
        vec3 rp = vec3(-sg * wingX, 0., shZ); p = rY(p - rp, flap * sg) + rp; n = rY(n, flap * sg);
    } else {
        float wh = smoothstep(neckZ - H * .05, neckZ + H * .05, p.z);
        vec3 np = vec3(0., 0., neckZ); p = rZ(p - np, instPose3.y * wh) + np; n = rZ(n, instPose3.y * wh); p = rX(p - np, -instPose3.x * wh) + np; n = rX(n, -instPose3.x * wh);
        spineW = smoothstep(spineZ - band * 1.5, spineZ + band * 1.5, p.z);
    }
    // spine: forward lean (+) and twist, carried by the torso and both arms
    vec3 sp = vec3(0., 0., spineZ);
    p = rZ(p - sp, instPose2.w * spineW) + sp; n = rZ(n, instPose2.w * spineW);
    p = rX(p - sp, -instPose2.z * spineW) + sp; n = rX(n, -instPose2.z * spineW);
    // root: side sway about the feet and vertical bob (feet stay planted)
    p = rY(p, instPose3.w); n = rY(n, instPose3.w);
    p.z += instPose3.z * ((bone == 1.0 || bone == 2.0) ? smoothstep(0., max(.05, kneeZ), v.z) : 1.0);
    if(creature && (abs(instState.y) > .0001 || abs(instState.z) > .0001)) {
        // death: topple about the heels (backward +, forward -) or roll to a side
        float fall = instState.y; vec3 fp = vec3(0., (fall > 0.0 ? -1.0 : 1.0) * depthY * .42, 0.);
        p = rX(p - fp, fall) + fp; n = rX(n, fall);
        float side = instState.z; vec3 rp = vec3((side > 0.0 ? -1.0 : 1.0) * E.rig2.w * .5, 0., 0.);
        p = rY(p - rp, side) + rp; n = rY(n, side);
        p.z += max(0., sin(min(abs(fall) + abs(side), 1.5708))) * depthY * .12 - instState.w;
    }
    v = p; modelNormal = n;
    float c = cos(instPos.w), s = sin(instPos.w);
    v.xy = vec2(c * v.x - s * v.y, s * v.x + c * v.y);
    modelNormal.xy = vec2(c * modelNormal.x - s * modelNormal.y, s * modelNormal.x + c * modelNormal.y);
    vec3 wp = v + instPos.xyz;
    if(mode == MODE_SHADOW) {
        // Quake III cg_shadows 3: the posed body flattened onto its floor along the sun.
        float floorZ = instTint.x, hgt = max(0., wp.z - floorZ);
        wp.xy += E.mode.yz * hgt; wp.z = floorZ + .035;
    }
    vec3 r = wp - F.camPos.xyz;
    gl_Position = legacyProject(r);
    vLocalPos = v; vWorldPos = wp; vModelNormal = modelNormal;
    vColor = creature ? mix(color, vec3(1.0, .33, .24), clamp(instState.x, 0., 1.) * .92) : color * mix(vec3(1.), instTint.rgb, .28);
    vUV = uv; vDist = length(r); vMat = int(material + .5);
    float lambert = max(dot(modelNormal, vec3(-.300, -.600, .625)), 0.);
    vLight = .69 + .33 * lambert;   // isGun == 2
}
