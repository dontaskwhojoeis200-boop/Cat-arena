// N7 menus (see include/cat/menu.hpp). Layout sizes are the legacy CSS pixels at 1280 x 720, scaled
// by MenuFrame::scale and fitted to small windows.
#include "cat/menu.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace cat::ui {
namespace {
const Color kInk = Color::hex(0x090a0b), kSoft = Color::hex(0x161719), kLine = Color::hex(0xa8afb5), kWhite = Color::hex(0xe8e9eb);
const Color kAsh = Color::hex(0x858a92), kWarm = Color::hex(0x9e5b50), kText = Color::hex(0xd6dadc), kBrass = Color::hex(0xe1c891), kDim = Color::hex(0x6d7177);

TextStyle ts(Face f, double size, Color c, double spacing = 0, Align a = Align::left, double weight = 0, bool shadow = true) {
    TextStyle s; s.face = f; s.size = size; s.color = c; s.spacing = spacing; s.align = a; s.weight = weight;
    if(shadow) s.shadow = Color::hex(0x000000, .6f * c.a);
    return s;
}
std::vector<std::array<double, 2>> sigil(double cx, double cy, double k) {   // .feral-cat-sigil path (16539), 120 x 90 view box
    std::vector<std::array<double, 2>> p{{11, 73}, {23, 17}, {46, 40}, {60, 35}, {74, 40}, {97, 17}, {109, 73}, {84, 78}, {70, 69}, {60, 75}, {50, 69}, {36, 78}};
    for(auto& q : p) { q[0] = cx + (q[0] - 60) * k; q[1] = cy + (q[1] - 45) * k; }
    return p;
}
}  // namespace

Menu::Menu(GameSettings& settings, std::vector<MapEntry> maps) : settings_(&settings), maps_(std::move(maps)) {
    selected_map_ = settings.map;
    if(std::none_of(maps_.begin(), maps_.end(), [&](const MapEntry& m) { return m.id == selected_map_; }) && !maps_.empty()) selected_map_ = maps_[0].id;
}

void Menu::go(Screen s) { screen_ = s; focus_id_.clear(); focus_ = 0; dropdown_.clear(); dragging_.clear(); }
void Menu::show_home() { go(Screen::home); host_mode_ = false; }
void Menu::show_maps() { go(Screen::maps); host_mode_ = false; focus_id_ = "maps.map." + selected_map_; }
bool Menu::text_focused() const { return focus_ < items_.size() && items_[focus_].id == focus_id_ && items_[focus_].kind == item_text; }
std::string* Menu::field_value(const std::string& id) {
    if(id == "mp.name") return &settings_->player_name;
    if(id == "mp.title") return &settings_->server_name;
    if(id == "mp.relay") return &settings_->relay;
    return nullptr;
}
void Menu::show_pause() { go(Screen::pause); }
void Menu::show_settings(bool from_pause) {
    before_settings_ = from_pause ? Screen::pause : Screen::home;
    if(from_pause) tab_ = SettingsTab::input;   // voidOpenSettingsFromPause: settingsTab('touch')
    else tab_ = SettingsTab::graphics;          // catSettings: settingsTab('gfx')
    go(Screen::settings);
    scroll_ = 0;
}
void Menu::show_hud_editor(bool from_pause) { before_editor_ = from_pause ? Screen::pause : Screen::settings; go(Screen::hud_editor); }
void Menu::show_loading(const LoadingState& s) { loading_ = s; go(Screen::loading); }

void Menu::add(const std::string& id, const Rect& r, int kind, bool enabled) {
    items_.push_back({id, r, enabled, kind});
}

void Menu::move_focus(int delta) {
    if(items_.empty()) return;
    const bool modal = !dropdown_.empty();
    if(!modal && screen_ == Screen::settings && focus_ < items_.size()) {   // tabs form one stop: Down enters the list, Up from the list's top returns to the tab
        const bool on_tab = items_[focus_].kind == item_tab;
        if(on_tab && delta > 0) {
            for(const auto& it : items_) if(it.kind != item_tab && it.kind != item_option && it.enabled) { focus_id_ = it.id; for(std::size_t i = 0; i < items_.size(); ++i) if(items_[i].id == focus_id_) focus_ = i; ensure_visible(); return; }
        }
        if(on_tab && delta < 0) { focus_id_ = "settings.close"; for(std::size_t i = 0; i < items_.size(); ++i) if(items_[i].id == focus_id_) focus_ = i; return; }
        if(!on_tab && delta < 0) {
            std::size_t prev = focus_;
            while(prev > 0 && (!items_[prev - 1].enabled || items_[prev - 1].kind == item_option)) --prev;
            if(prev > 0 && items_[prev - 1].kind == item_tab) { focus_id_ = "settings.tab." + std::to_string(static_cast<int>(tab_)); for(std::size_t i = 0; i < items_.size(); ++i) if(items_[i].id == focus_id_) focus_ = i; return; }
        }
        if(!on_tab && delta > 0 && items_[focus_].id == "settings.close") { focus_id_ = "settings.tab." + std::to_string(static_cast<int>(tab_)); for(std::size_t i = 0; i < items_.size(); ++i) if(items_[i].id == focus_id_) focus_ = i; return; }
    }
    const auto n = static_cast<long>(items_.size());
    long i = static_cast<long>(focus_ < items_.size() ? focus_ : 0);
    for(long step = 0; step < n; ++step) {
        i = (i + delta + n) % n;
        const auto& it = items_[static_cast<std::size_t>(i)];
        if(it.enabled && (modal ? it.kind == item_option : it.kind != item_option) && it.kind != item_hud) { focus_ = static_cast<std::size_t>(i); focus_id_ = it.id; break; }
    }
    ensure_visible();
}

void Menu::ensure_visible() {
    if(screen_ != Screen::settings || focus_ >= items_.size() || items_[focus_].id.rfind("set.", 0) != 0) return;
    const Rect& r = items_[focus_].rect;
    if(r.y < list_view_.y) scroll_ -= list_view_.y - r.y + 8;
    else if(r.bottom() > list_view_.bottom()) scroll_ += r.bottom() - list_view_.bottom() + 8;
    scroll_ = std::clamp(scroll_, 0.0, std::max(0.0, content_height_ - view_height_));
}

std::vector<Command> Menu::back() {
    if(!dropdown_.empty()) { dropdown_.clear(); return {}; }
    switch(screen_) {
        case Screen::maps: if(host_mode_) { go(Screen::multiplayer); host_mode_ = false; focus_id_ = "multi.host"; return {}; } go(Screen::home); return {};
        case Screen::multiplayer: case Screen::exit_confirm: go(Screen::home); return {};
        case Screen::servers: go(Screen::multiplayer); focus_id_ = "multi.join"; return {};
        case Screen::connecting: go(Screen::multiplayer); return {{Command::Kind::cancel_connect, ""}};
        case Screen::home: go(Screen::exit_confirm); focus_id_ = "exit.cancel"; return {};   // catBack on the home page asks to exit
        case Screen::settings: go(before_settings_); return {};
        case Screen::pause: close(); return {{Command::Kind::resume, ""}};
        case Screen::hud_editor: go(before_editor_); return {{Command::Kind::hud_editor_done, ""}};
        default: return {};
    }
}

std::vector<Command> Menu::adjust(const Item& item, int dir) {
    if(item.kind == item_tab) { tab_ = static_cast<SettingsTab>((static_cast<int>(tab_) + 6 + dir) % 6); scroll_ = 0; focus_id_ = "settings.tab." + std::to_string(static_cast<int>(tab_)); return {}; }
    if(item.id.rfind("set.", 0) != 0) {
        if(item.id == "editor.size" || item.id == "editor.alpha") {
            auto& rec = settings_->hud_layout()[hud_selected_];
            if(item.id == "editor.size") rec.size = std::clamp(rec.size + 5 * dir, 45, 200); else rec.opacity = std::clamp(rec.opacity + 5 * dir, 10, 100);
            return {{Command::Kind::settings_changed, "hudLayout"}};
        }
        if(item.id == "editor.item") {
            const auto& list = hud_items();
            auto it = std::find_if(list.begin(), list.end(), [&](const HudItem& h) { return hud_selected_ == h.id; });
            const auto idx = it == list.end() ? 0 : static_cast<long>(it - list.begin());
            hud_selected_ = list[static_cast<std::size_t>((idx + dir + static_cast<long>(list.size())) % static_cast<long>(list.size()))].id;
            return {};
        }
        move_focus(dir);
        return {};
    }
    const std::string id = item.id.substr(4);
    const SettingDef* d = find_setting(id);
    if(!d || !item.enabled) return {};
    if(d->kind == SettingKind::range) settings_->set_number(id, settings_->number(id) + d->step * dir);
    else if(d->kind == SettingKind::select) {
        const std::string cur = settings_->text(id);
        auto it = std::find_if(d->options.begin(), d->options.end(), [&](const SettingOption& o) { return o.value == cur; });
        long idx = it == d->options.end() ? 0 : static_cast<long>(it - d->options.begin());
        idx = std::clamp(idx + dir, 0L, static_cast<long>(d->options.size()) - 1);
        settings_->set(id, d->options[static_cast<std::size_t>(idx)].value);
    } else if(d->kind == SettingKind::toggle) settings_->set_flag(id, !settings_->flag(id));
    else return {};
    return {{Command::Kind::settings_changed, id}};
}

std::vector<Command> Menu::activate(const Item& item, double px, bool by_pointer) {
    if(!item.enabled) return {};
    const std::string& id = item.id;
    using K = Command::Kind;
    if(item.kind == item_option) {
        const SettingDef* d = find_setting(dropdown_);
        const auto index = static_cast<std::size_t>(std::stoul(id.substr(4)));
        std::vector<Command> out;
        if(d && index < d->options.size()) { settings_->set(d->id, d->options[index].value); out.push_back({K::settings_changed, d->id}); }
        focus_id_ = "set." + dropdown_;
        dropdown_.clear();
        return out;
    }
    if(item.kind == item_text) { move_focus(1); return {}; }   // Enter in a text field: the next control
    if(id == "home.play") { host_mode_ = false; go(Screen::maps); focus_id_ = "maps.map." + selected_map_; return {{K::preview_map, selected_map_}}; }
    if(id == "home.multi") { go(Screen::multiplayer); return {}; }
    // N8 multiplayer pages (legacy catHostChoice / catJoinChoice: host = the maps page with the server form)
    if(id == "multi.host") { host_mode_ = true; go(Screen::maps); focus_id_ = "maps.map." + selected_map_; return {{K::preview_map, selected_map_}}; }
    if(id == "multi.join") { show_servers(); return {{K::refresh_servers, ""}}; }
    if(id == "host.create") return {{K::host_server, selected_map_}};
    if(id == "host.local") return {{K::host_local, selected_map_}};
    if(id == "servers.refresh") return {{K::refresh_servers, ""}};
    if(id == "servers.back") { go(Screen::multiplayer); focus_id_ = "multi.join"; return {}; }
    if(id.rfind("join.", 0) == 0) return {{K::join_server, id.substr(5)}};
    if(id == "connect.cancel") { go(Screen::multiplayer); return {{K::cancel_connect, ""}}; }
    if(id == "pause.leave") return {{K::leave_room, ""}};
    if(id == "maps.back" && host_mode_) return back();
    if(id == "home.settings") { show_settings(false); return {}; }
    if(id == "home.exit") { go(Screen::exit_confirm); focus_id_ = "exit.cancel"; return {}; }
    if(id == "exit.confirm") return {{K::quit, ""}};
    if(id == "exit.cancel" || id == "maps.back" || id == "multi.back") { go(Screen::home); return {}; }
    if(id.rfind("maps.map.", 0) == 0) { selected_map_ = id.substr(9); settings_->map = selected_map_; return {{K::preview_map, selected_map_}}; }
    if(id == "maps.enter") return {{K::start_game, selected_map_}};
    if(id.rfind("settings.tab.", 0) == 0) { tab_ = static_cast<SettingsTab>(std::stoi(id.substr(13))); scroll_ = 0; return {}; }
    if(id == "settings.close") { go(before_settings_); return {}; }
    if(id == "pause.resume") { close(); return {{K::resume, ""}}; }
    if(id == "pause.settings") { show_settings(true); return {}; }
    if(id == "pause.customize") { show_hud_editor(true); return {}; }
    if(id == "pause.change_map") return {{K::change_map, ""}};
    if(id == "pause.return") return {{K::return_to_menu, ""}};
    if(id == "loading.enter") return {{K::enter_arena, ""}};
    if(id == "editor.done") { go(before_editor_); return {{K::hud_editor_done, ""}}; }
    if(id == "editor.reset") { settings_->hud_layout().erase(hud_selected_); return {{K::settings_changed, "hudLayout"}}; }
    if(id == "editor.item") return adjust(item, 1);
    if(id.rfind("hud.", 0) == 0) { hud_selected_ = id.substr(4); return {}; }
    if(id.rfind("set.", 0) == 0) {
        const std::string sid = id.substr(4);
        const SettingDef* d = find_setting(sid);
        if(!d) return {};
        switch(d->kind) {
            case SettingKind::toggle: settings_->set_flag(sid, !settings_->flag(sid)); return {{K::settings_changed, sid}};
            case SettingKind::select: {
                dropdown_ = sid;
                const std::string cur = settings_->text(sid);
                dropdown_index_ = 0;
                for(std::size_t i = 0; i < d->options.size(); ++i) if(d->options[i].value == cur) dropdown_index_ = i;
                focus_id_ = "opt." + std::to_string(dropdown_index_);
                return {};
            }
            case SettingKind::range: {
                if(by_pointer && item.rect.w > 0) {
                    const double t = std::clamp((px - item.rect.x) / item.rect.w, 0.0, 1.0);
                    settings_->set_number(sid, d->min + t * (d->max - d->min));
                    return {{K::settings_changed, sid}};
                }
                return {};
            }
            case SettingKind::action:
                if(sid == "vmReset") { settings_->restore_viewmodel(); return {{K::settings_changed, ""}}; }
                if(sid == "gfxReset") { settings_->restore_normal_graphics(); return {{K::settings_changed, ""}}; }
                if(sid == "gfxDebugOpen") return {{K::debug_view, ""}};
                if(sid == "testAudio") return {{K::test_audio, ""}};
                if(sid == "editHudLayout") { show_hud_editor(before_settings_ == Screen::pause); return {}; }
                if(sid == "resetHudLayout") { settings_->hud_layout().clear(); return {{K::reset_hud, ""}, {K::settings_changed, "hudLayout"}}; }
                if(sid == "fullscreenNow") return {{K::fullscreen_now, ""}};
                return {};
        }
    }
    return {};
}

std::vector<Command> Menu::handle(const MenuEvent& e) {
    using Kind = MenuEvent::Kind;
    const bool modal = !dropdown_.empty();
    auto hit = [&](double x, double y) -> const Item* {
        for(auto it = items_.rbegin(); it != items_.rend(); ++it) {   // topmost first (drawn last)
            if(modal && it->kind != item_option) continue;
            if(it->id.rfind("set.", 0) == 0 && !(x >= list_view_.x && x < list_view_.right() && y >= list_view_.y && y < list_view_.bottom())) continue;
            if(it->rect.contains(x, y)) return &*it;
        }
        return nullptr;
    };
    if(e.kind == Kind::key) {
        const Item* f = focus_ < items_.size() && items_[focus_].id == focus_id_ ? &items_[focus_] : nullptr;
        switch(e.key) {
            case NavKey::up: case NavKey::prev_item: move_focus(-1); return {};
            case NavKey::down: case NavKey::next_item: move_focus(1); return {};
            case NavKey::left: if(f && !modal) return adjust(*f, e.shift ? -5 : -1); move_focus(-1); return {};
            case NavKey::right: if(f && !modal) return adjust(*f, e.shift ? 5 : 1); move_focus(1); return {};
            case NavKey::accept: if(f) return activate(*f, 0, false); return {};
            case NavKey::back: return back();
            case NavKey::erase:
                if(f && f->kind == item_text) { if(std::string* v = field_value(f->id); v && !v->empty()) { v->pop_back(); return {{Command::Kind::form_changed, f->id}}; } return {}; }
                return back();
            case NavKey::next_tab: case NavKey::prev_tab: case NavKey::page_up: case NavKey::page_down:   // v0.18 fix: Ctrl+Tab / PageUp / PageDown switch tabs
                if(screen_ == Screen::settings && !modal) {
                    const bool next = e.key == NavKey::next_tab || e.key == NavKey::page_down;
                    tab_ = static_cast<SettingsTab>((static_cast<int>(tab_) + (next ? 1 : 5)) % 6); scroll_ = 0; focus_id_ = "settings.tab." + std::to_string(static_cast<int>(tab_));
                }
                return {};
            case NavKey::first: case NavKey::last: {
                for(std::size_t k = 0; k < items_.size(); ++k) {
                    const auto& it = items_[e.key == NavKey::first ? k : items_.size() - 1 - k];
                    if(it.enabled && it.kind != item_hud && (modal ? it.kind == item_option : it.kind != item_option)) { focus_id_ = it.id; break; }
                }
                return {};
            }
        }
        return {};
    }
    if(e.kind == Kind::text) {   // printable ASCII only: the relay keeps names to the font's characters anyway
        const Item* f = focus_ < items_.size() && items_[focus_].id == focus_id_ ? &items_[focus_] : nullptr;
        std::string* v = f && f->kind == item_text ? field_value(f->id) : nullptr;
        if(!v) return {};
        const std::size_t max = f->id == "mp.name" ? kNameMax : f->id == "mp.title" ? kTitleMax : kRelayMax;
        bool changed = false;
        for(const char ch : e.text) if(ch >= 32 && ch < 127 && v->size() < max) { v->push_back(ch); changed = true; }
        return changed ? std::vector<Command>{{Command::Kind::form_changed, f->id}} : std::vector<Command>{};
    }
    if(e.kind == Kind::wheel) {
        if(screen_ == Screen::settings && !modal) scroll_ = std::clamp(scroll_ - e.wheel * 60, 0.0, std::max(0.0, content_height_ - view_height_));
        return {};
    }
    if(e.kind == Kind::pointer_move) {
        if(scroll_armed_ && dragging_.empty() && std::abs(e.y - press_y_) > 8) { dragging_ = "scroll"; pressed_.clear(); scroll_armed_ = false; }
        if(!dragging_.empty()) {
            if(dragging_.rfind("set.", 0) == 0) {
                for(const auto& it : items_) if(it.id == dragging_) return activate(it, e.x, true);
                return {};
            }
            if(dragging_ == "scroll") { scroll_ = std::clamp(scroll_ - (e.y - touch_scroll_y_), 0.0, std::max(0.0, content_height_ - view_height_)); touch_scroll_y_ = e.y; return {}; }
            if(dragging_.rfind("hud.", 0) == 0) {   // hudMove
                const std::string id = dragging_.substr(4);
                for(const auto& it : items_) if(it.id == dragging_) {
                    auto& rec = settings_->hud_layout()[id];
                    rec.x = std::clamp(e.x - drag_dx_, 0.0, std::max(0.0, last_w_ - it.rect.w)) / last_w_;
                    rec.y = std::clamp(e.y - drag_dy_, 0.0, std::max(0.0, last_h_ - it.rect.h)) / last_h_;
                    return {{Command::Kind::settings_changed, "hudLayout"}};
                }
            }
            return {};
        }
        if(const Item* it = hit(e.x, e.y); it && it->enabled && it->kind != item_hud) focus_id_ = it->id;
        return {};
    }
    if(e.kind == Kind::pointer_down) {
        pointer_down_ = true;
        const Item* it = hit(e.x, e.y);
        if(modal && !it) { dropdown_.clear(); return {}; }
        if(!it) {
            if(screen_ == Screen::settings && list_view_.contains(e.x, e.y)) { dragging_ = "scroll"; touch_scroll_y_ = e.y; }
            return {};
        }
        focus_id_ = it->enabled && it->kind != item_hud ? it->id : focus_id_;
        if(it->kind == item_slider && it->enabled) { dragging_ = it->id; return activate(*it, e.x, true); }
        if(it->kind == item_hud) {
            hud_selected_ = it->id.substr(4);
            if(!settings_->hud_layout().count(hud_selected_)) settings_->hud_layout()[hud_selected_] = {it->rect.x / last_w_, it->rect.y / last_h_, 100, 100};   // hudRecord
            dragging_ = it->id; drag_dx_ = e.x - it->rect.x; drag_dy_ = e.y - it->rect.y;
            return {};
        }
        pressed_ = it->id;
        scroll_armed_ = screen_ == Screen::settings && !modal && it->kind != item_tab && list_view_.contains(e.x, e.y);
        press_y_ = touch_scroll_y_ = e.y;
        return {};
    }
    if(e.kind == Kind::pointer_up) {
        pointer_down_ = false;
        scroll_armed_ = false;
        const bool was_drag = !dragging_.empty();
        const std::string drag = dragging_;
        dragging_.clear();
        if(was_drag) return drag.rfind("hud.", 0) == 0 ? std::vector<Command>{{Command::Kind::settings_changed, "hudLayout"}} : std::vector<Command>{};
        const Item* it = hit(e.x, e.y);
        const std::string pressed = pressed_;
        pressed_.clear();
        if(it && it->id == pressed) return activate(*it, e.x, true);
        return {};
    }
    return {};
}

// ------------------------------------------------------------------------------------------- drawing
void Menu::backdrop(Canvas& c, const MenuFrame& f, std::optional<Rect> hole) {
    const double W = c.width(), H = c.height();
    // --feral-bg (cover), with the pointer parallax of the presentation script (-8..8 px)
    const double px = f.pointer_x >= 0 ? (f.pointer_x / W - .5) : 0, py = f.pointer_y >= 0 ? (f.pointer_y / H - .5) : 0;
    auto draw_bg = [&](const Rect& clip) {
        if(clip.w <= 0 || clip.h <= 0) return;
        c.push_clip(clip);
        if(f.background.width > 0) {
            const double iw = f.background.width, ih = f.background.height, k = std::max(W / iw, H / ih) * 1.02;
            const double w = iw * k, h = ih * k, x = (W - w) / 2 - px * 8 * f.scale, y = (H - h) / 2 - py * 8 * f.scale;
            c.image(f.background, {x, y, w, h}, Color{.86f, .86f, .88f, 1});
        } else c.rect({0, 0, W, H}, kInk);
        c.gradient({0, 0, W, H * .5}, Color::hex(0x000000, .15f), Color::hex(0x000000, .0f));
        c.gradient({0, H * .5, W, H * .5}, Color::hex(0x000000, .0f), Color::hex(0x000000, .35f));
        c.pop_clip();
    };
    if(!hole) { draw_bg({0, 0, W, H}); return; }
    const Rect h = *hole;
    draw_bg({0, 0, W, h.y}); draw_bg({0, h.bottom(), W, H - h.bottom()});
    draw_bg({0, h.y, h.x, h.h}); draw_bg({h.right(), h.y, W - h.right(), h.h});
}

bool Menu::button(Canvas& c, const MenuFrame& f, const std::string& id, const Rect& r, const std::string& label, const std::string& number, bool primary, bool enabled, bool pointed) {
    add(id, r, item_button, enabled);
    const bool focus = id == focus_id_ && enabled;
    const double k = f.scale;
    // pointer-proximity outline (script 55): brighter as the pointer approaches
    double near = 0;
    if(f.pointer_x >= 0) { const double dx = std::max({r.x - f.pointer_x, 0.0, f.pointer_x - r.right()}), dy = std::max({r.y - f.pointer_y, 0.0, f.pointer_y - r.bottom()}); near = std::clamp(1 - std::hypot(dx, dy) / (90 * k), 0.0, 1.0); }
    // .cat-nav buttons: corners cut at 45 degrees (12 px), the smaller controls 10 px
    const auto shape = Canvas::cut_shape(r, pointed ? std::min(r.h * .24, 12 * k) : 10 * k);
    const float a = enabled ? 1.f : .45f;
    const Color top = focus ? Color::hex(0x2c3035, .95f * a) : primary ? Color::hex(0x24272b, .95f * a) : Color::hex(0x17191c, .9f * a);
    const Color bottom = focus ? Color::hex(0x1a1c1f, .95f * a) : Color::hex(0x0e1012, .92f * a);
    const Color outline = mix(kLine.with_alpha(.55f * a), kWhite.with_alpha(a), focus ? 1.f : static_cast<float>(near * .6));
    c.shape(shape, top, bottom, (focus ? 1.6 : 1.1) * k, outline);
    // etched diagonal hatch (cat-nav button background)
    c.push_clip(r.inset(2 * k));
    for(double x = r.x + 30 * k; x < r.right(); x += 46 * k) c.line(x, r.bottom(), x + r.h * .45, r.y, 1 * k, Color::hex(0xffffff, .025f));
    c.pop_clip();
    double size = std::min(18.56 * k, r.h * .38);
    TextStyle t = ts(Face::serif_regular, size, (focus ? kWhite : kText).with_alpha(a), .21, number.empty() ? Align::center : Align::left);
    const double room = r.w - (number.empty() ? 30 : 90) * k;
    while(t.size > 7 * k && c.measure(label, t) > room) { t.size *= .92; t.spacing *= .9; }   // long labels in narrow buttons shrink to fit
    size = t.size;
    c.text(number.empty() ? r.x + r.w / 2 : r.x + 28 * k, r.y + r.h / 2 + size * .36, label, t);
    if(!number.empty()) c.text(r.right() - 28 * k, r.y + r.h / 2 + 4 * k, number, ts(Face::serif_regular, 11 * k, kAsh.with_alpha(a), .2, Align::right));
    if(pointed) { c.line(r.right() - 14 * k, r.y + r.h * .65, r.right() - 8 * k, r.y + r.h * .35, 1 * k, kLine.with_alpha(.5f * a)); c.line(r.right() - 18 * k, r.y + r.h * .65, r.right() - 12 * k, r.y + r.h * .35, 1 * k, kLine.with_alpha(.5f * a)); }
    return focus;
}

void Menu::home(Canvas& c, const MenuFrame& f) {
    backdrop(c, f);
    const double k = f.scale, W = c.width(), H = c.height();
    const double left = f.safe.x + std::max(24 * k, std::min(98 * k, W * .077));
    const double top = f.safe.y + std::max(20 * k, (H - 560 * k) / 2 + 50 * k);   // legacy rects at 1280 x 720: buttons from y 356
    c.polyline(sigil(left + 37 * k, top + 20 * k, .62 * k), 1.6 * k, kText.with_alpha(.9f), true);
    c.line(left, top + 64 * k, left + 22 * k, top + 64 * k, 1 * k, kAsh);
    c.text(left + 34 * k, top + 68 * k, "THE FERAL CLAW", ts(Face::sans, 11 * k, kText.with_alpha(.85f), .32, Align::left, .2));
    const double title = std::min(82 * k, (W - left * 2) / 8.6);
    c.text(left, top + 68 * k + title * 1.04, "CAT ARENA", ts(Face::serif_regular, title, kWhite, .07));
    const double sub_y = top + 68 * k + title * 1.04 + 30 * k;
    c.line(left, sub_y - 4 * k, left + 50 * k, sub_y - 4 * k, 1 * k, kAsh.with_alpha(.6f));
    c.text(left + 67 * k, sub_y, "FERAL CLAW", ts(Face::serif_regular, 13 * k, kText.with_alpha(.8f), .42));
    c.line(left + 218 * k, sub_y - 4 * k, left + 268 * k, sub_y - 4 * k, 1 * k, kAsh.with_alpha(.6f));
    const double bw = std::min(400 * k, W - left * 2), bh = std::min(54 * k, (H - sub_y - 40 * k) / 4.6);
    double y = sub_y + 43 * k;
    const std::pair<const char*, const char*> nav[] = {{"PLAY", "01"}, {"MULTIPLAYER", "02"}, {"SETTINGS", "03"}, {"EXIT", "04"}};
    const char* ids[] = {"home.play", "home.multi", "home.settings", "home.exit"};
    for(int i = 0; i < 4; ++i) { button(c, f, ids[i], {left, y, bw, bh}, nav[i].first, nav[i].second, i == 0); y += bh + 10 * k; }
    if(W > 900 * k) {
        const double rx = f.safe.right() - std::min(98 * k, W * .077), ry = H * .72;
        c.line(rx - 78 * k, ry, rx, ry, 1 * k, kAsh.with_alpha(.7f));
        const TextStyle mark = ts(Face::serif_regular, 12 * k, kAsh, .3, Align::right);
        c.text(rx, ry + 30 * k, "NINE LIVES · NO MERCY", mark);
        c.text(rx, ry + 55 * k, "IRON / ASH / CLAW", mark);
        c.text(rx, ry + 79 * k, "V. XVII", mark);
    }
    if(!f.status.empty()) c.text(left, H - f.safe.y - 14 * k - (H - f.safe.bottom()), f.status, ts(Face::mono, 10 * k, kAsh, .02, Align::left, .2));
}

void Menu::maps(Canvas& c, const MenuFrame& f) {
    const double k = f.scale, W = c.width(), H = c.height();
    const Rect frame{f.safe.x + std::max(12 * k, (W - 1050 * k) / 2), f.safe.y + std::max(10 * k, (H - 640 * k) / 2), std::min(1050 * k, f.safe.w - 24 * k), std::min(640 * k, f.safe.h - 20 * k)};
    const double pad = std::min(37 * k, frame.w * .03);
    const double list_w = std::min(384 * k, frame.w * .38);
    const Rect preview{frame.x + pad + list_w + 28 * k, frame.y + 182 * k, frame.w - pad * 2 - list_w - 28 * k, std::min(310 * k, (frame.w - pad * 2 - list_w - 28 * k) * .55)};
    backdrop(c, f, preview);
    // frame panel (cut corners), drawn around the live preview
    c.push_clip({0, 0, W, preview.y}); c.shape(Canvas::cut_shape(frame, 16 * k), Color::hex(0x0e1012, .82f), Color::hex(0x0b0c0e, .86f), 1 * k, kLine.with_alpha(.25f)); c.pop_clip();
    c.push_clip({0, preview.bottom(), W, H}); c.shape(Canvas::cut_shape(frame, 16 * k), Color::hex(0x0e1012, .82f), Color::hex(0x0b0c0e, .86f), 1 * k, kLine.with_alpha(.25f)); c.pop_clip();
    c.push_clip({0, preview.y, preview.x, preview.h}); c.shape(Canvas::cut_shape(frame, 16 * k), Color::hex(0x0e1012, .82f), Color::hex(0x0b0c0e, .86f), 1 * k, kLine.with_alpha(.25f)); c.pop_clip();
    c.push_clip({preview.right(), preview.y, W, preview.h}); c.shape(Canvas::cut_shape(frame, 16 * k), Color::hex(0x0e1012, .82f), Color::hex(0x0b0c0e, .86f), 1 * k, kLine.with_alpha(.25f)); c.pop_clip();
    const double x0 = frame.x + pad, x1 = frame.right() - pad;
    c.text(x0, frame.y + 64 * k, "CAT ARENA", ts(Face::serif_regular, 34 * k, kWhite, .12));
    c.text(x0 + c.measure("CAT ARENA", ts(Face::serif_regular, 34 * k, kWhite, .12)) + 20 * k, frame.y + 64 * k, "FERAL CLAW", ts(Face::serif_regular, 10 * k, kAsh, .3));
    if(frame.w > 760 * k) { c.text(x1, frame.y + 48 * k, "THE FERAL CLAW", ts(Face::serif_regular, 10 * k, kAsh, .25, Align::right)); c.text(x1, frame.y + 65 * k, "IRON · ASH · BLOOD", ts(Face::serif_regular, 10 * k, kAsh, .25, Align::right)); }
    c.line(x0, frame.y + 86 * k, x1, frame.y + 86 * k, 1 * k, kLine.with_alpha(.3f));
    c.text(x0, frame.y + 138 * k, host_mode_ ? "HOST · CHOOSE YOUR ARENA" : "CHOOSE YOUR ARENA", ts(Face::serif_regular, std::min(36 * k, frame.w / 26), kWhite, .12));
    button(c, f, "maps.back", {x1 - 100 * k, frame.y + 107 * k, 100 * k, 38 * k}, "← BACK", {}, false, true, true);
    c.line(x0, frame.y + 162 * k, x1, frame.y + 162 * k, 1 * k, kLine.with_alpha(.3f));
    double y = preview.y;
    const double row_h = host_mode_ ? 42 * k : 52 * k, row_step = host_mode_ ? 48 * k : 62 * k;
    for(const auto& m : maps_) {
        const Rect r{x0, y, list_w, row_h};
        add("maps.map." + m.id, r, item_button, true);
        const bool sel = m.id == selected_map_, focus = focus_id_ == "maps.map." + m.id;
        c.shape(Canvas::cut_shape(r, 12 * k), (focus ? Color::hex(0x2c3035, .95f) : Color::hex(0x191b1e, .92f)), Color::hex(0x0e1012, .92f), (sel || focus ? 1.6 : 1) * k,
                sel ? kWhite : focus ? kText : kLine.with_alpha(.6f));
        c.text(r.x + 18 * k, r.y + (host_mode_ ? 18 : 22) * k, m.title, ts(Face::serif_regular, 12.5 * k, sel ? kWhite : kText, .1));
        std::string tag = m.tag;   // .cat-map-option small: text-transform uppercase
        for(char& ch : tag) if(ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 32);
        c.text(r.x + 18 * k, r.y + (host_mode_ ? 33 : 38) * k, tag, ts(Face::sans, 9 * k, kAsh, .1));
        c.line(r.right() - 14 * k, r.y + r.h * .62, r.right() - 8 * k, r.y + r.h * .38, 1 * k, kLine.with_alpha(.5f)); c.line(r.right() - 18 * k, r.y + r.h * .62, r.right() - 12 * k, r.y + r.h * .38, 1 * k, kLine.with_alpha(.5f));
        y += row_step;
    }
    if(!host_mode_) button(c, f, "maps.enter", {x0, y, list_w, 40 * k}, "ENTER ARENA →", {}, true);
    else {   // the legacy coopFields + coopHost on the host's map page; HOST ON THIS PC is the native built-in relay
        const bool busy = f.mp && (f.mp->in_room || f.mp->listing);
        text_field(c, f, "mp.name", {x0, y, list_w, 32 * k}, "PLAYER NAME", settings_->player_name, "Marine Cat"); y += 37 * k;
        text_field(c, f, "mp.title", {x0, y, list_w, 32 * k}, "SERVER NAME", settings_->server_name, "Your server name"); y += 37 * k;
        text_field(c, f, "mp.relay", {x0, y, list_w, 32 * k}, "SERVER ADDRESS", settings_->relay, "ws://host:8787/ws"); y += 39 * k;
        const double half = (list_w - 8 * k) / 2;
        button(c, f, "host.local", {x0, y, half, 38 * k}, "HOST ON THIS PC", {}, true, !busy, false);
        button(c, f, "host.create", {x0 + half + 8 * k, y, half, 38 * k}, "CREATE SERVER", {}, false, !busy, false);
        y += 46 * k;
        if(f.mp && !f.mp->status.empty())
            c.paragraph({x0, y, list_w, 40 * k}, f.mp->status, ts(Face::mono, 9.5 * k, f.mp->status_error ? Color::hex(0xe08a7a) : kBrass, 0, Align::left, .3, false), 13 * k);
    }
    // preview frame and caption (the map itself is the live render behind this hole)
    c.frame(preview, 1 * k, kLine.with_alpha(.5f));
    const MapEntry* cur = nullptr;
    for(const auto& m : maps_) if(m.id == selected_map_) cur = &m;
    if(cur) {
        c.gradient({preview.x, preview.bottom() - 40 * k, preview.w, 40 * k}, Color::hex(0x000000, 0), Color::hex(0x000000, .7f));
        c.text(preview.x + 13 * k, preview.bottom() - 10 * k, cur->title, ts(Face::serif_regular, 11 * k, kBrass, .1));
        c.line(preview.x + 14 * k, preview.bottom() + 8 * k, preview.right() - 14 * k, preview.bottom() + 8 * k, 1 * k, kLine.with_alpha(.4f));
        c.text(preview.x, preview.bottom() + 26 * k, "IN-GAME CAPTURE", ts(Face::serif_regular, 10 * k, kAsh, .25));
        c.text(preview.right(), preview.bottom() + 26 * k, cur->tag, ts(Face::serif_regular, 10 * k, kAsh, .15, Align::right));
        c.paragraph({preview.x, preview.bottom() + 36 * k, preview.w, 60 * k}, cur->description, ts(Face::sans, 12 * k, kText.with_alpha(.85f), 0, Align::left, 0, false), 18 * k);
    }
    c.line(x0, frame.bottom() - 58 * k, x1, frame.bottom() - 58 * k, 1 * k, kLine.with_alpha(.25f));
    c.text(x0, frame.bottom() - 37 * k, "✦ THE FERAL CLAW", ts(Face::serif_regular, 10 * k, kAsh, .2));
    c.text(x1, frame.bottom() - 37 * k, host_mode_ ? "HOST ON THIS PC: FRIENDS JOIN WITH YOUR ADDRESS, PORT 8787" : "CAT ARENA · NATIVE N10", ts(Face::serif_regular, 10 * k, kAsh, .2, Align::right));
}

void Menu::multiplayer(Canvas& c, const MenuFrame& f) {   // catMulti: "Fight together"
    backdrop(c, f);
    const double k = f.scale, W = c.width();
    const double left = f.safe.x + std::max(24 * k, std::min(98 * k, W * .077));
    double y = f.safe.y + 120 * k;
    c.text(left, y, "FIGHT TOGETHER", ts(Face::serif_regular, std::min(40 * k, W / 18), kWhite, .12)); y += 40 * k;
    c.text(left, y, "Stand together. Survive together. Up to four players.", ts(Face::sans, 13 * k, kText)); y += 30 * k;
    const double bw = std::min(400 * k, W - 2 * left);
    button(c, f, "multi.host", {left, y, bw, 54 * k}, "HOST A SERVER", "CREATE", true); y += 64 * k;
    button(c, f, "multi.join", {left, y, bw, 54 * k}, "JOIN A SERVER", "BROWSE"); y += 74 * k;
    const std::string note = f.mp && !f.mp->status.empty() ? f.mp->status
        : "F: hold for 4 seconds near a downed teammate to revive. Downed players can look around, but cannot move or shoot. The wave resets only when all teammates are down.";
    c.paragraph({left, y, bw, 80 * k}, note, ts(Face::mono, 11 * k, f.mp && f.mp->status_error ? Color::hex(0xe08a7a) : kBrass, 0, Align::left, .2, false), 17 * k);
    y += 80 * k;
    button(c, f, "multi.back", {left, y, 140 * k, 40 * k}, "← BACK");
}

void Menu::text_field(Canvas& c, const MenuFrame& f, const std::string& id, const Rect& r, const std::string& label, const std::string& value, const std::string& placeholder) {
    add(id, r, item_text);
    const double k = f.scale;
    const bool focus = focus_id_ == id;
    c.shape(Canvas::cut_shape(r, 6 * k), Color::hex(0x0d0f12, .95f), Color::hex(0x0a0b0d, .95f), (focus ? 1.5 : 1) * k, focus ? kWhite : kLine.with_alpha(.45f));
    const double label_w = 118 * k;
    c.text(r.x + 10 * k, r.y + r.h / 2 + 3.5 * k, label, ts(Face::mono, 8.5 * k, kAsh, .1, Align::left, .3, false));
    TextStyle t = ts(Face::sans, 12 * k, value.empty() ? kDim : kText, 0, Align::left, .1, false);
    const std::string shown = value.empty() && !focus ? placeholder : value;
    std::string clipped = shown;
    const double room = r.w - label_w - 16 * k;
    while(!clipped.empty() && c.measure(clipped, t) > room) clipped.erase(0, 1);   // keep the end (the caret) in view
    const double tx = r.x + label_w;
    c.text(tx, r.y + r.h / 2 + 4.5 * k, clipped, t);
    if(focus && std::fmod(f.time, 1.0) < .55) {
        const double cx = tx + (value.empty() ? 0 : c.measure(clipped, t)) + 1.5 * k;
        c.line(cx, r.y + 7 * k, cx, r.bottom() - 7 * k, 1.2 * k, kWhite);
    }
}

void Menu::servers(Canvas& c, const MenuFrame& f) {   // catServers + the legacy server browser (serverList / serverBrowserStatus / coopStatus)
    backdrop(c, f);
    const double k = f.scale, W = c.width(), H = c.height();
    const Rect panel{std::max(f.safe.x + 8 * k, (W - 820 * k) / 2), std::max(f.safe.y + 8 * k, (H - 600 * k) / 2), std::min(820 * k, f.safe.w - 16 * k), std::min(600 * k, f.safe.h - 16 * k)};
    c.shape(Canvas::cut_shape(panel, 16 * k), Color::hex(0x121417, .95f), Color::hex(0x0a0b0d, .95f), 1 * k, kLine.with_alpha(.45f));
    const double pad = 24 * k, x0 = panel.x + pad, w = panel.w - 2 * pad;
    c.text(x0, panel.y + 48 * k, "AVAILABLE SERVERS", ts(Face::serif_regular, std::min(30 * k, panel.w / 22), kWhite, .12));
    button(c, f, "servers.back", {panel.right() - pad - 100 * k, panel.y + 20 * k, 100 * k, 38 * k}, "← BACK", {}, false, true, true);
    c.line(panel.x, panel.y + 70 * k, panel.right(), panel.y + 70 * k, 1 * k, kLine.with_alpha(.3f));
    double y = panel.y + 84 * k;
    const double fw = (w - 8 * k) / 2;
    text_field(c, f, "mp.name", {x0, y, fw, 32 * k}, "PLAYER NAME", settings_->player_name, "Marine Cat");
    text_field(c, f, "mp.relay", {x0 + fw + 8 * k, y, fw, 32 * k}, "SERVER ADDRESS", settings_->relay, "ws://host:8787/ws");
    y += 44 * k;
    const bool listing = f.mp && f.mp->listing, busy = f.mp && f.mp->in_room;
    button(c, f, "servers.refresh", {x0, y, 170 * k, 34 * k}, listing ? "CHECKING..." : "REFRESH LIST", {}, true, !listing && !busy, false);
    const std::string list_status = f.mp ? f.mp->list_status : "Multiplayer is not available in this build.";
    c.text(x0 + 186 * k, y + 22 * k, list_status, ts(Face::mono, 10.5 * k, kBrass, .02, Align::left, .3, false));
    y += 46 * k;
    const double list_bottom = panel.bottom() - 74 * k, row_h = 50 * k;
    if(!f.mp || f.mp->rooms.empty()) {
        const Rect r{x0, y, w, 56 * k};
        c.shape(Canvas::cut_shape(r, 8 * k), Color::hex(0x101315, .75f), Color::hex(0x101315, .75f), 1 * k, kLine.with_alpha(.3f));
        const std::string empty = !f.mp ? "" : listing ? "Checking the multiplayer server..." : f.mp->list_status.find("LIVE SERVER") != std::string::npos ? "NO LIVE SERVERS · Host a server from the Multiplayer menu."
                                                                                                           : "Cannot reach the server directory. Check your connection and the server address above, then refresh.";
        c.text(r.x + r.w / 2, r.y + 33 * k, empty, ts(Face::mono, 10.5 * k, kText, 0, Align::center, .3, false));
    } else {
        std::size_t shown = 0;
        for(const auto& room : f.mp->rooms) {
            if(y + row_h > list_bottom) break;
            const Rect r{x0, y, w, row_h - 6 * k};
            c.shape(Canvas::cut_shape(r, 8 * k), Color::hex(0x161a1d, .9f), Color::hex(0x101315, .9f), 1 * k, kLine.with_alpha(.35f));
            c.rect({r.x, r.y, 3 * k, r.h}, kBrass.with_alpha(.8f));
            c.text(r.x + 14 * k, r.y + 18 * k, room.title, ts(Face::sans, 13 * k, kWhite, 0, Align::left, .25, false));
            c.text(r.x + 14 * k, r.y + 35 * k, "HOST " + room.host + " · " + room.map_title + " · " + std::to_string(room.players) + "/" + std::to_string(room.max_players) + " PLAYERS · CODE " + room.id,
                   ts(Face::mono, 9.5 * k, kAsh, 0, Align::left, .25, false));
            const bool full = room.players >= room.max_players || !room.open;
            button(c, f, "join." + room.id, {r.right() - 104 * k, r.y + 6 * k, 96 * k, r.h - 12 * k}, !room.joinable ? "OTHER MAP" : full ? "FULL" : "JOIN", {}, true, room.joinable && !full && !busy, false);
            y += row_h; ++shown;
        }
        if(shown < f.mp->rooms.size()) c.text(x0, y + 12 * k, "+" + std::to_string(f.mp->rooms.size() - shown) + " MORE", ts(Face::mono, 10 * k, kAsh, 0, Align::left, .3, false));
    }
    const double sy = panel.bottom() - 58 * k;
    c.line(x0, sy, x0 + w, sy, 1 * k, kLine.with_alpha(.25f));
    const std::string status = f.mp && !f.mp->status.empty() ? f.mp->status : "Pick an available server above, or create your own. No join codes needed.";
    c.paragraph({x0, sy + 8 * k, w, 44 * k}, status, ts(Face::mono, 10 * k, f.mp && f.mp->status_error ? Color::hex(0xe08a7a) : kBrass, 0, Align::left, .3, false), 14 * k);
    if(focus_id_.empty()) focus_id_ = "servers.refresh";
}

void Menu::connecting(Canvas& c, const MenuFrame& f) {
    backdrop(c, f);
    const double k = f.scale, W = c.width(), H = c.height();
    const Rect card{(W - std::min(460 * k, W - 20 * k)) / 2, (H - 230 * k) / 2, std::min(460 * k, W - 20 * k), 230 * k};
    c.shape(Canvas::cut_shape(card, 14 * k), Color::hex(0x15171a, .98f), Color::hex(0x0b0c0e, .98f), 1 * k, kLine.with_alpha(.6f));
    c.text(card.x + card.w / 2, card.y + 36 * k, "MULTIPLAYER", ts(Face::serif_regular, 10 * k, kAsh, .3, Align::center));
    const int dots = static_cast<int>(f.time * 3) % 4;
    c.text(card.x + card.w / 2, card.y + 80 * k, "CONNECTING" + std::string(static_cast<std::size_t>(dots), '.'), ts(Face::serif_regular, 26 * k, kWhite, .12, Align::center));
    c.paragraph({card.x + 30 * k, card.y + 100 * k, card.w - 60 * k, 60 * k}, f.mp ? f.mp->status : "", ts(Face::mono, 10.5 * k, f.mp && f.mp->status_error ? Color::hex(0xe08a7a) : kBrass, 0, Align::center, .3, false), 15 * k);
    button(c, f, "connect.cancel", {card.x + 30 * k, card.bottom() - 58 * k, card.w - 60 * k, 40 * k}, "CANCEL");
}

void Menu::settings(Canvas& c, const MenuFrame& f) {
    const double k = f.scale, W = c.width(), H = c.height();
    if(before_settings_ == Screen::home) backdrop(c, f); else c.rect({0, 0, W, H}, Color::hex(0x050607, .55f));
    const Rect panel{std::max(f.safe.x + 8 * k, (W - 900 * k) / 2), std::max(f.safe.y + 8 * k, (H - 580 * k) / 2), std::min(900 * k, f.safe.w - 16 * k), std::min(580 * k, f.safe.h - 16 * k)};
    c.shape(Canvas::cut_shape(panel, 16 * k), Color::hex(0x121417, .97f), Color::hex(0x0a0b0d, .97f), 1 * k, kLine.with_alpha(.45f));
    const double pad = 18 * k;
    c.text(panel.x + pad, panel.y + 42 * k, "GAME SETTINGS", ts(Face::serif_regular, std::min(30 * k, panel.w / 22), kWhite, .14));
    c.line(panel.x, panel.y + 62 * k, panel.right(), panel.y + 62 * k, 1 * k, kLine.with_alpha(.35f));
    // tabs: all six reachable with mouse, keyboard (Left / Right on a tab, Q / E, PgUp / PgDn on gamepad shoulders)
    // six tabs in a row; a 3 x 2 grid on narrow windows and 2 x 3 on phones (v0.18 fix branch)
    const int columns = (panel.w - 2 * pad) / 6 >= 96 * k ? 6 : (panel.w - 2 * pad) / 3 >= 90 * k ? 3 : 2;
    const double tab_y = panel.y + 70 * k, tab_row = 34 * k, tab_w = (panel.w - 2 * pad) / columns;
    const double tab_h = tab_row * (6 / columns) + 4 * k * (6 / columns - 1);
    for(int t = 0; t < 6; ++t) {
        const Rect r{panel.x + pad + (t % columns) * tab_w, tab_y + (t / columns) * (tab_row + 4 * k), tab_w - 2 * k, tab_row};
        const std::string id = "settings.tab." + std::to_string(t);
        add(id, r, item_tab);
        const bool sel = static_cast<int>(tab_) == t, focus = focus_id_ == id;
        c.gradient(r, sel ? Color::hex(0x30343a) : Color::hex(0x1a1c1f), sel ? Color::hex(0x15171a) : Color::hex(0x101114));
        if(sel) c.rect({r.x, r.bottom() - 2 * k, r.w, 2 * k}, kWhite);
        if(focus) c.frame(r, 1.5 * k, kWhite);
        const double size = std::min(10.5 * k, r.w / 13);
        c.text(r.x + r.w / 2, r.y + r.h / 2 + size * .36, tab_title(static_cast<SettingsTab>(t)), ts(Face::serif_regular, size, sel ? kWhite : kText.with_alpha(.75f), .08, Align::center));
    }
    // scrolling list of the tab's controls
    list_view_ = {panel.x + pad, tab_y + tab_h + 10 * k, panel.w - 2 * pad, panel.bottom() - (tab_y + tab_h + 10 * k) - 64 * k};
    view_height_ = list_view_.h;
    std::optional<Rect> dropdown_anchor;
    const SettingDef* dropdown_def = nullptr;
    c.push_clip(list_view_);
    double y = list_view_.y - scroll_;
    const double w = list_view_.w - 14 * k;
    for(const auto& d : setting_defs()) {
        if(d.tab != tab_) continue;
        if(!d.section.empty()) {
            if(y > list_view_.y - scroll_ + 1) { c.line(list_view_.x, y + 8 * k, list_view_.x + w, y + 8 * k, 1 * k, Color::hex(0xc9a874, .35f)); y += 14 * k; }
            c.text(list_view_.x, y + 18 * k, d.section, ts(Face::mono, 12 * k, kBrass, .04, Align::left, .45));
            y += 28 * k;
        }
        const bool stored = d.support == Support::stored;
        const std::string id = "set." + d.id;
        const bool focus = focus_id_ == id;
        const float a = stored ? .5f : 1.f;
        const std::string value = settings_->text(d.id);
        double row_h = 0;
        if(d.kind == SettingKind::range) {
            row_h = 52 * k;
            const Rect row{list_view_.x, y, w, row_h};
            const Rect track{row.x + 6 * k, y + 34 * k, w - 12 * k, 8 * k};
            add(id, {track.x, y + 22 * k, track.w, 28 * k}, item_slider, !stored);
            if(focus) c.rect(row, Color::hex(0xffffff, .05f));
            c.text(row.x, y + 18 * k, d.label, ts(Face::mono, 12 * k, kBrass.with_alpha(a), .04, Align::left, .45));
            c.text(row.right(), y + 18 * k, format_value(d, value), ts(Face::mono, 12 * k, kBrass.with_alpha(a), .04, Align::right, .45));
            const double t = std::clamp((settings_->number(d.id) - d.min) / std::max(1e-9, d.max - d.min), 0.0, 1.0);
            c.rect(track, Color::hex(0x3a3d42, a));
            c.rect({track.x, track.y, track.w * t, track.h}, Color::hex(0xc4c8cc, a));
            c.circle(track.x + track.w * t, track.y + track.h / 2, (focus ? 9 : 7.5) * k, (focus ? kWhite : Color::hex(0xc4c8cc)).with_alpha(a), 20);
        } else if(d.kind == SettingKind::select) {
            row_h = 46 * k;
            const Rect box{list_view_.x + w * .48, y + 4 * k, w * .52, 38 * k};
            add(id, box, item_choice, !stored);
            if(focus) c.rect({list_view_.x, y, w, row_h}, Color::hex(0xffffff, .05f));
            c.text(list_view_.x, y + 28 * k, d.label, ts(Face::mono, 12 * k, kBrass.with_alpha(a), .04, Align::left, .45));
            c.rect(box, Color::hex(0x0d0f12, .95f * a));
            c.line(box.x, box.bottom(), box.right(), box.bottom(), 1 * k, kLine.with_alpha(.6f * a));
            if(focus) c.frame(box, 1.4 * k, kWhite);
            c.push_clip(box.inset(2 * k));
            c.text(box.x + 10 * k, box.y + 24 * k, format_value(d, value), ts(Face::sans, 13 * k, kText.with_alpha(a), 0, Align::left, 0, false));
            c.pop_clip();
            c.text(box.right() - 12 * k, box.y + 24 * k, "▼", ts(Face::sans, 10 * k, kText.with_alpha(a), 0, Align::right, 0, false));
            if(dropdown_ == d.id) { dropdown_anchor = box; dropdown_def = &d; }
        } else if(d.kind == SettingKind::toggle) {
            row_h = 44 * k;
            const Rect row{list_view_.x, y, w, row_h};
            add(id, row, item_check, !stored);
            if(focus) c.rect(row, Color::hex(0xffffff, .05f));
            c.text(row.x, y + 27 * k, d.label, ts(Face::sans, 13 * k, kText.with_alpha(a), 0, Align::left, .25));
            const Rect box{row.right() - 24 * k, y + 11 * k, 21 * k, 21 * k};
            c.rect(box, value == "true" ? Color::hex(0xc4c8cc, a) : Color::hex(0x23262a, a));
            c.frame(box, 1 * k, kLine.with_alpha(a));
            if(value == "true") c.polyline({{box.x + 5 * k, box.y + 11 * k}, {box.x + 9 * k, box.y + 15 * k}, {box.x + 16 * k, box.y + 6 * k}}, 2.6 * k, Color::hex(0x121417, a), false);
            c.line(row.x, row.bottom() - .5 * k, row.right(), row.bottom() - .5 * k, 1 * k, kLine.with_alpha(.15f));
        } else {
            row_h = 46 * k;
            button(c, f, id, {list_view_.x, y + 4 * k, w, 38 * k}, d.label, {}, false, !stored, false);
        }
        y += row_h;
        if(stored || d.support == Support::partial || (!d.note.empty() && (d.kind == SettingKind::range || d.section.size() > 0))) {
            const std::string note = stored ? "N10 · " + d.note : d.note;
            if(!note.empty()) y += c.paragraph({list_view_.x, y, w, 60 * k}, note, ts(Face::mono, 10.5 * k, stored ? kWarm.scaled(1.3f) : Color::hex(0xc9cdd1, .75f), 0, Align::left, .1, false), 15 * k) + 4 * k;
        }
    }
    content_height_ = y + scroll_ - list_view_.y + 10 * k;
    c.pop_clip();
    scroll_ = std::clamp(scroll_, 0.0, std::max(0.0, content_height_ - view_height_));
    if(content_height_ > view_height_) {   // scroll bar
        const double bar_h = std::max(30 * k, view_height_ * view_height_ / content_height_);
        const double bar_y = list_view_.y + (view_height_ - bar_h) * (scroll_ / std::max(1.0, content_height_ - view_height_));
        c.rect({list_view_.right() - 5 * k, list_view_.y, 3 * k, view_height_}, Color::hex(0xffffff, .06f));
        c.rect({list_view_.right() - 6 * k, bar_y, 5 * k, bar_h}, Color::hex(0xc4c8cc, .55f));
    }
    const bool from_pause = before_settings_ == Screen::pause;
    button(c, f, "settings.close", {panel.x + pad, panel.bottom() - 54 * k, panel.w - 2 * pad, 42 * k}, from_pause ? "← PAUSE MENU" : "BACK TO MAIN MENU", {}, true);
    const auto& status = tab_ == SettingsTab::hud && !f.display_status.empty() ? f.display_status : f.status;
    if(!status.empty()) c.text(panel.right() - pad, panel.y + 42 * k, status, ts(Face::mono, 9.5 * k, kAsh, 0, Align::right, .2));
    if(dropdown_anchor && dropdown_def) {   // the open <select>
        const double oh = 34 * k;
        const double total = oh * static_cast<double>(dropdown_def->options.size());
        double top = dropdown_anchor->bottom();
        if(top + total > H - 8 * k) top = std::max(8 * k, dropdown_anchor->y - total);
        const Rect box{dropdown_anchor->x, top, dropdown_anchor->w, total};
        c.rect(box.inset(-2 * k), Color::hex(0x000000, .6f));
        c.rect(box, Color::hex(0x15171a, .99f));
        c.frame(box, 1 * k, kLine);
        for(std::size_t i = 0; i < dropdown_def->options.size(); ++i) {
            const Rect r{box.x, box.y + oh * static_cast<double>(i), box.w, oh};
            const std::string oid = "opt." + std::to_string(i);
            add(oid, r, item_option);
            const bool cur = dropdown_def->options[i].value == settings_->text(dropdown_def->id);
            if(focus_id_ == oid) c.rect(r, Color::hex(0x3a3f46));
            c.text(r.x + 12 * k, r.y + 22 * k, (cur ? "✓ " : "   ") + dropdown_def->options[i].label, ts(Face::sans, 13 * k, kText, 0, Align::left, 0, false));
        }
    }
}

void Menu::exit_confirm(Canvas& c, const MenuFrame& f) {
    home(c, f);
    items_.clear();
    const double k = f.scale, W = c.width(), H = c.height();
    c.rect({0, 0, W, H}, Color::hex(0x000000, .6f));
    const Rect card{(W - std::min(420 * k, W - 20 * k)) / 2, (H - 276 * k) / 2, std::min(420 * k, W - 20 * k), 276 * k};
    c.shape(Canvas::cut_shape(card, 14 * k), Color::hex(0x15171a, .98f), Color::hex(0x0b0c0e, .98f), 1 * k, kLine.with_alpha(.6f));
    c.text(card.x + card.w / 2, card.y + 36 * k, "UNTIL THE NEXT HUNT", ts(Face::serif_regular, 10 * k, kAsh, .3, Align::center));
    // #catExitDialog h2: uppercase serif over two lines, letter-spaced
    c.text(card.x + card.w / 2, card.y + 72 * k, "LEAVE THE", ts(Face::serif_regular, 28 * k, kWhite, .12, Align::center));
    c.text(card.x + card.w / 2, card.y + 106 * k, "ARENA?", ts(Face::serif_regular, 28 * k, kWhite, .12, Align::center));
    c.text(card.x + card.w / 2, card.y + 134 * k, "Close Cat Arena and return to your device.", ts(Face::sans, 12 * k, kText, 0, Align::center));
    button(c, f, "exit.confirm", {card.x + 30 * k, card.y + 160 * k, card.w - 60 * k, 40 * k}, "EXIT GAME", {}, true);
    button(c, f, "exit.cancel", {card.x + 30 * k, card.y + 212 * k, card.w - 60 * k, 40 * k}, "STAY");
}

void Menu::loading(Canvas& c, const MenuFrame& f) {
    backdrop(c, f);
    const double k = f.scale, W = c.width(), H = c.height();
    const Rect card{std::max(8 * k, (W - 790 * k) / 2), std::max(8 * k, (H - 534 * k) / 2), std::min(790 * k, W - 16 * k), std::min(534 * k, H - 16 * k)};
    c.shape(Canvas::cut_shape(card, 16 * k), Color::hex(0x141619, .97f), Color::hex(0x0c0d0f, .97f), 1 * k, kLine.with_alpha(.6f));
    const double pad = 25 * k;
    c.text(card.x + pad, card.y + 26 * k, "// FERAL CLAW // " + loading_.map_title + " //", ts(Face::serif_regular, 10 * k, kAsh, .25));
    c.text(card.x + pad, card.y + 64 * k, "CAT ARENA", ts(Face::serif_regular, 36 * k, kWhite, .1));
    c.text(card.right() - pad, card.y + 40 * k, "NATIVE N10", ts(Face::mono, 10.5 * k, Color::hex(0xc9a874), .03, Align::right, .45));
    c.text(card.right() - pad, card.y + 54 * k, "CAT ARENA", ts(Face::mono, 10.5 * k, Color::hex(0xc9a874), .03, Align::right, .45));
    // scenery band: keep silhouettes (the legacy SVG towers), drawn as outlines
    const Rect band{card.x, card.y + 82 * k, card.w, 166 * k};
    c.gradient(band, Color::hex(0x3b3c3e), Color::hex(0x202224));
    c.push_clip(band);
    for(double s = 0; s < band.h; s += 4 * k) c.line(band.x, band.y + s, band.right(), band.y + s, 1 * k, Color::hex(0x000000, .12f));
    const double base = band.bottom();
    const double towers[][3] = {{.12, 70, 60}, {.30, 80, 46}, {.43, 100, 70}, {.58, 70, 56}, {.75, 72, 52}, {.87, 66, 50}};
    for(const auto& t : towers) {
        const double cx = band.x + band.w * t[0], th = t[1] * k, tw = t[2] * k;
        c.polyline({{cx - tw / 2, base}, {cx - tw / 2, base - th}, {cx - tw / 4, base - th}, {cx - tw / 4, base - th - 12 * k}, {cx + tw / 4, base - th - 12 * k}, {cx + tw / 4, base - th}, {cx + tw / 2, base - th}, {cx + tw / 2, base}},
                   3 * k, Color::hex(0xb7b29c, .8f), false);
    }
    c.ring(band.x + band.w / 2, base + band.w * .55, band.w * .78, 5 * k, Color::hex(0xb7b29c, .45f), 90);
    c.pop_clip();
    c.line(band.x, band.bottom(), band.right(), band.bottom(), 2 * k, Color::hex(0xc4c8cc, .8f));
    c.text(band.x + pad, band.bottom() - 14 * k, "MAP // " + loading_.map_title, ts(Face::mono, 12 * k, kText, .1, Align::left, .45));
    double y = band.bottom() + 30 * k;
    const int pct = loading_.ready ? 100 : std::clamp(loading_.phase * 100 / 7, 0, 99);
    c.text(card.x + pad, y, "MISSION PREPARATION", ts(Face::mono, 10.5 * k, Color::hex(0xc9a874), .1, Align::left, .45));
    c.text(card.right() - pad, y, std::to_string(pct) + "%", ts(Face::mono, 10.5 * k, Color::hex(0xc9a874), .1, Align::right, .45));
    y += 32 * k;
    static const char* const stages[] = {"GENERATE MAP BLUEPRINT", "PREPARE TERRAIN", "COMPILE COLLISION", "BUILD ENEMY NAVIGATION", "BAKE GEOMETRY + LIGHTING", "PLACE ENEMIES + PICKUPS", "WARM UP GRAPHICS"};
    const std::string msg = !loading_.error.empty() ? "INITIALIZATION INTERRUPTED" : loading_.ready ? loading_.map_title + " READY · AWAITING DEPLOYMENT" : std::string(stages[std::min(loading_.phase, 6)]) + "...";
    c.text(card.x + pad, y, msg, ts(Face::sans, std::min(22 * k, (card.w - 2 * pad) / 38), Color::hex(0xefe3c7), .02, Align::left, .5));
    y += 18 * k;
    const Rect bar{card.x + pad, y, card.w - 2 * pad, 15 * k};
    c.rect(bar, Color::hex(0x1d2023));
    c.gradient_h({bar.x, bar.y, bar.w * pct / 100.0, bar.h}, Color::hex(0x6c7279), Color::hex(0xe6e8ea));
    c.frame(bar, 1 * k, kLine.with_alpha(.5f));
    y += 32 * k;
    c.text(card.x + pad, y, loading_.info, ts(Face::mono, 10.5 * k, Color::hex(0xc9a874), 0, Align::left, .3));
    c.text(card.right() - pad, y, "PHASE " + std::to_string(std::min(loading_.phase, 7)) + "/7", ts(Face::mono, 10.5 * k, Color::hex(0xc9a874), 0, Align::right, .3));
    y += 26 * k;
    for(int i = 0; i < 7; ++i) {
        const double sx = card.x + pad + (i % 2) * (card.w - 2 * pad) / 2, sy = y + (i / 2) * 19 * k;
        const bool done = i < loading_.phase || loading_.ready, now = i == loading_.phase && !loading_.ready;
        c.text(sx, sy, std::string(done ? "✓ " : now ? "▶ " : "· ") + stages[i], ts(Face::mono, 10.5 * k, done ? Color::hex(0xa9c49a) : now ? kWhite : kAsh, .02, Align::left, .35));
    }
    y += 4 * 19 * k;
    c.line(card.x + pad, y, card.right() - pad, y, 1 * k, kLine.with_alpha(.3f));
    if(!loading_.error.empty()) c.paragraph({card.x + pad, y + 8 * k, card.w - 2 * pad, 40 * k}, "LOADING FAILED // " + loading_.error, ts(Face::mono, 10.5 * k, Color::hex(0xe08a7a), 0, Align::left, .3, false), 15 * k);
    else c.text(card.x + pad, y + 32 * k, loading_.ready ? "READY // Press ENTER THE ARENA to capture your mouse and play." : "TIP // Movement: Shift sprint · Ctrl/C crouch-slide · Space bunnyhop · strafe in air.",
                ts(Face::mono, 10.5 * k, Color::hex(0xc9a874), 0, Align::left, .3));
    if(loading_.ready) { button(c, f, "loading.enter", {card.right() - pad - 192 * k, y + 12 * k, 192 * k, 40 * k}, "ENTER THE ARENA ▶", {}, true); if(focus_id_.empty()) focus_id_ = "loading.enter"; }
}

void Menu::pause(Canvas& c, const MenuFrame& f) {
    const double k = f.scale, W = c.width(), H = c.height();
    c.rect({0, 0, W, H}, Color::hex(0x050607, .62f));
    const Rect card{(W - std::min(434 * k, W - 16 * k)) / 2, std::max(8 * k, (H - 402 * k) / 2), std::min(434 * k, W - 16 * k), std::min(402 * k, H - 16 * k)};
    c.shape(Canvas::cut_shape(card, 18 * k), Color::hex(0x15171a, .97f), Color::hex(0x0c0d0f, .97f), 1 * k, kLine.with_alpha(.55f));
    std::string map_title = f.current_map;
    for(const auto& m : maps_) if(m.id == f.current_map) map_title = m.title;
    c.text(card.x + card.w / 2, card.y + 30 * k, "// CAT ARENA · " + map_title + " //", ts(Face::serif_regular, 10 * k, kAsh, .25, Align::center));
    const bool in_room = f.mp && f.mp->in_room;
    c.text(card.x + card.w / 2, card.y + 68 * k, in_room ? "GAME MENU" : "GAME PAUSED", ts(Face::serif_regular, 34 * k, kWhite, .1, Align::center));
    c.paragraph({card.x + 30 * k, card.y + 82 * k, card.w - 60 * k, 40 * k}, in_room ? "Your squad is still fighting: the arena keeps running while this menu is open."
                : f.touch ? "Resume, open settings or edit your touch layout without losing the arena." : "Resume, open settings or edit your HUD layout without losing the arena.",
                ts(Face::sans, 12.5 * k, kText.with_alpha(.85f), 0, Align::center, 0, false), 20 * k);
    double y = card.y + 134 * k;
    // N8: in a server the map is the room's (no CHOOSE ANOTHER MAP) and RETURN leaves the server; the game keeps running
    const bool room = f.mp && f.mp->in_room;
    const std::pair<const char*, const char*> b[] = {{"pause.resume", "▶ RESUME GAME"}, {"pause.settings", "⚙ SETTINGS / CONTROLS"}, {"pause.customize", "✥ CUSTOMIZE HUD"}, {"pause.change_map", "CHOOSE ANOTHER MAP"},
                                                     {room ? "pause.leave" : "pause.return", room ? "LEAVE SERVER · MAIN MENU" : "RETURN TO MAIN MENU"}};
    for(int i = 0; i < 5; ++i) { button(c, f, b[i].first, {card.x + 28 * k, y, card.w - 56 * k, 40 * k}, b[i].second, {}, i == 0, !(room && i == 3)); y += 46 * k; }
    c.text(card.x + card.w / 2, card.bottom() - 18 * k, f.platform + " · VULKAN · " + (room ? f.mp->footer : std::string("SOLO")), ts(Face::mono, 10 * k, Color::hex(0xb7bdc4), .06, Align::center, .35));
    if(focus_id_.empty()) focus_id_ = "pause.resume";
}

void Menu::hud_editor(Canvas& c, const MenuFrame& f) {
    const double k = f.scale, W = c.width();
    for(const auto& e : f.hud) add("hud." + e.id, e.rect, item_hud);
    const Rect bar{std::max(8 * k, (W - 760 * k) / 2), f.safe.y + 8 * k, std::min(760 * k, W - 16 * k), 52 * k};
    c.shape(Canvas::cut_shape(bar, 10 * k), Color::hex(0x15171a, .96f), Color::hex(0x0c0d0f, .96f), 1 * k, kLine.with_alpha(.6f));
    double x = bar.x + 12 * k;
    c.text(x, bar.y + 31 * k, "✥ HUD EDITOR", ts(Face::serif_regular, 12 * k, kWhite, .12)); x += 120 * k;
    std::string name = hud_selected_;
    for(const auto& h : hud_items()) if(hud_selected_ == h.id) name = h.name;
    const auto rec_it = settings_->hud_layout().find(hud_selected_);
    const HudPlacement rec = rec_it == settings_->hud_layout().end() ? HudPlacement{0, 0, 100, 100} : rec_it->second;
    auto field = [&](const std::string& id, const std::string& label, const std::string& value, double w) {
        const Rect r{x, bar.y + 9 * k, w, 34 * k};
        add(id, r, item_choice);
        const bool focus = focus_id_ == id;
        c.rect(r, Color::hex(0x0d0f12));
        if(focus) c.frame(r, 1.4 * k, kWhite);
        c.text(r.x + 8 * k, r.y + 13 * k, label, ts(Face::mono, 8.5 * k, kAsh, .1, Align::left, .3, false));
        c.text(r.x + 8 * k, r.y + 28 * k, "◀ " + value + " ▶", ts(Face::sans, 11.5 * k, kText, 0, Align::left, .2, false));
        x += w + 8 * k;
    };
    field("editor.item", "ELEMENT", name, 176 * k);
    field("editor.size", "SIZE", std::to_string(rec.size) + "%", 96 * k);
    field("editor.alpha", "ALPHA", std::to_string(rec.opacity) + "%", 96 * k);
    button(c, f, "editor.reset", {x, bar.y + 9 * k, 100 * k, 34 * k}, "RESET ITEM", {}, false, true, false); x += 108 * k;
    button(c, f, "editor.done", {x, bar.y + 9 * k, std::max(80 * k, bar.right() - x - 12 * k), 34 * k}, "✓ SAVE / DONE", {}, true, true, false);
    {   // help line at the bottom centre, where no HUD element sits by default
        const std::string help = "Drag highlighted panels or touch controls. Keyboard: Tab chooses, arrows move, Left/Right on SIZE / ALPHA resize or fade.";
        TextStyle hs = ts(Face::mono, 10.5 * k, kBrass, 0, Align::center, .3);
        while(hs.size > 7 * k && c.measure(help, hs) > std::max(300 * k, W - 600 * k)) hs.size *= .92;   // between the health and weapon panels
        const double hy = f.safe.bottom() - 14 * k;
        c.rect({W / 2 - c.measure(help, hs) / 2 - 10 * k, hy - 16 * k, c.measure(help, hs) + 20 * k, 24 * k}, Color::hex(0x050607, .78f));
        c.text(W / 2, hy, help, hs);
    }
    if(focus_id_.empty()) focus_id_ = "editor.item";
}

void Menu::build(Canvas& c, const MenuFrame& f) {
    items_.clear();
    last_w_ = c.width(); last_h_ = c.height();
    switch(screen_) {
        case Screen::home: home(c, f); break;
        case Screen::maps: maps(c, f); break;
        case Screen::multiplayer: multiplayer(c, f); break;
        case Screen::settings: settings(c, f); break;
        case Screen::exit_confirm: exit_confirm(c, f); break;
        case Screen::loading: loading(c, f); break;
        case Screen::pause: pause(c, f); break;
        case Screen::hud_editor: hud_editor(c, f); break;
        case Screen::servers: servers(c, f); break;
        case Screen::connecting: connecting(c, f); break;
        case Screen::none: break;
    }
    focus_ = items_.size();
    for(std::size_t i = 0; i < items_.size(); ++i) if(items_[i].id == focus_id_) focus_ = i;
    if(focus_ == items_.size() || !items_[focus_].enabled) {   // first enabled item (in a dropdown: an item_option)
        for(std::size_t i = 0; i < items_.size(); ++i)
            if(items_[i].enabled && items_[i].kind != item_hud && (dropdown_.empty() ? items_[i].kind != item_option : items_[i].kind == item_option)) { focus_ = i; focus_id_ = items_[i].id; break; }
    }
}

}  // namespace cat::ui
