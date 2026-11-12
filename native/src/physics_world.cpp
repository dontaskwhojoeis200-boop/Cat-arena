// N3 collision world: the compiled map data queried like the legacy Stage 4 player collision.
// Every function names the legacy function it reproduces (legacy/index.html line numbers).
// Floating-point expressions keep the legacy operation order so results match the reference.
#include "cat/physics.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cat::physics {
namespace {
using Poly = std::vector<std::array<double, 2>>;
bool is_number(const assets::Value& v) { return std::holds_alternative<double>(v.data); }
bool is_object(const assets::Value& v) { return std::holds_alternative<assets::Value::Object>(v.data); }
bool is_string(const assets::Value& v) { return std::holds_alternative<std::string>(v.data); }
Poly read_poly(const assets::Value& v) {
    Poly p;
    for(const auto& pt : v.array()) p.push_back({pt.array().at(0).number(), pt.array().at(1).number()});
    if(p.size() < 3) throw std::runtime_error("collision polygon with fewer than 3 points");
    return p;
}
std::array<double, 4> bounds(const Poly& p) {
    std::array<double, 4> b{p[0][0], p[0][1], p[0][0], p[0][1]};
    for(const auto& q : p) { b[0] = std::min(b[0], q[0]); b[1] = std::min(b[1], q[1]); b[2] = std::max(b[2], q[0]); b[3] = std::max(b[3], q[1]); }
    return b;
}
// levelInside (8225): crossing-number test. The bounding-box early-out is result-neutral.
bool inside(double x, double y, const Poly& poly, const std::array<double, 4>& b) {
    if(x < b[0] || x > b[2] || y < b[1] || y > b[3]) return false;
    bool hit = false;
    for(std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const auto& a = poly[i];
        const auto& c = poly[j];
        if((a[1] > y) != (c[1] > y) && x < (c[0] - a[0]) * (y - a[1]) / (c[1] - a[1]) + a[0]) hit = !hit;
    }
    return hit;
}
// levelInsideOrEdge (8233)
bool inside_or_edge(double x, double y, const Poly& poly, const std::array<double, 4>& b, double epsilon) {
    if(x < b[0] - epsilon || x > b[2] + epsilon || y < b[1] - epsilon || y > b[3] + epsilon) return false;
    if(inside(x, y, poly, b)) return true;
    const double e2 = epsilon * epsilon;
    for(std::size_t i = 0; i < poly.size(); ++i) {
        const auto& a = poly[i];
        const auto& q = poly[(i + 1) % poly.size()];
        const double vx = q[0] - a[0], vy = q[1] - a[1];
        const double d = vx * vx + vy * vy;
        const double t = d != 0 ? std::max(0.0, std::min(1.0, ((x - a[0]) * vx + (y - a[1]) * vy) / d)) : 0;
        const double ex = x - a[0] - t * vx, ey = y - a[1] - t * vy;
        if(ex * ex + ey * ey <= e2) return true;
    }
    return false;
}
// levelDistanceSqToWall (8388)
double distance_sq_to_wall(double x, double y, const Wall& w) {
    const double vx = w.bx - w.ax, vy = w.by - w.ay, len = vx * vx + vy * vy;
    const double t = len != 0 ? std::max(0.0, std::min(1.0, ((x - w.ax) * vx + (y - w.ay) * vy) / len)) : 0;
    const double dx = x - w.ax - t * vx, dy = y - w.ay - t * vy;
    return dx * dx + dy * dy;
}
std::vector<std::vector<std::uint32_t>> read_grid(const assets::Value& v, std::size_t limit) {
    std::vector<std::vector<std::uint32_t>> g;
    for(const auto& c : v.array()) {
        std::vector<std::uint32_t> ids;
        for(const auto& id : c.array()) {
            const double n = id.number();
            if(n < 0 || n >= static_cast<double>(limit) || n != std::floor(n)) throw std::runtime_error("collision grid index out of range");
            ids.push_back(static_cast<std::uint32_t>(n));
        }
        g.push_back(std::move(ids));
    }
    return g;
}
}  // namespace

double collider_height(double crouch_offset) { return std::max(kPlayerCrouchHeight, kPlayerHeight - crouch_offset * .60); }   // stage4Height (8464)

double js_hypot(double a, double b) {
    a = std::fabs(a); b = std::fabs(b);
    if(std::isinf(a) || std::isinf(b)) return std::numeric_limits<double>::infinity();
    if(std::isnan(a) || std::isnan(b)) return std::numeric_limits<double>::quiet_NaN();
    const double max = std::max(a, b);
    if(max == 0) return 0;
    double sum = 0, compensation = 0;
    for(const double v : {a, b}) {
        const double n = v / max;
        const double summand = n * n - compensation;
        const double preliminary = sum + summand;
        compensation = (preliminary - sum) - summand;
        sum = preliminary;
    }
    return std::sqrt(sum) * max;
}

double js_hypot3(double a, double b, double c) {
    a = std::fabs(a); b = std::fabs(b); c = std::fabs(c);
    if(std::isinf(a) || std::isinf(b) || std::isinf(c)) return std::numeric_limits<double>::infinity();
    if(std::isnan(a) || std::isnan(b) || std::isnan(c)) return std::numeric_limits<double>::quiet_NaN();
    const double max = std::max({a, b, c});
    if(max == 0) return 0;
    double sum = 0, compensation = 0;
    for(const double v : {a, b, c}) {
        const double n = v / max;
        const double summand = n * n - compensation;
        const double preliminary = sum + summand;
        compensation = (preliminary - sum) - summand;
        sum = preliminary;
    }
    return std::sqrt(sum) * max;
}

CollisionWorld::CollisionWorld(const assets::Value& c) {
    cell_ = c.at("cell").number();
    gw_ = static_cast<long long>(c.at("gw").number());
    if(cell_ <= 0 || gw_ <= 0 || gw_ > 4096) throw std::runtime_error("invalid collision grid");
    for(const auto& s : c.at("slabs").array()) {
        Slab o;
        o.poly = read_poly(s.at("poly")); o.box = bounds(o.poly);
        o.z = s.at("z").number(); o.thickness = s.at("thickness").number();
        o.has_base = is_number(s.at("base")); if(o.has_base) o.base = s.at("base").number();
        o.nav = s.at("nav").boolean(); o.kind = s.at("kind").string(); o.roof = o.kind == "roof";
        if(is_string(s.at("id"))) o.id = s.at("id").string();
        if(is_object(s.at("plane"))) {
            const auto& p = s.at("plane");
            o.has_plane = true;
            o.ox = p.at("ox").number(); o.oy = p.at("oy").number(); o.dx = p.at("dx").number(); o.dy = p.at("dy").number();
            o.len = p.at("len").number(); o.z0 = p.at("z0").number(); o.z1 = p.at("z1").number();
            if(o.len <= 0) throw std::runtime_error("ramp plane with non-positive length");
        }
        slabs_.push_back(std::move(o));
    }
    for(const auto& w : c.at("walls").array())
        walls_.push_back({w.at("ax").number(), w.at("ay").number(), w.at("bx").number(), w.at("by").number(), w.at("z0").number(), w.at("z1").number()});
    auto read_solids = [](const assets::Value& list, bool with_massive) {
        std::vector<Solid> out;
        for(const auto& s : list.array()) { Solid o; o.poly = read_poly(s.at("poly")); o.box = bounds(o.poly); o.massive = with_massive && s.at("massive").boolean();
            if(with_massive) { o.z0 = s.at("z0").number(); o.z1 = s.at("z1").number(); }
            out.push_back(std::move(o)); }
        return out;
    };
    solids_ = read_solids(c.at("solids"), true);
    cavities_ = read_solids(c.at("cavities"), false);
    for(const auto& f : c.at("footprint").array()) { Solid o; o.poly = read_poly(f); o.box = bounds(o.poly); footprint_.push_back(std::move(o)); }
    slab_grid_ = read_grid(c.at("slabGrid"), slabs_.size());
    solid_grid_ = read_grid(c.at("solidGrid"), solids_.size());
    cavity_grid_ = read_grid(c.at("cavityGrid"), cavities_.size());
    const auto cells = static_cast<std::size_t>(gw_ * gw_);
    if(slab_grid_.size() != cells || solid_grid_.size() != cells || cavity_grid_.size() != cells) throw std::runtime_error("collision grid size mismatch");
    // levelAddWall (8364): 4 m buckets, each wall registered in every bucket its box +/- 1 m touches,
    // in wall order. stage4NearestWall depends on exactly this membership.
    for(std::uint32_t id = 0; id < walls_.size(); ++id) {
        const auto& w = walls_[id];
        const auto lo_x = static_cast<long long>(std::floor((std::min(w.ax, w.bx) - 1) / 4)), hi_x = static_cast<long long>(std::floor((std::max(w.ax, w.bx) + 1) / 4));
        const auto lo_y = static_cast<long long>(std::floor((std::min(w.ay, w.by) - 1) / 4)), hi_y = static_cast<long long>(std::floor((std::max(w.ay, w.by) + 1) / 4));
        for(auto ix = lo_x; ix <= hi_x; ++ix) for(auto iy = lo_y; iy <= hi_y; ++iy) wall_buckets_[{ix, iy}].push_back(id);
    }
    for(const auto& p : c.at("pads").array()) {
        Pad o;
        o.x = p.at("x").number(); o.y = p.at("y").number(); o.z = p.at("z").number(); o.r = p.at("r").number(); o.cooldown = p.at("cooldown").number();
        if(is_string(p.at("id"))) o.id = p.at("id").string();
        const auto& obj = p.object();
        if(auto it = obj.find("apex"); it != obj.end() && std::holds_alternative<assets::Value::Array>(it->second.data)) {
            o.has_apex = true;
            for(std::size_t i = 0; i < 3; ++i) o.apex[i] = it->second.array().at(i).number();
        } else {
            const char* names[3] = {"vx", "vy", "vz"};
            for(std::size_t i = 0; i < 3; ++i) if(auto v = obj.find(names[i]); v != obj.end() && is_number(v->second)) o.velocity[i] = v->second.number();
        }
        pads_.push_back(std::move(o));
    }
    for(const auto& s : c.at("spawns").array()) {
        Spawn o{s.at("x").number(), s.at("y").number(), s.at("z").number(), s.at("a").number(), {}};
        if(is_string(s.at("tag"))) o.tag = s.at("tag").string();
        spawns_.push_back(std::move(o));
    }
    static const assets::Value kNoPickups{assets::Value::Array{}};
    const auto pickups_it = c.object().find("pickups");
    for(const auto& p : (pickups_it != c.object().end() ? pickups_it->second : kNoPickups).array()) {   // worldPickupSpots: keep pickups that sit on a nav floor surface
        PickupSpot o{p.at("x").number(), p.at("y").number(), p.at("z").number(), {}};
        if(is_string(p.at("tag"))) o.tag = p.at("tag").string();
        std::array<const Slab*, 64> q;
        const auto n = slab_query(o.x, o.y, q);
        bool on_floor = false;
        for(std::size_t k = 0; k < n && !on_floor; ++k) on_floor = q[k]->nav && std::fabs(slab_z(*q[k], o.x, o.y) - o.z) < .08;
        if(on_floor) pickups_.push_back(std::move(o));
    }
}

const std::vector<std::uint32_t>& CollisionWorld::cell(const std::vector<std::vector<std::uint32_t>>& grid, double x, double y) const {   // worldCell (8248)
    const double fx = std::floor(x / cell_), fy = std::floor(y / cell_);
    if(!(fx >= 0) || !(fy >= 0) || fx >= static_cast<double>(gw_) || fy >= static_cast<double>(gw_)) return empty_;
    return grid[static_cast<std::size_t>(fy) * static_cast<std::size_t>(gw_) + static_cast<std::size_t>(fx)];
}
const std::vector<std::uint32_t>* CollisionWorld::wall_bucket(double x, double y) const {   // levelWallBucket (8383)
    const double fx = std::floor(x / 4), fy = std::floor(y / 4);
    if(!(std::fabs(fx) < 1e15) || !(std::fabs(fy) < 1e15)) return nullptr;
    const auto it = wall_buckets_.find({static_cast<long long>(fx), static_cast<long long>(fy)});
    return it == wall_buckets_.end() ? nullptr : &it->second;
}
bool CollisionWorld::walkable(double x, double y) const {   // levelWalkable (8304) = worldInFootprint && !worldInMassive
    bool foot = false;
    for(const auto& f : footprint_) if(inside(x, y, f.poly, f.box)) { foot = true; break; }
    if(!foot) return false;
    for(const auto id : cell(solid_grid_, x, y)) { const auto& s = solids_[id]; if(s.massive && inside(x, y, s.poly, s.box)) return false; }
    return true;
}
bool CollisionWorld::solid_at(double x, double y, double z, double r) const {   // levelSolidAt (8443)
    bool foot = false;
    for(const auto& f : footprint_) if(inside(x, y, f.poly, f.box)) { foot = true; break; }
    if(!foot) return true;
    if(z < -4.0 - 1.2) return true;                    // LEVEL_L1_Z - 1.2 (the constant is the same on every map)
    if(z < -.45 && !in_cavity(x, y)) return true;
    const double z0 = z - r, z1 = z + r;
    for(const auto id : cell(solid_grid_, x, y)) {     // levelFeatureSolidAt(x, y, z - r, z + r, 0)
        const auto& s = solids_[id];
        if(z1 < s.z0 + .015 || z0 > s.z1 - .015) continue;
        if(inside(x, y, s.poly, s.box)) return true;
    }
    if(const auto* ids = wall_bucket(x, y)) {          // levelCircleHitsWall(x, y, r, z - r, z + r)
        const double reach = (r + .095) * (r + .095);
        for(const auto id : *ids) {
            const auto& w = walls_[id];
            if(z1 < w.z0 + .03 || z0 > w.z1 - .03) continue;
            if(distance_sq_to_wall(x, y, w) < reach) return true;
        }
    }
    std::array<const Slab*, 64> q;
    const auto n = slab_query(x, y, q);
    for(std::size_t k = 0; k < n; ++k) {
        const double zz = slab_z(*q[k], x, y), th = slab_thickness(*q[k], zz);
        if(z <= zz + r && z >= zz - th - r) return true;
    }
    return false;
}
double CollisionWorld::shot_collision_t(double x, double y, double z, double nx, double ny, double nz, double r) const {   // worldShotCollisionT (11820)
    const double length = js_hypot3(nx - x, ny - y, nz - z);
    const int steps = static_cast<int>(std::max(1.0, std::ceil(length / .19)));
    if(solid_at(x, y, z, r)) return 0;
    for(int k = 1; k <= steps; ++k) {
        const double t = static_cast<double>(k) / steps;
        if(solid_at(x + (nx - x) * t, y + (ny - y) * t, z + (nz - z) * t, r)) return t;
    }
    return std::numeric_limits<double>::infinity();
}
std::optional<double> CollisionWorld::overhead(double x, double y) const {   // levelOverhead (8334)
    std::array<const Slab*, 64> q;
    const auto n = slab_query(x, y, q);
    std::optional<double> best;
    for(std::size_t k = 0; k < n; ++k) if(q[k]->kind == "roof") { const double u = q[k]->z - q[k]->thickness; if(!best || u < *best) best = u; }
    return best;
}
bool CollisionWorld::hits_slab(double x, double y, double r, double z0, double z1) const {   // levelHitsSlab (8408)
    (void)r;
    std::array<const Slab*, 64> q;
    const auto n = slab_query(x, y, q);
    for(std::size_t k = 0; k < n; ++k) {
        const double z = slab_z(*q[k], x, y), th = slab_thickness(*q[k], z);
        if(z0 < z - .02 && z1 > z - th + .02 && !(z - z0 <= .42 && z0 >= z - th)) return true;
    }
    return false;
}
bool CollisionWorld::feature_solid_at(double x, double y, double z0, double z1, double r) const {   // levelFeatureSolidAt (8423)
    // The legacy reads per-column lists holding every solid within 0.6 m; testing the solids of the
    // 4 m cells the disc touches gives the same answer for r <= 0.6.
    const auto cx0 = static_cast<long long>(std::floor((x - r) / cell_)), cx1 = static_cast<long long>(std::floor((x + r) / cell_));
    const auto cy0 = static_cast<long long>(std::floor((y - r) / cell_)), cy1 = static_cast<long long>(std::floor((y + r) / cell_));
    for(long long cy = cy0; cy <= cy1; ++cy) for(long long cx = cx0; cx <= cx1; ++cx)
        for(const auto id : cell(solid_grid_, (static_cast<double>(cx) + .5) * cell_, (static_cast<double>(cy) + .5) * cell_)) {
            const auto& s = solids_[id];
            if(z1 < s.z0 + .015 || z0 > s.z1 - .015) continue;
            if(r > 0 ? inside_or_edge(x, y, s.poly, s.box, r) : inside(x, y, s.poly, s.box)) return true;
        }
    return false;
}
bool CollisionWorld::in_cavity(double x, double y) const {   // worldInCavity (8281)
    for(const auto id : cell(cavity_grid_, x, y)) if(inside(x, y, cavities_[id].poly, cavities_[id].box)) return true;
    return false;
}
std::size_t CollisionWorld::slab_query(double x, double y, std::array<const Slab*, 64>& out) const {   // slabQuery (8267)
    std::size_t n = 0;
    for(const auto id : cell(slab_grid_, x, y)) {
        const auto& s = slabs_[id];
        if(inside_or_edge(x, y, s.poly, s.box, .02)) { if(n == out.size()) throw std::runtime_error("more than 64 slabs at one point"); out[n++] = &s; }
    }
    return n;
}
double CollisionWorld::slab_z(const Slab& s, double x, double y) {   // worldSlabZ (8249)
    if(!s.has_plane) return s.z;
    double u = ((x - s.ox) * s.dx + (y - s.oy) * s.dy) / s.len;
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    return s.z0 + (s.z1 - s.z0) * u;
}
double CollisionWorld::slab_thickness(const Slab& s, double z) { return s.has_base ? std::max(.12, z - s.base) : s.thickness; }   // worldSlabThickness (8254)
double CollisionWorld::floor_height(double x, double y, double feet) const {   // levelFloorHeight (8350)
    const double limit = feet + .52;
    double best = -1000;
    std::array<const Slab*, 64> q;
    const auto n = slab_query(x, y, q);
    for(std::size_t k = 0; k < n; ++k) { const double z = slab_z(*q[k], x, y); if(z <= limit && z > best) best = z; }
    return best;
}
std::optional<Surface> CollisionWorld::support(double x, double y, double feet, double step) const {   // stage4Support (8476)
    std::array<const Slab*, 64> q;
    const auto n = slab_query(x, y, q);
    const double lim = feet + step + .003;
    const Slab* best = nullptr;
    double bz = 0;
    for(std::size_t k = 0; k < n; ++k) {
        const double z = slab_z(*q[k], x, y);
        if(z > lim) continue;
        if(!best || z > bz) { best = q[k]; bz = z; }
    }
    if(!best) return std::nullopt;
    return Surface{bz, best};
}
bool CollisionWorld::walkable_radius(double x, double y, double r) const {   // stage4WalkableRadius (8492)
    if(!walkable(x, y)) return false;
    const double d = r * .707;
    return walkable(x + r, y) && walkable(x - r, y) && walkable(x, y + r) && walkable(x, y - r) &&
           walkable(x + d, y + d) && walkable(x - d, y + d) && walkable(x + d, y - d) && walkable(x - d, y - d);
}
bool CollisionWorld::wall_hit(double x, double y, double feet, double height, double r) const {   // stage4WallHit (8501)
    // The legacy picks 0.5 m acceleration lists or the 4 m bucket; both contain every wall within
    // `padded` of the point, so the result is that of testing all walls.
    const double lo = feet + r, hi = feet + height - r, padded = r + .15;
    const auto* ids = wall_bucket(x, y);
    if(!ids) return false;
    for(const auto id : *ids) {
        const auto& w = walls_[id];
        const double gap = w.z0 > hi ? w.z0 - hi : w.z1 < lo ? lo - w.z1 : 0;
        if(gap >= padded) continue;
        const double d2 = distance_sq_to_wall(x, y, w);
        if(d2 + gap * gap < padded * padded) return true;
    }
    return false;
}
bool CollisionWorld::capsule_clear(double x, double y, double feet, double height, double r) const {   // stage4CapsuleClear (8526)
    if(!std::isfinite(x + y + feet + height)) return false;
    if(!walkable_radius(x, y, r)) return false;
    if(feet < -.45 && !in_cavity(x, y)) return false;   // only carved cavities exist below the L2 datum
    if(wall_hit(x, y, feet, height, r)) return false;
    std::array<const Slab*, 64> q;
    const auto n = slab_query(x, y, q);
    for(std::size_t k = 0; k < n; ++k) {
        const double z = slab_z(*q[k], x, y);
        if(feet < z - kSlabTolerance && feet + height > z - slab_thickness(*q[k], z) + .025) return false;
    }
    return true;
}
std::optional<double> CollisionWorld::ceiling_plane(double x, double y, double from_head, double to_head) const {   // stage4CeilingPlane (8541)
    double plane = std::numeric_limits<double>::infinity();
    std::array<const Slab*, 64> q;
    const auto n = slab_query(x, y, q);
    for(std::size_t k = 0; k < n; ++k) {
        const double z = slab_z(*q[k], x, y);
        const double underside = z - slab_thickness(*q[k], z);
        if(underside >= from_head - .055 && underside <= to_head + .012) plane = std::min(plane, underside);
    }
    if(!std::isfinite(plane)) return std::nullopt;
    return plane;
}
std::optional<Surface> CollisionWorld::landing_plane(double x, double y, double from_feet, double to_feet) const {   // stage4LandingPlane (8551)
    std::array<const Slab*, 64> q;
    const auto n = slab_query(x, y, q);
    std::optional<Surface> best;
    for(std::size_t k = 0; k < n; ++k) {
        const double z = slab_z(*q[k], x, y);
        if(z > from_feet + .045 || z < to_feet - .045) continue;
        if(!best || z > best->z) best = Surface{z, q[k]};
    }
    return best;
}
std::optional<WallContact> CollisionWorld::nearest_wall(double x, double y, double feet, double height) const {   // stage4NearestWall (8561)
    const auto* ids = wall_bucket(x, y);
    if(!ids) return std::nullopt;
    std::optional<WallContact> closest;
    double best = std::numeric_limits<double>::infinity();
    for(const auto id : *ids) {
        const auto& w = walls_[id];
        if(w.z1 < feet - .15 || w.z0 > feet + height + .15) continue;
        const double ux = w.bx - w.ax, uy = w.by - w.ay, len = ux * ux + uy * uy;
        const double t = len != 0 ? std::max(0.0, std::min(1.0, ((x - w.ax) * ux + (y - w.ay) * uy) / len)) : 0;
        const double qx = w.ax + t * ux, qy = w.ay + t * uy;
        const double ex = x - qx, ey = y - qy, d = ex * ex + ey * ey;
        if(d < best) { best = d; closest = WallContact{&w, qx, qy, d}; }
    }
    return closest;
}
}  // namespace cat::physics
