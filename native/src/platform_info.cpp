// N7: see include/cat/platform_info.hpp.
#include "cat/platform_info.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2   // K32GetProcessMemoryInfo lives in kernel32 (no psapi.lib)
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <fstream>
#include <string>
#endif

namespace cat {

ProcessMemory process_memory() {
    ProcessMemory m;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX c{};
    c.cb = sizeof c;
    if(K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&c), sizeof c)) {
        m.current = c.WorkingSetSize;
        m.peak = c.PeakWorkingSetSize;
        m.ok = true;
        m.private_commit = c.PrivateUsage;
        m.private_commit_ok = true;
    }
#else
    std::ifstream in("/proc/self/status");   // Linux and Android
    std::string key;
    while(in >> key) {
        std::uint64_t kb = 0;
        if(key == "VmRSS:" && in >> kb) { m.current = kb * 1024; m.ok = true; }
        else if(key == "VmHWM:" && in >> kb) m.peak = kb * 1024;
        std::string rest; std::getline(in, rest);
    }
#endif
    return m;
}

}  // namespace cat
