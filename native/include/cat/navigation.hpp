#pragma once
// N4 stacked world navigation: a port of the legacy "NAV3" layered graph (legacy/index.html
// 6147-6470). Nodes sit on every walkable floor surface of a 1 m grid, so two floors above each
// other give two nodes in the same cell; edges are accepted only when a walker of the given size can
// really traverse them with the player collision (cat::physics::CollisionWorld). Three walker
// profiles (light, standard, heavy) have their own adjacency. A* and route smoothing follow the
// legacy search exactly (same heap, same float32 g-scores, same expansion limit) so that enemies in
// N6 take the routes the legacy enemies take. Verified against graphs and routes dumped from the
// unmodified legacy game for all four maps (tools/n4/legacy_navigation.cjs).
// Also: room/sector lookup from the baked regions.
#include "cat/physics.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cat::nav {
inline constexpr double kMapSize = 117;   // legacy SIZE
inline constexpr double kStep = 1.0;      // NAV3_STEP
inline constexpr std::uint32_t kMaxSearch = 16000;   // NAV3_MAX_SEARCH expansions

struct Profile { int index; const char* id; double radius, height, max_rise, slope; };   // NAV3_PROFILES
inline constexpr Profile kLight{0, "light", .27, 1.55, .66, .68};
inline constexpr Profile kStandard{1, "standard", .32, 1.76, .56, .61};
inline constexpr Profile kHeavy{2, "heavy", .48, 2.08, .48, .54};

enum class Layer : std::uint8_t { l1, l2, l3 };              // z <= -2, between, z >= 4.5
enum class SurfaceKind : std::uint8_t { floor, bridge, ramp };
struct Node {
    std::uint32_t id{};
    double x{}, y{}, z{};
    int gx{}, gy{};
    Layer layer{Layer::l2};
    SurfaceKind kind{SurfaceKind::floor};
    const physics::Slab* surface{};
};
struct Edge { std::uint32_t to{}; double cost{}; };
struct FloorSurface { double z{}; const physics::Slab* slab{}; SurfaceKind kind{SurfaceKind::floor}; };
struct Path {
    bool found{};
    std::vector<std::uint32_t> nodes;   // node after the start ... goal (the start itself is not included)
    std::uint32_t expanded{};
};
using Point = std::array<double, 3>;

class NavGraph {
public:
    explicit NavGraph(const physics::CollisionWorld& world);   // nav3Build
    const std::vector<Node>& nodes() const { return nodes_; }
    std::array<std::size_t, 3> layer_counts() const { return layer_counts_; }
    // Walkable (nav) floor surfaces under a point, lowest first (levelFloorSurfacesAt).
    std::vector<FloorSurface> floor_surfaces(double x, double y) const;
    // nav3Neighbors: computed on first use per node and profile, then cached.
    const std::vector<Edge>& neighbors(std::uint32_t id, const Profile& p);
    std::size_t precompute(const Profile& p);   // all nodes; returns the number of edges
    // nav3Traverse: can a walker move in a straight line from a to b, following the real floor?
    bool traverse(double x0, double y0, double z0, double x1, double y1, double z1, const Profile& p) const;
    // nav3Nearest: node to start or end a route at; null when none is reachable nearby.
    const Node* nearest(double x, double y, double z, const Profile& p, bool allow_distant = false) const;
    // nav3TargetFloor: the floor a (possibly airborne) target counts as standing on.
    double target_floor(double x, double y, double z) const;
    // A* as in processNavJobs. start == goal gives found with an empty node list.
    Path search(std::uint32_t start, std::uint32_t goal, const Profile& p);
    // The same search in slices, as the legacy runs it (processNavJobs gives every tick a work budget):
    // search_begin resets the scratch buffers; each search_pop takes one node off the open list and
    // returns closed (already expanded, nothing done), expanded, or goal; search_chain rebuilds the
    // node chain (node after the start ... goal; nullopt when the parent links do not reach the start).
    enum class Pop { closed, expanded, goal };
    void search_begin(std::uint32_t start, std::uint32_t goal);
    bool search_open_empty() const { return heap_id_.empty(); }
    Pop search_pop(std::uint32_t goal, const Profile& p);
    std::optional<std::vector<std::uint32_t>> search_chain(std::uint32_t start, std::uint32_t goal) const;
    // nav3ApplyRoute: string-pull the node chain from `from`, appending `target` when reachable.
    std::vector<Point> smooth(const Point& from, const std::vector<std::uint32_t>& chain, const Point& target, const Profile& p) const;
    // Convenience: nearest nodes, search, smoothing. Empty when no route exists.
    std::vector<Point> route(const Point& from, const Point& target, const Profile& p);
private:
    bool surface_valid(double x, double y, double z, const Profile& p) const;
    bool link(const Node& a, const Node& b, const Profile& p) const;
    const std::vector<std::uint32_t>* cell(int cx, int cy) const;
    const physics::CollisionWorld* world_;
    int grid_width_{};
    std::vector<Node> nodes_;
    std::vector<std::vector<std::uint32_t>> cells_;
    std::array<std::size_t, 3> layer_counts_{};
    std::array<std::vector<std::vector<Edge>>, 3> adjacency_;
    std::array<std::vector<std::uint8_t>, 3> adjacency_ready_;
    // search scratch (one search at a time, as in the legacy)
    std::vector<float> g_;
    std::vector<std::int32_t> parent_;
    std::vector<std::uint8_t> closed_;
    std::vector<std::int32_t> heap_id_;   // open list of the sliced search
    std::vector<double> heap_pri_;
};

// Rooms / sectors of a map (compiled.regions): named polygons used for location names and, later,
// for encounter logic. region_at reproduces a first-match lookup over the baked 4 m region grid.
struct Region { std::string id, title, type; std::vector<std::array<double, 2>> poly; std::array<double, 4> box{}; };
class RegionMap {
public:
    explicit RegionMap(const assets::Value& compiled);
    const std::vector<Region>& regions() const { return regions_; }
    const Region* region_at(double x, double y) const;
private:
    double cell_{4};
    long long gw_{};
    std::vector<Region> regions_;
    std::vector<std::vector<std::uint32_t>> grid_;
};
}  // namespace cat::nav
