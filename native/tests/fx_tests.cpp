// N5 weapon presentation tests. GPU-free.
//   cat_arena_fx_tests                                                    unit tests
//   cat_arena_fx_tests <assets/n1> <combat fixture> <weapon fx fixture>   replay against the legacy game
// The fx fixture (tools/n5/legacy_combat.cjs --fx-out) holds, for every tick of the 29 combat tapes,
// what the unmodified legacy effect code produced: the viewmodel camera renderGun3D sends to the
// shader, the weapon glow, quad counts and checksums of the three effect batches, the dynamic
// lights, the recoil spring, the world-space muzzle point and the number of cosmetic random draws.
#include "cat/weapon_fx.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace cat;
namespace {
int checks = 0;
void check(bool value, const std::string& why) { ++checks; if(!value) throw std::runtime_error(why); }
bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }
struct Reader {
    std::vector<char> data;
    std::size_t at{};
    explicit Reader(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if(!in) throw std::runtime_error("cannot open " + path);
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    void need(std::size_t n) const { if(at + n > data.size()) throw std::runtime_error("fixture truncated"); }
    std::uint32_t u32() { need(4); std::uint32_t v; std::memcpy(&v, data.data() + at, 4); at += 4; return v; }
    std::vector<double> f64(std::size_t n) { need(n * 8); std::vector<double> v(n); if(n) std::memcpy(v.data(), data.data() + at, n * 8); at += n * 8; return v; }
    std::string text(std::size_t n) { need(n); std::string s(data.data() + at, n); at += n; return s; }
};
constexpr int kWidth = 43;
const char* const kRecord[kWidth] = {"gunCamX", "gunCamY", "gunCamZ", "gunFwdX", "gunFwdY", "gunFwdZ", "gunRightX", "gunRightY", "gunRightZ", "gunUpX", "gunUpY", "gunUpZ", "gunTanHalf", "gunAspect", "glowR", "glowG", "glowB",
    "cosmeticDraws", "mainQuads", "markQuads", "gunQuads", "mainPosSum", "mainAttrSum", "mainColourSum", "markPosSum", "markColourSum", "gunPosSum", "gunColourSum", "lights", "lightSum",
    "recoilBack", "recoilClimb", "recoilRoll", "recoilYaw", "muzzleX", "muzzleY", "muzzleZ", "mainKeySum", "gunVisible", "gunAttrSum",
    "uploadedLights", "uploadedPosSum", "uploadedColourSum"};
// Exact for counts; sums of float32 vertex data get a small allowance (a last-bit difference in a
// sin/cos result can round one float the other way).
double allowance(int k, double want) {
    const bool count = k == 17 || k == 18 || k == 19 || k == 20 || k == 28 || k == 38 || k == 40;
    const bool sum = (k >= 21 && k <= 27) || k == 29 || k == 37 || k == 39 || k == 41 || k == 42;
    return count ? 0 : sum ? 2e-4 + 1e-6 * std::fabs(want) : 1e-9;
}
void sums(const fx::Batch& b, double& pos, double* attr, double& colour) {
    for(const auto& v : b.vertices) {
        pos += static_cast<double>(v.x) + static_cast<double>(v.y) + static_cast<double>(v.z);
        if(attr) *attr += static_cast<double>(v.u) + static_cast<double>(v.v) + static_cast<double>(v.tile) + static_cast<double>(v.flags);
        colour += static_cast<double>(v.r) + static_cast<double>(v.g) + static_cast<double>(v.b) + static_cast<double>(v.a) + static_cast<double>(v.add);
    }
}
void snapshot(const fx::WeaponFx& w, const fx::Frame& f, double* o) {
    for(int i = 0; i < kWidth; ++i) o[i] = 0;
    const auto& g = f.gun_view;
    if(g.visible) {
        o[0] = g.camera.x; o[1] = g.camera.y; o[2] = g.camera.z;
        for(std::size_t k = 0; k < 3; ++k) { o[3 + k] = g.camera.forward[k]; o[6 + k] = g.camera.right[k]; o[9 + k] = g.camera.up[k]; o[14 + k] = g.glow[k]; }
        o[12] = g.camera.tan_half; o[13] = g.camera.aspect; o[38] = 1;
    }
    o[17] = static_cast<double>(w.rng().calls());
    o[18] = static_cast<double>(f.main.quads()); o[19] = static_cast<double>(f.mark.quads()); o[20] = static_cast<double>(f.gun.quads());
    sums(f.main, o[21], &o[22], o[23]); sums(f.mark, o[24], nullptr, o[25]); sums(f.gun, o[26], &o[39], o[27]);
    o[28] = static_cast<double>(f.lights.size());
    for(const auto& l : f.lights) o[29] += l.x + l.y + l.z + l.radius + l.r + l.g + l.b;
    const auto r = w.recoil();
    o[30] = r[0]; o[31] = r[1]; o[32] = r[2]; o[33] = r[3];
    for(const float key : f.main.keys) o[37] += static_cast<double>(key);
    // N5.1: the lights the renderer receives (Q3R.uploadLights: best 16 for this view, as float32)
    const auto best = fx::select_lights(f.lights, f.camera.x, f.camera.y, f.camera.z);
    o[40] = static_cast<double>(best.size());
    for(const auto& l : best) {
        o[41] += static_cast<double>(static_cast<float>(l.x)) + static_cast<double>(static_cast<float>(l.y)) + static_cast<double>(static_cast<float>(l.z)) + static_cast<double>(static_cast<float>(l.radius));
        o[42] += static_cast<double>(static_cast<float>(l.r)) + static_cast<double>(static_cast<float>(l.g)) + static_cast<double>(static_cast<float>(l.b));
    }
}

int replay(const std::string& assets_root, const std::string& combat_fixture, const std::string& fx_fixture) {
    const auto doc = assets::load_data(assets_root + "/maps/helios-crucible/map.cadata");
    const physics::CollisionWorld world(doc.at("compiled"));
    Reader c(combat_fixture), r(fx_fixture);
    if(c.text(8) != "CATCMBT1" || c.u32() != 1) throw std::runtime_error("unsupported combat fixture");
    const auto scenarios = c.u32();
    if(c.u32() != 3 || c.u32() != 48) throw std::runtime_error("unexpected combat fixture layout");
    if(r.text(8) != "CATWFX01" || r.u32() != 2 || r.u32() != scenarios || r.u32() != kWidth) throw std::runtime_error("unsupported weapon fx fixture");
    const auto view = r.f64(2);
    const double fov = view[0], aspect = view[1];
    std::size_t total_ticks = 0, exact_ticks = 0, failures = 0, quads = 0, max_main = 0, lights = 0, uploaded = 0;
    std::uint64_t draws = 0, hash = 1469598103934665603ULL;
    double worst_camera = 0, worst_sum = 0;
    for(std::uint32_t s = 0; s < scenarios; ++s) {
        const auto name = c.text(c.u32());
        const auto ticks = c.u32();
        const auto start = c.f64(12);
        const auto target_count = static_cast<std::size_t>(start[10]);
        const auto targets = c.f64(target_count * 5), tape = c.f64(std::size_t(ticks) * 3);
        c.f64(std::size_t(ticks) * 48);
        if(r.text(r.u32()) != name || r.u32() != ticks) throw std::runtime_error("fixtures do not describe the same scenarios");
        const auto cosmetic_seed = r.f64(1)[0];
        const auto rec = r.f64(std::size_t(ticks) * kWidth);
        combat::Arena arena(world, static_cast<std::uint32_t>(static_cast<std::int32_t>(start[9])));
        arena.spawn(start[0], start[1], start[2], start[3], start[4], start[5]);
        arena.set_health(start[6]);
        arena.set_loadout(static_cast<int>(start[7]), static_cast<int>(start[8]));
        for(std::size_t t = 0; t < target_count; ++t)
            arena.add_target(targets[t * 5], targets[t * 5 + 1], targets[t * 5 + 2], static_cast<combat::TargetClass>(static_cast<int>(targets[t * 5 + 3])), targets[t * 5 + 4]);
        fx::WeaponFx effects(world, static_cast<std::uint32_t>(static_cast<std::int32_t>(cosmetic_seed)));
        fx::Frame frame;
        std::size_t exact = 0, first_bad = ticks, sc_quads = 0;
        double cam_err = 0, sum_err = 0;
        for(std::uint32_t i = 0; i < ticks; ++i) {
            arena.step({static_cast<std::uint32_t>(tape[i * 3]), tape[i * 3 + 1], tape[i * 3 + 2]});
            effects.tick(arena, fov, aspect);
            effects.build(arena, fov, aspect, frame);
            double got[kWidth];
            snapshot(effects, frame, got);
            const auto m = effects.muzzle_world(frame.camera, arena.current_weapon());
            got[34] = m[0]; got[35] = m[1]; got[36] = m[2];
            for(const double value : got) { unsigned char bytes[8]; std::memcpy(bytes, &value, 8); for(const auto b : bytes) { hash ^= b; hash *= 1099511628211ULL; } }
            bool same = true;
            for(int k = 0; k < kWidth; ++k) {
                const double want = rec[std::size_t(i) * kWidth + std::size_t(k)], err = std::fabs(got[k] - want), allowed = allowance(k, want);
                if(std::memcmp(&got[k], &want, 8) != 0 && !(got[k] == 0 && want == 0)) same = false;
                if(allowed > 1e-8) sum_err = std::max(sum_err, err); else cam_err = std::max(cam_err, err);
                if(err > allowed && first_bad == ticks) { first_bad = i; std::cout << "  first difference in " << name << " @ tick " << i << ": " << kRecord[k] << " native " << got[k] << ", legacy " << want << '\n'; }
            }
            exact += same;
            sc_quads += frame.main.quads() + frame.mark.quads() + frame.gun.quads();
            max_main = std::max(max_main, frame.main.quads()); lights += frame.lights.size(); uploaded += static_cast<std::size_t>(got[40]);
        }
        const bool ok = first_bad == ticks;
        failures += !ok;
        total_ticks += ticks; exact_ticks += exact; quads += sc_quads; draws += effects.rng().calls();
        worst_camera = std::max(worst_camera, cam_err); worst_sum = std::max(worst_sum, sum_err);
        std::cout << (ok ? "PASS " : "FAIL ") << name << ": " << ticks << " ticks, bit-exact " << exact << ", worst camera/recoil error " << cam_err << ", worst checksum error " << sum_err << ", quads " << sc_quads
                  << ", cosmetic draws " << effects.rng().calls() << '\n';
    }
    if(c.at != c.data.size() || r.at != r.data.size()) throw std::runtime_error("trailing bytes in a fixture");
    std::cout << (failures ? "FAIL " : "PASS ") << "legacy weapon effects: " << scenarios << " scenarios, " << total_ticks << " ticks, bit-exact ticks " << exact_ticks << "; viewmodel camera / glow / recoil / muzzle worst |error| "
              << worst_camera << " (tolerance 1e-09); batch checksums worst |error| " << worst_sum << "; quad counts, light counts and " << draws << " cosmetic random draws identical; " << quads << " quads built, largest main batch "
              << max_main << ", " << lights << " lights requested, " << uploaded << " uploaded to the shaders (selection equal to the legacy); native state hash " << std::hex << hash << std::dec << '\n';
    return failures ? 1 : 0;
}
// ---- synthetic world: 40 x 40 m room, floor z = 0, wall at x = 30 ---------------------------------
using V = assets::Value;
V arr_of(std::vector<V> v) { return V(V::Array(std::move(v))); }
template<class... T> V arr(T&&... items) { V::Array a; a.reserve(sizeof...(items)); (a.push_back(V(std::forward<T>(items))), ...); return V(std::move(a)); }
V rect(double x0, double y0, double x1, double y1) { return arr(arr(x0, y0), arr(x1, y0), arr(x1, y1), arr(x0, y1)); }
V synthetic_world() {
    V::Object floor;
    floor["id"] = V(std::string("floor")); floor["kind"] = V(std::string("floor")); floor["poly"] = rect(0, 0, 40, 40); floor["z"] = V(0.0); floor["thickness"] = V(.5);
    floor["nav"] = V(true); floor["plane"] = V(nullptr); floor["base"] = V(nullptr);
    std::vector<V> slab_grid, empty_grid;
    for(int i = 0; i < 100; ++i) { slab_grid.push_back(arr(0.0)); empty_grid.push_back(arr()); }
    V::Object wall;
    wall["ax"] = V(30.0); wall["ay"] = V(4.0); wall["bx"] = V(30.0); wall["by"] = V(36.0); wall["z0"] = V(0.0); wall["z1"] = V(4.0);
    V::Object spawn;
    spawn["x"] = V(20.0); spawn["y"] = V(30.0); spawn["z"] = V(0.0); spawn["a"] = V(0.0); spawn["tag"] = V(std::string("test"));
    V::Object c;
    c["cell"] = V(4.0); c["gw"] = V(10.0);
    c["slabs"] = arr(V(std::move(floor))); c["walls"] = arr(V(std::move(wall)));
    c["solids"] = arr(); c["cavities"] = arr(); c["footprint"] = arr(rect(0, 0, 40, 40));
    c["slabGrid"] = arr_of(std::move(slab_grid)); c["solidGrid"] = arr_of(empty_grid); c["cavityGrid"] = arr_of(empty_grid);
    c["pads"] = arr(); c["spawns"] = arr(V(std::move(spawn)));
    return V(std::move(c));
}
double dot(const std::array<double, 3>& a, const std::array<double, 3>& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
std::uint64_t arena_hash(const combat::Arena& a) {
    const auto& p = a.player().state();
    const double v[] = {p.x, p.y, p.z, p.vx, p.vy, p.vz, a.health(), double(a.ammo(0)), double(a.ammo(1)), double(a.weapon(0)), double(a.weapon(1)), a.cooldown_until(), a.reloading(), double(a.rng().calls()),
                        double(a.frags()), double(a.targets().size()), a.targets().empty() ? 0 : a.targets()[0].hp};
    std::uint64_t h = 1469598103934665603ULL;
    for(const double value : v) { unsigned char bytes[8]; std::memcpy(bytes, &value, 8); for(const auto b : bytes) { h ^= b; h *= 1099511628211ULL; } }
    return h;
}

void unit_tests() {
    const V data = synthetic_world();
    const physics::CollisionWorld world(data);
    const double aspect = 16.0 / 9.0, fov = 105;
    // ---- generated tables (from the legacy source) --------------------------------------------------
    {
        check(fx::kFxDef.size() == 16 && fx::tile_glow == 0 && fx::tile_explo0 == 8 && fx::tile_energy_cloud == 63 && fx::tile_tracer == 57, "effect tables: 16 weapon rows, 64 atlas tiles");
        for(std::size_t i = 0; i < 16; ++i) {
            const auto& d = fx::kFxDef[i];
            check(d.flash_life > 0 && d.flash_size > 0 && d.recoil_k > 0 && d.light[3] > 0 && fx::kMuzzleHeight[i] >= .5 && fx::kMuzzleHeight[i] <= .95 && std::string(fx::kGunFile[i]).size() > 3, "effect row " + std::to_string(i));
            const bool projectile = combat::weapons()[i].kind != combat::FireKind::hitscan;
            check((d.impact == fx::ImpactKind::explosion) == projectile, "explosive weapons have the explosion impact, hitscan weapons a tracer/beam: " + std::to_string(i));
        }
        check(fx::kFxDef[3].kind == fx::ShotKind::rail && fx::kFxDef[6].kind == fx::ShotKind::bolt && fx::kFxDef[1].brass == fx::BrassKind::shell && fx::kFxDef[11].brass == fx::BrassKind::brass, "rail, bolt and brass rows");
    }
    // ---- quad writers ---------------------------------------------------------------------------------
    {
        const fx::Camera cam{0, 0, 0, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 1, 1};
        fx::Batch b{2, true, {}, {}, 0};
        check(fx::sprite(b, cam, 10, 0, 0, 2, 4, 0, 5, 1, .5, .25, .8, 1, 4) && b.quads() == 1, "sprite adds one quad");
        const auto& v = b.vertices;
        check(v[0].x == 10 && v[0].y == -1 && v[0].z == -2 && v[2].y == 1 && v[2].z == 2 && v[0].u == 0 && v[0].v == 1 && v[2].u == 1 && v[2].v == 0 && v[0].tile == 5 && v[0].flags == 4 && v[1].a == .8F, "sprite corners face the camera");
        check(b.keys.size() == 1 && b.keys[0] == 100, "sort key = squared distance of the centre");
        check(fx::sprite(b, cam, 3, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1) && !fx::sprite(b, cam, 4, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1) && b.dropped == 1, "a full batch drops quads");
        const auto idx = b.indices();
        check(idx.size() == 12 && idx[0] == 0 && idx[5] == 3 && idx[6] == 4, "sorted batch: far quad first");
        fx::Batch u{8, false, {}, {}, 0};
        check(!fx::beam(u, cam, 5, 0, 0, 5, 0, 0, 1, 0, 1, 1, 1, 1) && fx::beam(u, cam, 5, 0, 0, 9, 0, 3, .5, 2, 1, 1, 1, 1), "zero-length beam is skipped");
        check(near(u.vertices[0].y, .25, 1e-6) || near(u.vertices[0].y, -.25, 1e-6), "beam is half a width wide on each side, perpendicular to the view");
        check(near(u.vertices[0].x, 5, 1e-6) && near(u.vertices[1].x, 9, 1e-6) && u.vertices[0].u == 0 && u.vertices[1].u == 1, "beam runs from a to b");
        const auto m = fx::WeaponFx::muzzle_world_at(fx::make_camera({0, 0, 1.5, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 0}, fov, 1, aspect), 0);
        check(near(m[0], .62, 1e-12) && m[1] > 0 && m[2] < 1.5, "the muzzle is 0.62 m ahead, right of and below the eye");
    }
    // ---- viewmodel: muzzle anchored on screen, raise on switch, reload dip, recoil ----------------------
    {
        combat::Arena arena(world, 3);
        arena.spawn(5, 20, 0, 0);
        fx::Frame frame;
        for(int gun = 0; gun < 16; ++gun) {
            arena.set_loadout(gun, (gun + 1) % 16);
            fx::WeaponFx w(world, 9);
            w.build(arena, fov, aspect, frame);
            const auto& c = frame.gun_view.camera;
            check(frame.gun_view.visible && frame.gun_view.weapon == gun, "weapon view");
            check(near(dot(c.forward, c.forward), 1, 1e-12) && near(dot(c.right, c.right), 1, 1e-12) && near(dot(c.up, c.up), 1, 1e-12) && near(dot(c.forward, c.right), 0, 1e-12) && near(dot(c.forward, c.up), 0, 1e-12), "viewmodel camera basis is orthonormal");
            const auto& mz = fx::kGunMuzzle[static_cast<std::size_t>(gun)];
            const std::array<double, 3> r{mz[0] - c.x, mz[1] - c.y, mz[2] - c.z};
            const double depth = dot(r, c.forward), sx = dot(r, c.right) / (c.tan_half * c.aspect * depth), sy = dot(r, c.up) / (c.tan_half * depth);
            check(near(sx, 2 * fx::kRefMuzzleX - 1, 1e-9) && near(sy, 1 - 2 * fx::kMuzzleHeight[static_cast<std::size_t>(gun)], 1e-9), "muzzle of weapon " + std::to_string(gun) + " sits at the reference screen position");
            check(near(c.tan_half, std::tan(30 * 3.141592653589793 / 180) / aspect, 1e-12), "viewmodel has its own 60 degree field of view");
        }
        fx::WeaponFx w(world, 9);
        arena.set_loadout(0, 3);
        auto rest = w.viewmodel_offsets(0, 0);
        check(rest.drop == 0 && rest.pitch == 0 && rest.back == 0 && rest.roll == 0, "at rest: no offset; the first weapon does not play the raise animation");
        auto raised = w.viewmodel_offsets(3, 0);
        check(near(raised.drop, 1.25, 1e-12) && near(raised.pitch, -.55, 1e-12), "switching drops the new weapon 1.25 below and tilts it");
        for(int i = 0; i < 36; ++i) w.tick(arena, fov, aspect);
        check(near(w.viewmodel_offsets(3, 0).drop, 0, 1e-9), "raised after 0.3 s");
        const auto dip = w.viewmodel_offsets(3, combat::weapons()[3].reload * .5);
        check(near(dip.drop, .42, 1e-9) && near(dip.roll, .5, 1e-9) && near(dip.pitch, -.28, 1e-9), "reload dip is deepest half way");
        // recoil: the spring kicks back on a shot and settles; presentation never touches the simulation
        combat::Arena a(world, 5), b(world, 5);
        a.spawn(5, 20, 0, 0); b.spawn(5, 20, 0, 0);
        a.set_loadout(12, 0); b.set_loadout(12, 0);
        a.add_target(11, 20, 0, combat::TargetClass::standard, 5000); b.add_target(11, 20, 0, combat::TargetClass::standard, 5000);
        fx::WeaponFx fa(world, 1);
        double peak = 0;
        for(int i = 0; i < 400; ++i) {
            const physics::TickInput in{i < 300 ? static_cast<std::uint32_t>(combat::key_fire) : 0U, i % 50 == 0 ? 4.0 : 0.0, 0};
            a.step(in); b.step(in);
            fa.tick(a, fov, aspect); fa.build(a, fov, aspect, frame);
            peak = std::max(peak, fa.recoil()[0]);
            check(arena_hash(a) == arena_hash(b), "effects do not change the simulation (tick " + std::to_string(i) + ")");
            if(i == 0) check(frame.gun.quads() == 3 && frame.gun_view.glow[0] > 0 && fa.tracers() == 1, "a shot: muzzle flash (glow, star, side plume), weapon glow, one tracer");
        }
        check(peak > .05 && peak < .5 && std::fabs(fa.recoil()[0]) < 1e-3 && frame.gun.quads() == 0 && frame.gun_view.glow[0] == 0, "hand cannon recoil peaks (" + std::to_string(peak) + ") and settles; the flash is gone");
        check(fa.marks() > 0 && fa.rng().calls() > 0 && a.rng().calls() == b.rng().calls(), "impact marks exist; cosmetic draws come from the effect stream only");
    }
    // ---- medkit billboard (legacy renderGeometry / billboard) -----------------------------------------
    {
        std::vector<float> v;
        fx::billboard(v, 10, 20, 1, .65, .65, fx::kAtlasMedkit, 0);
        check(v.size() == 72, "one billboard = 6 vertices of 12 floats");
        // yaw 0 looks along +x: the quad spans y (the view's right axis), bottom-left first
        check(v[0] == 10 && near(v[1], 20 - .325, 1e-6) && v[2] == 1 && v[3] == 0 && v[5] == 1 && v[6] == 1 && v[9] == 0 && v[10] == 0 && v[11] == 11, "first vertex: bottom, uv (0,0), up normal, medkit layer");
        check(near(v[12 + 1], 20 + .325, 1e-6) && v[12 + 9] == 1 && near(v[24 + 2], 1.65, 1e-6) && v[24 + 9] == 1 && v[24 + 10] == 1 && near(v[60 + 1], 20 - .325, 1e-6) && v[60 + 10] == 1, "0.65 m wide and tall, uv corners");
        std::vector<float> turned;
        fx::billboard(turned, 10, 20, 1, .65, .65, fx::kAtlasMedkit, 1.5707963267948966);
        check(near(turned[0], 10 + .325, 1e-6) && near(turned[1], 20, 1e-6), "the billboard turns with the view yaw");
        combat::Arena arena(world, 3);
        arena.spawn(5, 20, 0, 0);
        fx::WeaponFx w(world, 1);
        fx::Frame frame;
        w.build(arena, fov, aspect, frame);
        check(frame.atlas.size() == arena.pickups().size() * 72, "one billboard per available medkit");
    }
    // ---- dynamic light selection (Q3R.uploadLights) -------------------------------------------------
    {
        std::vector<fx::Light> lights;
        check(fx::select_lights(lights, 0, 0, 0).empty(), "no lights");
        for(int i = 0; i < 40; ++i) lights.push_back({double(i), 0, 0, 4, 1, 1, 1});   // equal lights, further and further away
        lights.push_back({200, 0, 0, 400, 1, 1, 1});                                    // far but huge
        const auto best = fx::select_lights(lights, 0, 0, 0);
        check(best.size() == 16, "at most 16 lights reach the shaders");
        // scores: 12 / (1 + 0.15 i) for the small lights, 1200 / 31 = 38.7 for the big one
        check(best[0].x == 200 && best[1].x == 0 && best[2].x == 1 && best[15].x == 14, "ordered by radius x brightness / (1 + 0.15 distance): the big light first, then the nearest");
        const auto elsewhere = fx::select_lights(lights, 39, 0, 0);
        check(elsewhere[0].x == 200 && elsewhere[1].x == 39 && elsewhere[2].x == 38, "the selection depends on the viewer");
        std::vector<fx::Light> ties{{1, 0, 0, 3, 1, 0, 0}, {-1, 0, 0, 3, 0, 1, 0}, {0, 1, 0, 3, 0, 0, 1}};
        const auto kept = fx::select_lights(ties, 0, 0, 0);
        check(kept[0].r == 1 && kept[1].g == 1 && kept[2].b == 1, "equal scores keep their order (stable sort)");
    }
    // ---- explosions and limits --------------------------------------------------------------------------
    {
        fx::WeaponFx w(world, 4);
        combat::Arena arena(world, 3);
        arena.spawn(5, 20, 0, 0);
        w.explosion(20, 20, .3, 1.15, false);
        check(w.entities() > 30 && w.marks() == 1, "rocket blast: fireball frames, ring, smoke, sparks, debris and a scorch mark on the floor (" + std::to_string(w.entities()) + " entities)");
        fx::Frame frame;
        w.build(arena, fov, aspect, frame);
        check(frame.main.quads() >= 20 && frame.mark.quads() == 1 && frame.lights.size() == 1 && near(frame.lights[0].radius, 9.5 * 1.15, 1e-9), "blast quads, one mark, one dynamic light");
        const auto before = w.entities();
        w.explosion(20, 20, 3.5, 1.42, true);   // rift blast in mid air next to no wall: no mark
        check(w.entities() > before && w.marks() == 1, "rift blast in mid air leaves no mark");
        w.explosion(29.6, 20, 2.5, 1.15, false);
        check(w.marks() == 2, "a blast beside a wall scorches the wall");
        for(int i = 0; i < 60; ++i) w.explosion(10 + i * .2, 10, .3, 1.15, false);
        check(w.entities() == 420 && w.marks() <= 200, "entity pool is capped at 420");
        for(int i = 0; i < 20 * 120; ++i) w.tick(arena, fov, aspect);
        check(w.entities() == 0 && w.marks() == 0, "all effects expire");
        const auto s = w.surface_at(29.9, 20, 1.5, 1, 0, 0);
        check(s && near(s->nx, -1, 1e-6) && near(s->ny, 0, 1e-6) && s->x > 29.8 && s->x < 30, "surface_at finds the wall and its normal");
        const auto f = w.surface_at(10, 10, .02, 0, 0, -1);
        check(f && near(f->nz, 1, 1e-9) && near(f->z, 0, .02), "surface_at finds the floor");
        check(!w.surface_at(10, 10, 5, 1, 0, 0), "no surface in mid air");
    }
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if(argc == 1) { unit_tests(); std::cout << "PASS weapon fx unit tests: " << checks << " checks\n"; return 0; }
        if(argc == 4) return replay(argv[1], argv[2], argv[3]);
        std::cerr << "usage: cat_arena_fx_tests [<assets/n1> <combat fixture> <weapon fx fixture>]\n";
        return 2;
    } catch(const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
