// VK_LAYER_CAT_device_sim - a TEST-ONLY Vulkan layer that makes the installed driver look like an
// older / less capable device, so the Vulkan 1.2 baseline path can be exercised on a machine whose
// only driver is newer (this project's CI device is a Vulkan 1.4 software rasterizer).
//
// It is never shipped with the game and never loaded unless a test enables it
// (VK_INSTANCE_LAYERS=VK_LAYER_CAT_device_sim). With no CAT_VKSIM_* variable set it passes everything through.
//
// Configuration is read from the environment at every vkCreateInstance:
//   CAT_VKSIM_API=1.2            VkPhysicalDeviceProperties::apiVersion is clamped to this version
//   CAT_VKSIM_HIDE_EXT=a,b | *   device extensions removed from enumeration ("*": all except VK_KHR_swapchain)
//   CAT_VKSIM_NO_FEATURES=a,b    feature bits forced to false in both the aggregated core structs and
//                                the extension structs: dynamicRendering, synchronization2,
//                                extendedDynamicState, pushDescriptor, hostImageCopy
//   CAT_VKSIM_HIDE_COMMANDS=a,b  vkGetDeviceProcAddr returns NULL for these commands although the
//                                device reports the feature (a broken/incomplete driver or loader)
// What the simulated device then does, like a real one of that version:
//   * reports the clamped version, the filtered extension list and the masked feature bits;
//   * zeroes VkPhysicalDeviceVulkan13Features / 14Features entirely when the simulated version is older;
//   * vkCreateDevice FAILS (VK_ERROR_EXTENSION_NOT_PRESENT / VK_ERROR_FEATURE_NOT_PRESENT) if the
//     application enables a hidden extension or a masked feature, so over-requesting cannot go unnoticed;
//   * vkGetDeviceProcAddr returns NULL for every core command newer than the simulated version and for
//     every command of an extension that is hidden or was not enabled (table generated from vk.xml).
// Limits: the driver underneath is still the real one. The layer proves that the engine neither
// requests nor resolves anything the simulated device lacks; it cannot reproduce old-driver bugs.
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
struct Command { const char* name; int core_minor; const char* extension; };
const Command kCommands[] = {
#include "vk_command_table.inc"
};
struct Config {
    std::uint32_t api{};                 // 0: not simulated
    bool hide_all{};
    std::set<std::string> hidden, no_features, no_commands;
    bool active() const { return api || hide_all || !hidden.empty() || !no_features.empty() || !no_commands.empty(); }
    bool hides(const char* ext) const { return (hide_all && std::strcmp(ext, VK_KHR_SWAPCHAIN_EXTENSION_NAME) != 0) || hidden.count(ext) != 0; }
    bool masks(const char* feature) const { return no_features.count(feature) != 0; }
    std::uint32_t minor() const { return api ? VK_API_VERSION_MINOR(api) : 99U; }
};
std::set<std::string> split(const char* text) {
    std::set<std::string> out;
    if(!text) return out;
    std::stringstream in{std::string(text)};
    std::string item;
    while(std::getline(in, item, ',')) if(!item.empty()) out.insert(item);
    return out;
}
Config read_config() {
    Config c;
    if(const char* api = std::getenv("CAT_VKSIM_API")) {
        unsigned major = 0, minor = 0;
        if(std::sscanf(api, "%u.%u", &major, &minor) == 2) c.api = VK_MAKE_API_VERSION(0, major, minor, 0);
    }
    c.hidden = split(std::getenv("CAT_VKSIM_HIDE_EXT"));
    c.hide_all = c.hidden.erase("*") != 0;
    c.no_features = split(std::getenv("CAT_VKSIM_NO_FEATURES"));
    c.no_commands = split(std::getenv("CAT_VKSIM_HIDE_COMMANDS"));
    return c;
}
struct InstanceData {
    VkInstance instance{};
    Config config;
    PFN_vkGetInstanceProcAddr gipa{};
    PFN_vkDestroyInstance destroy{};
    PFN_vkGetPhysicalDeviceProperties properties{};
    PFN_vkGetPhysicalDeviceProperties2 properties2{};
    PFN_vkGetPhysicalDeviceFeatures2 features2{};
    PFN_vkEnumerateDeviceExtensionProperties extensions{};
};
struct DeviceData {
    Config config;
    PFN_vkGetDeviceProcAddr gdpa{};
    PFN_vkDestroyDevice destroy{};
    std::set<std::string> enabled;
};
std::mutex g_mutex;
std::unordered_map<void*, InstanceData> g_instances;   // keyed by loader dispatch pointer
std::unordered_map<void*, DeviceData> g_devices;
void* key(const void* handle) { return *static_cast<void* const*>(handle); }
InstanceData instance_of(const void* handle) {
    std::lock_guard lock(g_mutex);
    return g_instances.at(key(handle));
}

// Feature structs are plain {sType, pNext, VkBool32...}: mask in place.
void mask_features(const Config& c, VkBaseOutStructure* s) {
    for(; s; s = s->pNext) {
        switch(static_cast<int>(s->sType)) {
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES: {
                auto* f = reinterpret_cast<VkPhysicalDeviceVulkan13Features*>(s);
                if(c.minor() < 3) { auto* next = f->pNext; *f = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, next}; }
                if(c.masks("dynamicRendering")) f->dynamicRendering = VK_FALSE;
                if(c.masks("synchronization2")) f->synchronization2 = VK_FALSE;
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES: {
                auto* f = reinterpret_cast<VkPhysicalDeviceVulkan14Features*>(s);
                if(c.minor() < 4) { auto* next = f->pNext; *f = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, next}; }
                if(c.masks("pushDescriptor")) f->pushDescriptor = VK_FALSE;
                if(c.masks("hostImageCopy")) f->hostImageCopy = VK_FALSE;
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES:
                if(c.masks("dynamicRendering") || (c.minor() < 3 && c.hides(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)))
                    reinterpret_cast<VkPhysicalDeviceDynamicRenderingFeatures*>(s)->dynamicRendering = VK_FALSE;
                break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES:
                if(c.masks("synchronization2") || (c.minor() < 3 && c.hides(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)))
                    reinterpret_cast<VkPhysicalDeviceSynchronization2Features*>(s)->synchronization2 = VK_FALSE;
                break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT:
                if(c.masks("extendedDynamicState") || (c.minor() < 3 && c.hides(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME)))
                    reinterpret_cast<VkPhysicalDeviceExtendedDynamicStateFeaturesEXT*>(s)->extendedDynamicState = VK_FALSE;
                break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES:
                if(c.masks("hostImageCopy") || (c.minor() < 4 && c.hides(VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME)))
                    reinterpret_cast<VkPhysicalDeviceHostImageCopyFeatures*>(s)->hostImageCopy = VK_FALSE;
                break;
            default: break;
        }
    }
}
// Returns the name of a feature the application tries to enable although the simulated device lacks it.
const char* over_requested_feature(const Config& c, const VkBaseInStructure* s) {
    for(; s; s = s->pNext) {
        switch(static_cast<int>(s->sType)) {
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES: {
                if(c.minor() < 3) return "VkPhysicalDeviceVulkan13Features passed to a pre-1.3 device";
                const auto* f = reinterpret_cast<const VkPhysicalDeviceVulkan13Features*>(s);
                if(f->dynamicRendering && c.masks("dynamicRendering")) return "dynamicRendering";
                if(f->synchronization2 && c.masks("synchronization2")) return "synchronization2";
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES: {
                if(c.minor() < 4) return "VkPhysicalDeviceVulkan14Features passed to a pre-1.4 device";
                const auto* f = reinterpret_cast<const VkPhysicalDeviceVulkan14Features*>(s);
                if(f->pushDescriptor && c.masks("pushDescriptor")) return "pushDescriptor";
                if(f->hostImageCopy && c.masks("hostImageCopy")) return "hostImageCopy";
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES:
                if(reinterpret_cast<const VkPhysicalDeviceDynamicRenderingFeatures*>(s)->dynamicRendering && c.masks("dynamicRendering")) return "dynamicRendering";
                break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES:
                if(reinterpret_cast<const VkPhysicalDeviceSynchronization2Features*>(s)->synchronization2 && c.masks("synchronization2")) return "synchronization2";
                break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT:
                if(reinterpret_cast<const VkPhysicalDeviceExtendedDynamicStateFeaturesEXT*>(s)->extendedDynamicState && c.masks("extendedDynamicState")) return "extendedDynamicState";
                break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES:
                if(reinterpret_cast<const VkPhysicalDeviceHostImageCopyFeatures*>(s)->hostImageCopy && c.masks("hostImageCopy")) return "hostImageCopy";
                break;
            default: break;
        }
    }
    return nullptr;
}

VKAPI_ATTR void VKAPI_CALL sim_GetPhysicalDeviceProperties(VkPhysicalDevice pd, VkPhysicalDeviceProperties* out) {
    const auto data = instance_of(pd);
    data.properties(pd, out);
    if(data.config.api && out->apiVersion > data.config.api)
        out->apiVersion = VK_MAKE_API_VERSION(0, 1, data.config.minor(), VK_API_VERSION_PATCH(out->apiVersion));
}
VKAPI_ATTR void VKAPI_CALL sim_GetPhysicalDeviceProperties2(VkPhysicalDevice pd, VkPhysicalDeviceProperties2* out) {
    const auto data = instance_of(pd);
    data.properties2(pd, out);
    if(data.config.api && out->properties.apiVersion > data.config.api)
        out->properties.apiVersion = VK_MAKE_API_VERSION(0, 1, data.config.minor(), VK_API_VERSION_PATCH(out->properties.apiVersion));
}
VKAPI_ATTR void VKAPI_CALL sim_GetPhysicalDeviceFeatures2(VkPhysicalDevice pd, VkPhysicalDeviceFeatures2* out) {
    const auto data = instance_of(pd);
    data.features2(pd, out);
    mask_features(data.config, static_cast<VkBaseOutStructure*>(out->pNext));
}
VKAPI_ATTR VkResult VKAPI_CALL sim_EnumerateDeviceExtensionProperties(VkPhysicalDevice pd, const char* layer, std::uint32_t* count, VkExtensionProperties* out) {
    if(layer && std::strcmp(layer, "VK_LAYER_CAT_device_sim") == 0) { *count = 0; return VK_SUCCESS; }
    const auto data = instance_of(pd);
    if(layer || !data.config.active()) return data.extensions(pd, layer, count, out);
    std::uint32_t n = 0;
    VkResult r = data.extensions(pd, nullptr, &n, nullptr);
    if(r != VK_SUCCESS) return r;
    std::vector<VkExtensionProperties> all(n);
    r = data.extensions(pd, nullptr, &n, all.data());
    if(r != VK_SUCCESS && r != VK_INCOMPLETE) return r;
    all.resize(n);
    all.erase(std::remove_if(all.begin(), all.end(), [&](const auto& e) { return data.config.hides(e.extensionName); }), all.end());
    if(!out) { *count = static_cast<std::uint32_t>(all.size()); return VK_SUCCESS; }
    const auto copy = std::min<std::size_t>(*count, all.size());
    std::copy_n(all.begin(), copy, out);
    *count = static_cast<std::uint32_t>(copy);
    return copy < all.size() ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL sim_GetDeviceProcAddr(VkDevice device, const char* name);
VKAPI_ATTR void VKAPI_CALL sim_DestroyDevice(VkDevice device, const VkAllocationCallbacks* alloc) {
    PFN_vkDestroyDevice next{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_devices.find(key(device));
        if(it == g_devices.end()) return;
        next = it->second.destroy;
        g_devices.erase(it);
    }
    next(device, alloc);
}
VKAPI_ATTR VkResult VKAPI_CALL sim_CreateDevice(VkPhysicalDevice pd, const VkDeviceCreateInfo* info, const VkAllocationCallbacks* alloc, VkDevice* device) {
    const auto data = instance_of(pd);
    auto* chain = static_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(info->pNext));
    while(chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && chain->function == VK_LAYER_LINK_INFO))
        chain = static_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(chain->pNext));
    if(!chain) return VK_ERROR_INITIALIZATION_FAILED;
    const auto next_gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    const auto next_gdpa = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    DeviceData dd;
    dd.config = data.config;
    for(std::uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
        const char* ext = info->ppEnabledExtensionNames[i];
        if(data.config.hides(ext)) {
            std::fprintf(stderr, "VK_LAYER_CAT_device_sim: REJECTED vkCreateDevice - extension %s is not available on the simulated device\n", ext);
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
        dd.enabled.insert(ext);
    }
    if(const char* feature = over_requested_feature(data.config, static_cast<const VkBaseInStructure*>(info->pNext))) {
        std::fprintf(stderr, "VK_LAYER_CAT_device_sim: REJECTED vkCreateDevice - %s is not available on the simulated device\n", feature);
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    const auto create = reinterpret_cast<PFN_vkCreateDevice>(next_gipa(data.instance, "vkCreateDevice"));
    const VkResult r = create(pd, info, alloc, device);
    if(r != VK_SUCCESS) return r;
    dd.gdpa = next_gdpa;
    dd.destroy = reinterpret_cast<PFN_vkDestroyDevice>(next_gdpa(*device, "vkDestroyDevice"));
    std::lock_guard lock(g_mutex);
    g_devices[key(*device)] = std::move(dd);
    return VK_SUCCESS;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL sim_GetDeviceProcAddr(VkDevice device, const char* name) {
    if(std::strcmp(name, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(sim_GetDeviceProcAddr);
    if(std::strcmp(name, "vkDestroyDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(sim_DestroyDevice);
    DeviceData dd;
    {
        std::lock_guard lock(g_mutex);
        dd = g_devices.at(key(device));
    }
    if(dd.config.no_commands.count(name) != 0) return nullptr;
    if(dd.config.active())
        for(const auto& c : kCommands) {
            if(std::strcmp(c.name, name) != 0) continue;
            const bool core_ok = c.core_minor >= 0 && static_cast<std::uint32_t>(c.core_minor) <= dd.config.minor();
            const bool ext_ok = c.extension && !dd.config.hides(c.extension) && dd.enabled.count(c.extension) != 0;
            // WSI commands are provided by instance extensions too; never hide the swapchain family.
            const bool wsi = c.extension && std::strcmp(c.extension, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            if(!core_ok && !ext_ok && !wsi) return nullptr;
            break;
        }
    return dd.gdpa(device, name);
}

VKAPI_ATTR void VKAPI_CALL sim_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* alloc) {
    PFN_vkDestroyInstance next{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_instances.find(key(instance));
        if(it == g_instances.end()) return;
        next = it->second.destroy;
        g_instances.erase(it);
    }
    next(instance, alloc);
}
VKAPI_ATTR VkResult VKAPI_CALL sim_CreateInstance(const VkInstanceCreateInfo* info, const VkAllocationCallbacks* alloc, VkInstance* instance) {
    auto* chain = static_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(info->pNext));
    while(chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && chain->function == VK_LAYER_LINK_INFO))
        chain = static_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(chain->pNext));
    if(!chain) return VK_ERROR_INITIALIZATION_FAILED;
    const auto next_gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    const auto create = reinterpret_cast<PFN_vkCreateInstance>(next_gipa(VK_NULL_HANDLE, "vkCreateInstance"));
    const VkResult r = create(info, alloc, instance);
    if(r != VK_SUCCESS) return r;
    InstanceData d;
    d.instance = *instance;
    d.config = read_config();
    d.gipa = next_gipa;
    d.destroy = reinterpret_cast<PFN_vkDestroyInstance>(next_gipa(*instance, "vkDestroyInstance"));
    d.properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(next_gipa(*instance, "vkGetPhysicalDeviceProperties"));
    d.properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(next_gipa(*instance, "vkGetPhysicalDeviceProperties2"));
    d.features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(next_gipa(*instance, "vkGetPhysicalDeviceFeatures2"));
    d.extensions = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(next_gipa(*instance, "vkEnumerateDeviceExtensionProperties"));
    if(d.config.active())
        std::fprintf(stderr, "VK_LAYER_CAT_device_sim: simulating api=%s hide=%s%zu extension(s) maskedFeatures=%zu\n",
                     d.config.api ? (std::to_string(VK_API_VERSION_MAJOR(d.config.api)) + "." + std::to_string(VK_API_VERSION_MINOR(d.config.api))).c_str() : "driver",
                     d.config.hide_all ? "all optional + " : "", d.config.hidden.size(), d.config.no_features.size());
    std::lock_guard lock(g_mutex);
    g_instances[key(*instance)] = std::move(d);
    return VK_SUCCESS;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL sim_GetInstanceProcAddr(VkInstance instance, const char* name) {
    static const std::pair<const char*, PFN_vkVoidFunction> kOwn[] = {
        {"vkGetInstanceProcAddr", reinterpret_cast<PFN_vkVoidFunction>(sim_GetInstanceProcAddr)},
        {"vkCreateInstance", reinterpret_cast<PFN_vkVoidFunction>(sim_CreateInstance)},
        {"vkDestroyInstance", reinterpret_cast<PFN_vkVoidFunction>(sim_DestroyInstance)},
        {"vkCreateDevice", reinterpret_cast<PFN_vkVoidFunction>(sim_CreateDevice)},
        {"vkDestroyDevice", reinterpret_cast<PFN_vkVoidFunction>(sim_DestroyDevice)},
        {"vkGetDeviceProcAddr", reinterpret_cast<PFN_vkVoidFunction>(sim_GetDeviceProcAddr)},
        {"vkGetPhysicalDeviceProperties", reinterpret_cast<PFN_vkVoidFunction>(sim_GetPhysicalDeviceProperties)},
        {"vkGetPhysicalDeviceProperties2", reinterpret_cast<PFN_vkVoidFunction>(sim_GetPhysicalDeviceProperties2)},
        {"vkGetPhysicalDeviceProperties2KHR", reinterpret_cast<PFN_vkVoidFunction>(sim_GetPhysicalDeviceProperties2)},
        {"vkGetPhysicalDeviceFeatures2", reinterpret_cast<PFN_vkVoidFunction>(sim_GetPhysicalDeviceFeatures2)},
        {"vkGetPhysicalDeviceFeatures2KHR", reinterpret_cast<PFN_vkVoidFunction>(sim_GetPhysicalDeviceFeatures2)},
        {"vkEnumerateDeviceExtensionProperties", reinterpret_cast<PFN_vkVoidFunction>(sim_EnumerateDeviceExtensionProperties)},
    };
    for(const auto& [own, pointer] : kOwn) if(std::strcmp(name, own) == 0) return pointer;
    if(!instance) return nullptr;
    PFN_vkGetInstanceProcAddr next{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_instances.find(key(instance));
        if(it == g_instances.end()) return nullptr;
        next = it->second.gipa;
    }
    return next(instance, name);
}
}  // namespace

extern "C" __attribute__((visibility("default"))) VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* version) {
    if(!version || version->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) return VK_ERROR_INITIALIZATION_FAILED;
    if(version->loaderLayerInterfaceVersion > 2) version->loaderLayerInterfaceVersion = 2;
    version->pfnGetInstanceProcAddr = sim_GetInstanceProcAddr;
    version->pfnGetDeviceProcAddr = sim_GetDeviceProcAddr;
    version->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}
