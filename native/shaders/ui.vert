#version 450
// N7 UI: triangles in normalized device coordinates (x right, y down) with atlas UV, colour and a mode
// (0 solid, [1, 2) distance-field glyph with boldness mode - 1, 2 atlas image).
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;
layout(location = 3) in float mode;
layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;
layout(location = 2) out float vMode;
void main() { gl_Position = vec4(position, 0., 1.); vUv = uv; vColor = color; vMode = mode; }
