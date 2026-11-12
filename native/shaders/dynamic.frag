#version 450
#extension GL_GOOGLE_include_directive : require
// Legacy main fragment shader, generic atlas branch with isGun = 2 (index.html 1125-1180): clamped
// atlas lookup, alpha cut-out, flat 0.80 light plus dynamic lights (no normal term), fog, posterize.
#include "frame.glsl"
layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vColor;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vDist;
layout(location = 4) flat in int vMat;
layout(location = 5) in float vLight;
layout(location = 0) out vec4 frag;
void main() {
    int m = vMat;
    vec4 tex = texture(uWorldTex, vec3(clamp(vUV, vec2(.008), vec2(.992)), float(m)));
    if(tex.a < .36) discard;
    vec3 light = vec3(.80);
    if(F.dl.x > 0) light += dlights(vWorldPos, vec3(0.)) * .84;
    vec3 col = vColor * tex.rgb * vLight * light * 1.19;
    float emissive = (m == 4 || m == 5) ? 1. : 0.;
    float t = F.camPos.w;
    float hum = m == 4 ? .93 + .07 * sin(t * 21.0 + vWorldPos.x * 3.1 + vWorldPos.y * 1.7) * sin(t * 7.3 + vWorldPos.z) : 1.0;
    col = mix(col, vColor * tex.rgb * 1.16 * hum, emissive * .97);
    if(feature(FEATURE_FOG)) {
        float fog = 1.0 - exp(-max(0.0, vDist - 14.0) * .008);
        fog *= .30 * (1.0 - emissive * .82) * F.fogColor.w;
        col = mix(col, vec3(.19, .23, .27), fog);
    }
    if(feature(FEATURE_POSTERIZE)) col = floor(clamp(col, 0., 1.) * 55.0) / 55.0;
    frag = vec4(clamp(col, 0., 1.), 1.);
}
