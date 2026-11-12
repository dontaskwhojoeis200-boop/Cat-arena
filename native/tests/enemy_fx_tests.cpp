// N6 enemy presentation tests. GPU-free.
//   cat_arena_enemy_fx_tests [<assets/n6>]                                         unit tests (the mesh checks need the asset directory)
//   cat_arena_enemy_fx_tests <assets/n1> <map-id> <enemy fixture> <pose fixture>   replay of the legacy animation
// The pose fixture (tools/n6/legacy_enemies.cjs --pose-out) holds 80 values per 1/120 s tick computed
// by the unmodified legacy enemyEvalPose / enemyDeathState / enemyHandWorld while the enemy fixture's
// tape plays: sums of the 16 joint angles over all enemies (plain and weighted by enemy id), both
// hands' world positions, attack progress, the dying bodies' topple, sink and slack pose, and the
// Marine Cat pose of the local player. Poses are float32, as the legacy stores them.
#include "cat/enemy_fx.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
using namespace cat;
namespace {
int checks = 0;
void check(bool value, const std::string& why) { ++checks; if(!value) throw std::runtime_error(why); }
bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }
struct Reader {
    std::vector<char> data;
    std::size_t at{};
    explicit Reader(const std::string& file) {
        std::ifstream in(file, std::ios::binary);
        if(!in) throw std::runtime_error("cannot open " + file);
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    void need(std::size_t n) const { if(at + n > data.size()) throw std::runtime_error("fixture truncated"); }
    std::uint32_t u32() { need(4); std::uint32_t v; std::memcpy(&v, data.data() + at, 4); at += 4; return v; }
    float f32() { need(4); float v; std::memcpy(&v, data.data() + at, 4); at += 4; return v; }
    std::vector<double> f64(std::size_t n) { need(n * 8); std::vector<double> v(n); if(n) std::memcpy(v.data(), data.data() + at, n * 8); at += n * 8; return v; }
    std::string text(std::size_t n) { need(n); std::string s(data.data() + at, n); at += n; return s; }
};
constexpr int kWidth = 81;   // pose fixture version 2: 80 = cosmetic random draws
std::string field(int k) {
    static const char* const joint[16] = {"hipL", "hipR", "kneeL", "kneeR", "shoulderPitchL", "shoulderPitchR", "elbowL", "elbowR", "shoulderRollL", "shoulderRollR", "spinePitch", "spineYaw", "headPitch", "headYaw", "bob", "rootRoll"};
    if(k < 16) return std::string("poseSum.") + joint[k];
    if(k < 32) return std::string("weightedPoseSum.") + joint[k - 16];
    if(k >= 45 && k < 61) return std::string("deathPoseSum.") + joint[k - 45];
    if(k == 80) return "cosmeticDraws";
    if(k >= 64) return std::string("marinePose.") + joint[k - 64];
    static const char* const other[] = {"enemies", "animationDraws", "leftHandX", "leftHandY", "leftHandZ", "rightHandX", "rightHandY", "rightHandZ", "attackProgress", "dying", "deathFall", "deathSide", "deathSink"};
    if(k >= 32 && k <= 44) return other[k - 32];
    return k == 61 ? "deathFlash" : k == 62 ? "deathVariantSide" : "deathZ";
}

int replay(const std::string& assets_root, const std::string& map, const std::string& fixture, const std::string& pose_fixture) {
    const auto doc = assets::load_data(assets_root + "/maps/" + map + "/map.cadata");
    const physics::CollisionWorld world(doc.at("compiled"));
    nav::NavGraph graph(world);
    Reader r(fixture), pr(pose_fixture);
    if(r.text(8) != "CATENMY1" || r.u32() != 1) throw std::runtime_error("unsupported enemy fixture");
    const auto scenarios = r.u32(), tape_width = r.u32(), record_width = r.u32();
    if(tape_width != 3 || record_width != 64) throw std::runtime_error("unexpected enemy fixture layout");
    if(pr.text(8) != "CATPOSE1" || pr.u32() != 2 || pr.u32() != scenarios || pr.u32() != kWidth) throw std::runtime_error("unsupported pose fixture");
    // Poses are float32 sums: a last-bit difference of a double (libm vs V8) can move a float by one unit in the last place.
    const double tolerance = 2e-5;
    std::size_t total_ticks = 0, exact_ticks = 0, failures = 0, posed = 0, most_bodies = 0;
    std::uint64_t draws = 0, hash = 1469598103934665603ULL;
    double worst = 0;
    for(std::uint32_t s = 0; s < scenarios; ++s) {
        const auto name = r.text(r.u32());
        const auto ticks = r.u32();
        const auto start = r.f64(16);
        const auto tape = r.f64(std::size_t(ticks) * 3);
        (void)r.f64(std::size_t(ticks) * 64);
        if(pr.text(pr.u32()) != name || pr.u32() != ticks) throw std::runtime_error("pose fixture does not match the enemy fixture");
        const auto rec = pr.f64(std::size_t(ticks) * kWidth);
        const auto seed = static_cast<std::int32_t>(start[9]);
        combat::Arena arena(world, static_cast<std::uint32_t>(seed));
        arena.spawn(start[0], start[1], start[2], start[3], start[4], start[5]);
        arena.set_health(start[6]);
        arena.set_loadout(static_cast<int>(start[7]), static_cast<int>(start[8]));
        combat::Horde horde(arena, graph);
        const int wave = static_cast<int>(start[10]);
        horde.set_wave(std::max(1, wave - 1), static_cast<int>(start[11]));
        horde.begin_wave(wave);
        fx::WeaponFx effects(world, static_cast<std::uint32_t>(seed * 31 + 17));
        fx::EnemyFx enemies(effects, static_cast<std::uint32_t>(seed * 131 + 7));
        double max_err = 0; int max_field = 0; std::size_t max_tick = 0, exact = 0, first_bad = ticks;
        fx::Pose death{};
        // effects run inside the step where update() runs WFX.update / EFX.update (before a respawn);
        // the legacy default horizontal FOV (setFov) on a 16:9 page
        arena.set_presentation_hook([&] { enemies.tick(arena, horde, 105, 16.0 / 9); });
        for(std::uint32_t i = 0; i < ticks; ++i) {
            arena.step({static_cast<std::uint32_t>(tape[i * 3]), tape[i * 3 + 1], tape[i * 3 + 2]});
            double got[kWidth] = {};
            for(const auto& b : arena.targets()) {
                const fx::Pose& pose = enemies.pose(b, arena);
                ++posed;
                const double k = static_cast<double>(b.uid % 7 + 1);
                for(std::size_t j = 0; j < 16; ++j) { got[j] += static_cast<double>(pose[j]); got[16 + j] += static_cast<double>(pose[j]) * k; }
                const auto& rig = fx::enemy_rig(b.type);
                const auto hl = fx::hand_world(rig, b.x, b.y, b.z, b.a, -1, pose), hr = fx::hand_world(rig, b.x, b.y, b.z, b.a, 1, pose);
                got[34] += hl[0]; got[35] += hl[1]; got[36] += hl[2]; got[37] += hr[0]; got[38] += hr[1]; got[39] += hr[2];
                got[40] += combat::attack_progress(b);
            }
            got[32] = static_cast<double>(arena.targets().size());
            got[41] = static_cast<double>(horde.corpses().size());
            if(enemies.bodies().size() != horde.corpses().size()) throw std::runtime_error("corpse lists out of step");
            most_bodies = std::max(most_bodies, enemies.bodies().size());
            for(std::size_t d = 0; d < horde.corpses().size(); ++d) {
                const auto& c = horde.corpses()[d];
                const auto& body = enemies.bodies()[d];
                if(body.corpse_id != c.id) throw std::runtime_error("corpse lists out of order");
                const auto st = fx::death_state(fx::enemy_rig(c.type), body.pose, body.variant, body.side, c.t, death);
                got[42] += st.fall; got[43] += st.side; got[44] += st.sink;
                for(std::size_t j = 0; j < 16; ++j) got[45 + j] += static_cast<double>(death[j]);
                got[61] += body.flash; got[62] += body.variant + 10 * body.side; got[63] += c.z;
            }
            const fx::Pose& marine = enemies.marine_pose(arena);
            for(std::size_t j = 0; j < 16; ++j) got[64 + j] = static_cast<double>(marine[j]);
            got[33] = static_cast<double>(enemies.rng().calls());
            got[80] = static_cast<double>(effects.rng().calls());
            for(const double value : got) { unsigned char bytes[8]; std::memcpy(bytes, &value, 8); for(const auto b : bytes) { hash ^= b; hash *= 1099511628211ULL; } }
            bool same = true;
            for(int k = 0; k < kWidth; ++k) {
                const double want = rec[std::size_t(i) * kWidth + std::size_t(k)], err = std::fabs(got[k] - want);
                if(std::memcmp(&got[k], &want, 8) != 0 && !(got[k] == 0 && want == 0)) same = false;
                if(err > max_err) { max_err = err; max_field = k; max_tick = i; }
                if(err > tolerance && first_bad == ticks) { first_bad = i; std::cout << "  first difference in " << name << " @ tick " << i << ": " << field(k) << " native " << got[k] << ", legacy " << want << '\n'; }
            }
            exact += same;
            if(first_bad != ticks && i > first_bad + 2) break;
        }
        const bool ok = first_bad == ticks;
        failures += !ok; total_ticks += ticks; exact_ticks += exact; worst = std::max(worst, ok ? max_err : 0.0); draws += enemies.rng().calls();
        std::cout << (ok ? "PASS " : "FAIL ") << name << ": " << ticks << " ticks, bit-exact " << exact << ", max |error| " << max_err << " (" << field(max_field) << " @ tick " << max_tick << "), animation draws " << enemies.rng().calls() << '\n';
    }
    if(r.at != r.data.size() || pr.at != pr.data.size()) throw std::runtime_error("trailing bytes in a fixture");
    std::cout << (failures ? "FAIL " : "PASS ") << "legacy enemy animation " << map << ": " << scenarios << " scenarios, " << total_ticks << " ticks, " << posed << " poses evaluated, bit-exact ticks " << exact_ticks << ", worst |error| " << worst
              << ", tolerance " << tolerance << "; most bodies " << most_bodies << ", animation draws " << draws << "; native pose hash " << std::hex << hash << std::dec << '\n';
    return failures ? 1 : 0;
}

struct Mesh { std::uint32_t vertices{}, flags{}; std::array<float, 16> rig{}; std::vector<float> data; std::vector<unsigned char> joint, part; };
Mesh load_mesh(const std::string& file) {
    Reader r(file);
    if(r.text(8) != "CATENM01") throw std::runtime_error("not an .emesh: " + file);
    Mesh m; m.vertices = r.u32(); m.flags = r.u32();
    for(auto& v : m.rig) v = r.f32();
    m.data.resize(std::size_t(m.vertices) * 12);
    for(auto& v : m.data) v = r.f32();
    if(m.flags & 1U) { const auto j = r.text(m.vertices), p = r.text(m.vertices); m.joint.assign(j.begin(), j.end()); m.part.assign(p.begin(), p.end()); }
    if(r.at != r.data.size()) throw std::runtime_error("trailing bytes in " + file);
    return m;
}

void unit_tests(const std::string& asset_dir) {
    using namespace cat::fx;
    // ---- tables ----
    check(kEnemyRig.size() == 20 && kEnemyLook.size() == 20 && combat::enemy_types().size() == 20, "20 creature types");
    std::set<RigClass> classes; std::set<AttackAnim> attacks; int ranged = 0, melee = 0;
    for(std::size_t i = 0; i < 20; ++i) {
        const auto& r = kEnemyRig[i];
        check(std::string(kEnemyName[i]) == combat::enemy_types()[i].name, "type order of the presentation tables matches the simulation");
        classes.insert(r.cls); attacks.insert(r.attack);
        check(r.knee > 0 && r.knee < r.hip && r.hip < r.shoulder && r.elbow < r.shoulder && r.shoulder < r.height && r.neck < r.height && r.spine > r.hip && r.spine < r.neck, std::string("plausible skeleton: ") + kEnemyName[i]);
        check((r.cls == RigClass::flyer) == combat::enemy_types()[i].flying, "flyer rigs belong to flying types");
        const bool has_proj = kEnemyLook[i].proj != EnemyProj::none, has_melee = kEnemyLook[i].melee != EnemyMelee::none;
        check(has_proj != has_melee, "every type has either a projectile look or a melee look");
        check(has_proj == combat::enemy_types()[i].ranged, std::string("projectile look <=> ranged type: ") + kEnemyName[i]);
        ranged += has_proj; melee += has_melee;
    }
    check(classes.size() == 6 && attacks.size() == 8 && ranged == 13 && melee == 7, "6 rig classes, 8 attack animations, 13 ranged and 7 melee looks");
    check(near(kDeathFall + kDeathStay + kDeathSink, 6.88), "death animation lasts 6.88 s");
    // ---- attack phases ----
    check(!attack_phases(-1).on, "no attack, no phases");
    { const auto p0 = attack_phases(0), mid = attack_phases(.4), strike = attack_phases(.76), end = attack_phases(1);
      check(p0.on && near(p0.wind, 0) && near(p0.strike, 0) && near(p0.hold, 0), "attack starts from rest");
      check(mid.wind > .7 && near(mid.strike, 0) && near(mid.hold, 1), "wind-up at 40 %");
      check(strike.strike > .99 && strike.wind < .15, "strike right after the release at 72 %");
      check(near(end.wind, 0) && near(end.strike, 0) && near(end.hold, 0), "attack ends at rest"); }
    // ---- pose evaluation ----
    {
        combat::Rng rng(5);
        AnimState a; Pose p{}, q{};
        PoseInput in; in.x = 10; in.y = 10; in.a = 0;
        eval_pose(a, kEnemyRig[0], in, 100, rng, p);
        check(a.ready && rng.calls() == 2 && a.phase >= 0 && a.phase < 6.28 && a.seed >= 0 && a.seed < 100, "a new creature draws its gait phase and idle seed");
        check(near(p[0], 0) && near(p[1], 0) && near(p[6], .32, 1e-6) && near(p[8], .08, 1e-6), "standing soldier: legs straight, arms at rest");
        // walk forward at 4.5 m/s for a second: the gait builds up and the legs swing in opposition
        double max_hip = 0; bool opposite = true;
        for(int i = 1; i <= 120; ++i) { in.y = 10 + 4.5 * i / 120.0; eval_pose(a, kEnemyRig[0], in, 100 + i / 120.0, rng, p); max_hip = std::max(max_hip, std::fabs(static_cast<double>(p[0]))); opposite = opposite && near(p[0], -p[1], 1e-6); }
        check(a.walk > .9 && a.speed > 4.3 && max_hip > .45 && max_hip <= .5501 && opposite, "soldier gait: hips swing up to 0.55 rad in opposition");
        check(rng.calls() == 2, "walking draws no random numbers");
        const double phase_before = a.phase;
        in.y += 2.5; eval_pose(a, kEnemyRig[0], in, 101 + 1 / 120.0, rng, p);
        check(near(a.phase - phase_before, 2 * 3.14159265358979323846, 1e-9), "one gait cycle per 2.5 m for a soldier");
        // pain: a fresh hit bends the creature and picks a side once
        in.hit_flash = .21; eval_pose(a, kEnemyRig[0], in, 101 + 2 / 120.0, rng, q);
        check(rng.calls() == 3 && near(a.pain, 1) && q[10] < p[10] - .25, "a hit draws the pain side and bends the spine back");
        in.hit_flash = .1; eval_pose(a, kEnemyRig[0], in, 101 + 3 / 120.0, rng, q);
        check(rng.calls() == 3, "the same hit does not redraw the side");
        // the same time twice changes nothing (dt = 0)
        Pose again{}; const AnimState before = a;
        eval_pose(a, kEnemyRig[0], in, 101 + 3 / 120.0, rng, again);
        check(again == q && near(a.phase, before.phase) && near(a.walk, before.walk), "evaluating twice at the same time is idempotent");
    }
    {   // look at the target: head yaw follows, limited to 0.9 rad
        combat::Rng rng(6); AnimState a; Pose p{};
        PoseInput in; in.x = 0; in.y = 0; in.a = 0; in.has_target = true; in.tx = -10; in.ty = 0.001; in.tz = 0;   // target to the left (+90 degrees)
        for(int i = 0; i <= 240; ++i) eval_pose(a, kEnemyRig[0], in, i / 120.0, rng, p);
        check(near(a.look, .9, 1e-3) && near(p[13], .9 * .65, 2e-3), "the head turns toward the player, at most 0.9 rad");
        in.tx = 0; in.ty = 100;   // too far: the look stays where it was
        eval_pose(a, kEnemyRig[0], in, 2.1, rng, p);
        check(near(a.look, .9, 1e-3), "targets beyond 30 m are ignored");
    }
    {   // attack animations differ per style; the flyer flaps while idle
        combat::Rng rng(7);
        const auto strike_pose = [&](int type, double progress) { AnimState a; Pose p{}; PoseInput in; in.attack_progress = progress; eval_pose(a, kEnemyRig[static_cast<std::size_t>(type)], in, 0, rng, p); return p; };
        const Pose shoot = strike_pose(0, .5), slam_up = strike_pose(3, .5), slam_down = strike_pose(3, .76), cast = strike_pose(9, .5), rest = strike_pose(0, -1);
        check(shoot[5] > 1.3 && near(rest[5], .12, 1e-6), "shoot: the right arm shoulders the weapon");
        check(slam_up[4] > 2 && slam_up[5] > 2 && slam_down[4] < 1.2 && slam_down[10] > slam_up[10] + .5, "slam: both fists overhead, then the body pounds forward");
        check(cast[4] > 2 && cast[5] > 2, "cast: both hands raised");
        AnimState a; Pose p{}; PoseInput in; double lo = 9, hi = -9;
        for(int i = 0; i < 240; ++i) { eval_pose(a, kEnemyRig[17], in, i / 120.0, rng, p); lo = std::min(lo, static_cast<double>(p[0])); hi = std::max(hi, static_cast<double>(p[0])); }
        check(near(lo, .28 - .58, 2e-2) && near(hi, .28 + .58, 2e-2) && near(p[0], p[1]), "flyer: wings flap between -0.30 and 0.86 rad while hovering");
    }
    // ---- death ----
    {
        Pose at{}; at[4] = 1; Pose out{};
        const auto& rig = kEnemyRig[0];
        auto st = death_state(rig, at, 0, 1, 0, out);
        check(near(st.fall, 0) && near(st.side, 0) && near(st.sink, 0) && near(out[4], 1), "death starts from the pose at death");
        st = death_state(rig, at, 0, 1, kDeathFall, out);
        check(near(st.fall, 3.14159265358979323846 / 2) && near(out[4], 0) && near(out[2], -.9, 1e-6) && near(out[8], .7, 1e-6), "variant 0: on its back after 0.78 s, limbs slack");
        check(near(death_state(rig, at, 1, 1, kDeathFall, out).fall, -3.14159265358979323846 / 2), "variant 1: on its face");
        st = death_state(rig, at, 2, -1, kDeathFall, out);
        check(near(st.side, -3.14159265358979323846 / 2) && near(st.fall, .25), "variant 2: rolls to its side");
        st = death_state(kEnemyRig[17], at, 3, 1, kDeathFall, out);
        check(near(st.fall, 3.14159265358979323846 / 2.4) && near(st.side, .4) && near(out[0], -.5, 1e-6), "flyer: tumbles, wings fold");
        check(near(death_state(rig, at, 0, 1, kDeathFall + kDeathStay, out).sink, 0), "no sinking while the body stays");
        check(near(death_state(rig, at, 0, 1, kDeathFall + kDeathStay + kDeathSink, out).sink, rig.depth * 1.6 + .4), "fully sunk at the end");
    }
    // ---- hands ----
    {
        const auto& rig = kEnemyRig[0]; Pose rest{};
        const auto l = hand_world(rig, 0, 0, 0, 0, -1, rest), r = hand_world(rig, 0, 0, 0, 0, 1, rest);
        check(near(l[0], -r[0]) && near(l[1], r[1]) && near(l[2], r[2]) && l[0] < 0, "rest pose hands are mirror images; the left hand (side -1) is on -X");
        Pose up{}; up[5] = 1.5708F;   // right shoulder pitched forward by 90 degrees
        const auto f = hand_world(rig, 0, 0, 0, 0, 1, up);
        check(f[2] > r[2] + .3 && f[1] > r[1] + .3, "raising the right arm moves the hand forward (+Y) and up");
        const auto turned = hand_world(rig, 5, 6, 7, 3.14159265358979323846 / 2, 1, rest);
        check(near(turned[0], 5 - r[1], 1e-9) && near(turned[1], 6 + r[0], 1e-9) && near(turned[2], 7 + r[2]), "hands turn with the facing and move with the body");
    }
    // ---- view test ----
    {
        physics::PlayerState p; p.x = 0; p.y = 0; p.z = 0; p.a = 0;   // looking along +X
        check(in_view(p, 90, 16.0 / 9, 20, 0, 1.5, 1) && in_view(p, 90, 16.0 / 9, 20, 19, 1.5, 1), "in front and near the edge: in view");
        check(!in_view(p, 90, 16.0 / 9, -10, 0, 1.5, 1) && !in_view(p, 90, 16.0 / 9, 20, 40, 1.5, 1), "behind or far to the side: not in view");
        check(in_view(p, 90, 16.0 / 9, -1, 0, 1.5, 1), "the margin keeps things right behind the eye");
    }
    // ---- Marine Cat metadata ----
    check(kMarineJoint.size() == 21 && kMarinePart.size() == 6 && kMarineGlbClip.size() == 7, "Marine Cat: 21 joints, 6 hit parts, 7 clips listed in the GLB");
    check(std::string(kMarinePart[0].name) == "head" && near(kMarinePart[0].damage_multiplier, 2) && std::string(kMarinePart[1].name) == "torso" && near(kMarinePart[1].damage_multiplier, 1), "head part doubles damage, torso does not");
    check(marine_part_at(0, 0, 1.6) == 0 && marine_part_at(0, 0, 1.15) == 1 && marine_part_at(.19, 0, .4) >= 4 && marine_part_at(.5, .1, .95) >= 2 && marine_part_at(.5, .1, .95) <= 3 && marine_part_at(3, 0, 1) == -1, "points classify into head, torso, legs, arms or nothing");
    check(marine_part_at(.19, 0, .4) != marine_part_at(-.19, 0, .4), "left and right legs are different parts");
    check(near(kMarineRig.height, 2.08) && kMarineRig.cls == RigClass::soldier, "the Marine Cat uses the soldier gait on its own skeleton");
    // ---- EnemyFx with a live arena ----
    {
        const auto doc = assets::load_data(asset_dir + "/../n1/maps/helios-crucible/map.cadata");
        const physics::CollisionWorld world(doc.at("compiled"));
        nav::NavGraph graph(world);
        combat::Arena arena(world, 77);
        const auto& spawn = doc.at("compiled").at("spawns").array().at(0);
        arena.spawn(spawn.at("x").number(), spawn.at("y").number(), spawn.at("z").number(), 0, 0, 10);
        arena.set_loadout(0, 1);
        combat::Horde horde(arena, graph);
        horde.begin_wave(1);
        WeaponFx effects(world, 3);
        EnemyFx enemies(effects, 4);
        Frame frame; EnemyFrame ef; BuildOptions options;
        const auto gameplay_draws = [&] { return arena.rng().calls(); };
        std::size_t most = 0, most_xray = 0; bool saw_body = false, saw_shadow = false;
        for(int i = 0; i < 1500; ++i) {
            std::uint32_t keys = 0;
            if(i >= 300 && i % 40 == 0 && i < 300 + 40 * 6) keys |= combat::key_fixture_kill;   // six gibbed kills
            const auto before = gameplay_draws();
            arena.set_health(100);   // keep the player alive: a death restarts the wave
            arena.step({keys, 3.0, 0.0});
            const auto after = gameplay_draws();
            enemies.tick(arena, horde, 90, 16.0 / 9);
            effects.build(arena, 90, 16.0 / 9, frame);
            const auto marks = frame.mark.quads();
            enemies.build(arena, horde, options, frame, ef);
            check(gameplay_draws() == after && after >= before, "presentation never draws from the gameplay stream");
            most = std::max(most, ef.instances());
            for(const auto& v : ef.xray) most_xray = std::max(most_xray, v.size());
            saw_shadow = saw_shadow || frame.mark.quads() > marks;
            saw_body = saw_body || !enemies.bodies().empty();
            for(const auto& list : ef.solid) for(const auto& e : list) for(const float v : e.pose) check(std::isfinite(v) && std::fabs(v) < 4, "finite joint angles");
            check(ef.marines.empty(), "first person: the local Marine Cat is never in the draw list");
        }
        check(most >= 5 && most <= 20, "visible creatures are instanced (at most the 20 alive)");
        check(saw_shadow, "blob shadows are added under creatures");
        check(arena.frags() >= 6 && !saw_body, "gibbed kills leave no corpse");
        check(enemies.animated() <= arena.targets().size() + 1, "animation state of dead creatures is released");
        options.local_marine = true; enemies.build(arena, horde, options, frame, ef);
        check(ef.marines.size() == 1 && near(ef.marines[0].pos[3], arena.player().state().a - 3.14159265358979323846 / 2, 1e-6), "third person: one Marine Cat at the player, turned by -90 degrees");
        options.local_marine = false; options.shadows = 3; enemies.build(arena, horde, options, frame, ef);
        std::size_t projected = 0; for(const auto& v : ef.shadow) projected += v.size();
        check(projected > 0, "shadow mode 3 lists projected bodies");
        // an ordinary kill (not below -40 hp) leaves a body; with seven of ten killed the outline pass is on
        check(horde.wave_total() == 10 && horde.wave_kills() == 6 && !arena.targets().empty(), "six of the ten enemies of wave 1 are gone");
        arena.damage_target(0, arena.targets()[0].hp + 1, arena.targets()[0].z + .5, 0);
        enemies.tick(arena, horde, 90, 16.0 / 9);
        check(horde.deaths().size() == 1 && !horde.deaths()[0].gibbed && horde.deaths()[0].corpse_id != 0, "the kill is reported as a death event with a corpse");
        effects.build(arena, 90, 16.0 / 9, frame); enemies.build(arena, horde, options, frame, ef);
        check(ef.xray_on, "the outline pass turns on when five or fewer enemies remain");
        std::size_t outlined = 0; for(const auto& v : ef.xray) outlined += v.size();
        check(outlined <= 5, "at most five outlines");
        saw_body = !enemies.bodies().empty();
        check(saw_body && enemies.bodies().size() == horde.corpses().size(), "ordinary kills leave bodies that play the death animation");
        std::size_t with_fall = 0;
        for(int i = 0; i < 100; ++i) { arena.step({0, 0.0, 0.0}); enemies.tick(arena, horde, 90, 16.0 / 9); }
        effects.build(arena, 90, 16.0 / 9, frame); enemies.build(arena, horde, options, frame, ef);
        for(const auto& list : ef.solid) for(const auto& e : list) with_fall += (e.state[1] != 0 || e.state[2] != 0);
        check(with_fall <= enemies.bodies().size(), "only bodies topple");
        enemies.clear();
        check(enemies.bodies().empty() && enemies.animated() == 0, "clear() forgets everything");
    }
    // ---- meshes ----
    for(std::size_t i = 0; i < 20; ++i) {
        const Mesh m = load_mesh(asset_dir + "/enemies/" + kEnemyFile[i] + ".emesh");
        check(m.vertices % 3 == 0 && m.vertices / 3 >= 200 && m.vertices / 3 <= 300, std::string("at most 300 triangles: ") + kEnemyName[i]);
        check(near(m.rig[0], kEnemyRig[i].hip, 1e-6) && near(m.rig[7], kEnemyRig[i].height, 1e-6) && near(m.rig[11], kEnemyRig[i].width, 1e-6), "the skeleton stored with the mesh is the generated table");
        std::set<int> bones; float top = 0;
        for(std::uint32_t v = 0; v < m.vertices; ++v) {
            const float mat = m.data[std::size_t(v) * 12 + 11];
            check(static_cast<int>(std::floor(mat)) == 400 + static_cast<int>(i), "material id 400 + type");
            bones.insert(static_cast<int>(std::floor((mat - std::floor(mat)) * 25 + .48F)));
            top = std::max(top, m.data[std::size_t(v) * 12 + 2]);
        }
        for(const int b : bones) check(b >= 0 && b <= 6, "limb tags 0 .. 6");
        const bool flyer = kEnemyRig[i].cls == RigClass::flyer;
        check(bones.count(0) && (flyer ? bones.count(5) && bones.count(6) && !bones.count(1) : bones.count(1) && bones.count(2) && bones.count(3) && bones.count(4) && !bones.count(5)), std::string("limb groups: ") + kEnemyName[i]);
        check(near(top, kEnemyRig[i].height, 2e-2), "mesh height is the rig height");
    }
    {
        const Mesh m = load_mesh(asset_dir + "/marine.emesh");
        check(m.vertices == 822 && (m.flags & 1U) && m.joint.size() == 822 && m.part.size() == 822, "Marine Cat: 274 triangles with joint and part per vertex");
        std::array<int, 6> count{}; int face = 0, mismatched = 0;
        for(std::uint32_t v = 0; v < m.vertices; ++v) {
            check(m.joint[v] < 21 && m.part[v] < 6, "joint and part indices in range");
            ++count[m.part[v]];
            face += m.data[std::size_t(v) * 12 + 11] == 20.0F;
            // a vertex skinned to a limb joint lies in (or right next to) that limb's hit volume in the rest pose
            const int at = marine_part_at(m.data[std::size_t(v) * 12], m.data[std::size_t(v) * 12 + 1], m.data[std::size_t(v) * 12 + 2]);
            mismatched += (at >= 0 && at != m.part[v]);
        }
        for(const int c : count) check(c > 0, "every hit part owns vertices");
        check(count[2] == count[3] && count[4] == count[5], "left and right limbs have the same vertex counts");
        check(face == 6, "one face quad (cat face atlas, material 20)");
        check(mismatched < 822 / 4, "skin parts agree with the hit volumes for most vertices");
    }
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if(argc == 2) { unit_tests(argv[1]); std::cout << "PASS enemy presentation unit tests: " << checks << " checks\n"; return 0; }
        if(argc == 5) return replay(argv[1], argv[2], argv[3], argv[4]);
        std::cerr << "usage: cat_arena_enemy_fx_tests <assets/n6> | <assets/n1> <map-id> <enemy fixture> <pose fixture>\n";
        return 2;
    } catch(const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
