// N7 UI tests: the settings model against the legacy page's own controls (fixture recorded from the
// running legacy game), settings persistence and presets, the font atlas, menu navigation with the
// keyboard only and with the mouse, the HUD layout at several screen sizes and safe areas, the HUD
// editor, and the control-mode / pointer-capture rules (legacy + v0.18 Android fix branch).
//   cat_arena_ui_tests <assets/n7> <tests/fixtures/n7/legacy_settings.json>
#include "cat/app_controls.hpp"
#include "cat/game_settings.hpp"
#include "cat/input_modes.hpp"
#include "cat/menu.hpp"
#include "cat/ui.hpp"
#include "cat/ui_hud.hpp"
#include "mini_json.hpp"

#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>

namespace {
int failures = 0, checks = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    if(!ok) { std::printf("FAIL %s\n", what.c_str()); ++failures; }
}
using namespace cat;
using namespace cat::ui;

void settings_match_legacy(const std::filesystem::path& fixture) {
    const auto j = testjson::load_json(fixture)["controls"];
    std::set<std::string> legacy_ids;
    int matched = 0;
    for(std::size_t i = 0; i < j.size(); ++i) {
        const auto& c = j[i];
        const std::string id = c["id"].str(), type = c["type"].str();
        legacy_ids.insert(id);
        const SettingDef* d = find_setting(id);
        if(!d) { check(false, "legacy control missing natively: " + id); continue; }
        bool ok = true;
        if(type == "range") {
            ok = d->kind == SettingKind::range && d->min == c["min"].num() && d->max == c["max"].num() && std::fabs(d->step - c["step"].num()) < 1e-12 && std::stod(d->fallback) == std::stod(c["value"].str());
        } else if(type == "checkbox") {
            ok = d->kind == SettingKind::toggle && (d->fallback == "true") == c["value"].boolean();
        } else if(type == "select") {
            ok = d->kind == SettingKind::select && d->fallback == c["value"].str() && d->options.size() == c["options"].size();
            for(std::size_t o = 0; ok && o < d->options.size(); ++o) ok = d->options[o].value == c["options"][o]["value"].str() && d->options[o].label == c["options"][o]["label"].str();
        } else ok = false;
        check(ok, "control " + id + " (" + type + ") matches the legacy default, range and options");
        matched += ok;
    }
    int extra = 0;
    for(const auto& d : setting_defs()) if(d.kind != SettingKind::action && !legacy_ids.count(d.id)) { ++extra; check(false, "native control not in the legacy page: " + d.id); }
    int applied = 0, partial = 0, stored = 0;
    for(const auto& d : setting_defs()) if(d.kind != SettingKind::action) { applied += d.support == Support::applied; partial += d.support == Support::partial; stored += d.support == Support::stored; }
    std::printf("%s settings: %d of %zu legacy controls match (default, range, options, labels of options); native: %d applied, %d applied with a difference, %d stored only (N10)\n",
                matched == static_cast<int>(j.size()) && extra == 0 ? "PASS" : "FAIL", matched, j.size(), applied, partial, stored);
}

void settings_behaviour() {
    GameSettings s;
    check(s.number("fovControl") == 105 && s.text("gfxPreset") == "normal" && s.flag("musicEnabled"), "defaults");
    check(s.set("fovControl", "200") && s.number("fovControl") == 125, "range clamps");
    check(s.set("motionStrength", "103") && s.number("motionStrength") == 105, "range snaps to the step");
    check(s.set("gfxGamma", "1.333") && std::fabs(s.number("gfxGamma") - 1.35) < 1e-9, "fractional steps snap");
    check(!s.set("crosshairStyle", "star") && s.text("crosshairStyle") == "classic", "unknown option rejected");
    check(!s.set("nope", "1") && !s.set("fovControl", "abc"), "unknown id / bad number rejected");
    check(s.set("gfxPreset", "fast") && s.text("gfxFilter") == "bilinear" && s.text("gfxEffects") == "0.7" && !s.flag("gfxBrass") && s.number("gfxGamma") > 1.3, "preset writes its values, keeps brightness");
    check(s.set("gfxShadows", "3") && s.text("gfxPreset") == "custom", "changing a preset value switches to CUSTOM");
    s.restore_normal_graphics();
    check(s.text("gfxPreset") == "normal" && s.number("gfxGamma") == 1 && s.text("gfxShadows") == "1", "RESTORE NORMAL GRAPHICS");
    s.set("vmSizeFov", "70"); s.restore_viewmodel();
    check(s.number("vmSizeFov") == 60, "viewmodel reset");
    s.set("gfxPreset", "fastest");
    s.hud_layout()["weaponBox"] = {.25, .5, 150, 40};
    s.map = "q3-gallery";
    std::stringstream io; s.save(io);
    GameSettings r = GameSettings::load(io);
    check(r == s, "save / load round trip (values, preset, HUD layout, map)");
    std::stringstream junk("fovControl=90\nbroken line\nhud.weaponBox=2,-1,999,5\nhud.unknown=0,0,100,100\ncrosshairColor=#000000\n");
    const GameSettings g = GameSettings::load(junk);
    check(g.number("fovControl") == 90 && g.text("crosshairColor") == "#f2cf93" && g.hud_layout().size() == 1 && g.hud_layout().at("weaponBox").x == 1 && g.hud_layout().at("weaponBox").size == 200 && g.hud_layout().at("weaponBox").opacity == 10,
          "load ignores bad lines and clamps HUD records like hudLoad");
    check(format_value(*find_setting("vmAngleOffset"), "0") == "LEVEL · 0°" && format_value(*find_setting("vmAngleOffset"), "5") == "+5°" && format_value(*find_setting("fpsLimit"), "241") == "UNLIMITED" &&
          format_value(*find_setting("sceneResolution"), "720") == "720p" && format_value(*find_setting("gfxPicmip"), "2") == "LOW", "legacy value read-outs");
    std::printf("%s settings behaviour (validation, presets, persistence)\n", failures ? "FAIL" : "PASS");
}

std::vector<MapEntry> maps() {
    return {{"helios-crucible", "HELIOS CRUCIBLE", "Three levels · Live reactor", "A burning heart of iron."},
            {"bastion-keep", "BASTION KEEP", "Three levels · Fortress", "A broken military stronghold."},
            {"reactor-bastion", "REACTOR BASTION", "Industrial · Survival", "The original reactor fortress."},
            {"q3-gallery", "THE ARSENAL", "Exhibition · Combat lab", "An arena of experimental materials."}};
}

struct Harness {
    const Atlas* atlas; GameSettings settings; Menu menu; double w, h; MenuFrame f;
    Harness(const Atlas* a, double width, double height) : atlas(a), menu(settings, maps()), w(width), h(height) { f.scale = std::min(width / 1280, height / 720) * 1.0; f.safe = {0, 0, width, height}; }
    void frame() { Canvas c(atlas, w, h); menu.build(c, f); }
    std::vector<Command> key(NavKey k, bool shift = false) { auto r = menu.handle({MenuEvent::Kind::key, k, 0, 0, 0, shift}); frame(); return r; }
    std::vector<Command> click(const std::string& id) {
        for(const auto& it : menu.items()) if(it.id == id) {
            const double x = it.rect.x + it.rect.w / 2, y = it.rect.y + it.rect.h / 2;
            menu.handle({MenuEvent::Kind::pointer_move, {}, x, y}); menu.handle({MenuEvent::Kind::pointer_down, {}, x, y});
            auto r = menu.handle({MenuEvent::Kind::pointer_up, {}, x, y}); frame(); return r;
        }
        check(false, "click target not found: " + id); return {};
    }
    const Menu::Item* item(const std::string& id) const { for(const auto& it : menu.items()) if(it.id == id) return &it; return nullptr; }
};
bool has(const std::vector<Command>& v, Command::Kind k, const std::string& arg = "*") { for(const auto& c : v) if(c.kind == k && (arg == "*" || c.arg == arg)) return true; return false; }

void keyboard_navigation(const Atlas* atlas) {
    const int before = failures;
    Harness t(atlas, 1280, 720);
    t.frame();
    check(t.menu.screen() == Screen::home && t.menu.focused() == "home.play", "home: PLAY has focus");
    t.key(NavKey::down); t.key(NavKey::down);
    check(t.menu.focused() == "home.settings", "down arrow walks the main menu");
    t.key(NavKey::accept);
    check(t.menu.screen() == Screen::settings && t.menu.settings_tab() == static_cast<int>(SettingsTab::graphics), "settings open on GRAPHICS from the main menu");
    // every enabled control of every tab is reachable with the keyboard alone, and is scrolled into view
    int reached = 0, expected = 0;
    for(int tab = 0; tab < 6; ++tab) {
        while(t.menu.settings_tab() != tab) t.key(NavKey::next_tab);
        std::set<std::string> want, seen;
        for(const auto& d : setting_defs()) if(static_cast<int>(d.tab) == tab && d.support != Support::stored) want.insert("set." + d.id);
        for(int i = 0; i < 120; ++i) {
            t.key(NavKey::down);
            const std::string id = t.menu.focused();
            if(id.rfind("set.", 0) == 0) {
                seen.insert(id);
                const auto* it = t.item(id);
                const auto* view = t.item("settings.close");
                check(it && view && it->rect.y >= 0 && it->rect.bottom() <= view->rect.y + 1, "focused control is scrolled into view: " + id);
            }
        }
        expected += static_cast<int>(want.size());
        for(const auto& id : want) { reached += seen.count(id) > 0; check(seen.count(id) > 0, "keyboard reaches " + id); }
        for(const auto& id : seen) check(want.count(id) > 0, "keyboard only stops on enabled controls: " + id);
    }
    std::printf("  keyboard reached %d of %d enabled settings controls in the six tabs\n", reached, expected);
    // tabs: Left / Right on a tab, PageDown / PageUp (v0.18 fix), wrap around
    t.key(NavKey::page_down); check(t.menu.settings_tab() == 0, "PageDown after the last tab wraps to the first");
    t.key(NavKey::page_up); check(t.menu.settings_tab() == 5, "PageUp goes back");
    while(t.menu.focused() != "set.fovControl") { t.key(NavKey::next_tab); if(t.menu.settings_tab() == 0) for(int i = 0; i < 3 && t.menu.focused() != "set.fovControl"; ++i) t.key(NavKey::down); }
    t.key(NavKey::right); check(t.settings.number("fovControl") == 106, "Right raises a slider by one step");
    t.key(NavKey::right, true); check(t.settings.number("fovControl") == 111, "Shift+Right raises by five steps");
    t.key(NavKey::down); check(t.menu.focused() == "set.crosshairStyle", "next control");
    t.key(NavKey::accept); check(t.menu.open_dropdown() == "crosshairStyle", "Enter opens a select");
    t.key(NavKey::down); auto cmd = t.key(NavKey::accept);
    check(t.settings.text("crosshairStyle") == "dot" && has(cmd, Command::Kind::settings_changed, "crosshairStyle") && t.menu.open_dropdown().empty(), "Down + Enter picks the next option");
    t.key(NavKey::left); check(t.settings.text("crosshairStyle") == "classic", "Left on a select steps back");
    while(t.menu.settings_tab() != 1) t.key(NavKey::next_tab);
    while(t.menu.focused() != "set.perfCpu") t.key(NavKey::down);
    cmd = t.key(NavKey::accept); check(t.settings.flag("perfCpu") && has(cmd, Command::Kind::settings_changed, "perfCpu"), "Enter toggles a checkbox");
    t.key(NavKey::back); check(t.menu.screen() == Screen::home, "Esc / Back leaves settings");
    t.key(NavKey::back); check(t.menu.screen() == Screen::exit_confirm && t.menu.focused() == "exit.cancel", "Back on the main menu asks before exiting (catBack)");
    t.key(NavKey::accept); check(t.menu.screen() == Screen::home, "STAY returns");
    t.key(NavKey::last); cmd = t.key(NavKey::accept); check(t.menu.screen() == Screen::exit_confirm, "EXIT opens the dialog");
    t.key(NavKey::up); cmd = t.key(NavKey::accept); check(has(cmd, Command::Kind::quit), "EXIT GAME quits");
    // PLAY -> maps -> enter, pause routing
    Harness p(atlas, 1280, 720); p.frame();
    auto c = p.key(NavKey::accept); check(p.menu.screen() == Screen::maps && has(c, Command::Kind::preview_map, "helios-crucible"), "PLAY shows the maps with a preview");
    p.key(NavKey::down); c = p.key(NavKey::accept); check(has(c, Command::Kind::preview_map, "bastion-keep") && p.menu.selected_map() == "bastion-keep", "keyboard picks a map");
    while(p.menu.focused() != "maps.enter") p.key(NavKey::down);
    c = p.key(NavKey::accept); check(has(c, Command::Kind::start_game, "bastion-keep"), "ENTER ARENA starts the chosen map");
    p.menu.show_loading({"BASTION KEEP", 7, "done", "", true, 900}); p.frame();
    c = p.key(NavKey::accept); check(has(c, Command::Kind::enter_arena), "ENTER THE ARENA from the keyboard");
    p.menu.show_pause(); p.frame();
    check(p.menu.focused() == "pause.resume", "pause: RESUME has focus");
    p.key(NavKey::down); p.key(NavKey::accept); check(p.menu.screen() == Screen::settings && p.menu.settings_tab() == static_cast<int>(SettingsTab::input), "pause -> SETTINGS / CONTROLS opens MOBILE / INPUT");
    p.key(NavKey::back); check(p.menu.screen() == Screen::pause, "settings from pause return to pause");
    c = p.key(NavKey::back); check(has(c, Command::Kind::resume) && !p.menu.active(), "Esc in the pause menu resumes");
    std::printf("%s keyboard-only navigation (menus, all settings tabs, selects, sliders, toggles, pause, exit)\n", failures == before ? "PASS" : "FAIL");
}

void mouse_navigation(const Atlas* atlas) {
    const int before = failures;
    Harness t(atlas, 1600, 900);
    t.frame();
    auto c = t.click("home.play"); check(t.menu.screen() == Screen::maps && has(c, Command::Kind::preview_map), "click PLAY");
    c = t.click("maps.map.q3-gallery"); check(has(c, Command::Kind::preview_map, "q3-gallery"), "click a map");
    c = t.click("maps.enter"); check(has(c, Command::Kind::start_game, "q3-gallery"), "click ENTER ARENA");
    t.click("maps.back"); t.click("home.settings");
    c = t.click("settings.tab.4"); check(t.menu.settings_tab() == 4, "click a tab");
    const auto* slider = t.item("set.masterVolume");
    check(slider != nullptr, "audio slider present");
    if(slider) {
        const double y = slider->rect.y + slider->rect.h / 2;
        t.menu.handle({MenuEvent::Kind::pointer_down, {}, slider->rect.x + 1, y});
        check(t.settings.number("masterVolume") == 0, "press at the left end of a slider");
        t.menu.handle({MenuEvent::Kind::pointer_move, {}, slider->rect.x + slider->rect.w * .5, y});
        t.menu.handle({MenuEvent::Kind::pointer_up, {}, slider->rect.x + slider->rect.w * .5, y});
        check(t.settings.number("masterVolume") == 50, "drag a slider to the middle");
    }
    t.click("settings.tab.1"); t.frame();
    const double s0 = t.menu.scroll();
    t.menu.handle({MenuEvent::Kind::wheel, {}, 800, 450, -3}); t.frame();
    check(t.menu.scroll() > s0, "mouse wheel scrolls the settings list");
    const double s1 = t.menu.scroll();
    for(const auto& it : t.menu.items()) if(it.id == "settings.close") { t.menu.handle({MenuEvent::Kind::pointer_down, {}, 800, it.rect.y - 60}); t.menu.handle({MenuEvent::Kind::pointer_move, {}, 800, it.rect.y - 160}); t.menu.handle({MenuEvent::Kind::pointer_up, {}, 800, it.rect.y - 160}); }
    t.frame();
    check(t.menu.scroll() > s1, "touch drag scrolls the settings list");
    t.click("settings.tab.0");
    c = t.click("set.crosshairColor"); check(t.menu.open_dropdown() == "crosshairColor", "click opens a select");
    c = t.click("opt.3"); check(t.settings.text("crosshairColor") == "#65e9ff" && t.menu.open_dropdown().empty(), "click an option");
    std::printf("%s mouse / touch navigation (buttons, maps, tabs, slider drag, wheel, touch scroll, selects)\n", failures == before ? "PASS" : "FAIL");
}

void hud_layouts(const Atlas* atlas) {
    const int before = failures;
    GameSettings s;
    struct Screen_ { double w, h, scale; Rect safe; };
    const Screen_ screens[] = {{1280, 720, 1, {0, 0, 1280, 720}}, {1920, 1080, 1.5, {0, 0, 1920, 1080}}, {2560, 1440, 2, {0, 0, 2560, 1440}}, {800, 480, 1, {0, 0, 800, 480}},
                               {2400, 1080, 2.625, {110, 0, 2180, 1040}}};   // phone in landscape with a notch and a gesture bar
    HudInfo info; info.weapon = "TRIPLE SPIKE"; info.other_weapon = "SUPER NAILGUN"; info.map_title = "REACTOR BASTION"; info.wave = 1; info.wave_remaining = 10; info.enemies_alive = 10; info.ammo = 36; info.clip = 36;
    for(const auto& sc : screens) for(const bool touch : {false, true}) {
        HudView v; v.settings = &s; v.scale = sc.scale; v.safe = sc.safe; v.touch = touch; v.perf = {{"FPS LIVE", "144"}, {"CPU", "2.1 ms"}};
        Canvas c(atlas, sc.w, sc.h);
        const auto elements = build_hud(c, info, v);
        char tag[96]; std::snprintf(tag, sizeof tag, "%.0fx%.0f %s", sc.w, sc.h, touch ? "touch" : "kbm");
        check(c.triangles() > 200, std::string("HUD draws: ") + tag);
        for(const auto& e : elements)
            check(e.rect.x >= sc.safe.x - .5 && e.rect.y >= sc.safe.y - .5 && e.rect.right() <= sc.safe.right() + .5 && e.rect.bottom() <= sc.safe.bottom() + .5, std::string("inside the safe area: ") + e.id + " " + tag);
        if(touch) for(std::size_t i = 0; i < elements.size(); ++i) for(std::size_t k = i + 1; k < elements.size(); ++k) {
            const auto& a = elements[i].rect; const auto& b = elements[k].rect;
            const bool overlap = a.x < b.right() - 1 && b.x < a.right() - 1 && a.y < b.bottom() - 1 && b.y < a.bottom() - 1;
            if(overlap) check(false, std::string("touch layout overlap ") + elements[i].id + " / " + elements[k].id + " " + tag);
        }
    }
    // custom placement (hudRecords): fraction of the viewport, size and opacity
    HudView v; v.settings = &s; v.scale = 1; v.safe = {0, 0, 1280, 720};
    const Rect base = default_hud_rect("leftStats", v, 1280, 720);
    s.hud_layout()["leftStats"] = {.5, .25, 150, 40};
    const Rect moved = placed_hud_rect("leftStats", v, 1280, 720);
    check(std::fabs(moved.x - 640) < 1e-9 && std::fabs(moved.y - 180) < 1e-9 && std::fabs(moved.w - base.w * 1.5) < 1e-9, "custom HUD placement and size");
    s.hud_layout()["leftStats"] = {.99, .99, 100, 100};
    const Rect clamped = placed_hud_rect("leftStats", v, 1280, 720);
    check(clamped.right() <= 1280 + 1e-9 && clamped.bottom() <= 720 + 1e-9, "placements are kept inside the viewport");
    // HUD scale follows the setting
    s.hud_layout().clear(); s.set("hudScale", "145");
    check(default_hud_rect("weaponBox", v, 1280, 720).w > base.w * 0 && default_hud_rect("weaponBox", v, 1280, 720).w > 227 * 145 / 115.0 - 1, "HUD SIZE scales the panels");
    // HUD editor: drag an element with the pointer
    GameSettings es; Menu m(es, maps());
    m.show_hud_editor(true);
    HudView ev; ev.settings = &es; ev.scale = 1; ev.safe = {0, 0, 1280, 720}; ev.editing = true;
    Canvas hc(atlas, 1280, 720);
    MenuFrame f; f.scale = 1; f.safe = ev.safe; f.hud = build_hud(hc, info, ev);
    Canvas mc(atlas, 1280, 720); m.build(mc, f);
    const Rect r0 = placed_hud_rect("weaponBox", ev, 1280, 720);
    m.handle({MenuEvent::Kind::pointer_down, {}, r0.x + 10, r0.y + 10});
    m.handle({MenuEvent::Kind::pointer_move, {}, r0.x - 290, r0.y - 190});
    const auto done = m.handle({MenuEvent::Kind::pointer_up, {}, r0.x - 290, r0.y - 190});
    const Rect r1 = placed_hud_rect("weaponBox", ev, 1280, 720);
    check(std::fabs(r1.x - (r0.x - 300)) < 1.5 && std::fabs(r1.y - (r0.y - 200)) < 1.5 && has(done, Command::Kind::settings_changed, "hudLayout"), "HUD editor drag moves an element");
    m.build(mc, f);
    m.handle({MenuEvent::Kind::key, NavKey::first});
    std::printf("%s HUD layout (5 screen sizes incl. a notched phone, keyboard and touch layouts, placements, scale, editor drag)\n", failures == before ? "PASS" : "FAIL");
}

void capture_rules() {
    const int before = failures;
    PointerCapture p;
    p.set_game_active(true);
    check(!p.wanted(), "no capture without a deliberate action");
    p.request(); check(p.wanted(), "click to play requests capture");
    p.set_menu_open(true); check(!p.wanted(), "opening pause / settings releases capture");
    p.set_menu_open(false); check(!p.wanted(), "closing the menu alone does not re-capture");
    p.request(); check(p.wanted(), "resume re-requests capture");
    p.set_focused(false); check(!p.wanted(), "losing focus releases");
    p.set_focused(true); p.request(); p.release(); check(!p.wanted(), "Esc releases");
    p.set_touch_mode(true); p.request(); check(!p.wanted(), "touch mode never captures");
    p.set_touch_mode(false); p.request(); p.platform_result(false); check(p.failures() == 1 && p.status() == "REQUESTING MOUSE CAPTURE", "a refused capture is counted and reported");
    p.platform_result(true); check(p.status() == "MOUSE LOCKED · ESC TO RELEASE", "status when locked");
    p.set_game_active(false); check(!p.wanted(), "leaving the game releases");
    InputModeTracker m(InputKind::touch);
    check(m.touch(), "AUTO on a touch device starts in touch mode");
    check(m.note(InputKind::kbm) && !m.touch(), "an external mouse (Android / DeX) switches AUTO to keyboard & mouse");
    check(m.note(InputKind::touch) && m.touch(), "touching the screen switches back");
    m.set_setting("kbm"); check(!m.touch() && !m.note(InputKind::touch), "fixed KEYBOARD & MOUSE ignores touches");
    m.set_setting("touch"); check(m.touch(), "fixed TOUCH");
    std::printf("%s control mode and pointer capture rules (legacy + v0.18 Android / DeX fix); device behaviour on Android: NOT_RUN\n", failures == before ? "PASS" : "FAIL");
}
}  // namespace

// N7 shell helpers: the legacy FPS statistics, sprintRequested() and the touch handlers.
void shell_helpers() {
    const int before = failures;
    {   // 200 frames of 10 ms, then two of 30 ms
        cat::PerfMeter m; double t = 0;
        m.frame(t, 1);
        for(int i = 0; i < 200; ++i) { t += 10; m.frame(t, 1); }
        t += 30; m.frame(t, 1); t += 30; m.frame(t, 1);
        t += 800; m.frame(t, 1);   // a stall of 800 ms (>= 500 ms) clears the history, as recordFrameInterval does
        const auto s0 = m.summary();
        check(s0.frames == 0, "a 500 ms+ stall clears the 60 s history");
        cat::PerfMeter p; t = 0; p.frame(t, 2);
        for(int i = 0; i < 200; ++i) { t += 10; p.frame(t, 2); }
        t += 30; p.frame(t, 2); t += 30; p.frame(t, 2);
        for(int i = 0; i < 34; ++i) { t += 10; p.frame(t, 2); }   // t = 2400 ms closes the third 800 ms window: statistics recomputed
        const auto s = p.summary();
        const double n = 236, total = 234 * 10 + 60;
        check(std::fabs(s.fps_avg - 1000 * n / total) < 1e-9, "FPS AVG = frames / time");
        check(std::fabs(s.low1 - 1000 / ((30 + 30 + 10) / 3.0)) < 1e-9, "1% LOW = mean of the slowest ceil(1%) frames");
        check(s.low01 == 0, "0.1% LOW needs 1000 frames");
        check(s.hitches == 2 && std::fabs(s.cpu_ms - 2) < 1e-12, "hitches of 25 ms+, CPU average");
    }
    {
        cat::SprintLatch l;
        check(!l.requested(false) && l.requested(true), "sprint HOLD follows the key");
        l.set_mode("toggle"); l.press(); check(l.requested(false), "sprint TOGGLE latches on a press"); l.press(); check(!l.requested(true), "a second press releases the latch");
        l.set_mode("auto"); check(l.requested(false), "sprint AUTO always requests sprint");
    }
    {
        GameSettings gs; HudView v; v.settings = &gs; v.touch = true; v.scale = 1; v.safe = {0, 0, 1280, 720};
        const Atlas* none = nullptr; (void)none;
        std::vector<HudElement> el;
        for(const char* id : {"stick", "fire", "jump", "swap", "touchSprint", "touchCrouch", "touchReload", "pauseButton"}) el.push_back({id, default_hud_rect(id, v, 1280, 720)});
        auto centre = [&](const char* id) { for(const auto& e : el) if(e.id == id) return std::array<double, 2>{e.rect.x + e.rect.w / 2, e.rect.y + e.rect.h / 2}; return std::array<double, 2>{}; };
        cat::TouchControls t;
        const auto st = centre("stick"); const auto stick = el[0].rect;
        check(t.down(1, st[0] + stick.w * .47, st[1], el, 1) == "stick" && std::fabs(t.stick_x() - 1) < 1e-12 && t.stick_y() == 0, "joystick: 47 % of the pad is full deflection");
        t.move(1, st[0] + stick.w, st[1] + stick.w, {});
        check(std::fabs(std::hypot(t.stick_x(), t.stick_y()) - 1) < 1e-12, "joystick clamps to the unit circle");
        const auto fi = centre("fire");
        check(t.down(2, fi[0], fi[1], el, 1) == "fire" && (t.keys() & cat::combat::key_fire), "FIRE holds the fire key");
        check(t.down(3, 640, 300, el, 2).empty(), "a free finger looks around");
        t.move(3, 660, 290, {150, true});
        check(std::fabs(t.look_dx() - 20 * 1.5 * 1.5 / 2) < 1e-12 && std::fabs(t.look_dy() - 10 * 1.5 * 1.5 / 2) < 1e-12, "look: 1.5 x sensitivity per CSS pixel, inverted Y");
        const auto sp = centre("touchSprint");
        t.down(4, sp[0], sp[1], el, 1);
        check(t.sprint_held() && t.take_sprint_presses() == 1, "SPRINT button");
        const auto cr = centre("touchCrouch");
        t.down(5, cr[0], cr[1], el, 1);
        check((t.keys() & cat::physics::key_crouch) && (t.keys() & cat::physics::key_slide), "SLIDE holds crouch and taps slide");
        t.end_tick();
        check(!(t.keys() & cat::physics::key_slide) && (t.keys() & cat::physics::key_crouch) && t.look_dx() == 0, "taps and look last one tick");
        const auto pa = centre("pauseButton");
        check(t.down(6, pa[0], pa[1], el, 1) == "pause", "PAUSE button");
        for(std::int64_t f = 1; f <= 5; ++f) t.up(f);
        check(t.keys() == 0 && t.stick_x() == 0 && !t.sprint_held() && !t.active(), "lifting every finger releases everything");
    }
    std::printf("%s shell helpers (legacy FPS statistics, sprint modes, touch joystick / look / buttons)\n", failures == before ? "PASS" : "FAIL");
}

int main(int argc, char** argv) {
    if(argc < 3) { std::printf("usage: cat_arena_ui_tests <assets/n7> <legacy_settings.json>\n"); return 2; }
    try {
        const std::filesystem::path assets = argv[1];
        settings_match_legacy(argv[2]);
        settings_behaviour();
        const Atlas atlas = Atlas::build({assets / "fonts/LiberationSerif-Bold.ttf", assets / "fonts/LiberationSans-Bold.ttf", assets / "fonts/LiberationMono-Bold.ttf", assets / "fonts/LiberationSerif-Regular.ttf"}, assets / "fonts/DejaVuSans-Bold.ttf");
        Canvas probe(&atlas, 100, 100);
        const double w = probe.measure("CAT ARENA", TextStyle{Face::serif, 20});
        check(atlas.glyph_count() > 280 && atlas.missing() == 0 && w > 80 && w < 140, "font atlas");
        std::printf("%s font atlas: %zu glyphs from 4 faces, %d requested code points missing (Liberation + DejaVu fallback), \"CAT ARENA\" at 20 px is %.1f px wide\n", atlas.glyph_count() > 280 && atlas.missing() == 0 ? "PASS" : "FAIL", atlas.glyph_count(), atlas.missing(), w);
        keyboard_navigation(&atlas);
        mouse_navigation(&atlas);
        hud_layouts(&atlas);
        capture_rules();
        shell_helpers();
    } catch(const std::exception& e) { std::printf("FAIL exception: %s\n", e.what()); return 1; }
    std::printf("%s ui (%d checks)\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
