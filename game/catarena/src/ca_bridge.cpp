// Cat Arena: Feral Claw — C++20 side of the C bridge (see ca_bridge.h). N10-Q3E stage B.
// Licensed under the GNU GPL v2 or later (docs/q3e/LICENSING.md).
#include "ca_bridge.h"

#include <array>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace {

constexpr std::string_view kProduct = "Cat Arena: Feral Claw N10-Q3E-E1";

// One static buffer per module name the engine can load; the strings live for the process.
template <std::size_t N>
struct FixedString {
    std::array<char, N> text{};
    const char* set(std::string_view a, std::string_view b) noexcept {
        std::snprintf(text.data(), text.size(), "%.*s (%.*s, C++20 bridge)", int(a.size()), a.data(), int(b.size()), b.data());
        return text.data();
    }
};

FixedString<160> g_build;

}  // namespace

extern "C" const char* CA_BuildString(const char* module) {
    try {
        return g_build.set(kProduct, module ? std::string_view(module) : std::string_view("module"));
    } catch(...) {
        return "Cat Arena: Feral Claw";
    }
}

extern "C" float CA_MetresToUnits(float metres) { return metres * CA_UNITS_PER_METRE; }
extern "C" float CA_UnitsToMetres(float units) { return units / CA_UNITS_PER_METRE; }

extern "C" int CA_SelfTest(void (*report)(const char* line)) {
    // Layout assumptions shared with the C game code.
    static_assert(sizeof(int) == 4, "the Quake III module ABI passes 32-bit ints");
    static_assert(sizeof(float) == 4, "the Quake III module ABI passes floats as 32-bit ints");
    static_assert(sizeof(void*) == sizeof(long long) || sizeof(void*) == 4, "unexpected pointer size");
    bool caught = false;
    try {
        throw std::runtime_error("bridge self-test");   // must stay inside C++
    } catch(const std::exception&) {
        caught = true;
    }
    const bool units_ok = CA_MetresToUnits(1.8f) > 57.5f && CA_MetresToUnits(1.8f) < 57.7f &&
                          CA_UnitsToMetres(CA_MetresToUnits(2.0f)) == 2.0f;
    const bool ok = caught && units_ok;
    if(report) {
        char line[128];
        std::snprintf(line, sizeof line, "C++20 bridge self-test: %s (exceptions contained: %s, units: %s)\n",
                      ok ? "PASS" : "FAIL", caught ? "yes" : "no", units_ok ? "ok" : "wrong");
        report(line);
    }
    return ok ? 1 : 0;
}
