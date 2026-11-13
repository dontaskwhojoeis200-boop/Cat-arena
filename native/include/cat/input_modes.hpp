#pragma once
// N7: control-mode detection (mobile-hud.js noteInput / applyInputMode) and the pointer-capture state
// machine of the legacy page plus the v0.18 fix branch (reference/patches, Android / DeX mouse):
//   * AUTO mode follows the last input used (touch vs keyboard / mouse); an external mouse on a touch
//     device switches to keyboard & mouse (dispatchGenericMotionEvent -> noteInput('kbm')).
//   * Capture is requested only by a deliberate action (click / ENTER THE ARENA / resume), released by
//     Esc, by opening a menu (pause, settings, HUD editor) and when the window loses focus; it is
//     re-requested when gameplay resumes. Touch mode never captures.
// SDL-free; the shell maps the result to SDL_SetWindowRelativeMouseMode (Android: pointer capture).
#include <cstdint>
#include <string>

namespace cat {

enum class InputKind : std::uint8_t { touch, kbm };

class InputModeTracker {
public:
    explicit InputModeTracker(InputKind initial = InputKind::kbm) : detected_(initial) {}
    void set_setting(const std::string& mode) { setting_ = mode; }   // "auto" | "touch" | "kbm"
    bool note(InputKind k);                  // input seen; returns true when the effective mode changed
    InputKind detected() const { return detected_; }
    bool touch() const { return setting_ == "auto" ? detected_ == InputKind::touch : setting_ == "touch"; }
    const char* status() const { return touch() ? "ACTIVE: TOUCH" : "ACTIVE: KEYBOARD & MOUSE"; }
private:
    std::string setting_{"auto"};
    InputKind detected_;
};

class PointerCapture {
public:
    void set_game_active(bool v) { active_ = v; if(!v) requested_ = false; }
    void set_menu_open(bool v) { menu_ = v; if(v) requested_ = false; }       // releaseCapture when a menu opens
    void set_focused(bool v) { focused_ = v; if(!v) requested_ = false; }    // blur releases
    void set_touch_mode(bool v) { touch_ = v; if(v) requested_ = false; }
    void request() { if(active_ && !menu_ && !touch_) requested_ = true; }    // attemptLock() (user gesture)
    void release() { requested_ = false; }                                    // Esc
    bool wanted() const { return requested_ && active_ && !menu_ && focused_ && !touch_; }
    void platform_result(bool captured) { captured_ = captured; if(!captured && wanted()) ++failures_; }
    bool captured() const { return captured_; }
    int failures() const { return failures_; }
    std::string status() const {
        if(touch_) return "TOUCH CONTROLS";
        if(captured_) return "MOUSE LOCKED · ESC TO RELEASE";
        if(wanted()) return "REQUESTING MOUSE CAPTURE";
        return "MOUSE NOT CAPTURED · CLICK TO PLAY";
    }
private:
    bool active_{}, menu_{}, focused_{true}, touch_{}, requested_{}, captured_{};
    int failures_{};
};

inline bool InputModeTracker::note(InputKind k) {
    const bool before = touch();
    detected_ = k;
    return touch() != before;
}

}  // namespace cat
