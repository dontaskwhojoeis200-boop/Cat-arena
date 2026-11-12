#pragma once
#include <array>
// Vulkan renderer. Consumes an immutable scene::MapScene (uploaded once) and a per-frame
// FrameView; never mutates simulation state. Two targets share all pipelines:
//   * window: SDL3 Vulkan surface + swapchain (VSync/mailbox/immediate)
//   * offscreen: device-local colour/depth images with CPU readback, no SDL/surface (captures, CI)
#include "cat/core.hpp"
#include "cat/scene.hpp"
#include "cat/vulkan_caps.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
struct SDL_Window;
namespace cat {
// Individually toggleable stages of the N2 map renderer (each was verified separately).
struct RenderFeatures {
    bool sky{true}, world{true}, materials{true}, clouds{true}, translucent{true};
    bool lightmap{true}, fog{true}, posterize{true}, culling{true};
    int debug_view{0};   // 0 normal, 1 lightmap only, 2 fullbright, 3 normals (legacy graphics-debug views)
    bool dynamic_lights{true};   // N5.1: r_dynamiclight (Q3R.S.dynamicLights)
};
// N5.1 dynamic light as the legacy uploads it (Q3R.uploadLights): at most 16 per frame are used.
struct DynamicLight { float x, y, z, radius, r, g, b; };
struct FrameView {
    scene::Camera camera;
    double time{};   // legacy `now` (seconds of simulated time); drives material waves/scrolls
    RenderFeatures features;
    // Lights of this frame (muzzle flashes, projectiles, explosions), already reduced to the 16 that
    // matter most to the view (cat::fx::select_lights). Applied to the world and to lightmapped
    // opaque materials. Empty = the static N2-N4 lighting, bit for bit.
    const DynamicLight* lights{};
    std::size_t light_count{};
};
// ---- N5 overlay: effects, first-person weapon, HUD. Plain data so the renderer does not depend on
// the gameplay/effect libraries; cat::fx fills it (see main.cpp).
struct OverlayCamera { float pos[3]{}, forward[3]{1, 0, 0}, right[3]{0, 1, 0}, up[3]{0, 0, 1}; float tan_half{1}, aspect{1}; };   // tan_half: vertical
struct OverlayBatch {   // effect quads: 12 floats per vertex (position | u v tile flags | r g b a | additive), triangle indices
    const float* vertices{}; std::size_t vertex_count{};
    const std::uint32_t* indices{}; std::size_t index_count{};
};
struct HudVertex { float x, y, r, g, b, a; };   // normalized device coordinates, x right, y down
struct SolidVertex { float x, y, z, r, g, b, a; };   // world space, opaque, depth tested (practice targets)
// N6: one instanced draw of a creature mesh. `instances` are 112-byte records (cat::fx::EnemyInstance:
// position + facing, state, 16 pose angles, tint). mesh: 0..19 creature type, 20 Marine Cat.
enum class EnemyPass : std::uint8_t { solid, outline, shadow, marine };
struct EnemyBatch { int mesh{}; EnemyPass pass{EnemyPass::solid}; const float* instances{}; std::size_t count{}; };
struct Overlay {
    OverlayCamera camera;              // world camera of the effect batches
    const SolidVertex* solid{}; std::size_t solid_vertices{};   // triangle list, drawn first
    // N5.1 dynamic world-atlas geometry (medkit billboards): legacy 12-float vertices (position,
    // normal, colour, uv, atlas layer), triangle list, opaque with alpha cut-out, two-sided.
    const float* atlas{}; std::size_t atlas_vertices{};
    OverlayBatch mark, main;           // decals (depth-offset), then translucent effects (already ordered back to front)
    bool gun_visible{};
    int gun{};                         // weapon model index
    OverlayCamera gun_camera;          // viewmodel camera (weapon model space)
    float gun_glow[3]{};               // muzzle-flash light on the weapon
    OverlayBatch gun_fx;               // muzzle flash in weapon space (no depth test)
    const HudVertex* hud{}; std::size_t hud_vertices{};   // triangle list, alpha blended, drawn last
    // N6 creatures: instanced batches drawn first, in the given order (legacy: creatures per type,
    // Marine Cats, projected shadows, "final five" outlines).
    const EnemyBatch* enemies{}; std::size_t enemy_batches{};
    float shadow_dir[2]{};             // projected-shadow offset per metre of height
    bool light_grid{true};             // Q3R.S.entityGrid: creatures sample the baked light volume
};
struct WeaponModel { std::vector<float> vertices; scene::Image base, metal_rough, normal; };   // 12 floats per vertex
struct OverlayAssets { scene::Image fx_atlas; std::vector<WeaponModel> weapons; };
// N6 creature meshes: 12 floats per vertex (the fraction of the material id is the limb tag), the
// skeleton (hip, knee, shoulder, elbow, neck, spine, wing root, height, shoulder x, wing tip, depth,
// width) and three textures. `marine` carries the cat-face atlas in `base`.
struct EnemyModel { std::vector<float> vertices; std::array<float, 12> rig{}; scene::Image base, metal_rough, normal; };
struct EnemyAssets { std::vector<EnemyModel> types; EnemyModel marine; };
struct RenderStats {
    std::uint32_t draw_calls{}, material_passes{}, visible_clusters{}, culled_clusters{};
    std::uint32_t overlay_draws{}, effect_quads{}, hud_triangles{}, dynamic_lights{};
    std::uint32_t enemy_draws{}, enemy_instances{};
};
struct DeviceReport {
    std::string device, device_type, driver, api, loader_api, color_format, depth_format, present_mode;
    bool validation{}, anisotropy{};
    float max_anisotropy{};
    std::uint32_t pipelines{};
    // N2.1: what the device offers and which render path was selected at run time.
    std::string driver_name, driver_info;      // VkPhysicalDeviceVulkan12Properties (1.2 core)
    std::string negotiated_api;                // min(device, request cap): core commands up to here may be used
    std::string instance_api;                  // VkApplicationInfo::apiVersion that was requested
    vk::DeviceCaps caps;                       // detected versions, extensions and feature bits
    vk::Path path;                             // selected source per optional feature + reasons
    std::vector<std::string> enabled_extensions;   // device extensions actually enabled
    // Objects the selected path created (the visible effect of each optional feature).
    std::uint32_t render_passes{}, framebuffers{}, material_pipelines{}, descriptor_sets{};
    std::uint64_t staging_texture_bytes{}, host_copy_texture_bytes{};
};
class Renderer {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    // `request` limits the Vulkan path (default: highest path the device supports, up to 1.4).
    // vk::Request::baseline() forces the mandatory Vulkan 1.2 path. A device below 1.2 throws.
    Renderer(SDL_Window*, const Settings&, std::filesystem::path shader_dir, const vk::Request& request = {});
    Renderer(std::uint32_t width, std::uint32_t height, const Settings&, std::filesystem::path shader_dir, const vk::Request& request = {});
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void resize();
    void load_scene(const scene::MapScene&);
    void draw(const Snapshot&);    // N0 path: triangle when no scene is loaded
    void draw(const FrameView&);
    // N5: uploads the effect atlas and the weapon models once (independent of the loaded map), then
    // draw(view, overlay) renders the map followed by: decals, effects, [depth clear] weapon model,
    // muzzle flash, HUD. draw(view) alone renders exactly what it did before N5.
    void load_overlay(const OverlayAssets&);
    void load_enemies(const EnemyAssets&);                 // once; survives load()
    bool enemies_loaded() const;
    void set_light_volume(const scene::Image& atlas);      // per map: the 624 x 234 probe atlas (empty image = flat grey)
    bool overlay_loaded() const;
    void draw(const FrameView&, const Overlay&);
    scene::Image capture();        // offscreen target only: pixels of the last frame (RGBA, top row first)
    std::pair<std::uint32_t, std::uint32_t> extent() const;
    RenderStats stats() const;
    DeviceReport report() const;
    // Validation-layer / debug-utils messages of WARNING or ERROR severity seen so far.
    std::uint32_t validation_warnings() const;
    std::uint32_t validation_errors() const;
};
}
