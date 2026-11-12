// Self-test of the test instrument: VK_LAYER_CAT_device_sim must really behave like an older device,
// otherwise "the baseline passes on a simulated 1.2 device" would prove nothing.
//   cat_arena_vk_sim_selftest <layer-manifest-dir>      exit 77: no Vulkan driver / layer cannot apply
#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
int checks = 0;
void check(bool value, const std::string& why) { ++checks; if(!value) throw std::runtime_error(why); }
struct Instance {
    VkInstance handle{};
    VkPhysicalDevice physical{};
    VkPhysicalDeviceProperties properties{};
    std::vector<VkExtensionProperties> extensions;
    Instance() {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.apiVersion = VK_API_VERSION_1_4;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        const VkResult r = vkCreateInstance(&info, nullptr, &handle);
        if(r != VK_SUCCESS) throw std::runtime_error("No Vulkan instance, VkResult=" + std::to_string(r));
        std::uint32_t n = 1;
        vkEnumeratePhysicalDevices(handle, &n, &physical);
        if(!physical) throw std::runtime_error("No Vulkan device");
        vkGetPhysicalDeviceProperties(physical, &properties);
        vkEnumerateDeviceExtensionProperties(physical, nullptr, &n, nullptr);
        extensions.resize(n);
        vkEnumerateDeviceExtensionProperties(physical, nullptr, &n, extensions.data());
        extensions.resize(n);
    }
    ~Instance() { if(handle) vkDestroyInstance(handle, nullptr); }
    bool has(const char* ext) const {
        for(const auto& e : extensions) if(std::strcmp(e.extensionName, ext) == 0) return true;
        return false;
    }
    VkResult device(const std::vector<const char*>& ext, const void* features, VkDevice* out) const {
        float priority = 1;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueCount = 1; qi.pQueuePriorities = &priority;
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        di.pNext = features; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
        di.enabledExtensionCount = static_cast<std::uint32_t>(ext.size()); di.ppEnabledExtensionNames = ext.empty() ? nullptr : ext.data();
        return vkCreateDevice(physical, &di, nullptr, out);
    }
};
}  // namespace

int main(int argc, char** argv) {
    if(argc != 2) { std::cerr << "usage: cat_arena_vk_sim_selftest <layer-manifest-dir>\n"; return 2; }
    const std::string dir = argv[1];
    if(const char* old = std::getenv("VK_LAYER_PATH")) setenv("VK_LAYER_PATH", (dir + ":" + old).c_str(), 1);
    else setenv("VK_ADD_LAYER_PATH", dir.c_str(), 1);
    setenv("VK_INSTANCE_LAYERS", "VK_LAYER_CAT_device_sim", 1);
    try {
        std::uint32_t real_api = 0;
        bool real_dynamic_rendering = false;
        {   // pass-through when not configured
            unsetenv("CAT_VKSIM_API"); unsetenv("CAT_VKSIM_HIDE_EXT"); unsetenv("CAT_VKSIM_NO_FEATURES");
            Instance real;
            real_api = real.properties.apiVersion;
            real_dynamic_rendering = real.has(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
            std::cout << "driver: " << real.properties.deviceName << " API " << VK_API_VERSION_MAJOR(real_api) << '.' << VK_API_VERSION_MINOR(real_api) << '.'
                      << VK_API_VERSION_PATCH(real_api) << ", " << real.extensions.size() << " extensions\n";
            if(real_api < VK_API_VERSION_1_3) { std::cout << "SKIP driver is below 1.3: nothing to hide\n"; return 77; }
            VkDevice device{};
            check(real.device({}, nullptr, &device) == VK_SUCCESS, "pass-through device creation");
            check(vkGetDeviceProcAddr(device, "vkCmdBeginRendering") != nullptr, "pass-through must not hide 1.3 commands");
            vkDestroyDevice(device, nullptr);
        }
        setenv("CAT_VKSIM_API", "1.2", 1); setenv("CAT_VKSIM_HIDE_EXT", "*", 1);
        {
            Instance sim;
            check(VK_API_VERSION_MINOR(sim.properties.apiVersion) == 2, "simulated device must report API 1.2 (layer not loaded?)");
            check(!sim.has(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) && !sim.has(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) && sim.extensions.size() <= 1,
                  "optional extensions must be hidden");
            auto features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(vkGetInstanceProcAddr(sim.handle, "vkGetPhysicalDeviceFeatures2"));
            check(features2 != nullptr, "features2 entry point");
            VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &v13};
            features2(sim.physical, &f2);
            check(!v13.dynamicRendering && !v13.synchronization2 && !v13.maintenance4, "1.3 feature bits must read as false on a simulated 1.2 device");
            VkDevice device{};
            if(real_dynamic_rendering)
                check(sim.device({VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME}, nullptr, &device) == VK_ERROR_EXTENSION_NOT_PRESENT, "enabling a hidden extension must fail");
            v13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            v13.dynamicRendering = VK_TRUE;
            check(sim.device({}, &v13, &device) == VK_ERROR_FEATURE_NOT_PRESENT, "enabling 1.3 features on a simulated 1.2 device must fail");
            check(sim.device({}, nullptr, &device) == VK_SUCCESS, "plain device creation");
            for(const char* hidden : {"vkCmdBeginRendering", "vkCmdBeginRenderingKHR", "vkCmdPipelineBarrier2", "vkQueueSubmit2KHR", "vkCmdSetCullMode",
                                      "vkCmdSetCullModeEXT", "vkCmdPushDescriptorSet", "vkCmdPushDescriptorSetKHR", "vkCopyMemoryToImage", "vkTransitionImageLayoutEXT"})
                check(vkGetDeviceProcAddr(device, hidden) == nullptr, std::string(hidden) + " must not resolve on a simulated bare 1.2 device");
            for(const char* kept : {"vkCmdDraw", "vkCmdPipelineBarrier", "vkQueueSubmit", "vkCreateRenderPass", "vkCreateRenderPass2", "vkCmdBindDescriptorSets"})
                check(vkGetDeviceProcAddr(device, kept) != nullptr, std::string(kept) + " (<= 1.2 core) must still resolve");
            vkDestroyDevice(device, nullptr);
        }
        setenv("CAT_VKSIM_API", "1.3", 1); setenv("CAT_VKSIM_HIDE_EXT", "*", 1); setenv("CAT_VKSIM_NO_FEATURES", "synchronization2", 1);
        {
            Instance sim;
            check(VK_API_VERSION_MINOR(sim.properties.apiVersion) == 3, "simulated 1.3 device");
            auto features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(vkGetInstanceProcAddr(sim.handle, "vkGetPhysicalDeviceFeatures2"));
            VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &v13};
            features2(sim.physical, &f2);
            check(v13.dynamicRendering && !v13.synchronization2, "masked feature bit must read false, others untouched");
            VkDevice device{};
            VkPhysicalDeviceVulkan13Features want{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            want.synchronization2 = VK_TRUE;
            check(sim.device({}, &want, &device) == VK_ERROR_FEATURE_NOT_PRESENT, "enabling a masked feature must fail");
            want.synchronization2 = VK_FALSE; want.dynamicRendering = VK_TRUE;
            check(sim.device({}, &want, &device) == VK_SUCCESS, "enabling an available 1.3 feature");
            check(vkGetDeviceProcAddr(device, "vkCmdBeginRendering") != nullptr, "1.3 core command on a simulated 1.3 device");
            check(vkGetDeviceProcAddr(device, "vkCmdPushDescriptorSet") == nullptr, "1.4 core command must not resolve on a simulated 1.3 device");
            vkDestroyDevice(device, nullptr);
        }
        std::cout << "PASS device simulation layer: " << checks << " checks\n";
        return 0;
    } catch(const std::exception& e) {
        const std::string what = e.what();
        if(what.rfind("No Vulkan", 0) == 0) { std::cout << "SKIP " << what << '\n'; return 77; }
        std::cerr << "FAIL " << what << '\n';
        return 1;
    }
}
