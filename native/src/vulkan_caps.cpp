// N2.1 capability model: see cat/vulkan_caps.hpp. GPU-free by design.
#include "cat/vulkan_caps.hpp"
#include <algorithm>
#include <stdexcept>

namespace cat::vk {
namespace {
constexpr std::array<FeatureInfo, kFeatureCount> kTable{{
    {Feature::dynamic_rendering, "dynamic_rendering", "VK_KHR_dynamic_rendering", kApi13, true, true, {nullptr, nullptr},
     "VkRenderPass + one framebuffer per target image"},
    {Feature::synchronization2, "synchronization2", "VK_KHR_synchronization2", kApi13, true, true, {nullptr, nullptr},
     "vkCmdPipelineBarrier + vkQueueSubmit"},
    {Feature::extended_dynamic_state, "extended_dynamic_state", "VK_EXT_extended_dynamic_state", kApi13, false, true, {nullptr, nullptr},
     "one static pipeline per cull/depth-write/depth-compare combination"},
    {Feature::push_descriptor, "push_descriptor", "VK_KHR_push_descriptor", kApi14, true, false, {nullptr, nullptr},
     "allocated descriptor set with a dynamic uniform-buffer offset"},
    {Feature::host_image_copy, "host_image_copy", "VK_EXT_host_image_copy", kApi14, true, true,
     {"VK_KHR_copy_commands2", "VK_KHR_format_feature_flags2"}, "staging buffer + vkCmdCopyBufferToImage"},
}};
std::size_t index(Feature f) {
    const auto i = static_cast<std::size_t>(f);
    if(i >= kFeatureCount) throw std::runtime_error("invalid Vulkan feature id");
    return i;
}
}  // namespace

std::string api_string(std::uint32_t v, bool with_patch) {
    auto s = std::to_string(api_major(v)) + "." + std::to_string(api_minor(v));
    if(with_patch) s += "." + std::to_string(api_patch(v));
    return s;
}
std::uint32_t parse_api(std::string_view text) {
    if(text == "1.1") return kApi11;
    if(text == "1.2") return kApi12;
    if(text == "1.3") return kApi13;
    if(text == "1.4") return kApi14;
    throw std::runtime_error("Vulkan API cap must be 1.1, 1.2, 1.3 or 1.4 (got '" + std::string(text) + "')");
}
const std::array<FeatureInfo, kFeatureCount>& feature_table() { return kTable; }
const FeatureInfo& info(Feature f) { return kTable[index(f)]; }
Feature parse_feature(std::string_view name) {
    for(const auto& f : kTable) if(name == f.name) return f.id;
    std::string known;
    for(const auto& f : kTable) known += std::string(known.empty() ? "" : ", ") + f.name;
    throw std::runtime_error("Unknown Vulkan feature '" + std::string(name) + "' (known: " + known + ")");
}
const char* source_name(Source s) { return s == Source::core ? "core" : s == Source::extension ? "extension" : "unavailable"; }

bool DeviceCaps::has_extension(std::string_view ext) const {
    return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}
Request Request::baseline() {
    Request r;
    r.max_api = kBaselineApi;
    r.allow_extensions = false;
    return r;
}

bool Path::is_baseline() const {
    return std::all_of(source.begin(), source.end(), [](Source s) { return s == Source::unavailable; });
}
std::string Path::tier() const {
    if(is_baseline()) return "vulkan-" + api_string(std::min(api, kHighestApi), false) + (api >= kApi13 ? "-baseline-fallbacks" : "-baseline");
    if(api >= kApi14) return "vulkan-1.4";
    if(api >= kApi13) return "vulkan-1.3";
    return "vulkan-1.2+extensions";
}
std::string Path::describe() const {
    std::string s = tier() + " [";
    for(const auto& f : kTable) s += std::string(f.id == Feature::dynamic_rendering ? "" : " ") + f.name + "=" + source_name((*this)[f.id]);
    return s + "]";
}
void Path::drop(Feature f, const std::string& reason) {
    const auto i = index(f);
    if(source[i] == Source::unavailable) return;
    source[i] = Source::unavailable;
    notes.push_back(std::string(info(f).name) + ": dropped at run time (" + reason + "); fallback: " + info(f).fallback);
}

bool meets_baseline(const DeviceCaps& caps, std::string* why) {
    if(caps.loader_api < kApi11) {
        if(why) *why = "Vulkan loader " + api_string(caps.loader_api) + " is older than 1.1";
        return false;
    }
    if(caps.device_api < kBaselineApi) {
        if(why) *why = "device API " + api_string(caps.device_api) + " is older than the Vulkan 1.1 baseline";
        return false;
    }
    return true;
}
std::uint32_t negotiated_api(const DeviceCaps& caps, const Request& request) {
    // The patch number is irrelevant for feature selection; compare major.minor only.
    const auto cap = [](std::uint32_t v) { return make_api(api_major(v), api_minor(v)); };
    return std::min({cap(caps.device_api), cap(request.max_api), kHighestApi});
}

Path select_path(const DeviceCaps& caps, const Request& request) {
    std::string why;
    if(!meets_baseline(caps, &why)) throw std::runtime_error("No Vulkan 1.1 capable device: " + why);
    if(request.max_api < kBaselineApi) throw std::runtime_error("Requested API cap " + api_string(request.max_api, false) + " is below the Vulkan 1.1 baseline");
    Path path;
    path.api = negotiated_api(caps, request);
    auto enable = [&](const char* ext) {
        if(std::find(path.extensions.begin(), path.extensions.end(), ext) == path.extensions.end()) path.extensions.emplace_back(ext);
    };
    for(const auto& f : kTable) {
        const auto i = index(f.id);
        const std::string name = f.name;
        auto reject = [&](const std::string& reason) { path.notes.push_back(name + ": unavailable (" + reason + "); fallback: " + f.fallback); };
        if(path.api < kApi12) { reject("Vulkan 1.1 uses the baseline path"); continue; }
        if(request.disabled[i]) { reject("disabled by request"); continue; }
        Source chosen = Source::unavailable;
        std::string reason;
        if(path.api >= f.promoted) {
            // Promoted to core at the negotiated version. Core status does not imply support:
            // the feature bit decides (extended dynamic state is the one bit-less, mandatory case).
            if(!f.core_has_bit || caps.core_bit[i]) chosen = Source::core;
            else reason = "core in " + api_string(f.promoted, false) + " but the device does not report the feature bit";
        } else if(!request.allow_extensions) {
            reason = "needs " + api_string(f.promoted, false) + " core (negotiated " + api_string(path.api, false) + ") and extensions are not allowed";
        } else if(!caps.has_extension(f.extension)) {
            reason = "needs " + api_string(f.promoted, false) + " core (negotiated " + api_string(path.api, false) + ") or " + f.extension + ", which the device does not advertise";
        } else if(f.extension_has_bit && !caps.extension_bit[i]) {
            reason = std::string(f.extension) + " is advertised but its feature bit is not reported";
        } else {
            bool deps = true;
            if(path.api < kApi13)
                for(const char* dep : f.requires_below_13)
                    if(dep && !caps.has_extension(dep)) { deps = false; reason = std::string(f.extension) + " needs " + dep + ", which the device does not advertise"; }
            if(deps) chosen = Source::extension;
        }
        if(chosen != Source::unavailable && f.id == Feature::host_image_copy) {
            // Supported is not the same as useful: the sampled-texture format must accept host copies
            // in the layout we upload into, without a sampling penalty.
            if(!caps.host_copy_format) { chosen = Source::unavailable; reason = "R8G8B8A8_UNORM lacks HOST_IMAGE_TRANSFER support"; }
            else if(!caps.host_copy_layout) { chosen = Source::unavailable; reason = "SHADER_READ_ONLY_OPTIMAL is not a host-copy destination layout"; }
            else if(!caps.host_copy_optimal) { chosen = Source::unavailable; reason = "host-transfer images would sample slower on this device (optimalDeviceAccess = false)"; }
        }
        path.source[i] = chosen;
        if(chosen == Source::unavailable) { reject(reason); continue; }
        if(chosen == Source::extension) {
            enable(f.extension);
            if(path.api < kApi13) for(const char* dep : f.requires_below_13) if(dep) enable(dep);
            path.notes.push_back(name + ": extension " + f.extension);
        } else {
            path.notes.push_back(name + ": core " + api_string(f.promoted, false) + (f.core_has_bit ? " (feature bit reported)" : " (mandatory, no feature bit)"));
        }
    }
    return path;
}
}  // namespace cat::vk
