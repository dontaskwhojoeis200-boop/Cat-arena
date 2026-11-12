// Shared frame state (set 0). Included by world/material/sky shaders.
// Projection reproduces legacy render(): gl_Position=(dot(r,right)/(tanHalf*aspect), dot(r,up)/tanHalf,
// depth*1.00025003-0.1000125, depth) [GL], converted to Vulkan clip space with identical window depth.
layout(set = 0, binding = 0, std140) uniform Frame {
    vec4 camPos;      // xyz eye, w = shader time (legacy `now`, seconds)
    vec4 camForward;  // xyz, w = tan(hfov/2)
    vec4 camRight;    // xyz, w = tan(hfov/2)/aspect
    vec4 camUp;       // xyz, w = Q3MAT lightScale = 1.9*lmIntensity*(overbright?1.32:1)
    vec4 fogColor;    // theme fog rgb, w = fogStrength setting
    vec4 params;      // x lmIntensity, y mapOverbright, z viewport width, w viewport height
    vec4 skyEye;      // sky bake eye (WORLD.meta.center, z=2), w unused
    ivec4 flags;      // x feature mask, y debug view, z/w unused
    vec4 skyBox;      // N4 image sky box: x rotation (rad), y horizon fog (uSkyFog), z 1 when present
    ivec4 dl;         // N5.1 dynamic lights: x count (0..16)
    vec4 dlPos[16];   // xyz position, w radius
    vec4 dlCol[16];   // rgb colour
} F;
layout(set = 0, binding = 1) uniform sampler2DArray uWorldTex;   // 16-layer world atlas (Q3R.worldArray)
layout(set = 0, binding = 2) uniform sampler2D uLightmap;        // baked lightmap atlas (Q3LM.texture)
layout(set = 0, binding = 3) uniform sampler2DArray uMatTex;     // 256 px material array (UNIT_MAT)
layout(set = 0, binding = 4) uniform sampler2DArray uKitTex;     // 512 px kit array (UNIT_KIT)
layout(set = 0, binding = 5) uniform sampler2D uPortalTex;       // portal/mirror feed (N2: black)
layout(set = 0, binding = 6) uniform sampler2DArray uBoxTex;     // six-face sky box, layers rt bk lf ft up dn (UNIT_BOX)

const int FEATURE_LIGHTMAP = 1, FEATURE_FOG = 2, FEATURE_POSTERIZE = 4;
bool feature(int bit) { return (F.flags.x & bit) != 0; }

// Dynamic lights of the legacy main program (dlights, index.html 958): squared linear falloff to the
// radius, wrapped Lambert term; a zero normal means "no normal" (full contribution).
vec3 dlights(vec3 P, vec3 N) {
    vec3 sum = vec3(0.);
    for(int i = 0; i < 16; i++) {
        if(i >= F.dl.x) break;
        vec3 L = F.dlPos[i].xyz - P;
        float d = length(L), r = F.dlPos[i].w;
        if(d < r) { float att = 1. - d / r; att *= att; float ndl = dot(N, N) > .01 ? clamp(dot(N, L / max(d, .001)) * .7 + .3, 0., 1.) : 1.; sum += F.dlCol[i].rgb * att * ndl; }
    }
    return sum;
}
vec4 legacyProject(vec3 r) {
    float depth = dot(r, F.camForward.xyz);
    return vec4(dot(r, F.camRight.xyz) / F.camForward.w, -dot(r, F.camUp.xyz) / F.camRight.w,
                depth * 1.000125015 - 0.05000625, depth);
}
