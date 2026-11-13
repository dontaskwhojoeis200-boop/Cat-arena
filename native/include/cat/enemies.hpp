#pragma once
// N6 enemies: the 20 legacy enemy types, waves and scaling, 30 Hz decisions inside the 120 Hz tick,
// ground navigation on the layered graph with sliced route searches, flyers with swept 3-D steering,
// attack state machine, enemy projectiles, melee, burn, death bookkeeping and the wave restart after
// a player death. GPU/SDL independent and deterministic.
//
// A port of the legacy enemy code: spawnBot / beginWave / tickWaves (legacy/index.html 11443-11500),
// the enemy part of update() (12236-12305), enemyAttack (11938-11979), botNavigation and the NAV3
// route jobs (6342-6571), updateFlyer (6582-6656), enemyStartDeath / updateDyingEnemies (10724-10740).
// The Horde works on the targets of a combat::Arena (an enemy is a combat::Target with type >= 0),
// so every weapon rule of N5 applies to enemies unchanged. Arena::step calls it at the places the
// legacy update() runs the same code.
// Verified against the unmodified legacy game: tools/n6/legacy_enemies.cjs, tests/enemy_tests.cpp.
//
// One deliberate difference: the legacy stops a tick's route search after 110 node expansions OR
// 1.8 ms of wall time, whichever comes first, and fills adjacency lists in idle ticks by wall time.
// Wall time makes the same inputs give different routes on different machines. Here the budget is
// the 110 expansions only (the recorder pins the legacy clock the same way).
#include "cat/combat.hpp"
#include "cat/navigation.hpp"
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace cat::combat {
inline constexpr std::size_t kEnemyTypeCount = 20;
enum class ShotKind : std::uint8_t { shot, spike, fireball, voidorb };
struct EnemyType {
    const char* name;
    double hp, speed;                 // base health and speed (ENEMY_STATS, enemyRunningSpeed / FLYING_STATS)
    bool flying;
    double flying_attack;             // damage of a flyer's shot
    TargetClass hit_class;            // enemyHitScale
    bool ranged, slam, orb, warden;   // RANGED_TYPES, ENEMY_SLAM_TYPES, ENEMY_ORB_TYPES, ENEMY_WARDEN_TYPES
    ShotKind shot;
    int nav_profile;                  // 0 light, 1 standard, 2 heavy (nav3Profile)
    double turn_rate, stride;         // enemyTurnRate; animation phase per metre
    double melee_damage;
    std::array<float, 3> palette;     // ENEMY_PALETTES
};
const std::array<EnemyType, kEnemyTypeCount>& enemy_types();
int enemy_type_index(const char* name);   // -1 when unknown

inline constexpr double kEnemySlowSpeed = 4.617, kEnemyStandardSpeed = 8.80875, kEnemyFastSpeed = 10.3275;
inline constexpr int kNavWorkPerSlice = 110;        // NAV3_WORK_PER_SLICE: open-list pops per tick
inline constexpr int kMaxEnemies = 20, kMaxEnemyShots = 105;

struct EnemyShot { double x{}, y{}, z{}, vx{}, vy{}, vz{}, life{}; ShotKind kind{ShotKind::shot}; int type{}; double damage{}, trail{}; std::uint32_t id{}; std::uint32_t target_key{}; };   // target_key: N8 teammate aimed at (0 local)
// A dead enemy while its death animation plays (enemyStartDeath). `gibbed` deaths leave no corpse.
struct Corpse { int type{}; double x{}, y{}, z{}, floor_z{}, vz{}, a{}, t{}; bool flyer{}; std::uint32_t id{}; };
struct ShotImpact { double x, y, z; ShotKind kind; int type; bool player; double vx, vy, vz; std::uint32_t seq; };
// An enemy that died during the last Arena::step, as it was at that moment (killBot -> enemyDeathFx):
// what presentation needs for the death animation or the gibs. `corpse_id` is 0 for a gibbed body.
struct DeathEvent {
    std::uint32_t uid{}, corpse_id{};
    int type{};
    double x{}, y{}, z{}, a{}, hp{}, hit_flash{}, attack_progress{-1};
    double now{}, player_x{}, player_y{}, player_z{};   // clock and player position when it happened
    bool flying{}, gibbed{};
    std::uint32_t seq{};                                 // Arena::next_event_seq
};
double attack_progress(const Target& b);
const char* shot_kind_name(ShotKind k);   // 'shot', 'spike', 'fireball', 'voidorb'
std::optional<ShotKind> shot_kind_from(const std::string& name);
// N8 guest: the host's world snapshot ('world' message) in simulation terms.
struct PuppetBot { std::uint32_t uid{}; int type{}; double x{}, y{}, z{}, a{}, hp{}, max_hp{100}, phase{}, walk{}, hit{}, wind{}, total_wind{}, recover{}, total_recover{}; };
struct PuppetShot { double x{}, y{}, z{}, vx{}, vy{}, vz{}, life{}; ShotKind kind{ShotKind::shot}; int type{-1}; };
struct WorldState {
    int wave{1}, total{10}, kills{}, spawned{};
    double cooldown{-1};
    std::vector<PuppetBot> bots;
    std::vector<PuppetShot> shots;
};   // enemyAttackProgress: -1 idle, 0 .. .72 wind-up, .72 .. 1 recovery
struct NavStats { std::uint64_t queries{}, success{}, failed{}, shared{}; };

class Horde {
public:
    Horde(Arena& arena, nav::NavGraph& graph);
    // ---- waves ----
    void set_wave(int number, int total) { wave_number_ = number; wave_total_ = total; }
    void begin_wave(int next = 1, bool soft_start = false);   // beginWave
    void spawn_enemy(int forced_type = -1);                    // spawnBot
    double scale() const;                                      // waveEnemyScale: 1.05 ^ (wave - 1)
    int wave_number() const { return wave_number_; }
    int wave_total() const { return wave_total_; }
    int wave_spawned() const { return wave_spawned_; }
    int wave_kills() const { return wave_kills_; }
    double wave_cooldown() const { return wave_cooldown_; }
    double wave_spawn_timer() const { return wave_spawn_timer_; }
    int remaining() const { return std::max(0, wave_total_ - wave_kills_); }
    std::uint32_t waves_begun() const { return waves_begun_; }   // beginWave calls so far (presentation: the wave announcement)
    // ---- state ----
    const std::vector<EnemyShot>& shots() const { return shots_; }
    const std::vector<Corpse>& corpses() const { return corpses_; }
    const std::vector<ShotImpact>& impacts() const { return impacts_; }   // of the last tick
    const std::vector<DeathEvent>& deaths() const { return deaths_; }     // of the last tick, in order
    void begin_step() { deaths_.clear(); }
    const NavStats& nav_stats() const { return nav_stats_; }
    std::size_t nav_queue() const { return queue_.size(); }
    int nav_active_expanded() const { return job_ ? static_cast<int>(job_->expanded) : -1; }
    double decision_accumulator() const { return ai_acc_; }
    // ---- N8 guest: no AI, waves or navigation; enemies follow the host's snapshots ----
    void set_puppet(bool puppet) { puppet_ = puppet; }
    bool puppet() const { return puppet_; }
    void queue_world(WorldState w) { pending_world_ = std::move(w); }   // applied inside the next step
    // ---- called by Arena::step, in the legacy update() order ----
    bool decision_tick(double dt);
    void update(double dt, bool decision);     // enemies, route jobs, enemy projectiles
    void update_corpses(double dt);
    void tick_waves(double dt);
    void enemy_died(const Target& dead);       // killBot: death presentation bookkeeping
    void count_kill() { ++wave_kills_; }
    void purge_shots_after_loop() { pending_purge_ = true; }
    void player_respawned();
    // Staged scenes (cat_arena --enemy-lineup, tests). stage_enemy spawns like spawnBot and then puts the
    // enemy at the given place with an empty route (the legacy lineup tool does the same); the caller
    // may set further fields (attack timers, hit flash) on the returned target. stage_corpse adds a body
    // `t` seconds into its death animation and returns its id.
    Target& stage_enemy(int type, double x, double y, double z, double a);
    void stage_wave(int number, int total, int spawned, int kills) { wave_number_ = number; wave_total_ = total; wave_spawned_ = spawned; wave_kills_ = kills; wave_cooldown_ = -1; }
    std::uint32_t stage_corpse(int type, double x, double y, double z, double floor_z, double a, double t);
    // Pieces, public for tests.
    bool line_of_sight(const Target& b) const;                 // enemy3DHasLineOfSight (towards enemyTargetFor(b))
    bool flyer_free(double x, double y, double z, double r, double h = 1.15) const;   // nav3FlyerFree
    static const nav::Profile& profile(int index);
private:
    struct Request { std::uint32_t uid, epoch; int profile; };
    struct Job { std::uint32_t uid, epoch; int profile; std::uint32_t start, goal, expanded; double tx, ty, tz; };
    struct Shared { std::uint32_t goal; int profile; std::uint32_t epoch; double t; std::uint32_t start; std::vector<std::uint32_t> nodes; };
    Target* find(std::uint32_t uid);
    int navigate(Target& b, double dt, bool decision, int search_left);   // botNavigation
    double move(Target& b, double tx, double ty, double tz, double budget, const nav::Profile& p);   // nav3Move
    void update_flyer(Target& b, double dt);
    bool flyer_sweep(double x, double y, double z, double nx, double ny, double nz, double r) const;
    void attack(Target& b);                    // enemyAttack
    void process_jobs();                       // processNavJobs
    bool start_job();                          // nav3StartJob; false when nothing is left to search this slice
    void finish_job(bool success);             // nav3Finish
    void apply_route(Target& b, const std::vector<std::uint32_t>& chain, double tx, double ty, double tz, const nav::Profile& p);
    std::optional<std::vector<std::uint32_t>> shared_route(std::uint32_t start, std::uint32_t goal, int profile) const;
    void reset_jobs();
    void face(Target& b, double desired, double dt);

    Arena* arena_;
    nav::NavGraph* graph_;
    int wave_number_{1}, wave_total_{10}, wave_spawned_{}, wave_kills_{};
    double wave_cooldown_{-1}, wave_spawn_timer_{}, ai_acc_{};
    bool wave_reset_pending_{}, pending_purge_{};
    int search_pool_{};
    std::uint32_t waves_begun_{};
    std::uint32_t serial_{}, epoch_{}, next_shot_id_{1}, next_corpse_id_{1};
    std::vector<EnemyShot> shots_;
    std::vector<Corpse> corpses_;
    std::vector<ShotImpact> impacts_;
    std::vector<DeathEvent> deaths_;
    std::deque<Request> queue_;
    std::optional<Job> job_;
    std::vector<Shared> routes_;               // NAV3_ROUTES ring (24 entries, 0.75 s)
    std::size_t route_slot_{};
    NavStats nav_stats_;
    // N8
    bool puppet_{};
    std::optional<WorldState> pending_world_;
    void puppet_update(double dt);                             // netGuestInterp + the guest branches of update()
    void apply_world(const WorldState& w);
    bool line_of_sight(const Target& b, const Arena::Aim& t) const;
};
}  // namespace cat::combat
