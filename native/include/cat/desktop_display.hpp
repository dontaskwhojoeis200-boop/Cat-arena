#pragma once

#include <string>
#include <utility>

struct SDL_Window;

namespace cat {
// Owns the selected window/fullscreen mode and retains the windowed size across F11 toggles.
class DesktopDisplay {
public:
    DesktopDisplay(SDL_Window* window, int default_width, int default_height);
    void configure(std::string resolution, int refresh);
    void fullscreen(bool enabled);
    bool fullscreen() const;
    void display_changed();
    std::string status() const;
private:
    bool apply_fullscreen();
    void apply_window_size();
    void failed(const char* action);
    SDL_Window* window_;
    int default_width_, default_height_;
    std::string resolution_{"default"}, issue_;
    int refresh_{};
    std::pair<int, int> window_size_{};
};
}  // namespace cat
