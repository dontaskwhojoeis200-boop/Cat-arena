#pragma once
// N7 shell helpers that do not need SDL (unit-tested in tests/ui_tests.cpp):
//   * PerfMeter: the performance overlay's frame statistics (live / 60 s average FPS, 1 % and 0.1 % lows,
//     25 ms hitches, CPU time), with the legacy formulas.
//   * TouchControls: the legacy touch handlers (index.html 14996-15001, mobile-hud.js 16382-16393):
//     joystick, look drag, FIRE / JUMP / SWAP and the SPRINT / SLIDE / RELOAD buttons, as key bits, a
//     stick vector and look counts for the 120 Hz simulation.
//   * SprintLatch: sprintRequested() of the v0.18 settings (hold / toggle / auto).
#include "cat/combat.hpp"
#include "cat/physics.hpp"
#include "cat/ui_hud.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace cat {

// recordFrameInterval / updateFpsDistribution of the legacy (index.html 14684-14740): intervals of
// 0..500 ms enter a 60 s history (at most 16384 frames; a longer stall clears it), the average is
// frames / total time, the 1 % low is the mean of the slowest ceil(1 %) frames (from 100 frames on),
// the 0.1 % low the mean of the slowest ceil(0.1 %) (from 1000 frames on), hitches count intervals of
// 25 ms or more since start. FPS LIVE and CPU are averaged over windows of at least 800 ms.
class PerfMeter {
public:
    struct Summary { double fps_live{}, fps_avg{}, low1{}, low01{}, cpu_ms{}; std::uint64_t hitches{}; std::size_t frames{}; };
    static constexpr std::size_t kCap = 16384;
    void frame(double now_ms, double cpu_ms) {
        if(last_ >= 0) {
            const double ms = now_ms - last_;
            if(ms > 0 && ms < 500) {
                while(frames_.size() >= kCap) frames_.pop_front();
                frames_.push_back({now_ms, ms});
                if(ms >= 25) ++hitches_;
            } else if(ms >= 500) frames_.clear();
        }
        last_ = now_ms;
        while(!frames_.empty() && frames_.front().t < now_ms - 60000) frames_.pop_front();
        if(window_start_ < 0) window_start_ = now_ms;
        ++window_frames_; window_cpu_ += cpu_ms;
        if(now_ms - window_start_ >= 800) {
            summary_.fps_live = window_frames_ * 1000.0 / (now_ms - window_start_); summary_.cpu_ms = window_cpu_ / window_frames_;
            window_start_ = now_ms; window_frames_ = 0; window_cpu_ = 0;
            distribution();
        }
        summary_.hitches = hitches_; summary_.frames = frames_.size();
    }
    const Summary& summary() const { return summary_; }
private:
    struct F { double t, ms; };
    void distribution() {
        const std::size_t n = frames_.size();
        summary_.fps_avg = summary_.low1 = summary_.low01 = 0;
        if(!n) return;
        double total = 0; for(const auto& f : frames_) total += f.ms;
        summary_.fps_avg = 1000.0 * double(n) / std::max(.00001, total);
        if(n < 100) return;
        const std::size_t k = (n + 99) / 100;
        std::vector<double> ms; ms.reserve(n);
        for(const auto& f : frames_) ms.push_back(f.ms);
        std::partial_sort(ms.begin(), ms.begin() + static_cast<std::ptrdiff_t>(k), ms.end(), std::greater<>());
        double sum = 0; for(std::size_t i = 0; i < k; ++i) sum += ms[i];
        summary_.low1 = 1000.0 / (sum / double(k));
        if(n < 1000) return;
        const std::size_t small = std::max<std::size_t>(1, (n + 999) / 1000);
        double t = 0; for(std::size_t i = 0; i < small; ++i) t += ms[i];
        summary_.low01 = 1000.0 / (t / double(small));
    }
    std::deque<F> frames_;
    Summary summary_;
    double last_{-1}, window_start_{-1}, window_cpu_{};
    int window_frames_{};
    std::uint64_t hitches_{};
};

// sprintRequested(): 'auto' always, 'toggle' latches on each press, 'hold' while the key / button is held.
class SprintLatch {
public:
    void set_mode(const std::string& m) { if(m != mode_) { mode_ = m; latched_ = false; } }
    void press() { if(mode_ == "toggle") latched_ = !latched_; }
    bool requested(bool held) const { return mode_ == "auto" || (mode_ == "toggle" ? latched_ : held); }
    void reset() { latched_ = false; }
private:
    std::string mode_{"hold"};
    bool latched_{};
};

class TouchControls {
public:
    struct Prefs { double sensitivity{100}; bool invert{}; };
    // A finger went down at (x, y) in pixels; `css` = pixels per CSS pixel (look speed is in CSS pixels).
    // Returns the id of the button it pressed ("" = look / nothing; "pause", "settings", "map" for the HUD buttons).
    std::string down(std::int64_t finger, double x, double y, const std::vector<ui::HudElement>& elements, double css) {
        css_ = css > 0 ? css : 1;
        auto find = [&](const char* id) -> const ui::Rect* { for(const auto& e : elements) if(e.id == id) return &e.rect; return nullptr; };
        auto near = [&](const char* id) { const ui::Rect* r = find(id); return r && std::hypot(x - r->x - r->w / 2, y - r->y - r->h / 2) < r->w * .75; };
        auto inside = [&](const char* id) { const ui::Rect* r = find(id); return r && r->contains(x, y); };
        // HUD buttons and the separate SPRINT / SLIDE / RELOAD controls take the touch first (isInteractiveTouch)
        if(inside("pauseButton")) return "pause";
        if(inside("fovToggle")) return "settings";
        if(inside("mapbutton")) return "map";
        if(inside("touchSprint")) { owned_[finger] = "touchSprint"; sprint_held_ = true; ++sprint_presses_; return "touchSprint"; }
        if(inside("touchCrouch")) { owned_[finger] = "touchCrouch"; taps_ |= physics::key_slide; return "touchCrouch"; }   // keys.ControlLeft = true; beginSlide()
        if(inside("touchReload")) { owned_[finger] = "touchReload"; taps_ |= combat::key_reload; return "touchReload"; }
        if(!move_ && near("stick")) { move_ = finger; stick_rect_ = *find("stick"); move_stick(x, y); return "stick"; }
        if(near("fire")) { owned_[finger] = "fire"; taps_ |= combat::key_fire; return "fire"; }
        if(near("jump")) { owned_[finger] = "jump"; taps_ |= physics::key_jump; return "jump"; }
        if(near("swap")) { owned_[finger] = "swap"; taps_ |= combat::key_switch; return "swap"; }
        if(!look_) { look_ = finger; last_x_ = x; last_y_ = y; }
        return {};
    }
    void move(std::int64_t finger, double x, double y, const Prefs& p) {
        if(move_ && *move_ == finger) move_stick(x, y);
        else if(look_ && *look_ == finger) {   // aim((dx) * 1.5 * sensitivity / 100, (dy) * 1.5 * sensitivity / 100 * invert)
            const double k = 1.5 * p.sensitivity / 100 / css_;
            look_dx_ += (x - last_x_) * k; look_dy_ += (y - last_y_) * k * (p.invert ? -1 : 1);
            last_x_ = x; last_y_ = y;
        }
    }
    void up(std::int64_t finger) {
        if(move_ && *move_ == finger) { move_.reset(); stick_x_ = stick_y_ = 0; }
        if(look_ && *look_ == finger) look_.reset();
        if(auto it = owned_.find(finger); it != owned_.end()) { if(it->second == "touchSprint") sprint_held_ = false; owned_.erase(it); }
    }
    void reset() { *this = TouchControls{}; }   // resetTouchInput()
    // Keys held by fingers this tick (fire, jump, crouch for the SLIDE button) plus taps since the last tick.
    std::uint32_t keys() const {
        std::uint32_t k = taps_;
        for(const auto& [f, id] : owned_) {
            if(id == "fire") k |= combat::key_fire;
            if(id == "jump") k |= physics::key_jump;
            if(id == "touchCrouch") k |= physics::key_crouch;
        }
        return k;
    }
    void end_tick() { taps_ = 0; look_dx_ = look_dy_ = 0; }
    bool sprint_held() const { return sprint_held_; }
    int take_sprint_presses() { const int n = sprint_presses_; sprint_presses_ = 0; return n; }
    double stick_x() const { return stick_x_; }
    double stick_y() const { return stick_y_; }
    double look_dx() const { return look_dx_; }
    double look_dy() const { return look_dy_; }
    bool active() const { return move_ || look_ || !owned_.empty(); }
    std::set<std::string> pressed() const {
        std::set<std::string> s;
        for(const auto& [f, id] : owned_) s.insert(id);
        if(move_) s.insert("stick");
        return s;
    }
private:
    void move_stick(double x, double y) {   // moveStick(): deflection over 47 % of the pad, clamped to the unit circle
        const ui::Rect& r = stick_rect_;
        const double dx = (x - r.x - r.w / 2) / (r.w * .47), dy = (y - r.y - r.h / 2) / (r.h * .47), v = std::max(1.0, std::hypot(dx, dy));
        stick_x_ = dx / v; stick_y_ = dy / v;
    }
    std::optional<std::int64_t> move_, look_;
    std::map<std::int64_t, std::string> owned_;
    ui::Rect stick_rect_;
    double stick_x_{}, stick_y_{}, last_x_{}, last_y_{}, look_dx_{}, look_dy_{}, css_{1};
    std::uint32_t taps_{};
    bool sprint_held_{};
    int sprint_presses_{};
};

}  // namespace cat
