// N3 physics tests. GPU-free.
//   cat_arena_physics_tests                                  unit tests on a small synthetic world
//   cat_arena_physics_tests <assets/n1> <helios_trajectories.bin>   + replay of trajectories recorded from
//        the unmodified legacy game (tools/n3/legacy_trajectories.cjs) on the baked Helios Crucible map
#include "cat/physics.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace cat;
using namespace cat::physics;
namespace {
int checks = 0;
void check(bool value, const std::string& why) { ++checks; if(!value) throw std::runtime_error(why); }
bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }
std::string num(double v) { return std::to_string(v); }

// ---- synthetic world ------------------------------------------------------------------------------
using V = assets::Value;
V arr_of(std::vector<V> v) { return V(V::Array(std::move(v))); }
// Built element by element: GCC 13 -O3 reports a false -Warray-bounds for vector<Value>{...} initializer lists.
template<class... T> V arr(T&&... items) { V::Array a; a.reserve(sizeof...(items)); (a.push_back(V(std::forward<T>(items))), ...); return V(std::move(a)); }
V poly(std::initializer_list<std::array<double, 2>> pts) { std::vector<V> out; for(const auto& p : pts) out.push_back(arr(p[0], p[1])); return arr_of(std::move(out)); }
V rect(double x0, double y0, double x1, double y1) { return poly({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}); }
V slab(const char* id, const char* kind, V p, double z, double thickness, bool nav, V plane = V(nullptr), V base = V(nullptr)) {
    V::Object o;
    o["id"] = V(std::string(id)); o["kind"] = V(std::string(kind)); o["poly"] = std::move(p); o["z"] = V(z); o["thickness"] = V(thickness);
    o["nav"] = V(nav); o["plane"] = std::move(plane); o["base"] = std::move(base);
    return V(std::move(o));
}
V plane(double ox, double oy, double dx, double dy, double len, double z0, double z1) {
    V::Object o;
    o["ox"] = V(ox); o["oy"] = V(oy); o["dx"] = V(dx); o["dy"] = V(dy); o["len"] = V(len); o["z0"] = V(z0); o["z1"] = V(z1);
    return V(std::move(o));
}
V wall(double ax, double ay, double bx, double by, double z0, double z1) {
    V::Object o;
    o["ax"] = V(ax); o["ay"] = V(ay); o["bx"] = V(bx); o["by"] = V(by); o["z0"] = V(z0); o["z1"] = V(z1);
    return V(std::move(o));
}
// 40 x 40 m room, floor at z = 0. Features (x ranges, all spanning y 10..14 unless noted):
//   step block       x 12..14  top 0.40   (walkable step)
//   high block       x 12..14  top 0.80   at y 16..20 (too high to step on)
//   ramp             x 18..24  0 -> 3     (30 degree-ish incline, 26.6 deg)
//   deck             x 24..32  top 3.0, 0.5 thick (stacked floor: walk on it and under it)
//   low deck         x 4..8    top 2.1, 0.5 thick at y 24..30 (underside 1.6: crouch only)
//   wall             x = 30, y 20..36, z 0..3
//   pad              (6, 6), apex (10, 6, 5)
V synthetic_world() {
    std::vector<V> slabs;
    slabs.push_back(slab("floor", "floor", rect(0, 0, 40, 40), 0, .5, true));
    slabs.push_back(slab("step", "block", rect(12, 10, 14, 14), .40, .40, false));
    slabs.push_back(slab("high", "block", rect(12, 16, 14, 20), .80, .80, false));
    slabs.push_back(slab("ramp", "ramp", rect(18, 10, 24, 14), 3, .5, true, plane(18, 12, 1, 0, 6, 0, 3), V(0.0)));
    slabs.push_back(slab("deck", "bridge", rect(24, 10, 32, 14), 3, .5, true));
    slabs.push_back(slab("low", "bridge", rect(4, 24, 8, 30), 2.1, .5, true));
    std::vector<V> ids;
    for(std::size_t i = 0; i < slabs.size(); ++i) ids.push_back(V(static_cast<double>(i)));
    std::vector<V> slab_grid, empty_grid;
    for(int i = 0; i < 100; ++i) { slab_grid.push_back(arr_of(ids)); empty_grid.push_back(arr()); }
    V::Object pad;
    pad["id"] = V(std::string("pad")); pad["x"] = V(6.0); pad["y"] = V(6.0); pad["z"] = V(0.0); pad["r"] = V(1.0); pad["cooldown"] = V(.35);
    pad["apex"] = arr(10.0, 6.0, 5.0);
    V::Object spawn;
    spawn["x"] = V(20.0); spawn["y"] = V(30.0); spawn["z"] = V(0.0); spawn["a"] = V(0.0); spawn["tag"] = V(std::string("test"));
    V::Object c;
    c["cell"] = V(4.0); c["gw"] = V(10.0);
    c["slabs"] = arr_of(std::move(slabs)); c["walls"] = arr(wall(30, 20, 30, 36, 0, 3));
    c["solids"] = arr(); c["cavities"] = arr(); c["footprint"] = arr(rect(0, 0, 40, 40));
    c["slabGrid"] = arr_of(std::move(slab_grid)); c["solidGrid"] = arr_of(empty_grid); c["cavityGrid"] = arr_of(empty_grid);
    c["pads"] = arr(V(std::move(pad))); c["spawns"] = arr(V(std::move(spawn)));
    return V(std::move(c));
}
void run(PlayerController& pc, std::uint32_t keys, int ticks, double dx = 0) { for(int i = 0; i < ticks; ++i) pc.step({keys, dx, 0}); }
double speed(const PlayerController& pc) { return std::hypot(pc.state().vx, pc.state().vy); }

void unit_tests() {
    const V data = synthetic_world();
    const CollisionWorld world(data);
    // ---- constants and the crouch collider discrepancy ----
    check(kTick == 1.0 / 120.0 && kWalkSpeed == 7.6 && kGravity == 18 && kJumpImpulse == 7.65 && kPlayerRadius == .30, "legacy constants");
    check(collider_height(0) == 1.80, "standing collider height");
    check(near(collider_height(kCrouchOffsetTarget), 1.506, 1e-12), "effective crouch collider is 1.80 - 0.49*0.60 = 1.506 m, not the nominal 1.20 m");
    check(collider_height(5) == kPlayerCrouchHeight, "collider height floor at the nominal crouch height");
    check(near(js_hypot(3, 4), 5, 1e-15) && js_hypot(0, 0) == 0 && js_hypot(-2, 0) == 2, "js_hypot");
    // ---- queries ----
    check(world.walkable(20, 20) && !world.walkable(-1, 20) && !world.walkable(41, 20), "footprint");
    check(world.capsule_clear(20, 30, 0, 1.8) && !world.capsule_clear(.2, 30, 0, 1.8), "capsule needs the whole radius inside the footprint");
    check(near(CollisionWorld::slab_z(world.slabs()[3], 21, 12), 1.5, 1e-12), "ramp plane height at mid span");
    check(world.support(28, 12, 3, kStepHeight)->z == 3 && world.support(28, 12, 0, kStepHeight)->z == 0, "stacked floors: support depends on feet height");
    check(world.capsule_clear(28, 12, 0, 1.8) && world.capsule_clear(28, 12, 3, 1.8) && !world.capsule_clear(28, 12, 1.5, 1.8), "under, on, and inside a deck");
    check(!world.capsule_clear(6, 27, 0, 1.8) && world.capsule_clear(6, 27, 0, collider_height(kCrouchOffsetTarget)), "1.6 m gap: standing blocked, crouched fits");
    check(world.wall_hit(29.7, 28, 0, 1.8, kPlayerRadius) && !world.wall_hit(29.5, 28, 0, 1.8, kPlayerRadius), "wall contact distance radius + 0.15");
    check(!world.wall_hit(29.7, 28, 3.6, 1.8, kPlayerRadius), "capsule above a wall's top does not touch it");
    check(*world.ceiling_plane(28, 12, 1.8, 2.6) == 2.5 && !world.ceiling_plane(20, 30, 1.8, 5), "ceiling plane = slab underside");
    check(world.landing_plane(28, 12, 3.4, 2.9)->z == 3 && world.landing_plane(28, 12, 2.0, -.2)->z == 0, "landing picks the surface crossed");

    PlayerController pc(world);
    // ---- walk / sprint / crouch speeds on flat ground ----
    pc.spawn(2, 38, 0, 0);
    run(pc, key_forward, 240);
    check(near(speed(pc), kWalkSpeed, .02) && pc.state().ground && near(pc.state().y, 38, 1e-9) && pc.state().x > 15, "walk speed 7.6, got " + num(speed(pc)));
    check(pc.stance() == Stance::run && pc.view().footsteps > 3, "run stance with footsteps");
    pc.spawn(2, 38, 0, 0);
    run(pc, key_forward | key_sprint, 240);
    check(near(speed(pc), kWalkSpeed * kSprintMult, .03) && pc.stance() == Stance::sprint && pc.fov_scale() > 1.04, "sprint speed 11.4, got " + num(speed(pc)));
    pc.spawn(2, 38, 0, 0);
    run(pc, key_forward | key_crouch, 240);
    check(near(speed(pc), kWalkSpeed * kCrouchMult, .03) && pc.stance() == Stance::crouch, "crouch speed 6.08, got " + num(speed(pc)));
    check(near(pc.state().crouch_offset, kCrouchOffsetTarget, 1e-6) && near(pc.height(), 1.506, 1e-5), "crouch offset converges to 0.49");
    run(pc, 0, 240);
    check(speed(pc) == 0 && pc.state().crouch_offset < 1e-6, "friction stops the player; stands up again");
    // strafing diagonally is not faster
    pc.spawn(20, 1, 0, 0);
    run(pc, key_forward | key_right, 180);
    check(near(speed(pc), kWalkSpeed, .02), "diagonal input is normalized");
    // ---- jump ----
    pc.spawn(20, 30, 0, 0);
    double apex = 0; int air = 0;
    pc.step({key_jump, 0, 0});
    check(!pc.state().ground && pc.jumps() == 1, "jump leaves the ground");
    for(int i = 0; i < 200 && !pc.state().ground; ++i) { pc.step({0, 0, 0}); apex = std::max(apex, pc.state().z); ++air; }
    check(pc.state().ground && pc.state().z == 0 && pc.state().vz == 0, "lands back on the floor");
    check(near(apex, kJumpImpulse * kJumpImpulse / (2 * kGravity), .04), "jump apex ~1.626 m, got " + num(apex));
    check(near(air * kTick, 2 * kJumpImpulse / kGravity, .03), "air time ~0.85 s, got " + num(air * kTick));
    check(pc.contacts().landings == 1 && pc.view().landings == 1, "one landing recorded");
    // jump buffer: pressed 10 ticks (0.083 s) before landing still jumps; coyote: jump shortly after leaving a ledge
    pc.spawn(20, 30, 0, 0);
    pc.step({key_jump, 0, 0});
    for(int i = 0; i < 200 && (pc.state().vz > 0 || pc.state().z > .30); ++i) pc.step({0, 0, 0});
    check(!pc.state().ground, "still airborne when the jump key is pressed again");
    pc.step({key_jump, 0, 0}); pc.step({0, 0, 0});
    for(int i = 0; i < 30 && pc.jumps() < 2; ++i) pc.step({0, 0, 0});
    check(pc.jumps() == 2, "buffered jump fires on landing");
    pc.spawn(30.5, 12, 3, 0);
    for(int i = 0; i < 200 && pc.state().ground; ++i) pc.step({key_forward, 0, 0});
    check(!pc.state().ground && pc.state().x > 32, "walks off the deck edge");
    pc.step({key_forward | key_jump, 0, 0});
    check(pc.jumps() == 1 && pc.state().vz > 7, "coyote-time jump just after leaving the ledge");
    for(int i = 0; i < 400 && !pc.state().ground; ++i) pc.step({0, 0, 0});
    check(pc.state().ground && pc.state().z == 0, "falls to the lower floor");
    // ---- steps ----
    pc.spawn(10, 12, 0, 0);
    run(pc, key_forward, 120);
    check(pc.state().x > 14.5 && pc.contacts().steps == 1 && pc.contacts().landings == 1 && pc.state().z == 0,
          "0.40 m block: one step up, then a short fall off the far side (drops over 0.24 m are falls)");
    pc.spawn(10, 12, 0, 0);
    bool on_top = false;
    for(int i = 0; i < 60; ++i) { pc.step({key_forward, 0, 0}); on_top = on_top || near(pc.state().z, .40, 1e-9); }
    check(on_top, "feet rise to the step top");
    pc.spawn(10, 18, 0, 0);
    run(pc, key_forward, 180);
    check(pc.state().x < 12 && pc.state().x > 11.5 && pc.state().z == 0 && pc.contacts().horizontal > 0, "0.80 m block blocks, x = " + num(pc.state().x));
    // ---- slope ----
    pc.spawn(16, 12, 0, 0);
    double max_slope = 0, min_speed_on_ramp = 99;
    for(int i = 0; i < 400 && pc.state().x < 25; ++i) {
        pc.step({key_forward, 0, 0});
        const auto& s = pc.state();
        if(s.x > 18.5 && s.x < 23.5) {
            check(near(s.z, (s.x - 18) * .5, 1e-9) && s.ground, "feet follow the ramp plane");
            max_slope = std::max(max_slope, pc.slope_angle());
            if(s.x > 20) min_speed_on_ramp = std::min(min_speed_on_ramp, speed(pc));
        }
    }
    check(near(max_slope, std::atan2(.5 * .66, .66), 1e-6), "slope angle = atan(0.5)");
    check(pc.state().x >= 25 && near(pc.state().z, 3, 1e-9) && min_speed_on_ramp < kWalkSpeed * .9, "uphill is slower and ends on the deck");
    const double pi = 3.141592653589793;
    pc.spawn(23, 12, 2.5, pi);   // facing down the ramp
    double max_down = 0;
    for(int i = 0; i < 300 && pc.state().x > 17; ++i) { pc.step({key_forward, 0, 0}); if(pc.state().x > 18.5 && pc.state().x < 22.5) max_down = std::max(max_down, speed(pc)); }
    check(pc.state().z == 0 && pc.state().ground && max_down > kWalkSpeed * 1.02, "downhill is faster than flat, got " + num(max_down));
    // ---- ceiling ----
    pc.spawn(28, 12, 0, 0);   // under the deck: underside 2.5, standing head 1.8
    pc.step({key_jump, 0, 0});
    double top = 0; bool bumped = false;
    for(int i = 0; i < 200 && !pc.state().ground; ++i) { pc.step({0, 0, 0}); top = std::max(top, pc.state().z); bumped = bumped || pc.contacts().ceiling > 0; }
    check(bumped && near(top, 2.5 - 1.8 - .012, 1e-9), "head stops 12 mm under the deck underside, feet top = " + num(top));
    check(pc.state().ground && pc.state().z == 0, "falls back after the ceiling hit");
    pc.spawn(28, 12, 3, 0);   // on top of the same deck, open sky
    pc.step({key_jump, 0, 0});
    top = 0;
    for(int i = 0; i < 200 && !pc.state().ground; ++i) { pc.step({0, 0, 0}); top = std::max(top, pc.state().z); }
    check(top > 4.5 && pc.state().z == 3 && pc.contacts().ceiling == 0, "jump on the deck: full height, lands on the deck (not the floor below)");
    // low deck: standing is blocked at the edge, crouching passes underneath
    pc.spawn(2, 27, 0, 0);
    run(pc, key_forward, 240);
    check(pc.state().x < 4 && pc.state().x > 3.5, "standing player cannot enter a 1.6 m gap, x = " + num(pc.state().x));
    pc.spawn(2, 27, 0, 0);
    run(pc, key_crouch, 60);
    run(pc, key_forward | key_crouch, 300);
    check(pc.state().x > 8.5, "crouched player passes under the low deck, x = " + num(pc.state().x));
    // ---- wall slide ----
    pc.spawn(26, 28, 0, .5);   // 28.6 degrees into the wall at x = 30
    run(pc, key_forward, 110);
    check(pc.state().x < 30 - kPlayerRadius - .15 + 1e-6 && pc.state().x > 29.4 && pc.state().y > 29.5 && pc.state().y < 36, "slides along the wall: x " + num(pc.state().x) + " y " + num(pc.state().y));
    check(std::fabs(pc.state().vx) < .2 && pc.state().vy > 3, "velocity into the wall is removed, tangential kept");
    // ---- slide ----
    pc.spawn(2, 38, 0, 0);
    run(pc, key_forward | key_sprint, 120);
    const double before = speed(pc);
    pc.step({key_forward | key_sprint | key_slide, 0, 0});
    check(pc.timers().slide_time > 1.1 && speed(pc) > before * 1.05 && pc.stance() == Stance::slide, "slide starts with the entry boost");
    int slide_ticks = 1;
    while(pc.timers().slide_time > 0 && slide_ticks < 400) { pc.step({key_forward | key_sprint, 0, 0}); ++slide_ticks; }
    check(near(slide_ticks * kTick, kSlideDuration, .02), "slide lasts 1.12 s");
    pc.spawn(2, 38, 0, 0);
    run(pc, key_forward | key_sprint, 120);
    pc.step({key_forward | key_sprint | key_slide, 0, 0});
    pc.step({key_forward | key_sprint, 0, 0});
    pc.step({key_forward | key_sprint | key_jump, 0, 0});
    check(pc.timers().slide_time == 0 && !pc.state().ground, "jump cancels the slide");
    pc.step({key_forward | key_sprint | key_crouch, 0, 0});
    check(near(pc.timers().slide_buffer, kLandingSlideBuffer - kTick, 1e-12), "crouch pressed in the air buffers a landing slide");
    pc.spawn(20, 30, 0, 0);
    pc.step({key_slide, 0, 0});
    check(pc.timers().slide_time == 0, "no slide from standstill");
    // ---- jump pad ----
    pc.spawn(4, 6, 0, 0);
    run(pc, key_forward, 40);
    check(pc.pad_launches() == 1, "pad launches");
    top = 0; double top_x = 0;
    for(int i = 0; i < 400 && !pc.state().ground; ++i) { pc.step({0, 0, 0}); if(pc.state().z > top) { top = pc.state().z; top_x = pc.state().x; } }
    check(near(top, 5, .08) && near(top_x, 10, .6), "pad arc apex near (10, 6, 5): z " + num(top) + " x " + num(top_x));
    // ---- look ----
    pc.spawn(20, 30, 0, 0);
    pc.aim(100, 50);
    check(near(pc.state().a, .3, 1e-12) && near(pc.state().pitch, .15, 1e-12), "0.003 rad per mouse count");
    pc.aim(1000, 1000);
    check(near(pc.state().a, .3 + .36, 1e-12) && near(pc.state().pitch, .15 + .27, 1e-12), "per-event clamp 120 / 90 counts");
    for(int i = 0; i < 10; ++i) pc.aim(0, 90);
    check(pc.state().pitch == kPitchLimit, "pitch limit 1.1 rad");
    // ---- view ----
    pc.spawn(20, 30, 0, 0);
    auto cam = pc.view_camera();
    check(near(cam.z, kEyeHeight, 1e-12) && near(cam.forward[0], 1, 1e-12) && near(cam.right[1], 1, 1e-12) && near(cam.up[2], 1, 1e-12) && cam.roll == 0, "resting eye at 1.5 m, legacy basis");
    run(pc, key_forward, 100);
    double bob_min = 9, bob_max = -9;
    for(int i = 0; i < 120; ++i) { pc.step({key_forward, 0, 0}); const double d = pc.view_camera().z - pc.shot_origin().z; bob_min = std::min(bob_min, d); bob_max = std::max(bob_max, d); }
    check(bob_min >= 0 && bob_max > .02 && bob_max < .06, "view bob while running: 0.." + num(bob_max));
    const auto wm = pc.weapon_motion();
    check(std::isfinite(wm.pitch + wm.yaw + wm.roll + wm.drop), "weapon motion finite");
    pc.spawn(20, 20, 0, 0);
    run(pc, key_right, 60);
    check(pc.view_camera().roll < -.001, "strafing right rolls the view");
    // ---- determinism ----
    auto trace = [&](std::uint32_t seed) {
        PlayerController a(world);
        a.spawn(20, 30, 0, 0);
        std::uint32_t s = seed;
        std::vector<double> out;
        for(int i = 0; i < 3000; ++i) {
            s = s * 1664525U + 1013904223U;
            const std::uint32_t keys = (i / 37 % 5 == 0 ? 0U : (s >> 8) & 0xFFU) & ~((i % 90) < 80 ? 0U : static_cast<std::uint32_t>(key_jump));
            a.step({keys, ((s >> 20) & 15) - 7.5, 0});
            out.insert(out.end(), {a.state().x, a.state().y, a.state().z, a.state().vx, a.state().vy, a.state().vz});
            check(std::isfinite(a.state().x + a.state().y + a.state().z) && world.walkable(a.state().x, a.state().y), "random play stays finite and inside the map");
        }
        return out;
    };
    const auto t1 = trace(7), t2 = trace(7), t3 = trace(8);
    check(std::memcmp(t1.data(), t2.data(), t1.size() * sizeof(double)) == 0, "same input tape gives bit-identical trajectories");
    check(std::memcmp(t1.data(), t3.data(), t1.size() * sizeof(double)) != 0, "different input gives a different trajectory");
    std::cout << "PASS physics units: " << checks << " checks\n";
}

// ---- legacy trajectory replay ---------------------------------------------------------------------
struct Reader {
    std::vector<char> data;
    std::size_t at{};
    void need(std::size_t n) const { if(at + n > data.size()) throw std::runtime_error("trajectory fixture truncated"); }
    std::uint32_t u32() { need(4); std::uint32_t v; std::memcpy(&v, data.data() + at, 4); at += 4; return v; }
    std::vector<double> f64(std::size_t n) { need(n * 8); std::vector<double> v(n); if(n) std::memcpy(v.data(), data.data() + at, n * 8); at += n * 8; return v; }
    std::string text(std::size_t n) { need(n); std::string s(data.data() + at, n); at += n; return s; }
};
const char* const kRecord[30] = {"x", "y", "z", "vx", "vy", "vz", "ground", "crouchOffset", "yaw", "pitch", "slideTime", "slideCooldown", "slideBuffer", "jumpBuffer",
    "coyoteTime", "slopeAngle", "fovScale", "padCooldown", "camZ", "camForwardZ", "camRoll", "camUpZ", "bobCycle", "stepChange", "landChange",
    "weaponPitch", "weaponYaw", "weaponRoll", "weaponDrop", "colliderHeight"};

int replay(const std::string& assets_root, const std::string& fixture, const std::string& map_id) {
    const auto doc = assets::load_data(assets_root + "/maps/" + map_id + "/map.cadata");
    const CollisionWorld world(doc.at("compiled"));
    if(map_id == "helios-crucible")
        check(world.slabs().size() == 167 && world.walls().size() == 2479 && world.pads().size() == 2 && world.spawns().size() == 28, "Helios collision data counts");
    std::ifstream in(fixture, std::ios::binary);
    if(!in) throw std::runtime_error("cannot open " + fixture);
    Reader r;
    r.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if(r.text(8) != "CATTRAJ1" || r.u32() != 2) throw std::runtime_error("unsupported trajectory fixture");
    const auto scenarios = r.u32(), tape_width = r.u32(), record_width = r.u32();
    if(tape_width != 3 || record_width != 30) throw std::runtime_error("unexpected trajectory layout");
    // Tolerance: the reference is IEEE double arithmetic in another runtime (V8). Identical formulas
    // give identical bits except where the two math libraries round sin/cos/atan2 differently.
    const double tolerance = 1e-9;
    std::size_t total_ticks = 0, exact_ticks = 0, failures = 0;
    std::uint64_t hash = 1469598103934665603ULL;   // FNV-1a over every native value: equal across builds = bit-identical simulation
    double worst = 0;
    Contacts sum;
    for(std::uint32_t s = 0; s < scenarios; ++s) {
        const auto name = r.text(r.u32());
        const auto ticks = r.u32();
        const auto start = r.f64(8), contacts = r.f64(6), tape = r.f64(std::size_t(ticks) * 3), rec = r.f64(std::size_t(ticks) * 30);
        PlayerController pc(world);
        pc.spawn(start[0], start[1], start[2], start[3], start[4], start[5]);
        pc.state().vz = start[6]; pc.state().ground = start[7] != 0;
        double max_err = 0; int max_field = 0; std::size_t max_tick = 0, exact = 0, first_bad = ticks;
        for(std::uint32_t i = 0; i < ticks; ++i) {
            pc.step({static_cast<std::uint32_t>(tape[i * 3]), tape[i * 3 + 1], tape[i * 3 + 2]});
            const auto& p = pc.state(); const auto& t = pc.timers();
            const auto cam = pc.view_camera(); const auto wm = pc.weapon_motion();
            const double got[30] = {p.x, p.y, p.z, p.vx, p.vy, p.vz, p.ground ? 1.0 : 0.0, p.crouch_offset, p.a, p.pitch, t.slide_time, t.slide_cooldown, t.slide_buffer,
                t.jump_buffer, t.coyote_time, pc.slope_angle(), pc.fov_scale(), t.pad_cooldown, cam.z, cam.forward[2], cam.roll, cam.up[2], pc.view().bob_cycle,
                pc.view().step_change, pc.view().land_change, wm.pitch, wm.yaw, wm.roll, wm.drop, pc.height()};
            bool same = true;
            for(const double value : got) { unsigned char bytes[8]; std::memcpy(bytes, &value, 8); for(const auto b : bytes) { hash ^= b; hash *= 1099511628211ULL; } }
            for(int k = 0; k < 30; ++k) {
                const double e = std::fabs(got[k] - rec[std::size_t(i) * 30 + std::size_t(k)]);
                if(std::memcmp(&got[k], &rec[std::size_t(i) * 30 + std::size_t(k)], 8) != 0 && !(got[k] == 0 && rec[std::size_t(i) * 30 + std::size_t(k)] == 0)) same = false;
                if(e > max_err) { max_err = e; max_field = k; max_tick = i; }
                if(e > tolerance && first_bad == ticks) first_bad = i;
            }
            exact += same;
        }
        const auto& c = pc.contacts();
        const bool contacts_ok = static_cast<double>(c.horizontal) == contacts[0] && static_cast<double>(c.ceiling) == contacts[1] && static_cast<double>(c.landings) == contacts[2] &&
                                 static_cast<double>(c.steps) == contacts[3] && static_cast<double>(c.iterations) == contacts[4] && static_cast<double>(c.recoveries) == contacts[5];
        const bool ok = max_err <= tolerance && contacts_ok;
        failures += !ok;
        total_ticks += ticks; exact_ticks += exact; worst = std::max(worst, max_err);
        sum.horizontal += c.horizontal; sum.ceiling += c.ceiling; sum.landings += c.landings; sum.steps += c.steps; sum.iterations += c.iterations; sum.recoveries += c.recoveries;
        std::cout << (ok ? "PASS " : "FAIL ") << name << ": " << ticks << " ticks, bit-exact " << exact << ", max |error| " << max_err << " (" << kRecord[max_field] << " @ tick " << max_tick
                  << "), contacts h/c/l/s/r " << c.horizontal << '/' << c.ceiling << '/' << c.landings << '/' << c.steps << '/' << c.recoveries << (contacts_ok ? "" : " != legacy")
                  << (first_bad < ticks ? ", first tick over tolerance " + std::to_string(first_bad) : std::string()) << '\n';
    }
    if(r.at != r.data.size()) throw std::runtime_error("trailing bytes in trajectory fixture");
    std::cout << (failures ? "FAIL " : "PASS ") << "legacy trajectories " << map_id << ": " << scenarios << " scenarios, " << total_ticks << " ticks (" << static_cast<double>(total_ticks) / 120.0 << " s), bit-exact ticks " << exact_ticks
              << ", worst |error| " << worst << ", tolerance " << tolerance << "; contacts horizontal " << sum.horizontal << ", ceiling " << sum.ceiling << ", landings " << sum.landings
              << ", steps " << sum.steps << ", recoveries " << sum.recoveries << "; native state hash " << std::hex << hash << std::dec << '\n';
    return failures ? 1 : 0;
}
// ---- every baked map: the collision data loads, spawns are valid, random play stays inside ----------
// (No legacy reference for the other three maps yet: that is N4. This only proves the loader and the
// controller are not Helios-specific.)
int all_maps(const std::string& assets_root) {
    for(const char* id : {"helios-crucible", "bastion-keep", "reactor-bastion", "q3-gallery"}) {
        const auto doc = assets::load_data(assets_root + "/maps/" + id + "/map.cadata");
        const CollisionWorld world(doc.at("compiled"));
        std::size_t clear = 0, supported = 0;
        Contacts sum;
        std::uint32_t seed = 12345;
        for(const auto& sp : world.spawns()) {
            const bool ok = world.capsule_clear(sp.x, sp.y, sp.z, kPlayerHeight);
            const auto floor = world.support(sp.x, sp.y, sp.z, .12);
            clear += ok; supported += floor && std::fabs(floor->z - sp.z) < .05;
            if(!ok) continue;
            PlayerController pc(world);
            pc.spawn(sp.x, sp.y, sp.z, sp.a);
            for(int i = 0; i < 600; ++i) {
                if(i % 40 == 0) seed = seed * 1664525U + 1013904223U;
                pc.step({(seed >> 9) & 0x7FU, ((seed >> 20) & 7) - 3.5, 0});
                const auto& p = pc.state();
                check(std::isfinite(p.x + p.y + p.z + p.vx + p.vy + p.vz) && world.walkable(p.x, p.y) && p.z > -40 && p.z < 80, std::string(id) + ": player left the world from spawn " + sp.tag);
            }
            const auto& c = pc.contacts();
            sum.horizontal += c.horizontal; sum.landings += c.landings; sum.steps += c.steps; sum.recoveries += c.recoveries; sum.ceiling += c.ceiling;
        }
        check(!world.spawns().empty() && clear == world.spawns().size() && supported == world.spawns().size(),
              std::string(id) + ": " + std::to_string(clear) + " clear / " + std::to_string(supported) + " supported of " + std::to_string(world.spawns().size()) + " spawns");
        std::cout << "PASS " << id << ": " << world.slabs().size() << " slabs, " << world.walls().size() << " walls, " << world.pads().size() << " pads, " << world.spawns().size()
                  << " spawns all clear and supported; 5 s random play per spawn: contacts horizontal " << sum.horizontal << ", ceiling " << sum.ceiling << ", landings " << sum.landings
                  << ", steps " << sum.steps << ", recoveries " << sum.recoveries << '\n';
    }
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if(argc == 1) { unit_tests(); return 0; }
        if(argc == 3 && std::string(argv[1]) == "--maps") return all_maps(argv[2]);
        if(argc == 3) return replay(argv[1], argv[2], "helios-crucible");
        if(argc == 4) return replay(argv[1], argv[2], argv[3]);
        std::cerr << "usage: cat_arena_physics_tests [<assets/n1> <trajectories.bin> [map-id] | --maps <assets/n1>]\n";
        return 2;
    } catch(const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
