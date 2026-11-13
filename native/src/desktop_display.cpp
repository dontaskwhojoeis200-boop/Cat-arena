#include "cat/desktop_display.hpp"

#include "cat/core.hpp"
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace cat {
namespace {
std::pair<int, int> dimensions(const std::string& resolution, int width, int height) {
    int w = 0, h = 0;
    if(std::sscanf(resolution.c_str(), "%dx%d", &w, &h) == 2 && w > 0 && h > 0) return {w, h};
    return {width, height};
}
}  // namespace

DesktopDisplay::DesktopDisplay(SDL_Window* window, int default_width, int default_height)
    : window_(window), default_width_(default_width), default_height_(default_height) {}

bool DesktopDisplay::fullscreen() const {
    return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
}

void DesktopDisplay::failed(const char* action) {
    issue_ = std::string(action) + ": " + SDL_GetError();
    log("warning", "display", issue_);
}

void DesktopDisplay::apply_window_size() {
    auto [w, h] = dimensions(resolution_, default_width_, default_height_);
    SDL_Rect usable{};
    const auto display = SDL_GetDisplayForWindow(window_);
    if(resolution_ != "default" && SDL_GetDisplayUsableBounds(display, &usable) && usable.w > 40 && usable.h > 75) {
        const double scale = std::min({1.0, static_cast<double>(usable.w - 32) / w,
                                      static_cast<double>(usable.h - 64) / h});
        w = std::max(1, static_cast<int>(w * scale));
        h = std::max(1, static_cast<int>(h * scale));
    }
    if(!SDL_SetWindowSize(window_, w, h)) failed("Could not resize window");
}

bool DesktopDisplay::apply_fullscreen() {
    const auto display = SDL_GetDisplayForWindow(window_);
    const auto* desktop = SDL_GetDesktopDisplayMode(display);
    const auto [w, h] = dimensions(resolution_, desktop ? desktop->w : default_width_, desktop ? desktop->h : default_height_);
    const SDL_DisplayMode* chosen = nullptr;
    if(resolution_ != "default" || refresh_ != 0) {
        int count = 0;
        auto** modes = SDL_GetFullscreenDisplayModes(display, &count);
        const double wanted = refresh_ != 0 ? static_cast<double>(refresh_) : desktop ? desktop->refresh_rate : 0;
        double best = std::numeric_limits<double>::infinity();
        for(int i = 0; modes && i < count; ++i) {
            const auto& mode = *modes[i];
            if(mode.w != w || mode.h != h) continue;
            const double difference = wanted > 0 ? std::abs(mode.refresh_rate - wanted) : 0;
            if((refresh_ != 0 && difference > 1.1) || difference >= best) continue;
            best = difference;
            chosen = &mode;
        }
        if(!chosen) {
            issue_ = "Requested mode unavailable. Using desktop fullscreen.";
            log("info", "display", issue_);
        } else if(SDL_SetWindowFullscreenMode(window_, chosen)) {
            SDL_free(modes);
            return true;
        } else failed("Could not select fullscreen display mode");
        SDL_free(modes);
    }
    if(SDL_SetWindowFullscreenMode(window_, nullptr)) return true;
    failed("Could not select desktop fullscreen");
    return false;
}

void DesktopDisplay::configure(std::string resolution, int refresh) {
    if(resolution == resolution_ && refresh == refresh_) return;
    const bool resized = resolution != resolution_;
    resolution_ = std::move(resolution);
    refresh_ = refresh;
    issue_.clear();
    if(fullscreen()) {
        if(resized) window_size_ = {};
        apply_fullscreen();
    } else if(resized) apply_window_size();
}

void DesktopDisplay::fullscreen(bool enabled) {
    if(enabled == fullscreen()) return;
    issue_.clear();
    if(enabled) {
        SDL_GetWindowSize(window_, &window_size_.first, &window_size_.second);
        if(!apply_fullscreen()) return;
    }
    if(!SDL_SetWindowFullscreen(window_, enabled)) { failed("Could not change fullscreen state"); return; }
    if(!SDL_SyncWindow(window_)) failed("Display mode change did not finish");
    if(!enabled) {
        if(window_size_.first > 0 && window_size_.second > 0) {
            if(!SDL_SetWindowSize(window_, window_size_.first, window_size_.second)) failed("Could not restore window size");
        } else apply_window_size();
    }
    log("info", "display", status());
}

void DesktopDisplay::display_changed() {
    issue_.clear();
    if(fullscreen()) apply_fullscreen();
}

std::string DesktopDisplay::status() const {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    const auto* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_));
    char info[112];
    if(mode && mode->refresh_rate > 0)
        std::snprintf(info, sizeof info, "%d×%d · %.2f Hz · %s", w, h, static_cast<double>(mode->refresh_rate), fullscreen() ? "FULLSCREEN" : "WINDOWED");
    else std::snprintf(info, sizeof info, "%d×%d · %s", w, h, fullscreen() ? "FULLSCREEN" : "WINDOWED");
    return issue_.empty() ? info : std::string(info) + " · " + issue_;
}
}  // namespace cat
