#version 450
// N5 HUD: coloured triangles in normalized device coordinates (x right, y down), no texture.
layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 vColor;
void main() { gl_Position = vec4(position, 0., 1.); vColor = color; }
