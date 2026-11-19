#pragma once
// N9 crash reporting.
//
// install() sets handlers for the ways the game can die without an exception reaching main():
//   Windows  unhandled SEH exceptions (access violation, stack overflow, …), std::terminate,
//            invalid CRT parameters, pure virtual calls  -> CatArena-crash-<time>.dmp (minidump:
//            threads, stacks, module list) + CatArena-crash-<time>.txt
//   others   SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, std::terminate -> CatArena-crash-<time>.txt
//            with a backtrace (development / CI hosts)
// The text report holds the build, the platform, the crash reason and address (module + offset on
// Windows) and the last 64 log lines. Reports are best effort: the process is already broken when
// they are written. Exceptions that reach main() are not crashes: main() logs them and shows its
// error dialog as before.
#include <filesystem>
#include <string>

namespace cat::crash {

struct Options {
    std::filesystem::path directory;   // where reports go (created if missing)
    std::string build;                 // e.g. "Cat Arena: Feral Claw N9 (windows-x64, Release)"
    bool dialog{};                     // Windows: tell the player where the report is (interactive runs)
};

void install(const Options& options);
// The folder reports are written to (empty before install()).
const std::filesystem::path& directory();
// --crash-test: crash on purpose to check the reporter. kind: "access" (null write), "abort",
// "terminate", "divide" (integer divide by zero; SIGFPE / EXCEPTION_INT_DIVIDE_BY_ZERO).
[[noreturn]] void deliberate(const std::string& kind);

}  // namespace cat::crash
