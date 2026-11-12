#version 450
// Legacy isGun==6 branch (sky bake): frag = vec4(vColor, 1). The box shader then samples it with
// skyFog = WORLD.meta.sky.horizonFog ?? 0 (0 for Helios), i.e. unchanged and not posterized.
layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 frag;
void main() { frag = vec4(vColor, 1.0); }
