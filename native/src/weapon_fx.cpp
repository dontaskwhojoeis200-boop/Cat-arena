// N5 weapon presentation: see cat/weapon_fx.hpp. Statement order and the order of random draws
// follow the legacy functions named in the comments (weapon-fx.js, fx-renderer.js, renderGun3D).
#include "cat/weapon_fx.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace cat::fx {
namespace {
using physics::js_hypot;
using physics::js_hypot3;
constexpr double kPi = 3.141592653589793;
constexpr std::size_t kMaxEntities = 420, kMaxMarks = 200, kMaxLights = 64;
constexpr double kDetail = 1;            // Q3R.S.effects (effects-detail setting), default 1
constexpr double kRailTrailTime = .6;    // Q3R.S.railTrailTime
constexpr std::array<double, 3> kWhite{1, 1, 1};
double js_round(double v) { return std::floor(v + .5); }
double or1(double v) { return v != 0 ? v : 1; }   // JS `x || 1`
const FxDef& def_of(int i) { return kFxDef[static_cast<std::size_t>(i)]; }
void add_light(Frame& f, double x, double y, double z, double radius, double r, double g, double b) { if(f.lights.size() < kMaxLights) f.lights.push_back({x, y, z, radius, r, g, b}); }
}  // namespace

Camera make_camera(const physics::ViewCamera& v, double fov, double scale, double aspect) {
    return {v.x, v.y, v.z, v.forward, v.right, v.up, std::tan((fov * scale) * kPi / 360) / aspect, aspect};
}

std::vector<Light> select_lights(const std::vector<Light>& lights, double cx, double cy, double cz) {
    struct Scored { double score; const Light* light; };
    std::vector<Scored> order;
    order.reserve(lights.size());
    for(const auto& l : lights) order.push_back({(l.radius * (l.r + l.g + l.b)) / (1 + js_hypot3(l.x - cx, l.y - cy, l.z - cz) * .15), &l});
    std::stable_sort(order.begin(), order.end(), [](const Scored& a, const Scored& b) { return a.score > b.score; });   // Array.prototype.sort is stable
    std::vector<Light> out;
    for(const auto& s : order) { if(out.size() >= 16) break; out.push_back(*s.light); }
    return out;
}

void billboard(std::vector<float>& out, double x, double y, double z, double w, double h, int material, double yaw, const std::array<double, 3>& tint) {   // billboard + quad (5190, 5395)
    const double rx = -std::sin(yaw) * w * .5, ry = std::cos(yaw) * w * .5;
    const double p[4][3] = {{x - rx, y - ry, z}, {x + rx, y + ry, z}, {x + rx, y + ry, z + h}, {x - rx, y - ry, z + h}};
    const double uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for(const int i : {0, 1, 2, 0, 2, 3})
        for(const double v : {p[i][0], p[i][1], p[i][2], 0.0, 0.0, 1.0, tint[0], tint[1], tint[2], uv[i][0], uv[i][1], static_cast<double>(material)}) out.push_back(static_cast<float>(v));
}

// ---- FXR quad writers ---------------------------------------------------------------------------
std::vector<std::uint32_t> Batch::indices() const {
    const std::size_t n = quads();
    std::vector<std::uint32_t> order(n);
    std::iota(order.begin(), order.end(), 0U);
    if(sorted) std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) { return keys[a] > keys[b]; });   // back to front
    std::vector<std::uint32_t> out;
    out.reserve(n * 6);
    for(const auto q : order) for(const std::uint32_t k : {0U, 1U, 2U, 0U, 2U, 3U}) out.push_back(q * 4 + k);
    return out;
}
bool quad4(Batch& b, const Camera& cam, const std::array<std::array<double, 3>, 4>& p, double u0, double v0, double u1, double v1, int tile, int flags, double r, double g, double bl, double a, double add) {
    if(b.quads() >= b.max_quads) { ++b.dropped; return false; }
    const double uv[4][2] = {{u0, v1}, {u1, v1}, {u1, v0}, {u0, v0}};
    for(std::size_t i = 0; i < 4; ++i)
        b.vertices.push_back({static_cast<float>(p[i][0]), static_cast<float>(p[i][1]), static_cast<float>(p[i][2]), static_cast<float>(uv[i][0]), static_cast<float>(uv[i][1]), static_cast<float>(tile),
                              static_cast<float>(flags), static_cast<float>(r), static_cast<float>(g), static_cast<float>(bl), static_cast<float>(a), static_cast<float>(add)});
    if(b.sorted) {
        const double cx = (p[0][0] + p[2][0]) * .5 - cam.x, cy = (p[0][1] + p[2][1]) * .5 - cam.y, cz = (p[0][2] + p[2][2]) * .5 - cam.z;
        b.keys.push_back(static_cast<float>(cx * cx + cy * cy + cz * cz));
    }
    return true;
}
bool sprite(Batch& b, const Camera& cam, double x, double y, double z, double w, double h, double rot, int tile, double r, double g, double bl, double a, double add, int flags) {
    const auto& R = cam.right; const auto& Up = cam.up;
    const double c = std::cos(rot), s = std::sin(rot), hw = w * .5, hh = h * .5;
    const double rx = (R[0] * c + Up[0] * s) * hw, ry = (R[1] * c + Up[1] * s) * hw, rz = (R[2] * c + Up[2] * s) * hw;
    const double ux = (Up[0] * c - R[0] * s) * hh, uy = (Up[1] * c - R[1] * s) * hh, uz = (Up[2] * c - R[2] * s) * hh;
    return quad4(b, cam, {{{x - rx - ux, y - ry - uy, z - rz - uz}, {x + rx - ux, y + ry - uy, z + rz - uz}, {x + rx + ux, y + ry + uy, z + rz + uz}, {x - rx + ux, y - ry + uy, z - rz + uz}}}, 0, 0, 1, 1, tile, flags, r, g, bl, a, add);
}
bool beam(Batch& b, const Camera& cam, double ax, double ay, double az, double bx, double by, double bz, double width, int tile, double r, double g, double bl, double a, double add, double u0, double u1, int flags) {
    double dx = bx - ax, dy = by - ay, dz = bz - az;
    const double len = js_hypot3(dx, dy, dz);
    if(len < 1e-5) return false;
    dx /= len; dy /= len; dz /= len;
    double vx = ax - cam.x, vy = ay - cam.y, vz = az - cam.z;
    double sax = dy * vz - dz * vy, say = dz * vx - dx * vz, saz = dx * vy - dy * vx;
    double k = width * .5 / or1(js_hypot3(sax, say, saz));
    sax *= k; say *= k; saz *= k;
    vx = bx - cam.x; vy = by - cam.y; vz = bz - cam.z;
    double sbx = dy * vz - dz * vy, sby = dz * vx - dx * vz, sbz = dx * vy - dy * vx;
    k = width * .5 / or1(js_hypot3(sbx, sby, sbz));
    sbx *= k; sby *= k; sbz *= k;
    return quad4(b, cam, {{{ax - sax, ay - say, az - saz}, {bx - sbx, by - sby, bz - sbz}, {bx + sbx, by + sby, bz + sbz}, {ax + sax, ay + say, az + saz}}}, u0, 0, u1, 1, tile, flags, r, g, bl, a, add);
}

// ---- effect state -------------------------------------------------------------------------------
WeaponFx::WeaponFx(const physics::CollisionWorld& world, std::uint32_t seed) : world_(&world), rng_(seed) {}
void WeaponFx::clear() {
    le_.clear(); marks_.clear(); tracers_.clear(); rails_.clear(); pending_.clear(); flash_.reset(); bolt_until_ = -1; projectile_fx_.clear();
}
WeaponFx::Entity& WeaponFx::le(const Entity& e) {
    if(le_.size() >= kMaxEntities) le_.erase(le_.begin());
    le_.push_back(e);
    return le_.back();
}
void WeaponFx::sparks(double x, double y, double z, double count, const std::array<double, 3>& col, double speed, double life, double nx, double ny, double nz) {
    const int n = static_cast<int>(js_round(count * kDetail));
    for(int i = 0; i < n; ++i) {
        const double v = speed * (.4 + random());
        const double dx = nx * .8 + rnd(), dy = ny * .8 + rnd(), dz = nz * .8 + rnd() + .4, l = or1(js_hypot3(dx, dy, dz));
        const double lived = life * (.6 + .6 * random());
        le({.life = lived, .x = x, .y = y, .z = z, .vx = dx / l * v, .vy = dy / l * v, .vz = dz / l * v, .grav = 9, .drag = 1.5, .s0 = .035, .s1 = .02, .a0 = 1, .a1 = 0,
            .r = col[0], .g = col[1], .b = col[2], .tile = tile_spark, .stretch = .035});
    }
}
void WeaponFx::puff(double x, double y, double z, double size, double life, const std::array<double, 3>& col, double alpha, int tile, double rise) {
    const double vx = rnd(.15), vy = rnd(.15), rot = random() * 6.28, rot_v = rnd(.8);
    const int pick = static_cast<int>(random() * 3) % 3 * (tile == tile_smoke1 ? 1 : 0);
    le({.life = life, .x = x, .y = y, .z = z, .vx = vx, .vy = vy, .vz = rise, .drag = .8, .s0 = size * .45, .s1 = size, .a0 = alpha, .a1 = 0, .rot = rot, .rot_v = rot_v,
        .r = col[0], .g = col[1], .b = col[2], .add = 0, .flags = 0, .tile = tile + pick, .fade_in = .08});
}
void WeaponFx::add_mark(double x, double y, double z, double nx, double ny, double nz, double size, int tile, const std::array<double, 3>& col, double alpha, const MarkOptions& opt) {   // addMark (cg_marks)
    if(marks_.size() >= kMaxMarks) marks_.erase(marks_.begin());
    double ux = -ny, uy = nx, uz = 0;
    if(std::fabs(nz) > .9) { ux = 1; uy = 0; uz = 0; }
    const double l = js_hypot3(ux, uy, uz);
    ux /= l; uy /= l; uz /= l;
    const double vx = ny * uz - nz * uy, vy = nz * ux - nx * uz, vz = nx * uy - ny * ux;
    const double a = opt.rot ? *opt.rot : random() * 6.28, c = std::cos(a), s = std::sin(a);
    const double ax = ux * c + vx * s, ay = uy * c + vy * s, az = uz * c + vz * s, bx = vx * c - ux * s, by = vy * c - uy * s, bz = vz * c - uz * s;
    marks_.push_back({x + nx * .012, y + ny * .012, z + nz * .012, ax * size * .5, ay * size * .5, az * size * .5, bx * size * .5, by * size * .5, bz * size * .5,
                      tile, col[0], col[1], col[2], alpha, opt.add, 0, opt.life, opt.glow});
}
void WeaponFx::floor_mark(double x, double y, double z, double size, int tile, const std::array<double, 3>& col, double alpha, const MarkOptions& opt) {
    const double f = world_->floor_height(x, y, z + .1);
    if(f < -900 || z - f > 2.2) return;
    add_mark(x, y, f, 0, 0, 1, size, tile, col, alpha, opt);
}
std::optional<WeaponFx::SurfacePoint> WeaponFx::surface_at(double ex, double ey, double ez, double dx, double dy, double dz) const {   // surfaceAt: point + normal from three short traces
    auto trace = [&](double ox, double oy, double oz) -> std::optional<std::array<double, 3>> {
        const double sx = ox - dx * .7, sy = oy - dy * .7, sz = oz - dz * .7, tx = ox + dx * .5, ty = oy + dy * .5, tz = oz + dz * .5;
        const double t = world_->shot_collision_t(sx, sy, sz, tx, ty, tz, .01);
        if(!std::isfinite(t)) return std::nullopt;
        const double L = 1.2, steps = std::max(1.0, std::ceil(L / .19));
        double lo = std::max(0.0, t - 1 / steps), hi = t;
        for(int i = 0; i < 9; ++i) { const double m = (lo + hi) * .5; if(world_->solid_at(sx + (tx - sx) * m, sy + (ty - sy) * m, sz + (tz - sz) * m, .01)) hi = m; else lo = m; }
        return std::array<double, 3>{sx + (tx - sx) * lo, sy + (ty - sy) * lo, sz + (tz - sz) * lo};
    };
    const auto p0 = trace(ex, ey, ez);
    if(!p0) return std::nullopt;
    double ux = -dy, uy = dx, uz = 0;
    if(js_hypot(ux, uy) < .2) { ux = 1; uy = 0; }
    const double l = js_hypot3(ux, uy, uz);
    ux /= l; uy /= l; uz /= l;
    const double vx = dy * uz - dz * uy, vy = dz * ux - dx * uz, vz = dx * uy - dy * ux, e = .07;
    const auto p1 = trace(ex + ux * e, ey + uy * e, ez + uz * e), p2 = trace(ex + vx * e, ey + vy * e, ez + vz * e);
    std::array<double, 3> n{-dx, -dy, -dz};
    if(p1 && p2) {
        const std::array<double, 3> a{(*p1)[0] - (*p0)[0], (*p1)[1] - (*p0)[1], (*p1)[2] - (*p0)[2]}, b{(*p2)[0] - (*p0)[0], (*p2)[1] - (*p0)[1], (*p2)[2] - (*p0)[2]};
        std::array<double, 3> c{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
        const double cl = js_hypot3(c[0], c[1], c[2]);
        if(cl > 1e-5) {
            for(auto& q : c) q = q / cl;
            if(c[0] * dx + c[1] * dy + c[2] * dz > 0) for(auto& q : c) q = -q;
            if(std::fabs(c[2]) > .94) c = {0, 0, c[2] > 0 ? 1.0 : -1.0};   // snap near-vertical normals
            if(-(c[0] * dx + c[1] * dy + c[2] * dz) > .08) n = c;
        }
    }
    return SurfacePoint{(*p0)[0], (*p0)[1], (*p0)[2], n[0], n[1], n[2]};
}
std::array<double, 3> WeaponFx::muzzle_world(const Camera& cam, int weapon) {   // muzzleWorld: where the viewmodel muzzle appears, 0.62 m in front of the eye
    const double tV = cam.tan_half, tH = tV * cam.aspect, d = .62;
    const double nx = 2 * kRefMuzzleX - 1, ny = 1 - 2 * std::max(.5, std::min(.95, kMuzzleHeight[static_cast<std::size_t>(weapon)]));
    const auto& f = cam.forward; const auto& r = cam.right; const auto& u = cam.up;
    return {cam.x + f[0] * d + r[0] * nx * d * tH + u[0] * ny * d * tV, cam.y + f[1] * d + r[1] * nx * d * tH + u[1] * ny * d * tV, cam.z + f[2] * d + r[2] * nx * d * tH + u[2] * ny * d * tV};
}

// ---- impacts ------------------------------------------------------------------------------------
void WeaponFx::blood(double x, double y, double z, double dx, double dy, double dz, double amount) {
    (void)dz;
    const int n = static_cast<int>(std::max(1.0, js_round(2 * amount * kDetail)));
    for(int i = 0; i < n; ++i) {
        const double bx = x + rnd(.08), by = y + rnd(.08), bz = z + rnd(.08), vx = -dx * 1.2 + rnd(1.4), vy = -dy * 1.2 + rnd(1.4), vz = rnd(.6) + .6;
        const double life = .5 + random() * .25, rot = random() * 6.28;
        le({.life = life, .x = bx, .y = by, .z = bz, .vx = vx, .vy = vy, .vz = vz, .grav = 6, .s0 = .18 * amount, .s1 = .42 * amount, .a0 = .95, .a1 = 0, .rot = rot, .r = .8, .g = .12, .b = .1, .add = 0, .flags = 0, .tile = tile_blood});
    }
    if(random() < .55) {
        const double size = .45 + .45 * random() * amount;
        floor_mark(x + dx * .5, y + dy * .5, z, size, tile_mark_blood, {.62, .05, .04}, .85, {.life = 14});
    }
}
void WeaponFx::impact(int index, double hx, double hy, double hz, double dx, double dy, double dz, bool enemy, double power) {
    const auto& def = def_of(index);
    const auto& col = def.color;
    if(enemy) {
        blood(hx, hy, hz, dx, dy, dz, def.kind == ShotKind::pellet ? .6 : (def.kind == ShotKind::rail || def.kind == ShotKind::cannon) ? 1.4 : .9);
        if(def.impact == ImpactKind::plasma || def.impact == ImpactKind::rail || def.impact == ImpactKind::bolt) {
            const double rot = random() * 6.28;
            le({.life = .18, .x = hx, .y = hy, .z = hz, .s0 = .3, .s1 = .7, .a0 = .9, .a1 = 0, .rot = rot, .r = col[0], .g = col[1], .b = col[2], .tile = tile_burst});
        }
        return;
    }
    const auto S = surface_at(hx, hy, hz, dx, dy, dz);
    const double x = S ? S->x : hx, y = S ? S->y : hy, z = S ? S->z : hz, nx = S ? S->nx : -dx, ny = S ? S->ny : -dy, nz = S ? S->nz : -dz;
    const double ox = x + nx * .05, oy = y + ny * .05, oz = z + nz * .05;
    auto light = [&](double life, double r, double g, double b, double radius) { le({.life = life, .x = ox, .y = oy, .z = oz, .s0 = 0, .s1 = 0, .a0 = 0, .a1 = 0, .has_light = true, .light = {r, g, b, radius}}); };
    switch(def.impact) {
    case ImpactKind::spark: {
        const double rot = random() * 6.28;
        le({.life = .09, .x = ox, .y = oy, .z = oz, .s0 = .22, .s1 = .34, .a0 = 1, .a1 = 0, .rot = rot, .r = 1, .g = .85, .b = .6, .tile = tile_burst});
        sparks(ox, oy, oz, 4, {1, .8, .5}, 4.5, .3, nx, ny, nz); puff(ox, oy, oz, .35, .55, {.6, .58, .55}, .32, tile_dust, .25);
        break; }
    case ImpactKind::puff:
        puff(ox, oy, oz, .42, .6, {.62, .58, .52}, .45, tile_dust, .2); sparks(ox, oy, oz, 2, {1, .82, .55}, 4, .25, nx, ny, nz);
        le({.life = .07, .x = ox, .y = oy, .z = oz, .s0 = .18, .s1 = .24, .a0 = .9, .a1 = 0, .r = 1, .g = .85, .b = .6, .tile = tile_spark_dot});
        break;
    case ImpactKind::heavy: {
        const double rot = random() * 6.28;
        le({.life = .12, .x = ox, .y = oy, .z = oz, .s0 = .35, .s1 = .6, .a0 = 1, .a1 = 0, .rot = rot, .r = 1, .g = .8, .b = .5, .tile = tile_burst});
        for(int i = 0; i < 2; ++i) puff(ox + nx * .15, oy + ny * .15, oz + nz * .15, .8, .9, {.6, .56, .5}, .5, tile_dust, .3);
        const double rot2 = random() * 6.28;
        le({.life = .6, .x = ox, .y = oy, .z = oz, .vx = nx * 2.5, .vy = ny * 2.5, .vz = nz * 2.5 + 2, .grav = 9, .s0 = .3, .s1 = .3, .a0 = 1, .a1 = .6, .rot = rot2, .add = 0, .flags = 0, .tile = tile_debris});
        sparks(ox, oy, oz, 7, {1, .8, .5}, 6, .4, nx, ny, nz);
        break; }
    case ImpactKind::plasma: {
        const double rot = random() * 6.28;
        le({.life = .28, .x = ox, .y = oy, .z = oz, .s0 = .35, .s1 = .85, .a0 = 1, .a1 = 0, .rot = rot, .rot_v = 4, .r = col[0], .g = col[1], .b = col[2], .tile = tile_plasma});
        le({.life = .22, .x = ox, .y = oy, .z = oz, .s0 = .2, .s1 = .9, .a0 = .8, .a1 = 0, .r = col[0], .g = col[1], .b = col[2], .tile = tile_ring});
        sparks(ox, oy, oz, 3, col, 3, .3, nx, ny, nz);
        light(.3, col[0], col[1], col[2], 3.2);
        break; }
    case ImpactKind::rail: {
        const double rot = random() * 6.28;
        le({.life = .25, .x = ox, .y = oy, .z = oz, .s0 = .45, .s1 = 1.0, .a0 = 1, .a1 = 0, .rot = rot, .r = col[0], .g = col[1], .b = col[2], .tile = tile_burst});
        le({.life = .35, .x = ox, .y = oy, .z = oz, .s0 = .25, .s1 = 1.3, .a0 = .9, .a1 = 0, .r = col[0], .g = col[1], .b = col[2], .tile = tile_ring});
        sparks(ox, oy, oz, 8, col, 5, .45, nx, ny, nz); puff(ox, oy, oz, .5, .9, {.62, .55, .66}, .35, tile_smoke1, .3);
        light(.45, col[0], col[1], col[2], 4.5);
        break; }
    case ImpactKind::bolt: {
        const double rot = random() * 6.28;
        le({.life = .08, .x = ox, .y = oy, .z = oz, .s0 = .4, .s1 = .55, .a0 = 1, .a1 = 0, .rot = rot, .r = col[0], .g = col[1], .b = col[2], .tile = tile_electric});
        sparks(ox, oy, oz, 2, {.8, .95, 1}, 4, .22, nx, ny, nz);
        break; }
    case ImpactKind::fire: {
        const double rot = random() * 6.28;
        le({.life = .45, .x = ox, .y = oy, .z = oz, .s0 = .4, .s1 = 1.1, .a0 = 1, .a1 = 0, .rot = rot, .r = 1, .g = 1, .b = 1, .flags = 0, .tile = tile_explo0, .frames = 8});
        for(int i = 0; i < 3; ++i) {
            const double vx = nx * 1.2 + rnd(1.2), vy = ny * 1.2 + rnd(1.2), vz = nz * 1.2 + rnd(1) + 1, r2 = random() * 6.28;
            le({.life = .5, .x = ox, .y = oy, .z = oz, .vx = vx, .vy = vy, .vz = vz, .grav = -1, .drag = 1.2, .s0 = .3, .s1 = .12, .a0 = 1, .a1 = 0, .rot = r2, .r = 1, .g = .55, .b = .2, .tile = tile_flame_gray});
        }
        le({.life = .8, .x = ox, .y = oy, .z = oz, .vz = .5, .s0 = .25, .s1 = .45, .a0 = 1, .a1 = 0, .r = 1, .g = 1, .b = 1, .tile = tile_embers});
        puff(ox, oy, oz + .2, .7, 1.1, {.35, .32, .3}, .45, tile_smoke1, .6);
        light(.35, 1, .55, .2, 4);
        break; }
    case ImpactKind::saw: {
        sparks(ox, oy, oz, 11, {1, .85, .55}, 6.5, .5, nx, ny, nz);
        const double rot = random() * 6.28;
        le({.life = .1, .x = ox, .y = oy, .z = oz, .s0 = .3, .s1 = .45, .a0 = 1, .a1 = 0, .rot = rot, .r = 1, .g = .9, .b = .7, .tile = tile_burst});
        puff(ox, oy, oz, .35, .5, {.6, .6, .62}, .3, tile_dust, .2);
        break; }
    case ImpactKind::explosion: break;
    }
    if(def.mark != MarkKind::none && S) {
        const double sz = (def.mark_size != 0 ? def.mark_size : .2) * (.85 + .3 * random()) * power;
        if(def.mark == MarkKind::hole) add_mark(x, y, z, nx, ny, nz, sz, tile_mark_hole, kWhite, .9);
        else if(def.mark == MarkKind::scorch) add_mark(x, y, z, nx, ny, nz, sz, tile_mark_scorch, kWhite, .75);
        else if(def.mark == MarkKind::slash) add_mark(x, y, z, nx, ny, nz, sz, tile_mark_slash, kWhite, .85, {.rot = std::atan2(dz, js_hypot(dx, dy))});
        else if(def.mark == MarkKind::energy) {
            add_mark(x, y, z, nx, ny, nz, sz * .8, tile_mark_scorch, kWhite, .55);
            add_mark(x, y, z, nx, ny, nz, sz, tile_mark_energy, col, 1, {.add = 1, .life = 3.5, .glow = true});
        }
    }
}

// ---- explosions -----------------------------------------------------------------------------------
void WeaponFx::explosion(double x, double y, double z, double power, bool rift) {
    const double d = kDetail;
    const std::array<double, 3> col = rift ? std::array<double, 3>{.8, .55, 1} : std::array<double, 3>{1, .62, .3};
    le({.life = .11, .x = x, .y = y, .z = z, .s0 = 2.2 * power, .s1 = 3.2 * power, .a0 = 1, .a1 = 0, .r = rift ? .85 : 1, .g = rift ? .7 : .88, .b = rift ? 1 : .6, .tile = tile_glow});
    if(rift) {
        const double rot = random() * 6.28;
        le({.life = .75, .x = x, .y = y, .z = z, .s0 = 1 * power, .s1 = 4.8 * power, .a0 = 1, .a1 = 0, .rot = rot, .rot_v = 1.5, .r = .75, .g = .5, .b = 1, .tile = tile_energy_cloud});
        le({.life = .6, .x = x, .y = y, .z = z, .s0 = 3.6 * power, .s1 = .4, .a0 = .9, .a1 = 0, .rot = 0, .rot_v = -7, .r = .85, .g = .6, .b = 1, .tile = tile_swirl});
        le({.life = .42, .x = x, .y = y, .z = z, .s0 = .6, .s1 = 6.5 * power, .a0 = .85, .a1 = 0, .r = .8, .g = .6, .b = 1, .tile = tile_ring});
        le({.life = .5, .x = x, .y = y, .z = z, .s0 = 2 * power, .s1 = 2.6 * power, .a0 = .7, .a1 = 0, .add = 0, .flags = 0, .tile = tile_dark_core});
        sparks(x, y, z, 16, {.85, .65, 1}, 7, .7);
    } else {
        const double rot = random() * 6.28;
        le({.life = .72, .x = x, .y = y, .z = z + .15, .s0 = 1.3 * power, .s1 = 3.4 * power, .a0 = 1, .a1 = .6, .rot = rot, .flags = 0, .tile = tile_explo0, .frames = 8});
        const double ex = x + rnd(.4), ey = y + rnd(.4), rot2 = random() * 6.28;
        le({.life = .62, .x = ex, .y = ey, .z = z + .35, .s0 = 1 * power, .s1 = 2.6 * power, .a0 = .9, .a1 = .4, .rot = rot2, .flags = 0, .tile = tile_explo0, .frames = 8});
        le({.life = .3, .x = x, .y = y, .z = z, .s0 = .5, .s1 = 5.2 * power, .a0 = .55, .a1 = 0, .r = 1, .g = .75, .b = .45, .tile = tile_ring});
        for(int i = 0; i < static_cast<int>(js_round(5 * d)); ++i) {
            const double px = x + rnd(.7), py = y + rnd(.7), pz = z + .3 + random() * .6, life = 1.5 + random();
            puff(px, py, pz, 1.6 * power, life, {.32, .3, .28}, .55, tile_smoke1, .7);
        }
        sparks(x, y, z, 18, {1, .75, .4}, 9, .75);
        for(int i = 0; i < static_cast<int>(js_round(4 * d)); ++i) {
            const double vx = rnd(5), vy = rnd(5), vz = 3 + random() * 4, r3 = random() * 6.28, rv = rnd(9);
            le({.life = .9, .x = x, .y = y, .z = z, .vx = vx, .vy = vy, .vz = vz, .grav = 12, .s0 = .22, .s1 = .22, .a0 = 1, .a1 = .4, .rot = r3, .rot_v = rv, .add = 0, .flags = 0, .tile = tile_debris});
        }
        le({.life = 1.1, .x = x, .y = y, .z = z + .2, .vz = 1.2, .s0 = .8, .s1 = 1.6, .a0 = 1, .a1 = 0, .tile = tile_embers});
    }
    le({.life = .55, .x = x, .y = y, .z = z + .3, .s0 = 0, .s1 = 0, .a0 = 0, .a1 = 0, .has_light = true, .light = {col[0] * 1.6, col[1] * 1.6, col[2] * 1.6, 9.5 * power}});
    // scorch on whatever it hit: the floor directly below, else the nearest wall
    const double f = world_->floor_height(x, y, z + .2);
    if(f > -900 && z - f < 1.1) {
        if(rift) add_mark(x, y, f, 0, 0, 1, 2.2 * power, tile_mark_energy, {.6, .4, .9}, .9, {.add = 1, .life = 5});
        else add_mark(x, y, f, 0, 0, 1, 2.2 * power, tile_mark_scorch, kWhite, .85);
    } else {
        const double dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {.707, .707}, {-.707, .707}, {.707, -.707}, {-.707, -.707}};
        for(const auto& a : dirs) {
            const auto S = surface_at(x, y, z, a[0], a[1], 0);
            if(S && js_hypot(S->x - x, S->y - y) < 1.1) { add_mark(S->x, S->y, S->z, S->nx, S->ny, S->nz, 2 * power, tile_mark_scorch, kWhite, .8); break; }
        }
    }
}

// ---- firing ---------------------------------------------------------------------------------------
void WeaponFx::fire(const combat::Arena& arena, int index, const std::vector<combat::ShotEvent>& shots, const combat::FireContext& ctx, double fov, double aspect) {
    (void)arena;
    const auto& def = def_of(index);
    const Camera cam = make_camera(ctx.view, fov, ctx.fov_scale, aspect);
    const auto m = muzzle_world(cam, ctx.weapon_in_hand);
    const double flash_rot = random() * 6.28, flash_scale = .85 + .3 * random();
    flash_ = Flash{0, std::max(.065, def.flash_life), index, flash_rot, flash_scale};
    recoil_.k = def.recoil_k; recoil_.vb += def.recoil_back * 15; recoil_.vc += def.recoil_climb * 24;
    recoil_.vr += rnd(def.recoil_roll * 20); recoil_.vy += rnd(def.recoil_jitter * 20);
    le({.life = def.flash_life * 1.6, .x = m[0], .y = m[1], .z = m[2], .s0 = 0, .s1 = 0, .a0 = 0, .a1 = 0, .has_light = true, .light = {def.light[0] * 1.4, def.light[1] * 1.4, def.light[2] * 1.4, def.light[3]}});
    if(def.smoke != 0) for(int i = 0; i < static_cast<int>(js_round(def.smoke * 2 * kDetail)); ++i) {
        const double life = .8 + random() * .5;
        puff(m[0] + cam.forward[0] * .25, m[1] + cam.forward[1] * .25, m[2] + cam.forward[2] * .25, .45 * def.smoke, life, {.75, .73, .7}, .28, tile_smoke1, .35);
    }
    if(def.brass != BrassKind::none) for(int i = 0; i < (def.brass_count ? def.brass_count : 1); ++i) pending_.push_back({def.brass_delay, def.brass, def.brass_scale});
    if(def.kind == ShotKind::rail) rail_charge_ = 0;
    const auto& weapon = combat::weapons()[static_cast<std::size_t>(index)];
    for(std::size_t i = 0; i < shots.size(); ++i) {
        const auto& s = shots[i];
        const double dx = s.x - m[0], dy = s.y - m[1], dz = s.z - m[2], dist = or1(js_hypot3(dx, dy, dz));
        std::array<double, 3> dir{s.x - ctx.eye.x, s.y - ctx.eye.y, s.z - ctx.eye.z};
        const double dl = or1(js_hypot3(dir[0], dir[1], dir[2]));
        dir[0] /= dl; dir[1] /= dl; dir[2] /= dl;
        if(def.kind == ShotKind::rail) {
            rails_.push_back({m[0], m[1], m[2], s.x, s.y, s.z, 0, std::max(.2, kRailTrailTime), random() * 6.28, def.color});
            if(s.hit) impact(index, s.x, s.y, s.z, dir[0], dir[1], dir[2], s.target, 1);
        } else if(def.kind == ShotKind::bolt) {
            bolt_until_ = ctx.now + weapon.delay * 1.9; bolt_def_ = index; bolt_range_ = weapon.range;
            if(s.hit && (++bolt_fx_) % 2 == 1) impact(index, s.x, s.y, s.z, dir[0], dir[1], dir[2], s.target, 1);
        } else {
            // pellets: a few tracers, every impact (up to 8)
            const bool pellet = def.kind == ShotKind::pellet;
            tracers_.push_back({m[0], m[1], m[2], s.x, s.y, s.z, 0, dist / (def.speed != 0 ? def.speed : 80), index, s.hit, s.target, dir, !pellet || i < 3, random() * 6.28, 0,
                                !pellet || i < static_cast<std::size_t>(std::max(3.0, js_round(8 * kDetail)))});
        }
    }
}
void WeaponFx::spawn_brass(const combat::Arena& arena, BrassKind type, double scale) {
    const auto cam = arena.player().view_camera();
    const auto& f = cam.forward; const auto& r = cam.right; const auto& u = cam.up;
    const auto& p = arena.player().state();
    const double x = cam.x + f[0] * .42 + r[0] * .24 - u[0] * .2, y = cam.y + f[1] * .42 + r[1] * .24 - u[1] * .2, z = cam.z + f[2] * .42 + r[2] * .24 - u[2] * .2;
    const double sp = 2.2 + random(), up = 2 + random();
    const double vx = r[0] * sp + u[0] * up + f[0] * rnd(.4) + p.vx * .9, vy = r[1] * sp + u[1] * up + f[1] * rnd(.4) + p.vy * .9, vz = r[2] * sp + u[2] * up + 2 + p.vz * .5;
    const double size = (type == BrassKind::shell ? .11 : .075) * scale, rot = random() * 6.28, rot_v = rnd(18);
    le({.life = 2.4, .x = x, .y = y, .z = z, .vx = vx, .vy = vy, .vz = vz, .grav = 14, .drag = .3, .s0 = size, .s1 = size, .a0 = 1, .a1 = 1, .rot = rot, .rot_v = rot_v, .add = 0, .flags = 0,
        .tile = type == BrassKind::shell ? tile_shell : tile_brass, .bounce = .42});
}

// ---- per tick ---------------------------------------------------------------------------------------
void WeaponFx::tick(const combat::Arena& arena, double fov, double aspect) {
    // the volley and the blasts in the order the step produced them (a rocket fired this tick and a
    // blast of an older rocket: whichever came first in update())
    const auto& e = arena.events();
    bool fired = e.fired_weapon < 0;
    for(const auto& b : e.blasts) {
        if(!fired && e.fire_seq < b.seq) { start_fire(arena, fov, aspect); fired = true; }
        start_blast(b);
    }
    if(!fired) start_fire(arena, fov, aspect);
    advance(arena);
}
void WeaponFx::start_fire(const combat::Arena& arena, double fov, double aspect) {
    const auto& e = arena.events();
    if(e.fired_weapon >= 0) fire(arena, e.fired_weapon, e.shots, e.fire, fov, aspect);
}
void WeaponFx::start_blast(const combat::BlastEvent& b) { explosion(b.x, b.y, b.fx_z(), b.power, b.kind == combat::FireKind::rift); }
void WeaponFx::advance(const combat::Arena& arena) { update(arena, physics::kTick); }
void WeaponFx::update(const combat::Arena& arena, double dt) {
    // recoil springs
    const double k = recoil_.k, c = 2 * std::sqrt(k) * .82;
    recoil_.vb += (-k * recoil_.back - c * recoil_.vb) * dt; recoil_.back += recoil_.vb * dt;
    recoil_.vc += (-k * recoil_.climb - c * recoil_.vc) * dt; recoil_.climb += recoil_.vc * dt;
    recoil_.vr += (-k * recoil_.roll - c * recoil_.vr) * dt; recoil_.roll += recoil_.vr * dt;
    recoil_.vy += (-k * recoil_.yaw - c * recoil_.vy) * dt; recoil_.yaw += recoil_.vy * dt;
    switch_t_ += dt;
    if(flash_) { flash_->t += dt; if(flash_->t >= flash_->life) flash_.reset(); }
    rail_charge_ = std::min(1.0, rail_charge_ + dt / std::max(.2, combat::weapons()[3].delay));
    for(std::size_t i = pending_.size(); i-- > 0;) {
        pending_[i].t -= dt;
        if(pending_[i].t <= 0) { const auto p = pending_[i]; spawn_brass(arena, p.type, p.scale); pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i)); }
    }
    bool expired = false;
    for(std::size_t i = le_.size(); i-- > 0;) {
        auto& e = le_[i];
        e.t += dt;
        if(e.t >= e.life) { expired = true; continue; }
        if(e.grav != 0) e.vz -= e.grav * dt;
        if(e.drag != 0) { const double f = std::max(0.0, 1 - e.drag * dt); e.vx *= f; e.vy *= f; e.vz *= f; }
        e.x += e.vx * dt; e.y += e.vy * dt; e.z += e.vz * dt; e.rot += e.rot_v * dt;
        if(e.bounce != 0) {
            e.floor_t -= dt;
            if(!e.has_floor || e.floor_t <= 0) { e.floor = world_->floor_height(e.x, e.y, e.z + .2); e.has_floor = true; e.floor_t = .08; }
            if(e.floor > -900 && e.z < e.floor + .03 && e.vz < 0) {
                e.z = e.floor + .03;
                if(e.vz < -1.2) {
                    if(e.blood && random() < .7) { const double size = .35 + random() * .3; floor_mark(e.x, e.y, e.z, size, tile_mark_blood, {.6, .05, .04}, .85, {.life = 14}); }
                    e.vz *= -e.bounce; e.vx *= .6; e.vy *= .6; e.rot_v *= .5;
                } else { e.vz = 0; e.vx *= .8; e.vy *= .8; e.rot_v *= .7; }
            }
        }
    }
    if(expired) le_.erase(std::remove_if(le_.begin(), le_.end(), [](const Entity& e) { return e.t >= e.life; }), le_.end());
    for(std::size_t i = tracers_.size(); i-- > 0;) {
        tracers_[i].t += dt;
        Tracer T = tracers_[i];
        const auto& def = def_of(T.def);
        const double f = std::min(1.0, T.t / std::max(.001, T.dur));
        if(def.kind == ShotKind::flame || def.kind == ShotKind::plasma) {
            T.smoke_t -= dt;
            if(T.smoke_t <= 0 && f < 1) {
                T.smoke_t = def.kind == ShotKind::flame ? .035 : .05;
                const double x = T.x0 + (T.x1 - T.x0) * f, y = T.y0 + (T.y1 - T.y0) * f, z = T.z0 + (T.z1 - T.z0) * f;
                if(def.kind == ShotKind::flame) { const double rot = random() * 6.28; le({.life = .35, .x = x, .y = y, .z = z, .vz = .6, .s0 = .22, .s1 = .06, .a0 = .9, .a1 = 0, .rot = rot, .r = 1, .g = .5, .b = .18, .tile = tile_flame_gray}); }
                else le({.life = .18, .x = x, .y = y, .z = z, .s0 = .14, .s1 = .04, .a0 = .8, .a1 = 0, .r = def.color[0], .g = def.color[1], .b = def.color[2], .tile = tile_glow});
            }
            tracers_[i].smoke_t = T.smoke_t;
        }
        if(def.kind == ShotKind::saw && random() < dt * 25) {
            const double x = T.x0 + (T.x1 - T.x0) * f, y = T.y0 + (T.y1 - T.y0) * f, z = T.z0 + (T.z1 - T.z0) * f;
            sparks(x, y, z, 1, {1, .85, .55}, 2, .25);
        }
        if(T.t >= T.dur) {
            if(T.hit && T.impact_ok) impact(T.def, T.x1, T.y1, T.z1, T.dir[0], T.dir[1], T.dir[2], T.enemy, 1);
            tracers_.erase(tracers_.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    for(std::size_t i = rails_.size(); i-- > 0;) { rails_[i].t += dt; if(rails_[i].t >= rails_[i].life) rails_.erase(rails_.begin() + static_cast<std::ptrdiff_t>(i)); }
    for(std::size_t i = marks_.size(); i-- > 0;) { marks_[i].t += dt; if(marks_[i].t >= marks_[i].life) marks_.erase(marks_.begin() + static_cast<std::ptrdiff_t>(i)); }
    // rocket / grenade / rift trails
    for(auto& kv : projectile_fx_) kv.second.alive = false;
    for(const auto& p : arena.projectiles()) {
        auto& s = projectile_fx_[p.id];
        s.alive = true;
        s.st -= dt;
        if(s.st > 0) continue;
        if(p.kind == combat::FireKind::rocket) {
            s.st = .022 / kDetail;
            const double v = or1(js_hypot3(p.vx, p.vy, p.vz));
            puff(p.x - p.vx / v * .45, p.y - p.vy / v * .45, p.z - p.vz / v * .45, .7, 1.3, {.68, .66, .63}, .42, tile_smoke1, .25);
        } else if(p.kind == combat::FireKind::grenade) {
            s.st = .04 / kDetail;
            puff(p.x, p.y, p.z, .4, .8, {.7, .7, .68}, .3, tile_smoke1, .2);
        } else {
            s.st = .03;
            const double x = p.x + rnd(.15), y = p.y + rnd(.15), z = p.z + rnd(.15), vx = rnd(.5), vy = rnd(.5), vz = rnd(.5);
            le({.life = .45, .x = x, .y = y, .z = z, .vx = vx, .vy = vy, .vz = vz, .s0 = .2, .s1 = .02, .a0 = .9, .a1 = 0, .r = .8, .g = .6, .b = 1, .tile = tile_spark_dot});
        }
    }
    std::erase_if(projectile_fx_, [](const auto& kv) { return !kv.second.alive; });
}

// ---- viewmodel ----------------------------------------------------------------------------------------
Offsets WeaponFx::viewmodel_offsets(int index, double reloading) {   // recoil spring + raise on switch + reload dip
    if(index != last_index_) { if(last_index_ >= 0) switch_t_ = 0; last_index_ = index; }
    const double raise = std::min(1.0, switch_t_ / .3), e = 1 - (1 - raise) * (1 - raise);
    double drop = (1 - e) * 1.25, pitch = -(1 - e) * .55, roll = 0;
    if(reloading > 0) {
        const double p = 1 - reloading / std::max(.01, combat::weapons()[static_cast<std::size_t>(index)].reload), s = std::sin(kPi * std::min(1.0, p));
        drop += .42 * std::pow(s, .7); roll += .5 * s; pitch += -.28 * s;
    }
    return {recoil_.back, recoil_.climb * .25, pitch + recoil_.climb, roll + recoil_.roll, recoil_.yaw, drop};
}

// ---- per frame ------------------------------------------------------------------------------------------
void WeaponFx::build(const combat::Arena& arena, double fov, double aspect, Frame& out) {
    const auto& player = arena.player();
    const double t = player.now();
    const Camera cam = make_camera(player.view_camera(), fov, player.fov_scale(), aspect);
    out.camera = cam;
    out.mark.clear(); out.main.clear(); out.gun.clear(); out.lights.clear();
    auto& main = out.main;
    // marks (fade during the last 1.2 s; energy glows fade quickly)
    for(const auto& m : marks_) {
        const double fade = std::min(1.0, (m.life - m.t) / 1.2), gl = m.glow ? std::max(0.0, 1 - m.t / m.life * 1.6) : 1, a = m.a * fade * gl;
        if(a <= .01) continue;
        quad4(out.mark, cam, {{{m.x - m.ax - m.bx, m.y - m.ay - m.by, m.z - m.az - m.bz}, {m.x + m.ax - m.bx, m.y + m.ay - m.by, m.z + m.az - m.bz}, {m.x + m.ax + m.bx, m.y + m.ay + m.by, m.z + m.az + m.bz},
                               {m.x - m.ax + m.bx, m.y - m.ay + m.by, m.z - m.az + m.bz}}}, 0, 0, 1, 1, m.tile, m.add != 0 ? 4 : 0, m.r, m.g, m.b, a, m.add);
    }
    // local entities
    for(const auto& e : le_) {
        const double k = e.t / e.life;
        if(e.has_light) { const double f = 1 - k; add_light(out, e.x, e.y, e.z, e.light[3] * (.6 + .4 * f), e.light[0] * f, e.light[1] * f, e.light[2] * f); if(e.s0 == 0 && e.s1 == 0) continue; }
        double a = e.a0 + (e.a1 - e.a0) * k;
        if(e.fade_in != 0 && e.t < e.fade_in) a *= e.t / e.fade_in;
        if(a <= .004) continue;
        const double s = e.s0 + (e.s1 - e.s0) * k;
        const int tile = e.frames ? e.tile + std::min(e.frames - 1, static_cast<int>(std::floor(k * e.frames))) : e.tile;
        if(e.stretch != 0) beam(main, cam, e.x - e.vx * e.stretch, e.y - e.vy * e.stretch, e.z - e.vz * e.stretch, e.x, e.y, e.z, s, tile, e.r, e.g, e.b, a, e.add, 0, 1, e.flags);
        else sprite(main, cam, e.x, e.y, e.z, s, s, e.rot, tile, e.r, e.g, e.b, a, e.add, e.flags);
    }
    // tracers / visual projectiles of the hitscan weapons
    for(const auto& T : tracers_) {
        if(!T.show) continue;
        const auto& def = def_of(T.def);
        const auto& c = def.color;
        const double k = std::min(1.0, T.t / std::max(.001, T.dur));
        const double dx = T.x1 - T.x0, dy = T.y1 - T.y0, dz = T.z1 - T.z0, L = or1(js_hypot3(dx, dy, dz)), ux = dx / L, uy = dy / L, uz = dz / L;
        const double x = T.x0 + dx * k, y = T.y0 + dy * k, z = T.z0 + dz * k;
        auto back = [&](double len) { const double b = std::min(len, L * k); return std::array<double, 3>{x - ux * b, y - uy * b, z - uz * b}; };
        const double grow = std::min(1.0, .3 + k * L / 1.6);
        switch(def.kind) {
        case ShotKind::nail: {
            const auto p = back(.6); beam(main, cam, p[0], p[1], p[2], x, y, z, .15, tile_nail, 1, 1, 1, 1, 0, 0, 1, 0);
            const auto q = back(1.4); beam(main, cam, q[0], q[1], q[2], x, y, z, .07, tile_tracer, c[0], c[1], c[2], def.hot ? .85 : .45, 1, 0, 1, 4);
            if(def.hot) sprite(main, cam, x, y, z, .22, .22, 0, tile_glow, c[0], c[1], c[2], .6, 1);
            break; }
        case ShotKind::pellet: { const auto q = back(2.4); beam(main, cam, q[0], q[1], q[2], x, y, z, .045, tile_tracer, c[0], c[1], c[2], .55, 1, 0, 1, 4); break; }
        case ShotKind::bullet: { const auto q = back(3.2); beam(main, cam, q[0], q[1], q[2], x, y, z, .06, tile_tracer, c[0], c[1], c[2], .8, 1, 0, 1, 4); break; }
        case ShotKind::cannon: { const auto q = back(4.2); beam(main, cam, q[0], q[1], q[2], x, y, z, .1, tile_tracer, c[0], c[1], c[2], .95, 1, 0, 1, 4); sprite(main, cam, x, y, z, .18, .18, 0, tile_glow, 1, .85, .6, .6, 1); break; }
        case ShotKind::spike: {
            const auto p = back(.55); beam(main, cam, p[0], p[1], p[2], x, y, z, .15, tile_spike, 1, .8, .6, 1, 0, 0, 1, 0);
            const auto q = back(1.6); beam(main, cam, q[0], q[1], q[2], x, y, z, .12, tile_tracer, c[0], c[1], c[2], .8, 1, 0, 1, 4);
            sprite(main, cam, x, y, z, .26, .26, 0, tile_glow, c[0], c[1], c[2], .55, 1);
            break; }
        case ShotKind::plasma:
            sprite(main, cam, x, y, z, .36 * grow, .36 * grow, t * 9 + T.spin, tile_plasma, c[0], c[1], c[2], 1, 1); sprite(main, cam, x, y, z, .7 * grow, .7 * grow, 0, tile_glow, c[0], c[1], c[2], .45, 1);
            add_light(out, x, y, z, 3.2, c[0] * .7, c[1] * .7, c[2] * .7);
            break;
        case ShotKind::flame:
            sprite(main, cam, x, y, z, .5 * grow, .5 * grow, t * 6 + T.spin, tile_fireball, 1, 1, 1, 1, 1, 0); sprite(main, cam, x, y, z, .9 * grow, .9 * grow, 0, tile_glow, 1, .5, .2, .4, 1);
            add_light(out, x, y, z, 3.6, 1, .5, .18);
            break;
        case ShotKind::saw: {
            sprite(main, cam, x, y, z, .42 * grow, .42 * grow, t * 40 + T.spin, tile_saw, 1, 1, 1, 1, 0, 0);
            const auto q = back(1); beam(main, cam, q[0], q[1], q[2], x, y, z, .18, tile_tracer, .8, .9, 1, .35, 1, 0, 1, 4);
            break; }
        default: break;
        }
    }
    // railgun: core + glow beam + spiral ring trail
    for(const auto& R : rails_) {
        const double k = R.t / R.life, f = 1 - k;
        const auto& c = R.col;
        const double core = std::max(0.0, 1 - k * 1.6);
        const double length = js_hypot3(R.bx - R.ax, R.by - R.ay, R.bz - R.az);
        beam(main, cam, R.ax, R.ay, R.az, R.bx, R.by, R.bz, .06, tile_rail_core, 1, .92, 1, core, 1, 0, length / 2, 5);
        beam(main, cam, R.ax, R.ay, R.az, R.bx, R.by, R.bz, .34 + .2 * k, tile_rail_core, c[0], c[1], c[2], .55 * f, 1, 0, length / 3, 5);
        const double dx = R.bx - R.ax, dy = R.by - R.ay, dz = R.bz - R.az, L = or1(js_hypot3(dx, dy, dz)), ux = dx / L, uy = dy / L, uz = dz / L;
        double px = -uy, py = ux;
        const double pz = 0;
        double pl = js_hypot(px, py);
        if(pl < .1) { px = 1; py = 0; pl = 1; }
        px /= pl; py /= pl;
        const double qx = uy * pz - uz * py, qy = uz * px - ux * pz, qz = ux * py - uy * px;
        const double step = .32, rad = .07 + .2 * k;
        const int count = static_cast<int>(std::min(240.0, std::floor(L / step)));
        for(int i = 1; i < count; ++i) {
            const double s = i * step, ang = R.phase + i * .55, cx = std::cos(ang) * rad, sy = std::sin(ang) * rad;
            sprite(main, cam, R.ax + ux * s + px * cx + qx * sy, R.ay + uy * s + py * cx + qy * sy, R.az + uz * s + pz * cx + qz * sy, .13 + .1 * k, .13 + .1 * k, 0, tile_spark_dot, c[0], c[1], c[2], .85 * f * f, 1);
        }
        if(k < .2) add_light(out, R.ax, R.ay, R.az, 4, c[0] * (1 - k * 5), c[1] * (1 - k * 5), c[2] * (1 - k * 5));
    }
    const int in_hand = arena.current_weapon();
    // thunderbolt: re-traced every frame from the current aim
    if(bolt_def_ >= 0 && t < bolt_until_ && !player.state().downed) {
        const auto m = muzzle_world(cam, in_hand);
        const auto o = player.shot_origin();
        const auto& p = player.state();
        const double cp = std::cos(p.pitch), dx = std::cos(p.a) * cp, dy = std::sin(p.a) * cp, dz = -std::sin(p.pitch);
        const double r = bolt_range_;
        const auto h = arena.shot_sweep(o.x, o.y, o.z, o.x + dx * r, o.y + dy * r, o.z + dz * r, .042);
        const auto& c = def_of(bolt_def_).color;
        const double L = js_hypot3(h.x - m[0], h.y - m[1], h.z - m[2]);
        const int tile = (static_cast<long long>(std::floor(t * 20)) % 2) ? tile_bolt1 : tile_bolt2;
        const double off = random();
        beam(main, cam, m[0], m[1], m[2], h.x, h.y, h.z, .42, tile, c[0], c[1], c[2], 1, 1, off, off + L / 2.2, 5);
        beam(main, cam, m[0], m[1], m[2], h.x, h.y, h.z, .1, tile_laser, c[0], c[1], c[2], .55, 1, 0, L / 2, 5);
        sprite(main, cam, h.x, h.y, h.z, .65, .65, t * 30, tile_electric, c[0], c[1], c[2], .9, 1);
        add_light(out, h.x, h.y, h.z, 4.5, c[0] * .8, c[1] * .8, c[2] * .8); add_light(out, m[0], m[1], m[2], 4, c[0] * .7, c[1] * .7, c[2] * .7);
    }
    // rockets, grenades, rift orbs: real projectiles, eased out of the viewmodel muzzle for 0.2 s
    for(const auto& p0 : arena.projectiles()) {
        auto& s = projectile_fx_[p0.id];
        if(!s.has_off) { const auto m = muzzle_world(cam, in_hand); s.off = {m[0] - p0.x, m[1] - p0.y, m[2] - p0.z}; s.off_t = t; s.has_off = true; }
        const double k = std::max(0.0, 1 - (t - s.off_t) / .2);
        const double px = k <= 0 ? p0.x : p0.x + s.off[0] * k, py = k <= 0 ? p0.y : p0.y + s.off[1] * k, pz = k <= 0 ? p0.z : p0.z + s.off[2] * k;
        if(p0.kind == combat::FireKind::rocket) {
            const double v = or1(js_hypot3(p0.vx, p0.vy, p0.vz)), ux = p0.vx / v, uy = p0.vy / v, uz = p0.vz / v;
            beam(main, cam, px - ux * .42, py - uy * .42, pz - uz * .42, px + ux * .32, py + uy * .32, pz + uz * .32, .26, tile_rocket, 1, 1, 1, 1, 0, 0, 1, 0);
            beam(main, cam, px - ux * .42, py - uy * .42, pz - uz * .42, px - ux * 1.25, py - uy * 1.25, pz - uz * 1.25, .34, tile_flash_side, 1, .62, .3, 1, 1, 0, 1, 4);
            sprite(main, cam, px - ux * .45, py - uy * .45, pz - uz * .45, .55 + .1 * std::sin(t * 60), .55, t * 20, tile_glow, 1, .7, .35, .9, 1);
            add_light(out, px, py, pz, 5.2, 1, .6, .28);
        } else if(p0.kind == combat::FireKind::grenade) {
            sprite(main, cam, px, py, pz, .3, .3, t * 7 + p0.x, tile_grenade, 1, 1, 1, 1, 0, 0);
            if(std::sin(t * 16) > .2) { sprite(main, cam, px, py, pz + .12, .16, .16, 0, tile_glow, 1, .25, .15, .9, 1); add_light(out, px, py, pz, 1.8, .8, .15, .08); }
        } else {
            const double pulse = 1 + .12 * std::sin(t * 22);
            sprite(main, cam, px, py, pz, .85 * pulse, .85 * pulse, t * 5, tile_swirl, .8, .55, 1, 1, 1);
            sprite(main, cam, px, py, pz, .42, .42, 0, tile_dark_core, 1, 1, 1, .85, 0, 0);
            sprite(main, cam, px, py, pz, .55, .55, -t * 7, tile_electric, .85, .7, 1, .9, 1);
            sprite(main, cam, px, py, pz, 1.5, 1.5, 0, tile_glow, .6, .4, 1, .45, 1); add_light(out, px, py, pz, 6, .7, .45, 1);
        }
    }
    // the flash lights the viewmodel itself
    if(flash_) { const double f = 1 - flash_->t / flash_->life; const auto& c = def_of(flash_->index).flash_color; glow_ = {c[0] * f * .55, c[1] * f * .55, c[2] * f * .55}; }
    else glow_ = {0, 0, 0};
    // medkits: a soft green glow above every available pickup (render(), 13588)
    for(const auto& p : arena.pickups()) if(p.cool <= 0) sprite(main, cam, p.x, p.y, p.z + .5, 1.3, 1.3, 0, tile_glow, .45, 1, .55, .22 + .08 * std::sin(t * 3 + p.x), 1);
    // ... and the medkit itself: a bobbing world-atlas billboard (renderGeometry(), 12353)
    out.atlas.clear();
    for(const auto& p : arena.pickups()) if(p.cool <= 0) billboard(out.atlas, p.x, p.y, .20 + std::sin(t * 2 + p.x) * .06 + p.z, .65, .65, kAtlasMedkit, player.state().a);

    // ---- weapon view (renderGun3D): yaw-only viewmodel camera, muzzle anchored on screen ----
    auto& gv = out.gun_view;
    gv.visible = !player.state().downed;
    if(!gv.visible) return;
    const int gun = in_hand;
    const auto q3w = player.weapon_motion();
    const auto vo = viewmodel_offsets(gun, arena.reloading());
    const auto& muzzle = kGunMuzzle[static_cast<std::size_t>(gun)];
    const double yaw = kPoseToeIn, cy = std::cos(yaw), sy = std::sin(yaw);
    const double muzzle_depth = kPoseMuzzleY - kPoseY, legacy_shift = .20 * muzzle_depth * std::tan(65 * kPi / 360);
    const double rest_x = kPoseX + legacy_shift + (muzzle[0] - kPoseMuzzleX), rest_y = kPoseY + (muzzle[1] - kPoseMuzzleY), rest_z = kPoseZ + (muzzle[2] - kPoseMuzzleZ);
    const double vx = muzzle[0] - rest_x, vy = muzzle[1] - rest_y, vz = muzzle[2] - rest_z;
    const double depth = std::max(.08, vx * sy + vy * cy);
    const double tan_h = std::tan(kRefFov * kPi / 360), tan_v = tan_h / aspect;
    const double rx = cy, ry = -sy;
    const double projected_x = (vx * rx + vy * ry) / (depth * tan_h), projected_y = vz / (depth * tan_v);
    const double desired_x = 2 * kRefMuzzleX - 1, desired_y = 1 - 2 * std::max(.50, std::min(.95, kMuzzleHeight[static_cast<std::size_t>(gun)]));
    const double offset_r = (projected_x - desired_x) * depth * tan_h, offset_u = (projected_y - desired_y) * depth * tan_v;
    const double cx = rest_x + offset_r * rx, cy0 = rest_y + offset_r * ry + vo.back, cz = rest_z + offset_u - q3w.drop + vo.drop + vo.lift;
    std::array<double, 3> F{sy, cy, 0}, Rv{cy, -sy, 0}, U{0, 0, 1};
    auto rot = [](std::array<double, 3>& a, std::array<double, 3>& b, double angle) {
        const double c = std::cos(angle), s = std::sin(angle);
        const std::array<double, 3> na{a[0] * c + b[0] * s, a[1] * c + b[1] * s, a[2] * c + b[2] * s}, nb{b[0] * c - a[0] * s, b[1] * c - a[1] * s, b[2] * c - a[2] * s};
        a = na; b = nb;
    };
    rot(Rv, F, q3w.yaw + vo.yaw); rot(F, U, q3w.pitch - vo.pitch); rot(Rv, U, q3w.roll + vo.roll);
    gv.weapon = gun; gv.camera = {cx, cy0, cz, F, Rv, U, tan_v, aspect}; gv.glow = glow_;
    // gun-space muzzle flash (WFX.buildGun)
    const auto& def = def_of(gun);
    const double mx = muzzle[0], my = muzzle[1], mz = muzzle[2];
    auto& b = out.gun;
    const auto& gc = gv.camera;
    if(flash_ && flash_->index == gun) {
        const auto& f = *flash_;
        const double k = f.t / f.life, s = def.flash_size * f.scale * (1 - .3 * k) * 1.65, a = 1 - k * k;
        const auto& c = def.flash_color;
        sprite(b, gc, mx, my + .12, mz, s * 1.5, s * 1.5, 0, tile_glow, c[0], c[1], c[2], .7 * a, 1);
        sprite(b, gc, mx, my + .2, mz, s, s, f.rot, tile_flash_star, c[0], c[1], c[2], a, 1);
        beam(b, gc, mx, my + .02, mz, mx, my + def.flash_side * f.scale * 1.45, mz, s * .72, tile_flash_side, c[0], c[1], c[2], a, 1, 0, 1, 4);
    }
    if(gun == 3 && rail_charge_ < 1) {   // railgun recharge glow
        const double k = rail_charge_, a = .25 + .5 * std::sin(k * kPi);
        const auto& c = def.color;
        sprite(b, gc, mx, my - .15, mz, .35 + .25 * k, .35 + .25 * k, t * 3, tile_heat_ring, c[0], c[1], c[2], a * .8, 1);
    }
    if(gun == 6 && bolt_def_ >= 0 && t < bolt_until_) { const auto& c = def.color; sprite(b, gc, mx, my + .05, mz, .55, .55, t * 40, tile_electric, c[0], c[1], c[2], .95, 1); }
    if(gun == 10 && !flash_) { const auto& c = def.color; sprite(b, gc, mx, my - .1, mz, .32, .32, t * 2, tile_swirl, c[0], c[1], c[2], .45, 1); }
}
}  // namespace cat::fx
