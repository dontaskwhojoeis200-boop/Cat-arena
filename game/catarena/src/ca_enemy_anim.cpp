// Cat Arena: Feral Claw — enemy animation for cgame (N10-Q3E stage D part 4). See ca_anim.h.
//
// Ported from N10:
//   CA_AnimPose        native/src/enemy_fx.cpp eval_pose (legacy enemyEvalPose 10630)
//   CA_AnimDeath       death_state (enemyDeathState 10742); CA_AnimDeathStart: enemyStartDeath 10725
//   CA_AttackProgress  native/src/enemies.cpp attack_progress (enemyAttackProgress)
//   CA_EnemySkin       native/shaders/enemy.vert (the legacy creature vertex shader): every triangle
//                      belongs to one limb chain (tag in the fraction of the material id); knees,
//                      elbows, wing tips, neck and spine bend with smooth weights from the rest
//                      position; then the death topple, the facing and the position.
// Presentation only: nothing here changes gameplay. N10 evaluates the deformation per vertex on the
// GPU; Quake III's cgame cannot pass bone poses to the renderer, so it runs here per vertex on the
// CPU and the result is drawn as polygons (cg_ca_enemies.c).
// GPL v2 or later (docs/q3e/LICENSING.md).
#include "ca_anim.h"
#include "ca_bridge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gen {
#include "cat/generated/enemy_tables.inc"
}

namespace {

constexpr double kPi = 3.14159265358979323846;
using gen::AttackAnim;
using gen::RigClass;

double ss01(double a, double b, double t) { t = std::max(0.0, std::min(1.0, (t - a) / (b - a))); return t * t * (3 - 2 * t); }
double facing_from(double vx, double vy) { return std::atan2(-vx, vy); }   // enemyFacingFromVec
const gen::Rig& rig_of(int type) { return gen::kEnemyRig[static_cast<std::size_t>(std::clamp(type, 0, 19))]; }

double next(caAnimState_t& s) {   // the N10 Rng (mulberry32), one stream per creature
    s.rng += 0x6D2B79F5U;
    std::uint32_t t = (s.rng ^ (s.rng >> 15)) * (1U | s.rng);
    t = (t + (t ^ (t >> 7)) * (61U | t)) ^ t;
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}

struct AttackPhases { bool on{}; double wind{}, strike{}, hold{}; };
AttackPhases attack_phases(double p) {   // attackPhases (10619)
    if(p < 0) return {};
    AttackPhases r;
    r.on = true;
    r.wind = ss01(0, .6, p) * (1 - ss01(.68, .78, p));
    r.strike = ss01(.66, .76, p) * (1 - ss01(.86, 1, p));
    r.hold = ss01(0, .25, p) * (1 - ss01(.88, 1, p));
    return r;
}

struct Mesh {
    std::array<float, 12> rig{};    // hip, knee, shoulder, elbow, neck, spine, wing root |x|, height, shoulder |x|, wing tip |x|, depth, width
    std::vector<float> v;           // 12 floats per vertex: position, normal, colour, uv, material
    int bone(std::size_t i) const {
        const float m = v[i * 12 + 11];
        if(!(m >= 400.0F && m < 440.0F)) return 0;
        return static_cast<int>(std::floor((m - std::floor(m)) * 25.0F + .48F));
    }
};
std::array<Mesh, CA_ENEMY_TYPES> g_mesh;

struct V3 { float x, y, z; };
V3 rX(V3 p, float a) { const float c = std::cos(a), s = std::sin(a); return {p.x, c * p.y - s * p.z, s * p.y + c * p.z}; }
V3 rY(V3 p, float a) { const float c = std::cos(a), s = std::sin(a); return {c * p.x + s * p.z, p.y, -s * p.x + c * p.z}; }
V3 rZ(V3 p, float a) { const float c = std::cos(a), s = std::sin(a); return {c * p.x - s * p.y, s * p.x + c * p.y, p.z}; }
V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
float smoothstep(float e0, float e1, float x) { const float t = std::clamp((x - e0) / (e1 - e0), 0.0F, 1.0F); return t * t * (3 - 2 * t); }
float mixf(float a, float b, float t) { return a + (b - a) * t; }

}  // namespace

extern "C" double CA_AttackProgress(int type, int ms) {   // enemyAttackProgress
    const auto* def = CA_EnemyType(type);
    if(!def || ms < 0) return -1;
    if(ms < def->windMs) return std::clamp(double(ms) / def->windMs, 0.0, 1.0) * .72;
    const int r = ms - def->windMs;
    if(r < def->recoverMs) return .72 + .28 * double(r) / def->recoverMs;
    return -1;
}

extern "C" void CA_AnimPose(caAnimState_t* st, int type, const caPoseInput_t* in, double time, float out[16]) {   // enemyEvalPose (10630)
    auto& A = *st;
    const auto& b = *in;
    const auto& rig = rig_of(type);
    if(!A.ready) {
        const unsigned seed = A.rng;
        A = caAnimState_t{};
        A.rng = seed;
        A.ready = 1; A.phase = next(A) * 6.28; A.px = b.x; A.py = b.y; A.pz = b.z; A.pa = b.a; A.seed = next(A) * 100; A.lastT = time; A.painDir = 1;
    }
    const double fdt = std::max(0.0, std::min(.1, time - A.lastT));
    A.lastT = time;
    const double moved = std::hypot(b.x - A.px, b.y - A.py);
    A.px = b.x; A.py = b.y; A.pz = b.z;
    const double inst = fdt > 0 ? moved / fdt : 0;
    A.speed += (std::min(inst, 24.0) - A.speed) * std::min(1.0, fdt * 10);
    const RigClass cls = rig.cls;
    const double H = rig.height;
    const double walk_t = std::max(0.0, std::min(1.0, A.speed / (cls == RigClass::heavy ? 3.2 : cls == RigClass::caster ? 3.4 : 4.5)));
    A.walk += (walk_t - A.walk) * std::min(1.0, fdt * 6);
    const double yaw_delta = std::atan2(std::sin(b.a - A.pa), std::cos(b.a - A.pa));
    A.pa = b.a;
    A.turn += ((fdt > 0 ? yaw_delta / fdt : 0) - A.turn) * std::min(1.0, fdt * 5);
    if(cls == RigClass::flyer) A.phase += fdt * (7 + A.walk * 5);
    else A.phase += moved * 2 * kPi / gen::kGaitStride[static_cast<std::size_t>(cls)];
    const double hit = std::max(0.0, std::min(1.0, b.hitFlash / .21));
    if(hit > A.pain + .3) A.painDir = next(A) < .5 ? -1 : 1;
    A.pain = std::max(A.pain - fdt * 4.5, hit);
    const double w = A.walk, ph = A.phase, t = time + A.seed, pain = A.pain;
    const double S = std::sin(ph), C = std::cos(ph);
    double hipL = 0, hipR = 0, kneeL = 0, kneeR = 0, shPL = 0, shPR = 0, elL = .25, elR = .25, shRL = .08, shRR = .08, lean = 0, twist = 0, headP = 0, headY = 0, bob = 0, roll = 0;
    // ---- locomotion ----
    const double breathe = std::sin(t * 1.7);
    if(cls == RigClass::soldier) {
        hipL = w * .55 * S; hipR = -hipL; kneeL = -w * (.12 + .7 * std::max(0.0, C)); kneeR = -w * (.12 + .7 * std::max(0.0, -C));
        shPL = -w * .42 * S + .12; shPR = w * .42 * S + .12; elL = elR = .32 + w * .22;
        lean = .07 * w; twist = .13 * w * S; headY = -twist * .7; bob = w * H * .024 * std::cos(2 * ph) + (1 - w) * H * .006 * breathe; roll = .03 * w * S;
    } else if(cls == RigClass::heavy) {
        hipL = w * .42 * S; hipR = -hipL; kneeL = -w * (.1 + .5 * std::max(0.0, C)); kneeR = -w * (.1 + .5 * std::max(0.0, -C));
        shRL = shRR = .26 + .04 * breathe; shPL = -w * .36 * S + .1; shPR = w * .36 * S + .1; elL = elR = .45;
        lean = .12 + .06 * w; twist = .08 * w * S; roll = .075 * w * S; headY = -twist * .5;
        bob = -w * H * .04 * std::pow(std::fabs(S), 4) + w * H * .012 + (1 - w) * H * .008 * breathe; headP = w * .06 * std::pow(std::fabs(S), 4);
    } else if(cls == RigClass::beast) {
        hipL = w * .72 * S; hipR = w * .72 * std::sin(ph + .55); kneeL = -w * (.3 + .75 * std::max(0.0, C)); kneeR = -w * (.3 + .75 * std::max(0.0, std::cos(ph + .55)));
        shPL = .35 + w * (.75 + .65 * std::sin(ph + kPi)); shPR = .35 + w * (.75 + .65 * std::sin(ph + kPi + .5)); elL = elR = .45 + .35 * w; shRL = shRR = .14;
        lean = .1 + .24 * w + .06 * w * std::sin(2 * ph); headP = -lean * .75; bob = w * H * .05 * std::max(0.0, std::sin(ph + .7)) + (1 - w) * H * .01 * breathe; roll = .02 * w * S;
    } else if(cls == RigClass::skitter) {
        const double q = ph;
        hipL = w * .48 * std::sin(q); hipR = -hipL; kneeL = -w * .4 * std::max(0.0, std::cos(q)); kneeR = -w * .4 * std::max(0.0, -std::cos(q));
        shPL = .25 + w * .55 * std::sin(q + kPi / 2); shPR = .25 - w * .55 * std::sin(q + kPi / 2); shRL = shRR = .38; elL = elR = .5;
        lean = .1; twist = .12 * w * std::sin(q); bob = w * H * .022 * std::fabs(std::sin(2 * q)) + (1 - w) * H * .006 * breathe; roll = .04 * w * std::sin(q);
    } else if(cls == RigClass::flyer) {
        const double f = std::sin(ph), f2 = std::sin(ph - .95);
        hipL = hipR = .28 + .58 * f; kneeL = kneeR = .42 * f2;
        lean = .22 * w - .04; roll = std::max(-.5, std::min(.5, -A.turn * .18)); bob = -H * .05 * f; headP = -lean * .6;
    } else {   // caster: hovering glide, robes barely stride
        hipL = w * .2 * S; hipR = -hipL; kneeL = -w * .18 * std::max(0.0, C); kneeR = -w * .18 * std::max(0.0, -C);
        shRL = shRR = .32 + .06 * std::sin(t * 2.1); shPL = .38 + .1 * std::sin(t * 1.3); shPR = .38 + .1 * std::sin(t * 1.3 + 1); elL = elR = .75;
        lean = .04 + .05 * w; bob = H * .022 * std::sin(t * 2.2) + H * .012; roll = .03 * std::sin(t * 1.1);
    }
    // ---- look at the target (head first, then the spine) ----
    if(b.hasTarget) {
        const double dx = b.tx - b.x, dy = b.ty - b.y, d = std::hypot(dx, dy);
        if(d < 30 && d > .4) {
            const double f = facing_from(dx, dy), want = std::atan2(std::sin(f - b.a), std::cos(f - b.a));
            A.look += (std::max(-.9, std::min(.9, want)) - A.look) * std::min(1.0, fdt * 5);
            const double pitch = std::atan2(b.tz + 1.2 - (b.z + rig.neck), d);
            A.lookP += (std::max(-.6, std::min(.6, -pitch)) - A.lookP) * std::min(1.0, fdt * 4);
        }
    } else {
        A.look += (.35 * std::sin(t * .37) * (1 - w) - A.look) * std::min(1.0, fdt * 2);
        A.lookP *= 1 - std::min(1.0, fdt * 2);
    }
    headY += A.look * .65; twist += A.look * .3; headP += A.lookP * .7;
    // ---- attacks ----
    const AttackPhases P = attack_phases(b.attackProgress);
    if(P.on) {
        const double wind = P.wind, strike = P.strike, hold = P.hold;
        switch(rig.attack) {
        case AttackAnim::shoot:   // shoulder the weapon, aim, recoil
            shPR = shPR * (1 - hold) + (1.42 + A.lookP * .8) * hold + .3 * strike; elR = elR * (1 - hold) + .08 * hold;
            shPL = shPL * (1 - hold) + 1.1 * hold; shRL = shRL * (1 - hold) - .22 * hold; elL = elL * (1 - hold) + .45 * hold;
            lean += -.12 * strike; headP += .12 * hold; twist += -.15 * hold; break;
        case AttackAnim::throw_:   // wind back, hurl overhand
            shPR = shPR * (1 - wind - strike) - .75 * wind + 1.45 * strike; elR = elR + .45 * wind - .2 * strike;
            twist += -.45 * wind + .5 * strike; lean += -.08 * wind + .24 * strike; shPL = shPL + .6 * wind; break;
        case AttackAnim::cast: case AttackAnim::scream:   // both hands up, then thrust the spell forward
            shPL = shPL * (1 - wind - strike) + 2.3 * wind + 1.35 * strike; shPR = shPR * (1 - wind - strike) + 2.3 * wind + 1.35 * strike;
            shRL = shRL + .35 * wind - .1 * strike; shRR = shRR + .35 * wind - .1 * strike; elL = elL * (1 - wind - strike) + .15; elR = elR * (1 - wind - strike) + .15;
            lean += -.16 * wind + .22 * strike;
            if(rig.attack == AttackAnim::scream) headP += -.55 * wind + .4 * strike;
            break;
        case AttackAnim::slam:   // raise both fists overhead, pound the ground
            shPL = shPL * (1 - wind - strike) + 2.3 * wind + .55 * strike; shPR = shPR * (1 - wind - strike) + 2.3 * wind + .55 * strike; elL = elR = .25 + .3 * strike;
            lean += -.24 * wind + .5 * strike; kneeL += -.45 * strike; kneeR += -.45 * strike; hipL += .25 * strike; hipR += .25 * strike; bob += -H * .07 * strike + H * .02 * wind; headP += .3 * strike; break;
        case AttackAnim::claw:   // lunge and rake with the right, follow with the left
            shPR = shPR * (1 - wind - strike) - .5 * wind + 1.35 * strike; shRR = shRR + .4 * wind - .2 * strike; elR = elR + .35 * wind - .15 * strike;
            shPL = shPL * (1 - strike) + 1.1 * strike + .2 * wind; twist += -.38 * wind + .5 * strike; lean += .38 * strike - .05 * wind; hipL += .35 * strike; kneeR += -.3 * strike; break;
        case AttackAnim::bite:   // rear up, snap down
            lean += -.45 * wind + .55 * strike; headP += -.35 * wind + .55 * strike; shPL += .6 * wind; shPR += .6 * wind; bob += H * .03 * wind; break;
        case AttackAnim::dive:   // flare the wings, pitch up, dive
            hipL = hipL * (1 - wind) - .35 * wind + .9 * strike; hipR = hipR * (1 - wind) - .35 * wind + .9 * strike; kneeL = kneeR = kneeL * (1 - wind) - .6 * wind;
            lean += -.32 * wind + .45 * strike; bob += H * .06 * wind - H * .05 * strike; break;
        }
    }
    // ---- pain ----
    if(pain > 0) { lean -= .32 * pain; headP -= .34 * pain; twist += .22 * pain * A.painDir; shRL += .4 * pain; shRR += .4 * pain; elL += .35 * pain; elR += .35 * pain; }
    const double v[16] = {hipL, hipR, kneeL, kneeR, shPL, shPR, elL, elR, shRL, shRR, lean, twist, headP, headY, bob, roll};
    for(std::size_t i = 0; i < 16; ++i) out[i] = static_cast<float>(v[i]);
}

extern "C" void CA_AnimDeathStart(caAnimState_t* st, int type, int* variant, float* side) {   // enemyStartDeath (10725)
    *variant = static_cast<int>(next(*st) * 3);
    if(rig_of(type).cls == RigClass::flyer) *variant = 3;
    *side = next(*st) < .5 ? -1.0F : 1.0F;
}

extern "C" void CA_AnimDeath(int type, const float at[16], int variant, float side_sign, double t, float out[16], float state3[3]) {   // enemyDeathState (10742)
    const auto& rig = rig_of(type);
    const double k = std::min(1.0, t / gen::kDeathFall), e = k * k;
    const double bounce = k >= 1 ? 0 : std::sin(std::max(0.0, (k - .82) / .18) * kPi) * .08;
    for(std::size_t j = 0; j < 16; ++j) out[j] = static_cast<float>(static_cast<double>(at[j]) * (1 - ss01(0, .6, k)));
    double fall = 0, side = 0;
    if(variant == 0) fall = (kPi / 2) * e - bounce;
    else if(variant == 1) fall = -(kPi / 2) * e + bounce;
    else if(variant == 2) { side = side_sign * (kPi / 2) * e; fall = .25 * e; }
    else { fall = (kPi / 2.4) * e; side = side_sign * .4 * e; }
    const double slack = ss01(.2, 1, k);
    const auto addv = [&](std::size_t i, double v) { out[i] = static_cast<float>(static_cast<double>(out[i]) + v); };
    if(rig.cls != RigClass::flyer) { addv(2, -.9 * slack); addv(3, -.7 * slack); addv(0, .5 * slack); addv(1, .2 * slack); addv(8, .7 * slack); addv(9, .5 * slack); addv(6, .6 * slack); addv(7, .8 * slack); addv(12, .4 * slack); }
    else { addv(0, -.5 * slack); addv(1, -.5 * slack); addv(2, -.8 * slack); addv(3, -.8 * slack); }
    const double sink_t = std::max(0.0, t - gen::kDeathFall - gen::kDeathStay) / gen::kDeathSink;
    state3[0] = static_cast<float>(fall);
    state3[1] = static_cast<float>(side);
    state3[2] = static_cast<float>(sink_t * sink_t * (rig.depth * 1.6 + .4));
}

extern "C" int CA_EnemyMeshLoad(int type, const unsigned char* data, int bytes) {
    if(type < 0 || type >= CA_ENEMY_TYPES || !data || bytes < 80 || std::memcmp(data, "CATENM01", 8) != 0) return 0;
    std::uint32_t count = 0;
    std::memcpy(&count, data + 8, 4);
    if(count == 0 || count % 3 != 0 || 80 + std::size_t(count) * 48 > std::size_t(bytes)) return 0;
    auto& m = g_mesh[static_cast<std::size_t>(type)];
    std::memcpy(m.rig.data(), data + 16, 12 * 4);
    m.v.resize(std::size_t(count) * 12);
    std::memcpy(m.v.data(), data + 80, std::size_t(count) * 48);
    return int(count);
}
extern "C" int CA_EnemyMeshVertexCount(int type) {
    return type >= 0 && type < CA_ENEMY_TYPES ? int(g_mesh[static_cast<std::size_t>(type)].v.size() / 12) : 0;
}
extern "C" float CA_EnemyHeight(int type) { return float(rig_of(type).height); }

extern "C" void CA_EnemySkin(int type, const float P[16], const float st[4], const float feet[3], float facing, caSkinVert_t* out) {   // enemy.vert
    if(type < 0 || type >= CA_ENEMY_TYPES) return;
    const auto& m = g_mesh[static_cast<std::size_t>(type)];
    const float hipZ = m.rig[0], kneeZ = m.rig[1], shZ = m.rig[2], elZ = m.rig[3];
    const float neckZ = m.rig[4], spineZ = m.rig[5], wingX = m.rig[6], H = std::max(.5F, m.rig[7]);
    const float shX = m.rig[8], wingTip = m.rig[9], depthY = m.rig[10], halfW = m.rig[11];
    const float band = H * .085F;
    // N10 facing a (model +Y forward) = Quake III yaw - 90 degrees
    const float a = facing - float(kPi / 2), ca = std::cos(a), sa = std::sin(a);
    const float hit = std::clamp(st[0], 0.0F, 1.0F) * .92F;
    const std::size_t n = m.v.size() / 12;
    for(std::size_t i = 0; i < n; ++i) {
        const float* q = &m.v[i * 12];
        const int bone = m.bone(i);
        const bool left = bone == 1 || bone == 3 || bone == 5;
        const float sg = left ? 1.0F : -1.0F;
        const V3 v0{q[0], q[1], q[2]};
        V3 p = v0, nrm{q[3], q[4], q[5]};
        float spineW = 1;
        if(bone == 1 || bone == 2) {
            const float hip = left ? P[0] : P[1], knee = left ? P[2] : P[3];
            const float wk = 1 - smoothstep(kneeZ - band, kneeZ + band, p.z);
            const V3 kp{0, 0, kneeZ};
            p = add(rX(sub(p, kp), knee * wk), kp); nrm = rX(nrm, knee * wk);
            const V3 hp{0, 0, hipZ};
            p = add(rX(sub(p, hp), hip), hp); nrm = rX(nrm, hip);
            spineW = 0;
        } else if(bone == 3 || bone == 4) {
            const float sp = left ? P[4] : P[5], el = left ? P[6] : P[7], sr = left ? P[8] : P[9];
            const float wf = 1 - smoothstep(elZ - band, elZ + band, p.z);
            const V3 ep{-sg * shX, 0, elZ};
            p = add(rX(sub(p, ep), el * wf), ep); nrm = rX(nrm, el * wf);
            const V3 s3{-sg * shX, 0, shZ};
            p = add(rY(sub(p, s3), sr * sg), s3); nrm = rY(nrm, sr * sg);
            p = add(rX(sub(p, s3), sp), s3); nrm = rX(nrm, sp);
        } else if(bone == 5 || bone == 6) {
            const float flap = left ? P[0] : P[1], tip = left ? P[2] : P[3];
            const float wt = smoothstep(wingX, mixf(wingX, wingTip, .75F), std::fabs(p.x));
            const V3 tp{-sg * mixf(wingX, wingTip, .45F), 0, shZ};
            p = add(rY(sub(p, tp), tip * wt * sg), tp); nrm = rY(nrm, tip * wt * sg);
            const V3 rp{-sg * wingX, 0, shZ};
            p = add(rY(sub(p, rp), flap * sg), rp); nrm = rY(nrm, flap * sg);
        } else {
            const float wh = smoothstep(neckZ - H * .05F, neckZ + H * .05F, p.z);
            const V3 np{0, 0, neckZ};
            p = add(rZ(sub(p, np), P[13] * wh), np); nrm = rZ(nrm, P[13] * wh);
            p = add(rX(sub(p, np), -P[12] * wh), np); nrm = rX(nrm, -P[12] * wh);
            spineW = smoothstep(spineZ - band * 1.5F, spineZ + band * 1.5F, p.z);
        }
        // spine: forward lean (+) and twist, carried by the torso and both arms
        const V3 spv{0, 0, spineZ};
        p = add(rZ(sub(p, spv), P[11] * spineW), spv); nrm = rZ(nrm, P[11] * spineW);
        p = add(rX(sub(p, spv), -P[10] * spineW), spv); nrm = rX(nrm, -P[10] * spineW);
        // root: side sway about the feet and vertical bob (feet stay planted)
        p = rY(p, P[15]); nrm = rY(nrm, P[15]);
        p.z += P[14] * ((bone == 1 || bone == 2) ? smoothstep(0, std::max(.05F, kneeZ), v0.z) : 1.0F);
        if(std::fabs(st[1]) > .0001F || std::fabs(st[2]) > .0001F) {
            // death: topple about the heels (backward +, forward -) or roll to a side
            const float fall = st[1];
            const V3 fp{0, (fall > 0 ? -1.0F : 1.0F) * depthY * .42F, 0};
            p = add(rX(sub(p, fp), fall), fp); nrm = rX(nrm, fall);
            const float side = st[2];
            const V3 rp{(side > 0 ? -1.0F : 1.0F) * halfW * .5F, 0, 0};
            p = add(rY(sub(p, rp), side), rp); nrm = rY(nrm, side);
            p.z += std::max(0.0F, std::sin(std::min(std::fabs(fall) + std::fabs(side), 1.5708F))) * depthY * .12F - st[3];
        }
        // facing and position (N10 metres -> Quake III units)
        caSkinVert_t& o = out[i];
        o.xyz[0] = feet[0] + (ca * p.x - sa * p.y) * CA_UNITS_PER_METRE;
        o.xyz[1] = feet[1] + (sa * p.x + ca * p.y) * CA_UNITS_PER_METRE;
        o.xyz[2] = feet[2] + p.z * CA_UNITS_PER_METRE;
        o.normal[0] = ca * nrm.x - sa * nrm.y;
        o.normal[1] = sa * nrm.x + ca * nrm.y;
        o.normal[2] = nrm.z;
        o.st[0] = q[9];
        o.st[1] = q[10];   // N10 samples the creature texture top row first, as Quake III does
        o.color[0] = mixf(q[6], 1.0F, hit);
        o.color[1] = mixf(q[7], .33F, hit);
        o.color[2] = mixf(q[8], .24F, hit);
    }
}
