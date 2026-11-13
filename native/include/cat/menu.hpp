#pragma once
// N7 menus in the legacy routing (index.html 16529-16698 "Original Cat Arena presentation and explicit
// menu routing", mobile-hud.js pause / settings / HUD editor, entry.js loading screen): home, map
// selection with the live in-game view, multiplayer (N8), settings (six tabs), exit confirmation,
// loading, pause and the HUD editor. Fully usable with keyboard, mouse, touch and gamepad (the
// v0.18 fix branch's requirements: settings scroll and keyboard navigation). GPU/SDL independent: the
// shell feeds events in and executes the returned commands.
#include "cat/game_settings.hpp"
#include "cat/ui.hpp"
#include "cat/ui_hud.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cat::ui {

// N8: `maps` doubles as the host's map choice (host_mode()), `servers` is the server browser (join),
// `connecting` waits for the relay's answer.
enum class Screen : std::uint8_t { none, home, maps, multiplayer, settings, exit_confirm, loading, pause, hud_editor, servers, connecting };
enum class NavKey : std::uint8_t { up, down, left, right, accept, back, next_tab, prev_tab, page_up, page_down, first, last, next_item, prev_item, erase };   // erase: Backspace in a text field

struct MenuEvent {
    enum class Kind : std::uint8_t { key, pointer_move, pointer_down, pointer_up, wheel, text } kind{Kind::key};
    NavKey key{};
    double x{}, y{}, wheel{};
    bool shift{};
    std::string text;   // Kind::text: typed characters (UTF-8) for the focused text field
};

struct Command {
    enum class Kind : std::uint8_t {
        preview_map,       // maps page: show this map (arg = id) behind the preview frame
        start_game,        // ENTER ARENA (arg = map id): load the map, show the loading screen
        enter_arena,       // loading screen: ENTER THE ARENA (capture the mouse, start the clock)
        resume,            // pause: resume (capture the mouse)
        return_to_menu,    // pause: back to the home screen (stops the game)
        change_map,        // pause: choose another map (legacy: return to menu, maps page)
        quit,              // exit dialog confirmed
        settings_changed,  // arg = control id ("" = several)
        test_audio, fullscreen_now, debug_view, hud_editor_done, reset_hud,
        // N8 multiplayer
        host_server,       // CREATE SERVER (arg = map id) on the relay address typed in the form
        host_local,        // HOST ON THIS PC (arg = map id): start the built-in relay, then create the server on it
        refresh_servers,   // REFRESH LIST / opening the server browser
        join_server,       // JOIN (arg = room code)
        cancel_connect,    // CANCEL while connecting
        leave_room,        // pause: leave the server (and return to the main menu)
        form_changed       // a multiplayer text field changed (save the settings)
    } kind;
    std::string arg;
};

struct MapEntry { std::string id, title, tag, description; };
struct LoadingState {
    std::string map_title;
    int phase{};                        // completed stages 0..7
    std::string info, error;
    bool ready{};
    double millis{};
};

// N8: what the multiplayer pages show (the shell owns the connection and the server list).
struct ServerRow { std::string id, title, host, map_title; int players{}, max_players{4}; bool open{}, joinable{}; };
struct MultiplayerView {
    std::string status;            // coopStatus: connection progress or the last error
    bool status_error{};
    std::string list_status;       // serverBrowserStatus
    bool listing{};                // a server list request is running
    std::vector<ServerRow> rooms;
    bool in_room{}, hosting{};
    std::string footer;            // pause footer, e.g. "CO-OP · HOSTING · 3/4"
};

struct MenuFrame {
    double time{};                      // seconds, for animation
    double scale{1};                    // UI pixels per CSS pixel
    Rect safe;                          // safe area (pixels)
    bool in_game{};                     // a session exists (pause routing)
    std::string current_map;            // map loaded in the shell
    std::string platform{"DESKTOP"};    // pause footer
    std::string status;                 // one-line status (audio / device notes)
    double pointer_x{-1}, pointer_y{-1};   // parallax and proximity glow
    bool touch{};
    std::vector<HudElement> hud;        // HUD editor: the elements as drawn this frame
    Region background, crest;           // atlas images (background grain / cat crest), empty when absent
    const MultiplayerView* mp{};        // N8 (nullptr: the multiplayer pages show "unavailable")
};

class Menu {
public:
    Menu(GameSettings& settings, std::vector<MapEntry> maps);
    Screen screen() const { return screen_; }
    bool active() const { return screen_ != Screen::none; }
    bool blocks_game() const { return screen_ != Screen::none; }   // the simulation does not run while any menu is open
    void show_home();
    void show_maps();
    void show_pause();
    void show_settings(bool from_pause);
    void show_hud_editor(bool from_pause);
    void show_loading(const LoadingState& s);
    void show_multiplayer() { go(Screen::multiplayer); }
    void show_servers() { go(Screen::servers); focus_id_ = "servers.refresh"; }
    void show_connecting() { go(Screen::connecting); }
    void show_host_maps() { host_mode_ = true; go(Screen::maps); focus_id_ = "host.local"; }
    bool host_mode() const { return host_mode_; }             // the maps page is the host's map choice
    bool text_focused() const;                                 // a text field has focus: typed characters go to it
    static constexpr std::size_t kNameMax = 20, kTitleMax = 32, kRelayMax = 200;   // the legacy input maxlength
    void update_loading(const LoadingState& s) { loading_ = s; }
    void close() { screen_ = Screen::none; dropdown_.clear(); }
    std::string selected_map() const { return selected_map_; }
    void select_map(const std::string& id) { selected_map_ = id; }
    std::vector<Command> handle(const MenuEvent& e);
    void build(Canvas& canvas, const MenuFrame& f);
    // Introspection for tests: the focusable items of the last build, and which one has focus.
    struct Item { std::string id; Rect rect; bool enabled{true}; int kind{}; };
    const std::vector<Item>& items() const { return items_; }
    std::string focused() const { return focus_ < items_.size() ? items_[focus_].id : std::string(); }
    int settings_tab() const { return static_cast<int>(tab_); }
    double scroll() const { return scroll_; }
    const std::string& open_dropdown() const { return dropdown_; }
    const std::string& hud_selected() const { return hud_selected_; }   // HUD editor selection (drawn highlighted)
private:
    enum ItemKind { item_button = 0, item_slider = 1, item_choice = 2, item_check = 3, item_tab = 4, item_option = 5, item_hud = 6, item_text = 7 };
    void go(Screen s);
    std::vector<Command> activate(const Item& item, double pointer_x, bool by_pointer);
    std::vector<Command> adjust(const Item& item, int direction);
    std::vector<Command> back();
    void move_focus(int delta);
    void ensure_visible();
    void add(const std::string& id, const Rect& r, int kind, bool enabled = true);
    // screens
    void home(Canvas& c, const MenuFrame& f);
    void maps(Canvas& c, const MenuFrame& f);
    void multiplayer(Canvas& c, const MenuFrame& f);
    void settings(Canvas& c, const MenuFrame& f);
    void exit_confirm(Canvas& c, const MenuFrame& f);
    void loading(Canvas& c, const MenuFrame& f);
    void pause(Canvas& c, const MenuFrame& f);
    void hud_editor(Canvas& c, const MenuFrame& f);
    void servers(Canvas& c, const MenuFrame& f);
    void connecting(Canvas& c, const MenuFrame& f);
    void text_field(Canvas& c, const MenuFrame& f, const std::string& id, const Rect& r, const std::string& label, const std::string& value, const std::string& placeholder);
    std::string* field_value(const std::string& id);
    void backdrop(Canvas& c, const MenuFrame& f, std::optional<Rect> hole = std::nullopt);
    bool button(Canvas& c, const MenuFrame& f, const std::string& id, const Rect& r, const std::string& label, const std::string& number = {}, bool primary = false, bool enabled = true, bool pointed = true);

    GameSettings* settings_;
    std::vector<MapEntry> maps_;
    Screen screen_{Screen::home}, before_settings_{Screen::home}, before_editor_{Screen::none};
    std::string selected_map_;
    bool host_mode_{};
    SettingsTab tab_{SettingsTab::view};
    double scroll_{}, content_height_{}, view_height_{};
    Rect list_view_;
    std::vector<Item> items_;
    std::size_t focus_{};
    std::string focus_id_;               // keeps focus across rebuilds
    std::string dropdown_;               // open select (control id)
    std::size_t dropdown_index_{};
    std::string dragging_;               // slider being dragged / HUD element being moved
    double drag_dx_{}, drag_dy_{};
    LoadingState loading_;
    std::string hud_selected_{"leftStats"};
    double last_w_{1280}, last_h_{720};
    bool pointer_down_{};
    std::string pressed_;                // item under the pointer when it went down (activates on release)
    double touch_scroll_y_{-1};
    bool scroll_armed_{};       // press on a list control: becomes a scroll drag after 8 px of travel (touch)
    double press_y_{};
};

}  // namespace cat::ui
