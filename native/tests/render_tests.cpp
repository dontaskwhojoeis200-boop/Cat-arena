// Offscreen Vulkan tests of the real Helios Crucible scene. Exit 77 when no Vulkan device exists
// (CTest reports SKIPPED; this is never counted as a pass).
#include "cat/renderer.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace cat;
namespace {
void check(bool value, const std::string& why) { if(!value) throw std::runtime_error(why); }
std::size_t differing(const scene::Image& a, const scene::Image& b) {
    std::size_t n = 0;
    for(std::size_t i = 0; i < a.rgba.size(); i += 4)
        if(a.rgba[i] != b.rgba[i] || a.rgba[i + 1] != b.rgba[i + 1] || a.rgba[i + 2] != b.rgba[i + 2]) ++n;
    return n;
}
const std::uint8_t* pixel(const scene::Image& img, std::uint32_t x, std::uint32_t y) { return &img.rgba[(std::size_t(y) * img.width + x) * 4]; }
}

int main(int argc, char** argv) {
    if(argc != 3) { std::cerr << "usage: cat_arena_render_tests <shader-dir> <assets/n1>\n"; return 2; }
    Settings settings; settings.validation = true;
    std::unique_ptr<Renderer> renderer;
    try { renderer = std::make_unique<Renderer>(320, 180, settings, argv[1]); }
    catch(const std::exception& e) {
        // Only a missing driver/device is a skip (VK_ERROR_INCOMPATIBLE_DRIVER = -9). Anything else,
        // e.g. a requested validation layer that fails to load, is a real failure.
        const std::string what = e.what();
        if(what.find("No Vulkan") != std::string::npos || what.find("VkResult=-9") != std::string::npos || what.find("loader required") != std::string::npos) {
            std::cout << "SKIP no usable Vulkan device: " << what << '\n';
            return 77;
        }
        std::cerr << "FAIL renderer creation: " << what << '\n';
        return 1;
    }
    try {
        scene::SceneOptions options;
        options.runtime_world_atlas = scene::default_runtime_world_atlas(argv[2]);
        options.sky_box_dir = scene::default_sky_box_dir(argv[2]);
        const auto s = scene::load_map_scene(argv[2], "helios-crucible", options);
        std::cout << "world atlas: " << s.stats.world_atlas_source << '\n';
        renderer->load_scene(s);
        const auto dev = renderer->report();
        std::cout << "device=" << dev.device << " (" << dev.device_type << ") api=" << dev.api << " validation=" << (dev.validation ? "on" : "off") << '\n';
        auto render = [&](const scene::Camera& cam, double t, RenderFeatures f = {}) { renderer->draw(FrameView{cam, t, f}); return renderer->capture(); };
        const double aspect = 320.0 / 180.0;
        const auto preview = scene::legacy_camera(78, 42, 0, 2.45, .15, 105, aspect);   // legacy menu preview pose

        const auto full = render(preview, 10);
        std::size_t clear = 0;
        double luma = 0;
        for(std::size_t i = 0; i < full.rgba.size(); i += 4) {
            luma += full.rgba[i] + full.rgba[i + 1] + full.rgba[i + 2];
            if(std::abs(full.rgba[i] - 138) <= 1 && std::abs(full.rgba[i + 1] - 79) <= 1 && std::abs(full.rgba[i + 2] - 43) <= 1) ++clear;
        }
        luma /= double(full.rgba.size()) / 4 * 3;
        check(luma > 8 && luma < 240, "preview frame mean luma out of range: " + std::to_string(luma));
        check(clear < full.rgba.size() / 4 / 2, "preview frame is mostly clear colour");
        check(renderer->stats().material_passes > 0 && renderer->stats().visible_clusters > 0, "no material clusters drawn");
        std::cout << "PASS preview frame: mean luma " << luma << ", clear-colour pixels " << clear << ", draws " << renderer->stats().draw_calls << '\n';

        check(render(preview, 10).rgba == full.rgba, "identical inputs must give identical pixels");
        std::cout << "PASS determinism: repeated frame is byte-identical\n";

        for(const char* name : {"lightmap", "fog", "posterize", "materials", "world", "sky"}) {
            RenderFeatures f;
            std::string n = name;
            (n == "lightmap" ? f.lightmap : n == "fog" ? f.fog : n == "posterize" ? f.posterize : n == "materials" ? f.materials : n == "world" ? f.world : f.sky) = false;
            const auto changed = differing(full, render(preview, 10, f));
            check(changed > 0, std::string("disabling ") + name + " changed nothing");
            std::cout << "PASS feature " << name << ": disabling changes " << changed << " pixels\n";
        }

        RenderFeatures lightmap_dim;
        lightmap_dim.lightmap_intensity = .6F;
        check(differing(full, render(preview, 10, lightmap_dim)) > 0, "lightmap intensity changes the frame");
        RenderFeatures normal_light;
        normal_light.overbright = false;
        check(differing(full, render(preview, 10, normal_light)) > 0, "overbright changes the frame");
        RenderFeatures light_fog;
        light_fog.fog_strength = .5F;
        check(differing(full, render(preview, 10, light_fog)) > 0, "fog density changes the frame");

        // Winding / cull orientation: from above a floor, the visible world surface must face up.
        RenderFeatures normals; normals.debug_view = 3; normals.sky = false; normals.materials = false; normals.clouds = false;
        const auto& spawn = s.spawns.at(0);
        const auto down = scene::legacy_camera(spawn[0], spawn[1], spawn[2] + 2, 0, 1.45, 105, aspect);
        const auto n = render(down, 0, normals);
        const auto* c = pixel(n, 160, 90);
        check(c[2] > 230 && std::abs(int(c[0]) - 128) < 24 && std::abs(int(c[1]) - 128) < 24,
              "floor below the camera does not face up (front-face/culling inverted?) rgb=" + std::to_string(c[0]) + "," + std::to_string(c[1]) + "," + std::to_string(c[2]));
        std::cout << "PASS winding: floor seen from above has an up normal (" << int(c[0]) << "," << int(c[1]) << "," << int(c[2]) << ")\n";

        const auto later = render(preview, 12.5);
        std::cout << "INFO material animation: t=10 vs t=12.5 differ in " << differing(full, later) << " pixels\n";
        auto arsenal = scene::load_map_scene(argv[2], "q3-gallery", options);
        RenderFeatures props_only;
        props_only.world = props_only.sky = props_only.culling = false;
        arsenal.clusters.clear();
        renderer->load_scene(arsenal);
        const auto flag = scene::legacy_camera(82.8, 77.5, 6.4, 1.57079632679, 0, 85, aspect);
        const auto flag_initial = render(flag, 0, props_only);
        check(renderer->stats().prop_triangles == 502 && renderer->stats().prop_draws >= 7, "all Arsenal prop parts submitted");
        check(differing(flag_initial, render(flag, .125, props_only)) > 0, "flag animation changes rendered pixels");
        props_only.materials = false;
        check(differing(flag_initial, render(flag, 0, props_only)) > 0, "Arsenal prop geometry reaches the framebuffer");
        std::cout << "PASS Arsenal: visible animated MD3 props on the native material path\n";
        check(renderer->validation_errors() == 0, "validation errors: " + std::to_string(renderer->validation_errors()));
        std::cout << "PASS validation: " << renderer->validation_errors() << " errors, " << renderer->validation_warnings() << " warnings (layer "
                  << (dev.validation ? "active" : "NOT installed") << ")\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
