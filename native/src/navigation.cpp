// N4 stacked navigation graph: port of the legacy NAV3 (legacy/index.html 6147-6470).
// Function names of the legacy are given in comments; floating-point order follows the source.
#include "cat/navigation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cat::nav {
namespace {
using physics::js_hypot;
constexpr int kDirections[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};   // NAV3_DIRECTIONS
using physics::js_hypot3;
double heuristic(const Node& a, const Node& b) { return js_hypot3(a.x - b.x, a.y - b.y, (a.z - b.z) * 1.5); }   // nav3Heuristic
// Binary heap with the legacy sift rules (nav3HeapPush / nav3HeapPop): equal priorities keep
// their legacy order, which decides which of several equally good routes is found.
struct Heap {
    std::vector<std::int32_t> id;
    std::vector<double> pri;
    void push(std::int32_t node, double priority) {
        id.push_back(0); pri.push_back(0);
        std::size_t i = id.size() - 1;
        while(i > 0) { const std::size_t parent = (i - 1) >> 1; if(pri[parent] <= priority) break; id[i] = id[parent]; pri[i] = pri[parent]; i = parent; }
        id[i] = node; pri[i] = priority;
    }
    std::int32_t pop() {
        const std::int32_t root = id[0];
        const std::size_t n = id.size() - 1;
        if(n > 0) {
            const std::int32_t tid = id[n]; const double tp = pri[n];
            std::size_t i = 0;
            while(i * 2 + 1 < n) { std::size_t c = i * 2 + 1; if(c + 1 < n && pri[c + 1] < pri[c]) ++c; if(pri[c] >= tp) break; id[i] = id[c]; pri[i] = pri[c]; i = c; }
            id[i] = tid; pri[i] = tp;
        }
        id.pop_back(); pri.pop_back();
        return root;
    }
};
bool point_in(double x, double y, const std::vector<std::array<double, 2>>& poly) {
    bool hit = false;
    for(std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const auto& a = poly[i]; const auto& c = poly[j];
        if((a[1] > y) != (c[1] > y) && x < (c[0] - a[0]) * (y - a[1]) / (c[1] - a[1]) + a[0]) hit = !hit;
    }
    return hit;
}
}  // namespace

std::vector<FloorSurface> NavGraph::floor_surfaces(double x, double y) const {   // levelFloorSurfacesAt (8341)
    std::array<const physics::Slab*, 64> q;
    const auto n = world_->slab_query(x, y, q);
    std::vector<FloorSurface> out;
    for(std::size_t k = 0; k < n; ++k) {
        if(!q[k]->nav) continue;
        out.push_back({physics::CollisionWorld::slab_z(*q[k], x, y), q[k], q[k]->has_plane ? SurfaceKind::ramp : q[k]->kind == "floor" ? SurfaceKind::floor : SurfaceKind::bridge});
    }
    std::stable_sort(out.begin(), out.end(), [](const FloorSurface& a, const FloorSurface& b) { return a.z < b.z; });
    return out;
}
bool NavGraph::surface_valid(double x, double y, double z, const Profile& p) const { return world_->capsule_clear(x, y, z, p.height, p.radius); }   // nav3SurfaceValid
const std::vector<std::uint32_t>* NavGraph::cell(int cx, int cy) const {
    // The legacy keys cells by cy * gridWidth + cx without range checks; with cx in [-1, gridWidth]
    // out-of-range keys can only alias cells that hold no nodes (rows/columns 0 and >= 116 are never filled).
    if(cx < 0 || cy < 0 || cx >= grid_width_ || cy >= grid_width_) return nullptr;
    const auto& c = cells_[static_cast<std::size_t>(cy) * static_cast<std::size_t>(grid_width_) + static_cast<std::size_t>(cx)];
    return c.empty() ? nullptr : &c;
}

NavGraph::NavGraph(const physics::CollisionWorld& world) : world_(&world) {   // nav3Build (6176)
    grid_width_ = static_cast<int>(std::ceil(kMapSize / kStep)) + 2;
    cells_.assign(static_cast<std::size_t>(grid_width_) * static_cast<std::size_t>(grid_width_), {});
    const int max = static_cast<int>(std::ceil(kMapSize / kStep));
    const Profile& p = kLight;
    for(int cy = 1; cy < max - 1; ++cy)
        for(int cx = 1; cx < max - 1; ++cx) {
            const double x = (cx + .5) * kStep, y = (cy + .5) * kStep;
            if(!world.walkable(x, y)) continue;
            const auto surfaces = floor_surfaces(x, y);
            std::vector<double> seen;
            for(const auto& s : surfaces) {
                if(std::any_of(seen.begin(), seen.end(), [&](double t) { return std::fabs(t - s.z) < .14; })) continue;
                if(!surface_valid(x, y, s.z, p)) continue;
                Node n;
                n.id = static_cast<std::uint32_t>(nodes_.size());
                n.x = x; n.y = y; n.z = s.z; n.gx = cx; n.gy = cy; n.kind = s.kind; n.surface = s.slab;
                n.layer = s.z <= -2 ? Layer::l1 : s.z >= 4.5 ? Layer::l3 : Layer::l2;
                nodes_.push_back(n); seen.push_back(s.z);
                cells_[static_cast<std::size_t>(cy) * static_cast<std::size_t>(grid_width_) + static_cast<std::size_t>(cx)].push_back(n.id);
            }
        }
    for(const auto& n : nodes_) ++layer_counts_[static_cast<std::size_t>(n.layer)];
    for(auto& a : adjacency_) a.assign(nodes_.size(), {});
    for(auto& r : adjacency_ready_) r.assign(nodes_.size(), 0);
}

bool NavGraph::traverse(double x0, double y0, double z0, double x1, double y1, double z1, const Profile& p) const {   // nav3Traverse (6216)
    if(std::fabs(z1 - z0) > js_hypot(x1 - x0, y1 - y0) * p.slope + .16) return false;
    const double distance = js_hypot(x1 - x0, y1 - y0);
    const int steps = static_cast<int>(std::max(1.0, std::ceil(distance / .22)));
    double feet = z0;
    for(int i = 1; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps, x = x0 + (x1 - x0) * t, y = y0 + (y1 - y0) * t, expected = z0 + (z1 - z0) * t;
        // Slopes are followed by grounding against the real floor, like the player controller.
        const auto s = world_->support(x, y, feet, p.max_rise);
        if(!s || s->slab->kind == "equipment" || s->z < feet - physics::kGroundSnap - .01) return false;
        const double z = s->z;
        if(std::fabs(z - expected) > .56 || !surface_valid(x, y, z, p)) return false;
        feet = z;
    }
    return std::fabs(feet - z1) <= .42;
}
bool NavGraph::link(const Node& a, const Node& b, const Profile& p) const {   // nav3Link (6232)
    if(std::fabs(a.z - b.z) > p.max_rise + .08) return false;
    if(std::fabs(a.z - b.z) > .07 && std::fabs(a.z - b.z) > js_hypot(a.x - b.x, a.y - b.y) * p.slope + .05) return false;
    if(a.gx != b.gx && a.gy != b.gy) {   // no diagonal corner cut when a wall divides both cardinal routes
        auto has_height = [&](const std::vector<std::uint32_t>* ids) {
            if(!ids) return false;
            return std::any_of(ids->begin(), ids->end(), [&](std::uint32_t id) { return std::fabs(nodes_[id].z - (a.z + b.z) * .5) <= p.max_rise + .14; });
        };
        if(!has_height(cell(b.gx, a.gy)) || !has_height(cell(a.gx, b.gy))) return false;
    }
    return traverse(a.x, a.y, a.z, b.x, b.y, b.z, p);
}
const std::vector<Edge>& NavGraph::neighbors(std::uint32_t id, const Profile& p) {   // nav3ComputeNeighbors (6273)
    const auto pi = static_cast<std::size_t>(p.index);
    auto& list = adjacency_.at(pi).at(id);
    if(adjacency_ready_[pi][id]) return list;
    const Node& a = nodes_[id];
    for(const auto& d : kDirections) {
        const auto* ids = cell(a.gx + d[0], a.gy + d[1]);
        if(!ids) continue;
        for(const auto n : *ids) {
            const Node& b = nodes_[n];
            if(!link(a, b, p)) continue;
            list.push_back({n, js_hypot(b.x - a.x, b.y - a.y) * (1 + std::fabs(b.z - a.z) * .24)});
        }
    }
    adjacency_ready_[pi][id] = 1;
    return list;
}
std::size_t NavGraph::precompute(const Profile& p) {
    std::size_t edges = 0;
    for(std::uint32_t id = 0; id < nodes_.size(); ++id) edges += neighbors(id, p).size();
    return edges;
}

const Node* NavGraph::nearest(double x, double y, double z, const Profile& p, bool allow_distant) const {   // nav3Nearest (6303)
    const int cx = static_cast<int>(std::floor(x / kStep)), cy = static_cast<int>(std::floor(y / kStep)), max_ring = allow_distant ? 9 : 5;
    const Node* best = nullptr;
    double score = std::numeric_limits<double>::infinity();
    for(int ring = 0; ring <= max_ring; ++ring) {
        for(int oy = -ring; oy <= ring; ++oy)
            for(int ox = -ring; ox <= ring; ++ox) {
                if(std::max(std::abs(ox), std::abs(oy)) != ring) continue;
                const auto* ids = cell(cx + ox, cy + oy);
                if(!ids) continue;
                for(const auto id : *ids) {
                    const Node& n = nodes_[id];
                    const double d = js_hypot(n.x - x, n.y - y), dz = std::fabs(n.z - z);
                    if(dz > 1.2 || d > max_ring + 1 || !surface_valid(n.x, n.y, n.z, p)) continue;
                    const double candidate = d + dz * 5;
                    if(candidate >= score) continue;
                    if(d > .02 && !traverse(x, y, z, n.x, n.y, n.z, p)) continue;
                    best = &n; score = candidate;
                }
            }
        if(best && ring > 1) break;
    }
    return best;
}
double NavGraph::target_floor(double x, double y, double z) const {   // nav3TargetFloorUncached (6331)
    bool found = false;
    double best = 0, distance = std::numeric_limits<double>::infinity();
    for(const auto& s : floor_surfaces(x, y)) {
        if(s.z > z + .55) continue;
        const double delta = std::fabs(s.z - z);
        if(delta < distance) { distance = delta; best = s.z; found = true; }
    }
    return found ? best : world_->floor_height(x, y, z);
}

Path NavGraph::search(std::uint32_t start, std::uint32_t goal, const Profile& p) {   // nav3StartJob + processNavJobs + nav3Finish
    Path out;
    if(start >= nodes_.size() || goal >= nodes_.size()) return out;
    const std::size_t n = nodes_.size();
    g_.assign(n, std::numeric_limits<float>::infinity());   // Float32Array: g-scores are single precision in the legacy
    parent_.assign(n, -1);
    closed_.assign(n, 0);
    Heap heap;
    g_[start] = 0;
    heap.push(static_cast<std::int32_t>(start), heuristic(nodes_[start], nodes_[goal]));
    bool success = false;
    while(true) {
        if(heap.id.empty() || out.expanded > kMaxSearch) break;
        const auto id = static_cast<std::uint32_t>(heap.pop());
        if(closed_[id]) continue;
        closed_[id] = 1; ++out.expanded;
        if(id == goal) { success = true; break; }
        for(const auto& e : neighbors(id, p)) {
            if(closed_[e.to]) continue;
            const double alt = static_cast<double>(g_[id]) + e.cost;
            if(alt >= static_cast<double>(g_[e.to])) continue;
            parent_[e.to] = static_cast<std::int32_t>(id);
            g_[e.to] = static_cast<float>(alt);
            heap.push(static_cast<std::int32_t>(e.to), alt + heuristic(nodes_[e.to], nodes_[goal]));
        }
    }
    if(!success) return out;
    std::int32_t id = static_cast<std::int32_t>(goal);
    for(int i = 0; i < 4000 && id != static_cast<std::int32_t>(start) && id >= 0; ++i) { out.nodes.push_back(static_cast<std::uint32_t>(id)); id = parent_[static_cast<std::size_t>(id)]; }
    if(id != static_cast<std::int32_t>(start)) { out.nodes.clear(); return out; }
    std::reverse(out.nodes.begin(), out.nodes.end());
    out.found = true;
    return out;
}

void NavGraph::search_begin(std::uint32_t start, std::uint32_t goal) {   // nav3StartJob (6441-6445)
    const std::size_t n = nodes_.size();
    g_.assign(n, std::numeric_limits<float>::infinity());
    parent_.assign(n, -1);
    closed_.assign(n, 0);
    Heap heap;
    g_[start] = 0;
    heap.push(static_cast<std::int32_t>(start), heuristic(nodes_[start], nodes_[goal]));
    heap_id_ = std::move(heap.id); heap_pri_ = std::move(heap.pri);
}
NavGraph::Pop NavGraph::search_pop(std::uint32_t goal, const Profile& p) {   // one pass of the processNavJobs loop body
    Heap heap{std::move(heap_id_), std::move(heap_pri_)};
    Pop result = Pop::closed;
    const auto id = static_cast<std::uint32_t>(heap.pop());
    if(!closed_[id]) {
        closed_[id] = 1;
        if(id == goal) result = Pop::goal;
        else {
            result = Pop::expanded;
            for(const auto& e : neighbors(id, p)) {
                if(closed_[e.to]) continue;
                const double alt = static_cast<double>(g_[id]) + e.cost;
                if(alt >= static_cast<double>(g_[e.to])) continue;
                parent_[e.to] = static_cast<std::int32_t>(id);
                g_[e.to] = static_cast<float>(alt);
                heap.push(static_cast<std::int32_t>(e.to), alt + heuristic(nodes_[e.to], nodes_[goal]));
            }
        }
    }
    heap_id_ = std::move(heap.id); heap_pri_ = std::move(heap.pri);
    return result;
}
std::optional<std::vector<std::uint32_t>> NavGraph::search_chain(std::uint32_t start, std::uint32_t goal) const {   // nav3Finish (6371-6376)
    std::vector<std::uint32_t> chain;
    std::int32_t id = static_cast<std::int32_t>(goal);
    for(int i = 0; i < 4000 && id != static_cast<std::int32_t>(start) && id >= 0; ++i) { chain.push_back(static_cast<std::uint32_t>(id)); id = parent_[static_cast<std::size_t>(id)]; }
    if(id != static_cast<std::int32_t>(start)) return std::nullopt;
    std::reverse(chain.begin(), chain.end());
    return chain;
}

std::vector<Point> NavGraph::smooth(const Point& from, const std::vector<std::uint32_t>& chain, const Point& target, const Profile& p) const {   // nav3ApplyRoute (6386)
    std::vector<Point> route;
    Point anchor = from;
    for(std::size_t i = 0; i < chain.size();) {
        // Smoothing may skip nodes only when the complete 3D height sweep works.
        std::size_t j = std::min(chain.size() - 1, i + 10);
        for(; j > i; --j) { const Node& n = nodes_[chain[j]]; if(traverse(anchor[0], anchor[1], anchor[2], n.x, n.y, n.z, p)) break; }
        const Node& n = nodes_[chain[j]];
        route.push_back({n.x, n.y, n.z}); anchor = {n.x, n.y, n.z}; i = j + 1;
    }
    if(traverse(anchor[0], anchor[1], anchor[2], target[0], target[1], target[2], p)) route.push_back(target);
    return route;
}
std::vector<Point> NavGraph::route(const Point& from, const Point& target, const Profile& p) {
    const double tz = target_floor(target[0], target[1], target[2]);
    const Node* start = nearest(from[0], from[1], from[2], p);
    const Node* goal = nearest(target[0], target[1], tz, p, true);
    if(!start || !goal) return {};
    const auto path = search(start->id, goal->id, p);
    if(!path.found) return {};
    return smooth(from, path.nodes, {target[0], target[1], tz}, p);
}

RegionMap::RegionMap(const assets::Value& c) {
    cell_ = c.at("cell").number();
    gw_ = static_cast<long long>(c.at("gw").number());
    for(const auto& r : c.at("regions").array()) {
        Region o;
        auto text = [&](const char* key) { const auto& obj = r.object(); const auto it = obj.find(key); return it != obj.end() && std::holds_alternative<std::string>(it->second.data) ? it->second.string() : std::string(); };
        o.id = text("id"); o.title = text("title"); o.type = text("type");
        for(const auto& pt : r.at("poly").array()) o.poly.push_back({pt.array().at(0).number(), pt.array().at(1).number()});
        if(o.poly.size() < 3) throw std::runtime_error("region polygon with fewer than 3 points");
        o.box = {o.poly[0][0], o.poly[0][1], o.poly[0][0], o.poly[0][1]};
        for(const auto& q : o.poly) { o.box[0] = std::min(o.box[0], q[0]); o.box[1] = std::min(o.box[1], q[1]); o.box[2] = std::max(o.box[2], q[0]); o.box[3] = std::max(o.box[3], q[1]); }
        regions_.push_back(std::move(o));
    }
    for(const auto& cell : c.at("regionGrid").array()) {
        std::vector<std::uint32_t> ids;
        for(const auto& id : cell.array()) {
            const double v = id.number();
            if(v < 0 || v >= static_cast<double>(regions_.size())) throw std::runtime_error("region grid index out of range");
            ids.push_back(static_cast<std::uint32_t>(v));
        }
        grid_.push_back(std::move(ids));
    }
    if(gw_ <= 0 || grid_.size() != static_cast<std::size_t>(gw_ * gw_)) throw std::runtime_error("region grid size mismatch");
}
const Region* RegionMap::region_at(double x, double y) const {
    const double fx = std::floor(x / cell_), fy = std::floor(y / cell_);
    if(!(fx >= 0) || !(fy >= 0) || fx >= static_cast<double>(gw_) || fy >= static_cast<double>(gw_)) return nullptr;
    for(const auto id : grid_[static_cast<std::size_t>(fy) * static_cast<std::size_t>(gw_) + static_cast<std::size_t>(fx)]) {
        const auto& r = regions_[id];
        if(x >= r.box[0] && x <= r.box[2] && y >= r.box[1] && y <= r.box[3] && point_in(x, y, r.poly)) return &r;
    }
    return nullptr;
}
}  // namespace cat::nav
