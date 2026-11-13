#version 450
layout(set = 0, binding = 0) uniform sampler2D atlas;
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 2) in float vMode;
layout(location = 0) out vec4 frag;
void main() {
    if(vMode < .5) { frag = vColor; return; }
    if(vMode < 1.999) {   // signed distance glyph: 0.5 is the outline; anti-aliased over one screen pixel
        // Four samples a quarter pixel apart, and the outline pushed out by 0.12 pixel: small text keeps its
        // hairlines (crossbars, arrow shafts) the way a browser's rasterizer does at 10-14 px.
        vec2 dx = dFdx(vUv) * .25, dy = dFdy(vUv) * .25;
        float d = texture(atlas, vUv).a;
        float fw = max(fwidth(d), 1e-4);
        float w = fw * .75;
        float edge = .5 - (vMode - 1.) * .18 - fw * .12;
        float a = smoothstep(edge - w, edge + w, texture(atlas, vUv + dx + dy).a) + smoothstep(edge - w, edge + w, texture(atlas, vUv - dx - dy).a)
                + smoothstep(edge - w, edge + w, texture(atlas, vUv + dx - dy).a) + smoothstep(edge - w, edge + w, texture(atlas, vUv - dx + dy).a);
        frag = vec4(vColor.rgb, vColor.a * a * .25);
        return;
    }
    frag = texture(atlas, vUv) * vColor;
}
