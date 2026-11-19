// N7 in-game HUD (see include/cat/ui_hud.hpp).
#include "cat/ui_hud.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cat::ui {
namespace {
constexpr double kPi = 3.141592653589793;
enum class Anchor { tl, tc, tr, bl, br, cc };
struct Box { const char* id; Anchor anchor; double x, y, w, h; };   // CSS px at 1280 x 720, HUD size 115 %
// Measured in the legacy page (legacy_ui_layout.json, hud_kbm / hud_touch). Native changes: the map /
// settings buttons and the mouse status no longer overlap (the legacy draws them on top of each other at
// this size), and touch mode gets a reachable pause button.
const Box kKeyboard[] = {
    {"brand", Anchor::tl, 16, 10, 300, 30}, {"score", Anchor::tr, 1051.7, 10, 212.3, 44}, {"waveDisplay", Anchor::tc, 546.3, 65, 187.4, 54},
    {"leftStats", Anchor::bl, 14, 605.7, 319.7, 102.3}, {"weaponBox", Anchor::br, 1038.3, 611.4, 227.7, 96.6}, {"stanceLabel", Anchor::bl, 19, 544.6, 49.1, 26.4},
    {"mouseStatus", Anchor::tl, 16, 46, 232, 22}, {"mapbutton", Anchor::tl, 16, 74, 214, 34}, {"fovToggle", Anchor::tl, 236, 74, 168, 34},
    {"perfOverlay", Anchor::tl, 16, 119, 195, 218.5}, {"pauseButton", Anchor::tr, 1173, 54, 91, 30}};
const Box kTouch[] = {
    {"brand", Anchor::tl, 10, 10, 97.8, 30}, {"score", Anchor::tc, 566.2, 10, 147.6, 34.9}, {"waveDisplay", Anchor::tc, 552, 55, 176.1, 43},
    {"leftStats", Anchor::bl, 10, 660, 159.5, 52}, {"weaponBox", Anchor::br, 1148, 661, 122, 51}, {"stanceLabel", Anchor::bl, 10, 626, 49.1, 26.4},
    {"perfOverlay", Anchor::tl, 10, 56, 190, 45.1}, {"mapbutton", Anchor::tr, 1060, 10, 116, 30}, {"fovToggle", Anchor::tr, 1180, 10, 90, 30}, {"pauseButton", Anchor::tr, 1180, 46, 90, 30},
    {"stick", Anchor::bl, 26, 518, 125, 125}, {"fire", Anchor::br, 1214, 564, 56, 56}, {"jump", Anchor::br, 1101, 561, 56, 56}, {"swap", Anchor::br, 1101, 484, 56, 56},
    {"touchSprint", Anchor::bl, 10, 443, 56, 56}, {"touchCrouch", Anchor::bl, 76, 443, 56, 56}, {"touchReload", Anchor::br, 1210, 453, 56, 56}};
bool touch_button(std::string_view id) { return id == "stick" || id == "fire" || id == "jump" || id == "swap" || id == "touchSprint" || id == "touchCrouch" || id == "touchReload" || id == "touchRevive"; }

// The "iron, bone, blood, brass" palette of the legacy restyle
const Color kPanel = Color::hex(0x0d0f11, .90f), kPanel2 = Color::hex(0x161719, .90f), kLine = Color::hex(0xa8afb5, .42f), kText = Color::hex(0xdce0e3);
const Color kBright = Color::hex(0xf1f3f4), kMuted = Color::hex(0xb7bdc4), kWarm = Color::hex(0x9e5b50), kBrass = Color::hex(0xd8bb83);

void panel(Canvas& c, const Rect& r, double k, float alpha, bool pointed = false) {
    const auto shape = pointed ? Canvas::pointed_shape(r, std::min(r.h * .5, 12 * k)) : Canvas::cut_shape(r, 9 * k);
    c.shape(shape, kPanel2.with_alpha(kPanel2.a * alpha), kPanel.with_alpha(kPanel.a * alpha), std::max(1.0, k), kLine.with_alpha(kLine.a * alpha));
}
TextStyle style(Face f, double size, Color col, double spacing = 0, Align a = Align::left, double weight = 0) {
    TextStyle s; s.face = f; s.size = size; s.color = col; s.spacing = spacing; s.align = a; s.weight = weight; s.shadow = Color::hex(0x000000, .55f * col.a);
    return s;
}
}  // namespace

Color css_color(std::string_view h) {
    if(h.size() != 7 || h[0] != '#') return kText;
    return Color::hex(static_cast<std::uint32_t>(std::stoul(std::string(h.substr(1)), nullptr, 16)));
}

void draw_crosshair(Canvas& c, double cx, double cy, double k, std::string_view s, Color col) {
    auto P = [&](double x, double y) { return std::array<double, 2>{cx + (x - 24) * k, cy + (y - 24) * k}; };
    auto seg = [&](double x0, double y0, double x1, double y1, double w) { const auto a = P(x0, y0), b = P(x1, y1); c.line(a[0], a[1], b[0], b[1], w * k, col); };
    auto dot = [&](double r) { c.circle(cx, cy, r * k, col, 20); };
    auto circle = [&](double r, double w) { c.ring(cx, cy, r * k, w * k, col, 40); };
    if(s == "classic") { seg(24, 5, 24, 17, 3); seg(24, 31, 24, 43, 3); seg(5, 24, 17, 24, 3); seg(31, 24, 43, 24, 3); }
    else if(s == "dot") dot(4.2);
    else if(s == "plus") { seg(24, 10, 24, 38, 2.4); seg(10, 24, 38, 24, 2.4); }
    else if(s == "ring") { circle(13.5, 2.5); dot(3.1); }
    else if(s == "bullseye") { circle(10.5, 2.2); dot(2.6); seg(24, 6, 24, 12, 2.3); seg(24, 36, 24, 42, 2.3); seg(6, 24, 12, 24, 2.3); seg(36, 24, 42, 24, 2.3); }
    else if(s == "circle") circle(11.5, 2.4);
    else if(s == "t") { seg(24, 7, 24, 18, 3); seg(9, 24, 19, 24, 3); seg(29, 24, 39, 24, 3); }
}

Rect default_hud_rect(std::string_view id, const HudView& v, double width, double height) {
    const double hud = v.settings ? v.settings->number("hudScale") / 115.0 : 1.0;
    const double touch_size = v.settings ? v.settings->number("touchSize") / 100.0 : 1.0;
    const Rect safe = v.safe.w > 0 ? v.safe : Rect{0, 0, width, height};
    const Box* first = v.touch ? std::begin(kTouch) : std::begin(kKeyboard);
    const Box* last = v.touch ? std::end(kTouch) : std::end(kKeyboard);
    for(const Box* b = first; b != last; ++b) {
        if(id != b->id) continue;
        const double k = v.scale * (touch_button(id) ? touch_size : hud);
        const double w = b->w * k, h = b->h * k;
        switch(b->anchor) {
            case Anchor::tl: return {safe.x + b->x * v.scale, safe.y + b->y * v.scale, w, h};
            case Anchor::tr: return {safe.right() - (1280 - b->x - b->w) * v.scale - w, safe.y + b->y * v.scale, w, h};
            case Anchor::tc: return {safe.x + safe.w / 2 + (b->x + b->w / 2 - 640) * v.scale - w / 2, safe.y + b->y * v.scale, w, h};
            case Anchor::bl: return {safe.x + b->x * v.scale, safe.bottom() - (720 - b->y - b->h) * v.scale - h, w, h};
            case Anchor::br: return {safe.right() - (1280 - b->x - b->w) * v.scale - w, safe.bottom() - (720 - b->y - b->h) * v.scale - h, w, h};
            case Anchor::cc: return {width / 2 - w / 2, height / 2 - h / 2, w, h};
        }
    }
    return {};
}

Rect placed_hud_rect(std::string_view id, const HudView& v, double width, double height) {
    const Rect base = default_hud_rect(id, v, width, height);
    if(!v.settings) return base;
    const auto& layout = v.settings->hud_layout();
    const auto it = layout.find(std::string(id));
    if(it == layout.end()) return base;
    const double s = it->second.size / 100.0, w = base.w * s, h = base.h * s;   // hudApplyOne: position clamped inside the viewport
    return {std::clamp(it->second.x * width, 0.0, std::max(0.0, width - w)), std::clamp(it->second.y * height, 0.0, std::max(0.0, height - h)), w, h};
}

std::vector<HudElement> build_hud(Canvas& c, const HudInfo& in, const HudView& v) {
    const double W = c.width(), H = c.height();
    const GameSettings* s = v.settings;
    std::vector<HudElement> out;
    auto opacity = [&](std::string_view id) -> float {
        if(!s) return 1.f;
        const auto it = s->hud_layout().find(std::string(id));
        return it == s->hud_layout().end() ? 1.f : static_cast<float>(it->second.opacity / 100.0);
    };
    auto place = [&](std::string_view id) { const Rect r = placed_hud_rect(id, v, W, H); out.push_back({std::string(id), r}); return r; };
    auto k_of = [&](const Rect& r, std::string_view id) { const Rect d = default_hud_rect(id, v, W, H); const double hud = s ? s->number("hudScale") / 115.0 : 1.0; return d.w > 0 ? v.scale * hud * r.w / d.w : v.scale; };

    // damage flash (#flash): red vignette, stronger for direct hits
    if(in.damage_flash > 0) c.rect({0, 0, W, H}, Color::hex(0x9e1b1b, static_cast<float>(std::min(.45, in.damage_flash * .45))));

    // brand
    {
        const Rect r = place("brand"); const float a = opacity("brand"); const double k = k_of(r, "brand");
        panel(c, r, k, a, true);
        TextStyle t = style(Face::serif_regular, 11 * k, kText.with_alpha(a), .11);
        double x = r.x + 14 * k;
        t.color = kBright.with_alpha(a); x += c.text(x, r.y + r.h * .66, "CAT / ARENA", t);
        if(!v.touch && !in.map_title.empty()) { t.color = kMuted.with_alpha(a); c.text(x + 6 * k, r.y + r.h * .66, in.map_title, t); }
    }
    // score
    if(!s || s->flag("showScore")) {
        const Rect r = place("score"); const float a = opacity("score"); const double k = k_of(r, "score");
        panel(c, r, k, a, true);
        TextStyle t = style(Face::serif, (v.touch ? 10 : 13.8) * k, kText.with_alpha(a), .12, Align::center);
        char b[96];
        std::snprintf(b, sizeof b, "ELIMS %llu   /   DEATHS %llu", static_cast<unsigned long long>(in.frags), static_cast<unsigned long long>(in.deaths));
        c.text(r.x + r.w / 2, r.y + r.h * .64, b, t);
    }
    // wave box (#waveDisplay)
    if(in.wave > 0) {
        const Rect r = place("waveDisplay"); const float a = opacity("waveDisplay"); const double k = k_of(r, "waveDisplay");
        panel(c, r, k, a);
        char b[96];
        std::snprintf(b, sizeof b, "WAVE %d", in.wave);
        c.text(r.x + r.w / 2, r.y + r.h * .48, b, style(Face::sans, (v.touch ? 15 : 21) * k, kBright.with_alpha(a), .06, Align::center, .2));
        if(in.wave_cooldown >= 0) std::snprintf(b, sizeof b, "NEXT WAVE IN %.1fs", in.wave_cooldown);
        else std::snprintf(b, sizeof b, "%d REMAINING · %d ACTIVE", in.wave_remaining, in.enemies_alive);
        c.text(r.x + r.w / 2, r.y + r.h * .82, b, style(Face::mono, (v.touch ? 8 : 10) * k, kBrass.with_alpha(a), .02, Align::center, .3));
    }
    // final five banner (#huntBanner)
    if(in.final_five) {
        const double k = v.scale;
        c.text(W / 2, (v.touch ? 112 : 136) * k, "⚠ LAST FIVE · X-RAY TRACKING ⚠", style(Face::mono, 11 * k, Color::hex(0xe7c27a), .14, Align::center, .3));
    }
    // health / speed (#leftStats)
    {
        const Rect r = place("leftStats"); const float a = opacity("leftStats"); const double k = k_of(r, "leftStats");
        panel(c, r, k, a);
        const double hp = std::max(0.0, in.health);
        const Color hpc = hp < 35 ? Color::hex(0xd47a6a) : kBright;
        const double x0 = r.x + (v.touch ? 10 : 18) * k;
        if(!v.touch) {   // sigil in place of the portrait
            const double cx = r.x + 46 * k, cy = r.y + r.h / 2, q = .62 * k;
            std::vector<std::array<double, 2>> sig{{11, 73}, {23, 17}, {46, 40}, {60, 35}, {74, 40}, {97, 17}, {109, 73}, {84, 78}, {70, 69}, {60, 75}, {50, 69}, {36, 78}};
            for(auto& p : sig) { p[0] = cx + (p[0] - 60) * q * .55; p[1] = cy + (p[1] - 50) * q * .55; }
            c.polyline(sig, 1.6 * k, kText.with_alpha(.8f * a), true);
        }
        const double tx = v.touch ? x0 + 36 * k : r.x + 88 * k;
        c.text(tx, r.y + r.h * .33, "HEALTH", style(Face::sans, (v.touch ? 9 : 13) * k, kMuted.with_alpha(a), .08));
        char b[32]; std::snprintf(b, sizeof b, "%d", static_cast<int>(std::ceil(hp)));
        c.text(tx, r.y + r.h * .80, b, style(Face::serif, (v.touch ? 25 : 44.8) * k, hpc.with_alpha(a), -.065));
        const Rect bar{v.touch ? r.x + 92 * k : r.x + 163 * k, r.y + (v.touch ? 8 : 26) * k, (v.touch ? 55 : 120) * k, (v.touch ? 7 : 15) * k};
        c.rect(bar, Color::hex(0x07090c, a));
        c.gradient_h({bar.x + k, bar.y + k, (bar.w - 2 * k) * std::clamp(hp / 100, 0.0, 1.0), bar.h - 2 * k}, Color::hex(0x8f969d, a), Color::hex(0xe7eaec, a));
        c.frame(bar, std::max(1.0, k * .8), kLine.with_alpha(a));
        if(!v.touch) {
            std::snprintf(b, sizeof b, "%.1f", in.speed);
            const double sx = bar.x;
            const double w = c.text(sx, r.y + r.h * .72, "SPEED ", style(Face::sans, 10 * k, kMuted.with_alpha(a), .05));
            c.text(sx + w, r.y + r.h * .72, b, style(Face::serif, 15 * k, kText.with_alpha(a)));
        }
    }
    // weapon box
    {
        const Rect r = place("weaponBox"); const float a = opacity("weaponBox"); const double k = k_of(r, "weaponBox");
        panel(c, r, k, a);
        const double right = r.right() - (v.touch ? 9 : 16) * k;
        c.text(right, r.y + (v.touch ? 15 : 24) * k, in.weapon, style(Face::serif_regular, (v.touch ? 10 : 12.65) * k, Color::hex(0xe4e6e8, a), .12, Align::right));
        char b[48];
        if(in.reloading > 0) {
            std::snprintf(b, sizeof b, "RELOADING %d%%", static_cast<int>(100 * (1 - in.reloading / std::max(.01, in.reload_time))));
            c.text(right, r.y + (v.touch ? 38 : 64) * k, b, style(Face::mono, (v.touch ? 11 : 15) * k, kBrass.with_alpha(a), .02, Align::right, .3));
        } else {
            std::snprintf(b, sizeof b, " /%d", in.clip);
            const TextStyle small = style(Face::serif, (v.touch ? 11 : 16) * k, kMuted.with_alpha(a), 0, Align::right);
            const double w = c.text(right, r.y + (v.touch ? 42 : 72) * k, b, small);
            std::snprintf(b, sizeof b, "%d", in.ammo);
            c.text(right - w, r.y + (v.touch ? 42 : 72) * k, b, style(Face::serif, (v.touch ? 21 : 36.8) * k, (in.ammo == 0 ? Color::hex(0xd47a6a) : kBright).with_alpha(a), -.04, Align::right));
        }
        if(!v.touch) c.text(right, r.y + r.h - 9 * k, "Q SWAP · " + in.other_weapon, style(Face::mono, 10 * k, kMuted.with_alpha(a), 0, Align::right, .25));
    }
    // stance (keyboard / mouse only: the legacy touch layout hides #stanceLabel, the crouch button shows it)
    if(!v.touch && (!s || s->flag("showStance"))) {
        const Rect r = place("stanceLabel"); const float a = opacity("stanceLabel"); const double k = k_of(r, "stanceLabel");
        const double w = std::max(r.w, c.measure(in.stance, style(Face::serif, 11.5 * k, kText, .12)) + 22 * k);
        panel(c, {r.x, r.y, w, r.h}, k, a, true);
        c.text(r.x + w / 2, r.y + r.h * .66, in.stance, style(Face::serif, 11.5 * k, kText.with_alpha(a), .12, Align::center));
    }
    // mouse status, map and settings buttons (keyboard / mouse)
    if(!v.touch) {
        if(v.show_mouse_status) {
            const Rect r = place("mouseStatus"); const double k = v.scale;
            c.text(r.x + 2 * k, r.y + 15 * k, v.captured ? "● MOUSE LOCKED · ESC TO RELEASE" : "● MOUSE NOT CAPTURED · CLICK TO PLAY", style(Face::mono, 10 * k, (v.captured ? kText : kBrass), 0, Align::left, .3));
            out.pop_back();
        }
        { const Rect r = place("mapbutton"); const float a = opacity("mapbutton"); const double k = k_of(r, "mapbutton");
          panel(c, r, k, a, true); c.text(r.x + r.w / 2, r.y + r.h * .64, "MAP: " + in.map_title + " ↻", style(Face::serif_regular, 11 * k, kText.with_alpha(a), .12, Align::center)); }
        { const Rect r = place("fovToggle"); const float a = opacity("fovToggle"); const double k = k_of(r, "fovToggle");
          panel(c, r, k, a, true); c.text(r.x + r.w / 2, r.y + r.h * .64, "⚙ SETTINGS · V", style(Face::serif_regular, 12 * k, kText.with_alpha(a), .12, Align::center)); }
    } else {
        { const Rect r = place("mapbutton"); const double k = k_of(r, "mapbutton"); panel(c, r, k, 1, true); c.text(r.x + r.w / 2, r.y + r.h * .64, "MAP ↻", style(Face::serif_regular, 11 * k, kText, .12, Align::center)); }
        { const Rect r = place("fovToggle"); const double k = k_of(r, "fovToggle"); panel(c, r, k, 1, true); c.text(r.x + r.w / 2, r.y + r.h * .64, "⚙ SETTINGS", style(Face::serif_regular, 10 * k, kText, .1, Align::center)); }
        { const Rect r = place("pauseButton"); const double k = k_of(r, "pauseButton"); panel(c, r, k, 1, true); c.text(r.x + r.w / 2, r.y + r.h * .64, "Ⅱ PAUSE", style(Face::serif_regular, 11 * k, kText, .12, Align::center)); }
    }
    // performance overlay
    if(!v.perf.empty()) {
        const Rect base = place("perfOverlay"); const float a = opacity("perfOverlay"); const double k = k_of(base, "perfOverlay");
        const double line = (v.touch ? 11 : 21) * k;
        const Rect r{base.x, base.y, base.w, (v.touch ? 22 : 32) * k + line * static_cast<double>(v.perf.size())};
        out.back().rect = r;
        panel(c, r, k, a);
        c.text(r.x + 12 * k, r.y + (v.touch ? 14 : 20) * k, "PERFORMANCE // LIVE", style(Face::mono, (v.touch ? 9 : 11) * k, kBrass.with_alpha(a), .06, Align::left, .4));
        double y = r.y + (v.touch ? 28 : 44) * k;
        for(const auto& p : v.perf) {
            c.text(r.x + 12 * k, y, p.label, style(Face::mono, (v.touch ? 9 : 12) * k, kText.with_alpha(a), 0, Align::left, .25));
            c.text(r.right() - 12 * k, y, p.value, style(Face::mono, (v.touch ? 9 : 12) * k, kBright.with_alpha(a), 0, Align::right, .25));
            y += line;
        }
    }
    // crosshair, hit marker
    {
        const std::string cs = s ? s->text("crosshairStyle") : "classic";
        const Color cc = css_color(s ? s->text("crosshairColor") : "#f2cf93");
        if(cs != "none") draw_crosshair(c, W / 2, H / 2, v.scale, cs, cc);
        if(in.hit_marker > 0 && (!s || s->flag("showHitFeedback"))) {   // #combatMarker
            const float a = static_cast<float>(std::min(1.0, in.hit_marker));
            const Color hc = in.hit_was_kill ? Color::hex(0xff6a5a, a) : in.hit_was_head ? Color::hex(0xffd36b, a) : Color::hex(0xf2f2f2, a);
            const double k = v.scale, d0 = 9 * k, d1 = 17 * k;
            for(const double ang : {kPi / 4, 3 * kPi / 4, 5 * kPi / 4, 7 * kPi / 4})
                c.line(W / 2 + std::cos(ang) * d0, H / 2 + std::sin(ang) * d0, W / 2 + std::cos(ang) * d1, H / 2 + std::sin(ang) * d1, 2.4 * k, hc);
        }
    }
    if(in.outgoing_time > 0 && (!s || s->flag("showHitFeedback"))) {
        char number[48];
        std::snprintf(number, sizeof number, "%.0f", std::round(in.outgoing_damage));
        const auto label = std::string(in.hit_was_head ? "CRITICAL  " : "DMG  ") + number;
        const auto alpha = static_cast<float>(std::clamp(in.outgoing_time / .55, 0.0, 1.0));
        const double rise = (1.0 - alpha) * 20 * v.scale;
        c.text(W * .57, H * .43 - rise, label,
               style(Face::mono, 14 * v.scale, Color::hex(in.hit_was_head ? 0xffd36b : 0xf2eee4, alpha), .04, Align::left, .4));
    }
    if(in.incoming_time > 0 && !in.incoming_text.empty()) {
        const auto alpha = static_cast<float>(std::clamp(in.incoming_time / .7, 0.0, 1.0));
        c.text(W * .5, H * .19 - (1.0 - alpha) * 10 * v.scale, in.incoming_text,
               style(Face::mono, 14 * v.scale, Color::hex(0xffa28c, alpha), .04, Align::center, .4));
    }
    // notices (#event), burn status, death notice
    if(!in.notice.empty() && in.notice_alpha > 0) {   // #event as measured at 1280 x 720: right 2 %, top 27 %, max-width 41 %, 3px double #bb8053
        const float a = static_cast<float>(std::min(1.0, in.notice_alpha * 1.5));
        const double k = v.scale, css_w = W / k;
        const double size = std::clamp(css_w * .035, 26.0, 52.0) * k * (v.touch ? .72 : 1);
        const TextStyle ts = style(Face::serif, size, Color::hex(0xedf0f1, a), .12, Align::right);
        const double pad_x = (v.touch ? 10 : 16) * k, pad_y = (v.touch ? 6 : 11) * k, max_w = W * .41 - 2 * pad_x;
        std::vector<std::string> lines; std::string line, word;
        auto flush_word = [&] {
            if(word.empty()) return;
            const std::string next = line.empty() ? word : line + " " + word;
            if(!line.empty() && c.measure(next, ts) > max_w) { lines.push_back(line); line = word; } else line = next;
            word.clear();
        };
        for(char ch : in.notice) { if(ch == ' ') flush_word(); else word += ch; }
        flush_word();
        if(!line.empty()) lines.push_back(line);
        double text_w = 0;
        for(const auto& l : lines) text_w = std::max(text_w, c.measure(l, ts));
        const double lh = size * 1.02, bw = text_w + 2 * pad_x, bh = lh * static_cast<double>(lines.size()) + 2 * pad_y;
        const Rect box{v.safe.right() - W * .02 - bw, v.safe.y + H * .27, bw, bh};
        c.gradient_h(box, Color::hex(0x24160b, .87f * a), Color::hex(0x100909, .93f * a));
        const Color brass = Color::hex(0xbb8053, a);
        c.frame(box, 1 * k, brass); c.frame(box.inset(2 * k), 1 * k, brass);
        double y = box.y + pad_y + size * .8;
        for(const auto& l : lines) { c.text(box.right() - pad_x, y, l, ts); y += lh; }
    }
    if(in.burning > 0) {
        char b[48]; std::snprintf(b, sizeof b, "BURNING · %.1fs", in.burning);
        c.text(W / 2, H * .62, b, style(Face::mono, 13 * v.scale, Color::hex(0xff9b5c), .1, Align::center, .35));
    }
    if(in.dead || in.respawn_grace > 2.2) c.text(W / 2, H * .42, "FRAGGED · RESPAWNING", style(Face::serif, 26 * v.scale, Color::hex(0xe8c0b8), .14, Align::center, .1));
    // keyboard control tips (#tip)
    if(s && s->flag("showControlTips") && !v.touch) {
        const auto layout = s->text("keyboardLayout");
        const auto movement = layout == "arrows" ? "ARROWS" : layout == "zqsd" ? "ZQSD" : "WASD";
        const auto fire = s->text("mouseFire") == "right" ? "RMB" : "LMB";
        const auto swap = layout == "zqsd" ? "TAB" : "Q/TAB";
        c.text(W / 2, H - 14 * v.scale, std::string(movement) + " MOVE · SHIFT SPRINT · SPACE JUMP · CTRL/C CROUCH-SLIDE · " +
               fire + " FIRE · R RELOAD · " + swap + " SWAP · V SETTINGS · ESC PAUSE",
               style(Face::mono, 10 * v.scale, kMuted, .02, Align::center, .25));
    }
    // touch controls
    if(v.touch) {
        const double opacity_setting = s ? s->number("touchOpacity") / 100.0 : .8;
        for(const char* id : {"stick", "touchSprint", "touchCrouch", "swap", "jump", "fire", "touchReload"}) {
            const Rect r = place(id);
            const float a = static_cast<float>(opacity_setting) * opacity(id);
            const bool down = v.pressed.count(id) > 0;
            const double k = r.w / (std::string_view(id) == "stick" ? 125.0 : 56.0);
            if(std::string_view(id) == "stick") {
                c.ring(r.x + r.w / 2, r.y + r.h / 2, r.w / 2 - 1.5 * k, 2 * k, kLine.with_alpha(.8f * a), 48);
                c.circle(r.x + r.w / 2, r.y + r.h / 2, r.w / 2 - 2.5 * k, Color::hex(0x0d0f11, .45f * a), 48);
                const double kx = r.x + r.w / 2 + v.knob_x * r.w * .32, ky = r.y + r.h / 2 + v.knob_y * r.h * .32;
                c.circle(kx, ky, 22 * k, Color::hex(0xaeb6bf, .57f * a), 32);
                c.text(r.x + r.w / 2, r.bottom() + 12 * k, "MOVE", style(Face::serif, 10 * k, kText.with_alpha(a), .05, Align::center));
                continue;
            }
            const bool fire = std::string_view(id) == "fire";
            c.circle(r.x + r.w / 2, r.y + r.h / 2, r.w / 2, (fire ? Color::hex(0x48212b, .75f) : Color::hex(0x0d0f11, .6f)).with_alpha((down ? 1.f : .75f) * a), 36);
            c.ring(r.x + r.w / 2, r.y + r.h / 2, r.w / 2 - k, 1.6 * k, (down ? kBright : kLine).with_alpha(a), 36);
            const char* label = fire ? "FIRE" : std::string_view(id) == "jump" ? "JUMP" : std::string_view(id) == "swap" ? "SWAP" : std::string_view(id) == "touchSprint" ? "SPRINT" : std::string_view(id) == "touchCrouch" ? "SLIDE" : "RELOAD";
            c.text(r.x + r.w / 2, r.y + r.h / 2 + (fire ? 5 : 3.5) * k, label, style(Face::serif, (fire ? 15 : 10) * k, kText.with_alpha(a), .05, Align::center));
        }
    }
    // HUD editor outlines
    if(v.editing) {
        for(const auto& e : out) {
            const bool sel = e.id == v.selected;
            c.frame(e.rect, sel ? 2.5 * v.scale : 1.2 * v.scale, sel ? Color::hex(0xe7c27a) : Color::hex(0xa8afb5, .7f));
        }
    }
    return out;
}

void build_coop_hud(Canvas& c, const CoopHud& in, const HudView& v) {
    const double k = v.scale, W = c.width(), H = c.height();
    // name tags: nearest drawn last (on top); smaller and fainter with distance, dimmed behind walls
    std::vector<const CoopTag*> order;
    for(const auto& t : in.tags) order.push_back(&t);
    std::sort(order.begin(), order.end(), [](const CoopTag* a, const CoopTag* b) { return a->distance > b->distance; });
    for(const CoopTag* t : order) {
        if(t->x < -60 * k || t->x > W + 60 * k || t->y < -40 * k || t->y > H + 40 * k) continue;
        const double near = std::clamp(1 - (t->distance - 6) / 60, .55, 1.0);
        const float a = static_cast<float>((t->occluded ? .5 : 1) * std::clamp(1.25 - t->distance / 90, .35, 1.0));
        std::string name = t->name.empty() ? "MARINE CAT" : t->name;
        for(char& ch : name) if(ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 32);
        const TextStyle ns = style(Face::serif_regular, 12.5 * k * near, (t->downed ? Color::hex(0xff9a8a) : kBright).with_alpha(a), .1, Align::center, .2);
        const double tw = std::max(c.measure(name, ns), 54 * k * near), bar_w = tw, y = t->y;
        const Rect box{t->x - tw / 2 - 8 * k, y - 18 * k * near, tw + 16 * k, (t->downed ? 34 : 26) * k * near};
        c.shape(Canvas::cut_shape(box, 5 * k * near), kPanel2.with_alpha(.55f * a), kPanel.with_alpha(.55f * a), std::max(1.0, k * .8), kLine.with_alpha(kLine.a * a));
        c.text(t->x, y - 4 * k * near, name, ns);
        const Rect bar{t->x - bar_w / 2, y + 1.5 * k * near, bar_w, 3 * k * near};
        c.rect(bar, Color::hex(0x07090c, .9f * a));
        const double hp = std::clamp(t->hp / 100, 0.0, 1.0);
        c.rect({bar.x, bar.y, bar.w * hp, bar.h}, (hp > .5 ? Color::hex(0xc9d2c4) : hp > .25 ? Color::hex(0xd8bb83) : Color::hex(0xd8705e)).with_alpha(a));
        if(t->downed) c.text(t->x, y + 12.5 * k * near, t->reviving ? "REVIVING" : "DOWN · HOLD F", style(Face::mono, 8.5 * k * near, Color::hex(0xffb09e, a), .1, Align::center, .35));
        if(t->host) c.text(box.right() - 3 * k, box.y - 2 * k, "HOST", style(Face::mono, 7 * k * near, kBrass.with_alpha(a), .1, Align::right, .3));
    }
    // revive status (#coopReviveStatus)
    if(!in.revive.empty()) {
        const TextStyle rs = style(Face::serif, std::clamp(W * .016, 13.0 * k, 19.0 * k), Color::hex(0xfff0c7), .08, Align::center);
        const double tw = std::min(c.measure(in.revive, rs), W * .85);
        const Rect r{W / 2 - tw / 2 - 22 * k, H * (v.touch ? .55 : .67) - 20 * k, tw + 44 * k, 40 * k};
        c.shape(Canvas::cut_shape(r, 9 * k), Color::hex(0x160b0b, .87f), Color::hex(0x0d0606, .87f), 2 * k, in.revive_alert ? Color::hex(0xe3a36a) : kLine);
        c.text(W / 2, r.y + r.h / 2 + rs.size * .36, in.revive, rs);
    }
    // room badge (#roomBadge)
    if(!in.badge.empty()) {
        const TextStyle bs = style(Face::mono, (v.touch ? 10 : 12) * k, kText, .12, Align::center, .35);
        const double bw = std::max(185 * k, c.measure(in.badge, bs) + 24 * k), bh = 34 * k;
        const Rect r{v.safe.right() - 18 * k - bw, v.safe.bottom() - (v.touch ? 74 : 116) * k - bh, bw, bh};
        panel(c, r, k, 1);
        c.text(r.x + r.w / 2, r.y + r.h / 2 + 4.5 * k, in.badge, bs);
    }
}

}  // namespace cat::ui
