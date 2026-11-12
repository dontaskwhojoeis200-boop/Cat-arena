#version 450
#extension GL_GOOGLE_include_directive : require
// First-person weapon model: legacy fragment path `isGun==1 && vMat==300` (all 16 weapon models use
// material 300): base colour, metal/roughness and normal map, one fixed light, muzzle-flash glow.
#include "overlay.glsl"
layout(set = 0, binding = 0) uniform sampler2D meshyBase;
layout(set = 0, binding = 1) uniform sampler2D meshyMR;
layout(set = 0, binding = 2) uniform sampler2D meshyNormal;
layout(location = 0) in vec3 vLocalPos;
layout(location = 1) in vec3 vModelNormal;
layout(location = 2) in vec2 vUV;
layout(location = 0) out vec4 frag;
void main() {
    vec3 base = texture(meshyBase, vUV).rgb;
    vec3 mr = texture(meshyMR, vUV).rgb;
    vec3 N = normalize(vModelNormal);
    // Window y points down in Vulkan (overlayProject negates y), so the y derivatives are negated to
    // give the same tangent frame as the legacy GL shader.
    vec3 dp1 = dFdx(vLocalPos), dp2 = -dFdy(vLocalPos);
    vec2 du1 = dFdx(vUV), du2 = -dFdy(vUV);
    vec3 p2 = cross(dp2, N), p1 = cross(N, dp1);
    vec3 T = p2 * du1.x + p1 * du2.x, B = p2 * du1.y + p1 * du2.y;
    float inv = inversesqrt(max(max(dot(T, T), dot(B, B)), .000001));
    vec3 mapN = texture(meshyNormal, vUV).xyz * 2. - 1.;
    N = normalize(mat3(T * inv, B * inv, N) * mapN);
    vec3 L = normalize(vec3(-.35, -.6, .85));
    float lit = max(dot(N, L), 0.);
    float spec = pow(max(dot(N, normalize(L + vec3(0., -1., .2))), 0.), mix(70., 8., mr.g));
    vec3 c = base * (.73 + .32 * lit) + mix(vec3(.04), base, mr.b) * spec * (1. - mr.g) * .20;
    frag = vec4(clamp(c * (vec3(1.) + P.extra.rgb), 0., 1.), 1.);   // gunLit
}
