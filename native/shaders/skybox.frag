#version 450
// N4 image sky box fragment stage: direct port of the legacy box shader (3916-3929).
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
layout(location = 0) in vec3 vDir;
layout(location = 0) out vec4 frag;
void main() {
    vec3 d = vDir; float a = F.skyBox.x; d = vec3(cos(a) * d.x - sin(a) * d.y, sin(a) * d.x + cos(a) * d.y, d.z);
    vec3 ad = abs(d); int face; vec2 uv;
    // Q3 skybox faces: rt +x, bk +y, lf -x, ft -y, up +z, dn -z (layer order of SKY_SUFFIX)
    if(ad.x >= ad.y && ad.x >= ad.z) { if(d.x > 0.) { face = 0; uv = vec2(d.y, d.z) / ad.x; } else { face = 2; uv = vec2(-d.y, d.z) / ad.x; } }
    else if(ad.y >= ad.z) { if(d.y > 0.) { face = 1; uv = vec2(-d.x, d.z) / ad.y; } else { face = 3; uv = vec2(d.x, d.z) / ad.y; } }
    else { if(d.z > 0.) { face = 4; uv = vec2(d.y, -d.x) / ad.z; } else { face = 5; uv = vec2(d.y, d.x) / ad.z; } }
    float n = float(textureSize(uBoxTex, 0).x);
    vec2 st = clamp(vec2(uv.x * .5 + .5, .5 - uv.y * .5), vec2(.5 / n), vec2(1. - .5 / n));
    vec3 c = textureLod(uBoxTex, vec3(st, float(face)), 0.).rgb;
    float h = 1. - smoothstep(-.05, .35, normalize(vDir).z);
    c = mix(c, F.fogColor.rgb, h * F.skyBox.y * F.fogColor.w);
    frag = vec4(c, 1.);
}
