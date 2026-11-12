#version 450
#extension GL_GOOGLE_include_directive : require
// First-person weapon model: legacy main program with isGun = 1, instanced = 0 (12-float vertices).
#include "overlay.glsl"
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;   // colour and material id of the 12-float vertex are not used by material 300
layout(location = 0) out vec3 vLocalPos;
layout(location = 1) out vec3 vModelNormal;
layout(location = 2) out vec2 vUV;
void main() {
    gl_Position = overlayProject(position - P.camPos.xyz);
    vLocalPos = position; vModelNormal = normal; vUV = uv;
}
