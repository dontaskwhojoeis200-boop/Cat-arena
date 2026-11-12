// Per-pass material state (set 1, dynamic UBO). Mirrors the uniforms Q3MAT.uploadStages writes
// (legacy 4004-4047). Layout must match cat::gpu::DrawData in renderer.cpp (std140).
struct StageData {
    ivec4 a;      // src, tcGen, rgbGen code, alphaGen code
    ivec4 b;      // blend src, blend dst, alphaFunc, clampMap
    vec4 cst;     // stageColorInto result
    vec4 preS;    // xyz = (a, b, e) of the pre-turb tcMod matrix
    vec4 preT;    // xyz = (c, d, f)
    vec4 postS;
    vec4 postT;
    vec4 turb;    // amp, phase, freq, enabled
    vec4 tcS;     // tcGen vector s
    vec4 tcT;     // tcGen vector t
    vec4 misc;    // x texture layer (animMap frame resolved on the CPU), y portal range
};
layout(set = 1, binding = 0, std140) uniform Draw {
    ivec4 h0;     // x stage count, y collapse, z fog mode, w sprite (0, 1 autosprite, 2 autosprite2)
    ivec4 h1;     // x deform count, y kit-lit (vertex-lit model), z sky dome variant, w dynamic lights on this pass
    ivec4 dType;  // deform types (1 wave, 2 bulge, 3 move, 4 normal)
    vec4 dA[3];
    vec4 dB[3];
    vec4 skyp;    // x sky rotation, y cloud height, z material time (frame.time % 1000), w unused
    StageData st[8];
} D;
const float Q3U = 32.0;
