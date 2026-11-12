// N4 navigation tests. GPU-free.
//   cat_arena_nav_tests <assets/n1> <map-id> <map_navigation.bin>
// Builds the native layered navigation graph for the map and compares it with the graph and routes
// dumped from the unmodified legacy game (tools/n4/legacy_navigation.cjs): every node, the adjacency
// of all three walker profiles, start/goal node choice, A* expansion count, node chain and smoothed
// route of every query. Then checks properties that must hold on any map, and the region lookup.
#include "cat/navigation.hpp"
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
struct Reader {
    std::vector<char> data;
    std::size_t at{};
    void need(std::size_t n) const { if(at + n > data.size()) throw std::runtime_error("navigation fixture truncated"); }
    std::uint32_t u32() { need(4); std::uint32_t v; std::memcpy(&v, data.data() + at, 4); at += 4; return v; }
    std::vector<double> f64(std::size_t n) { need(n * 8); std::vector<double> v(n); if(n) std::memcpy(v.data(), data.data() + at, n * 8); at += n * 8; return v; }
    std::string text(std::size_t n) { need(n); std::string s(data.data() + at, n); at += n; return s; }
};
bool same(double a, double b) { return std::memcmp(&a, &b, 8) == 0 || (a == 0 && b == 0); }
}  // namespace

int main(int argc, char** argv) {
    if(argc != 4) { std::cerr << "usage: cat_arena_nav_tests <assets/n1> <map-id> <map_navigation.bin>\n"; return 2; }
    try {
        const std::string map = argv[2];
        const auto doc = assets::load_data(std::string(argv[1]) + "/maps/" + map + "/map.cadata");
        const physics::CollisionWorld world(doc.at("compiled"));
        nav::NavGraph graph(world);
        std::ifstream in(argv[3], std::ios::binary);
        if(!in) throw std::runtime_error(std::string("cannot open ") + argv[3]);
        Reader r;
        r.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if(r.text(8) != "CATNAV01" || r.u32() != 1) throw std::runtime_error("unsupported navigation fixture");
        const auto node_count = r.u32(), query_count = r.u32();
        // ---- nodes ----
        const auto legacy_nodes = r.f64(std::size_t(node_count) * 6);
        check(graph.nodes().size() == node_count, "node count: native " + std::to_string(graph.nodes().size()) + ", legacy " + std::to_string(node_count));
        for(std::uint32_t i = 0; i < node_count; ++i) {
            const auto& n = graph.nodes()[i];
            const double* l = &legacy_nodes[std::size_t(i) * 6];
            check(n.id == i && same(n.x, l[0]) && same(n.y, l[1]) && same(n.z, l[2]) && n.gx == int(l[3]) && n.gy == int(l[4]) &&
                  static_cast<int>(n.layer) * 10 + static_cast<int>(n.kind) == int(l[5]), "node " + std::to_string(i) + " differs from the legacy");
        }
        const auto layers = graph.layer_counts();
        // ---- adjacency per walker profile ----
        const nav::Profile* profiles[3] = {&nav::kLight, &nav::kStandard, &nav::kHeavy};
        std::size_t edges[3]{};
        for(int p = 0; p < 3; ++p) {
            const auto legacy_edges = r.u32(), legacy_hash = r.u32();
            const auto counts = r.f64(node_count);
            std::uint32_t hash = 2166136261U;
            auto mix = [&](std::uint32_t w) { hash = (hash ^ w) * 16777619U; };
            for(std::uint32_t id = 0; id < node_count; ++id) {
                const auto& list = graph.neighbors(id, *profiles[p]);
                check(static_cast<double>(list.size()) == counts[id], std::string(profiles[p]->id) + ": neighbour count of node " + std::to_string(id) + " is " +
                      std::to_string(list.size()) + ", legacy " + std::to_string(int(counts[id])));
                for(const auto& e : list) {
                    std::uint32_t words[2]; std::memcpy(words, &e.cost, 8);
                    mix(id); mix(e.to); mix(words[0]); mix(words[1]); ++edges[p];
                }
            }
            check(edges[p] == legacy_edges && hash == legacy_hash, std::string(profiles[p]->id) + ": adjacency (neighbour ids, order or costs) differs from the legacy");
        }
        check(edges[0] >= edges[1] && edges[1] >= edges[2], "smaller walkers must not have fewer links than larger ones");
        // ---- routes ----
        std::size_t found = 0, between_floors = 0, longest = 0;
        for(std::uint32_t q = 0; q < query_count; ++q) {
            const auto h = r.f64(12);
            const auto chain = r.f64(r.u32());
            const auto route = r.f64(std::size_t(r.u32()) * 3);
            const auto& p = *profiles[static_cast<std::size_t>(h[0])];
            const std::string name = "query " + std::to_string(q) + " (" + p.id + ")";
            const double floor = graph.target_floor(h[4], h[5], h[6]);
            check(same(floor, h[7]), name + ": target floor " + std::to_string(floor) + ", legacy " + std::to_string(h[7]));
            const auto* start = graph.nearest(h[1], h[2], h[3], p);
            const auto* goal = graph.nearest(h[4], h[5], floor, p, true);
            check((start ? double(start->id) : -1.0) == h[8] && (goal ? double(goal->id) : -1.0) == h[9], name + ": start/goal node differs from the legacy");
            if(!start || !goal) { check(h[10] == 0 && chain.empty(), name + ": legacy found a route without start/goal"); continue; }
            const auto path = graph.search(start->id, goal->id, p);
            check(double(path.found) == h[10] && double(path.expanded) == h[11], name + ": search result/expansions " + std::to_string(path.expanded) + ", legacy " + std::to_string(int(h[11])));
            check(path.nodes.size() == chain.size(), name + ": chain length");
            for(std::size_t i = 0; i < chain.size(); ++i) check(double(path.nodes[i]) == chain[i], name + ": chain node " + std::to_string(i) + " differs");
            if(!path.found) continue;
            const auto smooth = graph.smooth({h[1], h[2], h[3]}, path.nodes, {h[4], h[5], floor}, p);
            check(smooth.size() * 3 == route.size(), name + ": smoothed route length " + std::to_string(smooth.size()) + ", legacy " + std::to_string(route.size() / 3));
            for(std::size_t i = 0; i < smooth.size(); ++i)
                check(same(smooth[i][0], route[i * 3]) && same(smooth[i][1], route[i * 3 + 1]) && same(smooth[i][2], route[i * 3 + 2]), name + ": route point " + std::to_string(i) + " differs");
            // properties: every leg of the smoothed route is traversable, and route() gives the same answer
            nav::Point at{h[1], h[2], h[3]};
            for(const auto& w : smooth) { check(graph.traverse(at[0], at[1], at[2], w[0], w[1], w[2], p), name + ": smoothed leg is not traversable"); at = w; }
            const auto direct = graph.route({h[1], h[2], h[3]}, {h[4], h[5], h[6]}, p);
            check(direct.size() == smooth.size(), name + ": route() differs from nearest+search+smooth");
            ++found; longest = std::max(longest, path.nodes.size());
            if(std::fabs(h[3] - floor) > 1) ++between_floors;
        }
        if(r.at != r.data.size()) throw std::runtime_error("trailing bytes in navigation fixture");
        // ---- regions (rooms / sectors) ----
        const nav::RegionMap regions(doc.at("compiled"));
        std::size_t spawns_in_region = 0, nodes_in_region = 0;
        for(const auto& sp : world.spawns()) spawns_in_region += regions.region_at(sp.x, sp.y) != nullptr;
        for(const auto& n : graph.nodes()) nodes_in_region += regions.region_at(n.x, n.y) != nullptr;
        check(!regions.regions().empty() && regions.region_at(-5, -5) == nullptr, "regions load; a point outside the map has none");
        std::cout << "PASS navigation " << map << ": " << node_count << " nodes (L1 " << layers[0] << ", L2 " << layers[1] << ", L3 " << layers[2] << ") identical to legacy; edges light/standard/heavy "
                  << edges[0] << '/' << edges[1] << '/' << edges[2] << " identical; " << query_count << " route queries identical (" << found << " routes, " << between_floors
                  << " between floors, longest chain " << longest << " nodes); regions " << regions.regions().size() << ", spawns inside a region " << spawns_in_region << '/' << world.spawns().size()
                  << ", nav nodes inside a region " << nodes_in_region << '/' << node_count << "; " << checks << " checks\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
