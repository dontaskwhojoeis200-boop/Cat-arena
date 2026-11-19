// N9 crash reporting (see crash_report.hpp). Everything here runs in a process that is already
// failing, so the handlers prepare what they can at install() time, write with plain system calls
// and never wait for a lock.
#include "cat/crash_report.hpp"
#include "cat/core.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <system_error>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <dbghelp.h>
#  include <stdlib.h>
#else
#  include <csignal>
#  include <ctime>
#  include <fcntl.h>
#  include <unistd.h>
#  if defined(__GLIBC__)
#    include <execinfo.h>
#  endif
#endif

namespace cat::crash {
namespace {
std::filesystem::path g_dir;
std::string g_build;
bool g_dialog = false;
std::atomic<bool> g_reported{false};   // one report per process (terminate → abort must not write twice)

#ifdef _WIN32
// ---------------------------------------------------------------------------------------------------- Windows
std::wstring g_dir_w;
EXCEPTION_POINTERS* g_exception = nullptr;
DWORD g_thread = 0;
char g_reason[512];

void write_all(HANDLE f, const char* s) { DWORD n = 0; WriteFile(f, s, static_cast<DWORD>(std::strlen(s)), &n, nullptr); }

const char* exception_name(DWORD code) {
    switch(code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_IN_PAGE_ERROR: return "in-page error (file or disk read failed)";
        case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "floating-point divide by zero";
        case 0xC0000409: return "stack buffer overrun / fast fail";
        case 0xE06D7363: return "C++ exception not caught";
        default: return "exception";
    }
}

// Writes the minidump and the text report; runs on its own thread so that a stack overflow on the
// crashing thread does not stop it.
DWORD WINAPI writer(LPVOID) {
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t stem[64];
    swprintf(stem, 64, L"CatArena-crash-%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    const std::wstring base = g_dir_w + L"\\" + stem;
    // minidump (dbghelp.dll is part of Windows; loaded here so the game does not depend on it otherwise)
    bool dumped = false;
    if(HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll")) {
        using Fn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
        if(auto write_dump = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")))) {
            HANDLE f = CreateFileW((base + L".dmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if(f != INVALID_HANDLE_VALUE) {
                MINIDUMP_EXCEPTION_INFORMATION info{g_thread, g_exception, FALSE};
                const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory);
                dumped = write_dump(GetCurrentProcess(), GetCurrentProcessId(), f, type, g_exception ? &info : nullptr, nullptr, nullptr) == TRUE;
                CloseHandle(f);
            }
        }
    }
    HANDLE f = CreateFileW((base + L".txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(f != INVALID_HANDLE_VALUE) {
        char line[1024];
        write_all(f, "Cat Arena: Feral Claw crash report\r\n\r\n");
        std::snprintf(line, sizeof line, "Build:   %s\r\nTime:    %04u-%02u-%02u %02u:%02u:%02u\r\nReason:  %s\r\n", g_build.c_str(), t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, g_reason);
        write_all(f, line);
        if(g_exception && g_exception->ExceptionRecord) {
            void* address = g_exception->ExceptionRecord->ExceptionAddress;
            HMODULE module = nullptr;
            char module_name[MAX_PATH] = "?";
            if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(address), &module) && module)
                GetModuleFileNameA(module, module_name, MAX_PATH);
            const char* short_name = std::strrchr(module_name, '\\');
            std::snprintf(line, sizeof line, "Where:   %s+0x%llx\r\n", short_name ? short_name + 1 : module_name,
                          static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(module)));
            write_all(f, line);
            if(g_exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && g_exception->ExceptionRecord->NumberParameters >= 2) {
                const auto& p = g_exception->ExceptionRecord->ExceptionInformation;
                std::snprintf(line, sizeof line, "Access:  %s address 0x%llx\r\n", p[0] == 0 ? "read of" : p[0] == 1 ? "write to" : "execute at", static_cast<unsigned long long>(p[1]));
                write_all(f, line);
            }
        }
        OSVERSIONINFOW os{sizeof os};
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
        if(auto rtl = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")))) rtl(&os);
        std::snprintf(line, sizeof line, "Windows: %lu.%lu build %lu\r\nDump:    %s\r\n\r\nLast log lines:\r\n", os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber,
                      dumped ? "written next to this file (.dmp)" : "could not be written");
        write_all(f, line);
        for(const auto& l : recent_log_lines(true)) { write_all(f, "  "); write_all(f, l.c_str()); write_all(f, "\r\n"); }
        CloseHandle(f);
    }
    if(g_dialog) {
        const std::wstring text = L"Cat Arena stopped because of an error.\n\nA crash report was saved in:\n" + g_dir_w + L"\n\n(" + stem +
                                  L".txt and .dmp). Please send both files with a description of what you were doing.";
        MessageBoxW(nullptr, text.c_str(), L"Cat Arena: Feral Claw", MB_OK | MB_ICONERROR | MB_TOPMOST);
    }
    return 0;
}

void report(EXCEPTION_POINTERS* ep, const char* reason) {
    if(g_reported.exchange(true)) return;
    g_exception = ep;
    g_thread = GetCurrentThreadId();
    std::snprintf(g_reason, sizeof g_reason, "%s", reason);
    if(HANDLE th = CreateThread(nullptr, 0, writer, nullptr, 0, nullptr)) { WaitForSingleObject(th, 60000); CloseHandle(th); }
    else writer(nullptr);
}

LONG WINAPI unhandled(EXCEPTION_POINTERS* ep) {
    char reason[160];
    const DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    std::snprintf(reason, sizeof reason, "%s (code 0x%08lx)", exception_name(code), static_cast<unsigned long>(code));
    report(ep, reason);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
}
void report_here_and_exit(const char* reason) {
    // No exception record: the dump holds every thread's stack as it is now.
    report(nullptr, reason);
    TerminateProcess(GetCurrentProcess(), 3);
}
void on_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, std::uintptr_t) { report_here_and_exit("invalid parameter passed to a C runtime function"); }
void on_purecall() { report_here_and_exit("pure virtual function call"); }
void platform_install() {
    g_dir_w = g_dir.wstring();
    SetUnhandledExceptionFilter(unhandled);
    _set_invalid_parameter_handler(on_invalid_parameter);
    _set_purecall_handler(on_purecall);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);   // no CRT dialog: the report replaces it
    ULONG guarantee = 32 * 1024;   // stack left for the filter after a stack overflow
    SetThreadStackGuarantee(&guarantee);
}
#else
// ---------------------------------------------------------------------------------------------------- POSIX
std::string g_dir_s;
char g_altstack[64 * 1024];

void write_all(int fd, const char* s) { std::size_t n = std::strlen(s); while(n > 0) { const auto w = ::write(fd, s, n); if(w <= 0) return; s += w; n -= static_cast<std::size_t>(w); } }

void write_report(const char* reason, bool trace) {
    if(g_reported.exchange(true)) return;
    char stamp[32];
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
    const std::string path = g_dir_s + "/CatArena-crash-" + stamp + ".txt";
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fd < 0) return;
    char line[1024];
    std::snprintf(line, sizeof line, "Cat Arena: Feral Claw crash report\n\nBuild:   %s\nTime:    %s\nReason:  %s\n\nBacktrace:\n", g_build.c_str(), stamp, reason);
    write_all(fd, line);
#if defined(__GLIBC__)
    if(trace) { void* frames[64]; const int n = backtrace(frames, 64); backtrace_symbols_fd(frames, n, fd); }
#else
    (void)trace;
#endif
    write_all(fd, "\nLast log lines:\n");
    for(const auto& l : recent_log_lines(true)) { write_all(fd, "  "); write_all(fd, l.c_str()); write_all(fd, "\n"); }
    ::close(fd);
    std::snprintf(line, sizeof line, "Crash report written: %s\n", path.c_str());
    write_all(2, line);
}
void on_signal(int sig, siginfo_t* info, void*) {
    char reason[160];
    std::snprintf(reason, sizeof reason, "signal %d (%s) at address %p", sig, sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGILL ? "SIGILL" : sig == SIGFPE ? "SIGFPE" : sig == SIGABRT ? "SIGABRT" : "?",
                  info ? info->si_addr : nullptr);
    write_report(reason, true);
    std::signal(sig, SIG_DFL);   // SA_RESETHAND as well: the default action ends the process with the same signal
    raise(sig);
}
void platform_install() {
    g_dir_s = g_dir.string();
    stack_t ss{};
    ss.ss_sp = g_altstack; ss.ss_size = sizeof g_altstack;
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    for(int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(sig, &sa, nullptr);
}
void report_here_and_exit(const char* reason) { write_report(reason, true); std::signal(SIGABRT, SIG_DFL); std::abort(); }
#endif

void on_terminate() {
    std::string reason = "std::terminate";
    if(const auto e = std::current_exception()) {
        try { std::rethrow_exception(e); }
        catch(const std::exception& x) { reason += std::string(": uncaught exception: ") + x.what(); }
        catch(...) { reason += ": uncaught non-standard exception"; }
    }
    report_here_and_exit(reason.c_str());
}
}  // namespace

const std::filesystem::path& directory() { return g_dir; }

void install(const Options& options) {
    g_dir = options.directory;
    g_build = options.build;
    g_dialog = options.dialog;
    std::error_code ec;
    std::filesystem::create_directories(g_dir, ec);
    platform_install();
    std::set_terminate(on_terminate);
    log("info", "crash", "Crash reports go to " + path_utf8(g_dir));
}

void deliberate(const std::string& kind) {
    log("info", "crash", "Deliberate crash for --crash-test " + kind);
    if(kind == "abort") std::abort();
    if(kind == "terminate") std::terminate();
    if(kind == "divide") { volatile int zero = 0; volatile int x = 1 / zero; (void)x; }
    volatile int* volatile p = nullptr;
    *p = 1;   // "access" (and the fallback for anything else)
    std::abort();
}

}  // namespace cat::crash
