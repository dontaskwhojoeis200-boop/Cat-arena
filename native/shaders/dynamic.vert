#version 450
#extension GL_GOOGLE_include_directive : require
// N5.1 dynamic world-atlas geometry (legacy renderGeometry(): the medkit billboards), drawn by the
// legacy main program with isGun = 2, instanced = 0. 12-float vertices.
#include "frame.glsl"
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 color;
layout(location = 3) in vec2 uv;
layout(location = 4) in float material;
layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vColor;
layout(location = 2) out vec2 vUV;
layout(location = 3) out float vDist;
layout(location = 4) flat out int vMat;
layout(location = 5) out float vLight;
void main() {
    vec3 r = position - F.camPos.xyz;
    gl_Position = legacyProject(r);
    vWorldPos = position; vColor = color; vUV = uv; vDist = length(r); vMat = int(material + .5);
    float lambert = max(dot(normal, vec3(-.300, -.600, .625)), 0.);
    vLight = .69 + .33 * lambert;   // isGun == 2
}
