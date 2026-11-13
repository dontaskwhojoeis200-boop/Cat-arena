// N6 enemy presentation (see cat/enemy_fx.hpp). Legacy line numbers refer to legacy/index.html.
#include "cat/enemy_fx.hpp"
#include "cat/js_math.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace cat::fx {
namespace {
using physics::js_hypot;
using physics::js_hypot3;
constexpr double kPi = 3.14159265358979323846;
double clampd(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
double ss01(double a, double b, double t) { t = std::max(0.0, std::min(1.0, (t - a) / (b - a))); return t * t * (3 - 2 * t); }
double js_round(double v) { return std::floor(v + .5); }
double facing_from(double vx, double vy) { return jsmath::atan2(-vx, vy); }   // enemyFacingFromVec
struct V3 { double x, y, z; };
V3 rx(V3 p, double a) { const double c = jsmath::cos(a), s = jsmath::sin(a); return {p.x, c * p.y - s * p.z, s * p.y + c * p.z}; }
V3 ry(V3 p, double a) { const double c = jsmath::cos(a), s = jsmath::sin(a); return {c * p.x + s * p.z, p.y, -s * p.x + c * p.z}; }
V3 rz(V3 p, double a) { const double c = jsmath::cos(a), s = jsmath::sin(a); return {c * p.x - s * p.y, s * p.x + c * p.y, p.z}; }
template <class F> V3 about(V3 p, V3 piv, F f) { const V3 q = f(V3{p.x - piv.x, p.y - piv.y, p.z - piv.z}); return {q.x + piv.x, q.y + piv.y, q.z + piv.z}; }
double hit_height(combat::TargetClass c) { return c == combat::TargetClass::heavy ? 1.10 : c == combat::TargetClass::fast ? .90 : 1.0; }   // enemyHitScale(b).h
void add_light(Frame& f, double x, double y, double z, double radius, double r, double g, double b) { if(f.lights.size() < 64) f.lights.push_back({x, y, z, radius, r, g, b}); }
}  // namespace

const Rig& enemy_rig(int type) { return kEnemyRig[static_cast<std::size_t>(std::clamp(type, 0, 19))]; }
std::size_t EnemyFrame::instances() const {
    std::size_t n = marines.size();
    for(const auto& v : solid) n += v.size();
    return n;
}
void EnemyFrame::clear() {
    for(auto& v : solid) v.clear();
    for(auto& v : xray) v.clear();
    for(auto& v : shadow) v.clear();
    marines.clear(); xray_on = false;
}

AttackPhases attack_phases(double p) {   // attackPhases (10619)
    if(p < 0) return {};
    AttackPhases r; r.on = true;
    r.wind = ss01(0, .6, p) * (1 - ss01(.68, .78, p));
    r.strike = ss01(.66, .76, p) * (1 - ss01(.86, 1, p));
    r.hold = ss01(0, .25, p) * (1 - ss01(.88, 1, p));
    return r;
}

void eval_pose(AnimState& A, const Rig& rig, const PoseInput& b, double time, combat::Rng& rng, Pose& out) {   // enemyEvalPose (10630)
    if(!A.ready) {
        A = AnimState{};
        A.ready = true; A.phase = rng.next() * 6.28; A.px = b.x; A.py = b.y; A.pz = b.z; A.pa = b.a; A.seed = rng.next() * 100; A.last_t = time;
    }
    const double fdt = std::max(0.0, std::min(.1, time - A.last_t));
    A.last_t = time;
    const double moved = js_hypot(b.x - A.px, b.y - A.py);
    A.px = b.x; A.py = b.y; A.pz = b.z;
    const double inst = fdt > 0 ? moved / fdt : 0;
    A.speed += (std::min(inst, 24.0) - A.speed) * std::min(1.0, fdt * 10);
    const RigClass cls = rig.cls;
    const double H = rig.height;
    const double walk_t = std::max(0.0, std::min(1.0, A.speed / (cls == RigClass::heavy ? 3.2 : cls == RigClass::caster ? 3.4 : 4.5)));
    A.walk += (walk_t - A.walk) * std::min(1.0, fdt * 6);
    const double yaw_delta = jsmath::atan2(jsmath::sin(b.a - A.pa), jsmath::cos(b.a - A.pa));
    A.pa = b.a;
    A.turn += ((fdt > 0 ? yaw_delta / fdt : 0) - A.turn) * std::min(1.0, fdt * 5);
    if(cls == RigClass::flyer) A.phase += fdt * (7 + A.walk * 5);
    else A.phase += moved * 2 * kPi / kGaitStride[static_cast<std::size_t>(cls)];
    const double hit = std::max(0.0, std::min(1.0, b.hit_flash / .21));
    if(hit > A.pain + .3) A.pain_dir = rng.next() < .5 ? -1 : 1;
    A.pain = std::max(A.pain - fdt * 4.5, hit);
    const double w = A.walk, ph = A.phase, t = time + A.seed, pain = A.pain;
    const double S = jsmath::sin(ph), C = jsmath::cos(ph);
    double hipL = 0, hipR = 0, kneeL = 0, kneeR = 0, shPL = 0, shPR = 0, elL = .25, elR = .25, shRL = .08, shRR = .08, lean = 0, twist = 0, headP = 0, headY = 0, bob = 0, roll = 0;
    // ---- locomotion ----
    const double breathe = jsmath::sin(t * 1.7);
    if(cls == RigClass::soldier) {
        hipL = w * .55 * S; hipR = -hipL; kneeL = -w * (.12 + .7 * std::max(0.0, C)); kneeR = -w * (.12 + .7 * std::max(0.0, -C));
        shPL = -w * .42 * S + .12; shPR = w * .42 * S + .12; elL = elR = .32 + w * .22;
        lean = .07 * w; twist = .13 * w * S; headY = -twist * .7; bob = w * H * .024 * jsmath::cos(2 * ph) + (1 - w) * H * .006 * breathe; roll = .03 * w * S;
    } else if(cls == RigClass::heavy) {
        hipL = w * .42 * S; hipR = -hipL; kneeL = -w * (.1 + .5 * std::max(0.0, C)); kneeR = -w * (.1 + .5 * std::max(0.0, -C));
        shRL = shRR = .26 + .04 * breathe; shPL = -w * .36 * S + .1; shPR = w * .36 * S + .1; elL = elR = .45;
        lean = .12 + .06 * w; twist = .08 * w * S; roll = .075 * w * S; headY = -twist * .5;
        bob = -w * H * .04 * jsmath::pow(std::fabs(S), 4) + w * H * .012 + (1 - w) * H * .008 * breathe; headP = w * .06 * jsmath::pow(std::fabs(S), 4);
    } else if(cls == RigClass::beast) {
        hipL = w * .72 * S; hipR = w * .72 * jsmath::sin(ph + .55); kneeL = -w * (.3 + .75 * std::max(0.0, C)); kneeR = -w * (.3 + .75 * std::max(0.0, jsmath::cos(ph + .55)));
        shPL = .35 + w * (.75 + .65 * jsmath::sin(ph + kPi)); shPR = .35 + w * (.75 + .65 * jsmath::sin(ph + kPi + .5)); elL = elR = .45 + .35 * w; shRL = shRR = .14;
        lean = .1 + .24 * w + .06 * w * jsmath::sin(2 * ph); headP = -lean * .75; bob = w * H * .05 * std::max(0.0, jsmath::sin(ph + .7)) + (1 - w) * H * .01 * breathe; roll = .02 * w * S;
    } else if(cls == RigClass::skitter) {
        const double q = ph * 1.0;
        hipL = w * .48 * jsmath::sin(q); hipR = -hipL; kneeL = -w * .4 * std::max(0.0, jsmath::cos(q)); kneeR = -w * .4 * std::max(0.0, -jsmath::cos(q));
        shPL = .25 + w * .55 * jsmath::sin(q + kPi / 2); shPR = .25 - w * .55 * jsmath::sin(q + kPi / 2); shRL = shRR = .38; elL = elR = .5;
        lean = .1; twist = .12 * w * jsmath::sin(q); bob = w * H * .022 * std::fabs(jsmath::sin(2 * q)) + (1 - w) * H * .006 * breathe; roll = .04 * w * jsmath::sin(q);
    } else if(cls == RigClass::flyer) {
        const double f = jsmath::sin(ph), f2 = jsmath::sin(ph - .95);
        hipL = hipR = .28 + .58 * f; kneeL = kneeR = .42 * f2;
        lean = .22 * w - .04; roll = std::max(-.5, std::min(.5, -A.turn * .18)); bob = -H * .05 * f; headP = -lean * .6;
    } else {   // caster: hovering glide, robes barely stride
        hipL = w * .2 * S; hipR = -hipL; kneeL = -w * .18 * std::max(0.0, C); kneeR = -w * .18 * std::max(0.0, -C);
        shRL = shRR = .32 + .06 * jsmath::sin(t * 2.1); shPL = .38 + .1 * jsmath::sin(t * 1.3); shPR = .38 + .1 * jsmath::sin(t * 1.3 + 1); elL = elR = .75;
        lean = .04 + .05 * w; bob = H * .022 * jsmath::sin(t * 2.2) + H * .012; roll = .03 * jsmath::sin(t * 1.1);
    }
    // ---- look at the target (head first, then the spine) ----
    if(b.has_target) {
        const double dx = b.tx - b.x, dy = b.ty - b.y, d = js_hypot(dx, dy);
        if(d < 30 && d > .4) {
            const double f = facing_from(dx, dy), want = jsmath::atan2(jsmath::sin(f - b.a), jsmath::cos(f - b.a));
            A.look += (std::max(-.9, std::min(.9, want)) - A.look) * std::min(1.0, fdt * 5);
            const double pitch = jsmath::atan2(b.tz + 1.2 - (b.z + rig.neck), d);
            A.look_p += (std::max(-.6, std::min(.6, -pitch)) - A.look_p) * std::min(1.0, fdt * 4);
        }
    } else {
        A.look += (.35 * jsmath::sin(t * .37) * (1 - w) - A.look) * std::min(1.0, fdt * 2);
        A.look_p *= 1 - std::min(1.0, fdt * 2);
    }
    headY += A.look * .65; twist += A.look * .3; headP += A.look_p * .7;
    // ---- attacks ----
    const AttackPhases P = attack_phases(b.attack_progress);
    if(P.on) {
        const double wind = P.wind, strike = P.strike, hold = P.hold;
        switch(rig.attack) {
        case AttackAnim::shoot:   // shoulder the weapon, aim, recoil
            shPR = shPR * (1 - hold) + (1.42 + A.look_p * .8) * hold + .3 * strike; elR = elR * (1 - hold) + .08 * hold;
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
    if(pain > 0) { lean -= .32 * pain; headP -= .34 * pain; twist += .22 * pain * A.pain_dir; shRL += .4 * pain; shRR += .4 * pain; elL += .35 * pain; elR += .35 * pain; }
    const double v[16] = {hipL, hipR, kneeL, kneeR, shPL, shPR, elL, elR, shRL, shRR, lean, twist, headP, headY, bob, roll};
    for(std::size_t i = 0; i < 16; ++i) out[i] = static_cast<float>(v[i]);
}

DeathState death_state(const Rig& rig, const Pose& at_death, int variant, double side_sign, double t, Pose& out) {   // enemyDeathState (10742)
    const double k = std::min(1.0, t / kDeathFall), e = k * k;
    const double bounce = k >= 1 ? 0 : jsmath::sin(std::max(0.0, (k - .82) / .18) * kPi) * .08;
    for(std::size_t j = 0; j < 16; ++j) out[j] = static_cast<float>(static_cast<double>(at_death[j]) * (1 - ss01(0, .6, k)));
    double fall = 0, side = 0;
    if(variant == 0) fall = (kPi / 2) * e - bounce;
    else if(variant == 1) fall = -(kPi / 2) * e + bounce;
    else if(variant == 2) { side = side_sign * (kPi / 2) * e; fall = .25 * e; }
    else { fall = (kPi / 2.4) * e; side = side_sign * .4 * e; }
    const double slack = ss01(.2, 1, k);
    const auto add = [&](std::size_t i, double v) { out[i] = static_cast<float>(static_cast<double>(out[i]) + v); };
    if(rig.cls != RigClass::flyer) { add(2, -.9 * slack); add(3, -.7 * slack); add(0, .5 * slack); add(1, .2 * slack); add(8, .7 * slack); add(9, .5 * slack); add(6, .6 * slack); add(7, .8 * slack); add(12, .4 * slack); }
    else { add(0, -.5 * slack); add(1, -.5 * slack); add(2, -.8 * slack); add(3, -.8 * slack); }
    const double sink_t = std::max(0.0, t - kDeathFall - kDeathStay) / kDeathSink;
    return {fall, side, sink_t * sink_t * (rig.depth * 1.6 + .4)};
}

std::array<double, 3> hand_world(const Rig& rig, double x, double y, double z, double a, int side, const Pose& P) {   // enemyHandWorld (10759)
    const bool left = side < 0;
    const double sg = left ? 1 : -1;
    const double c = jsmath::cos(a), s = jsmath::sin(a);
    if(rig.cls == RigClass::flyer) {
        V3 p{0, rig.depth * .7, rig.shoulder * .8};
        p = about(p, V3{0, 0, rig.spine}, [&](V3 q) { return rx(q, -static_cast<double>(P[10])); });
        p.z += static_cast<double>(P[14]);
        return {x + c * p.x - s * p.y, y + s * p.x + c * p.y, z + p.z};
    }
    const double sp = left ? P[4] : P[5], el = left ? P[6] : P[7], sr = left ? P[8] : P[9];
    V3 p{-sg * rig.shoulder_x * 1.06, rig.depth * .18, std::max(rig.elbow - (rig.shoulder - rig.elbow) * .95, .12)};
    p = about(p, V3{-sg * rig.shoulder_x, 0, rig.elbow}, [&](V3 q) { return rx(q, el); });
    const V3 sh{-sg * rig.shoulder_x, 0, rig.shoulder};
    p = about(p, sh, [&](V3 q) { return ry(q, sr * sg); });
    p = about(p, sh, [&](V3 q) { return rx(q, sp); });
    const V3 spv{0, 0, rig.spine};
    p = about(p, spv, [&](V3 q) { return rz(q, static_cast<double>(P[11])); });
    p = about(p, spv, [&](V3 q) { return rx(q, -static_cast<double>(P[10])); });
    p = ry(p, static_cast<double>(P[15]));
    p.z += static_cast<double>(P[14]);
    return {x + c * p.x - s * p.y, y + s * p.x + c * p.y, z + p.z};
}

bool in_view(const physics::PlayerState& p, double fov_deg, double aspect, double x, double y, double z, double r) {   // inView (5302)
    const double ca = jsmath::cos(p.a), sa = jsmath::sin(p.a), cp = jsmath::cos(p.pitch), sp = jsmath::sin(p.pitch);
    const double th = jsmath::tan(std::min(88.0, fov_deg * .5 + 3) * kPi / 180), tv = th / aspect;
    const double dx = x - p.x, dy = y - p.y, eye = p.z + (p.downed ? .62 : 1.5) - p.crouch_offset, dz = z - eye;
    const double depth = dx * ca * cp + dy * sa * cp - dz * sp;
    if(depth < -r - 1.25) return false;
    const double right = -dx * sa + dy * ca, up = dx * ca * sp + dy * sa * sp + dz * cp;
    return std::fabs(right) <= std::max(0.0, depth) * th + r + 2.0 && std::fabs(up) <= std::max(0.0, depth) * tv + r + 2.0;
}

int marine_part_at(double x, double y, double z) {
    for(std::size_t i = 0; i < kMarinePart.size(); ++i) {
        const auto& part = kMarinePart[i];
        const auto& a = kMarineJoint[static_cast<std::size_t>(part.bone_a)].rest;
        if(!part.capsule) {
            const double cx = a[0] + part.center[0], cy = a[1] + part.center[1], cz = a[2] + part.center[2];
            if(std::fabs(x - cx) <= part.size[0] / 2 && std::fabs(y - cy) <= part.size[1] / 2 && std::fabs(z - cz) <= part.size[2] / 2) return static_cast<int>(i);
        } else {
            const auto& b = kMarineJoint[static_cast<std::size_t>(part.bone_b)].rest;
            const double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2], len2 = ux * ux + uy * uy + uz * uz;
            const double t = len2 > 0 ? clampd(((x - a[0]) * ux + (y - a[1]) * uy + (z - a[2]) * uz) / len2, 0, 1) : 0;
            const double dx = x - (a[0] + ux * t), dy = y - (a[1] + uy * t), dz = z - (a[2] + uz * t);
            if(dx * dx + dy * dy + dz * dz <= part.radius * part.radius) return static_cast<int>(i);
        }
    }
    return -1;
}

// ---- EnemyFx -------------------------------------------------------------------------------------------
EnemyFx::EnemyFx(WeaponFx& fx, std::uint32_t seed) : fx_(&fx), rng_(seed) {}
void EnemyFx::clear() {
    anim_.clear(); trail_.clear(); bodies_.clear(); guests_.clear();
    marine_anim_ = AnimState{}; marine_pose_ = Pose{}; marine_shoot_ = marine_melee_ = marine_hurt_ = 0;
}
const Pose& EnemyFx::pose(const combat::Target& b, const combat::Arena& arena) {
    auto& live = anim_[b.uid];
    const auto& ps = arena.player().state();
    PoseInput in;
    in.x = b.x; in.y = b.y; in.z = b.z; in.a = b.a; in.hit_flash = b.hit_flash; in.attack_progress = combat::attack_progress(b);
    in.has_target = !ps.downed; in.tx = ps.x; in.ty = ps.y; in.tz = ps.z;
    eval_pose(live.anim, enemy_rig(b.type), in, arena.now(), rng_, live.pose);
    live.posed = true;
    return live.pose;
}
void EnemyFx::marine(AnimState& anim, Pose& pose, double x, double y, double z, double a, double now, double shoot_until, double melee_until, double hurt_until, bool downed) {   // drawMarineCats: add(p)
    PoseInput in;   // p._isMarine: no target, no attack
    in.x = x; in.y = y; in.z = z; in.a = a;
    eval_pose(anim, kMarineRig, in, now, rng_, pose);
    const auto add = [&](std::size_t i, double v) { pose[i] = static_cast<float>(static_cast<double>(pose[i]) + v); };
    const double shoot = clampd((shoot_until - now) / .14, 0, 1), melee = clampd((melee_until - now) / .42, 0, 1), hurt = clampd((hurt_until - now) / .27, 0, 1);
    if(shoot != 0) { add(5, .08 * shoot); add(7, .06 * shoot); add(10, -.055 * shoot); }
    if(melee != 0) { add(5, .22 * melee); add(7, -.17 * melee); add(11, .07 * melee); }
    if(hurt != 0) { add(10, -.12 * hurt); add(12, .08 * hurt); }
    if(downed) { pose.fill(0); pose[15] = -.84F; pose[2] = -.25F; pose[3] = -.23F; pose[12] = -.12F; }
}
const Pose& EnemyFx::marine_pose(const combat::Arena& arena) {   // the local player (drawMarineCats.self)
    const auto& ps = arena.player().state();
    marine(marine_anim_, marine_pose_, ps.x, ps.y, ps.z, ps.a, arena.now(), marine_shoot_, marine_melee_, marine_hurt_, ps.downed);
    return marine_pose_;
}
void EnemyFx::stage_body(std::uint32_t corpse_id, int type, const PoseInput& at_death, double time, int variant, double side, double flash) {
    AnimState fresh;
    Body body;
    eval_pose(fresh, enemy_rig(type), at_death, time, rng_, body.pose);
    body.corpse_id = corpse_id; body.type = type; body.variant = enemy_rig(type).cls == RigClass::flyer ? 3 : variant; body.side = side; body.flash = flash;
    bodies_.push_back(body);
    if(bodies_.size() > 24) bodies_.erase(bodies_.begin());
}

void EnemyFx::start_death(const combat::DeathEvent& e) {   // enemyStartDeath (10725)
    auto& live = anim_[e.uid];
    PoseInput in;
    in.x = e.x; in.y = e.y; in.z = e.z; in.a = e.a; in.hit_flash = e.hit_flash; in.attack_progress = e.attack_progress;
    in.has_target = true; in.tx = e.player_x; in.ty = e.player_y; in.tz = e.player_z;
    Body body;
    eval_pose(live.anim, enemy_rig(e.type), in, e.now, rng_, body.pose);
    body.variant = static_cast<int>(rng_.next() * 3);
    if(enemy_rig(e.type).cls == RigClass::flyer) body.variant = 3;
    body.side = rng_.next() < .5 ? -1 : 1;
    body.corpse_id = e.corpse_id; body.type = e.type; body.flash = 1;
    bodies_.push_back(body);
    if(bodies_.size() > 24) bodies_.erase(bodies_.begin());
}
void EnemyFx::gibs(double x, double y, double z, double scale) {   // WFX.gibs (10962)
    auto& f = *fx_;
    if(!f.settings_.blood) { f.explosion(x, y, z + .6, .5, false); return; }
    const int n = static_cast<int>(js_round(9 * f.settings_.detail));
    for(int i = 0; i < n; ++i) {
        WeaponFx::Entity e;
        e.x = x + f.rnd(.3); e.y = y + f.rnd(.3); e.z = z + .4 + f.random() * 1.2; e.vx = f.rnd(5); e.vy = f.rnd(5); e.vz = 3 + f.random() * 6; e.grav = 16; e.drag = .2; e.life = 4 + f.random() * 2;
        e.s0 = (.25 + .2 * f.random()) * scale; e.s1 = (.25 + .2 * f.random()) * scale; e.a0 = 1; e.a1 = 1; e.tile = i % 2 ? tile_gib1 : tile_gib2; e.rot = f.random() * 6.28; e.rot_v = f.rnd(10);
        e.add = 0; e.flags = 0; e.bounce = .35; e.blood = true;
        f.le(e);
    }
    for(int i = 0; i < 4; ++i) {
        const double bx = x + f.rnd(.3), by = y + f.rnd(.3), bz = z + .6 + f.random(), dx = f.rnd(), dy = f.rnd();
        f.blood(bx, by, bz, dx, dy, 0, 1.6);
    }
    WeaponFx::Entity e;
    e.x = x; e.y = y; e.z = z + .8; e.life = .5; e.s0 = 1; e.s1 = 2.4; e.a0 = .85; e.a1 = 0; e.tile = tile_blood; e.rot = f.random() * 6.28; e.add = 0; e.flags = 0; e.r = .75; e.g = .08; e.b = .06;
    f.le(e);
}

namespace {
struct ShotLook { EnemyProj proj; std::array<double, 3> col; };
ShotLook look_of(int type, combat::ShotKind kind) {   // EFX.look
    if(type >= 0 && type < 20 && kEnemyLook[static_cast<std::size_t>(type)].proj != EnemyProj::none) return {kEnemyLook[static_cast<std::size_t>(type)].proj, kEnemyLook[static_cast<std::size_t>(type)].color};
    const std::array<double, 3> col = kind == combat::ShotKind::fireball ? std::array<double, 3>{1, .5, .2} : kind == combat::ShotKind::voidorb ? std::array<double, 3>{.75, .45, 1} : std::array<double, 3>{1, .7, .4};
    return {kKindDefaultProj[static_cast<std::size_t>(kind)], col};
}
}  // namespace

void EnemyFx::shot_impact(const combat::ShotImpact& s) {   // EFX.shotImpact (11296)
    auto& f = *fx_;
    const ShotLook L = look_of(s.type, s.kind);
    const auto& c = L.col;
    const double x = s.x, y = s.y, z = s.z;
    const auto sprite_le = [&](double life, double s0, double s1, double a0, int tile, bool tinted) {
        WeaponFx::Entity e; e.x = x; e.y = y; e.z = z; e.life = life; e.s0 = s0; e.s1 = s1; e.a0 = a0; e.a1 = 0; e.tile = tile;
        if(tinted) { e.r = c[0]; e.g = c[1]; e.b = c[2]; }
        return e;
    };
    const auto light_le = [&](double life, double radius) { WeaponFx::Entity e; e.x = x; e.y = y; e.z = z; e.life = life; e.has_light = true; e.light = {c[0], c[1], c[2], radius}; e.s0 = 0; e.s1 = 0; e.a0 = 0; e.a1 = 0; f.le(e); };
    switch(L.proj) {
    case EnemyProj::fireball: case EnemyProj::rock: f.explosion(x, y, z, .62, false); break;
    case EnemyProj::voidorb: f.explosion(x, y, z, .78, true); break;
    case EnemyProj::hellfire: f.explosion(x, y, z, .55, true); f.le(sprite_le(.5, .5, 1.6, 1, tile_flame_gray, true)); break;
    case EnemyProj::bomb: {
        auto e = sprite_le(.4, .6, 2.6, 1, tile_plasma, true); e.rot_v = 5; f.le(e);
        f.le(sprite_le(.35, .4, 3.2, .8, tile_ring, true));
        light_le(.4, 6); f.sparks(x, y, z, 10, c, 6, .5);
        break; }
    case EnemyProj::acid: {
        for(int i = 0; i < 5; ++i) {
            WeaponFx::Entity e; e.x = x; e.y = y; e.z = z; e.vx = f.rnd(2); e.vy = f.rnd(2); e.vz = 1 + f.random() * 2; e.grav = 10; e.life = .5; e.s0 = .12; e.s1 = .06; e.a0 = 1; e.a1 = .3; e.tile = tile_acid; e.add = 0; e.flags = 0;
            f.le(e);
        }
        WeaponFx::Entity e; e.x = x; e.y = y; e.z = z; e.life = .35; e.s0 = .3; e.s1 = .9; e.a0 = .8; e.a1 = 0; e.tile = tile_smoke2; e.add = 0; e.flags = 0; e.r = .5; e.g = .9; e.b = .3;
        f.le(e);
        if(!s.player) f.floor_mark(x, y, z, .7, tile_mark_blood, {.25, .75, .12}, .75, MarkOptions{{}, 0, 8, false});
        break; }
    case EnemyProj::arc: {
        auto e = sprite_le(.18, .5, 1.1, 1, tile_electric, true); e.rot = f.random() * 6.28; f.le(e);
        f.sparks(x, y, z, 6, c, 5, .35); light_le(.2, 4);
        break; }
    case EnemyProj::wisp: {
        auto e = sprite_le(.45, .4, 1.6, .8, tile_smoke3, true); e.add = 1; e.flags = 0; f.le(e);
        f.le(sprite_le(.3, .3, 1.4, .7, tile_ring, true));
        break; }
    default: {
        auto e = sprite_le(.12, .3, .55, 1, tile_burst, true); e.rot = f.random() * 6.28; f.le(e);
        f.sparks(x, y, z, 4, c, 4, .3);
        if(!s.player) {
            const double d = js_hypot3(s.vx, s.vy, s.vz), len = d != 0 ? d : 1;
            if(const auto S = f.surface_at(x, y, z, s.vx / len, s.vy / len, s.vz / len))
                f.add_mark(S->x, S->y, S->z, S->nx, S->ny, S->nz, .24, L.proj == EnemyProj::bonespike || L.proj == EnemyProj::quill ? tile_mark_hole : tile_mark_scorch, {1, 1, 1}, .7);
        }
        break; }
    }
}

void EnemyFx::update(const combat::Arena& arena, const combat::Horde& horde, double dt) {   // EFX.update (11214)
    auto& f = *fx_;
    std::unordered_map<std::uint32_t, double> trail;
    for(const auto& s : horde.shots()) {
        const auto found = trail_.find(s.id);
        double ft = (found == trail_.end() ? 0 : found->second) - dt;
        if(ft > 0) { trail[s.id] = ft; continue; }
        const ShotLook L = look_of(s.type, s.kind);
        const auto& c = L.col;
        ft = .035;
        trail[s.id] = ft;
        switch(L.proj) {
        case EnemyProj::fireball: case EnemyProj::rock: {
            WeaponFx::Entity e; e.x = s.x + f.rnd(.06); e.y = s.y + f.rnd(.06); e.z = s.z + f.rnd(.06); e.vz = .5; e.life = .4; e.s0 = .34; e.s1 = .1; e.a0 = .85; e.a1 = 0; e.tile = tile_fireball; e.rot = f.random() * 6.28; e.flags = 0;
            f.le(e);
            if(f.random() < .4) f.puff(s.x, s.y, s.z, .45, .9, {.3, .28, .26}, .35, tile_smoke1, .4);
            break; }
        case EnemyProj::hellfire: {
            WeaponFx::Entity e; e.x = s.x; e.y = s.y; e.z = s.z; e.vz = .8; e.life = .45; e.s0 = .3; e.s1 = .05; e.a0 = .9; e.a1 = 0; e.tile = tile_flame_gray; e.rot = f.random() * 6.28; e.r = c[0]; e.g = c[1]; e.b = c[2];
            f.le(e); break; }
        case EnemyProj::voidorb: case EnemyProj::bomb: case EnemyProj::arc: {
            WeaponFx::Entity e; e.x = s.x + f.rnd(.1); e.y = s.y + f.rnd(.1); e.z = s.z + f.rnd(.1); e.vx = f.rnd(.4); e.vy = f.rnd(.4); e.vz = f.rnd(.4); e.life = .4; e.s0 = .16; e.s1 = .02; e.a0 = .9; e.a1 = 0; e.tile = tile_spark_dot;
            e.r = c[0]; e.g = c[1]; e.b = c[2];
            f.le(e); break; }
        case EnemyProj::acid:
            if(f.random() < .5) { WeaponFx::Entity e; e.x = s.x; e.y = s.y; e.z = s.z; e.grav = 9; e.life = .5; e.s0 = .09; e.s1 = .05; e.a0 = .9; e.a1 = .2; e.tile = tile_acid; e.add = 0; e.flags = 0; f.le(e); }
            break;
        case EnemyProj::wisp: {
            WeaponFx::Entity e; e.x = s.x; e.y = s.y; e.z = s.z; e.vx = f.rnd(.3); e.vy = f.rnd(.3); e.vz = .3; e.life = .55; e.s0 = .35; e.s1 = .1; e.a0 = .5; e.a1 = 0; e.tile = tile_smoke3; e.add = 1; e.flags = 0; e.r = c[0]; e.g = c[1]; e.b = c[2];
            f.le(e); break; }
        default: break;
        }
    }
    trail_.swap(trail);
    (void)arena;
    for(const auto& b : arena.targets()) {
        if(b.type < 0) continue;
        const auto& T = kEnemyLook[static_cast<std::size_t>(b.type)];
        auto& live = anim_[b.uid];
        const double p = combat::attack_progress(b), was = live.efx_p;
        live.efx_p = p;
        if(T.melee != EnemyMelee::none && was < .7 && p >= .7) {   // strike frame
            const double fx = -jsmath::sin(b.a), fy = jsmath::cos(b.a);
            const auto& c = T.color;
            if(T.melee == EnemyMelee::slam) {
                const double z = b.z + .05;
                WeaponFx::Entity ring; ring.x = b.x + fx * 1.2; ring.y = b.y + fy * 1.2; ring.z = z + .1; ring.life = .45; ring.s0 = .6; ring.s1 = 4.2; ring.a0 = .8; ring.a1 = 0; ring.tile = tile_ring; ring.r = c[0]; ring.g = c[1]; ring.b = c[2];
                f.le(ring);
                f.floor_mark(b.x + fx * 1.15, b.y + fy * 1.15, b.z, 2.4, tile_slam, {1, 1, 1}, .9, MarkOptions{{}, 0, 6, false});
                for(int i = 0; i < 5; ++i) { const double a = i * 1.256; f.puff(b.x + fx * 1.2 + jsmath::cos(a) * .7, b.y + fy * 1.2 + jsmath::sin(a) * .7, z + .2, 1.1, 1.1, {.55, .5, .42}, .5, tile_dust, .5); }
                WeaponFx::Entity deb; deb.x = b.x + fx * 1.2; deb.y = b.y + fy * 1.2; deb.z = z + .3; deb.vx = 0; deb.vy = 0; deb.vz = 4; deb.grav = 12; deb.life = .7; deb.s0 = .5; deb.s1 = .5; deb.a0 = 1; deb.a1 = .5; deb.tile = tile_debris; deb.add = 0; deb.flags = 0;
                deb.rot = f.random() * 6;
                f.le(deb);
                f.sparks(b.x + fx * 1.2, b.y + fy * 1.2, z + .1, 6, {1, .8, .5}, 5, .4);
            }
        }
        if(T.melee == EnemyMelee::none && T.flash && was < .72 && p >= .72) {   // muzzle flash on the release frame
            const auto h = hand_world(enemy_rig(b.type), b.x, b.y, b.z, b.a, 1, live.pose);
            WeaponFx::Entity e; e.x = h[0]; e.y = h[1]; e.z = h[2]; e.life = .09; e.s0 = .55; e.s1 = .8; e.a0 = 1; e.a1 = 0; e.tile = tile_flash_star; e.rot = f.random() * 6.28; e.r = T.color[0]; e.g = T.color[1]; e.b = T.color[2];
            f.le(e);
            WeaponFx::Entity l; l.x = h[0]; l.y = h[1]; l.z = h[2]; l.life = .12; l.has_light = true; l.light = {T.color[0], T.color[1], T.color[2], 4}; l.s0 = 0; l.s1 = 0; l.a0 = 0; l.a1 = 0;
            f.le(l);
        }
    }
}

void EnemyFx::tick(const combat::Arena& arena, const combat::Horde& horde, double fov, double aspect) {
    // Effects the gameplay code starts while it runs (killBot -> enemyDeathFx, the enemy shot loop -> EFX.shotImpact).
    // Everything in the order update() started it (Arena::next_event_seq): the volley (WFX.fire), blasts
    // (spawnExplosionVisual), enemy deaths (enemyDeathFx) and enemy shot impacts (EFX.shotImpact).
    const auto& ev = arena.events();
    struct Item { std::uint32_t seq; int kind; std::size_t index; };
    std::vector<Item> order;
    if(ev.fired_weapon >= 0) order.push_back({ev.fire_seq, 0, 0});
    for(std::size_t i = 0; i < ev.blasts.size(); ++i) order.push_back({ev.blasts[i].seq, 1, i});
    for(std::size_t i = 0; i < horde.deaths().size(); ++i) order.push_back({horde.deaths()[i].seq, 2, i});
    for(std::size_t i = 0; i < horde.impacts().size(); ++i) order.push_back({horde.impacts()[i].seq, 3, i});
    std::stable_sort(order.begin(), order.end(), [](const Item& a, const Item& b) { return a.seq < b.seq; });
    for(const auto& it : order) {
        if(it.kind == 0) fx_->start_fire(arena, fov, aspect);
        else if(it.kind == 1) fx_->start_blast(ev.blasts[it.index]);
        else if(it.kind == 3) shot_impact(horde.impacts()[it.index]);
        else {
            const auto& e = horde.deaths()[it.index];
            if(e.gibbed) { gibs(e.x, e.y, e.z, std::max(.8, hit_height(combat::enemy_types()[static_cast<std::size_t>(e.type)].hit_class))); continue; }
            start_death(e);
            fx_->blood(e.x, e.y, e.z + 1, 0, 0, -1, 1.4);
        }
    }
    fx_->advance(arena);                           // WFX.update

    update(arena, horde, physics::kTick);          // EFX.update
    // updateDyingEnemies: the Horde advances time and the fall of a flyer; here only the hit flash and the list.
    std::unordered_set<std::uint32_t> alive_corpses;
    for(const auto& c : horde.corpses()) alive_corpses.insert(c.id);
    for(std::size_t i = bodies_.size(); i-- > 0;) {
        bodies_[i].flash = std::max(0.0, bodies_[i].flash - physics::kTick * 5);
        if(!alive_corpses.count(bodies_[i].corpse_id)) bodies_.erase(bodies_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    // forget the animation state of enemies that are gone
    if(anim_.size() > arena.targets().size()) {
        std::unordered_set<std::uint32_t> alive;
        for(const auto& b : arena.targets()) if(b.type >= 0) alive.insert(b.uid);
        for(auto it = anim_.begin(); it != anim_.end();) it = alive.count(it->first) ? std::next(it) : anim_.erase(it);
    }
}

void EnemyFx::build_fx(const combat::Arena& arena, const combat::Horde& horde, const BuildOptions& o, Frame& frame) {   // EFX.build (11247)
    auto& main = frame.main;
    const Camera& cam = frame.camera;
    const double t = arena.now();
    const auto& ps = arena.player().state();
    const double fov = o.fov_deg * arena.player().fov_scale();
    for(const auto& s : horde.shots()) {
        const ShotLook L = look_of(s.type, s.kind);
        const auto& c = L.col;
        const double vlen = js_hypot3(s.vx, s.vy, s.vz), v = vlen != 0 ? vlen : 1, ux = s.vx / v, uy = s.vy / v, uz = s.vz / v, sp = std::fmod(s.x * 7 + s.y * 3, 6.28);
        const auto seg = [&](double back, double fwd, double w, int tile, double r, double g, double bl, double a, double add, int flags) {
            beam(main, cam, s.x - ux * back, s.y - uy * back, s.z - uz * back, s.x + ux * fwd, s.y + uy * fwd, s.z + uz * fwd, w, tile, r, g, bl, a, add, 0, 1, flags);
        };
        const auto spr = [&](double w, double rot, int tile, double r, double g, double bl, double a, double add = 1, int flags = 4) { sprite(main, cam, s.x, s.y, s.z, w, w, rot, tile, r, g, bl, a, add, flags); };
        switch(L.proj) {
        case EnemyProj::blaster: seg(.55, .2, .24, tile_blaster, c[0], c[1], c[2], 1, 1, 4); spr(.5, 0, tile_glow, c[0], c[1], c[2], .45, 1); add_light(frame, s.x, s.y, s.z, 2.6, c[0] * .6, c[1] * .6, c[2] * .6); break;
        case EnemyProj::dart: seg(.6, .15, .2, tile_blaster, c[0], c[1], c[2], 1, 1, 4); seg(1.4, 0, .08, tile_tracer, c[0], c[1], c[2], .6, 1, 4); add_light(frame, s.x, s.y, s.z, 2.2, c[0] * .5, c[1] * .5, c[2] * .5); break;
        case EnemyProj::laser: seg(1.1, .1, .12, tile_laser, c[0], c[1], c[2], 1, 1, 4); spr(.32, 0, tile_glow, c[0], c[1], c[2], .6, 1); add_light(frame, s.x, s.y, s.z, 2.2, c[0] * .5, c[1] * .5, c[2] * .5); break;
        case EnemyProj::bonespike: seg(.55, .35, .26, tile_spike, 1, 1, 1, 1, 0, 0); seg(1.5, -.3, .1, tile_tracer, c[0], c[1], c[2], .45, 1, 4); break;
        case EnemyProj::quill: seg(.6, .3, .28, tile_feather, 1, 1, 1, 1, 0, 0); seg(1.3, -.3, .08, tile_tracer, c[0], c[1], c[2], .35, 1, 4); break;
        case EnemyProj::arc: spr(.55, t * 25 + sp, tile_electric, c[0], c[1], c[2], 1, 1); spr(.8, 0, tile_glow, c[0], c[1], c[2], .5, 1); add_light(frame, s.x, s.y, s.z, 3, c[0] * .7, c[1] * .7, c[2] * .7); break;
        case EnemyProj::acid: spr(.32, t * 3 + sp, tile_acid, 1, 1, 1, 1, 0, 0); spr(.6, 0, tile_glow, c[0], c[1], c[2], .35, 1); break;
        case EnemyProj::rock: spr(.38, t * 5 + sp, tile_rock, 1, 1, 1, 1, 0, 0); spr(.7, 0, tile_glow, 1, .5, .2, .45, 1); add_light(frame, s.x, s.y, s.z, 3, 1, .45, .15); break;
        case EnemyProj::fireball: spr(.5, t * 7 + sp, tile_fireball, 1, 1, 1, 1, 1, 0); spr(.95, 0, tile_glow, 1, .5, .18, .45, 1); add_light(frame, s.x, s.y, s.z, 3.6, 1, .5, .16); break;
        case EnemyProj::hellfire: spr(.34, 0, tile_dark_core, 1, 1, 1, .8, 0, 0); spr(.55, t * 8 + sp, tile_flame_gray, c[0], c[1], c[2], 1, 1); spr(.9, 0, tile_glow, c[0], c[1], c[2], .4, 1);
            add_light(frame, s.x, s.y, s.z, 3.4, c[0] * .7, c[1] * .5, c[2] * .8); break;
        case EnemyProj::voidorb: spr(.62, t * 6 + sp, tile_swirl, c[0], c[1], c[2], 1, 1); spr(.3, 0, tile_dark_core, 1, 1, 1, .9, 0, 0); spr(1, 0, tile_glow, c[0], c[1], c[2], .4, 1);
            add_light(frame, s.x, s.y, s.z, 3.8, c[0] * .6, c[1] * .4, c[2] * .8); break;
        case EnemyProj::wisp: seg(.75, .2, .55, tile_wisp, c[0], c[1], c[2], 1, 1, 4); add_light(frame, s.x, s.y, s.z, 2.8, c[0] * .5, c[1] * .6, c[2] * .7); break;
        case EnemyProj::bomb: spr(.6, t * 10 + sp, tile_plasma, c[0], c[1], c[2], 1, 1); spr(1.1, 0, tile_glow, c[0], c[1], c[2], .45, 1); add_light(frame, s.x, s.y, s.z, 4, c[0] * .6, c[1] * .8, c[2] * .5); break;
        case EnemyProj::none: break;
        }
    }
    // charge-up glows at the animated hands, melee arcs
    for(const auto& b : arena.targets()) {
        if(b.type < 0) continue;
        const auto& T = kEnemyLook[static_cast<std::size_t>(b.type)];
        const double p = combat::attack_progress(b);
        if(p < 0) continue;
        if(!in_view(ps, fov, o.aspect, b.x, b.y, b.z + 1.2, 3.4)) continue;
        const auto& c = T.color;
        const Rig& rig = enemy_rig(b.type);
        const Pose& pose = anim_[b.uid].pose;
        if(T.charge && p > .18 && p < .76) {
            const double k = std::min(1.0, (p - .18) / .5), flick = .85 + .15 * jsmath::sin(t * 40 + b.x);
            const int hands[2] = {-1, 1};
            const bool both = rig.cls != RigClass::flyer && T.rune;
            for(int hi = both ? 0 : 1; hi < 2; ++hi) {
                const auto h = hand_world(rig, b.x, b.y, b.z, b.a, hands[hi], pose);
                sprite(main, cam, h[0], h[1], h[2], .25 + .45 * k, .25 + .45 * k, t * 4, T.proj == EnemyProj::rock ? tile_flame_gray : tile_plasma, c[0], c[1], c[2], flick, 1);
                sprite(main, cam, h[0], h[1], h[2], .6 + .8 * k, .6 + .8 * k, 0, tile_glow, c[0], c[1], c[2], .45 * k, 1);
                if(T.rune) sprite(main, cam, h[0], h[1], h[2], .5 + .5 * k, .5 + .5 * k, -t * 2.5, tile_rune, c[0], c[1], c[2], .7 * k, 1);
                add_light(frame, h[0], h[1], h[2], 2.5 + 2 * k, c[0] * k * .7, c[1] * k * .7, c[2] * k * .7);
            }
        }
        if(T.melee != EnemyMelee::none && p > .66 && p < .9) {
            const double k = (p - .66) / .24, fx = -jsmath::sin(b.a), fy = jsmath::cos(b.a), a = jsmath::sin(k * kPi);
            if(T.melee == EnemyMelee::claw) { const auto h = hand_world(rig, b.x, b.y, b.z, b.a, 1, pose); sprite(main, cam, (h[0] + b.x + fx * .9) / 2, (h[1] + b.y + fy * .9) / 2, b.z + 1, 1.6, 1.3, -.6 + k * 1.6, tile_claw, c[0], c[1], c[2], a, 1); }
            else if(T.melee == EnemyMelee::bite) sprite(main, cam, b.x + fx * .8, b.y + fy * .8, b.z + .55, .9, .9, 0, tile_bite, c[0], c[1], c[2], a, 1);
            else sprite(main, cam, b.x + fx * .9, b.y + fy * .9, b.z + 1.6 - 1.2 * k, 1.2, 1.2, k * 3, tile_burst, c[0], c[1], c[2], a * .6, 1);
        }
    }
    // blob shadows (cg_shadows 1): under creatures (mode 3: under flyers only, the others get projected bodies)
    if(o.shadows == 1 || o.shadows == 3) {
        const auto& world = arena.world();
        for(const auto& b : arena.targets()) {
            if(b.type < 0) continue;
            if(o.shadows == 3 && !b.flying) continue;
            if(js_hypot(b.x - cam.x, b.y - cam.y) > 40) continue;
            const Rig& r = enemy_rig(b.type);
            const double size = std::max(r.width, r.depth) * 1.5, alpha = .7;
            const double f = world.floor_height(b.x, b.y, b.z + .3);
            if(f < -900) continue;
            const double h = b.z - f;
            if(h > 6) continue;
            const double a = alpha * std::max(0.0, 1 - h / 6);
            if(a < .02) continue;
            const double s = size * (1 + h * .12) * .5, z = f + .02;
            quad4(frame.mark, cam, {{{b.x - s, b.y - s, z}, {b.x + s, b.y - s, z}, {b.x + s, b.y + s, z}, {b.x - s, b.y + s, z}}}, 0, 0, 1, 1, tile_shadow, 0, 1, 1, 1, a, 0);
        }
    }
}

void EnemyFx::build(const combat::Arena& arena, const combat::Horde& horde, const BuildOptions& o, Frame& frame, EnemyFrame& out) {
    out.clear();
    ++frame_;
    // render(): EFX.build runs before drawEnemyInstances, so glows and claw arcs follow the hands of the
    // pose a creature had when it was last drawn (a zero pose if it never was).
    build_fx(arena, horde, o, frame);
    const auto& ps = arena.player().state();
    const double fov = o.fov_deg * arena.player().fov_scale();
    const auto put = [](std::vector<EnemyInstance>& list, double x, double y, double z, double a, double hit, double fall, double side, double sink, const Pose& pose, double floor_z) {
        EnemyInstance e{};
        e.pos[0] = static_cast<float>(x); e.pos[1] = static_cast<float>(y); e.pos[2] = static_cast<float>(z); e.pos[3] = static_cast<float>(a);
        e.state[0] = static_cast<float>(hit); e.state[1] = static_cast<float>(fall); e.state[2] = static_cast<float>(side); e.state[3] = static_cast<float>(sink);
        std::copy(pose.begin(), pose.end(), e.pose);
        e.tint[0] = static_cast<float>(floor_z);
        list.push_back(e);
    };
    // fillEnemyInstances: live creatures in the order of the enemy list, then the dying / dead bodies
    const int remaining = horde.wave_total() - horde.wave_kills();
    out.xray_on = !(horde.wave_cooldown() >= 0 || remaining > 5 || remaining < 1 || arena.targets().empty());   // drawFinalFiveXray
    out.shadow_dir = {static_cast<float>(-jsmath::cos(o.sun_azimuth) * .55), static_cast<float>(-jsmath::sin(o.sun_azimuth) * .55)};
    for(const auto& b : arena.targets()) {
        if(b.type < 0) continue;
        const auto ti = static_cast<std::size_t>(b.type);
        const bool seen = in_view(ps, fov, o.aspect, b.x, b.y, b.z + 1, 1.85), seen_xray = out.xray_on && in_view(ps, fov, o.aspect, b.x, b.y, b.z + 1, 2.1);
        if(!seen && !seen_xray) continue;
        auto& live = anim_[b.uid];
        if(live.frame != frame_ || !live.posed) { pose(b, arena); live.frame = frame_; }
        if(seen && out.solid[ti].size() < 128) put(out.solid[ti], b.x, b.y, b.z, b.a, clampd(b.hit_flash * 5.2, 0, 1), 0, 0, 0, live.pose, b.z);
        if(seen_xray && out.xray[ti].size() < 5) put(out.xray[ti], b.x, b.y, b.z, b.a, 0, 0, 0, 0, live.pose, b.z);
        if(o.shadows == 3 && seen && !b.flying && js_hypot(b.x - ps.x, b.y - ps.y) <= 36 && out.shadow[ti].size() < 48) put(out.shadow[ti], b.x, b.y, b.z, b.a, clampd(b.hit_flash * 5.2, 0, 1), 0, 0, 0, live.pose, b.z);
    }
    for(const auto& d : horde.corpses()) {
        const auto body = std::find_if(bodies_.begin(), bodies_.end(), [&](const Body& k) { return k.corpse_id == d.id; });
        if(body == bodies_.end()) continue;
        const auto ti = static_cast<std::size_t>(d.type);
        if(out.solid[ti].size() >= 128 || !in_view(ps, fov, o.aspect, d.x, d.y, d.z + .6, 2.4)) continue;
        const DeathState st = death_state(enemy_rig(d.type), body->pose, body->variant, body->side, d.t, scratch_);
        put(out.solid[ti], d.x, d.y, d.z, d.a, body->flash * .5, st.fall, st.side, st.sink, scratch_, d.floor_z);
    }
    if(o.local_marine && !ps.downed) {   // drawMarineCats(withLocal)
        const Pose& pose = marine_pose(arena);
        EnemyInstance e{};
        e.pos[0] = static_cast<float>(ps.x); e.pos[1] = static_cast<float>(ps.y); e.pos[2] = static_cast<float>(ps.z); e.pos[3] = static_cast<float>(ps.a - kPi / 2);
        e.state[1] = static_cast<float>(std::min(1.0, js_hypot(ps.vx, ps.vy) / 7.5));
        std::copy(pose.begin(), pose.end(), e.pose);
        e.tint[0] = e.tint[1] = e.tint[2] = 1;
        out.marines.push_back(e);
    }
    for(const auto& g : o.guests) {   // netPlayers: other players' Marine Cats, at most four in all
        if(out.marines.size() >= 4 || !std::isfinite(g.x) || !std::isfinite(g.y)) break;
        auto& [anim, pose] = guests_[g.id];
        marine(anim, pose, g.x, g.y, g.z, g.a, arena.now(), g.shoot_until, g.melee_until, g.hurt_until, g.downed);
        EnemyInstance e{};
        e.pos[0] = static_cast<float>(g.x); e.pos[1] = static_cast<float>(g.y); e.pos[2] = static_cast<float>(g.z); e.pos[3] = static_cast<float>(g.a - kPi / 2);
        e.state[0] = static_cast<float>(g.step); e.state[1] = static_cast<float>(std::min(1.0, js_hypot(g.vx, g.vy) / 7.5));
        std::copy(pose.begin(), pose.end(), e.pose);
        e.tint[0] = g.color[0]; e.tint[1] = g.color[1]; e.tint[2] = g.color[2];
        out.marines.push_back(e);
    }
}
}  // namespace cat::fx
