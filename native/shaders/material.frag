#version 450
#extension GL_GOOGLE_include_directive : require
// Port of Q3MAT fsSource (legacy 3892-3965), non-box variant. Dynamic lights are not part of the
// static N2 scene (uDlights = 0); the user clip plane is the identity (0,0,0,1).
#include "frame.glsl"
#include "material_data.glsl"
layout(location = 0) in vec4 vST[4];
layout(location = 4) in vec4 vColor;
layout(location = 5) in vec4 vDiffuse;
layout(location = 6) in vec3 vWorld;
layout(location = 7) in float vDist;
layout(location = 8) in vec3 vNormal;
layout(location = 9) in vec3 vKit;
layout(location = 10) in vec2 vLm;
layout(location = 0) out vec4 frag;

vec4 bf(int f, vec4 src, vec4 dst) {
    if(f == 0) return vec4(0.); if(f == 1) return vec4(1.); if(f == 2) return src; if(f == 3) return 1. - src;
    if(f == 4) return dst; if(f == 5) return 1. - dst; if(f == 6) return vec4(src.a); if(f == 7) return vec4(1. - src.a);
    if(f == 8) return vec4(dst.a); if(f == 9) return vec4(1. - dst.a);
    float sa = min(src.a, 1. - dst.a); return vec4(sa, sa, sa, 1.);
}
bool alphaOk(int f, float a) { return f == 0 || (f == 1 && a > 0.) || (f == 2 && a < .5) || (f == 3 && a >= .5); }
vec4 stageRGBA(int k, vec4 tex) {
    vec4 c = D.st[k].cst; int rg = D.st[k].a.z, ag = D.st[k].a.w;
    if(rg == 1) c.rgb = vColor.rgb; else if(rg == 2) c.rgb = vColor.rgb; else if(rg == 3) c.rgb = 1. - vColor.rgb; else if(rg == 4) c.rgb = vDiffuse.rgb;
    if(ag == 1) c.a = vColor.a; else if(ag == 2) c.a = 1. - vColor.a; else if(ag == 3) c.a = vDiffuse.a;
    else if(ag == 4) c.a = clamp(vDist / max(D.st[k].misc.y, .001), 0., 1.);
    return tex * c;
}
vec2 stageST(int k) { vec4 v = vST[k >> 1]; return (k & 1) == 1 ? v.zw : v.xy; }
vec4 sampleStage(int k) {
    vec2 st = stageST(k);
    if(D.st[k].b.w == 1) st = clamp(st, vec2(.002), vec2(.998));
    int src = D.st[k].a.x; float layer = D.st[k].misc.x;
    if(src == 0) return texture(uMatTex, vec3(st, layer));
    if(src == 1) return vec4(texture(uLightmap, st).rgb * F.camUp.w, 1.);
    if(src == 2) return vec4(1.);
    if(src == 3) return texture(uPortalTex, gl_FragCoord.xy / F.params.zw);
    if(src == 5) return texture(uPortalTex, st);
    if(src == 6) return texture(uKitTex, vec3(st, layer));
    if(src == 7) return vec4(vKit, 1.);
    return texture(uWorldTex, vec3(st, layer));
}
void main() {
    int N = D.h0.x;
    // Sample every stage first (legacy order), so implicit derivatives stay defined before discard.
    vec4 t[8];
    for(int k = 0; k < 8; k++) t[k] = k < N ? sampleStage(k) : vec4(0.);
    vec4 acc = stageRGBA(0, t[0]);
    if(!alphaOk(D.st[0].b.z, acc.a)) discard;
    if(D.h0.y == 1) {
        for(int k = 1; k < 8; k++) {
            if(k >= N) break;
            vec4 c = stageRGBA(k, t[k]);
            if(alphaOk(D.st[k].b.z, c.a)) acc = clamp(c * bf(D.st[k].b.x, c, acc) + acc * bf(D.st[k].b.y, c, acc), 0., 1.);
        }
    }
    if(F.flags.y == 1) acc = vec4((D.h1.y == 1 ? vKit : texture(uLightmap, vLm).rgb * F.camUp.w) * .62, 1.);
    if(F.flags.y == 3) acc = vec4(normalize(vNormal) * .5 + .5, 1.);
    // Dynamic lights (Q3MAT fragment shader, 3955): only on lightmapped opaque materials and only in
    // the first pass of a multi-pass material (D.h1.w, set by the renderer like dlightUniforms / 4144).
    if(D.h1.w == 1 && F.dl.x > 0) {
        vec3 sum = vec3(0.);
        vec3 Nd = normalize(vNormal);
        for(int i = 0; i < 16; i++) {
            if(i >= F.dl.x) break;
            vec3 L = F.dlPos[i].xyz - vWorld;
            float d = length(L), r = F.dlPos[i].w;
            if(d < r) { float att = 1. - d / r; att *= att; sum += F.dlCol[i].rgb * att * clamp(dot(Nd, L / max(d, .001)) * .7 + .3, 0., 1.); }
        }
        acc.rgb = clamp(acc.rgb * (1. + sum * .84), 0., 1.);
    }
    float fog = feature(FEATURE_FOG) ? (1. - exp(-max(0., vDist - 14.) * .008)) * .3 * F.fogColor.w : 0.;
    int mode = D.h0.z;
    if(mode == 0) acc.rgb = mix(acc.rgb, F.fogColor.rgb, fog);
    else if(mode == 1) acc.rgb *= 1. - fog;
    else if(mode == 2) acc.a *= 1. - fog;
    else if(mode == 4) acc.rgb = mix(acc.rgb, vec3(1.), fog);
    if(feature(FEATURE_POSTERIZE)) acc.rgb = floor(acc.rgb * 55.) / 55.;
    frag = acc;
}
