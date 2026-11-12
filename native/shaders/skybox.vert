#version 450
// N4 image sky box (Q3MAT BOX_VARIANT, legacy 3867 / 3916-3929). The legacy draws a unit cube at
// camPos + p*200 with vDir = p. A full-screen triangle at the far plane with the view ray per pixel
// gives the same direction field: only the direction (not its length) is used by the fragment stage.
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
layout(location = 0) out vec3 vDir;
void main() {
    vec2 ndc = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    // legacyProject: clip.x = dot(r, right) / tanHalfX, clip.y = -dot(r, up) / tanHalfY, w = dot(r, forward)
    vDir = F.camForward.xyz + F.camRight.xyz * (ndc.x * F.camForward.w) - F.camUp.xyz * (ndc.y * F.camRight.w);
    gl_Position = vec4(ndc, 1.0, 1.0);   // depth 1: LEQUAL passes only where nothing was drawn
}
