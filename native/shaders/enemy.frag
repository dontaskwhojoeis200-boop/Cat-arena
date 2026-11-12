#version 450
#extension GL_GOOGLE_include_directive : require
// N6 creatures and Marine Cats: the legacy main fragment shader (legacy/index.html 980-1010, 1101,
// 1125-1180). Creature materials (400-419): base colour, metal/roughness and normal map, lit by the
// baked light volume where the creature stands (the "entity light grid"), dynamic lights, a sheen
// term and distance fog. The outline and shadow passes write a constant colour. The Marine Cat uses
// the world atlas (armour) and the cat-face atlas (material 20).
#include "frame.glsl"
#include "enemy.glsl"
layout(set = 1, binding = 0) uniform sampler2D enemyBase;     // creature base colour; Marine Cat: cat-face atlas (5 cells)
layout(set = 1, binding = 1) uniform sampler2D enemyMR;
layout(set = 1, binding = 2) uniform sampler2D enemyNormal;
layout(set = 1, binding = 3) uniform sampler2D lightVolume;   // 24 slices of 76 x 76 probes in a 624 x 234 atlas
layout(location = 0) in vec3 vLocalPos;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec3 vModelNormal;
layout(location = 3) in vec3 vColor;
layout(location = 4) in vec2 vUV;
layout(location = 5) in float vDist;
layout(location = 6) in float vLight;
layout(location = 7) flat in int vMat;
layout(location = 0) out vec4 frag;
// Quake III R_ColorShiftLightingBytes: scale up, then normalize so the brightest channel saturates.
vec3 colorShift(vec3 c) { float m = max(c.r, max(c.g, c.b)); return m > 1.0 ? c / m : c; }
vec3 probeAt(vec3 P) {
    float zLayer = clamp((P.z + 6.0) / 30.0 * 23.0, 0.0, 23.0);
    float low = floor(zLayer), high = min(23.0, low + 1.0);
    vec2 xy = clamp(P.xy / 120.0, vec2(.001), vec2(.999));
    vec2 pixel = 1.0 + xy * 75.0 + vec2(.5);
    vec2 aUV = (vec2(mod(low, 8.0), floor(low / 8.0)) * 78.0 + pixel) / vec2(624.0, 234.0);
    vec2 bUV = (vec2(mod(high, 8.0), floor(high / 8.0)) * 78.0 + pixel) / vec2(624.0, 234.0);
    return mix(texture(lightVolume, aUV), texture(lightVolume, bUV), fract(zLayer)).rgb;
}
void main() {
    int mode = enemyMode();
    if(mode == MODE_SHADOW) { frag = vec4(0., 0., 0., .36); return; }
    if(mode == MODE_XRAY) { frag = vec4(1.0, .67, .19, .66); return; }
    if(vMat >= 400 && vMat < 440) {
        vec3 base = texture(enemyBase, vUV).rgb;
        vec3 mr = texture(enemyMR, vUV).rgb;
        vec3 N = normalize(vModelNormal);
        // Window y points down in Vulkan (legacyProject negates y): negate the y derivatives to get the legacy tangent frame.
        vec3 dp1 = dFdx(vLocalPos), dp2 = -dFdy(vLocalPos);
        vec2 du1 = dFdx(vUV), du2 = -dFdy(vUV);
        vec3 p2 = cross(dp2, N), p1 = cross(N, dp1);
        vec3 T = p2 * du1.x + p1 * du2.x, B = p2 * du1.y + p1 * du2.y;
        float inv = inversesqrt(max(max(dot(T, T), dot(B, B)), .000001));
        N = normalize(mat3(T * inv, B * inv, N) * (texture(enemyNormal, vUV).xyz * 2. - 1.));
        vec3 Ld = normalize(vec3(-.3, .6, .8));
        vec3 light = vec3(.76 + .28 * max(dot(N, Ld), 0.));
        if(E.mode.w > .5) {
            // Quake III light grid: ambient + directed light sampled at the model.
            vec3 g = probeAt(vWorldPos + vec3(0., 0., .4)) * 1.9;
            light = colorShift((g * .78 + g * .52 * max(dot(N, Ld), 0.) + vec3(.14)) * F.params.y * 1.12);
            light = max(light, vec3(.46));   // Quake III keeps a minimum ambient on entities
        }
        light += dlights(vWorldPos, N);
        float rough = clamp(mr.g, .15, 1.), metal = mr.b;
        float sheen = pow(max(dot(N, normalize(vec3(-.15, .45, 1.))), 0.), mix(38., 4., rough)) * (1. - rough) * .18;
        vec3 paint = (base * light + mix(vec3(.04), base, metal) * sheen) * mix(vec3(1.), vColor, .8);
        float fog = feature(FEATURE_FOG) ? clamp((vDist - 13.) * .031, 0., .55) : 0.;
        frag = vec4(mix(paint, F.fogColor.rgb, fog), 1.);
        return;
    }
    if(vMat >= 16 && vMat <= 20) {   // cat face: five cells side by side, alpha cut-out (the legacy uploads this image bottom row first)
        float fi = float(vMat - 16);
        vec2 uv = vec2((fi + clamp(vUV.x, .002, .998)) / 5.0, 1.0 - clamp(vUV.y, .002, .998));
        vec4 face = texture(enemyBase, uv);
        if(face.a < .34) discard;
        frag = vec4(face.rgb * (.84 + .16 * vLight), 1.);
        return;
    }
    // generic world-atlas branch with isGun = 2 (as dynamic.frag)
    int m = vMat;
    vec4 tex = texture(uWorldTex, vec3(clamp(vUV, vec2(.008), vec2(.992)), float(m)));
    if(tex.a < .36) discard;
    vec3 light = vec3(.80);
    if(F.dl.x > 0) light += dlights(vWorldPos, vec3(0.)) * .84;
    vec3 col = vColor * tex.rgb * vLight * light * 1.19;
    if(feature(FEATURE_FOG)) {
        float fog = 1.0 - exp(-max(0.0, vDist - 14.0) * .008);
        fog *= .30 * F.fogColor.w;
        col = mix(col, vec3(.19, .23, .27), fog);
    }
    if(feature(FEATURE_POSTERIZE)) col = floor(clamp(col, 0., 1.) * 55.0) / 55.0;
    frag = vec4(clamp(col, 0., 1.), 1.);
}
