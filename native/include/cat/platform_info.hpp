#pragma once
// N7: process memory for the performance overlay (the legacy shows the JavaScript heap and the page's
// memory; the native game shows what the operating system counts for the process).
#include <cstdint>

namespace cat {
// bytes: resident / working set and its peak; `private_bytes`: Windows PrivateUsage (private commit charge),
// Linux RssAnon (resident anonymous memory; the closest available figure, not a commit charge).
struct ProcessMemory { std::uint64_t current{}, peak{}, private_bytes{}; bool ok{}; };
ProcessMemory process_memory();
}  // namespace cat
