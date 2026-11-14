#pragma once
// N7: process memory for the performance overlay (the legacy shows the JavaScript heap and the page's
// memory; the native game shows what the operating system counts for the process).
#include <cstdint>

namespace cat {
struct ProcessMemory {
    std::uint64_t current{}, peak{};   // bytes: resident / working set and its peak
    bool ok{};
    std::uint64_t private_commit{};
    bool private_commit_ok{};
};
ProcessMemory process_memory();
}  // namespace cat
