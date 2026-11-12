// N2 Vulkan renderer: N0 instance/device/swapchain foundation + Helios map rendering.
// Frame order reproduces legacy render() (legacy/index.html 13572-13662):
//   clear(theme sky) -> sky geometry (== baked sky box) -> world -> portal + opaque material
//   clusters -> sky cloud layers at the far plane -> translucent clusters back to front.
//
// N2.1 compatibility model (docs/N2_1_VULKAN_COMPATIBILITY.md):
//   * Mandatory baseline: a Vulkan 1.2 device. The baseline path uses VkRenderPass + framebuffers,
//     vkCmdPipelineBarrier / vkQueueSubmit, static pipeline state, a dynamic-offset uniform buffer
//     and staging-buffer texture uploads. It calls Vulkan 1.0 device commands only.
//   * Optional paths, each selected independently at run time by cat::vk::select_path() from the
//     detected versions, extensions and feature bits: dynamic rendering, synchronization2, extended
//     dynamic state (1.3 core or KHR/EXT extension), push descriptors and host image copy (1.4 core
//     or KHR/EXT extension). Every one of them has the baseline code as its fallback.
//   * Only Vulkan 1.0 + WSI commands are linked. Everything newer is resolved with
//     vkGetInstanceProcAddr / vkGetDeviceProcAddr and a missing entry point drops that feature.
//   * All paths issue the same draws with the same state, so the image does not depend on the path.
#include "cat/renderer.hpp"
#include "cat/utf8_path.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace cat {
namespace {
void check(VkResult result, const char* operation) {
    if(result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + " VkResult=" + std::to_string(result));
}
struct Counters { std::atomic<std::uint32_t> warnings{0}, errors{0}; };
VKAPI_ATTR VkBool32 VKAPI_CALL diagnostic(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                          const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    const bool error = severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    if(auto* counters = static_cast<Counters*>(user)) { if(error) ++counters->errors; else ++counters->warnings; }
    log(error ? "error" : "warning", "vulkan-validation", data->pMessage);
    return VK_FALSE;
}
std::vector<std::uint32_t> shader_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if(!file) throw std::runtime_error("Missing shader: " + path_utf8(path));
    auto size = file.tellg();
    if(size <= 0 || static_cast<std::uint64_t>(size) % 4 != 0) throw std::runtime_error("Invalid SPIR-V size: " + path_utf8(path));
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), size);
    if(!file || words[0] != 0x07230203) throw std::runtime_error("Invalid SPIR-V: " + path_utf8(path));
    return words;
}
VkBlendFactor blend_factor(int code) {   // legacy BLEND_TABLE order (3993)
    static constexpr std::array<VkBlendFactor, 11> table{
        VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
        VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA_SATURATE};
    if(code < 0 || code > 10) throw std::runtime_error("blend code out of range");
    return table[static_cast<std::size_t>(code)];
}
std::string version_string(std::uint32_t v) {
    return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) + "." + std::to_string(VK_API_VERSION_PATCH(v));
}
const char* format_name(VkFormat f) {
    switch(f) {
        case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
        case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case VK_FORMAT_B8G8R8A8_SRGB: return "B8G8R8A8_SRGB";
        case VK_FORMAT_R8G8B8A8_SRGB: return "R8G8B8A8_SRGB";
        case VK_FORMAT_D32_SFLOAT: return "D32_SFLOAT";
        case VK_FORMAT_D32_SFLOAT_S8_UINT: return "D32_SFLOAT_S8_UINT";
        case VK_FORMAT_D24_UNORM_S8_UINT: return "D24_UNORM_S8_UINT";
        case VK_FORMAT_X8_D24_UNORM_PACK32: return "X8_D24_UNORM";
        case VK_FORMAT_D16_UNORM: return "D16_UNORM";
        default: return "other";
    }
}
const char* present_name(VkPresentModeKHR m) {
    switch(m) {
        case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
        case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
        case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
        default: return "other";
    }
}
// The legacy draws with gl.frontFace(CW) (its positive-forward camera with a world-left `right`
// vector reverses conventional winding). Clip Y is negated here so the image has the same on-screen
// orientation as WebGL, and Vulkan's facing formula (spec: a = -1/2 * sum over framebuffer coords,
// Y down) makes "clockwise" mean clockwise as displayed - exactly GL's CW. An earlier revision used
// COUNTER_CLOCKWISE and culled every floor top; render_tests' normals check caught it.
constexpr VkFrontFace kLegacyFrontFace = VK_FRONT_FACE_CLOCKWISE;
constexpr float kLightScale = 1.9F * 1.0F * 1.32F;   // Q3MAT frame.lightScale (normal preset overbright)
constexpr float kMapOverbright = 1.32F, kLmIntensity = 1.0F, kFogStrength = 1.0F;
}  // namespace

namespace gpu {
// std140 mirrors of shaders/frame.glsl and shaders/material_data.glsl
struct FrameData {
    float cam_pos[4], cam_forward[4], cam_right[4], cam_up[4], fog_color[4], params[4], sky_eye[4];
    std::int32_t flags[4];
    float sky_box[4];   // N4: x rotation (rad), y horizon fog, z 1 when an image sky box is present
    std::int32_t dl[4];                   // N5.1: x light count
    float dl_pos[16][4], dl_col[16][4];   // xyz + radius, rgb
};
static_assert(sizeof(FrameData) == 144 + 16 + 2 * 16 * 16);
struct StageData {
    std::int32_t a[4], b[4];
    float cst[4], pre_s[4], pre_t[4], post_s[4], post_t[4], turb[4], tc_s[4], tc_t[4], misc[4];
};
static_assert(sizeof(StageData) == 176);
struct DrawData {
    std::int32_t h0[4], h1[4], d_type[4];
    float d_a[3][4], d_b[3][4], skyp[4];
    StageData st[8];
};
static_assert(sizeof(DrawData) == 160 + 8 * 176);
}  // namespace gpu

namespace {
struct Buffer { VkBuffer buffer{}; VkDeviceMemory memory{}; VkDeviceSize size{}; void* mapped{}; };
struct Texture { VkImage image{}; VkDeviceMemory memory{}; VkImageView view{}; };
struct PipelineKey {
    int kind{};   // 0 material/cloud
    int cull{}, src{1}, dst{0};
    bool blend{}, depth_write{true}, depth_equal{}, polygon_offset{};
    auto operator<=>(const PipelineKey&) const = default;
};
struct Pass {
    PipelineKey key;
    VkPipeline pipeline{};
    std::vector<std::uint32_t> stages;   // indices into Material::stages
    bool collapse{};
    int fog{};
    std::uint32_t slot{};                // draw-UBO slot, rewritten every frame
};
struct GpuMaterial { std::vector<Pass> passes; };
struct VertexLayout {
    std::uint32_t stride{}; std::vector<VkVertexInputAttributeDescription> attributes;
    std::uint32_t instance_stride{};   // N6: a second, per-instance binding (attributes with binding 1) when non-zero
};
struct PipelineState {
    VkCullModeFlags cull{VK_CULL_MODE_NONE};
    bool depth_test{true}, depth_write{true}, blend{}, polygon_offset{};
    VkCompareOp compare{VK_COMPARE_OP_LESS};
    VkBlendFactor src{VK_BLEND_FACTOR_ONE}, dst{VK_BLEND_FACTOR_ZERO};
    bool dynamic_raster{};   // extended dynamic state: cull mode, depth write and depth compare set per draw
    float offset_units{-2};  // polygonOffset units when polygon_offset is set
};
struct OverlayPush { float cam_pos[4], forward[4], right[4], up[4], extra[4]; };   // shaders/overlay.glsl
struct EnemyPush { float rig0[4], rig1[4], rig2[4], mode[4]; };                    // shaders/enemy.glsl
static_assert(sizeof(EnemyPush) == 64);
constexpr std::size_t kEnemyInstanceBytes = 112, kEnemyMaxInstances = 2048;
static_assert(sizeof(OverlayPush) == 80);
constexpr std::size_t kFxMaxQuads = 640 + 3600 + 48, kHudMaxVertices = 90000, kSolidMaxVertices = 16384, kAtlasMaxVertices = 4096;
VkCullModeFlags cull_flags(int legacy_cull) {   // Q3MAT cull: 0 back, 1 front, 2 none
    return legacy_cull == 0 ? VK_CULL_MODE_BACK_BIT : legacy_cull == 1 ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_NONE;
}
bool has_stencil(VkFormat f) { return f == VK_FORMAT_D32_SFLOAT_S8_UINT || f == VK_FORMAT_D24_UNORM_S8_UINT; }
// Entry points newer than Vulkan 1.0 are never linked: Android's libvulkan.so and older desktop
// loaders do not export them, and a 1.2 driver does not implement the 1.3/1.4 ones. They are
// resolved at run time and stay null when unavailable; a selected feature whose entry points are
// missing is dropped before any resource is created.
struct Dispatch {
    // Instance level, Vulkan 1.1 core (required: the capability queries need them).
    PFN_vkGetPhysicalDeviceFeatures2 features2{};
    PFN_vkGetPhysicalDeviceProperties2 properties2{};
    PFN_vkGetPhysicalDeviceFormatProperties2 format_properties2{};
    PFN_vkGetPhysicalDeviceImageFormatProperties2 image_format_properties2{};
    // Device level, optional. Core name when the feature is core at the negotiated API, else the KHR/EXT alias.
    PFN_vkCmdBeginRendering begin_rendering{};
    PFN_vkCmdEndRendering end_rendering{};
    PFN_vkCmdPipelineBarrier2 pipeline_barrier2{};
    PFN_vkQueueSubmit2 queue_submit2{};
    PFN_vkCmdSetCullMode set_cull_mode{};
    PFN_vkCmdSetDepthWriteEnable set_depth_write_enable{};
    PFN_vkCmdSetDepthCompareOp set_depth_compare_op{};
    PFN_vkCmdPushDescriptorSet push_descriptor_set{};
    PFN_vkTransitionImageLayout transition_image_layout{};
    PFN_vkCopyMemoryToImage copy_memory_to_image{};
};
}  // namespace

struct Renderer::Impl {
    SDL_Window* window{};
    Settings settings;
    std::filesystem::path shaders;
    bool offscreen{};
    Counters counters;
    VkInstance instance{}; VkDebugUtilsMessengerEXT messenger{}; VkSurfaceKHR surface{};
    VkPhysicalDevice physical{}; VkDevice device{}; std::uint32_t family{}; VkQueue queue{};
    VkPhysicalDeviceProperties properties{};
    std::uint32_t loader_api{};
    bool validation{}, anisotropy{};
    float max_anisotropy{1};
    VkFormat format{}, depth_format{};
    VkExtent2D extent{};
    VkPresentModeKHR present_mode{VK_PRESENT_MODE_FIFO_KHR};
    VkSwapchainKHR swapchain{};
    std::vector<VkImageView> views; std::vector<VkFramebuffer> framebuffers;
    std::vector<VkSemaphore> presented;   // indexed by acquired image, not CPU frame
    Texture color_target, depth_target;
    Buffer readback;
    bool have_capture{};
    VkRenderPass pass{};
    VkFormat pass_format{}, pass_depth{};
    VkPipelineLayout triangle_layout{}; VkPipeline triangle_pipeline{};
    VkCommandPool pool{}; VkCommandBuffer command{};
    VkSemaphore acquired{}; VkFence fence{}; bool dirty{true};
    // scene
    bool scene_loaded{};
    VkDescriptorSetLayout set0_layout{}, set1_layout{};
    VkPipelineLayout scene_layout{};
    VkDescriptorPool descriptor_pool{};
    VkDescriptorSet set0{}, set1{};
    VkSampler sampler_repeat{}, sampler_clamp{};
    Buffer frame_ubo, draw_ubo;
    VkDeviceSize draw_stride{};
    Buffer world_vb, world_ib, sky_vb, sky_ib, mat_vb, mat_ib, cloud_vb, cloud_ib;
    std::uint32_t world_indices{}, sky_indices{}, cloud_indices{};
    Texture world_tex, lightmap_tex, mat_tex, kit_tex, portal_tex, box_tex;
    VkPipeline skybox_pipeline{};
    bool has_sky_box{};
    double sky_fog{}, sky_rotate_deg{};
    VkPipeline sky_pipeline{}, world_pipeline{}, dynamic_pipeline{};   // dynamic: N5.1 atlas billboards (needs the scene layout)
    std::map<PipelineKey, VkPipeline> material_pipelines;
    std::vector<scene::Material> materials;
    std::vector<scene::MaterialBinding> bindings;
    std::vector<scene::Cluster> clusters;
    std::vector<GpuMaterial> gpu_materials;
    std::size_t sky_material{};
    scene::Theme theme{};
    std::array<double, 2> sky_center{};
    double sky_eye_z{2};
    std::uint32_t pass_slots{};
    RenderStats stats;
    // N2.1: capability detection and the selected path
    vk::Request request;
    vk::DeviceCaps caps;
    vk::Path path;
    Dispatch fn;
    std::uint32_t instance_api{};
    std::vector<std::string> enabled_extensions;
    std::string driver_name, driver_info;
    bool use_dynamic_rendering{}, use_sync2{}, use_dynamic_state{}, use_push_descriptor{}, use_host_copy{};
    std::vector<VkImage> swap_images;   // dynamic rendering transitions the acquired image itself
    bool pipelines_built{};
    std::uint64_t staging_texture_bytes{}, host_copy_texture_bytes{};
    // N5 overlay (effects, weapon model, HUD): map independent, survives load()
    bool overlay_ready{};
    VkDescriptorSetLayout overlay_layout1{}, overlay_layout3{};
    VkPipelineLayout fx_layout{}, gun_layout{}, hud_layout{};
    VkDescriptorPool overlay_pool{};
    VkDescriptorSet fx_set{};
    std::vector<VkDescriptorSet> gun_sets;
    VkSampler sampler_fx{}, sampler_gun{};
    Texture fx_tex;
    std::vector<Texture> gun_tex;        // base, metal/rough, normal per weapon
    std::vector<Buffer> gun_vb;
    std::vector<std::uint32_t> gun_vertex_count;
    Buffer fx_vb, fx_ib, hud_vb, solid_vb, atlas_vb;   // host visible, rewritten every frame
    VkPipelineLayout solid_layout{};
    VkPipeline fx_pipeline{}, fx_mark_pipeline{}, fx_gun_pipeline{}, gun_pipeline{}, hud_pipeline{}, solid_pipeline{};
    float last_fog[4]{};
    // N6 creatures: meshes and textures are map independent; the pipeline layout uses set 0 of the scene
    bool enemies_ready{};
    VkDescriptorSetLayout enemy_set_layout{};
    VkPipelineLayout enemy_layout{};
    VkDescriptorPool enemy_pool{};
    std::vector<VkDescriptorSet> enemy_sets;      // 20 creature types + Marine Cat
    std::vector<Texture> enemy_tex;
    Texture volume_tex{};
    VkSampler sampler_enemy{}, sampler_volume{};
    std::vector<Buffer> enemy_vb;
    std::vector<std::uint32_t> enemy_vertex_count;
    std::vector<std::array<float, 12>> enemy_rig;
    Buffer enemy_instances{};
    VkPipeline enemy_pipeline{}, enemy_outline_pipeline{}, enemy_shadow_pipeline{};

    Impl(SDL_Window* w, const Settings& s, std::filesystem::path shader_path, bool off, const vk::Request& r)
        : window(w), settings(s), shaders(std::move(shader_path)), offscreen(off), request(r) {}
    ~Impl() { cleanup(); }

    // ------------------------------------------------------------ synchronization (sync2 or 1.0 fallback)
    // Stage and access masks are passed as the Vulkan 1.0 flag types; every bit used here has the
    // same value in VkPipelineStageFlags2 / VkAccessFlags2, so both paths describe the same dependency.
    void image_barrier(VkCommandBuffer cb, VkImage image, VkImageAspectFlags aspect, std::uint32_t levels, std::uint32_t layers,
                       VkImageLayout from, VkImageLayout to, VkPipelineStageFlags src_stage, VkAccessFlags src_access,
                       VkPipelineStageFlags dst_stage, VkAccessFlags dst_access) {
        if(use_sync2) {
            VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            b.srcStageMask = src_stage; b.srcAccessMask = src_access; b.dstStageMask = dst_stage; b.dstAccessMask = dst_access;
            b.oldLayout = from; b.newLayout = to; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = image; b.subresourceRange = {aspect, 0, levels, 0, layers};
            VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            d.imageMemoryBarrierCount = 1; d.pImageMemoryBarriers = &b;
            fn.pipeline_barrier2(cb, &d);
            return;
        }
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = src_access; b.dstAccessMask = dst_access; b.oldLayout = from; b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image; b.subresourceRange = {aspect, 0, levels, 0, layers};
        vkCmdPipelineBarrier(cb, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    void buffer_barrier(VkCommandBuffer cb, VkBuffer buffer, VkPipelineStageFlags src_stage, VkAccessFlags src_access,
                        VkPipelineStageFlags dst_stage, VkAccessFlags dst_access) {
        if(use_sync2) {
            VkBufferMemoryBarrier2 b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            b.srcStageMask = src_stage; b.srcAccessMask = src_access; b.dstStageMask = dst_stage; b.dstAccessMask = dst_access;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.buffer = buffer; b.size = VK_WHOLE_SIZE;
            VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            d.bufferMemoryBarrierCount = 1; d.pBufferMemoryBarriers = &b;
            fn.pipeline_barrier2(cb, &d);
            return;
        }
        VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        b.srcAccessMask = src_access; b.dstAccessMask = dst_access;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.buffer = buffer; b.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cb, src_stage, dst_stage, 0, 0, nullptr, 1, &b, 0, nullptr);
    }
    void submit(VkCommandBuffer cb, VkSemaphore wait, VkPipelineStageFlags wait_stage, VkSemaphore signal, VkFence signal_fence, const char* what) {
        if(use_sync2) {
            VkSemaphoreSubmitInfo w{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO}, s{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
            w.semaphore = wait; w.stageMask = wait_stage;
            s.semaphore = signal; s.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            VkCommandBufferSubmitInfo c{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
            c.commandBuffer = cb;
            VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
            si.waitSemaphoreInfoCount = wait ? 1U : 0U; si.pWaitSemaphoreInfos = wait ? &w : nullptr;
            si.signalSemaphoreInfoCount = signal ? 1U : 0U; si.pSignalSemaphoreInfos = signal ? &s : nullptr;
            si.commandBufferInfoCount = 1; si.pCommandBufferInfos = &c;
            check(fn.queue_submit2(queue, 1, &si, signal_fence), what);
            return;
        }
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount = wait ? 1U : 0U; si.pWaitSemaphores = wait ? &wait : nullptr; si.pWaitDstStageMask = wait ? &wait_stage : nullptr;
        si.signalSemaphoreCount = signal ? 1U : 0U; si.pSignalSemaphores = signal ? &signal : nullptr;
        si.commandBufferCount = 1; si.pCommandBuffers = &cb;
        check(vkQueueSubmit(queue, 1, &si, signal_fence), what);
    }

    // ------------------------------------------------------------ resources
    std::uint32_t memory_type(std::uint32_t bits, VkMemoryPropertyFlags flags) const {
        VkPhysicalDeviceMemoryProperties mp{};
        vkGetPhysicalDeviceMemoryProperties(physical, &mp);
        for(std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if((bits & (1U << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags) return i;
        throw std::runtime_error("No suitable Vulkan memory type");
    }
    Buffer make_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags) {
        Buffer b; b.size = size;
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device, &bi, nullptr, &b.buffer), "create buffer");
        VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(device, b.buffer, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size; ai.memoryTypeIndex = memory_type(req.memoryTypeBits, flags);
        check(vkAllocateMemory(device, &ai, nullptr, &b.memory), "allocate buffer memory");
        check(vkBindBufferMemory(device, b.buffer, b.memory, 0), "bind buffer memory");
        if(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) check(vkMapMemory(device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped), "map buffer");
        return b;
    }
    void destroy(Buffer& b) noexcept {
        if(b.mapped) vkUnmapMemory(device, b.memory);
        if(b.buffer) vkDestroyBuffer(device, b.buffer, nullptr);
        if(b.memory) vkFreeMemory(device, b.memory, nullptr);
        b = {};
    }
    void destroy(Texture& t) noexcept {
        if(t.view) vkDestroyImageView(device, t.view, nullptr);
        if(t.image) vkDestroyImage(device, t.image, nullptr);
        if(t.memory) vkFreeMemory(device, t.memory, nullptr);
        t = {};
    }
    Texture make_image(VkExtent2D size, VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                       std::uint32_t levels = 1, std::uint32_t layers = 1, VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D) {
        Texture t;
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = fmt; ii.extent = {size.width, size.height, 1};
        ii.mipLevels = levels; ii.arrayLayers = layers; ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = usage; ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(device, &ii, nullptr, &t.image), "create image");
        VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device, t.image, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size; ai.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device, &ai, nullptr, &t.memory), "allocate image memory");
        check(vkBindImageMemory(device, t.image, t.memory, 0), "bind image memory");
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = t.image; vi.viewType = view_type; vi.format = fmt; vi.subresourceRange = {aspect, 0, levels, 0, layers};
        check(vkCreateImageView(device, &vi, nullptr, &t.view), "create image view");
        return t;
    }
    template<class F> void one_time(F record) {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        VkCommandBuffer cb{};
        check(vkAllocateCommandBuffers(device, &ai, &cb), "upload command buffer");
        ScopeExit free_cb([&] { vkFreeCommandBuffers(device, pool, 1, &cb); });
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(cb, &bi), "begin upload");
        record(cb);
        check(vkEndCommandBuffer(cb), "end upload");
        submit(cb, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, VK_NULL_HANDLE, "submit upload");
        check(vkQueueWaitIdle(queue), "wait upload");
    }
    Buffer device_buffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
        if(size == 0) size = 4;   // empty streams (e.g. a map without material surfaces) stay valid handles
        Buffer staging = make_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        ScopeExit free_staging([&] { destroy(staging); });
        if(data) std::memcpy(staging.mapped, data, static_cast<std::size_t>(size)); else std::memset(staging.mapped, 0, static_cast<std::size_t>(size));
        Buffer b = make_buffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        one_time([&](VkCommandBuffer cb) { VkBufferCopy c{0, 0, size}; vkCmdCopyBuffer(cb, staging.buffer, b.buffer, 1, &c); });
        return b;
    }
    // Uploads same-sized RGBA8 layers; mips are box-filtered on the CPU (deterministic across drivers).
    Texture texture(const std::vector<scene::Image>& layers, bool mips, bool array) {
        if(layers.empty()) throw std::runtime_error("texture without layers");
        const auto w = layers[0].width, h = layers[0].height;
        for(const auto& l : layers)
            if(l.width != w || l.height != h || l.rgba.size() != std::size_t(w) * h * 4U) throw std::runtime_error("texture layer size mismatch");
        std::vector<std::vector<scene::Image>> chains;
        VkDeviceSize total = 0;
        for(const auto& l : layers) {
            chains.push_back(mips ? scene::mip_chain(l) : std::vector<scene::Image>{l});
            for(const auto& m : chains.back()) total += m.rgba.size();
        }
        const auto levels = static_cast<std::uint32_t>(chains[0].size());
        const auto count = static_cast<std::uint32_t>(layers.size());
        const auto view_type = array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        if(use_host_copy) {
            // Optional path (1.4 hostImageCopy / VK_EXT_host_image_copy): copy from CPU memory straight
            // into the image. No staging buffer, no command buffer, no queue submission. The pixel
            // data and the final layout are identical to the staging path below.
            Texture t = make_image({w, h}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_HOST_TRANSFER_BIT,
                                   VK_IMAGE_ASPECT_COLOR_BIT, levels, count, view_type);
            VkHostImageLayoutTransitionInfo tr{VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO};
            tr.image = t.image; tr.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; tr.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            tr.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, count};
            check(fn.transition_image_layout(device, 1, &tr), "host image layout transition");
            std::vector<VkMemoryToImageCopy> host_regions;
            for(std::uint32_t layer = 0; layer < count; ++layer)
                for(std::uint32_t level = 0; level < levels; ++level) {
                    const auto& m = chains[layer][level];
                    VkMemoryToImageCopy r{VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY};
                    r.pHostPointer = m.rgba.data(); r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1}; r.imageExtent = {m.width, m.height, 1};
                    host_regions.push_back(r);
                }
            VkCopyMemoryToImageInfo ci{VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO};
            ci.dstImage = t.image; ci.dstImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            ci.regionCount = static_cast<std::uint32_t>(host_regions.size()); ci.pRegions = host_regions.data();
            check(fn.copy_memory_to_image(device, &ci), "host image copy");
            host_copy_texture_bytes += total;
            return t;
        }
        staging_texture_bytes += total;
        Buffer staging = make_buffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        ScopeExit free_staging([&] { destroy(staging); });
        std::vector<VkBufferImageCopy> regions;
        VkDeviceSize offset = 0;
        for(std::uint32_t layer = 0; layer < count; ++layer)
            for(std::uint32_t level = 0; level < levels; ++level) {
                const auto& m = chains[layer][level];
                std::memcpy(static_cast<std::uint8_t*>(staging.mapped) + offset, m.rgba.data(), m.rgba.size());
                VkBufferImageCopy r{};
                r.bufferOffset = offset; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1}; r.imageExtent = {m.width, m.height, 1};
                regions.push_back(r);
                offset += m.rgba.size();
            }
        Texture t = make_image({w, h}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                               VK_IMAGE_ASPECT_COLOR_BIT, levels, count, view_type);
        one_time([&](VkCommandBuffer cb) {
            image_barrier(cb, t.image, VK_IMAGE_ASPECT_COLOR_BIT, levels, count, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            vkCmdCopyBufferToImage(cb, staging.buffer, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<std::uint32_t>(regions.size()), regions.data());
            image_barrier(cb, t.image, VK_IMAGE_ASPECT_COLOR_BIT, levels, count, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                          VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        });
        return t;
    }

    // ------------------------------------------------------------ teardown
    void cleanup_targets() noexcept {
        for(auto f : framebuffers) vkDestroyFramebuffer(device, f, nullptr);
        framebuffers.clear();
        for(auto v : views) vkDestroyImageView(device, v, nullptr);
        views.clear();
        swap_images.clear();
        for(auto s : presented) vkDestroySemaphore(device, s, nullptr);
        presented.clear();
        if(swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = {};
        destroy(depth_target);
        destroy(color_target);
        destroy(readback);
        have_capture = false;
    }
    void cleanup_pipelines() noexcept {
        if(triangle_pipeline) vkDestroyPipeline(device, triangle_pipeline, nullptr);
        triangle_pipeline = {};
        if(sky_pipeline) vkDestroyPipeline(device, sky_pipeline, nullptr);
        if(skybox_pipeline) vkDestroyPipeline(device, skybox_pipeline, nullptr);
        skybox_pipeline = {};
        if(world_pipeline) vkDestroyPipeline(device, world_pipeline, nullptr);
        if(dynamic_pipeline) vkDestroyPipeline(device, dynamic_pipeline, nullptr);
        sky_pipeline = world_pipeline = dynamic_pipeline = {};
        destroy_enemy_pipelines();
        for(auto& [key, p] : material_pipelines) vkDestroyPipeline(device, p, nullptr);
        material_pipelines.clear();
        for(auto& m : gpu_materials) for(auto& p : m.passes) p.pipeline = {};
        for(VkPipeline* p : {&fx_pipeline, &fx_mark_pipeline, &fx_gun_pipeline, &gun_pipeline, &hud_pipeline, &solid_pipeline}) { if(*p) vkDestroyPipeline(device, *p, nullptr); *p = {}; }
        if(pass) vkDestroyRenderPass(device, pass, nullptr);
        pass = {};
        pipelines_built = false;
    }
    void destroy_enemy_pipelines() noexcept {
        for(VkPipeline* p : {&enemy_pipeline, &enemy_outline_pipeline, &enemy_shadow_pipeline}) { if(*p) vkDestroyPipeline(device, *p, nullptr); *p = {}; }
    }
    void cleanup_enemies() noexcept {
        destroy_enemy_pipelines();
        if(enemy_layout) vkDestroyPipelineLayout(device, enemy_layout, nullptr);
        enemy_layout = {};
        for(auto& b : enemy_vb) destroy(b);
        enemy_vb.clear(); enemy_vertex_count.clear(); enemy_rig.clear();
        destroy(enemy_instances);
        for(auto& t : enemy_tex) destroy(t);
        enemy_tex.clear();
        destroy(volume_tex);
        if(sampler_enemy) vkDestroySampler(device, sampler_enemy, nullptr);
        if(sampler_volume) vkDestroySampler(device, sampler_volume, nullptr);
        sampler_enemy = sampler_volume = {};
        if(enemy_pool) vkDestroyDescriptorPool(device, enemy_pool, nullptr);
        enemy_pool = {}; enemy_sets.clear();
        if(enemy_set_layout) vkDestroyDescriptorSetLayout(device, enemy_set_layout, nullptr);
        enemy_set_layout = {};
        enemies_ready = false;
    }
    void cleanup_scene() noexcept {
        destroy_enemy_pipelines();   // their layout uses set 0 of the scene
        if(enemy_layout) vkDestroyPipelineLayout(device, enemy_layout, nullptr);
        enemy_layout = {};
        for(Buffer* b : {&frame_ubo, &draw_ubo, &world_vb, &world_ib, &sky_vb, &sky_ib, &mat_vb, &mat_ib, &cloud_vb, &cloud_ib}) destroy(*b);
        for(Texture* t : {&world_tex, &lightmap_tex, &mat_tex, &kit_tex, &portal_tex, &box_tex}) destroy(*t);
        if(sampler_repeat) vkDestroySampler(device, sampler_repeat, nullptr);
        if(sampler_clamp) vkDestroySampler(device, sampler_clamp, nullptr);
        sampler_repeat = sampler_clamp = {};
        if(descriptor_pool) vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        descriptor_pool = {}; set0 = set1 = {};
        if(scene_layout) vkDestroyPipelineLayout(device, scene_layout, nullptr);
        scene_layout = {};
        if(set0_layout) vkDestroyDescriptorSetLayout(device, set0_layout, nullptr);
        if(set1_layout) vkDestroyDescriptorSetLayout(device, set1_layout, nullptr);
        set0_layout = set1_layout = {};
        scene_loaded = false;
    }
    void cleanup_overlay() noexcept {
        for(Buffer* b : {&fx_vb, &fx_ib, &hud_vb, &solid_vb, &atlas_vb}) destroy(*b);
        for(auto& b : gun_vb) destroy(b);
        gun_vb.clear(); gun_vertex_count.clear();
        destroy(fx_tex);
        for(auto& t : gun_tex) destroy(t);
        gun_tex.clear();
        if(sampler_fx) vkDestroySampler(device, sampler_fx, nullptr);
        if(sampler_gun) vkDestroySampler(device, sampler_gun, nullptr);
        sampler_fx = sampler_gun = {};
        if(overlay_pool) vkDestroyDescriptorPool(device, overlay_pool, nullptr);
        overlay_pool = {}; fx_set = {}; gun_sets.clear();
        for(VkPipelineLayout* l : {&fx_layout, &gun_layout, &hud_layout, &solid_layout}) { if(*l) vkDestroyPipelineLayout(device, *l, nullptr); *l = {}; }
        for(VkDescriptorSetLayout* l : {&overlay_layout1, &overlay_layout3}) { if(*l) vkDestroyDescriptorSetLayout(device, *l, nullptr); *l = {}; }
        overlay_ready = false;
    }
    void cleanup() noexcept {
        if(device) {
            (void)vkDeviceWaitIdle(device);
            cleanup_targets();
            cleanup_pipelines();
            if(triangle_layout) vkDestroyPipelineLayout(device, triangle_layout, nullptr);
            for(auto& [key, p] : material_pipelines) vkDestroyPipeline(device, p, nullptr);
            material_pipelines.clear();
            cleanup_scene();
            cleanup_overlay();
            cleanup_enemies();
            if(fence) vkDestroyFence(device, fence, nullptr);
            if(acquired) vkDestroySemaphore(device, acquired, nullptr);
            if(pool) vkDestroyCommandPool(device, pool, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if(surface) vkDestroySurfaceKHR(instance, surface, nullptr);
        if(messenger) {
            auto destroy_messenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if(destroy_messenger) destroy_messenger(instance, messenger, nullptr);
        }
        if(instance) vkDestroyInstance(instance, nullptr);
    }

    // ------------------------------------------------------------ N2.1 capability detection
    // Reads what the chosen physical device reports. Nothing is inferred from the version number:
    // core feature bits come from the aggregated VkPhysicalDeviceVulkan1xFeatures structs (queried
    // only when the negotiated API defines them), extension feature bits from the extension structs
    // (queried only when the extension is advertised), in two separate chains because the
    // specification forbids mixing an aggregated struct with the per-feature structs it replaces.
    void detect_capabilities() {
        using F = vk::Feature;
        auto idx = [](F f) { return static_cast<std::size_t>(f); };
        caps = {};
        caps.name = properties.deviceName; caps.loader_api = loader_api; caps.device_api = properties.apiVersion;
        std::uint32_t n = 0;
        check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &n, nullptr), "device extension count");
        std::vector<VkExtensionProperties> available(n);
        check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &n, available.data()), "device extensions");
        for(const auto& e : available) caps.extensions.emplace_back(e.extensionName);
        std::sort(caps.extensions.begin(), caps.extensions.end());
        const auto api = vk::negotiated_api(caps, request);
        if(api >= vk::kApi13) {
            VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            VkPhysicalDeviceVulkan14Features v14{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};
            if(api >= vk::kApi14) v13.pNext = &v14;
            VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &v13};
            fn.features2(physical, &f2);
            caps.core_bit[idx(F::dynamic_rendering)] = v13.dynamicRendering == VK_TRUE;
            caps.core_bit[idx(F::synchronization2)] = v13.synchronization2 == VK_TRUE;
            caps.core_bit[idx(F::extended_dynamic_state)] = true;   // mandatory part of 1.3 core; no feature bit exists
            if(api >= vk::kApi14) {
                caps.core_bit[idx(F::push_descriptor)] = v14.pushDescriptor == VK_TRUE;
                caps.core_bit[idx(F::host_image_copy)] = v14.hostImageCopy == VK_TRUE;   // optional even in 1.4
            }
        }
        VkPhysicalDeviceDynamicRenderingFeatures dr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
        VkPhysicalDeviceSynchronization2Features s2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
        VkPhysicalDeviceHostImageCopyFeatures hic{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES};
        void* chain = nullptr;
        auto link = [&](F f, auto& s) { if(caps.has_extension(vk::info(f).extension)) { s.pNext = chain; chain = &s; } };
        link(F::dynamic_rendering, dr); link(F::synchronization2, s2); link(F::extended_dynamic_state, eds); link(F::host_image_copy, hic);
        if(chain) {
            VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, chain};
            fn.features2(physical, &f2);
        }
        caps.extension_bit[idx(F::dynamic_rendering)] = dr.dynamicRendering == VK_TRUE;
        caps.extension_bit[idx(F::synchronization2)] = s2.synchronization2 == VK_TRUE;
        caps.extension_bit[idx(F::extended_dynamic_state)] = eds.extendedDynamicState == VK_TRUE;
        caps.extension_bit[idx(F::host_image_copy)] = hic.hostImageCopy == VK_TRUE;
        caps.extension_bit[idx(F::push_descriptor)] = caps.has_extension(vk::info(F::push_descriptor).extension);   // no feature struct
        // Host image copy is only selected when it is usable for the engine's textures and not slower to sample.
        if(caps.core_bit[idx(F::host_image_copy)] || caps.extension_bit[idx(F::host_image_copy)]) {
            if(api >= vk::kApi13 || caps.has_extension("VK_KHR_format_feature_flags2")) {
                VkFormatProperties3 fp3{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
                VkFormatProperties2 fp2{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, &fp3};
                fn.format_properties2(physical, VK_FORMAT_R8G8B8A8_UNORM, &fp2);
                caps.host_copy_format = (fp3.optimalTilingFeatures & VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT) != 0;
            }
            VkPhysicalDeviceHostImageCopyProperties hp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_PROPERTIES};
            VkPhysicalDeviceProperties2 hp2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &hp};
            fn.properties2(physical, &hp2);   // counts
            std::vector<VkImageLayout> src_layouts(hp.copySrcLayoutCount), dst_layouts(hp.copyDstLayoutCount);
            hp.pCopySrcLayouts = src_layouts.data(); hp.pCopyDstLayouts = dst_layouts.data();
            fn.properties2(physical, &hp2);   // layouts
            caps.host_copy_layout = std::find(dst_layouts.begin(), dst_layouts.end(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) != dst_layouts.end();
            VkPhysicalDeviceImageFormatInfo2 ii{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
            ii.format = VK_FORMAT_R8G8B8A8_UNORM; ii.type = VK_IMAGE_TYPE_2D; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
            ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_HOST_TRANSFER_BIT;
            VkHostImageCopyDevicePerformanceQuery perf{VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY};
            VkImageFormatProperties2 ip{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, &perf};
            caps.host_copy_optimal = fn.image_format_properties2(physical, &ii, &ip) == VK_SUCCESS && perf.optimalDeviceAccess == VK_TRUE;
        }
        // Driver identification is Vulkan 1.2 core (the device passed the 1.2 check above).
        VkPhysicalDeviceVulkan12Properties p12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
        VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &p12};
        fn.properties2(physical, &p2);
        driver_name = p12.driverName; driver_info = p12.driverInfo;
    }
    // Enables exactly what the selected path uses: the extensions it lists and the feature bits of
    // the features it selected. A feature is never enabled just because the version offers it.
    void create_device() {
        using F = vk::Feature;
        auto core = [&](F f) { return path[f] == vk::Source::core ? VK_TRUE : VK_FALSE; };
        auto ext = [&](F f) { return path[f] == vk::Source::extension; };
        float priority = 1;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex = family; qi.queueCount = 1; qi.pQueuePriorities = &priority;
        std::vector<const char*> names;
        if(!offscreen) names.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        for(const auto& e : path.extensions) names.push_back(e.c_str());
        enabled_extensions.assign(names.begin(), names.end());
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.features.samplerAnisotropy = anisotropy ? VK_TRUE : VK_FALSE;
        VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceVulkan14Features v14{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};
        VkPhysicalDeviceDynamicRenderingFeatures dr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
        VkPhysicalDeviceSynchronization2Features s2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
        VkPhysicalDeviceHostImageCopyFeatures hic{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES};
        void** tail = &features.pNext;
        auto link = [&](auto& s) { *tail = &s; tail = &s.pNext; };
        if(path.api >= vk::kApi13) { v13.dynamicRendering = core(F::dynamic_rendering); v13.synchronization2 = core(F::synchronization2); link(v13); }
        if(path.api >= vk::kApi14) { v14.pushDescriptor = core(F::push_descriptor); v14.hostImageCopy = core(F::host_image_copy); link(v14); }
        // Extension-sourced features only exist below their promotion version, so these never
        // coexist with the aggregated struct that contains the same bit.
        if(ext(F::dynamic_rendering)) { dr.dynamicRendering = VK_TRUE; link(dr); }
        if(ext(F::synchronization2)) { s2.synchronization2 = VK_TRUE; link(s2); }
        if(ext(F::extended_dynamic_state)) { eds.extendedDynamicState = VK_TRUE; link(eds); }
        if(ext(F::host_image_copy)) { hic.hostImageCopy = VK_TRUE; link(hic); }
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        di.pNext = &features; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
        di.enabledExtensionCount = static_cast<std::uint32_t>(names.size()); di.ppEnabledExtensionNames = names.empty() ? nullptr : names.data();
        check(vkCreateDevice(physical, &di, nullptr, &device), "create device");
    }
    // Resolves the entry points of the selected features: the core name when the feature is core at
    // the negotiated API, the KHR/EXT alias when it comes from an extension. A feature with a
    // missing entry point is dropped (its fallback is used) before anything depends on it.
    void resolve_device_functions() {
        using F = vk::Feature;
        std::string missing;
        auto load = [&](F f, const char* core_name, const char* alias) -> PFN_vkVoidFunction {
            if(!path.uses(f)) return nullptr;
            const char* name = path[f] == vk::Source::core ? core_name : alias;
            auto p = vkGetDeviceProcAddr(device, name);
            if(!p) missing = name;
            return p;
        };
        auto settle = [&](F f) {
            const bool ok = missing.empty();
            if(!ok) {
                log("warning", "vulkan", std::string(vk::info(f).name) + " selected but entry point " + missing + " is not provided; using the fallback");
                path.drop(f, "entry point " + missing + " is not provided by the device");
            }
            missing.clear();
            return ok;
        };
        fn.begin_rendering = reinterpret_cast<PFN_vkCmdBeginRendering>(load(F::dynamic_rendering, "vkCmdBeginRendering", "vkCmdBeginRenderingKHR"));
        fn.end_rendering = reinterpret_cast<PFN_vkCmdEndRendering>(load(F::dynamic_rendering, "vkCmdEndRendering", "vkCmdEndRenderingKHR"));
        if(!settle(F::dynamic_rendering)) fn.begin_rendering = nullptr, fn.end_rendering = nullptr;
        fn.pipeline_barrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2>(load(F::synchronization2, "vkCmdPipelineBarrier2", "vkCmdPipelineBarrier2KHR"));
        fn.queue_submit2 = reinterpret_cast<PFN_vkQueueSubmit2>(load(F::synchronization2, "vkQueueSubmit2", "vkQueueSubmit2KHR"));
        if(!settle(F::synchronization2)) fn.pipeline_barrier2 = nullptr, fn.queue_submit2 = nullptr;
        fn.set_cull_mode = reinterpret_cast<PFN_vkCmdSetCullMode>(load(F::extended_dynamic_state, "vkCmdSetCullMode", "vkCmdSetCullModeEXT"));
        fn.set_depth_write_enable = reinterpret_cast<PFN_vkCmdSetDepthWriteEnable>(load(F::extended_dynamic_state, "vkCmdSetDepthWriteEnable", "vkCmdSetDepthWriteEnableEXT"));
        fn.set_depth_compare_op = reinterpret_cast<PFN_vkCmdSetDepthCompareOp>(load(F::extended_dynamic_state, "vkCmdSetDepthCompareOp", "vkCmdSetDepthCompareOpEXT"));
        if(!settle(F::extended_dynamic_state)) fn.set_cull_mode = nullptr, fn.set_depth_write_enable = nullptr, fn.set_depth_compare_op = nullptr;
        fn.push_descriptor_set = reinterpret_cast<PFN_vkCmdPushDescriptorSet>(load(F::push_descriptor, "vkCmdPushDescriptorSet", "vkCmdPushDescriptorSetKHR"));
        if(!settle(F::push_descriptor)) fn.push_descriptor_set = nullptr;
        fn.transition_image_layout = reinterpret_cast<PFN_vkTransitionImageLayout>(load(F::host_image_copy, "vkTransitionImageLayout", "vkTransitionImageLayoutEXT"));
        fn.copy_memory_to_image = reinterpret_cast<PFN_vkCopyMemoryToImage>(load(F::host_image_copy, "vkCopyMemoryToImage", "vkCopyMemoryToImageEXT"));
        if(!settle(F::host_image_copy)) fn.transition_image_layout = nullptr, fn.copy_memory_to_image = nullptr;
    }

    // ------------------------------------------------------------ instance / device (N0, plus offscreen)
    void init() {
        auto enumerate_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
        loader_api = VK_API_VERSION_1_0;
        if(enumerate_version) check(enumerate_version(&loader_api), "loader version");
        if(loader_api < VK_API_VERSION_1_1) throw std::runtime_error("Vulkan 1.1 loader required");
        if(request.max_api < vk::kBaselineApi) throw std::runtime_error("Requested Vulkan API cap is below the 1.2 baseline");
        // VkApplicationInfo::apiVersion is the highest version this run may use. A 1.1+ loader accepts
        // any value; the device may still be older, which the per-device negotiation below handles.
        // Capping it (vk::Request::max_api) makes the validation layer check the lower-version path
        // as such: a 1.3 command used under a 1.2 cap is reported as an error.
        instance_api = std::min(request.max_api, vk::kHighestApi);
        std::uint32_t count = 0;
        std::vector<const char*> extensions;
        if(!offscreen) {
            const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&count);
            if(!sdl_extensions) throw std::runtime_error(SDL_GetError());
            extensions.assign(sdl_extensions, sdl_extensions + count);
        }
        check(vkEnumerateInstanceLayerProperties(&count, nullptr), "layer count");
        std::vector<VkLayerProperties> layers(count);
        check(vkEnumerateInstanceLayerProperties(&count, layers.data()), "layers");
        validation = settings.validation && std::any_of(layers.begin(), layers.end(), [](const auto& l) { return std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0; });
        check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr), "extension count");
        std::vector<VkExtensionProperties> available(count);
        check(vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data()), "extensions");
        const bool debug = std::any_of(available.begin(), available.end(), [](const auto& e) { return std::strcmp(e.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0; });
        if(debug && validation) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if(settings.validation && !validation) log("warning", "vulkan", "Validation requested but VK_LAYER_KHRONOS_validation is not installed");
        VkDebugUtilsMessengerCreateInfoEXT debug_info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debug_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug_info.pfnUserCallback = diagnostic;
        debug_info.pUserData = &counters;
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Cat Arena: Feral Claw N2.1"; app.applicationVersion = VK_MAKE_VERSION(0, 2, 1);
        app.pEngineName = "Cat Arena Native"; app.engineVersion = VK_MAKE_VERSION(0, 2, 1); app.apiVersion = instance_api;
        const char* layer = "VK_LAYER_KHRONOS_validation";
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()); info.ppEnabledExtensionNames = extensions.data();
        info.enabledLayerCount = validation ? 1U : 0U; info.ppEnabledLayerNames = validation ? &layer : nullptr;
        info.pNext = (debug && validation) ? &debug_info : nullptr;
        check(vkCreateInstance(&info, nullptr, &instance), "create instance");
        if(debug && validation) {
            auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            if(!create) throw std::runtime_error("Debug extension entry point missing");
            check(create(instance, &debug_info, nullptr, &messenger), "debug messenger");
        }
        if(!offscreen && !SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) throw std::runtime_error(SDL_GetError());
        // Vulkan 1.1 instance-level queries, resolved at run time (never linked).
        auto instance_fn = [&](const char* name) { return vkGetInstanceProcAddr(instance, name); };
        fn.features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(instance_fn("vkGetPhysicalDeviceFeatures2"));
        fn.properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(instance_fn("vkGetPhysicalDeviceProperties2"));
        fn.format_properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties2>(instance_fn("vkGetPhysicalDeviceFormatProperties2"));
        fn.image_format_properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(instance_fn("vkGetPhysicalDeviceImageFormatProperties2"));
        if(!fn.features2 || !fn.properties2 || !fn.format_properties2 || !fn.image_format_properties2)
            throw std::runtime_error("Vulkan 1.1 loader required (physical-device query entry points missing)");
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "device count");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "devices");
        int best_score = -1;
        std::string rejected;
        for(auto candidate : devices) {
            VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(candidate, &props);
            if(props.apiVersion < VK_API_VERSION_1_2) {   // mandatory baseline
                log("warning", "vulkan", std::string("Skipping device ") + props.deviceName + ": API " + version_string(props.apiVersion) + " is below the Vulkan 1.2 baseline");
                rejected += std::string(rejected.empty() ? "" : ", ") + props.deviceName + " (API " + version_string(props.apiVersion) + ")";
                continue;
            }
            std::uint32_t n = 0;
            if(!offscreen) {
                check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &n, nullptr), "device extensions count");
                std::vector<VkExtensionProperties> ex(n);
                check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &n, ex.data()), "device extensions");
                if(!std::any_of(ex.begin(), ex.end(), [](const auto& e) { return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0; })) continue;
                std::uint32_t formats = 0, modes = 0;
                check(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &formats, nullptr), "surface formats count");
                check(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface, &modes, nullptr), "surface modes count");
                if(!formats || !modes) continue;
            }
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, nullptr);
            std::vector<VkQueueFamilyProperties> families(n);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, families.data());
            for(std::uint32_t i = 0; i < n; ++i) {
                VkBool32 present = VK_TRUE;
                if(!offscreen) check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present), "present support");
                if(!families[i].queueCount || !(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
                const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4 : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                                : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 2 : 1;
                if(score > best_score) { best_score = score; physical = candidate; family = i; }
                break;
            }
        }
        if(!physical) throw std::runtime_error(std::string(offscreen ? "No Vulkan 1.2+ device with a graphics queue"
                                                                    : "No Vulkan 1.2+ device with swapchain and combined graphics/present queue") +
                                               (rejected.empty() ? "" : "; below the 1.2 baseline: " + rejected));
        vkGetPhysicalDeviceProperties(physical, &properties);
        VkPhysicalDeviceFeatures supported{}; vkGetPhysicalDeviceFeatures(physical, &supported);
        anisotropy = supported.samplerAnisotropy == VK_TRUE;   // optional capability; 1x fallback
        max_anisotropy = anisotropy ? std::min(4.0F, properties.limits.maxSamplerAnisotropy) : 1.0F;   // normal preset anisotropy 4
        detect_capabilities();
        path = vk::select_path(caps, request);
        create_device();
        resolve_device_functions();
        use_dynamic_rendering = path.uses(vk::Feature::dynamic_rendering);
        use_sync2 = path.uses(vk::Feature::synchronization2);
        use_dynamic_state = path.uses(vk::Feature::extended_dynamic_state);
        use_push_descriptor = path.uses(vk::Feature::push_descriptor);
        use_host_copy = path.uses(vk::Feature::host_image_copy);
        log("info", "vulkan", std::string("Device=") + properties.deviceName + " API=" + version_string(properties.apiVersion) +
                                  " driver=" + driver_name + " (" + driver_info + ") loader=" + version_string(loader_api) +
                                  " instanceApi=" + vk::api_string(instance_api, false) + " negotiated=" + vk::api_string(path.api, false) +
                                  " anisotropy=" + (anisotropy ? std::to_string(max_anisotropy) : std::string("unsupported")) +
                                  " validation=" + (validation ? "on" : "off") + (offscreen ? " target=offscreen" : " target=window"));
        log("info", "vulkan", "Render path: " + path.describe());
        for(const auto& note : path.notes) log("info", "vulkan", "  " + note);
        vkGetDeviceQueue(device, family, 0, &queue);
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.queueFamilyIndex = family; pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(vkCreateCommandPool(device, &pi, nullptr, &pool), "command pool");
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device, &ai, &command), "command buffer");
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device, &si, nullptr, &acquired), "acquire semaphore");
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device, &fi, nullptr, &fence), "frame fence");
        depth_format = VK_FORMAT_UNDEFINED;
        for(auto candidate : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}) {
            VkFormatProperties fp{}; vkGetPhysicalDeviceFormatProperties(physical, candidate, &fp);
            if(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) { depth_format = candidate; break; }
        }
        if(depth_format == VK_FORMAT_UNDEFINED) throw std::runtime_error("No depth attachment format");
        if(depth_format == VK_FORMAT_D16_UNORM) log("warning", "vulkan", "Only D16 depth available; near 0.05 / far 400 precision will be reduced");
    }

    // ------------------------------------------------------------ pipelines
    VkShaderModule module(const char* name) {
        auto words = shader_bytes(shaders / name);
        VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        mi.codeSize = words.size() * 4; mi.pCode = words.data();
        VkShaderModule m{};
        check(vkCreateShaderModule(device, &mi, nullptr, &m), name);
        return m;
    }
    VkPipeline make_pipeline(const char* vs_name, const char* fs_name, const VertexLayout& layout, VkPipelineLayout pipeline_layout, const PipelineState& st) {
        VkShaderModule vs = module(vs_name), fs{};
        ScopeExit free_modules([&] { vkDestroyShaderModule(device, vs, nullptr); if(fs) vkDestroyShaderModule(device, fs, nullptr); });
        fs = module(fs_name);
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vs; stages[0].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";
        VkVertexInputBindingDescription binding[2] = {{0, layout.stride, VK_VERTEX_INPUT_RATE_VERTEX}, {1, layout.instance_stride, VK_VERTEX_INPUT_RATE_INSTANCE}};
        VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        if(layout.stride) {
            vertex.vertexBindingDescriptionCount = layout.instance_stride ? 2 : 1; vertex.pVertexBindingDescriptions = binding;
            vertex.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(layout.attributes.size()); vertex.pVertexAttributeDescriptions = layout.attributes.data();
        }
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1; viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = st.cull; raster.frontFace = kLegacyFrontFace; raster.lineWidth = 1;
        if(st.polygon_offset) { raster.depthBiasEnable = VK_TRUE; raster.depthBiasSlopeFactor = -1; raster.depthBiasConstantFactor = st.offset_units; }   // polygonOffset(-1,-2)
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = st.depth_test ? VK_TRUE : VK_FALSE; depth.depthWriteEnable = st.depth_write ? VK_TRUE : VK_FALSE; depth.depthCompareOp = st.compare;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if(st.blend) {   // gl.blendFunc: same factors for colour and alpha
            attachment.blendEnable = VK_TRUE;
            attachment.srcColorBlendFactor = attachment.srcAlphaBlendFactor = st.src;
            attachment.dstColorBlendFactor = attachment.dstAlphaBlendFactor = st.dst;
            attachment.colorBlendOp = attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1; blend.pAttachments = &attachment;
        std::vector<VkDynamicState> states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        if(st.dynamic_raster) {   // optional extended dynamic state: the static values above are ignored for these
            states.push_back(VK_DYNAMIC_STATE_CULL_MODE);
            states.push_back(VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE);
            states.push_back(VK_DYNAMIC_STATE_DEPTH_COMPARE_OP);
        }
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = static_cast<std::uint32_t>(states.size()); dynamic.pDynamicStates = states.data();
        VkGraphicsPipelineCreateInfo gi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gi.stageCount = 2; gi.pStages = stages; gi.pVertexInputState = &vertex; gi.pInputAssemblyState = &assembly; gi.pViewportState = &viewport;
        gi.pRasterizationState = &raster; gi.pMultisampleState = &ms; gi.pDepthStencilState = &depth; gi.pColorBlendState = &blend;
        gi.pDynamicState = &dynamic; gi.layout = pipeline_layout;
        // Dynamic rendering: the pipeline names its attachment formats; baseline: it names the render pass.
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &format; rendering.depthAttachmentFormat = depth_format;
        if(use_dynamic_rendering) gi.pNext = &rendering; else gi.renderPass = pass;
        VkPipeline p{};
        check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gi, nullptr, &p), "graphics pipeline");
        return p;
    }
    void make_render_pass() {
        VkAttachmentDescription a[2]{};
        a[0].format = format; a[0].samples = VK_SAMPLE_COUNT_1_BIT; a[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; a[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; a[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; a[0].finalLayout = offscreen ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        a[1].format = depth_format; a[1].samples = VK_SAMPLE_COUNT_1_BIT; a[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; a[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; a[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; a[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 1; sub.pColorAttachments = &color_ref; sub.pDepthStencilAttachment = &depth_ref;
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask = offscreen ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        deps[1].dstAccessMask = offscreen ? VK_ACCESS_TRANSFER_READ_BIT : 0;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = 2; rp.pAttachments = a; rp.subpassCount = 1; rp.pSubpasses = &sub; rp.dependencyCount = 2; rp.pDependencies = deps;
        check(vkCreateRenderPass(device, &rp, nullptr, &pass), "render pass");
    }
    static VertexLayout layout_sky() { return {48, {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 24}}}; }
    static VertexLayout layout_world() {
        return {56, {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 24},
                     {3, 0, VK_FORMAT_R32G32_SFLOAT, 36}, {4, 0, VK_FORMAT_R32_SFLOAT, 44}, {5, 0, VK_FORMAT_R32G32_SFLOAT, 48}}};
    }
    static VertexLayout layout_material() {
        return {72, {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 24},
                     {3, 0, VK_FORMAT_R32G32_SFLOAT, 40}, {4, 0, VK_FORMAT_R32G32_SFLOAT, 48}, {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 56}}};
    }
    // Baseline: one pipeline per complete state key. With extended dynamic state, cull mode, depth
    // write and depth compare are set per draw (draw_material), so passes that differ only in those
    // share a pipeline: the key is reduced to blend state + polygon offset.
    VkPipeline material_pipeline(const PipelineKey& full_key) {
        PipelineKey key = full_key;
        if(use_dynamic_state) { key.cull = 0; key.depth_write = true; key.depth_equal = false; }
        if(auto it = material_pipelines.find(key); it != material_pipelines.end()) return it->second;
        PipelineState st;
        st.dynamic_raster = use_dynamic_state;
        st.cull = cull_flags(key.cull);
        st.depth_write = key.depth_write; st.compare = key.depth_equal ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL;
        st.blend = key.blend; st.src = blend_factor(key.src); st.dst = blend_factor(key.dst); st.polygon_offset = key.polygon_offset;
        auto p = make_pipeline("material.vert.spv", "material.frag.spv", layout_material(), scene_layout, st);
        material_pipelines.emplace(key, p);
        return p;
    }
    void build_pipelines() {
        if(!triangle_layout) {
            VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            check(vkCreatePipelineLayout(device, &li, nullptr, &triangle_layout), "triangle layout");
        }
        PipelineState tri; tri.depth_test = false; tri.depth_write = false;
        triangle_pipeline = make_pipeline("triangle.vert.spv", "triangle.frag.spv", {}, triangle_layout, tri);
        if(!scene_loaded) return;
        PipelineState sky; sky.compare = VK_COMPARE_OP_LESS;   // bake: depthFunc LESS, no culling
        sky_pipeline = make_pipeline("sky.vert.spv", "sky.frag.spv", layout_sky(), scene_layout, sky);
        PipelineState box; box.depth_write = false; box.compare = VK_COMPARE_OP_LESS_OR_EQUAL;   // drawSky: no cull, depthMask false, LEQUAL
        skybox_pipeline = make_pipeline("skybox.vert.spv", "skybox.frag.spv", {}, scene_layout, box);
        PipelineState world; world.cull = VK_CULL_MODE_BACK_BIT; world.compare = VK_COMPARE_OP_LESS;   // render(): CULL_FACE BACK, depth LESS
        world_pipeline = make_pipeline("world.vert.spv", "world.frag.spv", layout_world(), scene_layout, world);
        PipelineState dynamic; dynamic.compare = VK_COMPARE_OP_LESS;   // drawDynamic: isGun 2, CULL_FACE disabled
        dynamic_pipeline = make_pipeline("dynamic.vert.spv", "dynamic.frag.spv",
            {48, {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 24}, {3, 0, VK_FORMAT_R32G32_SFLOAT, 36}, {4, 0, VK_FORMAT_R32_SFLOAT, 44}}}, scene_layout, dynamic);
        for(auto& m : gpu_materials) for(auto& p : m.passes) p.pipeline = material_pipeline(p.key);
        build_enemy_pipelines();
    }
    // ------------------------------------------------------------ N6 creatures
    static VertexLayout layout_enemy() {
        VertexLayout l{48, {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 24}, {3, 0, VK_FORMAT_R32G32_SFLOAT, 36}, {4, 0, VK_FORMAT_R32_SFLOAT, 44}}, kEnemyInstanceBytes};
        for(std::uint32_t i = 0; i < 7; ++i) l.attributes.push_back({5 + i, 1, VK_FORMAT_R32G32B32A32_SFLOAT, i * 16});   // instPos, instState, instPose0..3, instTint
        return l;
    }
    void build_enemy_pipelines() {
        if(!enemies_ready || !scene_loaded || !set0_layout || enemy_pipeline) return;
        if(!enemy_layout) {
            VkDescriptorSetLayout sets[2] = {set0_layout, enemy_set_layout};
            VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(EnemyPush)};
            VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            li.setLayoutCount = 2; li.pSetLayouts = sets; li.pushConstantRangeCount = 1; li.pPushConstantRanges = &range;
            check(vkCreatePipelineLayout(device, &li, nullptr, &enemy_layout), "enemy pipeline layout");
        }
        PipelineState solid; solid.cull = VK_CULL_MODE_BACK_BIT; solid.compare = VK_COMPARE_OP_LESS;   // drawEnemyInstances: CULL_FACE on, depth LESS
        enemy_pipeline = make_pipeline("enemy.vert.spv", "enemy.frag.spv", layout_enemy(), enemy_layout, solid);
        // drawFinalFiveXray: SRC_ALPHA blend, depthMask false, depth GREATER (only where hidden), two-sided
        PipelineState outline; outline.depth_write = false; outline.compare = VK_COMPARE_OP_GREATER; outline.blend = true; outline.src = VK_BLEND_FACTOR_SRC_ALPHA; outline.dst = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        enemy_outline_pipeline = make_pipeline("enemy.vert.spv", "enemy.frag.spv", layout_enemy(), enemy_layout, outline);
        // drawEnemyShadows: ONE / ONE_MINUS_SRC_ALPHA, two-sided, depth LESS with depth writes (one layer per pixel), polygonOffset(-1,-2)
        PipelineState shadow; shadow.compare = VK_COMPARE_OP_LESS; shadow.blend = true; shadow.src = VK_BLEND_FACTOR_ONE; shadow.dst = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; shadow.polygon_offset = true;
        enemy_shadow_pipeline = make_pipeline("enemy.vert.spv", "enemy.frag.spv", layout_enemy(), enemy_layout, shadow);
    }
    void write_enemy_volume() {
        for(const auto set : enemy_sets) {
            VkDescriptorImageInfo info{sampler_volume, volume_tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = set; w.dstBinding = 3; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &info;
            vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
        }
    }
    static scene::Image flat_volume() { scene::Image i; i.width = 1; i.height = 1; i.rgba = {107, 107, 107, 255}; return i; }   // 0.42 * 1.9 = 0.8: the flat light of the legacy without a grid
    void set_light_volume(const scene::Image& atlas) {
        check(vkDeviceWaitIdle(device), "light volume idle");
        destroy(volume_tex);
        volume_tex = texture({atlas.rgba.empty() ? flat_volume() : atlas}, false, false);
        if(enemies_ready) write_enemy_volume();
    }
    void load_enemies(const EnemyAssets& a) {
        check(vkDeviceWaitIdle(device), "enemies idle");
        Texture keep = volume_tex; volume_tex = {};
        cleanup_enemies();
        volume_tex = keep;
        if(a.types.empty()) throw std::runtime_error("enemies: no creature models");
        const auto count = static_cast<std::uint32_t>(a.types.size() + 1);
        VkDescriptorSetLayoutBinding b[4];
        for(std::uint32_t i = 0; i < 4; ++i) b[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount = 4; li.pBindings = b;
        check(vkCreateDescriptorSetLayout(device, &li, nullptr, &enemy_set_layout), "enemy set layout");
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count * 4};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = count; pi.poolSizeCount = 1; pi.pPoolSizes = &size;
        check(vkCreateDescriptorPool(device, &pi, nullptr, &enemy_pool), "enemy descriptor pool");
        // meshEnemyTextures: CLAMP_TO_EDGE, LINEAR_MIPMAP_LINEAR; light volume and cat faces: LINEAR, no mip maps
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_LINEAR; si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; si.maxAnisotropy = 1; si.maxLod = VK_LOD_CLAMP_NONE;
        check(vkCreateSampler(device, &si, nullptr, &sampler_enemy), "enemy sampler");
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; si.maxLod = 0;
        check(vkCreateSampler(device, &si, nullptr, &sampler_volume), "light volume sampler");
        if(!volume_tex.view) volume_tex = texture({flat_volume()}, false, false);
        enemy_tex.reserve(count * 3);
        auto add = [&](const EnemyModel& m, bool marine) {
            if(m.vertices.empty() || m.vertices.size() % 36 != 0) throw std::runtime_error("enemies: model is not a list of 12-float triangles");
            enemy_vb.push_back(device_buffer(m.vertices.data(), m.vertices.size() * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
            enemy_vertex_count.push_back(static_cast<std::uint32_t>(m.vertices.size() / 12));
            enemy_rig.push_back(m.rig);
            VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            ai.descriptorPool = enemy_pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &enemy_set_layout;
            VkDescriptorSet set{};
            check(vkAllocateDescriptorSets(device, &ai, &set), "enemy descriptor set");
            std::uint32_t binding = 0;
            for(const scene::Image* image : {&m.base, marine ? &m.base : &m.metal_rough, marine ? &m.base : &m.normal}) {
                enemy_tex.push_back(texture({*image}, !marine, false));
                VkDescriptorImageInfo info{marine ? sampler_volume : sampler_enemy, enemy_tex.back().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                w.dstSet = set; w.dstBinding = binding++; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &info;
                vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
            }
            enemy_sets.push_back(set);
        };
        for(const auto& m : a.types) add(m, false);
        add(a.marine, true);
        write_enemy_volume();
        enemy_instances = make_buffer(kEnemyMaxInstances * kEnemyInstanceBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        enemies_ready = true;
        if(pipelines_built) build_enemy_pipelines();
    }
    void record_enemies(VkCommandBuffer cb, const Overlay& o) {
        if(!enemies_ready || !enemy_pipeline || !o.enemies || !o.enemy_batches) return;
        const VkDeviceSize zero = 0;
        std::size_t at = 0;
        bool bound = false;
        for(std::size_t i = 0; i < o.enemy_batches; ++i) {
            const EnemyBatch& batch = o.enemies[i];
            if(batch.mesh < 0 || static_cast<std::size_t>(batch.mesh) >= enemy_vb.size() || !batch.count || !batch.instances) continue;
            const std::size_t n = std::min(batch.count, kEnemyMaxInstances - at);
            if(!n) break;
            std::memcpy(static_cast<char*>(enemy_instances.mapped) + at * kEnemyInstanceBytes, batch.instances, n * kEnemyInstanceBytes);
            const auto m = static_cast<std::size_t>(batch.mesh);
            const VkPipeline pipeline = batch.pass == EnemyPass::outline ? enemy_outline_pipeline : batch.pass == EnemyPass::shadow ? enemy_shadow_pipeline : enemy_pipeline;
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            if(!bound) { vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, enemy_layout, 0, 1, &set0, 0, nullptr); bound = true; }
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, enemy_layout, 1, 1, &enemy_sets[m], 0, nullptr);
            const VkBuffer buffers[2] = {enemy_vb[m].buffer, enemy_instances.buffer};
            const VkDeviceSize offsets[2] = {zero, at * kEnemyInstanceBytes};
            vkCmdBindVertexBuffers(cb, 0, 2, buffers, offsets);
            EnemyPush p{};
            const auto& rig = enemy_rig[m];
            for(std::size_t k = 0; k < 4; ++k) { p.rig0[k] = rig[k]; p.rig1[k] = rig[4 + k]; p.rig2[k] = rig[8 + k]; }
            p.mode[0] = static_cast<float>(static_cast<int>(batch.pass)); p.mode[1] = o.shadow_dir[0]; p.mode[2] = o.shadow_dir[1]; p.mode[3] = o.light_grid ? 1.0F : 0.0F;
            vkCmdPushConstants(cb, enemy_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(p), &p);
            vkCmdDraw(cb, enemy_vertex_count[m], static_cast<std::uint32_t>(n), 0, 0);
            ++stats.draw_calls; ++stats.overlay_draws; ++stats.enemy_draws; stats.enemy_instances += static_cast<std::uint32_t>(n);
            at += n;
        }
    }
    // ------------------------------------------------------------ N5 overlay
    static VertexLayout layout_fx() { return {48, {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 28}, {3, 0, VK_FORMAT_R32_SFLOAT, 44}}}; }
    static VertexLayout layout_gun() { return {48, {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32_SFLOAT, 36}}}; }
    static VertexLayout layout_hud() { return {24, {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 8}}}; }
    void build_overlay_pipelines() {
        if(!overlay_ready || fx_pipeline) return;
        // FXR.draw: blend ONE / ONE_MINUS_SRC_ALPHA (premultiplied), depthMask false, no culling, depth LEQUAL.
        PipelineState fx; fx.depth_write = false; fx.compare = VK_COMPARE_OP_LESS_OR_EQUAL; fx.blend = true; fx.src = VK_BLEND_FACTOR_ONE; fx.dst = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        fx_pipeline = make_pipeline("fx.vert.spv", "fx.frag.spv", layout_fx(), fx_layout, fx);
        PipelineState mark = fx; mark.polygon_offset = true; mark.offset_units = -3;   // marks: polygonOffset(-1,-3)
        fx_mark_pipeline = make_pipeline("fx.vert.spv", "fx.frag.spv", layout_fx(), fx_layout, mark);
        PipelineState flash = fx; flash.depth_test = false;   // gun batch: depthTest false
        fx_gun_pipeline = make_pipeline("fx.vert.spv", "fx.frag.spv", layout_fx(), fx_layout, flash);
        PipelineState gun; gun.compare = VK_COMPARE_OP_LESS;   // renderGun3D: depth cleared, two-sided
        gun_pipeline = make_pipeline("gun.vert.spv", "gun.frag.spv", layout_gun(), gun_layout, gun);
        PipelineState hud; hud.depth_test = false; hud.depth_write = false; hud.blend = true; hud.src = VK_BLEND_FACTOR_SRC_ALPHA; hud.dst = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        hud_pipeline = make_pipeline("hud.vert.spv", "hud.frag.spv", layout_hud(), hud_layout, hud);
        PipelineState solid; solid.compare = VK_COMPARE_OP_LESS;
        solid_pipeline = make_pipeline("solid.vert.spv", "hud.frag.spv", {28, {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 12}}}, solid_layout, solid);
    }
    void load_overlay(const OverlayAssets& a) {
        check(vkDeviceWaitIdle(device), "overlay idle");
        for(VkPipeline* p : {&fx_pipeline, &fx_mark_pipeline, &fx_gun_pipeline, &gun_pipeline, &hud_pipeline, &solid_pipeline}) { if(*p) vkDestroyPipeline(device, *p, nullptr); *p = {}; }
        cleanup_overlay();
        if(a.weapons.empty()) throw std::runtime_error("overlay: no weapon models");
        const auto guns = static_cast<std::uint32_t>(a.weapons.size());
        auto set_layout = [&](std::uint32_t count, VkDescriptorSetLayout& out) {
            std::vector<VkDescriptorSetLayoutBinding> b(count);
            for(std::uint32_t i = 0; i < count; ++i) b[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            li.bindingCount = count; li.pBindings = b.data();
            check(vkCreateDescriptorSetLayout(device, &li, nullptr, &out), "overlay set layout");
        };
        set_layout(1, overlay_layout1); set_layout(3, overlay_layout3);
        auto pipeline_layout = [&](const VkDescriptorSetLayout* set, bool push, VkPipelineLayout& out) {
            VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(OverlayPush)};
            VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            if(set) { li.setLayoutCount = 1; li.pSetLayouts = set; }
            if(push) { li.pushConstantRangeCount = 1; li.pPushConstantRanges = &range; }
            check(vkCreatePipelineLayout(device, &li, nullptr, &out), "overlay pipeline layout");
        };
        pipeline_layout(&overlay_layout1, true, fx_layout); pipeline_layout(&overlay_layout3, true, gun_layout); pipeline_layout(nullptr, false, hud_layout); pipeline_layout(nullptr, true, solid_layout);
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 + guns * 3};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = 1 + guns; pi.poolSizeCount = 1; pi.pPoolSizes = &size;
        check(vkCreateDescriptorPool(device, &pi, nullptr, &overlay_pool), "overlay descriptor pool");
        // Samplers as the legacy sets them: CLAMP_TO_EDGE, LINEAR_MIPMAP_LINEAR; the effect atlas is limited to 4 mip levels.
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_LINEAR; si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; si.maxAnisotropy = 1; si.maxLod = 4;
        check(vkCreateSampler(device, &si, nullptr, &sampler_fx), "fx sampler");
        si.maxLod = VK_LOD_CLAMP_NONE;
        check(vkCreateSampler(device, &si, nullptr, &sampler_gun), "gun sampler");
        auto allocate = [&](VkDescriptorSetLayout layout) {
            VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            ai.descriptorPool = overlay_pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &layout;
            VkDescriptorSet set{};
            check(vkAllocateDescriptorSets(device, &ai, &set), "overlay descriptor set");
            return set;
        };
        auto write = [&](VkDescriptorSet set, std::uint32_t binding, VkSampler sampler, const Texture& t) {
            VkDescriptorImageInfo info{sampler, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = set; w.dstBinding = binding; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &info;
            vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
        };
        fx_tex = texture({a.fx_atlas}, true, false);
        fx_set = allocate(overlay_layout1);
        write(fx_set, 0, sampler_fx, fx_tex);
        gun_tex.reserve(guns * 3);
        for(const auto& w : a.weapons) {
            if(w.vertices.empty() || w.vertices.size() % 36 != 0) throw std::runtime_error("overlay: weapon model is not a list of 12-float triangles");
            gun_vb.push_back(device_buffer(w.vertices.data(), w.vertices.size() * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
            gun_vertex_count.push_back(static_cast<std::uint32_t>(w.vertices.size() / 12));
            const auto set = allocate(overlay_layout3);
            std::uint32_t binding = 0;
            for(const scene::Image* image : {&w.base, &w.metal_rough, &w.normal}) { gun_tex.push_back(texture({*image}, true, false)); write(set, binding++, sampler_gun, gun_tex.back()); }
            gun_sets.push_back(set);
        }
        const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        fx_vb = make_buffer(kFxMaxQuads * 4 * 48, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host);
        fx_ib = make_buffer(kFxMaxQuads * 6 * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, host);
        hud_vb = make_buffer(kHudMaxVertices * sizeof(HudVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host);
        solid_vb = make_buffer(kSolidMaxVertices * sizeof(SolidVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host);
        atlas_vb = make_buffer(kAtlasMaxVertices * 48, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host);
        overlay_ready = true;
        if(pipelines_built) build_overlay_pipelines();
    }
    void record_overlay(VkCommandBuffer cb, const Overlay& o) {
        const VkDeviceSize zero = 0;
        auto push = [&](VkPipelineLayout layout, const OverlayCamera& c, float up_w, const float* extra, float extra_w) {
            OverlayPush p{};
            for(int i = 0; i < 3; ++i) { p.cam_pos[i] = c.pos[i]; p.forward[i] = c.forward[i]; p.right[i] = c.right[i]; p.up[i] = c.up[i]; p.extra[i] = extra[i]; }
            p.forward[3] = c.tan_half * c.aspect; p.right[3] = c.tan_half; p.up[3] = up_w; p.extra[3] = extra_w;
            vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(p), &p);
        };
        // The three effect batches share one vertex and one index buffer.
        std::size_t vertex_at = 0, index_at = 0;
        struct Range { std::uint32_t first_index{}, count{}; std::int32_t base_vertex{}; };
        auto upload = [&](const OverlayBatch& b) {
            Range r;
            if(!b.vertex_count || !b.index_count || (vertex_at + b.vertex_count) > kFxMaxQuads * 4 || (index_at + b.index_count) > kFxMaxQuads * 6) return r;
            std::memcpy(static_cast<char*>(fx_vb.mapped) + vertex_at * 48, b.vertices, b.vertex_count * 48);
            std::memcpy(static_cast<char*>(fx_ib.mapped) + index_at * 4, b.indices, b.index_count * 4);
            r = {static_cast<std::uint32_t>(index_at), static_cast<std::uint32_t>(b.index_count), static_cast<std::int32_t>(vertex_at)};
            vertex_at += b.vertex_count; index_at += b.index_count;
            stats.effect_quads += static_cast<std::uint32_t>(b.vertex_count / 4);
            return r;
        };
        const Range mark = upload(o.mark), main = upload(o.main), flash = upload(o.gun_fx);
        record_enemies(cb, o);   // legacy order: creatures, Marine Cats, shadows, outlines, then the dynamic geometry
        auto draw_batch = [&](VkPipeline pipeline, const Range& r, const OverlayCamera& c, float fog_on) {
            if(!r.count) return;
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, fx_layout, 0, 1, &fx_set, 0, nullptr);
            vkCmdBindVertexBuffers(cb, 0, 1, &fx_vb.buffer, &zero);
            vkCmdBindIndexBuffer(cb, fx_ib.buffer, 0, VK_INDEX_TYPE_UINT32);
            push(fx_layout, c, fog_on, last_fog, last_fog[3]);
            vkCmdDrawIndexed(cb, r.count, 1, r.first_index, r.base_vertex, 0); ++stats.draw_calls; ++stats.overlay_draws;
        };
        if(o.atlas && o.atlas_vertices && dynamic_pipeline) {   // legacy order: dynamic geometry before marks and effects
            const std::size_t n = std::min(o.atlas_vertices, kAtlasMaxVertices) / 3 * 3;
            std::memcpy(atlas_vb.mapped, o.atlas, n * 48);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, dynamic_pipeline);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_layout, 0, 1, &set0, 0, nullptr);
            vkCmdBindVertexBuffers(cb, 0, 1, &atlas_vb.buffer, &zero);
            vkCmdDraw(cb, static_cast<std::uint32_t>(n), 1, 0, 0); ++stats.draw_calls; ++stats.overlay_draws;
        }
        if(o.solid && o.solid_vertices) {
            const std::size_t n = std::min(o.solid_vertices, kSolidMaxVertices) / 3 * 3;
            std::memcpy(solid_vb.mapped, o.solid, n * sizeof(SolidVertex));
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, solid_pipeline);
            vkCmdBindVertexBuffers(cb, 0, 1, &solid_vb.buffer, &zero);
            const float none[3]{};
            push(solid_layout, o.camera, 0, none, 0);
            vkCmdDraw(cb, static_cast<std::uint32_t>(n), 1, 0, 0); ++stats.draw_calls; ++stats.overlay_draws;
        }
        draw_batch(fx_mark_pipeline, mark, o.camera, 1);
        draw_batch(fx_pipeline, main, o.camera, 1);
        if(o.gun_visible && o.gun >= 0 && static_cast<std::size_t>(o.gun) < gun_vb.size()) {
            // renderGun3D: weapon-only depth plane, then the two-sided model with its own camera
            VkClearAttachment clear{};
            clear.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; clear.clearValue.depthStencil = {1.0F, 0};
            VkClearRect rect{{{0, 0}, extent}, 0, 1};
            vkCmdClearAttachments(cb, 1, &clear, 1, &rect);
            const auto g = static_cast<std::size_t>(o.gun);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, gun_pipeline);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, gun_layout, 0, 1, &gun_sets[g], 0, nullptr);
            vkCmdBindVertexBuffers(cb, 0, 1, &gun_vb[g].buffer, &zero);
            push(gun_layout, o.gun_camera, 0, o.gun_glow, 0);
            vkCmdDraw(cb, gun_vertex_count[g], 1, 0, 0); ++stats.draw_calls; ++stats.overlay_draws;
            draw_batch(fx_gun_pipeline, flash, o.gun_camera, 0);
        }
        if(o.hud && o.hud_vertices) {
            const std::size_t n = std::min(o.hud_vertices, kHudMaxVertices) / 3 * 3;
            std::memcpy(hud_vb.mapped, o.hud, n * sizeof(HudVertex));
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, hud_pipeline);
            vkCmdBindVertexBuffers(cb, 0, 1, &hud_vb.buffer, &zero);
            vkCmdDraw(cb, static_cast<std::uint32_t>(n), 1, 0, 0); ++stats.draw_calls; ++stats.overlay_draws;
            stats.hud_triangles = static_cast<std::uint32_t>(n / 3);
        }
    }

    // ------------------------------------------------------------ targets
    void create_offscreen_target() {
        color_target = make_image(extent, format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        readback = make_buffer(VkDeviceSize(extent.width) * extent.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        views.push_back(VK_NULL_HANDLE);   // framebuffer 0 uses color_target.view
    }
    bool recreate() {
        if(!offscreen) {
            int width = 0, height = 0;
            if(!SDL_GetWindowSizeInPixels(window, &width, &height)) throw std::runtime_error(SDL_GetError());
            if(width <= 0 || height <= 0 || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) return false;
        }
        check(vkDeviceWaitIdle(device), "resize idle");
        const VkExtent2D keep = extent;
        cleanup_targets();
        if(offscreen) {
            extent = keep;
            format = VK_FORMAT_R8G8B8A8_UNORM;
            create_offscreen_target();
        } else if(!create_swapchain()) return false;
        depth_target = make_image(extent, depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        if(pipelines_built && pass_format != format) cleanup_pipelines();
        if(!pipelines_built) {
            if(!use_dynamic_rendering) make_render_pass();   // baseline; dynamic rendering needs no render pass object
            pass_format = format; pass_depth = depth_format;
            build_pipelines();
            build_overlay_pipelines();
            pipelines_built = true;
        }
        const std::size_t n = use_dynamic_rendering ? 0 : offscreen ? 1 : views.size();   // framebuffers: baseline only
        for(std::size_t i = 0; i < n; ++i) {
            VkImageView attachments[2] = {offscreen ? color_target.view : views[i], depth_target.view};
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fi.renderPass = pass; fi.attachmentCount = 2; fi.pAttachments = attachments; fi.width = extent.width; fi.height = extent.height; fi.layers = 1;
            VkFramebuffer fb{};
            check(vkCreateFramebuffer(device, &fi, nullptr, &fb), "framebuffer");
            framebuffers.push_back(fb);
        }
        dirty = false;
        log("info", "vulkan", std::string(offscreen ? "Offscreen target " : "Swapchain ") + std::to_string(extent.width) + "x" + std::to_string(extent.height) +
                                  " color=" + format_name(format) + " depth=" + format_name(depth_format) + (offscreen ? "" : std::string(" present=") + present_name(present_mode)));
        return true;
    }
    bool create_swapchain() {
        int width = 0, height = 0;
        if(!SDL_GetWindowSizeInPixels(window, &width, &height)) throw std::runtime_error(SDL_GetError());
        VkSurfaceCapabilitiesKHR surface_caps{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &surface_caps), "surface capabilities");
        if(!(surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) throw std::runtime_error("Surface cannot be color attachment");
        extent = surface_caps.currentExtent;
        if(extent.width == std::numeric_limits<std::uint32_t>::max())
            extent = {std::clamp(static_cast<std::uint32_t>(width), surface_caps.minImageExtent.width, surface_caps.maxImageExtent.width),
                      std::clamp(static_cast<std::uint32_t>(height), surface_caps.minImageExtent.height, surface_caps.maxImageExtent.height)};
        if(!extent.width || !extent.height) return false;
        std::uint32_t n = 0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, nullptr), "formats count");
        std::vector<VkSurfaceFormatKHR> formats(n);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, formats.data()), "formats");
        if(formats.empty()) throw std::runtime_error("No surface formats");
        // WebGL writes shader outputs to a non-sRGB 8-bit canvas. A UNORM swapchain reproduces that;
        // an *_SRGB swapchain would gamma-encode again and wash out every colour.
        auto chosen = formats[0];
        if(chosen.format == VK_FORMAT_UNDEFINED) chosen = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        bool unorm = false;
        for(auto pref : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}) {
            for(auto f : formats) if(f.format == pref && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = f; unorm = true; break; }
            if(unorm) break;
        }
        if(!unorm) log("warning", "vulkan", "No UNORM swapchain format; colours may differ from the legacy WebGL canvas");
        format = chosen.format;
        check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &n, nullptr), "modes count");
        std::vector<VkPresentModeKHR> modes(n);
        check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &n, modes.data()), "modes");
        present_mode = VK_PRESENT_MODE_FIFO_KHR;
        if(!settings.vsync) {
            if(std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()) present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
            else if(std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end()) present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            else log("warning", "vulkan", "VSync off requested; surface supports FIFO only");
        }
        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        for(auto bit : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if(surface_caps.supportedCompositeAlpha & bit) { alpha = bit; break; }
        std::uint32_t images = surface_caps.minImageCount + 1;
        if(surface_caps.maxImageCount) images = std::min(images, surface_caps.maxImageCount);
        VkSwapchainCreateInfoKHR sc{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        sc.surface = surface; sc.minImageCount = images; sc.imageFormat = format; sc.imageColorSpace = chosen.colorSpace; sc.imageExtent = extent;
        sc.imageArrayLayers = 1; sc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sc.preTransform = surface_caps.currentTransform; sc.compositeAlpha = alpha; sc.presentMode = present_mode; sc.clipped = VK_TRUE;
        check(vkCreateSwapchainKHR(device, &sc, nullptr, &swapchain), "swapchain");
        check(vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr), "image count");
        std::vector<VkImage> handles(n);
        check(vkGetSwapchainImagesKHR(device, swapchain, &n, handles.data()), "images");
        swap_images = handles;
        for(auto image : handles) {
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = format; vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkImageView view{};
            check(vkCreateImageView(device, &vi, nullptr, &view), "image view");
            views.push_back(view);
            VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VkSemaphore semaphore{};
            check(vkCreateSemaphore(device, &si, nullptr, &semaphore), "present semaphore");
            presented.push_back(semaphore);
        }
        return true;
    }

    // ------------------------------------------------------------ scene upload
    void load(const scene::MapScene& s) {
        check(vkDeviceWaitIdle(device), "scene idle");
        for(auto& [key, p] : material_pipelines) vkDestroyPipeline(device, p, nullptr);
        material_pipelines.clear();
        if(sky_pipeline) vkDestroyPipeline(device, sky_pipeline, nullptr);
        if(skybox_pipeline) vkDestroyPipeline(device, skybox_pipeline, nullptr);
        skybox_pipeline = {};
        if(world_pipeline) vkDestroyPipeline(device, world_pipeline, nullptr);
        if(dynamic_pipeline) vkDestroyPipeline(device, dynamic_pipeline, nullptr);
        sky_pipeline = world_pipeline = dynamic_pipeline = {};
        cleanup_scene();
        staging_texture_bytes = host_copy_texture_bytes = 0;
        materials = s.material_defs; bindings = s.bindings; clusters = s.clusters;
        sky_material = s.sky_material_index; theme = s.theme; sky_center = s.sky_center; sky_eye_z = s.sky_eye_z;
        // Descriptor layouts
        VkDescriptorSetLayoutBinding b0[7]{};
        b0[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        for(std::uint32_t i = 1; i < 7; ++i)   // 1-5: N2 textures, 6: N4 sky box array
            b0[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_FRAGMENT_BIT) | (i == 2 ? static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_VERTEX_BIT) : 0U), nullptr};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount = 7; li.pBindings = b0;
        check(vkCreateDescriptorSetLayout(device, &li, nullptr, &set0_layout), "set0 layout");
        // Set 1 = the per-pass Draw block (same shader interface on both paths).
        //   baseline: an allocated set with a UNIFORM_BUFFER_DYNAMIC binding, selected by dynamic offset
        //   push descriptors: a push-descriptor set layout, the buffer range is pushed per pass
        VkDescriptorSetLayoutBinding b1{0, use_push_descriptor ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
                                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        li.bindingCount = 1; li.pBindings = &b1;
        li.flags = use_push_descriptor ? static_cast<VkDescriptorSetLayoutCreateFlags>(VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT) : 0U;
        check(vkCreateDescriptorSetLayout(device, &li, nullptr, &set1_layout), "set1 layout");
        VkDescriptorSetLayout set_layouts[2] = {set0_layout, set1_layout};
        VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pli.setLayoutCount = 2; pli.pSetLayouts = set_layouts;
        check(vkCreatePipelineLayout(device, &pli, nullptr, &scene_layout), "scene pipeline layout");
        const std::uint32_t allocated_sets = use_push_descriptor ? 1U : 2U;
        VkDescriptorPoolSize sizes[3] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1}};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = allocated_sets; pi.poolSizeCount = use_push_descriptor ? 2U : 3U; pi.pPoolSizes = sizes;
        check(vkCreateDescriptorPool(device, &pi, nullptr, &descriptor_pool), "descriptor pool");
        VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dai.descriptorPool = descriptor_pool; dai.descriptorSetCount = allocated_sets; dai.pSetLayouts = set_layouts;
        VkDescriptorSet sets[2]{};
        check(vkAllocateDescriptorSets(device, &dai, sets), "descriptor sets");
        set0 = sets[0]; set1 = sets[1];   // set1 stays null with push descriptors
        // Samplers: normal preset = trilinear, anisotropy 4 (when supported), REPEAT; lightmap bilinear, clamp, no mips.
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_LINEAR; si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.anisotropyEnable = anisotropy ? VK_TRUE : VK_FALSE; si.maxAnisotropy = max_anisotropy; si.maxLod = VK_LOD_CLAMP_NONE;
        check(vkCreateSampler(device, &si, nullptr, &sampler_repeat), "repeat sampler");
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.anisotropyEnable = VK_FALSE; si.maxAnisotropy = 1; si.maxLod = 0;
        check(vkCreateSampler(device, &si, nullptr, &sampler_clamp), "clamp sampler");
        // Geometry
        auto vb = [&](const assets::Mesh& m) { return device_buffer(m.vertices.data(), m.vertices.size() * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT); };
        auto ib = [&](const assets::Mesh& m) { return device_buffer(m.indices.data(), m.indices.size() * sizeof(std::uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT); };
        world_vb = vb(s.world); world_ib = ib(s.world); world_indices = static_cast<std::uint32_t>(s.world.indices.size());
        sky_vb = vb(s.sky); sky_ib = ib(s.sky); sky_indices = static_cast<std::uint32_t>(s.sky.indices.size());
        mat_vb = vb(s.materials); mat_ib = ib(s.materials);
        cloud_vb = vb(s.clouds); cloud_ib = ib(s.clouds); cloud_indices = static_cast<std::uint32_t>(s.clouds.indices.size());
        // Textures
        world_tex = texture(s.world_layers, true, true);
        lightmap_tex = texture({s.lightmap}, false, false);
        mat_tex = texture(s.layers256, true, true);
        kit_tex = texture(s.layers512, true, true);
        portal_tex = texture({scene::Image{1, 1, {0, 0, 0, 255}}}, false, false);
        {   // N4 image sky box: six layers in legacy order; a 1x1 placeholder keeps the binding valid otherwise
            std::vector<scene::Image> faces;
            has_sky_box = !s.sky_box.rgba.empty();
            if(has_sky_box) {
                const auto n = s.sky_box.width;
                for(std::uint32_t f = 0; f < 6; ++f) {
                    scene::Image face{n, n, {}};
                    face.rgba.assign(s.sky_box.rgba.begin() + std::ptrdiff_t(f) * n * n * 4, s.sky_box.rgba.begin() + std::ptrdiff_t(f + 1) * n * n * 4);
                    faces.push_back(std::move(face));
                }
            } else faces.assign(6, scene::Image{1, 1, {0, 0, 0, 255}});
            box_tex = texture(faces, false, true);
            sky_fog = s.sky_fog;
            sky_rotate_deg = s.material_defs.at(s.sky_material_index).sky_rotate;
        }
        // Material passes (Q3MAT.drawMaterial / applyState)
        gpu_materials.assign(materials.size(), {});
        pass_slots = 0;
        for(std::size_t i = 0; i < materials.size(); ++i) {
            const auto& m = materials[i];
            if(m.stages.empty()) throw std::runtime_error("material without stages is not supported: " + m.name);
            auto active = scene::active_stages(m, true);
            std::vector<std::uint32_t> idx;
            for(const auto* st : active) idx.push_back(static_cast<std::uint32_t>(st - m.stages.data()));
            const int cull = m.cull == scene::Cull::back ? 0 : m.cull == scene::Cull::front ? 1 : 2;
            if(i == sky_material) {   // drawSky -> skyDraw: no culling, no depth writes, LEQUAL; one pass per stage
                for(auto k : idx) {
                    const auto& st = m.stages[k];
                    Pass p; p.stages = {k}; p.fog = 3;
                    p.key = {0, 2, st.blend_src, st.blend_dst, st.has_blend && !(st.blend_src == 1 && st.blend_dst == 0), false, false, false};
                    p.slot = pass_slots++;
                    gpu_materials[i].passes.push_back(p);
                }
                continue;
            }
            if(scene::can_collapse(m, idx.size())) {
                Pass p; p.stages = idx; p.collapse = true;
                const auto& first = m.stages[idx[0]];
                p.key = {0, cull, 1, 1, m.all_add, !m.all_add && first.depth_write, first.depth_equal, m.polygon_offset};
                p.fog = scene::fog_mode(m.all_add, 1, 1, !m.all_add);
                p.slot = pass_slots++;
                gpu_materials[i].passes.push_back(p);
            } else {
                for(auto k : idx) {
                    const auto& st = m.stages[k];
                    Pass p; p.stages = {k};
                    const bool blend = st.has_blend && !(st.blend_src == 1 && st.blend_dst == 0);
                    p.key = {0, cull, st.blend_src, st.blend_dst, blend, st.depth_write, st.depth_equal, m.polygon_offset};
                    p.fog = scene::fog_mode(st.has_blend, st.blend_src, st.blend_dst, false);
                    p.slot = pass_slots++;
                    gpu_materials[i].passes.push_back(p);
                }
            }
        }
        // Uniform buffers
        frame_ubo = make_buffer(sizeof(gpu::FrameData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        const VkDeviceSize align = std::max<VkDeviceSize>(properties.limits.minUniformBufferOffsetAlignment, 16);
        draw_stride = (sizeof(gpu::DrawData) + align - 1) / align * align;
        draw_ubo = make_buffer(draw_stride * std::max(1U, pass_slots), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        // Descriptor writes
        VkDescriptorBufferInfo frame_info{frame_ubo.buffer, 0, sizeof(gpu::FrameData)};
        VkDescriptorBufferInfo draw_info{draw_ubo.buffer, 0, sizeof(gpu::DrawData)};
        VkDescriptorImageInfo box_image{sampler_clamp, box_tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo images[5] = {
            {sampler_repeat, world_tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, {sampler_clamp, lightmap_tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {sampler_repeat, mat_tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, {sampler_repeat, kit_tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {sampler_clamp, portal_tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet w[8]{};
        for(auto& x : w) x.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[7].dstSet = set0; w[7].dstBinding = 6; w[7].descriptorCount = 1; w[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[7].pImageInfo = &box_image;
        w[0].dstSet = set0; w[0].dstBinding = 0; w[0].descriptorCount = 1; w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w[0].pBufferInfo = &frame_info;
        for(std::uint32_t i = 0; i < 5; ++i) {
            w[i + 1].dstSet = set0; w[i + 1].dstBinding = i + 1; w[i + 1].descriptorCount = 1;
            w[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[i + 1].pImageInfo = &images[i];
        }
        w[6].dstSet = set1; w[6].dstBinding = 0; w[6].descriptorCount = 1; w[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; w[6].pBufferInfo = &draw_info;
        if(use_push_descriptor) std::swap(w[6], w[7]);   // set 1 is pushed per pass: write the six set-0 samplers + frame block only
        vkUpdateDescriptorSets(device, use_push_descriptor ? 7U : 8U, w, 0, nullptr);
        scene_loaded = true;
        if(pipelines_built) {   // targets already exist: build scene pipelines now
            if(triangle_pipeline) vkDestroyPipeline(device, triangle_pipeline, nullptr);
            triangle_pipeline = {};
            build_pipelines();
        }
        log("info", "renderer", "Scene " + s.id + " uploaded: " + std::to_string(s.stats.world_triangles) + " world, " +
                                    std::to_string(s.stats.material_triangles) + " material, " + std::to_string(s.stats.sky_triangles) + " sky triangles; " +
                                    std::to_string(materials.size()) + " materials, " + std::to_string(pass_slots) + " material passes, " +
                                    (pipelines_built ? std::to_string(material_pipelines.size()) + " material pipelines"
                                                     : std::string("pipelines are created with the first render target")) +
                                    "; textures: " + std::to_string(staging_texture_bytes) + " bytes via staging, " +
                                    std::to_string(host_copy_texture_bytes) + " bytes via host copy");
    }

    // ------------------------------------------------------------ per-frame state
    void write_frame(const FrameView& v) {
        gpu::FrameData f{};
        const auto& c = v.camera;
        auto put = [](float* dst, const scene::V3& a, double w) { dst[0] = float(a[0]); dst[1] = float(a[1]); dst[2] = float(a[2]); dst[3] = float(w); };
        put(f.cam_pos, c.position, v.time);
        put(f.cam_forward, c.forward, c.tan_half_x);
        put(f.cam_right, c.right, c.tan_half_y);
        put(f.cam_up, c.up, kLightScale);
        f.fog_color[0] = theme.fog[0]; f.fog_color[1] = theme.fog[1]; f.fog_color[2] = theme.fog[2]; f.fog_color[3] = kFogStrength;
        for(int i = 0; i < 4; ++i) last_fog[i] = f.fog_color[i];   // effects use the same theme fog (fxOpts)
        const std::size_t lights = v.features.dynamic_lights && v.lights ? std::min<std::size_t>(v.light_count, 16) : 0;
        f.dl[0] = static_cast<std::int32_t>(lights);
        for(std::size_t i = 0; i < lights; ++i) {
            const auto& l = v.lights[i];
            f.dl_pos[i][0] = l.x; f.dl_pos[i][1] = l.y; f.dl_pos[i][2] = l.z; f.dl_pos[i][3] = l.radius;
            f.dl_col[i][0] = l.r; f.dl_col[i][1] = l.g; f.dl_col[i][2] = l.b; f.dl_col[i][3] = 1;
        }
        stats.dynamic_lights = static_cast<std::uint32_t>(lights);
        f.params[0] = kLmIntensity; f.params[1] = kMapOverbright; f.params[2] = float(extent.width); f.params[3] = float(extent.height);
        f.sky_eye[0] = float(sky_center[0]); f.sky_eye[1] = float(sky_center[1]); f.sky_eye[2] = float(sky_eye_z);
        f.sky_box[0] = float(sky_rotate_deg * std::numbers::pi / 180 * v.time);   // drawSky: rot = skyRotate * pi/180 * time
        f.sky_box[1] = float(sky_fog); f.sky_box[2] = has_sky_box ? 1.0F : 0.0F;
        const auto& ft = v.features;
        f.flags[0] = (ft.lightmap ? 1 : 0) | (ft.fog ? 2 : 0) | (ft.posterize ? 4 : 0);
        f.flags[1] = ft.debug_view;
        std::memcpy(frame_ubo.mapped, &f, sizeof f);
    }
    void write_pass(const scene::Material& m, const scene::MaterialBinding& binding, const Pass& p, double time, bool sky, const RenderFeatures& ft, bool first_pass) {
        gpu::DrawData d{};
        d.h0[0] = static_cast<std::int32_t>(p.stages.size()); d.h0[1] = p.collapse ? 1 : 0; d.h0[2] = p.fog; d.h0[3] = m.autosprite;
        int deforms = 0;
        for(const auto& df : m.deforms) {
            if(deforms >= 3 || df.type == scene::DeformType::autosprite || df.type == scene::DeformType::autosprite2) continue;
            const auto o = static_cast<std::size_t>(deforms);
            d.d_type[o] = static_cast<std::int32_t>(df.type);
            auto wave = [&](float* a) { a[0] = float(df.wave.base); a[1] = float(df.wave.amp); a[2] = float(df.wave.phase); a[3] = float(df.wave.freq); };
            if(df.type == scene::DeformType::wave) { wave(d.d_a[o]); d.d_b[o][0] = float(df.spread); d.d_b[o][1] = float(df.wave.func); }
            else if(df.type == scene::DeformType::bulge) { d.d_a[o][0] = float(df.width); d.d_a[o][1] = float(df.height); d.d_a[o][2] = float(df.speed); }
            else if(df.type == scene::DeformType::move) { wave(d.d_a[o]); d.d_b[o][0] = float(df.vec[0]); d.d_b[o][1] = float(df.vec[1]); d.d_b[o][2] = float(df.vec[2]); d.d_b[o][3] = float(df.wave.func); }
            else if(df.type == scene::DeformType::normal) { d.d_a[o][0] = float(df.amp); d.d_a[o][1] = float(df.freq); }
            ++deforms;
        }
        d.h1[0] = deforms; d.h1[1] = m.vertex_lit ? 1 : 0; d.h1[2] = sky ? 1 : 0;
        d.h1[3] = m.lightmapped && !m.translucent && first_pass ? 1 : 0;   // drawMaterial: dl = lightmapped && !translucent; "Q3 adds dlights once, after the base pass"
        d.skyp[0] = float(m.sky_rotate * std::numbers::pi / 180 * time);
        d.skyp[1] = float(m.sky.cloud_height);
        d.skyp[2] = float(std::fmod(time, 1000.0));
        for(std::size_t k = 0; k < p.stages.size(); ++k) {
            const auto si = p.stages[k];
            const auto& st = m.stages[si];
            auto& o = d.st[k];
            auto src = static_cast<std::int32_t>(st.source);
            if(ft.debug_view == 2 && (st.source == scene::Source::lightmap || st.source == scene::Source::vertex_light)) src = 2;   // r_fullbright
            o.a[0] = src; o.a[1] = static_cast<std::int32_t>(st.tcgen); o.a[2] = scene::rgb_code(st.rgb); o.a[3] = scene::alpha_code(st.alpha);
            o.b[0] = st.has_blend ? st.blend_src : 1; o.b[1] = st.has_blend ? st.blend_dst : 0;
            o.b[2] = static_cast<std::int32_t>(st.alpha_func); o.b[3] = st.clamp ? 1 : 0;
            const auto c = scene::stage_color(st, time);
            for(int j = 0; j < 4; ++j) o.cst[j] = float(c[std::size_t(j)]);
            const auto tex = scene::stage_texture(st, time);
            auto mat = [](float* s, float* t, const scene::TexMatrix& x) {
                s[0] = float(x.a); s[1] = float(x.b); s[2] = float(x.e); t[0] = float(x.c); t[1] = float(x.d); t[2] = float(x.f);
            };
            mat(o.pre_s, o.pre_t, tex.pre);
            mat(o.post_s, o.post_t, tex.post);
            if(tex.turb) { o.turb[0] = float(tex.turb_amp); o.turb[1] = float(tex.turb_phase); o.turb[2] = float(tex.turb_freq); o.turb[3] = 1; }
            for(int j = 0; j < 3; ++j) { o.tc_s[j] = float(st.tc_s[std::size_t(j)]); o.tc_t[j] = float(st.tc_t[std::size_t(j)]); }
            const auto& frames = binding.stage_layers.at(si);
            std::uint32_t layer = frames.empty() ? 0 : frames[0];
            if(frames.size() > 1 && st.anim_freq > 0)   // RB_BindAnimatedImage
                layer = frames[static_cast<std::size_t>(std::floor(time * st.anim_freq)) % frames.size()];
            o.misc[0] = float(layer);
            o.misc[1] = st.alpha == scene::AlphaGen::portal ? float(st.portal_range / 32.0) : 1.0F;
        }
        std::memcpy(static_cast<std::uint8_t*>(draw_ubo.mapped) + draw_stride * p.slot, &d, sizeof d);
    }

    void draw_material(VkCommandBuffer cb, std::size_t mi, const std::vector<std::pair<std::uint32_t, std::uint32_t>>& ranges) {
        for(const auto& p : gpu_materials[mi].passes) {
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeline);
            if(use_dynamic_state) {   // same values the baseline bakes into the pipeline (material_pipeline)
                fn.set_cull_mode(cb, cull_flags(p.key.cull));
                fn.set_depth_write_enable(cb, p.key.depth_write ? VK_TRUE : VK_FALSE);
                fn.set_depth_compare_op(cb, p.key.depth_equal ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL);
            }
            if(use_push_descriptor) {
                VkDescriptorBufferInfo range{draw_ubo.buffer, draw_stride * p.slot, sizeof(gpu::DrawData)};
                VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstBinding = 0; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo = &range;
                fn.push_descriptor_set(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_layout, 1, 1, &write);
            } else {
                const auto offset = static_cast<std::uint32_t>(draw_stride * p.slot);
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_layout, 1, 1, &set1, 1, &offset);
            }
            ++stats.material_passes;
            for(const auto& [first, count] : ranges) { vkCmdDrawIndexed(cb, count, 1, first, 0, 0); ++stats.draw_calls; }
        }
    }

    void record_scene(VkCommandBuffer cb, const FrameView& v) {
        const auto& ft = v.features;
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_layout, 0, 1, &set0, 0, nullptr);
        const VkDeviceSize zero = 0;
        if(ft.sky && !has_sky_box) {   // "$baked" sky: the authored sky geometry from the bake eye
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sky_pipeline);
            vkCmdBindVertexBuffers(cb, 0, 1, &sky_vb.buffer, &zero);
            vkCmdBindIndexBuffer(cb, sky_ib.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, sky_indices, 1, 0, 0, 0); ++stats.draw_calls;
            VkClearAttachment clear{VK_IMAGE_ASPECT_DEPTH_BIT, 0, {}};
            clear.clearValue.depthStencil = {1.0F, 0};
            VkClearRect rect{{{0, 0}, extent}, 0, 1};
            vkCmdClearAttachments(cb, 1, &clear, 1, &rect);
        }
        if(ft.world) {
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, world_pipeline);
            vkCmdBindVertexBuffers(cb, 0, 1, &world_vb.buffer, &zero);
            vkCmdBindIndexBuffer(cb, world_ib.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, world_indices, 1, 0, 0, 0); ++stats.draw_calls;
        }
        // Visible static clusters (legacy q3ClusterVisibleMain without PVS: inView only).
        std::vector<std::uint32_t> visible;
        for(std::uint32_t i = 0; i < clusters.size(); ++i) {
            const auto& c = clusters[i];
            if(!ft.culling || scene::in_view(v.camera, c.center[0], c.center[1], c.center[2], c.radius)) visible.push_back(i);
            else ++stats.culled_clusters;
        }
        stats.visible_clusters = static_cast<std::uint32_t>(visible.size());
        if(ft.materials) {
            vkCmdBindVertexBuffers(cb, 0, 1, &mat_vb.buffer, &zero);
            vkCmdBindIndexBuffer(cb, mat_ib.buffer, 0, VK_INDEX_TYPE_UINT32);
            // Portal surfaces first (Q3PORTAL.drawSurfaces), each cluster separately.
            for(auto i : visible) if(materials[clusters[i].material].portal) draw_material(cb, clusters[i].material, {{clusters[i].first, clusters[i].count}});
            // Opaque: consecutive clusters of one material share state; contiguous index runs merge.
            for(std::size_t k = 0; k < visible.size();) {
                const auto& c = clusters[visible[k]];
                const auto& m = materials[c.material];
                if(m.portal || m.translucent) { ++k; continue; }
                std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
                std::size_t end = k;
                while(end < visible.size() && clusters[visible[end]].material == c.material) {
                    const auto& e = clusters[visible[end]];
                    if(!ranges.empty() && ranges.back().first + ranges.back().second == e.first) ranges.back().second += e.count;
                    else ranges.emplace_back(e.first, e.count);
                    ++end;
                }
                draw_material(cb, c.material, ranges);
                k = end;
            }
        }
        if(ft.sky && has_sky_box) {   // Q3MAT.drawSky: image box behind everything (LEQUAL, no depth write), then the cloud stages
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, skybox_pipeline);
            vkCmdDraw(cb, 3, 1, 0, 0); ++stats.draw_calls;
        }
        if(ft.clouds && ft.sky) {   // Q3MAT.drawSky cloud stages on the dome, depth LEQUAL at the far plane
            vkCmdBindVertexBuffers(cb, 0, 1, &cloud_vb.buffer, &zero);
            vkCmdBindIndexBuffer(cb, cloud_ib.buffer, 0, VK_INDEX_TYPE_UINT32);
            draw_material(cb, sky_material, {{0, cloud_indices}});
        }
        if(ft.materials && ft.translucent) {   // translucentOrder: sort ascending, then farthest first
            std::vector<std::pair<double, std::uint32_t>> order;
            for(auto i : visible) {
                const auto& c = clusters[i];
                if(!materials[c.material].translucent || materials[c.material].portal) continue;
                const double dist = std::hypot(c.center[0] - v.camera.position[0], c.center[1] - v.camera.position[1], c.center[2] - v.camera.position[2]);
                order.emplace_back(dist, i);
            }
            std::stable_sort(order.begin(), order.end(), [&](const auto& a, const auto& b) {
                const int sa = materials[clusters[a.second].material].sort, sb = materials[clusters[b.second].material].sort;
                return sa != sb ? sa < sb : a.first > b.first;
            });
            vkCmdBindVertexBuffers(cb, 0, 1, &mat_vb.buffer, &zero);
            vkCmdBindIndexBuffer(cb, mat_ib.buffer, 0, VK_INDEX_TYPE_UINT32);
            for(const auto& [dist, i] : order) draw_material(cb, clusters[i].material, {{clusters[i].first, clusters[i].count}});
        }
    }

    void frame(const FrameView* view, const Overlay* overlay = nullptr) {
        if(dirty && !recreate()) return;
        check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "wait frame");
        std::uint32_t index = 0;
        bool suboptimal = false;
        if(!offscreen) {
            auto result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index);
            if(result == VK_ERROR_OUT_OF_DATE_KHR) { dirty = true; return; }
            if(result != VK_SUBOPTIMAL_KHR) check(result, "acquire");
            suboptimal = result == VK_SUBOPTIMAL_KHR;
        }
        stats = {};
        const bool scene_frame = view && scene_loaded;
        if(scene_frame) {
            write_frame(*view);
            for(std::size_t i = 0; i < materials.size(); ++i)
                for(const auto& p : gpu_materials[i].passes) write_pass(materials[i], bindings[i], p, view->time, i == sky_material, view->features, &p == &gpu_materials[i].passes.front());
        }
        check(vkResetCommandBuffer(command, 0), "reset command");
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command, &bi), "begin command");
        VkClearValue clears[2]{};
        if(scene_frame) clears[0].color = {{theme.sky[0], theme.sky[1], theme.sky[2], 1.F}};   // gl.clearColor(...t.sky,1)
        else clears[0].color = {{.015F, .015F, .02F, 1.F}};
        clears[1].depthStencil = {1.0F, 0};
        const VkImage target = offscreen ? color_target.image : use_dynamic_rendering ? swap_images.at(index) : VkImage{};
        if(use_dynamic_rendering) {
            // Optional path: no render pass / framebuffer objects. The layout transitions the baseline
            // render pass performs implicitly (initialLayout UNDEFINED -> attachment layouts) are explicit here.
            VkImageAspectFlags depth_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            if(has_stencil(depth_format)) depth_aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;   // combined formats transition both aspects
            image_barrier(command, target, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            image_barrier(command, depth_target.image, depth_aspect, 1, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 0,
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
            VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}, depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            color.imageView = offscreen ? color_target.view : views.at(index); color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; color.storeOp = VK_ATTACHMENT_STORE_OP_STORE; color.clearValue = clears[0];
            depth.imageView = depth_target.view; depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; depth.clearValue = clears[1];
            VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
            ri.renderArea.extent = extent; ri.layerCount = 1; ri.colorAttachmentCount = 1; ri.pColorAttachments = &color; ri.pDepthAttachment = &depth;
            fn.begin_rendering(command, &ri);
        } else {
            VkRenderPassBeginInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            ri.renderPass = pass; ri.framebuffer = framebuffers.at(offscreen ? 0 : index); ri.renderArea.extent = extent; ri.clearValueCount = 2; ri.pClearValues = clears;
            vkCmdBeginRenderPass(command, &ri, VK_SUBPASS_CONTENTS_INLINE);
        }
        VkViewport vp{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
        VkRect2D rect{{0, 0}, extent};
        vkCmdSetViewport(command, 0, 1, &vp);
        vkCmdSetScissor(command, 0, 1, &rect);
        if(scene_frame) { record_scene(command, *view); if(overlay && overlay_ready) record_overlay(command, *overlay); }
        else { vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, triangle_pipeline); vkCmdDraw(command, 3, 1, 0, 0); stats.draw_calls = 1; }
        if(use_dynamic_rendering) {
            fn.end_rendering(command);
            // Same final layouts the baseline render pass declares as finalLayout.
            if(offscreen) image_barrier(command, target, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            else image_barrier(command, target, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0);
        } else vkCmdEndRenderPass(command);
        if(offscreen) {
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {extent.width, extent.height, 1};
            vkCmdCopyImageToBuffer(command, color_target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
            buffer_barrier(command, readback.buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
        }
        check(vkEndCommandBuffer(command), "end command");
        // Reset only after successful acquire/record: out-of-date must not strand an unsignaled fence.
        check(vkResetFences(device, 1, &fence), "reset fence");
        if(offscreen) submit(command, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, fence, "submit");
        else submit(command, acquired, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, presented.at(index), fence, "submit");
        if(offscreen) { check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "offscreen frame"); have_capture = true; return; }
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1; present.pWaitSemaphores = &presented.at(index); present.swapchainCount = 1; present.pSwapchains = &swapchain; present.pImageIndices = &index;
        auto result = vkQueuePresentKHR(queue, &present);
        if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || suboptimal) dirty = true;
        if(result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR) check(result, "present");
    }
};

Renderer::Renderer(SDL_Window* window, const Settings& settings, std::filesystem::path shader_dir, const vk::Request& request)
    : impl_(std::make_unique<Impl>(window, settings, std::move(shader_dir), false, request)) {
    if(!window) throw std::runtime_error("Renderer: null window (use the offscreen constructor)");
    impl_->init();
}
Renderer::Renderer(std::uint32_t width, std::uint32_t height, const Settings& settings, std::filesystem::path shader_dir, const vk::Request& request)
    : impl_(std::make_unique<Impl>(nullptr, settings, std::move(shader_dir), true, request)) {
    if(width < 16 || height < 16 || width > 16384 || height > 16384) throw std::runtime_error("Renderer: invalid offscreen size");
    impl_->extent = {width, height};
    impl_->init();
    impl_->recreate();
}
Renderer::~Renderer() = default;
void Renderer::resize() { impl_->dirty = true; }
void Renderer::load_scene(const scene::MapScene& s) { impl_->load(s); }
void Renderer::draw(const Snapshot&) { impl_->frame(nullptr); }
void Renderer::draw(const FrameView& v) { impl_->frame(&v); }
void Renderer::draw(const FrameView& v, const Overlay& o) { impl_->frame(&v, &o); }
void Renderer::load_overlay(const OverlayAssets& a) { impl_->load_overlay(a); }
void Renderer::load_enemies(const EnemyAssets& a) { impl_->load_enemies(a); }
bool Renderer::enemies_loaded() const { return impl_->enemies_ready; }
void Renderer::set_light_volume(const scene::Image& atlas) { impl_->set_light_volume(atlas); }
bool Renderer::overlay_loaded() const { return impl_->overlay_ready; }
scene::Image Renderer::capture() {
    if(!impl_->offscreen) throw std::runtime_error("capture() requires the offscreen renderer");
    if(!impl_->have_capture) throw std::runtime_error("capture() before any frame was rendered");
    scene::Image img{impl_->extent.width, impl_->extent.height, {}};
    const auto* p = static_cast<const std::uint8_t*>(impl_->readback.mapped);
    img.rgba.assign(p, p + std::size_t(img.width) * img.height * 4U);   // R8G8B8A8, row 0 = top
    return img;
}
std::pair<std::uint32_t, std::uint32_t> Renderer::extent() const { return {impl_->extent.width, impl_->extent.height}; }
RenderStats Renderer::stats() const { return impl_->stats; }
DeviceReport Renderer::report() const {
    const auto& p = impl_->properties;
    DeviceReport r;
    r.device = p.deviceName;
    r.device_type = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "discrete" : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? "integrated"
                  : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? "virtual" : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? "cpu" : "other";
    r.driver = std::to_string(p.driverVersion);
    r.api = version_string(p.apiVersion); r.loader_api = version_string(impl_->loader_api);
    r.color_format = format_name(impl_->format); r.depth_format = format_name(impl_->depth_format);
    r.present_mode = impl_->offscreen ? "offscreen" : present_name(impl_->present_mode);
    r.validation = impl_->validation; r.anisotropy = impl_->anisotropy; r.max_anisotropy = impl_->max_anisotropy;
    r.pipelines = static_cast<std::uint32_t>(impl_->material_pipelines.size()) + (impl_->sky_pipeline ? 2U : 0U) + (impl_->skybox_pipeline ? 1U : 0U) + (impl_->triangle_pipeline ? 1U : 0U) + (impl_->fx_pipeline ? 6U : 0U);
    r.driver_name = impl_->driver_name; r.driver_info = impl_->driver_info;
    r.negotiated_api = vk::api_string(impl_->path.api, false); r.instance_api = vk::api_string(impl_->instance_api, false);
    r.caps = impl_->caps; r.path = impl_->path; r.enabled_extensions = impl_->enabled_extensions;
    r.render_passes = impl_->pass ? 1U : 0U; r.framebuffers = static_cast<std::uint32_t>(impl_->framebuffers.size());
    r.material_pipelines = static_cast<std::uint32_t>(impl_->material_pipelines.size());
    r.descriptor_sets = (impl_->set0 ? 1U : 0U) + (impl_->set1 ? 1U : 0U);
    r.staging_texture_bytes = impl_->staging_texture_bytes; r.host_copy_texture_bytes = impl_->host_copy_texture_bytes;
    return r;
}
std::uint32_t Renderer::validation_warnings() const { return impl_->counters.warnings.load(); }
std::uint32_t Renderer::validation_errors() const { return impl_->counters.errors.load(); }
}  // namespace cat
