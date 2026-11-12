#pragma once
// Paths <-> UTF-8 text (N6.5, Windows). SDL (SDL_GetBasePath, argv through SDL_main) hands out UTF-8.
// On Windows a std::filesystem::path built from a narrow string, and path::string(), use the ANSI
// code page instead; path::string() even throws for characters the code page cannot represent (a user
// folder such as "C:\Users\Zoë"). On Linux both helpers are the identity.
#include <filesystem>
#include <string>
#include <string_view>

namespace cat {
inline std::filesystem::path utf8_path(std::string_view utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }
inline std::string path_utf8(const std::filesystem::path& path) { const auto text = path.u8string(); return std::string(text.begin(), text.end()); }
}  // namespace cat
