// Camera of the N5 overlay passes (effects, weapon model), passed as push constants. The projection is
// the legacy one (see frame.glsl legacyProject) with the camera of the pass instead of the world camera.
layout(push_constant, std430) uniform Push {
    vec4 camPos;      // xyz
    vec4 camForward;  // xyz, w = tanHalf * aspect
    vec4 camRight;    // xyz, w = tanHalf (vertical)
    vec4 camUp;       // xyz, w = fogOn (effects)
    vec4 extra;       // effects: fog rgb + fogStrength; weapon: gunGlow rgb
} P;
vec4 overlayProject(vec3 r) {
    float depth = dot(r, P.camForward.xyz);
    return vec4(dot(r, P.camRight.xyz) / P.camForward.w, -dot(r, P.camUp.xyz) / P.camRight.w, depth * 1.000125015 - 0.05000625, depth);
}
