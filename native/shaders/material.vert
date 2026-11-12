#version 450
#extension GL_GOOGLE_include_directive : require
// Port of Q3MAT vsSource (legacy 3810-3891) with uModel = identity (static surfaces / sky dome).
#include "frame.glsl"
#include "material_data.glsl"
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aUV;
layout(location = 4) in vec2 aLmUV;
layout(location = 5) in vec4 aSprite;
layout(location = 0) out vec4 vST[4];
layout(location = 4) out vec4 vColor;
layout(location = 5) out vec4 vDiffuse;
layout(location = 6) out vec3 vWorld;
layout(location = 7) out float vDist;
layout(location = 8) out vec3 vNormal;
layout(location = 9) out vec3 vKit;
layout(location = 10) out vec2 vLm;

float q3wave(int f, vec4 w, float extra) {   // w = (base, amp, phase, freq)
    float uTime = D.skyp.z;
    float x = w.z + extra + uTime * w.w;
    if(f == 5) {
        float t = (uTime + w.z + extra) * w.w; float i = floor(t), fr = fract(t);
        float a = fract(sin(i * 12.9898) * 43758.5453) * 2. - 1., b = fract(sin((i + 1.) * 12.9898) * 43758.5453) * 2. - 1.;
        return w.x + w.y * mix(a, b, fr * fr * (3. - 2. * fr));
    }
    x = fract(x); float v = 0.;
    if(f == 0) v = sin(x * 6.28318530718); else if(f == 1) v = x < .25 ? x * 4. : (x < .75 ? 2. - x * 4. : x * 4. - 4.);
    else if(f == 2) v = x < .5 ? 1. : -1.; else if(f == 3) v = x; else v = 1. - x;
    return w.x + w.y * v;
}
float hash13(vec3 p) { p = fract(p * .1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y) * p.z); }
float vnoise3(vec3 p) {
    vec3 i = floor(p), f = fract(p); f = f * f * (3. - 2. * f);
    return mix(mix(mix(hash13(i), hash13(i + vec3(1, 0, 0)), f.x), mix(hash13(i + vec3(0, 1, 0)), hash13(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash13(i + vec3(0, 0, 1)), hash13(i + vec3(1, 0, 1)), f.x), mix(hash13(i + vec3(0, 1, 1)), hash13(i + vec3(1, 1, 1)), f.x), f.y), f.z) * 2. - 1.;
}
vec2 tcMods(int k, vec2 st, vec3 wp) {
    st = vec2(dot(D.st[k].preS.xyz, vec3(st, 1.)), dot(D.st[k].preT.xyz, vec3(st, 1.)));
    vec4 tb = D.st[k].turb;
    if(tb.w > 0.) {
        float now = tb.y + D.skyp.z * tb.z;
        st += vec2(sin(((wp.x + wp.z) * Q3U / 1024. + now) * 6.28318530718), sin((wp.y * Q3U / 1024. + now) * 6.28318530718)) * tb.x;
        st = vec2(dot(D.st[k].postS.xyz, vec3(st, 1.)), dot(D.st[k].postT.xyz, vec3(st, 1.)));
    }
    return st;
}

void main() {
    vec3 p = aPos, n = aNormal; vec2 uv = aUV;
    vec3 camPos = F.camPos.xyz, camRight = F.camRight.xyz, camUp = F.camUp.xyz, camForward = F.camForward.xyz;
    if(D.h0.w == 1) {   // deformVertexes autosprite
        vec3 mid = aSprite.xyz; float corner = mod(aSprite.w, 4.); float r = floor(aSprite.w / 4.) / 256.;
        vec3 R = camRight * r, U = camUp * r;
        if(corner < .5) { p = mid - R + U; uv = vec2(0., 0.); } else if(corner < 1.5) { p = mid + R + U; uv = vec2(1., 0.); }
        else if(corner < 2.5) { p = mid + R - U; uv = vec2(1., 1.); } else { p = mid - R - U; uv = vec2(0., 1.); }
        n = -camForward;
    } else if(D.h0.w == 2) {   // autosprite2: rotate about the long axis toward the viewer
        vec3 mid = aSprite.xyz; float corner = mod(aSprite.w, 4.); float w = floor(aSprite.w / 4.) / 256.; vec3 A = aNormal;
        vec3 toCam = normalize(camPos - mid); vec3 P = normalize(cross(A, toCam)) * w;
        if(corner < .5) { p = mid - A + P; uv = vec2(0., 0.); } else if(corner < 1.5) { p = mid + A + P; uv = vec2(1., 0.); }
        else if(corner < 2.5) { p = mid + A - P; uv = vec2(1., 1.); } else { p = mid - A - P; uv = vec2(0., 1.); }
        n = toCam;
    }
    for(int i = 0; i < 3; i++) {
        if(i >= D.h1.x) break;
        int ty = D.dType[i]; vec4 A = D.dA[i], B = D.dB[i];
        if(ty == 1) { float off = (p.x + p.y + p.z) * Q3U * B.x; p += n * q3wave(int(B.y), A, off) / Q3U; }
        else if(ty == 2) { float now = D.skyp.z * A.z; float sc = sin(uv.x * A.x + now) * A.y; p += n * sc / Q3U; }
        else if(ty == 3) { p += B.xyz * q3wave(int(B.w), A, 0.) / Q3U; }
        else if(ty == 4) { vec3 q = p * Q3U * .98 * .0625; float t = D.skyp.z * A.y;
            n = normalize(n + vec3(vnoise3(q + vec3(t, 0, 0)), vnoise3(q + vec3(0, t, 7.)), vnoise3(q + vec3(3., 5., t))) * A.x); }
    }
    bool sky = D.h1.z == 1;
    if(sky) {   // Q3 R_InitSkyTexCoords cloud projection (radiusWorld 4096)
        float a = D.skyp.x; vec3 d = normalize(vec3(cos(a) * p.x - sin(a) * p.y, sin(a) * p.x + cos(a) * p.y, p.z));
        float rw = 4096., hc = D.skyp.y; vec3 sv = d;
        float dd = dot(sv, sv);
        float q = (-2. * sv.z * rw + 2. * sqrt(sv.z * sv.z * rw * rw + 2. * (sv.x * sv.x + sv.y * sv.y + sv.z * sv.z) * rw * hc + dd * hc * hc)) / (2. * dd);
        vec3 cv = sv * q; cv.z += rw; cv = normalize(cv); uv = vec2(acos(clamp(cv.x, -1., 1.)), acos(clamp(cv.y, -1., 1.)));
        p = camPos + p * 200.;
    }
    vec3 r = p - camPos;
    float depth = dot(r, camForward);
    gl_Position = sky ? vec4(dot(r, camRight) / F.camForward.w, -dot(r, camUp) / F.camRight.w, depth, depth) : legacyProject(r);
    vWorld = p; vDist = length(r); vNormal = n; vColor = aColor; vLm = aLmUV;
    // rgbGen lightingDiffuse / alphaGen lightingSpecular with Q3MAT DEFAULT_LIGHT
    vec3 L = normalize(vec3(-.3, -.6, .75));
    float ndl = max(dot(n, L), 0.); vec3 vw = normalize(camPos - p); vec3 refl = 2. * dot(n, L) * n - L;
    vDiffuse = vec4(vec3(.45, .45, .48) + vec3(.55, .52, .5) * ndl, pow(max(dot(refl, vw), 0.), 4.));
    // vertex-lit models: their lightmap texel per vertex (uKitBaked=1 once the lightmap is ready)
    vKit = D.h1.y == 1 ? textureLod(uLightmap, aLmUV, 0.).rgb * F.camUp.w : vec3(1.);
    vec2 sts[8];
    for(int k = 0; k < 8; k++) {
        if(k >= D.h0.x) { sts[k] = vec2(0.); continue; }
        vec2 st; int g = D.st[k].a.y;
        if(g == 1) st = aLmUV;
        else if(g == 2) { vec3 vv = normalize(camPos - p); float dn = dot(n, vv); vec3 rf = n * 2. * dn - vv; st = vec2(.5 + rf.y * .5, .5 - rf.z * .5); }
        else if(g == 3) st = vec2(dot(p * Q3U, D.st[k].tcS.xyz), dot(p * Q3U, D.st[k].tcT.xyz));
        else st = uv;
        sts[k] = tcMods(k, st, p);
    }
    for(int k = 0; k < 4; k++) vST[k] = vec4(sts[2 * k], sts[2 * k + 1]);
}
