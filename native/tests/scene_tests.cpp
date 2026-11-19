// N2 scene-preparation tests. No GPU. With an asset root argument, also validates the real
// Helios Crucible scene built from the N1 bake.
#include "cat/scene.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
using namespace cat;
using namespace cat::scene;
namespace {
void check(bool value, const std::string& why) { if(!value) throw std::runtime_error(why); }
bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }
template<class F> void reject(F f, const char* why) {
    bool failed = false;
    try { f(); } catch(const std::exception&) { failed = true; }
    check(failed, why);
}
assets::Value obj(assets::Value::Object o) { return assets::Value(std::move(o)); }
assets::Value str(const char* s) { return assets::Value(std::string(s)); }
assets::Value num(double d) { return assets::Value(d); }
assets::Value arr(assets::Value::Array a) { return assets::Value(std::move(a)); }
assets::Value null() { return assets::Value{}; }

assets::Value stage(const char* map, assets::Value blend, const char* rgb = "identity") {
    return obj({{"map", str(map)}, {"maps", null()}, {"animFreq", num(0)}, {"blend", std::move(blend)},
                {"alphaFunc", null()}, {"depthWrite", assets::Value(true)}, {"depthFunc", str("lequal")},
                {"clamp", assets::Value(false)}, {"detail", assets::Value(false)},
                {"rgbGen", obj({{"type", str(rgb)}})}, {"alphaGen", obj({{"type", str("identity")}})},
                {"tcGen", obj({{"type", str("base")}})}, {"tcMods", arr({})}});
}
assets::Value material(const char* name, assets::Value::Array stages, bool translucent, const char* sprite = nullptr) {
    assets::Value::Array deforms;
    if(sprite) deforms.push_back(obj({{"type", str(sprite)}}));
    return obj({{"name", str(name)}, {"id", num(4)}, {"sort", num(translucent ? 9 : 3)}, {"cull", str("back")},
                {"translucent", assets::Value(translucent)}, {"autosprite", sprite ? str(sprite) : null()},
                {"deforms", arr(std::move(deforms))}, {"stages", arr(std::move(stages))}, {"sky", null()}, {"skyRotate", num(0)},
                {"portal", assets::Value(false)}, {"mirror", assets::Value(false)}, {"polygonOffset", assets::Value(false)}});
}

void waves_and_tcmods() {
    check(near(wave_table(WaveFunc::sin, .25), 1) && near(wave_table(WaveFunc::sin, 1.25), 1), "sin table wraps");
    check(near(wave_table(WaveFunc::triangle, .5), 0) && near(wave_table(WaveFunc::triangle, .25), 1), "triangle");
    check(wave_table(WaveFunc::square, .3) == 1 && wave_table(WaveFunc::square, .7) == -1, "square");
    check(near(wave_table(WaveFunc::inverse_sawtooth, .25), .75), "inverse sawtooth");
    // Golden values: legacy NOISE LCG (seed 0x51ed2701) stored as Float32Array, computed independently.
    check(noise1(0) == double(0.39152008295059204F) && noise1(1) == double(-0.577292799949646F), "noise table golden");
    check(near(noise1(.5), -0.09288635849952698, 1e-12), "noise smoothstep interpolation");
    check(noise1(256) == noise1(0) && noise1(-256) == noise1(0), "noise period 256");
    check(near(eval_wave({WaveFunc::sin, .82, .18, 0, .4}, .625), 1.0), "wave base+amp at peak");
    std::cout << "PASS waves: tables, period, noise golden values\n";

    TcMod scroll{TcModType::scroll}; scroll.s = .25; scroll.t = -1.6;
    const auto m = tcmod_matrix(scroll, 3);   // .75, -4.8 -> fract .2
    check(near(m.e, .75) && near(m.f, .2, 1e-12), "scroll fract");
    TcMod scale{TcModType::scale}; scale.s = 1; scale.t = 6;
    Stage st; st.tcmods = {scale, scroll};
    const auto tex = stage_texture(st, 3);
    // Sequential reference (applyTcModsReference): scale then scroll.
    const auto composed = tex.pre.apply(.3, .4);
    const auto seq = tcmod_matrix(scroll, 3).apply(tcmod_matrix(scale, 3).apply(.3, .4)[0], tcmod_matrix(scale, 3).apply(.3, .4)[1]);
    check(near(composed[0], seq[0]) && near(composed[1], seq[1]), "tcMod composition order");
    TcMod turb{TcModType::turb}; turb.amp = .06; turb.freq = .18;
    st.tcmods = {scale, turb, scroll};
    const auto t2 = stage_texture(st, 3);
    check(t2.turb && near(t2.turb_amp, .06) && near(t2.post.e, .75) && near(t2.pre.d, 6), "turb splits pre/post");
    TcMod rot{TcModType::rotate}; rot.speed = 90;
    const auto r = tcmod_matrix(rot, 1).apply(.5, .5);
    check(near(r[0], .5) && near(r[1], .5), "rotate about texture centre");
    std::cout << "PASS tcMods: scroll fract, composition order, turb split, rotation centre\n";
}

void camera_projection() {
    const auto c = legacy_camera(10, 20, 0, 0, 0, 105, 16.0 / 9.0);
    check(near(c.position[2], 1.5), "eye height");
    check(near(c.forward[0], 1) && near(c.right[1], 1) && near(c.up[2], 1), "legacy basis at yaw 0");
    auto ndc = [&](V3 p) { auto q = clip_position(c, p); return std::array<double, 3>{q[0] / q[3], q[1] / q[3], q[2] / q[3]}; };
    check(near(ndc({10 + kNear, 20, 1.5})[2], 0, 1e-6) && near(ndc({10 + kFar, 20, 1.5})[2], 1, 1e-6), "near/far map to Vulkan 0..1");
    const auto ahead = ndc({20, 20, 1.5});
    check(near(ahead[0], 0) && near(ahead[1], 0) && ahead[2] > 0 && ahead[2] < 1, "centre of view");
    check(ndc({20, 21, 1.5})[0] > 0, "legacy right vector -> +x");
    check(ndc({20, 20, 2.5})[1] < 0, "world up -> Vulkan -y (top of framebuffer)");
    const double edge = std::tan(52.5 * 3.14159265358979323846 / 180) * 10;
    check(near(ndc({20, 20 + edge, 1.5})[0], 1, 1e-9), "horizontal FOV 105");
    check(in_view(c, 30, 20, 1.5, 0) && !in_view(c, -10, 20, 1.5, 0) && in_view(c, 8.9, 20, 1.5, 0), "inView cone and margin");
    reject([] { (void)legacy_camera(0, 0, 0, NAN, 0, 105, 1); }, "nonfinite camera accepted");
    std::cout << "PASS camera: legacy basis, Vulkan depth/clip mapping, FOV, inView\n";
}

void images() {
    Image atlas{512, 512, std::vector<std::uint8_t>(512U * 512U * 4U)};
    for(std::uint32_t y = 0; y < 512; ++y)
        for(std::uint32_t x = 0; x < 512; ++x) {
            auto* p = &atlas.rgba[(std::size_t(y) * 512 + x) * 4];
            p[0] = static_cast<std::uint8_t>(x / 128); p[1] = static_cast<std::uint8_t>(y / 128);
            p[2] = static_cast<std::uint8_t>(y % 128); p[3] = 255;
        }
    const auto layers = world_atlas_layers(atlas);
    check(layers.size() == 16, "16 layers");
    const auto& l6 = layers[6];   // tile (2,1)
    check(l6.rgba[0] == 2 && l6.rgba[1] == 1 && l6.rgba[2] == 127, "layer row 0 = tile bottom row (flip)");
    check(l6.rgba[(127U * 128U) * 4 + 2] == 0, "layer row 127 = tile top row");
    Image small{4, 2, {}};
    for(std::uint8_t i = 0; i < 32; ++i) small.rgba.push_back(i);
    auto flipped = small; flip_rows(flipped); flip_rows(flipped);
    check(flipped == small, "flip involution");
    const auto mips = mip_chain(Image{4, 4, std::vector<std::uint8_t>(64, 200)});
    check(mips.size() == 3 && mips[2].width == 1 && mips[2].rgba[0] == 200, "mip chain to 1x1");
    reject([] { (void)decode_image(std::vector<std::uint8_t>{1, 2, 3}); }, "garbage image accepted");
    reject([] { (void)world_atlas_layers(Image{256, 256, std::vector<std::uint8_t>(256U * 256U * 4U)}); }, "wrong atlas size");
    std::cout << "PASS images: atlas layer extraction/flip, row flip, mips, decode rejection\n";
}

void materials() {
    auto kit = parse_material(material("models/bastion/BA_x", {stage("models/bastion/BA_x", null(), "identityLighting"),
                                                                stage("$vertexlight", arr({num(4), num(0)}))}, false));
    check(kit.vertex_lit && kit.lightmapped && !kit.all_add && can_collapse(kit, 2), "vertex-lit kit resolve/collapse");
    check(kit.stages[1].source == Source::vertex_light && kit.stages[0].source == Source::material, "stage sources");
    check(fog_mode(false, 1, 0, true) == 0, "collapsed opaque fog");
    auto lamp = parse_material(material("lamp", {stage("$whiteimage", arr({num(1), num(1)}))}, true, "autosprite"));
    check(lamp.all_add && lamp.autosprite == 1 && lamp.cull == Cull::none && !can_collapse(lamp, 1), "additive sprite resolve");
    check(fog_mode(true, 1, 1, false) == 1 && fog_mode(true, 6, 7, false) == 2 && fog_mode(true, 4, 0, false) == 4 &&
          fog_mode(true, 2, 3, false) == 3, "fog modes");
    auto glass = parse_material(material("glass", {stage("$world:3", arr({num(6), num(7)})), stage("$lightmap", arr({num(4), num(0)}))}, true));
    check(!can_collapse(glass, 2) && glass.stages[0].source == Source::world && glass.stages[0].world_layer == 3, "translucent multipass");
    reject([] { (void)parse_material(material("bad", {stage("$world:16", null())}, false)); }, "world layer 16 accepted");
    reject([] { (void)parse_material(material("bad", {stage("$unknown", null())}, false)); }, "unknown special map accepted");
    reject([] { (void)parse_material(material("bad", {stage("x", arr({num(4), num(11)}))}, false)); }, "blend code accepted");
    Stage s; s.rgb = RgbGen::wave; s.rgb_wave = {WaveFunc::sin, .9, .5, 0, 1};
    check(stage_color(s, .25)[0] == 1, "rgbGen wave clamps to 1");
    std::cout << "PASS materials: resolve, collapse, fog modes, sprites, validation\n";
}

void vertex_light() {
    // Synthetic compiled world: square footprint 2..115, one massive block at 50..54 x 50..54.
    auto square = [](double a, double b) { return arr({arr({num(a), num(a)}), arr({num(b), num(a)}), arr({num(b), num(b)}), arr({num(a), num(b)})}); };
    assets::Value::Array grid(32 * 32, arr({}));
    for(int y = 12; y <= 13; ++y) for(int x = 12; x <= 13; ++x) grid[std::size_t(y * 32 + x)] = arr({num(0)});
    auto compiled = obj({{"footprint", arr({square(2, 115)})}, {"cell", num(4)}, {"gw", num(32)}, {"solidGrid", arr(std::move(grid))},
                         {"solids", arr({obj({{"massive", assets::Value(true)}, {"poly", square(50, 54)}})})}});
    const WalkGrid walk(compiled);
    check(walk.walkable(30, 30) && !walk.walkable(52, 52) && !walk.walkable(1, 1), "walkability");
    check(walk.map_cell(-1, 5) && walk.map_cell(117, 5) && walk.map_cell(52, 52) && !walk.map_cell(30, 30), "mapCell");
    auto vtx = [](double x, double y, double z, double nx, double ny, double nz, float mat) {
        std::array<float, 14> v{};
        v[0] = float(x); v[1] = float(y); v[2] = float(z); v[3] = float(nx); v[4] = float(ny); v[5] = float(nz); v[11] = mat;
        return v;
    };
    // Expected values computed independently from the legacy formula.
    check(near(legacy_vertex_light(vtx(30, 30, 0, 0, 0, 1, 2), walk), .8719, 1e-6), "open floor");
    check(near(legacy_vertex_light(vtx(30, 30, 8, 0, 0, -1, 1), walk), .5451, 1e-6), "ceiling");
    check(near(legacy_vertex_light(vtx(30, 30, .5, 1, 0, 0, 1), walk), .6241, 1e-6), "low wall");
    check(legacy_vertex_light(vtx(30, 30, 0, 0, 0, 1, 4), walk) == 1, "lamp untouched");
    // Floor cell (49.5,52.5): x+0.61 -> cell 50 blocked; x-0.61 -> 48 free; y+-0.61 -> 49 free. 1 blocked.
    check(near(legacy_vertex_light(vtx(49.5, 52.5, 0, 0, 0, 1, 2), walk), (.79 + .63 * .13) * (1 - .062), 1e-6), "contact occlusion");
    check(legacy_vertex_light(vtx(30, 30, 0, 0, 0, 1, 3), walk) > .87, "CEILING material has no occlusion term");
    std::cout << "PASS vertex light: walk grid, mapCell bounds, lightVertexColor golden values\n";
}

void helios(const std::filesystem::path& root) {
    SceneOptions options;
    options.runtime_world_atlas = default_runtime_world_atlas(root);
    check(!options.runtime_world_atlas.empty(), "assets/n2/world-atlas-runtime.png missing");
    const auto s = load_map_scene(root, "helios-crucible", options);
    check(s.stats.world_atlas_source.starts_with("runtime capture"), "runtime world atlas not used");
    check(s.stats.world_triangles == 7308 && s.stats.sky_triangles == 1142 && s.stats.material_triangles == 161014, "Helios triangle counts");
    check(s.lightmap.width == 1024 && s.lightmap.height == 567 && s.world_layers.size() == 16, "lightmap + atlas");
    check(s.spawns.size() == 28 && s.sky_material == "sky/reactor_bastion", "spawns + sky");
    check(s.stats.vertex_light_min >= .48 && s.stats.vertex_light_max <= 1.12 && s.stats.lit_vertices_changed > 1000, "vertex light range");
    std::uint64_t covered = 0;
    int last_sort = -1, last_id = -1;
    std::set<std::uint32_t> materials_seen;
    for(const auto& c : s.clusters) {
        const auto& m = s.material_defs.at(c.material);
        check(m.sort > last_sort || (m.sort == last_sort && m.id >= last_id), "clusters sorted by (sort, id)");
        last_sort = m.sort; last_id = m.id;
        check(c.first == covered && c.count % 3 == 0 && c.count > 0, "contiguous cluster ranges");
        covered += c.count;
        materials_seen.insert(c.material);
    }
    check(covered == s.materials.indices.size(), "clusters cover every material triangle");
    for(auto i : s.materials.indices) check(i < s.materials.vertex_count(), "material index bounds");
    check(materials_seen.size() == 33, "33 Helios surface materials");
    for(std::size_t i = 0; i < s.material_defs.size(); ++i) {
        const auto& m = s.material_defs[i];
        for(std::size_t k = 0; k < m.stages.size(); ++k) {
            const auto& st = m.stages[k];
            for(auto layer : s.bindings[i].stage_layers[k]) {
                if(st.source == Source::material) check(layer < s.layers256.size(), "256 layer bounds " + m.name);
                if(st.source == Source::kit) check(layer < s.layers512.size(), "512 layer bounds " + m.name);
            }
        }
        if(m.translucent && i != s.sky_material_index) check(m.all_add, "Helios translucent surface materials are all additive: " + m.name);
    }
    std::cout << "PASS helios: " << s.stats.world_triangles << " world / " << s.stats.material_triangles << " material / "
              << s.stats.sky_triangles << " sky triangles, " << s.stats.clusters << " clusters, " << s.stats.materials
              << " materials, " << s.stats.textures256 << "x256 + " << s.stats.textures512 << "x512 textures, vertex light "
              << s.stats.vertex_light_min << ".." << s.stats.vertex_light_max << " on " << s.stats.lit_vertices_changed << " vertices\n";
    reject([&] { (void)load_map_scene(root, "not-a-map"); }, "unknown map accepted");

    SceneOptions arsenal_options = options;
    arsenal_options.sky_box_dir = default_sky_box_dir(root);
    const auto arsenal = load_map_scene(root, "q3-gallery", arsenal_options);
    const auto& props = arsenal.props;
    check(props.models.size() == 4 && props.placements.size() == 3, "Arsenal rigs share four original MD3 files");
    check(props.indices.size() / 3 == 502 && props.vertex_count == 452, "all seven Arsenal prop parts are present");
    std::vector<PropMotion> motion(props.placements.size());
    std::vector<float> vertices(props.vertex_count * kMaterialVertexStride);
    animate_props(props, motion, vertices, 0, nullptr);
    const auto initial = vertices;
    const V3 nearby{90, 58, 0};
    animate_props(props, motion, vertices, .125, &nearby);
    const auto& turret = props.placements.at(1);
    const auto head = turret.parts.at(1).first_vertex * kMaterialVertexStride;
    check(!std::equal(vertices.begin(), vertices.begin() + 60 * kMaterialVertexStride, initial.begin()),
          "flag vertices interpolate across frames");
    check(motion[1].yaw < 0 && motion[1].yaw >= -.25 && vertices[head] != initial[head], "turret turns toward a nearby player at the legacy rate");
    check(std::all_of(vertices.begin(), vertices.end(), [](float v) { return std::isfinite(v); }), "transformed vertices stay finite");
    const std::array<std::uint8_t, 108> invalid{};
    reject([&] { (void)decode_md3(invalid); }, "invalid MD3 header accepted");
    std::cout << "PASS Arsenal: three animated prop rigs, 502 triangles and shared source models\n";
}
}  // namespace

int main(int argc, char** argv) {
    try {
        waves_and_tcmods();
        camera_projection();
        images();
        materials();
        vertex_light();
        if(argc > 1) helios(argv[1]);
        return 0;
    } catch(const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
