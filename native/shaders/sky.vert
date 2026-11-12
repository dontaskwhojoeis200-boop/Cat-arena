#version 450
#extension GL_GOOGLE_include_directive : require
// Map sky geometry (legacy sunMesh). The legacy bakes it once into a 512px cube map seen from
// WORLD.meta.center at z=2 (q3InitSky/bakeSkyBox) and draws that box at infinite depth. Drawing the
// same geometry from the same fixed eye with the current view rotation is the equivalent image
// without the cube-map resampling. Depth here only orders the sky geometry against itself; the
// renderer clears depth before the world. Far plane 4000 so no sky triangle is clipped (the bake's
// per-face depth never exceeded the 400 far plane).
#include "frame.glsl"
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 0) out vec3 vColor;
void main() {
    vec3 r = position - F.skyEye.xyz;
    float depth = dot(r, F.camForward.xyz);
    const float n = 0.05, f = 4000.0;
    gl_Position = vec4(dot(r, F.camRight.xyz) / F.camForward.w, -dot(r, F.camUp.xyz) / F.camRight.w,
                       depth * (f / (f - n)) - f * n / (f - n), depth);
    vColor = color;
}
