#pragma once
// N2.1 Vulkan capability model and render-path selection. No Vulkan headers, no GPU: the renderer
// fills DeviceCaps from the running device, and select_path() decides which optional features are
// used. Everything here is unit-tested with synthetic devices (tests/vkcaps_tests.cpp).
//
// Policy
//   * Vulkan 1.2 is the mandatory device baseline. A 1.2 device with no optional extension renders
//     everything (render passes + framebuffers, vkCmdPipelineBarrier/vkQueueSubmit, static pipeline
//     state, dynamic-offset uniform buffers, staging-buffer texture uploads).
//   * Each optional feature is selected independently, highest path first:
//       core      - promoted into the negotiated API version AND its feature bit is reported
//       extension - the KHR/EXT extension (and its dependencies) is advertised AND its bit is reported
//       unavailable -> the 1.2 fallback is used
//   * An API version alone never enables a feature: the feature bit (or, for bit-less extensions,
//     the extension itself) must be reported, and the entry points must resolve at run time.
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cat::vk {
constexpr std::uint32_t make_api(std::uint32_t major, std::uint32_t minor, std::uint32_t patch = 0) {
    return (major << 22U) | (minor << 12U) | patch;
}
constexpr std::uint32_t api_major(std::uint32_t v) { return (v >> 22U) & 0x7FU; }
constexpr std::uint32_t api_minor(std::uint32_t v) { return (v >> 12U) & 0x3FFU; }
constexpr std::uint32_t api_patch(std::uint32_t v) { return v & 0xFFFU; }
constexpr std::uint32_t kApi11 = make_api(1, 1), kApi12 = make_api(1, 2), kApi13 = make_api(1, 3), kApi14 = make_api(1, 4);
constexpr std::uint32_t kBaselineApi = kApi12;   // minimum device version
constexpr std::uint32_t kHighestApi = kApi14;    // highest version the engine has code paths for
std::string api_string(std::uint32_t v, bool with_patch = true);
std::uint32_t parse_api(std::string_view text);   // "1.2", "1.3", "1.4"; throws on anything else

enum class Feature : std::uint8_t {
    dynamic_rendering,        // 1.3 core / VK_KHR_dynamic_rendering      -> fallback: VkRenderPass + framebuffers
    synchronization2,         // 1.3 core / VK_KHR_synchronization2       -> fallback: vkCmdPipelineBarrier, vkQueueSubmit
    extended_dynamic_state,   // 1.3 core / VK_EXT_extended_dynamic_state -> fallback: one static pipeline per state
    push_descriptor,          // 1.4 core / VK_KHR_push_descriptor        -> fallback: dynamic-offset descriptor set
    host_image_copy,          // 1.4 optional / VK_EXT_host_image_copy    -> fallback: staging buffer + copy command
    count
};
constexpr std::size_t kFeatureCount = static_cast<std::size_t>(Feature::count);
enum class Source : std::uint8_t { unavailable, core, extension };

struct FeatureInfo {
    Feature id;
    const char* name;                      // CLI / report name
    const char* extension;                 // providing extension
    std::uint32_t promoted;                // core since this version
    bool core_has_bit;                     // false: mandatory in core, no feature bit exists (1.3 extended dynamic state)
    bool extension_has_bit;                // false: extension presence is the capability (VK_KHR_push_descriptor)
    std::array<const char*, 2> requires_below_13;   // extra extensions needed when the device API is < 1.3
    const char* fallback;                  // what the 1.2 baseline does instead
};
const std::array<FeatureInfo, kFeatureCount>& feature_table();
const FeatureInfo& info(Feature f);
Feature parse_feature(std::string_view name);   // throws on unknown names
const char* source_name(Source s);

// What the running device reports. Filled by the renderer; synthetic in unit tests.
struct DeviceCaps {
    std::string name;
    std::uint32_t loader_api{};            // vkEnumerateInstanceVersion
    std::uint32_t device_api{};            // VkPhysicalDeviceProperties::apiVersion
    std::vector<std::string> extensions;   // vkEnumerateDeviceExtensionProperties
    // Feature bits as reported through the aggregated core structs (VkPhysicalDeviceVulkan13/14Features).
    // Only meaningful when the negotiated API reaches the promotion version; the renderer leaves them false otherwise.
    std::array<bool, kFeatureCount> core_bit{};
    // Feature bits as reported through the extension structs (only queried when the extension is advertised).
    std::array<bool, kFeatureCount> extension_bit{};
    // Host image copy is only useful when all three also hold for the R8G8B8A8_UNORM sampled textures:
    bool host_copy_format{};    // VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT for optimal tiling
    bool host_copy_layout{};    // SHADER_READ_ONLY_OPTIMAL listed in pCopyDstLayouts
    bool host_copy_optimal{};   // VkHostImageCopyDevicePerformanceQuery::optimalDeviceAccess (no sampling penalty)
    bool has_extension(std::string_view ext) const;
};

// What the application allows. Default: everything the device can do, up to Vulkan 1.4.
struct Request {
    std::uint32_t max_api{kHighestApi};          // caps the negotiated API (and VkApplicationInfo::apiVersion)
    bool allow_extensions{true};                 // false: only core-promoted features
    std::array<bool, kFeatureCount> disabled{};  // per-feature opt-out
    static Request baseline();                   // strict Vulkan 1.2 path: max_api 1.2, no optional extension
    Request& disable(Feature f) { disabled[static_cast<std::size_t>(f)] = true; return *this; }
};

// The decision. `api` is min(device, request.max_api): the version whose core commands may be used.
struct Path {
    std::uint32_t api{};
    std::array<Source, kFeatureCount> source{};
    std::vector<std::string> extensions;   // device extensions to enable for the selected features
    std::vector<std::string> notes;        // one line per feature: why it was (not) selected
    Source operator[](Feature f) const { return source[static_cast<std::size_t>(f)]; }
    bool uses(Feature f) const { return (*this)[f] != Source::unavailable; }
    bool is_baseline() const;              // no optional feature in use
    std::string tier() const;              // "vulkan-1.2-baseline", "vulkan-1.2+extensions", "vulkan-1.3", "vulkan-1.4"
    std::string describe() const;          // tier + per-feature sources, one line
    // Renderer: a selected feature turned out unusable (missing entry point, ...). Falls back for that feature only.
    void drop(Feature f, const std::string& reason);
};

// True when the device meets the mandatory baseline (device API >= 1.2 and loader >= 1.1).
bool meets_baseline(const DeviceCaps& caps, std::string* why = nullptr);
// The API version whose core functionality may be used with this device and request.
std::uint32_t negotiated_api(const DeviceCaps& caps, const Request& request);
// Throws std::runtime_error when the device does not meet the baseline or the request is invalid.
Path select_path(const DeviceCaps& caps, const Request& request = {});
}  // namespace cat::vk
