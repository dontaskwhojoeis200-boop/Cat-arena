#version 450
#extension GL_GOOGLE_include_directive : require
// Effect quads: legacy fx-renderer.js fragment shader. Output is premultiplied (blend ONE, ONE_MINUS_SRC_ALPHA);
// additive quads write alpha 0.
#include "overlay.glsl"
layout(set = 0, binding = 0) uniform sampler2D fxAtlas;
layout(location = 0) in vec2 vUV;
layout(location = 1) flat in vec2 vTile;
layout(location = 2) flat in float vFlags;
layout(location = 3) in vec4 vCol;
layout(location = 4) in float vAdd;
layout(location = 5) in float vDist;
layout(location = 0) out vec4 frag;
void main() {
    float f = vFlags + .5;
    bool repU = mod(f, 2.0) >= 1.0, repV = mod(floor(f / 2.0), 2.0) >= 1.0, hot = mod(floor(f / 4.0), 2.0) >= 1.0;
    vec2 uv = vec2(repU ? fract(vUV.x) : clamp(vUV.x, 0., 1.), repV ? fract(vUV.y) : clamp(vUV.y, 0., 1.));
    const float inset = .75 / 128.;
    vec2 t = (vTile + vec2(inset) + uv * (1.0 - 2.0 * inset)) / 8.0;
    vec4 s = textureGrad(fxAtlas, t, dFdx(vUV) * (.125 - inset * .25), dFdy(vUV) * (.125 - inset * .25));
    vec3 c = s.rgb * vCol.rgb;
    if(hot) c += (vec3(1.) - vCol.rgb) * s.rgb * s.a * s.a * .85;
    float a = s.a * vCol.a;
    float fog = P.camUp.w * clamp((1.0 - exp(-max(0.0, vDist - 16.0) * .012)) * .62 * P.extra.w, 0., .8);
    vec3 pm = c * a;
    pm = mix(pm, P.extra.rgb * a * (1.0 - vAdd), fog);
    frag = vec4(pm, a * (1.0 - vAdd));
}
