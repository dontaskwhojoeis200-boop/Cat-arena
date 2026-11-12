#version 450
#extension GL_GOOGLE_include_directive : require
// N5 practice targets: flat-coloured world-space triangles (hud.frag writes the colour).
#include "overlay.glsl"
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 vColor;
void main() { gl_Position = overlayProject(position - P.camPos.xyz); vColor = color; }
