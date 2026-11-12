#pragma once
// N5 weapon presentation: first-person viewmodel pose, muzzle flash, tracers, beams, impacts,
// explosions, marks, projectile sprites and pickup glows. GPU/SDL independent: it turns the events
// of combat::Arena into quads for the effect renderer and a camera for the weapon model.
//
// A port of the legacy weapon-fx.js (legacy/index.html 10800-11177: WFX_DEF, WFX), the effect
// batcher fx-renderer.js (2638-2765: FXR sprite / beam / quad) and the viewmodel camera of
// renderGun3D (14484-14571). It never changes gameplay state. Cosmetic randomness (spark directions,
// flash rotation, recoil jitter ...) uses its own generator, so the gameplay random stream of the
// Arena is the same with or without presentation.
// Verified against the unmodified legacy game: tools/n5/legacy_combat.cjs --fx, tests/fx_tests.cpp.
// N5.1: Frame::lights, reduced with select_lights, light the map (renderer: FrameView::lights).
// Not ported: blood/gib effects of dying enemies (N6), remote players' shots (N8).
#include "cat/combat.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cat::fx {
#include "cat/generated/weapon_fx_tables.inc"

// FXR camera: tan_half is the VERTICAL half-angle tangent (the legacy `tanHalf` uniform).
struct Camera { double x{}, y{}, z{}; std::array<double, 3> forward{1, 0, 0}, right{0, 1, 0}, up{0, 0, 1}; double tan_half{1}, aspect{1}; };
Camera make_camera(const physics::ViewCamera& view, double horizontal_fov_deg, double fov_scale, double aspect);

// FXR vertex (12 floats): position | u v tile flags | premultiplied-colour inputs r g b a | additive.
// flags: 1 repeat U, 2 repeat V, 4 white-hot core.
struct Vertex { float x, y, z, u, v, tile, flags, r, g, b, a, add; };
struct Batch {
    std::size_t max_quads{};
    bool sorted{};
    std::vector<Vertex> vertices;      // 4 per quad, insertion order
    std::vector<float> keys;           // squared distance to the camera (sorted batches)
    std::uint32_t dropped{};
    std::size_t quads() const { return vertices.size() / 4; }
    void clear() { vertices.clear(); keys.clear(); dropped = 0; }
    // Triangle indices: insertion order, or back to front for a sorted batch (as FXR.draw).
    std::vector<std::uint32_t> indices() const;
};
struct Light { double x, y, z, radius, r, g, b; };
struct GunView {
    bool visible{};
    int weapon{};
    Camera camera;                     // viewmodel camera (model space of the weapon mesh)
    std::array<double, 3> glow{};      // muzzle-flash light on the weapon (gunGlow)
};
struct Frame {
    Batch mark{640, false, {}, {}, 0}, main{3600, true, {}, {}, 0}, gun{48, false, {}, {}, 0};
    Camera camera;
    GunView gun_view;
    std::vector<Light> lights;         // Q3R.addLight calls of this frame (max 64)
    std::vector<float> atlas;          // N5.1 renderGeometry(): medkit billboards, 12 floats per vertex (world-atlas layout)
};
struct MarkOptions { std::optional<double> rot; double add{}; double life{12}; bool glow{}; };   // addMark options
struct Offsets { double back{}, lift{}, pitch{}, roll{}, yaw{}, drop{}; };   // WFX.viewmodelOffsets

// Q3R.uploadLights: the 16 lights that matter most to a view at (cx, cy, cz), best first. Score =
// radius * (r + g + b) / (1 + 0.15 * distance); equal scores keep their order.
std::vector<Light> select_lights(const std::vector<Light>& lights, double cx, double cy, double cz);

// billboard() of the legacy dynamic geometry: an upright quad that turns with the view yaw only.
// 6 vertices x 12 floats (position, normal 0 0 1, colour, uv, atlas layer) appended to `out`.
inline constexpr int kAtlasMedkit = 11;   // MATERIAL.MEDKIT
void billboard(std::vector<float>& out, double x, double y, double z, double w, double h, int material, double yaw, const std::array<double, 3>& tint = {1, 1, 1});

// Quad writers (FXR.quad4 / sprite / beam / quadAxes); `cam` orients sprites and beams.
bool quad4(Batch& b, const Camera& cam, const std::array<std::array<double, 3>, 4>& p, double u0, double v0, double u1, double v1, int tile, int flags, double r, double g, double bl, double a, double add);
bool sprite(Batch& b, const Camera& cam, double x, double y, double z, double w, double h, double rot, int tile, double r, double g, double bl, double a, double add = 1, int flags = 4);
bool beam(Batch& b, const Camera& cam, double ax, double ay, double az, double bx, double by, double bz, double width, int tile, double r, double g, double bl, double a, double add = 1,
          double u0 = 0, double u1 = 1, int flags = 4);

class EnemyFx;
class WeaponFx {
    friend class EnemyFx;   // N6: enemy effects are added to the same entity / mark lists
public:
    WeaponFx(const physics::CollisionWorld& world, std::uint32_t seed);
    void clear();
    // Once per simulation tick, right after Arena::step(): starts the effects of the step's events
    // (WFX.fire, WFX.explosion) and advances every effect by one tick (WFX.update).
    void tick(const combat::Arena& arena, double horizontal_fov_deg, double aspect);
    // The two halves of tick(), for callers that interleave other effects with the step's events
    // (fx::EnemyFx): start the effects of one event, then advance every effect by one tick.
    void start_fire(const combat::Arena& arena, double horizontal_fov_deg, double aspect);
    void start_blast(const combat::BlastEvent& b);
    void advance(const combat::Arena& arena);
    // Once per rendered frame: fills the batches, the lights and the weapon view.
    void build(const combat::Arena& arena, double horizontal_fov_deg, double aspect, Frame& out);

    // Pieces, public for tests.
    void fire(const combat::Arena& arena, int index, const std::vector<combat::ShotEvent>& shots, const combat::FireContext& context, double horizontal_fov_deg, double aspect);
    void explosion(double x, double y, double z, double power, bool rift);
    Offsets viewmodel_offsets(int index, double reloading);
    static std::array<double, 3> muzzle_world(const Camera& cam, int weapon);
    struct SurfacePoint { double x, y, z, nx, ny, nz; };
    std::optional<SurfacePoint> surface_at(double ex, double ey, double ez, double dx, double dy, double dz) const;

    std::size_t entities() const { return le_.size(); }
    std::size_t tracers() const { return tracers_.size(); }
    std::size_t rails() const { return rails_.size(); }
    std::size_t marks() const { return marks_.size(); }
    std::size_t pending_brass() const { return pending_.size(); }
    std::array<double, 4> recoil() const { return {recoil_.back, recoil_.climb, recoil_.roll, recoil_.yaw}; }
    const combat::Rng& rng() const { return rng_; }
    combat::Rng& random_stream() { return rng_; }
private:
    struct Entity {   // localEntity
        double t{}, life{1}, x{}, y{}, z{}, vx{}, vy{}, vz{}, grav{}, drag{}, s0{.2}, s1{.2}, a0{1}, a1{}, rot{}, rot_v{}, r{1}, g{1}, b{1}, add{1};
        int flags{4}, tile{}, frames{};
        double stretch{}, bounce{}, floor{}, floor_t{}, fade_in{};
        bool has_floor{}, blood{}, has_light{};
        std::array<double, 4> light{};
    };
    struct Mark { double x, y, z, ax, ay, az, bx, by, bz; int tile; double r, g, b, a, add, t, life; bool glow; };
    struct Tracer { double x0, y0, z0, x1, y1, z1, t, dur; int def; bool hit, enemy; std::array<double, 3> dir; bool show; double spin, smoke_t; bool impact_ok; };
    struct Rail { double ax, ay, az, bx, by, bz, t, life, phase; std::array<double, 3> col; };
    struct Pending { double t; BrassKind type; double scale; };
    struct Flash { double t, life; int index; double rot, scale; };
    struct Spring { double back{}, vb{}, climb{}, vc{}, roll{}, vr{}, yaw{}, vy{}, k{200}; };
    struct ProjectileFx { double st{}; bool has_off{}; std::array<double, 3> off{}; double off_t{}; bool alive{}; };

    double random() { return rng_.next(); }
    double rnd(double a = 1) { return (rng_.next() * 2 - 1) * a; }
    Entity& le(const Entity& e);
    void sparks(double x, double y, double z, double n, const std::array<double, 3>& col, double speed = 5, double life = .35, double nx = 0, double ny = 0, double nz = 1);
    void puff(double x, double y, double z, double size, double life, const std::array<double, 3>& col, double alpha, int tile, double rise);
    void add_mark(double x, double y, double z, double nx, double ny, double nz, double size, int tile, const std::array<double, 3>& col, double alpha, const MarkOptions& opt = {});
    void floor_mark(double x, double y, double z, double size, int tile, const std::array<double, 3>& col, double alpha, const MarkOptions& opt);
    void blood(double x, double y, double z, double dx, double dy, double dz, double amount);
    void impact(int def, double hx, double hy, double hz, double dx, double dy, double dz, bool enemy, double power);
    void spawn_brass(const combat::Arena& arena, BrassKind type, double scale);
    void update(const combat::Arena& arena, double dt);

    const physics::CollisionWorld* world_;
    combat::Rng rng_;
    std::vector<Entity> le_;
    std::vector<Mark> marks_;
    std::vector<Tracer> tracers_;
    std::vector<Rail> rails_;
    std::vector<Pending> pending_;
    Spring recoil_;
    std::optional<Flash> flash_;
    double switch_t_{9}, rail_charge_{}, bolt_until_{-1}, bolt_range_{};
    int last_index_{-1}, bolt_def_{-1};
    std::uint64_t bolt_fx_{};
    std::array<double, 3> glow_{};
    std::unordered_map<std::uint32_t, ProjectileFx> projectile_fx_;
};
}  // namespace cat::fx
