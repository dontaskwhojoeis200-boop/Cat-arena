// N6 enemy tests. GPU-free.
//   cat_arena_enemy_tests [--units <assets/n1>]                   unit tests (tables; with the assets also waves, spawning,
//                                                                 attacks, bodies and the restart on Helios Crucible)
//   cat_arena_enemy_tests <assets/n1> <map-id> <enemy fixture>    replay of tapes recorded from the legacy game
// The fixture (tools/n6/legacy_enemies.cjs) holds 64 values per 1/120 s tick of the unmodified legacy
// update(): player, weapons, wave counters, sums over all enemies (position, health, facing, attack
// timers, animation phase, route state), enemy projectiles, route-search statistics, corpses and the
// number of random draws. The native Arena + Horde must reproduce all of them.
#include "cat/enemies.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
using namespace cat;
using namespace cat::combat;
namespace {
int checks = 0;
void check(bool value, const std::string& why) { ++checks; if(!value) throw std::runtime_error(why); }
struct Reader {
    std::vector<char> data;
    std::size_t at{};
    void need(std::size_t n) const { if(at + n > data.size()) throw std::runtime_error("enemy fixture truncated"); }
    std::uint32_t u32() { need(4); std::uint32_t v; std::memcpy(&v, data.data() + at, 4); at += 4; return v; }
    std::vector<double> f64(std::size_t n) { need(n * 8); std::vector<double> v(n); if(n) std::memcpy(v.data(), data.data() + at, n * 8); at += n * 8; return v; }
    std::string text(std::size_t n) { need(n); std::string s(data.data() + at, n); at += n; return s; }
};
constexpr int kWidth = 64;
const char* const kRecord[kWidth] = {"x", "y", "z", "vx", "vy", "vz", "ground", "hp", "yaw", "pitch", "slot", "weapon0", "weapon1", "mag0", "mag1", "cooldown", "reloading", "rngCalls", "score", "deaths", "respawnGrace", "burnRemaining", "flash",
    "waveNumber", "waveTotal", "waveSpawned", "waveKills", "waveCooldown", "waveSpawnTimer",
    "bots", "botSumX", "botSumY", "botSumZ", "botSumHp", "botSumFacing", "botSumCool", "botSumAttackWind", "botSumAttackRecover", "botSumWalkAmount", "botSumAnimPhase", "botSumPathIndex", "botSumPathLength",
    "botSumNavStuck", "botSumPathCooldown", "botsQueued", "botSumSpeed", "botSumUid", "botSumType",
    "enemyShots", "shotSumX", "shotSumY", "shotSumZ", "shotSumDamage", "shotSumLife",
    "navQueue", "navQueries", "navSuccess", "navFailed", "navShared", "navActiveExpanded", "playerProjectiles", "playerProjectileSum", "dying", "aiDecisionAcc"};

void snapshot(const Arena& a, const Horde& h, double* o) {
    for(int i = 0; i < kWidth; ++i) o[i] = 0;
    const auto& p = a.player().state();
    o[0] = p.x; o[1] = p.y; o[2] = p.z; o[3] = p.vx; o[4] = p.vy; o[5] = p.vz; o[6] = p.ground ? 1 : 0; o[7] = a.health(); o[8] = p.a; o[9] = p.pitch;
    o[10] = a.slot(); o[11] = a.weapon(0); o[12] = a.weapon(1); o[13] = a.ammo(0); o[14] = a.ammo(1); o[15] = a.cooldown_until(); o[16] = a.reloading();
    o[17] = static_cast<double>(a.rng().calls()); o[18] = static_cast<double>(a.frags()); o[19] = static_cast<double>(a.deaths()); o[20] = a.respawn_grace(); o[21] = a.burn_remaining(); o[22] = a.damage_flash();
    o[23] = h.wave_number(); o[24] = h.wave_total(); o[25] = h.wave_spawned(); o[26] = h.wave_kills(); o[27] = h.wave_cooldown(); o[28] = h.wave_spawn_timer();
    o[29] = static_cast<double>(a.targets().size());
    for(const auto& b : a.targets()) {
        o[30] += b.x; o[31] += b.y; o[32] += b.z; o[33] += b.hp; o[34] += b.a; o[35] += b.cool; o[36] += b.attack_wind; o[37] += b.attack_recover; o[38] += b.walk_amount; o[39] += b.anim_phase;
        o[40] += static_cast<double>(b.path_index); o[41] += static_cast<double>(b.path.size()); o[42] += b.nav_stuck; o[43] += b.path_cooldown; o[44] += b.nav_queued ? 1 : 0; o[45] += b.speed; o[46] += b.uid; o[47] += b.type;
    }
    o[48] = static_cast<double>(h.shots().size());
    for(const auto& s : h.shots()) { o[49] += s.x; o[50] += s.y; o[51] += s.z; o[52] += s.damage; o[53] += s.life; }
    o[54] = static_cast<double>(h.nav_queue()); o[55] = static_cast<double>(h.nav_stats().queries); o[56] = static_cast<double>(h.nav_stats().success); o[57] = static_cast<double>(h.nav_stats().failed);
    o[58] = static_cast<double>(h.nav_stats().shared); o[59] = h.nav_active_expanded();
    const auto projectiles = a.projectiles();
    o[60] = static_cast<double>(projectiles.size());
    for(const auto& q : projectiles) o[61] += q.x + q.y + q.z;
    o[62] = static_cast<double>(h.corpses().size()); o[63] = h.decision_accumulator();
}

int replay(const std::string& assets_root, const std::string& map, const std::string& fixture) {
    const auto doc = assets::load_data(assets_root + "/maps/" + map + "/map.cadata");
    const physics::CollisionWorld world(doc.at("compiled"));
    nav::NavGraph graph(world);
    std::ifstream in(fixture, std::ios::binary);
    if(!in) throw std::runtime_error("cannot open " + fixture);
    Reader r;
    r.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if(r.text(8) != "CATENMY1" || r.u32() != 1) throw std::runtime_error("unsupported enemy fixture");
    const auto scenarios = r.u32(), tape_width = r.u32(), record_width = r.u32();
    if(tape_width != 3 || record_width != kWidth) throw std::runtime_error("unexpected enemy fixture layout");
    const double tolerance = 1e-9;
    std::size_t total_ticks = 0, exact_ticks = 0, failures = 0, max_enemies = 0, max_shots = 0;
    std::uint64_t hash = 1469598103934665603ULL, kills = 0, deaths = 0, routes = 0, shared = 0, draws = 0;
    int highest_wave = 0;
    double worst = 0;
    std::set<int> types;
    for(std::uint32_t s = 0; s < scenarios; ++s) {
        const auto name = r.text(r.u32());
        const auto ticks = r.u32();
        const auto start = r.f64(16);
        const auto tape = r.f64(std::size_t(ticks) * 3), rec = r.f64(std::size_t(ticks) * kWidth);
        Arena arena(world, static_cast<std::uint32_t>(static_cast<std::int32_t>(start[9])));
        arena.spawn(start[0], start[1], start[2], start[3], start[4], start[5]);
        arena.set_health(start[6]);
        arena.set_loadout(static_cast<int>(start[7]), static_cast<int>(start[8]));
        Horde horde(arena, graph);
        const int wave = static_cast<int>(start[10]);
        horde.set_wave(std::max(1, wave - 1), static_cast<int>(start[11]));
        horde.begin_wave(wave);
        double max_err = 0; int max_field = 0; std::size_t max_tick = 0, exact = 0, first_bad = ticks;
        for(std::uint32_t i = 0; i < ticks; ++i) {
            arena.step({static_cast<std::uint32_t>(tape[i * 3]), tape[i * 3 + 1], tape[i * 3 + 2]});
            double got[kWidth];
            snapshot(arena, horde, got);
            for(const auto& b : arena.targets()) types.insert(b.type);
            max_enemies = std::max(max_enemies, arena.targets().size()); max_shots = std::max(max_shots, horde.shots().size()); highest_wave = std::max(highest_wave, horde.wave_number());
            for(const double value : got) { unsigned char bytes[8]; std::memcpy(bytes, &value, 8); for(const auto b : bytes) { hash ^= b; hash *= 1099511628211ULL; } }
            bool same = true;
            for(int k = 0; k < kWidth; ++k) {
                const double want = rec[std::size_t(i) * kWidth + std::size_t(k)], err = std::fabs(got[k] - want);
                if(std::memcmp(&got[k], &want, 8) != 0 && !(got[k] == 0 && want == 0)) same = false;
                if(err > max_err) { max_err = err; max_field = k; max_tick = i; }
                if(err > tolerance && first_bad == ticks) { first_bad = i; std::cout << "  first difference in " << name << " @ tick " << i << ": " << kRecord[k] << " native " << got[k] << ", legacy " << want << '\n'; }
            }
            exact += same;
            if(first_bad != ticks && i > first_bad + 2) break;   // values diverge after the first difference; stop early
        }
        const bool ok = first_bad == ticks;
        failures += !ok;
        total_ticks += ticks; exact_ticks += exact; worst = std::max(worst, ok ? max_err : 0.0);
        kills += arena.frags(); deaths += arena.deaths(); routes += horde.nav_stats().success; shared += horde.nav_stats().shared; draws += arena.rng().calls();
        std::cout << (ok ? "PASS " : "FAIL ") << name << ": " << ticks << " ticks, bit-exact " << exact << ", max |error| " << max_err << " (" << kRecord[max_field] << " @ tick " << max_tick << "), wave " << horde.wave_number()
                  << ", kills " << arena.frags() << ", deaths " << arena.deaths() << ", routes " << horde.nav_stats().success << "/" << horde.nav_stats().queries << " (shared " << horde.nav_stats().shared
                  << "), random draws " << arena.rng().calls() << '\n';
    }
    if(r.at != r.data.size()) throw std::runtime_error("trailing bytes in enemy fixture");
    std::cout << (failures ? "FAIL " : "PASS ") << "legacy enemies " << map << ": " << scenarios << " scenarios, " << total_ticks << " ticks (" << static_cast<double>(total_ticks) / 120.0 << " s), bit-exact ticks " << exact_ticks
              << ", worst |error| " << worst << ", tolerance " << tolerance << "; enemy types seen " << types.size() << ", most enemies " << max_enemies << ", most enemy shots " << max_shots << ", highest wave " << highest_wave
              << ", kills " << kills << ", player deaths " << deaths << ", routes " << routes << " (" << shared << " shared), random draws " << draws << "; native state hash " << std::hex << hash << std::dec << '\n';
    return failures ? 1 : 0;
}
bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }
// Table rules that do not need a map.
void table_tests() {
    const auto& t = enemy_types();
    check(t.size() == 20, "20 enemy types");
    std::set<std::string> names;
    int flyers = 0, ranged = 0, melee = 0;
    for(std::size_t i = 0; i < t.size(); ++i) {
        names.insert(t[i].name);
        check(enemy_type_index(t[i].name) == static_cast<int>(i), std::string("type index of ") + t[i].name);
        check(t[i].hp > 0 && t[i].speed > 0, std::string("health and speed of ") + t[i].name);
        flyers += t[i].flying; ranged += t[i].ranged; melee += !t[i].ranged;
        if(t[i].flying) check(i >= 17 && t[i].ranged && t[i].flying_attack > 0, "flyers are the last three types and shoot");
        if(!t[i].ranged) check(t[i].melee_damage > 0, std::string("melee damage of ") + t[i].name);
        if(t[i].slam) check(!t[i].ranged, "slam types fight in melee");
    }
    check(names.size() == 20 && flyers == 3 && ranged + melee == 20, "unique names, three flyers");
    check(enemy_type_index("no such enemy") == -1, "unknown type name");
    check(std::string(t[0].name) == "grunt" && std::string(t[17].name) == "skyprobe" && std::string(t[18].name) == "skyraider" && std::string(t[19].name) == "skyjuggernaut", "ENEMY_TYPES order");
    // enemyAttackProgress: wind-up maps to 0 .. .72, recovery to .72 .. 1, idle to -1
    Target b; b.type = 0;
    check(attack_progress(b) == -1, "idle: no attack progress");
    b.attack_total = 1; b.attack_wind = 1; check(near(attack_progress(b), 0), "wind-up start");
    b.attack_wind = .5; check(near(attack_progress(b), .36), "wind-up half way");
    b.attack_wind = 0; b.attack_recover = .5; b.attack_recover_total = 1; check(near(attack_progress(b), .86), "recovery half way");
    b.attack_recover = 0; check(attack_progress(b) == -1, "attack over");
}
// Rules of waves, spawning, attacks and deaths on a real map (Helios Crucible).
void map_tests(const std::string& assets_root) {
    const auto doc = assets::load_data(assets_root + "/maps/helios-crucible/map.cadata");
    const physics::CollisionWorld world(doc.at("compiled"));
    nav::NavGraph graph(world);
    const auto& sp = world.spawns().at(0);
    const auto& types = enemy_types();
    auto fresh = [&](Arena& arena) { arena.spawn(sp.x, sp.y, sp.z, sp.a, 0, 100); arena.set_loadout(0, 1); };
    // ---- wave sizes, upfront batch, forced flyers, scaling (beginWave) ----
    {
        Arena arena(world, 11); fresh(arena);
        Horde horde(arena, graph); arena.attach(&horde);
        int total = 10;
        for(int wave = 1; wave <= 12; ++wave) {
            if(wave > 1) total = static_cast<int>(std::ceil(total * 1.25));
            if(wave > 1) horde.set_wave(wave - 1, [&] { int t = 10; for(int w = 2; w < wave; ++w) t = static_cast<int>(std::ceil(t * 1.25)); return t; }());   // the previous wave's total
            horde.begin_wave(wave);
            check(horde.wave_number() == wave && horde.wave_total() == total, "wave " + std::to_string(wave) + " has " + std::to_string(total) + " enemies");
            const int upfront = std::min(total, std::min(18, 10 + wave / 2));
            check(static_cast<int>(arena.targets().size()) == upfront && horde.wave_spawned() == upfront, "upfront batch of wave " + std::to_string(wave));
            check(near(horde.scale(), std::pow(1.05, wave - 1)), "scale 1.05^(wave-1)");
            check(arena.targets()[0].type == 17, "every wave opens with a skyprobe");
            if(wave >= 3) check(arena.targets()[1].type == 18, "from wave 3 the second enemy is a skyraider");
            if(wave >= 5) check(arena.targets()[2].type == 19, "from wave 5 the third enemy is a skyjuggernaut");
            for(const auto& b : arena.targets()) {
                const auto& def = types[static_cast<std::size_t>(b.type)];
                check(near(b.hp, def.hp * horde.scale()) && near(b.max_hp, b.hp) && near(b.speed, def.speed * horde.scale()) && near(b.damage_scale, horde.scale()), "health, speed and damage scale with the wave");
                check(b.flying == def.flying && b.cls == def.hit_class, "flying flag and hit class from the table");
                check(b.cool >= 1.15 && b.cool < 2.5 && b.path_cooldown >= 0 && b.path_cooldown < .6, "first attack after 1.15 .. 2.5 s");
            }
        }
    }
    // ---- spawning during a wave: one every 0.2 s, never more than 20 alive; spawn distance ----
    {
        Arena arena(world, 12); fresh(arena);
        Horde horde(arena, graph); arena.attach(&horde);
        horde.set_wave(11, 52); horde.begin_wave(12);   // 65 enemies, 16 upfront
        check(horde.wave_total() == 65 && arena.targets().size() == 16, "wave 12: 65 enemies, 16 upfront");
        std::vector<int> spawn_ticks;
        for(int i = 0; i < 240; ++i) {
            const int before = horde.wave_spawned();
            arena.set_health(100);
            arena.step({0, 0, 0});
            if(horde.wave_spawned() != before) spawn_ticks.push_back(i);
            check(arena.targets().size() <= 20, "at most 20 enemies alive");
        }
        check(horde.wave_spawned() == 20 && spawn_ticks.size() == 4, "four more spawned until 20 are alive");
        for(std::size_t k = 1; k < spawn_ticks.size(); ++k) check(spawn_ticks[k] - spawn_ticks[k - 1] == 24 || spawn_ticks[k] - spawn_ticks[k - 1] == 25, "spawns 0.2 s apart");
    }
    {
        Arena arena(world, 13); fresh(arena);
        Horde horde(arena, graph); arena.attach(&horde);
        horde.begin_wave(1);
        const auto& p = arena.player().state();
        for(const auto& b : arena.targets()) check(std::hypot(b.x - p.x, b.y - p.y) >= 29.999, "enemies spawn at least 30 m away");
        // 30 Hz decisions inside the 120 Hz tick
        int decisions = 0;
        for(int i = 0; i < 120; ++i) { const double before = horde.decision_accumulator(); arena.step({0, 0, 0}); decisions += horde.decision_accumulator() < before; }
        check(decisions == 30, "30 AI decisions per second");
    }
    // ---- the wave ends when every enemy is dead; the next starts 3.5 s later ----
    {
        Arena arena(world, 14); fresh(arena);
        Horde horde(arena, graph); arena.attach(&horde);
        horde.begin_wave(1);
        int ticks = 0;
        while(horde.wave_kills() < horde.wave_total() && ticks < 2000) { arena.set_health(100); arena.step({key_fixture_kill, 0, 0}); ++ticks; }
        check(horde.wave_kills() == 10 && arena.targets().empty() && arena.frags() == 10, "ten kills end wave 1");
        check(near(horde.wave_cooldown(), 3.5), "3.5 s between waves");
        check(horde.corpses().empty() && horde.deaths().size() == 1 && horde.deaths()[0].gibbed, "9999 damage gibs: no body");
        int wait = 0;
        while(horde.wave_number() == 1 && wait < 1000) { arena.set_health(100); arena.step({0, 0, 0}); ++wait; }
        check(horde.wave_number() == 2 && horde.wave_total() == 13 && (wait == 420 || wait == 421), "wave 2 (13 enemies) after 3.5 s");
    }
    // ---- an ordinary kill leaves a body for 6.88 s (fall .78 + stay 4.5 + sink 1.6) ----
    {
        Arena arena(world, 15); fresh(arena);
        Horde horde(arena, graph); arena.attach(&horde);
        horde.begin_wave(1);
        const auto& p = arena.player().state();
        auto& b = horde.stage_enemy(0, p.x + 4, p.y, p.z, 0);
        const auto uid = b.uid;
        const std::size_t index = arena.targets().size() - 1;
        arena.damage_target(index, arena.targets()[index].hp, arena.targets()[index].z + 1, 0);
        check(horde.deaths().size() == 1 && !horde.deaths()[0].gibbed && horde.deaths()[0].uid == uid && horde.corpses().size() == 1, "a kill to 0 hp leaves a body");
        check(arena.frags() == 1 && horde.wave_kills() == 1, "the kill counts for the wave");
        int alive = 0;
        while(!horde.corpses().empty() && alive < 2000) { arena.set_health(100); arena.step({0, 0, 0}); ++alive; }
        check(alive >= 825 && alive <= 827, "the body stays 6.88 s");
    }
    // ---- melee: a fiend next to the player strikes for 17 x scale ----
    {
        Arena arena(world, 16); fresh(arena);
        Horde horde(arena, graph); arena.attach(&horde);
        horde.set_wave(3, 17); horde.begin_wave(4);
        arena.clear_targets();
        const auto& p = arena.player().state();
        auto& f = horde.stage_enemy(2, p.x + std::cos(p.a) * 1.0, p.y + std::sin(p.a) * 1.0, p.z, 0);
        f.cool = 0;
        double taken = 0; bool wound_up = false;
        for(int i = 0; i < 240 && taken == 0; ++i) { arena.step({0, 0, 0}); taken += arena.events().player_damage; wound_up = wound_up || (!arena.targets().empty() && arena.targets()[0].attack_wind > 0); }
        check(wound_up, "the strike is wound up first");
        check(near(taken, 17 * std::pow(1.05, 3), 1e-9), "fiend melee damage 17 x 1.05^3");
    }
    // ---- ranged attacks: shots with the damage of their kind, at most 105 in flight ----
    {
        Arena arena(world, 17); fresh(arena);
        Horde horde(arena, graph); arena.attach(&horde);
        horde.begin_wave(1);
        std::set<double> damages;
        std::size_t most = 0;
        for(int i = 0; i < 1800; ++i) {
            arena.set_health(100);
            arena.step({0, 0, 0});
            most = std::max(most, horde.shots().size());
            for(const auto& s : horde.shots()) {
                const auto& def = types[static_cast<std::size_t>(s.type)];
                const double want = def.flying ? def.flying_attack : s.kind == ShotKind::fireball ? 4 : s.kind == ShotKind::voidorb ? 19 : s.kind == ShotKind::spike ? 6 : 9;
                check(near(s.damage, want), std::string("shot damage of ") + def.name);
                check(def.ranged && s.kind == def.shot, "only ranged types shoot, with their own projectile");
                damages.insert(s.damage);
            }
        }
        check(most > 0 && most <= static_cast<std::size_t>(kMaxEnemyShots), "enemies shoot; never more than 105 shots");
    }
    // ---- the player's death: the wave restarts softly at wave 1, enemy shots are cleared ----
    {
        Arena arena(world, 18); fresh(arena);
        Horde horde(arena, graph); arena.attach(&horde);
        horde.set_wave(3, 17); horde.begin_wave(4);
        bool died = false;
        for(int i = 0; i < 10 && !died; ++i) { arena.step({key_fixture_hurt, 0, 0}); died = arena.deaths() > 0; }
        check(died, "35 damage per tick kills the player");
        check(horde.shots().empty(), "enemy shots are cleared at the respawn");
        arena.step({0, 0, 0});
        check(horde.wave_number() == 1 && horde.wave_total() == 10 && horde.wave_spawned() == 3 && arena.targets().size() == 3, "soft restart: wave 1, three enemies at once");
        check(near(horde.wave_spawn_timer(), .18 - 1.0 / 120, 1e-12) || near(horde.wave_spawn_timer(), .18, 1e-12), "the rest follow 0.18 s later");
        check(arena.health() == 100 && near(arena.respawn_grace(), 2.5 - 1.0 / 120, 1e-9), "respawned with full health and 2.5 s of protection");
    }
}
void unit_tests(const char* assets_root) {
    table_tests();
    if(assets_root) map_tests(assets_root);
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if(argc == 1 || (argc == 3 && std::string(argv[1]) == "--units")) {
            unit_tests(argc == 3 ? argv[2] : nullptr);
            std::cout << "PASS enemy unit tests" << (argc == 3 ? " (tables and Helios Crucible)" : " (tables only; --units <assets/n1> adds the map tests)") << ": " << checks << " checks\n";
            return 0;
        }
        if(argc == 4) return replay(argv[1], argv[2], argv[3]);
        std::cerr << "usage: cat_arena_enemy_tests [--units <assets/n1>] | <assets/n1> <map-id> <enemy fixture>\n";
        return 2;
    } catch(const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
