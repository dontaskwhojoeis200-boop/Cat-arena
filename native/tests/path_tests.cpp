// N2.1 Vulkan path matrix: renders the real Helios Crucible scene on every selectable render path and
// requires (a) the selection to match the detected capabilities, (b) the objects each path creates to
// match its features, (c) byte-identical frames on all paths, (d) no validation errors.
//
//   cat_arena_path_tests <shader-dir> <assets/n1>                 paths selectable on the installed driver
//   cat_arena_path_tests <shader-dir> <assets/n1> --sim <dir>     additionally/instead: simulated older
//        devices through the test-only layer VK_LAYER_CAT_device_sim found in <dir> (Linux)
// Exit 77 when no usable Vulkan device exists (CTest: SKIPPED, never counted as a pass).
#include "cat/renderer.hpp"
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace cat;
namespace {
using F = vk::Feature;
void check(bool value, const std::string& why) { if(!value) throw std::runtime_error(why); }
struct Run {
    DeviceReport report;
    std::vector<scene::Image> frames;
    std::uint32_t errors{}, warnings{};
};
struct Context {
    std::string shaders;
    const scene::MapScene* scene{};
    Run reference;
    bool have_reference{};
    int passed{};
};
Run render(const Context& ctx, const vk::Request& request) {
    Settings settings;
    settings.validation = true;
    Renderer renderer(320, 180, settings, ctx.shaders, request);
    renderer.load_scene(*ctx.scene);
    const double aspect = 320.0 / 180.0;
    const auto& spawn = ctx.scene->spawns.at(0);
    Run run;
    auto shot = [&](const scene::Camera& cam, double t, RenderFeatures f = {}) { renderer.draw(FrameView{cam, t, f}); run.frames.push_back(renderer.capture()); };
    shot(scene::legacy_camera(78, 42, 0, 2.45, .15, 105, aspect), 10);                                  // legacy menu preview pose
    shot(scene::legacy_camera(spawn[0], spawn[1], spawn[2], 1.0, .1, 105, aspect), 3.7);                // animated materials, translucents
    RenderFeatures normals; normals.debug_view = 3;
    shot(scene::legacy_camera(spawn[0], spawn[1], spawn[2] + 2, 0, 1.45, 105, aspect), 0, normals);     // winding / cull state
    shot(scene::legacy_camera(78, 42, 0, 2.45, .15, 105, aspect), 10);                                  // second frame on the same targets
    run.report = renderer.report();
    run.errors = renderer.validation_errors();
    run.warnings = renderer.validation_warnings();
    return run;
}
// Invariants every path must satisfy, whatever was selected.
void verify(Context& ctx, const std::string& name, const vk::Request& request, const Run& run) {
    const auto& r = run.report;
    const auto& p = r.path;
    // (a) selection is exactly what the capability model derives from the detected capabilities
    const auto expected = vk::select_path(r.caps, request);
    check(p.api == expected.api, name + ": negotiated API differs from select_path");
    for(const auto& f : vk::feature_table()) {
        const auto i = static_cast<std::size_t>(f.id);
        check(p.source[i] == expected.source[i] || p.source[i] == vk::Source::unavailable,
              name + ": " + f.name + " selected as " + vk::source_name(p.source[i]) + ", capability model says " + vk::source_name(expected.source[i]));
        if(request.disabled[i]) check(!p.uses(f.id), name + ": disabled feature " + f.name + " is in use");
        if(p.source[i] == vk::Source::core) check(p.api >= f.promoted, name + ": " + f.name + " used as core below its promotion version");
        if(p.source[i] == vk::Source::extension) check(request.allow_extensions && r.caps.has_extension(f.extension), name + ": " + f.name + " extension path without the extension");
    }
    check(p.api <= request.max_api && p.api >= vk::kBaselineApi, name + ": negotiated API outside [1.1, cap]");
    check(vk::api_minor(r.caps.device_api) >= 1, name + ": device below the 1.1 baseline was accepted");
    for(const auto& e : r.enabled_extensions)
        check(r.caps.has_extension(e), name + ": enabled extension the device does not advertise: " + e);
    check(r.enabled_extensions.size() == p.extensions.size(), name + ": enabled extensions differ from the selected path's list");
    if(!request.allow_extensions) check(r.enabled_extensions.empty(), name + ": extension enabled although extensions are not allowed");
    // (b) each feature is visible in the objects created; each fallback too
    check((r.render_passes == 0 && r.framebuffers == 0) == p.uses(F::dynamic_rendering), name + ": render pass / framebuffer objects do not match dynamic_rendering");
    if(!p.uses(F::dynamic_rendering)) check(r.render_passes == 1 && r.framebuffers == 1, name + ": baseline needs exactly one render pass and one offscreen framebuffer");
    check(r.descriptor_sets == (p.uses(F::push_descriptor) ? 1U : 2U), name + ": descriptor set count does not match push_descriptor");
    check((r.host_copy_texture_bytes > 0) == p.uses(F::host_image_copy) && (r.staging_texture_bytes == 0) == p.uses(F::host_image_copy),
          name + ": texture upload route does not match host_image_copy");
    check(r.staging_texture_bytes + r.host_copy_texture_bytes == ctx.reference.report.staging_texture_bytes + ctx.reference.report.host_copy_texture_bytes || !ctx.have_reference,
          name + ": uploaded texture bytes differ between paths");
    check(r.material_pipelines > 0, name + ": no material pipelines");
    // (c) the image does not depend on the path
    if(ctx.have_reference) {
        check(run.frames.size() == ctx.reference.frames.size(), name + ": frame count");
        for(std::size_t i = 0; i < run.frames.size(); ++i)
            check(run.frames[i].rgba == ctx.reference.frames[i].rgba, name + ": frame " + std::to_string(i) + " differs from the reference path");
    }
    check(run.frames[0].rgba == run.frames[3].rgba, name + ": repeated frame is not identical");
    // (d) validation
    check(run.errors == 0, name + ": " + std::to_string(run.errors) + " validation errors");
    ++ctx.passed;
    std::cout << "PASS " << name << ": " << p.describe() << " | device API " << r.api << ", negotiated " << r.negotiated_api << ", extensions enabled "
              << r.enabled_extensions.size() << " | renderPasses " << r.render_passes << ", framebuffers " << r.framebuffers << ", materialPipelines "
              << r.material_pipelines << ", descriptorSets " << r.descriptor_sets << ", staging " << r.staging_texture_bytes << " B, hostCopy "
              << r.host_copy_texture_bytes << " B | frames identical, validation " << run.errors << " errors / " << run.warnings << " warnings"
              << (r.validation ? "" : " (layer NOT installed)") << '\n';
}
vk::Request only(F keep) {
    vk::Request r;
    for(const auto& f : vk::feature_table()) if(f.id != keep) r.disable(f.id);
    return r;
}
#if defined(__linux__)
void set_sim(const char* api, const char* hide, const char* no_features, const char* no_commands = nullptr) {
    auto put = [](const char* name, const char* value) { if(value) setenv(name, value, 1); else unsetenv(name); };
    put("CAT_VKSIM_API", api); put("CAT_VKSIM_HIDE_EXT", hide); put("CAT_VKSIM_NO_FEATURES", no_features); put("CAT_VKSIM_HIDE_COMMANDS", no_commands);
}
#endif
}  // namespace

int main(int argc, char** argv) {
    if(argc != 3 && !(argc == 5 && std::string(argv[3]) == "--sim")) {
        std::cerr << "usage: cat_arena_path_tests <shader-dir> <assets/n1> [--sim <layer-manifest-dir>]\n";
        return 2;
    }
    const bool sim = argc == 5;
#if defined(__linux__)
    if(sim) {   // make the test layer discoverable without hiding layers the user already configured
        const std::string dir = argv[4];
        if(const char* old = std::getenv("VK_LAYER_PATH")) setenv("VK_LAYER_PATH", (dir + ":" + old).c_str(), 1);
        else setenv("VK_ADD_LAYER_PATH", dir.c_str(), 1);
        setenv("VK_INSTANCE_LAYERS", "VK_LAYER_CAT_device_sim", 1);
        set_sim(nullptr, nullptr, nullptr);
    }
#else
    if(sim) { std::cout << "SKIP device simulation layer is only built on Linux\n"; return 77; }
#endif
    Context ctx;
    ctx.shaders = argv[1];
    scene::MapScene map;
    try {
        scene::SceneOptions options;
        options.runtime_world_atlas = scene::default_runtime_world_atlas(argv[2]);
        options.sky_box_dir = scene::default_sky_box_dir(argv[2]);
        map = scene::load_map_scene(argv[2], "helios-crucible", options);
        ctx.scene = &map;
        ctx.reference = render(ctx, {});
    } catch(const std::exception& e) {
        const std::string what = e.what();
        if(what.find("No Vulkan") != std::string::npos || what.find("VkResult=-9") != std::string::npos || what.find("loader required") != std::string::npos) {
            std::cout << "SKIP no usable Vulkan device: " << what << '\n';
            return 77;
        }
        std::cerr << "FAIL reference render: " << what << '\n';
        return 1;
    }
    try {
        const auto& caps = ctx.reference.report.caps;
        std::cout << "driver: " << ctx.reference.report.device << " (" << ctx.reference.report.driver_name << ", " << ctx.reference.report.driver_info << "), device API "
                  << ctx.reference.report.api << ", loader " << ctx.reference.report.loader_api << ", " << caps.extensions.size() << " device extensions\n";
        verify(ctx, "auto (reference)", {}, ctx.reference);
        ctx.have_reference = true;
        if(!sim) {
            // ---- every path selectable on the installed driver ---------------------------------------
            struct Case { std::string name; vk::Request request; std::function<void(const Run&)> expect; };
            std::vector<Case> cases;
            cases.push_back({"baseline (Vulkan 1.1, no optional feature)", vk::Request::baseline(), [](const Run& r) {
                check(r.report.path.is_baseline() && r.report.negotiated_api == "1.1" && r.report.instance_api == "1.1" && r.report.enabled_extensions.empty(),
                      "Request::baseline() must run the pure 1.1 path");
            }});
            vk::Request r12; r12.max_api = vk::kApi12;
            cases.push_back({"Vulkan 1.2 + extensions", r12, [](const Run& r) {
                for(auto s : r.report.path.source) check(s != vk::Source::core, "no core 1.3/1.4 feature may be used under a 1.2 cap");
            }});
            vk::Request r13; r13.max_api = vk::kApi13;
            cases.push_back({"Vulkan 1.3 + extensions", r13, [](const Run& r) {
                check(r.report.path[F::push_descriptor] != vk::Source::core && r.report.path[F::host_image_copy] != vk::Source::core, "1.4 core features under a 1.3 cap");
            }});
            r13.allow_extensions = false;
            cases.push_back({"Vulkan 1.3 core only", r13, [](const Run& r) {
                check(!r.report.path.uses(F::push_descriptor) && !r.report.path.uses(F::host_image_copy), "1.4 features under a 1.3 core-only cap");
            }});
            vk::Request r14; r14.allow_extensions = false;
            cases.push_back({"Vulkan 1.4 core only", r14, [](const Run&) {}});
            for(const auto& f : vk::feature_table()) {
                cases.push_back({std::string("without ") + f.name, vk::Request{}.disable(f.id), [](const Run&) {}});
                cases.push_back({std::string("only ") + f.name, only(f.id), [id = f.id](const Run& r) {
                    for(const auto& g : vk::feature_table()) if(g.id != id) check(!r.report.path.uses(g.id), "a disabled feature is in use");
                }});
            }
            vk::Request none;
            for(const auto& f : vk::feature_table()) none.disable(f.id);
            cases.push_back({"all optional features disabled at the driver's API", none, [](const Run& r) { check(r.report.path.is_baseline(), "expected fallbacks only"); }});
            std::uint32_t baseline_pipelines = 0;
            for(const auto& c : cases) {
                const auto run = render(ctx, c.request);
                verify(ctx, c.name, c.request, run);
                c.expect(run);
                if(run.report.path.is_baseline() && !baseline_pipelines) baseline_pipelines = run.report.material_pipelines;
                if(run.report.path.uses(F::extended_dynamic_state) && baseline_pipelines)
                    check(run.report.material_pipelines <= baseline_pipelines, c.name + ": extended dynamic state must not need more pipelines than the baseline");
            }
        }
#if defined(__linux__)
        else {
            // ---- simulated devices (test layer) ------------------------------------------------------
            const auto real_minor = vk::api_minor(caps.device_api);
            set_sim("1.2", "*", nullptr);
            {
                const auto probe = render(ctx, {});
                if(probe.report.api.rfind("1.2.", 0) != 0) {
                    std::cerr << "FAIL VK_LAYER_CAT_device_sim is not active (device still reports API " << probe.report.api << ")\n";
                    return 1;
                }
                verify(ctx, "SIM bare Vulkan 1.2 device (no optional extension)", {}, probe);
                check(probe.report.path.is_baseline() && probe.report.negotiated_api == "1.2" && probe.report.enabled_extensions.empty() &&
                      probe.report.caps.extensions.size() <= 1, "a bare 1.2 device must run the baseline path");
            }
            {   // explicit caps on the bare device change nothing
                const auto run = render(ctx, vk::Request::baseline());
                verify(ctx, "SIM bare Vulkan 1.2 device, Request::baseline()", vk::Request::baseline(), run);
            }
            set_sim("1.2", nullptr, nullptr);
            {
                const auto run = render(ctx, {});
                verify(ctx, "SIM Vulkan 1.2 device with the driver's extensions", {}, run);
                for(const auto& f : vk::feature_table()) {
                    check(run.report.path[f.id] != vk::Source::core, "core 1.3/1.4 feature used on a 1.2 device");
                    if(ctx.reference.report.caps.has_extension(f.extension) && f.id != F::host_image_copy)
                        check(run.report.path[f.id] == vk::Source::extension, std::string(f.name) + " should come from its extension on a 1.2 device");
                }
            }
            set_sim("1.2", nullptr, "dynamicRendering");
            {
                const auto run = render(ctx, {});
                verify(ctx, "SIM Vulkan 1.2 device, VK_KHR_dynamic_rendering advertised but feature bit off", {}, run);
                check(!run.report.path.uses(F::dynamic_rendering) && run.report.render_passes == 1, "an extension without its feature bit must not be used");
            }
            if(real_minor >= 3) {
                set_sim("1.3", "*", nullptr);
                {
                    const auto run = render(ctx, {});
                    verify(ctx, "SIM bare Vulkan 1.3 device", {}, run);
                    check(run.report.path[F::dynamic_rendering] == vk::Source::core && run.report.path[F::synchronization2] == vk::Source::core &&
                          run.report.path[F::extended_dynamic_state] == vk::Source::core && !run.report.path.uses(F::push_descriptor) &&
                          !run.report.path.uses(F::host_image_copy), "bare 1.3 device: exactly the three 1.3 core features");
                }
                set_sim("1.3", "*", "dynamicRendering,synchronization2");
                {
                    const auto run = render(ctx, {});
                    verify(ctx, "SIM Vulkan 1.3 device reporting no dynamicRendering/synchronization2 bits", {}, run);
                    check(!run.report.path.uses(F::dynamic_rendering) && !run.report.path.uses(F::synchronization2) && run.report.render_passes == 1,
                          "API 1.3 alone must not enable features whose bits are not reported");
                }
            } else std::cout << "NOTE driver is below 1.3: simulated 1.3 devices not run\n";
            if(real_minor >= 4) {
                set_sim("1.4", "*", "hostImageCopy");
                {
                    const auto run = render(ctx, {});
                    verify(ctx, "SIM Vulkan 1.4 device without hostImageCopy", {}, run);
                    check(!run.report.path.uses(F::host_image_copy) && run.report.staging_texture_bytes > 0, "hostImageCopy is optional in 1.4: staging fallback expected");
                }
                set_sim("1.4", "*", "pushDescriptor,hostImageCopy,dynamicRendering,synchronization2");
                {
                    const auto run = render(ctx, {});
                    verify(ctx, "SIM Vulkan 1.4 device reporting no optional feature bits", {}, run);
                    check(!run.report.path.uses(F::push_descriptor) && !run.report.path.uses(F::dynamic_rendering), "feature bits decide, not the version");
                }
                // The device reports the features, but some entry points do not resolve: each affected
                // feature must be dropped at run time (before any resource exists) and its fallback used.
                set_sim("1.4", "*", nullptr, "vkQueueSubmit2,vkCmdEndRendering,vkCmdSetDepthCompareOp,vkCmdPushDescriptorSet,vkCopyMemoryToImage");
                {
                    const auto run = render(ctx, {});
                    verify(ctx, "SIM Vulkan 1.4 device with unresolvable entry points", {}, run);
                    check(run.report.path.is_baseline() && run.report.render_passes == 1, "features with missing entry points must fall back");
                    std::size_t dropped = 0;
                    for(const auto& note : run.report.path.notes) if(note.find("dropped at run time") != std::string::npos) ++dropped;
                    check(dropped == vk::kFeatureCount, "every dropped feature must be recorded, got " + std::to_string(dropped));
                    for(const auto& f : vk::feature_table())
                        check(run.report.caps.core_bit[static_cast<std::size_t>(f.id)], std::string(f.name) + " was reported by the device in this scenario");
                }
            } else std::cout << "NOTE driver is below 1.4: simulated 1.4 devices not run\n";
            set_sim("1.1", "*", nullptr);
            {
                const auto run = render(ctx, {});
                verify(ctx, "SIM bare Vulkan 1.1 device", {}, run);
                check(run.report.path.is_baseline() && run.report.negotiated_api == "1.1" &&
                      run.report.enabled_extensions.empty(), "a bare Vulkan 1.1 device must render with baseline features");
            }
            set_sim("1.0", "*", nullptr);
            {
                bool rejected = false;
                std::string what;
                try { (void)render(ctx, {}); } catch(const std::exception& e) { what = e.what(); rejected = what.find("No Vulkan 1.1") != std::string::npos; }
                check(rejected, "a Vulkan 1.0 device must be rejected with a clear message: " + what);
                ++ctx.passed;
                std::cout << "PASS SIM Vulkan 1.0 device is rejected: " << what << '\n';
            }
            set_sim(nullptr, nullptr, nullptr);
        }
#endif
        std::cout << "PASS " << ctx.passed << " path configurations\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
