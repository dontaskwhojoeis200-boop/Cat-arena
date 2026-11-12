#version 450
layout(location=0) out vec3 color;
vec2 positions[3]=vec2[](vec2(0,-0.65),vec2(0.65,0.55),vec2(-0.65,0.55));
vec3 colors[3]=vec3[](vec3(0.95,0.95,0.95),vec3(0.5,0.15,0.1),vec3(0.35,0.35,0.35));
void main() { gl_Position=vec4(positions[gl_VertexIndex],0,1); color=colors[gl_VertexIndex]; }
