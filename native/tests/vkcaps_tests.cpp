// N2.1: render-path selection against synthetic devices. No GPU, no Vulkan headers.
#include "cat/vulkan_caps.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace cat::vk;
namespace {
int checks = 0;
void check(bool value, const std::string& why) { ++checks; if(!value) throw std::runtime_error(why); }
const char* const kAllExtensions[] = {"VK_KHR_swapchain", "VK_KHR_dynamic_rendering", "VK_KHR_synchronization2", "VK_EXT_extended_dynamic_state",
                                      "VK_KHR_push_descriptor", "VK_EXT_host_image_copy", "VK_KHR_copy_commands2", "VK_KHR_format_feature_flags2"};
DeviceCaps device(std::uint32_t api, bool extensions, bool bits) {
    DeviceCaps c;
    c.name = "synthetic";
    c.loader_api = make_api(1, 3, 275);
    c.device_api = api;
    if(extensions) for(const char* e : kAllExtensions) c.extensions.emplace_back(e);
    else c.extensions.emplace_back("VK_KHR_swapchain");
    c.core_bit.fill(bits);
    c.extension_bit.fill(bits && extensions);
    c.host_copy_format = c.host_copy_layout = c.host_copy_optimal = bits;
    return c;
}
bool all(const Path& p, Source s) {
    for(auto x : p.source) if(x != s) return false;
    return true;
}
bool throws(const DeviceCaps& c, const Request& r = {}) {
    try { (void)select_path(c, r); } catch(const std::runtime_error&) { return true; }
    return false;
}
}  // namespace

int main() {
    try {
        using F = Feature;
        // --- the mandatory baseline -------------------------------------------------------------
        {
            const auto p = select_path(device(make_api(1, 2, 131), false, false));
            check(p.api == kApi12 && p.is_baseline() && p.extensions.empty(), "bare 1.2 device must select the baseline");
            check(p.tier() == "vulkan-1.2-baseline", "tier name: " + p.tier());
            check(p.notes.size() == kFeatureCount, "one note per feature");
        }
        check(throws(device(make_api(1, 1, 200), true, true)), "a Vulkan 1.1 device must be rejected, whatever it advertises");
        check(throws(device(make_api(1, 0, 3), true, true)), "a Vulkan 1.0 device must be rejected");
        {
            auto c = device(kApi14, true, true);
            c.loader_api = make_api(1, 0, 0);
            check(throws(c), "a 1.0 loader must be rejected");
            Request r; r.max_api = kApi11;
            check(throws(device(kApi14, true, true), r), "an API cap below 1.2 is invalid");
        }
        // --- 1.2 + extensions ---------------------------------------------------------------------
        {
            const auto p = select_path(device(kApi12, true, true));
            check(p.api == kApi12 && all(p, Source::extension), "1.2 device with all extensions: every feature through its extension");
            check(p.tier() == "vulkan-1.2+extensions", "tier name: " + p.tier());
            check(p.extensions.size() == 7, "5 feature extensions + 2 host-image-copy dependencies, got " + std::to_string(p.extensions.size()));
            Request r; r.allow_extensions = false;
            check(select_path(device(kApi12, true, true), r).is_baseline(), "extensions not allowed -> baseline on a 1.2 device");
            check(select_path(device(kApi12, true, true), Request::baseline()).is_baseline(), "Request::baseline() on a 1.2 device");
        }
        {   // advertised extension, feature bit off: must not be used
            auto c = device(kApi12, true, true);
            c.extension_bit[static_cast<std::size_t>(F::dynamic_rendering)] = false;
            const auto p = select_path(c);
            check(!p.uses(F::dynamic_rendering) && p.uses(F::synchronization2), "extension without its feature bit is not used");
            check(p.uses(F::push_descriptor), "VK_KHR_push_descriptor has no feature bit; presence is enough");
        }
        {   // host image copy via extension needs its two dependencies below 1.3
            auto c = device(kApi12, true, true);
            c.extensions.erase(std::find(c.extensions.begin(), c.extensions.end(), "VK_KHR_copy_commands2"));
            check(!select_path(c).uses(F::host_image_copy), "host image copy without VK_KHR_copy_commands2 on 1.2");
        }
        // --- 1.3 ----------------------------------------------------------------------------------
        {
            const auto p = select_path(device(kApi13, false, true));
            check(p.api == kApi13 && p[F::dynamic_rendering] == Source::core && p[F::synchronization2] == Source::core &&
                  p[F::extended_dynamic_state] == Source::core, "1.3 device: three core features");
            check(!p.uses(F::push_descriptor) && !p.uses(F::host_image_copy) && p.extensions.empty(), "1.3 device without extensions: no 1.4 features");
            check(p.tier() == "vulkan-1.3", "tier name: " + p.tier());
        }
        {
            const auto p = select_path(device(kApi13, true, true));
            check(p[F::push_descriptor] == Source::extension && p[F::host_image_copy] == Source::extension, "1.3 device: 1.4 features through extensions");
            check(p.extensions.size() == 2, "on 1.3 host image copy needs no extra dependency extensions");
        }
        {   // "do not assume an API version guarantees a feature": 1.3 device reporting no feature bits
            const auto p = select_path(device(kApi13, false, false));
            check(!p.uses(F::dynamic_rendering) && !p.uses(F::synchronization2), "1.3 without feature bits: render pass + legacy sync");
            check(p[F::extended_dynamic_state] == Source::core, "extended dynamic state is mandatory core 1.3 (no bit exists)");
        }
        // --- 1.4 ----------------------------------------------------------------------------------
        {
            const auto p = select_path(device(kApi14, false, true));
            check(p.api == kApi14 && all(p, Source::core) && p.extensions.empty() && p.tier() == "vulkan-1.4", "1.4 device: everything core");
        }
        {   // hostImageCopy is optional in 1.4
            auto c = device(kApi14, false, true);
            c.core_bit[static_cast<std::size_t>(F::host_image_copy)] = false;
            const auto p = select_path(c);
            check(!p.uses(F::host_image_copy) && p.uses(F::push_descriptor), "1.4 without hostImageCopy falls back to staging uploads only");
        }
        for(int k = 0; k < 3; ++k) {   // host copy supported but not useful
            auto c = device(kApi14, false, true);
            (k == 0 ? c.host_copy_format : k == 1 ? c.host_copy_layout : c.host_copy_optimal) = false;
            check(!select_path(c).uses(F::host_image_copy), "host image copy requires format, layout and optimal access");
        }
        {   // device newer than the engine: clamp to 1.4
            const auto p = select_path(device(make_api(1, 5, 0), false, true));
            check(p.api == kApi14, "future device versions are used as 1.4");
        }
        // --- requests -----------------------------------------------------------------------------
        {
            Request r; r.max_api = kApi13;
            const auto p = select_path(device(kApi14, true, true), r);
            check(p.api == kApi13 && p[F::dynamic_rendering] == Source::core && p[F::push_descriptor] == Source::extension, "cap 1.3 on a 1.4 device");
            r.allow_extensions = false;
            check(!select_path(device(kApi14, true, true), r).uses(F::push_descriptor), "cap 1.3 without extensions: no push descriptors");
            const auto b = select_path(device(kApi14, true, true), Request::baseline());
            check(b.api == kApi12 && b.is_baseline() && b.extensions.empty(), "Request::baseline() forces the 1.2 path on a 1.4 device");
        }
        for(const auto& f : feature_table()) {
            Request r; r.disable(f.id);
            const auto p = select_path(device(kApi14, true, true), r);
            for(const auto& g : feature_table())
                check(p.uses(g.id) == (g.id != f.id), std::string("disabling ") + f.name + " must only affect that feature");
            check(parse_feature(f.name) == f.id, "parse_feature round trip");
        }
        {
            Request r;
            for(const auto& f : feature_table()) r.disable(f.id);
            const auto p = select_path(device(kApi14, true, true), r);
            check(p.is_baseline() && p.api == kApi14 && p.tier() == "vulkan-1.4-baseline-fallbacks", "all features disabled: " + p.tier());
        }
        {   // runtime drop (missing entry point)
            auto p = select_path(device(kApi14, false, true));
            const auto notes = p.notes.size();
            p.drop(F::synchronization2, "vkQueueSubmit2 not resolved");
            check(!p.uses(F::synchronization2) && p.uses(F::dynamic_rendering) && p.notes.size() == notes + 1, "drop affects one feature and records why");
            p.drop(F::synchronization2, "again");
            check(p.notes.size() == notes + 1, "dropping twice records once");
        }
        check(parse_api("1.2") == kApi12 && parse_api("1.3") == kApi13 && parse_api("1.4") == kApi14, "parse_api");
        bool bad = false;
        try { (void)parse_api("1.1"); } catch(const std::runtime_error&) { bad = true; }
        check(bad, "parse_api rejects 1.1");
        bad = false;
        try { (void)parse_feature("ray_tracing"); } catch(const std::runtime_error&) { bad = true; }
        check(bad, "parse_feature rejects unknown names");
        check(api_string(make_api(1, 4, 318)) == "1.4.318" && api_string(kApi12, false) == "1.2", "api_string");
        check(negotiated_api(device(make_api(1, 3, 250), false, true), {}) == kApi13, "negotiated_api ignores the patch number");
        std::cout << "PASS vkcaps: " << checks << " checks\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
