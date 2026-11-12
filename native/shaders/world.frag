#version 450
#extension GL_GOOGLE_include_directive : require
// Legacy main fragment shader, static-world branch (legacy/index.html 1125-1180), with
// useStaticLightmap=1 and worldLightMode=1 (per-pixel lightmap, the default lightQuality=1).
// N5.1: dynamic lights are added to the surface light before the debug views, as in the legacy (1164).
#include "frame.glsl"
layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vModelNormal;
layout(location = 2) in vec3 vColor;
layout(location = 3) in vec2 vUV;
layout(location = 4) in float vDist;
layout(location = 5) flat in int vMat;
layout(location = 6) in vec2 vLmUV;
layout(location = 0) out vec4 frag;

vec3 colorShift(vec3 c) { float m = max(c.r, max(c.g, c.b)); return m > 1.0 ? c / m : c; }

void main() {
    int m = vMat;
    vec4 tex = texture(uWorldTex, vec3(vUV, float(m)));
    if(tex.a < .36) discard;
    vec3 light = vec3(.80);
    float exterior = 0.0;
    vec3 Nw = normalize(vModelNormal);
    if(feature(FEATURE_LIGHTMAP)) {
        // rgb = light/1.9, a = sky exposure
        vec4 lm = texture(uLightmap, vLmUV);
        exterior = lm.a;
        light = colorShift(lm.rgb * 1.90 * F.params.x * F.params.y * 1.19) / 1.19;
    }
    if(F.dl.x > 0) light += dlights(vWorldPos, Nw) * .84;
    if(F.flags.y == 1) { frag = vec4(clamp(light * .62, 0., 1.), 1.); return; }   // r_lightmap
    if(F.flags.y == 2) light = vec3(1.);                                            // r_fullbright
    if(F.flags.y == 3) { frag = vec4(Nw * .5 + .5, 1.); return; }                   // normals
    vec3 col = vColor * tex.rgb * 1.0 * light * 1.19;   // vLight = 1 for the world
    float emissive = (m == 4 || m == 5) ? 1. : 0.;
    float t = F.camPos.w;
    float hum = m == 4 ? .93 + .07 * sin(t * 21.0 + vWorldPos.x * 3.1 + vWorldPos.y * 1.7) * sin(t * 7.3 + vWorldPos.z) : 1.0;
    col = mix(col, vColor * tex.rgb * 1.16 * hum, emissive * .97);
    if(feature(FEATURE_FOG)) {
        vec3 atmospheric = mix(vec3(.19, .23, .27), vec3(.69, .38, .21), exterior);
        float fog = 1.0 - exp(-max(0.0, vDist - 14.0) * (.008 + exterior * .018));
        fog *= mix(.30, .85, exterior) * (1.0 - emissive * .82) * F.fogColor.w;
        col = mix(col, atmospheric, fog);
    }
    if(feature(FEATURE_POSTERIZE)) col = floor(clamp(col, 0., 1.) * 55.0) / 55.0;
    frag = vec4(clamp(col, 0., 1.), 1.);
}
