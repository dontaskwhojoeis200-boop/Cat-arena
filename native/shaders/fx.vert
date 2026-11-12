#version 450
#extension GL_GOOGLE_include_directive : require
// Effect quads: legacy fx-renderer.js vertex shader (FXR).
#include "overlay.glsl"
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aUV;    // u v tile flags
layout(location = 2) in vec4 aCol;
layout(location = 3) in float aAdd;
layout(location = 0) out vec2 vUV;
layout(location = 1) flat out vec2 vTile;
layout(location = 2) flat out float vFlags;
layout(location = 3) out vec4 vCol;
layout(location = 4) out float vAdd;
layout(location = 5) out float vDist;
void main() {
    vec3 r = aPos - P.camPos.xyz;
    gl_Position = overlayProject(r);
    vUV = aUV.xy; vTile = vec2(mod(aUV.z, 8.0), floor(aUV.z / 8.0 + .001)); vFlags = aUV.w; vCol = aCol; vAdd = aAdd; vDist = length(r);
}
