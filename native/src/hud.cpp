// N5 HUD: see cat/hud.hpp.
#include "cat/hud.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace cat::hud {
namespace {
struct Glyph { char c; const char* rows[7]; };
// 5 x 7 dot matrix, one string per row, '1' = lit.
constexpr Glyph kFont[] = {
    {'0', {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}}, {'1', {"00100", "01100", "00100", "00100", "00100", "00100", "01110"}},
    {'2', {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}}, {'3', {"11111", "00010", "00100", "00010", "00001", "10001", "01110"}},
    {'4', {"00010", "00110", "01010", "10010", "11111", "00010", "00010"}}, {'5', {"11111", "10000", "11110", "00001", "00001", "10001", "01110"}},
    {'6', {"00110", "01000", "10000", "11110", "10001", "10001", "01110"}}, {'7', {"11111", "00001", "00010", "00100", "01000", "01000", "01000"}},
    {'8', {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}}, {'9', {"01110", "10001", "10001", "01111", "00001", "00010", "01100"}},
    {'A', {"01110", "10001", "10001", "11111", "10001", "10001", "10001"}}, {'B', {"11110", "10001", "10001", "11110", "10001", "10001", "11110"}},
    {'C', {"01110", "10001", "10000", "10000", "10000", "10001", "01110"}}, {'D', {"11100", "10010", "10001", "10001", "10001", "10010", "11100"}},
    {'E', {"11111", "10000", "10000", "11110", "10000", "10000", "11111"}}, {'F', {"11111", "10000", "10000", "11110", "10000", "10000", "10000"}},
    {'G', {"01110", "10001", "10000", "10111", "10001", "10001", "01111"}}, {'H', {"10001", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {'I', {"01110", "00100", "00100", "00100", "00100", "00100", "01110"}}, {'J', {"00111", "00010", "00010", "00010", "00010", "10010", "01100"}},
    {'K', {"10001", "10010", "10100", "11000", "10100", "10010", "10001"}}, {'L', {"10000", "10000", "10000", "10000", "10000", "10000", "11111"}},
    {'M', {"10001", "11011", "10101", "10101", "10001", "10001", "10001"}}, {'N', {"10001", "10001", "11001", "10101", "10011", "10001", "10001"}},
    {'O', {"01110", "10001", "10001", "10001", "10001", "10001", "01110"}}, {'P', {"11110", "10001", "10001", "11110", "10000", "10000", "10000"}},
    {'Q', {"01110", "10001", "10001", "10001", "10101", "10010", "01101"}}, {'R', {"11110", "10001", "10001", "11110", "10100", "10010", "10001"}},
    {'S', {"01111", "10000", "10000", "01110", "00001", "00001", "11110"}}, {'T', {"11111", "00100", "00100", "00100", "00100", "00100", "00100"}},
    {'U', {"10001", "10001", "10001", "10001", "10001", "10001", "01110"}}, {'V', {"10001", "10001", "10001", "10001", "10001", "01010", "00100"}},
    {'W', {"10001", "10001", "10001", "10101", "10101", "10101", "01010"}}, {'X', {"10001", "10001", "01010", "00100", "01010", "10001", "10001"}},
    {'Y', {"10001", "10001", "10001", "01010", "00100", "00100", "00100"}}, {'Z', {"11111", "00001", "00010", "00100", "01000", "10000", "11111"}},
    {'.', {"00000", "00000", "00000", "00000", "00000", "01100", "01100"}}, {',', {"00000", "00000", "00000", "00000", "01100", "00100", "01000"}},
    {':', {"00000", "01100", "01100", "00000", "01100", "01100", "00000"}}, {'/', {"00000", "00001", "00010", "00100", "01000", "10000", "00000"}},
    {'-', {"00000", "00000", "00000", "11111", "00000", "00000", "00000"}}, {'+', {"00000", "00100", "00100", "11111", "00100", "00100", "00000"}},
    {'%', {"11000", "11001", "00010", "00100", "01000", "10011", "00011"}}, {'!', {"00100", "00100", "00100", "00100", "00100", "00000", "00100"}},
    {'?', {"01110", "10001", "00001", "00010", "00100", "00000", "00100"}}, {'(', {"00010", "00100", "01000", "01000", "01000", "00100", "00010"}},
    {')', {"01000", "00100", "00010", "00010", "00010", "00100", "01000"}}, {'\'', {"00100", "00100", "01000", "00000", "00000", "00000", "00000"}},
    {'*', {"00000", "00100", "10101", "01110", "10101", "00100", "00000"}}, {'<', {"00010", "00100", "01000", "10000", "01000", "00100", "00010"}},
    {'>', {"01000", "00100", "00010", "00001", "00010", "00100", "01000"}}, {'=', {"00000", "00000", "11111", "00000", "11111", "00000", "00000"}},
    {'_', {"00000", "00000", "00000", "00000", "00000", "00000", "11111"}}, {'#', {"01010", "01010", "11111", "01010", "11111", "01010", "01010"}},
};
const Glyph* glyph(char c) {
    if(c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    for(const auto& g : kFont) if(g.c == c) return &g;
    return nullptr;
}
}  // namespace

bool has_glyph(char c) { return c == ' ' || glyph(c) != nullptr; }

void Canvas::quad(const std::array<std::array<double, 2>, 4>& p, Color c) {
    auto v = [&](std::size_t i) { return Vertex{static_cast<float>(p[i][0] / width_ * 2 - 1), static_cast<float>(p[i][1] / height_ * 2 - 1), c.r, c.g, c.b, c.a}; };
    for(const std::size_t i : {0U, 1U, 2U, 0U, 2U, 3U}) vertices_.push_back(v(i));
}
void Canvas::rect(double x, double y, double w, double h, Color c) { quad({{{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}}, c); }
void Canvas::line(double x0, double y0, double x1, double y1, double thickness, Color c) {
    const double dx = x1 - x0, dy = y1 - y0, len = std::hypot(dx, dy);
    if(len < 1e-9) return;
    const double nx = -dy / len * thickness * .5, ny = dx / len * thickness * .5;
    quad({{{x0 + nx, y0 + ny}, {x1 + nx, y1 + ny}, {x1 - nx, y1 - ny}, {x0 - nx, y0 - ny}}}, c);
}
double Canvas::text_width(std::string_view s, double dot) { return s.empty() ? 0 : (static_cast<double>(s.size()) * 6 - 1) * dot; }
double Canvas::text(double x, double y, double dot, std::string_view s, Color c) {
    for(const char ch : s) {
        if(const auto* g = glyph(ch)) {
            for(int row = 0; row < 7; ++row) {
                // merge horizontal runs of lit dots into one rectangle
                for(int col = 0; col < 5;) {
                    if(g->rows[row][col] != '1') { ++col; continue; }
                    int end = col;
                    while(end < 5 && g->rows[row][end] == '1') ++end;
                    rect(x + col * dot, y + row * dot, (end - col) * dot, dot, c);
                    col = end;
                }
            }
        } else if(ch != ' ') rect(x + dot, y + 2 * dot, 3 * dot, 3 * dot, c);   // unknown character: a small block
        x += 6 * dot;
    }
    return x;
}

void build(Canvas& k, const State& s) {
    const double w = k.width(), h = k.height();
    const double u = std::max(1.0, std::floor(h / 360));   // HUD scale: 1 dot per 360 px of height
    const Color white{1, 1, 1, .95F}, dim{.75F, .82F, .86F, .85F}, shade{0, 0, 0, .45F}, amber{1, .78F, .3F, 1}, red{1, .25F, .2F, 1};
    // damage flash
    if(s.damage_flash > 0) k.rect(0, 0, w, h, {.8F, .05F, .03F, static_cast<float>(std::min(.45, s.damage_flash * .45))});
    // top line: map, score
    k.text(12 * u, 10 * u, 2 * u, s.map_title, dim);
    char buf[96];
    std::snprintf(buf, sizeof buf, "ELIMS %llu / DEATHS %llu", static_cast<unsigned long long>(s.frags), static_cast<unsigned long long>(s.deaths));
    k.text_right(w - 12 * u, 10 * u, 2 * u, buf, white);
    if(s.targets > 0) { std::snprintf(buf, sizeof buf, "PRACTICE TARGETS %d", s.targets); k.text(12 * u, 32 * u, 1 * u, buf, dim); }
    if(s.wave > 0) {   // wave box, top centre below the score line (legacy #waveDisplay: "WAVE n", "x REMAINING · y ACTIVE")
        const Color gold{1, .78F, .42F, 1}, tan{.89F, .73F, .54F, 1};
        k.rect(w / 2 - 96 * u, 26 * u, 192 * u, 36 * u, {.13F, .08F, .07F, .85F});
        std::snprintf(buf, sizeof buf, "WAVE %d", s.wave);
        k.text_center(w / 2, 30 * u, 3 * u, buf, gold);
        if(s.wave_cooldown >= 0) std::snprintf(buf, sizeof buf, "CLEARED - NEXT WAVE IN %d", static_cast<int>(std::ceil(s.wave_cooldown)));
        else std::snprintf(buf, sizeof buf, "%d REMAINING - %d ACTIVE", s.wave_remaining, s.enemies_alive);
        k.text_center(w / 2, 53 * u, 1 * u, buf, tan);
        if(s.final_five && s.wave_cooldown < 0) {   // #huntBanner
            k.rect(w / 2 - 92 * u, 68 * u, 184 * u, 15 * u, {.13F, .09F, .05F, .9F});
            k.text_center(w / 2, 72 * u, 1 * u, "! LAST FIVE - X-RAY TRACKING !", {1, .91F, .63F, 1});
        }
    }
    if(s.burning > 0) k.text_center(w / 2, h - 74 * u, 2 * u, "BURNING", red);
    // health, bottom left
    const double hx = 14 * u, hy = h - 46 * u;
    k.rect(hx - 6 * u, hy - 6 * u, 150 * u, 44 * u, shade);
    const Color hp_color = s.health <= 25 ? red : s.health <= 50 ? amber : Color{.55F, 1, .6F, 1};
    std::snprintf(buf, sizeof buf, "%d", static_cast<int>(std::ceil(std::max(0.0, s.health))));
    const double after = k.text(hx, hy, 4 * u, buf, hp_color);
    k.text(after + 4 * u, hy + 14 * u, 2 * u, "HEALTH", dim);
    k.rect(hx, hy + 32 * u, 136 * u, 3 * u, {1, 1, 1, .15F});
    k.rect(hx, hy + 32 * u, 136 * u * std::clamp(s.health / 100.0, 0.0, 1.0), 3 * u, hp_color);
    if(s.respawn_grace > 0) k.text(hx, hy - 20 * u, 2 * u, "SPAWN PROTECTION", amber);
    // ammo and weapon, bottom right
    const double ax = w - 14 * u, ay = h - 46 * u;
    k.rect(ax - 190 * u, ay - 24 * u, 196 * u, 62 * u, shade);
    k.text_right(ax, ay - 18 * u, 2 * u, s.weapon, {s.weapon_color[0], s.weapon_color[1], s.weapon_color[2], 1});
    std::snprintf(buf, sizeof buf, "/ %d", s.clip);
    const double clip_w = Canvas::text_width(buf, 2 * u);
    k.text_right(ax, ay + 14 * u, 2 * u, buf, dim);
    if(s.reloading > 0) k.text_right(ax - clip_w - 6 * u, ay, 4 * u, "...", amber);
    else { std::snprintf(buf, sizeof buf, "%d", s.ammo); k.text_right(ax - clip_w - 6 * u, ay, 4 * u, buf, s.ammo == 0 ? red : white); }
    if(s.reloading > 0) {
        k.rect(ax - 180 * u, ay + 32 * u, 180 * u, 3 * u, {1, 1, 1, .15F});
        k.rect(ax - 180 * u, ay + 32 * u, 180 * u * std::clamp(1 - s.reloading / std::max(.01, s.reload_time), 0.0, 1.0), 3 * u, amber);
        k.text_center(w / 2, h * .62, 2 * u, "RELOADING", amber);
    }
    if(!s.other_weapon.empty()) { std::string other = "Q: " + s.other_weapon; k.text_right(ax, ay - 36 * u, 1 * u, other, dim); }
    // crosshair: four ticks around a centre dot
    const double cx = std::floor(w / 2), cy = std::floor(h / 2), gap = 4 * u, len = 6 * u, t = std::max(1.0, u);
    const Color cross{1, 1, 1, .85F};
    k.rect(cx - gap - len, cy - t / 2, len, t, cross); k.rect(cx + gap, cy - t / 2, len, t, cross);
    k.rect(cx - t / 2, cy - gap - len, t, len, cross); k.rect(cx - t / 2, cy + gap, t, len, cross);
    k.rect(cx - t / 2, cy - t / 2, t, t, cross);
    // hit marker: an X that fades (the legacy combatMarker)
    if(s.hit_marker > 0) {
        const Color m = s.hit_was_kill ? Color{1, .3F, .25F, static_cast<float>(s.hit_marker)} : s.hit_was_head ? Color{1, .85F, .3F, static_cast<float>(s.hit_marker)} : Color{1, 1, 1, static_cast<float>(s.hit_marker)};
        const double a = 6 * u, b = 13 * u;
        for(const double sx : {-1.0, 1.0}) for(const double sy : {-1.0, 1.0}) k.line(cx + sx * a, cy + sy * a, cx + sx * b, cy + sy * b, 2 * u, m);
    }
    if(!s.notice.empty() && s.notice_alpha > 0) k.text_center(w / 2, h * .70, 2 * u, s.notice, {1, .9F, .55F, static_cast<float>(std::min(1.0, s.notice_alpha))});
    if(!s.pause_title.empty()) {   // N6.5 pause panel: title, then one line of help per '|'-separated part
        std::vector<std::string> lines;
        for(std::size_t a = 0; a <= s.pause_help.size();) { const auto b = std::min(s.pause_help.find('|', a), s.pause_help.size()); if(b > a) lines.push_back(s.pause_help.substr(a, b - a)); a = b + 1; }
        const double panel_h = (36 + 12 * static_cast<double>(lines.size())) * u;
        k.rect(w / 2 - 200 * u, h * .5 - panel_h / 2, 400 * u, panel_h, {.08F, .05F, .05F, .86F});
        k.text_center(w / 2, h * .5 - panel_h / 2 + 9 * u, 2.5 * u, s.pause_title, {1, .78F, .42F, 1});
        for(std::size_t i = 0; i < lines.size(); ++i) k.text_center(w / 2, h * .5 - panel_h / 2 + (34 + 12 * static_cast<double>(i)) * u, 1 * u, lines[i], {.92F, .86F, .76F, 1});
    }
}

void target_boxes(const std::vector<combat::Target>& targets, std::vector<SolidVertex>& out) {
    out.clear();
    for(const auto& t : targets) {
        if(t.type >= 0) continue;
        const double f = std::clamp(t.hp / std::max(1.0, t.max_hp), 0.0, 1.0), flash = std::clamp(t.hit_flash / .21, 0.0, 1.0);
        const auto boxes = t.boxes();
        for(std::size_t k = 0; k < 2; ++k) {
            const auto& b = boxes[k];
            // body: red (low health) to class colour; head: lighter
            const double base[3] = {.85 - .45 * f, .25 + .45 * f, t.cls == combat::TargetClass::heavy ? .25 : t.cls == combat::TargetClass::fast ? .75 : .45};
            const double lift = k == 1 ? .18 : 0;
            const double px[8][3] = {{b.x0, b.y0, b.z0}, {b.x1, b.y0, b.z0}, {b.x1, b.y1, b.z0}, {b.x0, b.y1, b.z0}, {b.x0, b.y0, b.z1}, {b.x1, b.y0, b.z1}, {b.x1, b.y1, b.z1}, {b.x0, b.y1, b.z1}};
            const int faces[6][4] = {{0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}, {4, 5, 6, 7}, {3, 2, 1, 0}};
            const double shade[6] = {.72, .86, .62, .78, 1.0, .45};
            for(int face = 0; face < 6; ++face) {
                const double s = shade[face];
                const float r = static_cast<float>(std::min(1.0, (base[0] + lift) * s + flash)), g = static_cast<float>(std::min(1.0, (base[1] + lift) * s + flash)), bl = static_cast<float>(std::min(1.0, (base[2] + lift) * s + flash));
                for(const int i : {0, 1, 2, 0, 2, 3}) { const auto& p = px[faces[face][i]]; out.push_back({static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2]), r, g, bl, 1}); }
            }
        }
    }
}
}  // namespace cat::hud
