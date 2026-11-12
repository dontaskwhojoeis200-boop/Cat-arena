#version 450
#extension GL_GOOGLE_include_directive : require
// Static world stream (N1 layout 14): legacy main program `vs` with instanced=0, isGun=0.
#include "frame.glsl"
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 color;
layout(location = 3) in vec2 uv;
layout(location = 4) in float material;
layout(location = 5) in vec2 lmuv;
layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vModelNormal;
layout(location = 2) out vec3 vColor;
layout(location = 3) out vec2 vUV;
layout(location = 4) out float vDist;
layout(location = 5) flat out int vMat;
layout(location = 6) out vec2 vLmUV;
void main() {
    vec3 r = position - F.camPos.xyz;
    gl_Position = legacyProject(r);
    vWorldPos = position;
    vModelNormal = normal;
    vColor = color;
    vUV = uv;
    vDist = length(r);
    vMat = int(material + .5);
    vLmUV = lmuv;
}
