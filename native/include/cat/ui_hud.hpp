#pragma once
// N7 in-game HUD in the legacy layout (index.html 627, CSS restyle 525-625 / 16700+, mobile-hud.js):
// brand, score, wave box, health / speed, weapon box, stance, crosshair styles, performance overlay,
// map and settings buttons, notices, and the touch controls. Default positions are the element boxes
// measured in the legacy page at 1280 x 720 (docs/n7-evidence/legacy-ui/legacy_ui_layout.json); the
// HUD editor stores per-element position, size and opacity as the legacy hudRecords do.
#include "cat/game_settings.hpp"
#include "cat/ui.hpp"

#include <set>
#include <string>
#include <vector>

namespace cat::ui {

struct HudInfo {
    double health{100}, speed{};
    int ammo{}, clip{};
    double reloading{}, reload_time{1};
    std::string weapon, other_weapon, map_title, stance{"RUN"};
    std::uint64_t frags{}, deaths{};
    int wave{}, wave_remaining{}, enemies_alive{};
    double wave_cooldown{-1};
    bool final_five{};
    double burning{}, damage_flash{}, hit_marker{}, respawn_grace{};
    bool hit_was_kill{}, hit_was_head{};
    std::string notice;
    double notice_alpha{};
    bool dead{};
};
struct PerfLine { std::string label, value; };

struct HudView {
    const GameSettings* settings{};
    double scale{1};                   // UI pixels per CSS pixel (display scale)
    Rect safe;                         // safe area in pixels (notches, rounded corners)
    bool touch{};                      // touch controls instead of keyboard / mouse hints
    bool captured{};                   // mouse captured (status line)
    std::vector<PerfLine> perf;        // empty = performance overlay hidden
    bool editing{};                    // HUD editor: outlines every editable element
    std::string selected;              // HUD editor selection
    std::set<std::string> pressed;     // touch buttons held
    double knob_x{}, knob_y{};         // joystick deflection, -1..1
    bool show_mouse_status{true};
};

struct HudElement { std::string id; Rect rect; };

// Draws the HUD; returns the boxes of the editable / touchable elements (HUD editor, touch input).
std::vector<HudElement> build_hud(Canvas& canvas, const HudInfo& info, const HudView& view);
// Where an element is drawn without a custom placement (pixels).
Rect default_hud_rect(std::string_view id, const HudView& view, double width, double height);
Rect placed_hud_rect(std::string_view id, const HudView& view, double width, double height);
// One crosshair shape of setCrosshair() (14869), drawn in a 48 x 48 CSS-pixel box.
void draw_crosshair(Canvas& canvas, double cx, double cy, double scale, std::string_view style, Color color);
Color css_color(std::string_view hex);   // "#rrggbb"

}  // namespace cat::ui
