#pragma once
// N5 HUD and practice-target geometry. GPU/SDL independent: produces coloured triangles.
//
// The legacy HUD is HTML/CSS drawn by the browser (legacy/index.html 627, updateHud 14620-14640); it
// has no geometry or font of its own to port. The native HUD shows the same information (health,
// magazine / clip or the reload state, weapon name, eliminations and deaths, crosshair, hit marker,
// damage flash) with a built-in 5 x 7 bitmap font. Layout and typeface are native, not a copy of the
// CSS. A real UI system and the legacy menus are a later milestone.
#include "cat/combat.hpp"
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace cat::hud {
struct Vertex { float x, y, r, g, b, a; };          // normalized device coordinates (x right, y down)
struct SolidVertex { float x, y, z, r, g, b, a; };  // world space
struct Color { float r{1}, g{1}, b{1}, a{1}; };

class Canvas {   // pixel coordinates, origin top-left
public:
    Canvas(double width, double height) : width_(width), height_(height) {}
    double width() const { return width_; }
    double height() const { return height_; }
    void clear() { vertices_.clear(); }
    void rect(double x, double y, double w, double h, Color c);
    void line(double x0, double y0, double x1, double y1, double thickness, Color c);
    // 5 x 7 dot font (6 x 9 cell). `dot` = pixel size of one font dot. Returns the x after the text.
    double text(double x, double y, double dot, std::string_view s, Color c);
    static double text_width(std::string_view s, double dot);
    void text_right(double right, double y, double dot, std::string_view s, Color c) { text(right - text_width(s, dot), y, dot, s, c); }
    void text_center(double center, double y, double dot, std::string_view s, Color c) { text(center - text_width(s, dot) / 2, y, dot, s, c); }
    const std::vector<Vertex>& vertices() const { return vertices_; }
private:
    void quad(const std::array<std::array<double, 2>, 4>& p, Color c);
    double width_, height_;
    std::vector<Vertex> vertices_;
};
bool has_glyph(char c);

struct State {
    double health{100};
    int ammo{}, clip{};
    double reloading{}, reload_time{1};   // seconds left / full duration of the current reload
    std::string weapon, other_weapon, map_title, notice;
    std::array<float, 3> weapon_color{1, 1, 1};
    std::uint64_t frags{}, deaths{};
    int targets{};
    // N6 waves: wave 0 = no wave play (practice / no enemies)
    int wave{}, wave_remaining{}, enemies_alive{};
    double wave_cooldown{-1};  // >= 0: seconds until the next wave
    bool final_five{};         // five or fewer left: outlines through walls are on
    double burning{};          // seconds of fireball burn left
    double damage_flash{};     // 0..1
    double hit_marker{};       // 0..1, fades after a hit on a target
    bool hit_was_kill{}, hit_was_head{};
    double respawn_grace{};
    double notice_alpha{};
    std::string pause_title, pause_help;   // N6.5: shown while an interactive game is paused (empty = not paused)
};
void build(Canvas& canvas, const State& state);

// Practice targets: the two legacy hit boxes of each target as flat-shaded boxes, tinted by remaining
// health, white while hit. Enemies (type >= 0) are skipped: they are drawn as creatures (N6).
void target_boxes(const std::vector<combat::Target>& targets, std::vector<SolidVertex>& out);
}  // namespace cat::hud
