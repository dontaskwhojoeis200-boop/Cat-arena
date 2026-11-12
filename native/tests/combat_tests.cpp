// N5 weapon, projectile, damage and pickup tests. GPU-free.
//   cat_arena_combat_tests                                  unit tests on a small synthetic world
//   cat_arena_combat_tests <assets/n1> <combat fixture>     replay of tapes recorded from the legacy game
// The fixture (tools/n5/legacy_combat.cjs) holds, for every 1/120 s tick of 29 scripted scenarios on
// Helios Crucible, 48 values of the unmodified legacy game: player state, loadout, magazines, timers,
// number of random draws, projectile counts and positions, hitscan end points, detonations, target
// health, frags and pickup cooldowns. The native Arena must reproduce all of them.
#include "cat/combat.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using namespace cat;
using namespace cat::combat;
namespace {
int checks = 0;
void check(bool value, const std::string& why) { ++checks; if(!value) throw std::runtime_error(why); }
bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }
std::string num(double v) { return std::to_string(v); }
struct Reader {
    std::vector<char> data;
    std::size_t at{};
    void need(std::size_t n) const { if(at + n > data.size()) throw std::runtime_error("combat fixture truncated"); }
    std::uint32_t u32() { need(4); std::uint32_t v; std::memcpy(&v, data.data() + at, 4); at += 4; return v; }
    std::vector<double> f64(std::size_t n) { need(n * 8); std::vector<double> v(n); if(n) std::memcpy(v.data(), data.data() + at, n * 8); at += n * 8; return v; }
    std::string text(std::size_t n) { need(n); std::string s(data.data() + at, n); at += n; return s; }
};
constexpr int kWidth = 48;
const char* const kRecord[kWidth] = {"x", "y", "z", "vx", "vy", "vz", "ground", "hp", "slot", "weapon0", "weapon1", "mag0", "mag1", "cooldown", "reloading", "gunKick", "muzzleFlash", "rngCalls",
    "rockets", "grenades", "bolts", "projSumX", "projSumY", "projSumZ", "shots", "shotHits", "enemyHits", "shotEndSumX", "shotEndSumY", "shotEndSumZ",
    "explosions", "explosionX", "explosionY", "explosionZ", "explosionPower", "targetHp0", "targetHp1", "targetHp2", "targetHp3", "targetHp4", "targetHp5", "targetHp6", "targetHp7",
    "targets", "frags", "pickupCooldownSum", "yaw", "pitch"};

// The 48 values of one tick, in the fixture's layout.
void snapshot(const Arena& a, double* o) {
    for(int i = 0; i < kWidth; ++i) o[i] = 0;
    const auto& p = a.player().state();
    o[0] = p.x; o[1] = p.y; o[2] = p.z; o[3] = p.vx; o[4] = p.vy; o[5] = p.vz; o[6] = p.ground ? 1 : 0; o[7] = a.health();
    o[8] = a.slot(); o[9] = a.weapon(0); o[10] = a.weapon(1); o[11] = a.ammo(0); o[12] = a.ammo(1); o[13] = a.cooldown_until(); o[14] = a.reloading(); o[15] = a.gun_kick(); o[16] = a.muzzle_flash();
    o[17] = static_cast<double>(a.rng().calls());
    for(const auto& q : a.projectiles()) {
        o[q.kind == FireKind::rocket ? 18 : q.kind == FireKind::grenade ? 19 : 20] += 1;
        o[21] += q.x; o[22] += q.y; o[23] += q.z;
    }
    const auto& e = a.events();
    o[24] = static_cast<double>(e.shots.size());
    for(const auto& s : e.shots) { if(s.hit) o[25] += 1; if(s.target) o[26] += 1; o[27] += s.x; o[28] += s.y; o[29] += s.z; }
    o[30] = static_cast<double>(e.blasts.size());
    if(!e.blasts.empty()) { const auto& b = e.blasts.back(); o[31] = b.x; o[32] = b.y; o[33] = b.fx_z(); o[34] = b.power; }
    for(std::size_t k = 0; k < 8 && k < a.targets().size(); ++k) o[35 + k] = a.targets()[k].hp;
    o[43] = static_cast<double>(a.targets().size()); o[44] = static_cast<double>(a.frags());
    for(const auto& q : a.pickups()) o[45] += std::max(0.0, q.cool);
    o[46] = p.a; o[47] = p.pitch;
}

int replay(const std::string& assets_root, const std::string& fixture) {
    const auto doc = assets::load_data(assets_root + "/maps/helios-crucible/map.cadata");
    const physics::CollisionWorld world(doc.at("compiled"));
    std::ifstream in(fixture, std::ios::binary);
    if(!in) throw std::runtime_error("cannot open " + fixture);
    Reader r;
    r.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if(r.text(8) != "CATCMBT1" || r.u32() != 1) throw std::runtime_error("unsupported combat fixture");
    const auto scenarios = r.u32(), tape_width = r.u32(), record_width = r.u32();
    if(tape_width != 3 || record_width != kWidth) throw std::runtime_error("unexpected combat fixture layout");
    // Tolerance as in the N3 replay: both sides are IEEE doubles; only libm rounding of sin/cos may differ.
    const double tolerance = 1e-9;
    std::size_t total_ticks = 0, exact_ticks = 0, failures = 0;
    std::uint64_t hash = 1469598103934665603ULL, shots = 0, shot_hits = 0, target_hits = 0, blasts = 0, frags = 0, draws = 0;
    double worst = 0;
    bool weapon_fired[kWeaponCount]{};
    for(std::uint32_t s = 0; s < scenarios; ++s) {
        const auto name = r.text(r.u32());
        const auto ticks = r.u32();
        const auto start = r.f64(12);
        const auto target_count = static_cast<std::size_t>(start[10]);
        const auto targets = r.f64(target_count * 5), tape = r.f64(std::size_t(ticks) * 3), rec = r.f64(std::size_t(ticks) * kWidth);
        Arena arena(world, static_cast<std::uint32_t>(static_cast<std::int32_t>(start[9])));
        arena.spawn(start[0], start[1], start[2], start[3], start[4], start[5]);
        arena.set_health(start[6]);
        arena.set_loadout(static_cast<int>(start[7]), static_cast<int>(start[8]));
        for(std::size_t t = 0; t < target_count; ++t)
            arena.add_target(targets[t * 5], targets[t * 5 + 1], targets[t * 5 + 2], static_cast<TargetClass>(static_cast<int>(targets[t * 5 + 3])), targets[t * 5 + 4]);
        double max_err = 0; int max_field = 0; std::size_t max_tick = 0, exact = 0, first_bad = ticks;
        std::uint64_t sc_shots = 0, sc_blasts = 0;
        for(std::uint32_t i = 0; i < ticks; ++i) {
            arena.step({static_cast<std::uint32_t>(tape[i * 3]), tape[i * 3 + 1], tape[i * 3 + 2]});
            double got[kWidth];
            snapshot(arena, got);
            const auto& e = arena.events();
            if(e.fired_weapon >= 0) weapon_fired[e.fired_weapon] = true;
            sc_shots += e.shots.size(); sc_blasts += e.blasts.size();
            for(const auto& sh : e.shots) { shot_hits += sh.hit; target_hits += sh.target; }
            bool same = true;
            for(const double value : got) { unsigned char bytes[8]; std::memcpy(bytes, &value, 8); for(const auto b : bytes) { hash ^= b; hash *= 1099511628211ULL; } }
            for(int k = 0; k < kWidth; ++k) {
                const double want = rec[std::size_t(i) * kWidth + std::size_t(k)];
                const double err = std::fabs(got[k] - want);
                if(std::memcmp(&got[k], &want, 8) != 0 && !(got[k] == 0 && want == 0)) same = false;
                if(err > max_err) { max_err = err; max_field = k; max_tick = i; }
                if(err > tolerance && first_bad == ticks) { first_bad = i; std::cout << "  first difference in " << name << " @ tick " << i << ": " << kRecord[k] << " native " << got[k] << ", legacy " << want << '\n'; }
            }
            exact += same;
        }
        const bool ok = max_err <= tolerance;
        failures += !ok;
        total_ticks += ticks; exact_ticks += exact; worst = std::max(worst, max_err);
        shots += sc_shots; blasts += sc_blasts; frags += arena.frags(); draws += arena.rng().calls();
        std::cout << (ok ? "PASS " : "FAIL ") << name << ": " << ticks << " ticks, bit-exact " << exact << ", max |error| " << max_err << " (" << kRecord[max_field] << " @ tick " << max_tick << "), pellets "
                  << sc_shots << ", detonations " << sc_blasts << ", frags " << arena.frags() << ", random draws " << arena.rng().calls() << ", hp " << arena.health() << '\n';
    }
    if(r.at != r.data.size()) throw std::runtime_error("trailing bytes in combat fixture");
    std::size_t fired = 0;
    for(const bool f : weapon_fired) fired += f;
    if(fired != kWeaponCount) { ++failures; std::cout << "FAIL only " << fired << " of 16 weapons fired in the fixture\n"; }
    std::cout << (failures ? "FAIL " : "PASS ") << "legacy combat: " << scenarios << " scenarios, " << total_ticks << " ticks (" << static_cast<double>(total_ticks) / 120.0 << " s), bit-exact ticks " << exact_ticks
              << ", worst |error| " << worst << ", tolerance " << tolerance << "; weapons fired " << fired << "/16, pellets " << shots << " (world or target hit " << shot_hits << ", target " << target_hits
              << "), detonations " << blasts << ", frags " << frags << ", random draws " << draws << "; native state hash " << std::hex << hash << std::dec << '\n';
    return failures ? 1 : 0;
}
void unit_tests();
}  // namespace

int main(int argc, char** argv) {
    try {
        if(argc == 1) { unit_tests(); std::cout << "PASS combat unit tests: " << checks << " checks\n"; return 0; }
        if(argc == 3) return replay(argv[1], argv[2]);
        std::cerr << "usage: cat_arena_combat_tests [<assets/n1> <combat fixture>]\n";
        return 2;
    } catch(const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
namespace {
// ---- synthetic world: a 40 x 40 m room, floor at z = 0, open sky, one wall at x = 30 (y 4..36, z 0..4),
// one medkit at (10, 30) and one spawn point at (20, 30). ----------------------------------------
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
    V::Object kit;
    kit["x"] = V(10.0); kit["y"] = V(30.0); kit["z"] = V(0.0); kit["tag"] = V(std::string("health"));
    V::Object off;   // not on a floor surface: must be dropped (worldPickupSpots)
    off["x"] = V(12.0); off["y"] = V(30.0); off["z"] = V(2.0); off["tag"] = V(std::string("floating"));
    V::Object c;
    c["cell"] = V(4.0); c["gw"] = V(10.0);
    c["slabs"] = arr(V(std::move(floor))); c["walls"] = arr(V(std::move(wall)));
    c["solids"] = arr(); c["cavities"] = arr(); c["footprint"] = arr(rect(0, 0, 40, 40));
    c["slabGrid"] = arr_of(std::move(slab_grid)); c["solidGrid"] = arr_of(empty_grid); c["cavityGrid"] = arr_of(empty_grid);
    c["pads"] = arr(); c["spawns"] = arr(V(std::move(spawn))); c["pickups"] = arr(V(std::move(kit)), V(std::move(off)));
    return V(std::move(c));
}
constexpr double kDt = physics::kTick;
const double kEye = physics::kEyeHeight;
Arena make(const physics::CollisionWorld& world, int a, int b, double x = 5, double y = 20, double yaw = 0, std::uint32_t seed = 7) {
    Arena arena(world, seed);
    arena.spawn(x, y, 0, yaw);
    arena.set_loadout(a, b);
    return arena;
}

void unit_tests() {
    const V data = synthetic_world();
    const physics::CollisionWorld world(data);
    const auto& w = weapons();
    // ---- weapon table: 16 separate rows -----------------------------------------------------------
    {
        int clips = 0, pellets = 0, shotguns = 0, blast = 0;
        for(std::size_t i = 0; i < kWeaponCount; ++i) {
            clips += w[i].clip; pellets += w[i].pellets; shotguns += w[i].shotgun; blast += w[i].kind != FireKind::hitscan;
            check(w[i].damage > 0 && w[i].delay > 0 && w[i].clip > 0 && w[i].reload > 0 && w[i].range > 0 && w[i].pellets >= 1, std::string(w[i].name) + ": row is complete");
            for(std::size_t k = 0; k < i; ++k) check(std::string(w[i].name) != w[k].name, "weapon names are unique");
        }
        check(clips == 454 && pellets == 57 && shotguns == 3 && blast == 3, "weapon table totals (clips 454, pellets 57, 3 shotguns, 3 explosive)");
        check(w[1].shotgun && w[7].shotgun && w[15].shotgun && w[1].pellets == 8 && w[7].pellets == 14 && w[15].pellets == 20 && w[9].pellets == 3, "shotguns and pellet counts");
        check(w[2].kind == FireKind::grenade && w[4].kind == FireKind::rocket && w[10].kind == FireKind::rift, "the three projectile weapons");
        check(w[8].kind == FireKind::hitscan && w[13].kind == FireKind::hitscan, "plasma repeater and hellfire carbine are hitscan, as in the legacy");
        check(w[3].rail && w[6].lightning && w[3].spread == 0 && w[6].spread == 0 && w[10].spread == 0, "rail / lightning classes, zero-spread weapons");
        check(std::string(w[0].name) == "NAILGUN" && w[0].damage == 13 && w[0].delay == .09 && w[0].clip == 50 && w[0].reload == 1.35 && w[0].spread == .022 && w[0].range == 27, "NAILGUN row");
        check(std::string(w[15].name) == "QUAD SHOTGUN" && w[15].damage == 14 && w[15].delay == 1.15 && w[15].clip == 8 && w[15].reload == 2.5 && w[15].spread == .225 && w[15].range == 17, "QUAD SHOTGUN row");
        check(w[3].damage == 132 && w[3].range == 100 && w[12].damage == 78 && w[12].range == 95 && w[11].clip == 100 && w[11].delay == .065 && w[6].delay == .075, "rail, hand cannon, chaingun, thunderbolt values");
    }
    // ---- random stream: mulberry32, reference values computed with the JavaScript used for the recording ----
    {
        Rng a(1), b(12345), c(0xdeadbeefU);
        check(a.next() == 0.6270739405881613 && a.next() == 0.002735721180215478 && a.next() == 0.5274470399599522, "mulberry32 seed 1");
        check(b.next() == 0.9797282677609473 && b.next() == 0.3067522644996643 && b.next() == 0.484205421525985, "mulberry32 seed 12345");
        check(c.next() == 0.9413696140982211 && c.next() == 0.26719574979506433 && c.next() == 0.772033357527107, "mulberry32 seed 0xdeadbeef");
        check(a.calls() == 3, "draw counter");
    }
    // ---- hit boxes and ray tests --------------------------------------------------------------------
    {
        const Box b{2, 4, -1, 1, 0, 2};
        check(point_box_distance(3, 0, 1, b) == 0 && point_box_distance(0, 0, 1, b) == 2 && point_box_distance(3, 0, 5, b) == 3, "point-box distance: inside, face");
        check(near(point_box_distance(5, 2, 3, b), std::sqrt(3.0), 1e-12), "point-box distance: corner");
        check(near(segment_box_time(0, 0, 1, 10, 0, 1, b, 0), .2, 1e-15), "ray enters the box at x = 2");
        check(near(segment_box_time(0, 0, 1, 10, 0, 1, b, .5), .15, 1e-15), "shot radius grows the box");
        check(segment_box_time(3, 0, 1, 10, 0, 1, b, 0) == 0, "start inside: t = 0");
        check(segment_box_time(0, 1.2, 1, 10, 1.2, 1, b, 0) < 0 && segment_box_time(0, 1.2, 1, 10, 1.2, 1, b, .25) > 0, "parallel miss; hit once the radius reaches it");
        check(segment_box_time(0, 0, 1, 1.5, 0, 1, b, 0) < 0 && segment_box_time(10, 0, 1, 20, 0, 1, b, 0) < 0, "segment ends before / starts after the box");
        check(segment_box_time(0, 0, 5, 10, 0, -5, b, 0) > 0 && segment_box_time(0, 0, 5, 1, 0, 4, b, 0) < 0, "diagonal hit and short diagonal miss");
        Target t; t.x = 10; t.y = 20; t.z = 1;
        auto boxes = t.boxes();
        check(near(boxes[0].x0, 9.61, 1e-12) && near(boxes[0].y1, 20.34, 1e-12) && near(boxes[0].z0, 1.02, 1e-12) && near(boxes[0].z1, 2.24, 1e-12), "standard body box");
        check(near(boxes[1].x1, 10.62, 1e-12) && near(boxes[1].y0, 19.52, 1e-12) && near(boxes[1].y1, 20.49, 1e-12) && near(boxes[1].z0, 2.06, 1e-12) && near(boxes[1].z1, 3.18, 1e-12), "standard head box");
        check(near(t.height(), 2.18, 1e-12), "standard height");
        t.cls = TargetClass::heavy; boxes = t.boxes();
        check(near(boxes[0].x0, 10 - .39 * 1.22, 1e-12) && near(boxes[1].z1, 1 + 2.18 * 1.10, 1e-12) && near(t.height(), 2.398, 1e-12), "heavy class is 1.22 wide, 1.10 tall");
        t.cls = TargetClass::fast; boxes = t.boxes();
        check(near(boxes[0].x1, 10 + .39 * .86, 1e-12) && near(boxes[0].z1, 1 + 1.24 * .90, 1e-12) && near(t.height(), 1.962, 1e-12), "fast class is .86 wide, .90 tall");
    }
    // ---- world shot collision -----------------------------------------------------------------------
    {
        check(!world.solid_at(20, 20, 1.5) && world.solid_at(20, 20, -.2) && world.solid_at(-1, 20, 1.5) && world.solid_at(30.05, 20, 1.5) && !world.solid_at(30.05, 20, 4.2), "solid_at: air, floor slab, outside, wall, above the wall");
        const double t = world.shot_collision_t(20, 20, 1.5, 35, 20, 1.5, .042);
        check(t > (9.6 / 15) && t < (10.0 / 15), "a shot stops at the wall (sampled every 0.19 m): t " + num(t));
        check(world.shot_collision_t(20, 20, 1.5, 28, 20, 1.5, .042) == std::numeric_limits<double>::infinity(), "clear segment");
        check(world.shot_collision_t(20, 20, 6, 35, 20, 6, .042) == std::numeric_limits<double>::infinity(), "over the wall");
        check(world.shot_collision_t(20, 20, -.2, 25, 20, 1.5, .042) == 0, "start inside a solid: t = 0");
        check(world.pickups().size() == 1 && world.pickups()[0].tag == "health", "only pickups on a floor surface are kept");
    }
    // ---- hitscan: rail (no spread) ------------------------------------------------------------------
    {
        auto a = make(world, 3, 0);
        const auto id = a.add_target(11, 20, 0, TargetClass::standard, 1000);
        a.fire();
        // Level shot from the eye (z 1.5): the head box (z 1.06..2.18) is wider than the body and is met first.
        check(a.events().shots.size() == 1 && a.events().shots[0].target && a.events().shots[0].head, "level rail shot hits the head box");
        check(near(a.events().shots[0].x, 11 - .62 - .042, 1e-9) && near(a.events().shots[0].z, kEye, 1e-12), "hit point: head box face minus the shot radius, x " + num(a.events().shots[0].x));
        check(a.targets()[0].hp == 1000 - 264 && a.events().damage.size() == 1 && a.events().damage[0].head && a.events().damage[0].amount == 264 && a.events().damage[0].target_id == id, "head shot = 2 x 132");
        check(a.ammo(0) == 6 && a.gun_kick() == 1 && a.muzzle_flash() == .075 && a.cooldown_until() == .98 && a.events().fired_weapon == 3, "ammo, kick, rail muzzle flash time, cooldown");
        check(a.rng().calls() == 3, "two spread draws per pellet + one hurt roll");
        auto b = make(world, 3, 0);
        b.add_target(11, 20, 0, TargetClass::standard, 1000);
        b.player().state().pitch = .14;   // aim down: passes under the head box
        b.fire();
        check(b.events().shots[0].target && !b.events().shots[0].head && b.targets()[0].hp == 1000 - 132, "body shot = 132");
        check(near(b.events().shots[0].x, 11 - .39 - .042, 1e-9), "hit point on the body box");
        auto c = make(world, 3, 0);
        c.add_target(33, 20, 0, TargetClass::standard, 1000);   // behind the wall
        c.fire();
        check(c.events().shots[0].hit && !c.events().shots[0].target && c.targets()[0].hp == 1000 && c.events().shots[0].x > 29.6 && c.events().shots[0].x < 30, "the wall stops the shot: x " + num(c.events().shots[0].x));
        auto d = make(world, 6, 0);   // thunderbolt: range 20, no spread
        d.add_target(27, 20, 0, TargetClass::standard, 1000);   // 22 m away
        d.fire();
        check(!d.events().shots[0].hit && !d.events().shots[0].target && near(d.events().shots[0].x, 5 + .24 + 20, 1e-12) && d.targets()[0].hp == 1000, "out of range: the shot ends at origin + 20 m");
        check(d.muzzle_flash() == .075, "lightning muzzle flash time");
        // closest of two targets in line is hit; shots do not pass through
        auto e = make(world, 3, 0);
        e.add_target(15, 20, 0, TargetClass::standard, 1000); e.add_target(10, 20, 0, TargetClass::heavy, 1000);
        e.fire();
        check(e.targets()[0].hp == 1000 && e.targets()[1].hp == 1000 - 264, "the nearer target takes the hit");
    }
    // ---- spread: pellets stay inside the cone, shotguns spend one round per volley --------------------
    for(const int index : {1, 7, 15, 9, 0, 11}) {
        auto a = make(world, index, 3, 2, 20);
        a.fire();
        const auto& d = w[static_cast<std::size_t>(index)];
        check(static_cast<int>(a.events().shots.size()) == d.pellets && a.ammo(0) == d.clip - 1 && a.rng().calls() == static_cast<std::uint64_t>(2 * d.pellets), std::string(d.name) + ": pellets per shot, one round used");
        double widest = 0;
        for(const auto& s : a.events().shots) {
            const double az = std::atan2(s.y - 20, s.x - 2), el = std::atan2(s.z - kEye, std::hypot(s.x - 2, s.y - 20));
            check(std::fabs(az) <= d.spread + 1e-9 && std::fabs(el) <= d.spread * .7 + 1e-9, std::string(d.name) + ": pellet inside the spread cone");
            widest = std::max(widest, std::fabs(az));
        }
        if(d.pellets >= 8) check(widest > d.spread * .4, std::string(d.name) + ": pellets actually spread");
        check(a.muzzle_flash() == (d.pellets > 1 ? .145 : .105), std::string(d.name) + ": muzzle flash time");
    }
    // ---- cadence, magazine and reload for every weapon ---------------------------------------------------
    for(std::size_t index = 0; index < kWeaponCount; ++index) {
        const auto& d = w[index];
        auto a = make(world, static_cast<int>(index), static_cast<int>((index + 1) % kWeaponCount), 3, 20);
        std::vector<int> shot_ticks;
        int reload_start = -1, reload_done = -1, tick = 0;
        for(; tick < 40000 && reload_done < 0; ++tick) {
            a.step({key_fire, 0, 0});
            if(a.events().fired_weapon >= 0) { check(a.events().fired_weapon == static_cast<int>(index), "fired weapon index"); shot_ticks.push_back(tick == 0 ? -1 : tick); }   // the press fires before tick 0 advances the clock
            if(a.events().reload_started) reload_start = tick;
            if(a.events().reload_done) reload_done = tick;
            check(a.health() == 100, std::string(d.name) + ": firing down the room does not hurt the player");
        }
        check(static_cast<int>(shot_ticks.size()) == d.clip, std::string(d.name) + ": a magazine holds " + std::to_string(d.clip) + " shots, fired " + std::to_string(shot_ticks.size()));
        for(std::size_t k = 1; k < shot_ticks.size(); ++k) {
            const int gap = shot_ticks[k] - shot_ticks[k - 1];
            check(gap * kDt >= d.delay - 1e-9 && (gap - 1) * kDt < d.delay + 1e-9, std::string(d.name) + ": " + std::to_string(gap) + " ticks between shots for a delay of " + num(d.delay) + " s");
        }
        check(reload_start == shot_ticks.back(), std::string(d.name) + ": the empty magazine reloads automatically on the last shot");
        const int reload_ticks = reload_done - reload_start;
        check(reload_ticks * kDt >= d.reload - 1e-9 && (reload_ticks - 1) * kDt < d.reload + 1e-9, std::string(d.name) + ": reload takes " + num(d.reload) + " s, measured " + std::to_string(reload_ticks) + " ticks");
        check(a.ammo(0) == d.clip && a.reloading() == 0, std::string(d.name) + ": magazine full after the reload");
        check(a.rng().calls() == (d.kind == FireKind::hitscan ? static_cast<std::uint64_t>(2 * d.pellets * d.clip) : 0U), std::string(d.name) + ": random draws only for pellet spread");
        check(a.ammo(1) == w[(index + 1) % kWeaponCount].clip && a.weapon(0) == static_cast<int>(index), "the other slot is untouched");
    }
    // ---- reload key and weapon swap --------------------------------------------------------------------
    {
        auto a = make(world, 0, 3);
        a.reload();
        check(a.reloading() == 0, "reload with a full magazine does nothing");
        a.step({key_fire, 0, 0}); a.step({0, 0, 0});
        check(a.ammo(0) == 49, "one nail fired");
        a.step({key_reload, 0, 0});
        check(a.reloading() > 1.3 && a.events().reload_started, "R starts the reload");
        a.step({key_reload | key_fire, 0, 0});
        check(a.ammo(0) == 49 && a.events().fired_weapon < 0, "no firing while reloading");
        a.step({key_reload | key_fire | key_switch, 0, 0});
        check(a.slot() == 1 && a.reloading() == 0 && a.events().switched && near(a.cooldown_until(), a.player().now() - kDt + .15, 1e-12), "swap cancels the reload and sets a 0.15 s cooldown");
        check(a.ammo(0) == 49 && a.ammo(1) == 7 - 0 && a.current_weapon() == 3, "magazines are kept per slot; the cancelled reload did not fill the magazine");
        int first = -1;
        for(int i = 0; i < 40 && first < 0; ++i) { a.step({key_fire | key_switch | key_reload, 0, 0}); if(a.events().fired_weapon == 3) first = i; }
        check(first >= 16 && first <= 18, "the swapped-in weapon fires after the 0.15 s swap delay (tick " + std::to_string(first) + ")");
        // tapping fire: a click fires at once, between ticks (mousedown), not only on the next tick
        auto b = make(world, 12, 0);
        b.step({key_fire, 0, 0});
        check(b.events().fired_weapon == 12 && b.ammo(0) == 6, "click fires on the press");
        b.step({0, 0, 0}); b.step({key_fire, 0, 0});
        check(b.events().fired_weapon < 0, "a second click inside the weapon delay does not fire");
    }
    // ---- projectile travel ------------------------------------------------------------------------------
    {
        auto a = make(world, 4, 10);   // rocket
        a.fire();
        auto p = a.projectiles();
        check(p.size() == 1 && p[0].kind == FireKind::rocket && near(p[0].x, 5.24, 1e-12) && p[0].z == kEye && p[0].vx == 26 && p[0].vy == 0 && near(p[0].vz, 0, 1e-12) && p[0].life == 2.6, "rocket spawns 0.24 m in front of the eye at 26 m/s");
        check(a.muzzle_flash() == .16 && a.rng().calls() == 0 && a.ammo(0) == 5, "explosive muzzle flash time, no spread draws");
        int n = 0;
        for(; n < 400 && a.events().blasts.empty(); ++n) {
            a.step({0, 0, 0});
            p = a.projectiles();
            if(!p.empty()) check(near(p[0].x, 5.24 + 26 * kDt * (n + 1), 1e-9) && p[0].y == 20 && p[0].z == kEye, "rocket flies straight at constant speed");
        }
        const auto& blast = a.events().blasts.at(0);
        check(blast.kind == FireKind::rocket && blast.power == 1.15 && blast.x > 29.5 && blast.x < 29.9 && near(blast.z, kEye, 1e-12), "rocket detonates on the wall: x " + num(blast.x));
        check(std::abs(n - static_cast<int>(std::lround((blast.x - 5.24) / 26 * 120))) <= 1 && a.projectiles().empty() && a.health() == 100, "time to impact = distance / 26 m/s (" + std::to_string(n) + " ticks)");
        // lifetime: fired into the open sky, a rocket detonates after 2.6 s
        auto b = make(world, 4, 10);
        b.player().state().pitch = -1.4;   // steep, so that it stays over the room for the whole fuse
        b.fire();
        int ticks = 0;
        for(; ticks < 400 && b.events().blasts.empty(); ++ticks) b.step({0, 0, 0});
        check(std::abs(ticks - 312) <= 1, "rocket fuse 2.6 s = 312 ticks, measured " + std::to_string(ticks));
        check(near(b.events().blasts.at(0).z, kEye + .24 * std::sin(1.4) + 26 * std::sin(1.4) * ticks * kDt, 1e-6), "rocket height at the end of the fuse");
        // grenade: gravity 15 m/s^2, +1.5 m/s lift, semi-implicit Euler like the legacy
        auto g = make(world, 2, 4);
        g.fire();
        p = g.projectiles();
        check(p.size() == 1 && p[0].kind == FireKind::grenade && p[0].vx == 22 && near(p[0].vz, 1.5, 1e-12) && p[0].life == 2.3, "grenade: 22 m/s plus 1.5 m/s upward");
        int k = 0;
        for(; k < 400 && g.events().blasts.empty(); ++k) {
            g.step({0, 0, 0});
            p = g.projectiles();
            const double m = k + 1;
            if(!p.empty()) check(near(p[0].x, 5.24 + 22 * kDt * m, 1e-9) && near(p[0].z, kEye + 1.5 * kDt * m - 15 * kDt * kDt * m * (m + 1) / 2, 1e-9), "grenade arc at tick " + std::to_string(k));
        }
        const auto& gb = g.events().blasts.at(0);
        check(gb.kind == FireKind::grenade && gb.power == 1.08 && gb.z < .35 && gb.x > 15 && gb.x < 19 && g.health() == 100, "grenade lands and detonates on the floor ~12 m away: x " + num(gb.x) + " z " + num(gb.z));
        // rift bolt: 19 m/s, strongest blast
        auto r = make(world, 10, 4);
        r.fire();
        p = r.projectiles();
        check(p.size() == 1 && p[0].kind == FireKind::rift && p[0].vx == 19 && p[0].life == 2.6, "rift bolt: 19 m/s");
        int q = 0;
        for(; q < 400 && r.events().blasts.empty(); ++q) r.step({0, 0, 0});
        check(r.events().blasts.at(0).kind == FireKind::rift && r.events().blasts.at(0).power == 1.42 && r.events().blasts.at(0).x > 29.4 && q > n, "rift bolt detonates on the wall, later than a rocket");
        // a projectile detonates on a target and the damage is splash from that point (no extra direct-hit damage)
        auto t = make(world, 4, 10);
        t.add_target(15, 20, 0, TargetClass::standard, 1000);
        t.fire();
        for(int i = 0; i < 200 && t.events().blasts.empty(); ++i) t.step({0, 0, 0});
        check(near(t.events().blasts.at(0).x, 15 - .62 - .14, .25) && t.events().damage.size() == 1 && !t.events().damage[0].head, "rocket detonates on the target's box");
        // The detonation point is one projectile radius (0.14 m) off the box, so a direct hit deals slightly less than 95 x 1.15.
        check(near(t.targets()[0].hp, 1000 - 95 * 1.15 * (1 - .14 / (3.35 * 1.15)), 1e-9), "direct rocket hit = splash from 0.14 m: " + num(1000 - t.targets()[0].hp));
    }
    // ---- splash falloff ---------------------------------------------------------------------------------
    for(const double power : {kRocketPower, kGrenadePower, kRiftPower}) {
        auto a = make(world, 4, 10, 5, 8);
        // Detonation at (20, 20, 1.15): inside the height range of both boxes, so the distance to a
        // target at x = 20 + d is d - 0.62 (head box half width).
        const double offsets[] = {.62, 1.0, 2.0, 3.0, 4.0, 4.6, 5.6};
        for(const double d : offsets) a.add_target(20 + d, 20 + (d > 5 ? 0 : 0), 0, TargetClass::standard, 1000);
        // spread the targets around so that they do not shade each other (splash ignores cover anyway)
        a.explode(20, 20, power, 1.15, FireKind::rocket);
        const double radius = kEnemySplashRadius * power;
        double previous = 1e9;
        for(std::size_t i = 0; i < std::size(offsets); ++i) {
            const double distance = offsets[i] - .62, expected = distance < radius ? 95 * power * (1 - distance / radius) : 0;
            const double dealt = 1000 - a.targets()[i].hp;
            check(near(dealt, expected, 1e-9), "splash at " + num(distance) + " m with power " + num(power) + ": dealt " + num(dealt) + ", expected " + num(expected));
            check(dealt <= previous, "splash damage falls with distance");
            previous = dealt;
        }
        check(near(1000 - a.targets()[0].hp, 95 * power, 1e-9), "full damage at contact");
        check(a.health() == 100 && a.player().state().vz == 0, "player 19 m away: untouched");
        check(a.rng().calls() == a.events().damage.size(), "one hurt roll per damaged survivor");
    }
    // ---- self damage and blast impulse (rocket / grenade jump) --------------------------------------------
    {
        const double full = std::sqrt(2 * physics::kGravity * physics::kPlayerHeight * 2.25);
        auto a = make(world, 4, 2, 20, 20);
        a.explode(20, 20, kRocketPower, 0, FireKind::rocket);   // at the feet
        const double range = kPlayerSplashRadius * kRocketPower;
        check(a.health() == 100 - std::floor(38 * kRocketPower * (1 - .035 / range) + .5) && a.health() == 57, "rocket at the feet: 43 damage, hp " + num(a.health()));
        check(near(a.player().state().vz, full, 1e-12) && near(full, 12.0748, 1e-4) && !a.player().state().ground, "full launch speed sqrt(2 g 2.25 h) = 12.07 m/s");
        check(near(a.player().state().vx, -8, 1e-12) && near(a.player().state().vy, 0, 1e-12), "blast straight below pushes back along the view direction at 8 m/s");
        check(a.events().player_damage == 43 && a.damage_flash() == .62, "damage event and flash");
        auto b = make(world, 4, 2, 20, 20);
        b.explode(18, 20, kRocketPower, .9, FireKind::rocket);   // 2 m to the side: 1.7 m from the hit box
        const double pct = (range - 1.7) / (range * .58);
        check(b.health() == 100 - std::floor(38 * kRocketPower * (1 - 1.7 / range) + .5) && b.health() == 76, "rocket 1.7 m away: 24 damage, hp " + num(b.health()));
        check(near(b.player().state().vz, full * pct, 1e-9) && near(b.player().state().vx, 8 * pct, 1e-9) && b.player().state().vy == 0, "impulse scales with distance and pushes away from the blast");
        auto c = make(world, 4, 2, 20, 20);
        c.explode(20 - .3 - range - .01, 20, kRocketPower, .9, FireKind::rocket);
        check(c.health() == 100 && c.player().state().vz == 0 && c.player().state().ground, "outside the radius: nothing");
        c.explode(20 - .3 - range + .02, 20, kRocketPower, .9, FireKind::rocket);
        check(c.health() == 99, "edge of the radius: at least 1 damage");
        // grenade and rift radii / damage
        auto g = make(world, 2, 4, 20, 20); g.explode(20, 20, kGrenadePower, 0, FireKind::grenade);
        auto r = make(world, 10, 4, 20, 20); r.explode(20, 20, kRiftPower, 0, FireKind::rift);
        check(g.health() == 100 - 41 && r.health() == 100 - 54, "grenade 41 and rift 54 self damage at the feet: " + num(100 - g.health()) + ", " + num(100 - r.health()));
        // crouching lowers the hit box (nominal 1.20 m): a blast overhead is further away
        auto s = make(world, 4, 2, 20, 20), d = make(world, 4, 2, 20, 20);
        d.player().state().crouch_offset = physics::kCrouchOffsetTarget;
        s.explode(20, 20, kRocketPower, 3.0, FireKind::rocket); d.explode(20, 20, kRocketPower, 3.0, FireKind::rocket);
        check(s.health() == 100 - std::floor(38 * kRocketPower * (1 - 1.2 / range) + .5) && d.health() == 100 - std::floor(38 * kRocketPower * (1 - 1.8 / range) + .5) && d.health() > s.health(), "crouched hit box is 1.20 m tall");
        // rocket jump through the simulation: look down, jump and fire; compare with a plain jump
        auto plain = make(world, 0, 1, 20, 20), jump = make(world, 4, 2, 20, 20);
        double apex_plain = 0, apex_jump = 0;
        for(int i = 0; i < 4; ++i) { plain.step({0, 0, 90}); jump.step({0, 0, 90}); }
        check(near(jump.player().state().pitch, 1.08, 1e-12), "looking down");
        for(int i = 0; i < 360; ++i) {
            const std::uint32_t keys = i < 3 ? static_cast<std::uint32_t>(physics::key_jump) : 0U;
            plain.step({keys, 0, 0}); jump.step({keys | (i < 3 ? static_cast<std::uint32_t>(key_fire) : 0U), 0, 0});
            apex_plain = std::max(apex_plain, plain.player().state().z); apex_jump = std::max(apex_jump, jump.player().state().z);
        }
        check(near(apex_plain, 7.65 * 7.65 / 36, .08), "plain jump apex ~1.63 m: " + num(apex_plain));
        check(apex_jump > 4.0 && apex_jump < 4.8 && jump.health() < 70 && jump.health() > 40 && jump.player().state().ground, "rocket jump apex " + num(apex_jump) + " m, hp " + num(jump.health()));
        // respawn protection and death
        auto e = make(world, 4, 2, 20, 20);
        e.set_health(20);
        check(e.take_damage(0) == false && e.take_damage(-5) == false && e.health() == 20, "zero / negative damage is ignored");
        e.explode(20, 20, kRocketPower, 0, FireKind::rocket);
        check(e.health() == 0 && e.dead() && e.deaths() == 1 && e.player().state().vz == 0, "lethal blast: dead, no launch");
        e.step({key_fire, 0, 0});
        check(!e.dead() && e.health() == 100 && e.respawn_grace() == 2.5 && near(e.player().state().x, 20, 1e-12) && near(e.player().state().y, 30, 1e-12) && e.projectiles().empty(), "respawn at a spawn point with full health");
        check(e.weapon(0) != e.weapon(1) && e.ammo(0) == w[static_cast<std::size_t>(e.weapon(0))].clip && e.rng().calls() == 4, "respawn: one score draw per spawn point, one pick, then a new loadout of two different weapons");
        e.explode(20, 30, kRocketPower, 0, FireKind::rocket);
        check(e.health() == 100 && near(e.player().state().vz, full, 1e-12), "respawn protection blocks the damage for 2.5 s; the blast still launches (as in the legacy)");
    }
    // ---- shotgun recoil ---------------------------------------------------------------------------------
    {
        const struct { int index; double force, lift; } rows[] = {{1, 4.7, 7.3}, {7, 5.7, 7.8}, {15, 6.7, 8.2}};
        for(const auto& row : rows) {
            auto a = make(world, row.index, 0, 20, 20, 0);
            a.fire();
            check(near(a.player().state().vx, -row.force, 1e-12) && a.player().state().vy == 0 && a.player().state().vz == 0 && a.player().state().ground, std::string(w[static_cast<std::size_t>(row.index)].name) + ": ground recoil");
            auto b = make(world, row.index, 0, 20, 20, 0);
            b.player().state().z = 3; b.player().state().ground = false; b.player().state().vz = -4;
            b.fire();
            check(near(b.player().state().vx, -row.force * .55, 1e-12) && b.player().state().vz == row.lift, std::string(w[static_cast<std::size_t>(row.index)].name) + ": airborne recoil lifts (double jump)");
            auto c = make(world, row.index, 0, 20, 20, 0);
            c.player().state().z = 3; c.player().state().ground = false; c.player().state().vz = 11;
            c.fire();
            check(c.player().state().vz == 11, "the lift never slows a faster rise");
        }
        auto n = make(world, 12, 0, 20, 20, 0);
        n.fire();
        check(n.player().state().vx == 0, "other weapons have no recoil push");
        auto d = make(world, 15, 0, 20, 20, physics::kLookYaw * 0 + 1.5707963267948966);
        d.fire();
        check(near(d.player().state().vy, -6.7, 1e-12) && near(d.player().state().vx, 0, 1e-12), "recoil is opposite to the view yaw");
    }
    // ---- kills: frag count and weapon reroll -------------------------------------------------------------
    {
        auto a = make(world, 3, 0);
        a.add_target(11, 20, 0, TargetClass::standard, 200);
        a.add_target(11, 24, 0, TargetClass::standard, 200);
        a.fire();
        check(a.targets().size() == 1 && a.frags() == 1 && a.events().damage[0].killed && near(a.targets()[0].y, 24, 1e-12), "264 head damage kills a 200 hp target; it is removed");
        check(a.weapon(0) != 3 && a.events().reroll == a.weapon(0) && a.weapon(1) == 0 && a.ammo(0) == w[static_cast<std::size_t>(a.weapon(0))].clip && a.ammo(1) == 50, "a frag swaps the weapon in hand for a different one with a full magazine");
        check(near(a.cooldown_until(), .12, 1e-12) && a.rng().calls() == 3, "0.12 s cooldown after the reroll; draws: 2 spread + 1 reroll (no hurt roll on a kill)");
        // every reroll result differs from the old weapon and all 15 others are reachable
        bool seen[kWeaponCount]{};
        for(std::uint32_t seed = 1; seed <= 400; ++seed) {
            auto b = make(world, 6, 0, 5, 20, 0, seed);
            b.add_target(11, 20, 0, TargetClass::fast, 1);
            b.fire();
            check(b.weapon(0) != 6 && b.weapon(0) >= 0 && b.weapon(0) < 16, "reroll result");
            seen[b.weapon(0)] = true;
        }
        std::size_t kinds = 0;
        for(const bool s : seen) kinds += s;
        check(kinds == 15, "reroll reaches all 15 other weapons");
        // a volley that kills keeps the firing weapon's pellet count (the row is read before the loop)
        auto c = make(world, 15, 0, 9.3, 20);
        c.add_target(11, 20, 0, TargetClass::heavy, 20);
        c.fire();
        check(c.frags() == 1 && c.events().shots.size() == 20, "quad shotgun volley completes after the kill");
        for(std::uint32_t seed = 1; seed <= 200; ++seed) {
            Arena d(world, seed);
            d.spawn(5, 20, 0, 0);
            d.roll_loadout();
            check(d.weapon(0) != d.weapon(1) && d.weapon(0) >= 0 && d.weapon(0) < 16 && d.weapon(1) >= 0 && d.weapon(1) < 16 && d.slot() == 0 && near(d.cooldown_until(), .2, 1e-12), "rolled loadout: two different weapons");
        }
    }
    // ---- medkits ----------------------------------------------------------------------------------------
    {
        auto a = make(world, 0, 1, 10, 30);
        a.step({0, 0, 0});
        check(a.health() == 100 && a.pickups()[0].cool == 0, "full health: the medkit stays");
        a.set_health(37);
        a.step({0, 0, 0});
        check(a.health() == 77 && a.pickups()[0].cool == 13 && a.events().healed == 40, "medkit heals 40 and starts a 13 s cooldown");
        a.step({0, 0, 0});
        check(a.health() == 77, "no second heal while it cools down");
        for(int i = 0; i < 13 * 120 + 2; ++i) a.step({0, 0, 0});
        check(a.health() == 100 && a.events().healed == 0, "available again after 13 s; health is capped at 100: " + num(a.health()));
        auto b = make(world, 0, 1, 10 + kMedkitRadius + .01, 30);
        b.set_health(50); b.step({0, 0, 0});
        check(b.health() == 50, "outside the 0.85 m pickup radius");
        auto c = make(world, 0, 1, 10, 30);
        c.set_health(50); c.player().state().z = 1.3; c.player().state().ground = false; c.step({0, 0, 0});
        check(c.health() == 50, "1.2 m above the medkit's deck: not picked up");
    }
    // ---- determinism --------------------------------------------------------------------------------------
    {
        auto run = [&](std::uint32_t seed) {
            auto a = make(world, 7, 4, 6, 20, 0, seed);
            a.add_target(12, 20, 0, TargetClass::standard, 5000); a.add_target(16, 21, 0, TargetClass::heavy, 5000);
            std::uint64_t hash = 1469598103934665603ULL;
            for(int i = 0; i < 1500; ++i) {
                std::uint32_t keys = key_fire;
                if(i % 400 > 300) keys |= physics::key_left;
                if(i % 700 == 350 || i % 700 == 351) keys |= key_switch;
                a.step({keys, (i % 90) == 0 ? 5.0 : 0.0, 0});
                double v[kWidth];
                snapshot(a, v);
                for(const double value : v) { unsigned char bytes[8]; std::memcpy(bytes, &value, 8); for(const auto byte : bytes) { hash ^= byte; hash *= 1099511628211ULL; } }
            }
            return hash;
        };
        check(run(11) == run(11), "same seed and input: identical state every tick");
        check(run(11) != run(12), "the seed changes the pellet spread");
    }
}
}  // namespace
