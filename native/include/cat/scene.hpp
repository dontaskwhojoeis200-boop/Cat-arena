#pragma once
// N2 render-scene preparation. Platform and GPU independent: no SDL, no Vulkan.
// Converts the N1 baked map/material/texture data into renderer-ready streams while
// reproducing the legacy WebGL rules (legacy/index.html line references in scene.cpp).
#include "cat/assets.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cat::scene {

// ---------------------------------------------------------------- images
// RGBA8, first row in memory = first row uploaded (= texture coordinate t=0 in GL and Vulkan).
struct Image {
    std::uint32_t width{}, height{};
    std::vector<std::uint8_t> rgba;
    bool operator==(const Image&) const = default;
};
Image decode_image(std::span<const std::uint8_t> bytes);   // PNG / JPEG, decoded top row first
Image load_image(const std::filesystem::path&);
Image load_raw_rgba(const std::filesystem::path&);         // N1 headerless square RGBA8 (.rgba)
void flip_rows(Image&);
std::vector<Image> mip_chain(const Image&);                // level 0 + 2x2 box-filtered levels to 1x1
void write_png(const std::filesystem::path&, const Image&);
// Legacy Q3R.rebuildWorld: 512x512 atlas -> 16 layers of 128x128, rows flipped per layer.
std::vector<Image> world_atlas_layers(const Image& atlas);

// ---------------------------------------------------------------- legacy view (q3ViewCamera / render())
using V3 = std::array<double, 3>;
inline constexpr double kEyeHeight = 1.5;     // Q3_VIEW.eye
inline constexpr double kDefaultFov = 105.0;  // setFov default, horizontal degrees
inline constexpr double kNear = 0.05;         // from gl_Position z = depth*1.00025003-0.1000125
inline constexpr double kFar = 400.0;
struct Camera {
    V3 position{}, forward{}, right{}, up{};
    double yaw{}, pitch{}, fov_deg{kDefaultFov}, aspect{1};
    double tan_half_x{}, tan_half_y{};
};
// Basis exactly as legacy q3ViewCamera with zero bob/roll: forward=(ca*cp, sa*cp, -sp),
// right=(-sa, ca, 0), up=(ca*sp, sa*sp, cp). Note `right` is world-left of `forward` in the
// right-handed Z-up frame; the legacy compensates with gl.frontFace(CW). Kept for parity.
Camera legacy_camera(double x, double y, double feet_z, double yaw, double pitch, double hfov_deg, double aspect);
// Vulkan clip coordinates producing the same window depth as the legacy GL projection.
std::array<double, 4> clip_position(const Camera&, const V3& world);
// Legacy inView(): generous cone test used to skip static clusters.
bool in_view(const Camera&, double x, double y, double z, double radius);

// ---------------------------------------------------------------- material model (Q3MAT / q3-shader)
enum class WaveFunc : std::uint8_t { sin, triangle, square, sawtooth, inverse_sawtooth, noise };
struct Wave { WaveFunc func{}; double base{}, amp{}, phase{}, freq{}; };
double wave_table(WaveFunc, double x);
double noise1(double t);
double eval_wave(const Wave&, double time, double extra_phase = 0);

enum class TcModType : std::uint8_t { scroll, scale, rotate, stretch, transform, turb, entity_translate };
struct TcMod {
    TcModType type{};
    double s{}, t{}, speed{};
    Wave wave{};
    std::array<double, 4> m{1, 0, 0, 1};
    std::array<double, 2> translate{};
    double amp{}, phase{}, freq{};
};
struct TexMatrix {   // s' = a*s + b*t + e ; t' = c*s + d*t + f
    double a{1}, b{0}, c{0}, d{1}, e{0}, f{0};
    std::array<double, 2> apply(double s, double t) const { return {a * s + b * t + e, c * s + d * t + f}; }
};
TexMatrix tcmod_matrix(const TcMod&, double time);
TexMatrix compose(const TexMatrix& after, const TexMatrix& before);   // after(before(st))

enum class Source : std::int32_t { material = 0, lightmap = 1, white = 2, mirror = 3, world = 4, portal_feed = 5, kit = 6, vertex_light = 7 };
enum class RgbGen : std::uint8_t { identity, identity_lighting, constant, wave, entity, one_minus_entity, vertex, exact_vertex, one_minus_vertex, lighting_diffuse };
enum class AlphaGen : std::uint8_t { identity, constant, wave, entity, one_minus_entity, vertex, one_minus_vertex, lighting_specular, portal };
enum class TcGen : std::int32_t { base = 0, lightmap = 1, environment = 2, vector = 3 };
enum class AlphaFunc : std::int32_t { none = 0, gt0 = 1, lt128 = 2, ge128 = 3 };
int rgb_code(RgbGen);     // legacy RGB_CODE
int alpha_code(AlphaGen); // legacy ALPHA_CODE

struct Stage {
    std::string map;
    std::vector<std::string> maps;   // animMap frames (empty = single map)
    double anim_freq{};
    Source source{Source::material};
    std::uint32_t world_layer{};
    bool has_blend{};
    int blend_src{1}, blend_dst{0};   // legacy BLEND_TABLE codes 0..10
    AlphaFunc alpha_func{};
    bool depth_write{true}, depth_equal{}, clamp{}, detail{};
    RgbGen rgb{};
    std::array<double, 3> rgb_const{1, 1, 1};
    Wave rgb_wave{};
    AlphaGen alpha{};
    double alpha_const{1};
    Wave alpha_wave{};
    double portal_range{256};
    TcGen tcgen{};
    std::array<double, 3> tc_s{}, tc_t{};
    std::vector<TcMod> tcmods;
};
struct StageTexture { TexMatrix pre, post; bool turb{}; double turb_amp{}, turb_phase{}, turb_freq{}; };
StageTexture stage_texture(const Stage&, double time);           // Q3Shader.stageTexMatrixInto
std::array<double, 4> stage_color(const Stage&, double time);    // Q3Shader.stageColorInto(overbright=1)

enum class DeformType : std::int32_t { wave = 1, bulge = 2, move = 3, normal = 4, autosprite = 10, autosprite2 = 11 };
struct Deform { DeformType type{}; Wave wave{}; double spread{}, width{}, height{}, speed{}, amp{}, freq{}; std::array<double, 3> vec{}; };
enum class Cull : std::uint8_t { back, front, none };
struct SkyParams { bool present{}; double cloud_height{128}; std::string farbox, nearbox; };

struct Material {
    std::string name;
    int id{-1}, sort{3};
    Cull cull{Cull::back};
    bool translucent{}, portal{}, mirror{}, polygon_offset{};
    int autosprite{};   // 0 none, 1 autosprite, 2 autosprite2
    std::vector<Deform> deforms;
    std::vector<Stage> stages;
    SkyParams sky;
    double sky_rotate{};
    // Q3MAT.resolve()
    bool vertex_lit{}, lightmapped{}, all_add{}, has_detail{}, md3{};
};
Material parse_material(const assets::Value&);
std::vector<const Stage*> active_stages(const Material&, bool detail_textures);
bool can_collapse(const Material&, std::size_t stage_count);   // drawMaterial canCollapse
// fogModeFor: 0 blend to fog colour, 1 additive fade, 2 alpha fade, 3 none, 4 filter fade to white.
int fog_mode(bool has_blend, int blend_src, int blend_dst, bool collapsed_opaque);

// ---------------------------------------------------------------- legacy world vertex lighting
// The legacy bakeMap() runs lightVertexColor(g) on the world stream before lightmap charting.
// N1 captured the stream before that call, so N2 reproduces it from baked collision data.
class WalkGrid {
public:
    static constexpr int size = 117;   // legacy SIZE
    explicit WalkGrid(const assets::Value& compiled);
    bool walkable(double x, double y) const;   // levelWalkable: footprint && !massive solid
    bool map_cell(double x, double y) const;   // mapCell: true = blocked (also outside the grid)
private:
    const assets::Value* compiled_{};
    std::vector<std::uint8_t> blocked_;
};
// Multiplier applied to world vertex RGB. vertex14 = N1 world layout (14 floats).
double legacy_vertex_light(std::span<const float> vertex14, const WalkGrid&);

// ---------------------------------------------------------------- prepared map scene
struct Theme { std::array<float, 3> sky{}, fog{}; };
// THEMES[mapIndex]; createMap() is always called with mapIndex (0+3)%3 = 0 for WorldKit maps.
inline constexpr Theme kLegacyTheme0{{.54F, .31F, .17F}, {.67F, .36F, .21F}};

struct Cluster {
    std::uint32_t material{};      // index into MapScene::materials
    std::uint32_t first{}, count{};
    V3 center{};
    double radius{};
};
struct MaterialBinding { std::vector<std::vector<std::uint32_t>> stage_layers; };   // per stage, per animMap frame
struct Md3FrameVertex { V3 position, normal; };
struct Md3Tag { std::string name; V3 origin; std::array<V3, 3> axes; };
struct Md3Surface {
    std::string material;
    std::vector<std::array<float, 2>> uv;
    std::vector<std::uint32_t> indices;
    std::vector<Md3FrameVertex> vertices;   // frames packed consecutively
};
struct Md3Model {
    std::size_t frame_count{};
    std::vector<std::vector<Md3Tag>> tags;
    std::vector<Md3Surface> surfaces;
};
Md3Model decode_md3(std::span<const std::uint8_t> bytes);

struct PropPart {
    std::size_t model{};
    std::vector<Cluster> draws;   // material and index ranges in the shared prop index buffer
    std::size_t first_vertex{};
    std::size_t parent{}, tag{};
    double fps{};
};
struct MapProp {
    V3 position, ambient, directed, direction;
    double yaw{}, scale{1}, radius{}, phase{};
    bool turret{};
    std::vector<PropPart> parts;
};
struct PropScene {
    std::vector<Md3Model> models;
    std::vector<MapProp> placements;
    std::vector<std::uint32_t> indices;
    std::size_t vertex_count{};
};
struct PropMotion { double last_time{}, yaw{}; bool initialized{}; };
inline constexpr std::size_t kMaterialVertexStride = 18;
void animate_props(const PropScene&, std::span<PropMotion>, std::span<float> vertices, double time, const V3* player);
void load_props(PropScene&, const assets::Value& document, const std::filesystem::path& root,
                const std::function<std::uint32_t(const std::string&)>& material_index);
struct SceneStats {
    std::string world_atlas_source;
    std::size_t world_triangles{}, sky_triangles{}, material_triangles{}, clusters{}, materials{};
    std::size_t textures256{}, textures512{}, lit_vertices_changed{};
    double vertex_light_min{1}, vertex_light_max{1};
};
struct MapScene {
    std::string id, sky_material;
    Theme theme{kLegacyTheme0};
    std::array<double, 2> sky_center{58.5, 58.5};   // WORLD.meta.center; sky bake eye z = 2
    double sky_eye_z{2}, sun_azimuth{-.62};
    assets::Mesh world, sky, materials, clouds;    // clouds: 18-float dome (legacy skyGeometry)
    std::vector<Material> material_defs;          // used materials + sky material (last)
    std::vector<MaterialBinding> bindings;        // parallel to material_defs
    std::vector<Cluster> clusters;                // legacy buildStatic order: (sort, id), then insertion
    PropScene props;
    std::size_t sky_material_index{};
    Image lightmap;
    // N4: image sky box (skyParms <farbox> other than $baked). Empty for baked-geometry skies.
    Image sky_box;                 // width N, height 6N
    std::string sky_box_name;      // farbox name; set even when the image is missing
    bool sky_box_missing{};        // named box requested but no image found
    double sky_fog{};              // WORLD.meta.sky.horizonFog (uSkyFog of the box shader)
    std::vector<Image> world_layers, layers256, layers512;
    std::vector<std::string> names256, names512;
    std::vector<std::array<double, 4>> spawns;    // x, y, z, yaw
    SceneStats stats;
};
struct SceneOptions {
    // The legacy world samples the 16-layer array captured at startup (Q3R.applyTextures, legacy
    // 2996) BEFORE paintWorldMaterials (8980) repaints texCanvas; N1 baked the repainted canvas.
    // N2 ships that runtime capture as assets/n2/world-atlas-runtime.png. Empty = N1 atlas (warns).
    std::filesystem::path runtime_world_atlas;
    // N4: directory with extracted image sky boxes ("skybox-<farbox with / as ->.png", six faces
    // stacked rt bk lf ft up dn). The Arsenal's box is painted by a JavaScript generator in the legacy
    // (Q3Tex.skyBoxes), captured by tools/n4/legacy_skybox.cjs into assets/n4. Empty = not available.
    std::filesystem::path sky_box_dir;
};
std::filesystem::path default_sky_box_dir(const std::filesystem::path& asset_root);   // <assets>/n4 next to n1, if present
MapScene load_map_scene(const std::filesystem::path& asset_root, std::string_view map_id, const SceneOptions& options = {});
// assets/n1 -> sibling assets/n2/world-atlas-runtime.png when it exists, else empty.
std::filesystem::path default_runtime_world_atlas(const std::filesystem::path& asset_root);
assets::Mesh sky_dome(std::string_view sky_material, double sun_azimuth);   // skyGeometry + setSkyDomeColors

}  // namespace cat::scene
